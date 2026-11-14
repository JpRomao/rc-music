#include "WindowsYoutubeRuntime.hpp"
#include "WindowsDownloadArtifactPins.hpp"

#include <QStringList>
#include <QUuid>

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <utility>

namespace music_player::m0 {
namespace {
using Error = WindowsYoutubeRuntimeError;
using Role = WindowsYoutubeRuntimeRole;
using download_artifacts::Artifact;
using download_artifacts::Handle;

constexpr std::array<const char*, 4> role_paths{"extractor/yt-dlp.exe", "javascript/node.exe",
                                                "extractor/_internal/yt_dlp_ejs/yt/solver/core.min.js",
                                                "extractor/_internal/yt_dlp_ejs/yt/solver/lib.min.js"};

QString owned(const QString& value) { return QString(value.constData(), value.size()); }
QByteArray owned(const QByteArray& value) { return QByteArray(value.constData(), value.size()); }

std::optional<std::size_t> roleIndex(const Role role) {
    switch (role) {
    case Role::Extractor:
        return 0;
    case Role::Node:
        return 1;
    case Role::EjsCore:
        return 2;
    case Role::EjsLibrary:
        return 3;
    case Role::Support:
        return {};
    }
    return {};
}

bool asciiFilename(const QString& value) {
    return std::all_of(value.begin(), value.end(), [](const QChar unit) {
        return (unit >= u'a' && unit <= u'z') || (unit >= u'A' && unit <= u'Z') || (unit >= u'0' && unit <= u'9') ||
               unit == u'_' || unit == u'-' || unit == u'.';
    });
}

bool reservedFilename(const QString& value) {
    const auto base = value.section(u'.', 0, 0).toUpper();
    if (base == QLatin1String("CON") || base == QLatin1String("PRN") || base == QLatin1String("AUX") ||
        base == QLatin1String("NUL"))
        return true;
    return base.size() == 4 && (base.startsWith(QLatin1String("COM")) || base.startsWith(QLatin1String("LPT"))) &&
           base[3] >= u'1' && base[3] <= u'9';
}

Error relativePath(const QString& value) {
    if (value.isEmpty())
        return Error::InvalidConfiguration;
    if (value.size() > youtube_runtime_max_relative_units || value.count(u'/') + 1 > youtube_runtime_max_segments)
        return Error::LimitExceeded;
    const auto components = value.split(u'/');
    for (const auto& component : components) {
        if (component.isEmpty() || component.size() > 255 || component == QLatin1String(".") ||
            component == QLatin1String("..") || component.endsWith(u'.') || !asciiFilename(component) ||
            reservedFilename(component))
            return Error::InvalidConfiguration;
    }
    return Error::None;
}

bool correctRolePath(const Role role, const QString& relative) {
    if (const auto index = roleIndex(role))
        return relative == QLatin1String(role_paths[*index]);
    return role == Role::Support && (relative.startsWith(QLatin1String("extractor/_internal/")) ||
                                     relative.startsWith(QLatin1String("notices/")));
}

struct Ownership final {
    QString path{}, final_path{};
    FILE_ID_INFO identity{};
    bool known() const { return !path.isEmpty() && !final_path.isEmpty(); }
};

struct Source final {
    Role role{};
    QString relative{};
    Artifact artifact{};
};

struct Child final {
    QString name{};
    bool directory{};
};

struct Directory final {
    QString relative{};
    Ownership created{};
    Handle pin{};
    std::vector<Child> children{};
};

struct File final {
    Role role{};
    Artifact artifact{};
    // Reauthentication may overwrite Artifact's observed identity. Cleanup
    // remains bound to the object created by copyPinned, never a substitute.
    Ownership created{};
};

bool sameDirectory(const Directory& directory) {
    FILE_ID_INFO actual{};
    return directory.created.known() && directory.pin.valid() &&
           download_artifacts::information(directory.pin.get(), true, actual) &&
           download_artifacts::sameIdentity(actual, directory.created.identity) &&
           download_artifacts::finalPath(directory.pin.get()) == directory.created.final_path;
}

bool exactContents(const Directory& directory) {
    WIN32_FIND_DATAW entry{};
    const auto pattern = download_artifacts::nativePath(directory.created.path + QLatin1String("/*"));
    const HANDLE search = FindFirstFileW(pattern.c_str(), &entry);
    if (search == INVALID_HANDLE_VALUE)
        return false;
    struct SearchGuard final {
        HANDLE handle;
        ~SearchGuard() { FindClose(handle); }
    } guard{search};
    std::vector<bool> seen(directory.children.size(), false);
    std::size_t count{};
    do {
        const auto name = QString::fromWCharArray(entry.cFileName);
        if (name != QLatin1String(".") && name != QLatin1String("..")) {
            if (++count > directory.children.size() || !asciiFilename(name) ||
                (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                return false;
            const auto child =
                std::find_if(directory.children.begin(), directory.children.end(), [&name](const Child& expected) {
                    return name.compare(expected.name, Qt::CaseInsensitive) == 0;
                });
            if (child == directory.children.end() ||
                ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != child->directory)
                return false;
            const auto index = static_cast<std::size_t>(child - directory.children.begin());
            if (seen[index])
                return false;
            seen[index] = true;
        }
        if (!FindNextFileW(search, &entry))
            return GetLastError() == ERROR_NO_MORE_FILES && count == directory.children.size();
    } while (true);
}

Error artifactError(const download_artifacts::Error error) {
    return error == download_artifacts::Error::HashMismatch ? Error::HashMismatch : Error::FileAccess;
}
} // namespace

struct WindowsYoutubeRuntime::Impl final {
    QString directory{};
    std::vector<Directory> directories{};
    std::vector<File> files{};
    std::vector<Handle> parent_ancestors{};

    bool unchanged() const {
        return !directories.empty() && std::all_of(directories.begin(), directories.end(), [](const Directory& item) {
            return sameDirectory(item) && exactContents(item);
        }) && std::all_of(files.begin(), files.end(), [](const File& item) {
            return item.created.known() && download_artifacts::unchanged(item.artifact) &&
                   download_artifacts::sameIdentity(item.created.identity, item.artifact.identity) &&
                   item.created.final_path == item.artifact.final_path;
        });
    }

    ~Impl() {
        // Release file pins and retain every directory and parent pin during
        // file removal, so cleanup cannot traverse a substituted ancestor.
        for (auto& file : files)
            file.artifact.handle = Handle{};
        try {
            if (directories.empty() || !directories.front().created.known())
                return;
            for (const auto& item : directories)
                if (item.created.known() && !sameDirectory(item))
                    return;
            const auto& root = directories.front().created;
            Handle check(CreateFileW(download_artifacts::nativePath(root.path).c_str(),
                                     FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY, FILE_SHARE_READ, nullptr,
                                     OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                                     nullptr));
            FILE_ID_INFO actual{};
            if (!check.valid() || !download_artifacts::information(check.get(), true, actual) ||
                !download_artifacts::sameIdentity(actual, root.identity) ||
                download_artifacts::finalPath(check.get()) != root.final_path)
                return;
            for (auto file = files.rbegin(); file != files.rend(); ++file) {
                const auto& created = file->created;
                if (created.known())
                    static_cast<void>(
                        download_artifacts::removeOwned(created.path, false, created.identity, created.final_path));
            }
            check = Handle{};
            for (auto item = directories.rbegin(); item != directories.rend(); ++item) {
                const auto& created = item->created;
                // Only this leaf's pin is released. All of its ancestors
                // remain pinned until their own later cleanup iteration.
                item->pin = Handle{};
                if (created.known())
                    static_cast<void>(
                        download_artifacts::removeOwned(created.path, true, created.identity, created.final_path));
            }
        } catch (...) {
            // An uncertain identity/allocation leaves residuals for review.
        }
    }
};

WindowsYoutubeRuntime::WindowsYoutubeRuntime(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
WindowsYoutubeRuntime::~WindowsYoutubeRuntime() = default;

std::shared_ptr<const WindowsYoutubeRuntime> WindowsYoutubeRuntime::prepare(const WindowsYoutubeRuntimeConfig& config,
                                                                            WindowsYoutubeRuntimeError* error) {
    const auto fail = [error](const Error cause) -> std::shared_ptr<const WindowsYoutubeRuntime> {
        if (error)
            *error = cause;
        return {};
    };
    try {
        if (config.artifacts.size() > youtube_runtime_max_files)
            return fail(Error::LimitExceeded);
        if (config.artifacts.empty() || config.runtime_parent.isEmpty() || config.runtime_parent.size() > 30'000)
            return fail(Error::InvalidConfiguration);
        QString parent;
        if (!download_artifacts::validPath(owned(config.runtime_parent), parent))
            return fail(Error::InvalidConfiguration);
        std::array<bool, role_paths.size()> roles{};
        std::vector<QString> file_keys;
        std::vector<Source> sources;
        file_keys.reserve(config.artifacts.size());
        sources.reserve(config.artifacts.size());
        for (const auto& supplied : config.artifacts) {
            const auto relative_error = relativePath(supplied.relative_path);
            if (relative_error != Error::None)
                return fail(relative_error);
            if (supplied.source_path.size() > 30'000 || supplied.sha256.size() != 64)
                return fail(Error::InvalidConfiguration);
            Source source;
            source.role = supplied.role;
            source.relative = owned(supplied.relative_path);
            source.artifact.sha256 = owned(supplied.sha256);
            if (!correctRolePath(source.role, source.relative) ||
                !download_artifacts::validPin(source.artifact.sha256) ||
                !download_artifacts::validPath(owned(supplied.source_path), source.artifact.path))
                return fail(Error::InvalidConfiguration);
            if (const auto index = roleIndex(source.role)) {
                if (roles[*index])
                    return fail(Error::InvalidConfiguration);
                roles[*index] = true;
            }
            const auto key = source.relative.toLower();
            for (const auto& previous : file_keys) {
                if (previous == key || previous.startsWith(key + u'/') || key.startsWith(previous + u'/'))
                    return fail(Error::InvalidConfiguration);
            }
            file_keys.push_back(key);
            sources.push_back(std::move(source));
        }
        if (!std::all_of(roles.begin(), roles.end(), [](const bool present) { return present; }))
            return fail(Error::InvalidConfiguration);

        auto impl = std::make_unique<Impl>();
        impl->directory =
            parent + QLatin1String("/rcmusic-youtube-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        // Derive the complete directory/child inventory from validated entries.
        // ASCII case aliases and file/directory conflicts were already refused.
        std::map<QString, std::vector<Child>> contents;
        std::map<QString, QString> directory_spellings;
        contents.try_emplace(QString{});
        for (const auto& source : sources) {
            QString full;
            if (!download_artifacts::validPath(impl->directory + u'/' + source.relative, full))
                return fail(Error::InvalidConfiguration);
            File destination;
            destination.role = source.role;
            destination.artifact.path = std::move(full);
            destination.artifact.sha256 = source.artifact.sha256;
            impl->files.push_back(std::move(destination));
            const auto components = source.relative.split(u'/');
            QString directory;
            for (qsizetype index = 0; index < components.size(); ++index) {
                const bool child_directory = index + 1 < components.size();
                auto& children = contents[directory];
                const auto& name = components[index];
                const auto existing = std::find_if(children.begin(), children.end(), [&name](const Child& item) {
                    return item.name.compare(name, Qt::CaseInsensitive) == 0;
                });
                if (existing == children.end())
                    children.push_back({owned(name), child_directory});
                else if (existing->directory != child_directory)
                    return fail(Error::InvalidConfiguration);
                if (child_directory) {
                    directory = directory.isEmpty() ? name : directory + u'/' + name;
                    const auto [spelling, inserted] = directory_spellings.try_emplace(directory.toLower(), directory);
                    if (!inserted && spelling->second != directory)
                        return fail(Error::InvalidConfiguration);
                    contents.try_emplace(directory);
                }
            }
        }
        impl->directories.reserve(contents.size());
        for (const auto& [relative, children] : contents) {
            Directory item;
            item.relative = relative;
            item.created.path = relative.isEmpty() ? impl->directory : impl->directory + u'/' + relative;
            item.children = children;
            impl->directories.push_back(std::move(item));
        }
        // Lexical order places every directory before its descendants.
        // Reverse iteration is therefore also a safe cleanup depth order.
        std::vector<Handle> source_ancestors;
        quint64 total_bytes{};
        for (auto& source : sources) {
            auto& artifact = source.artifact;
            if (!download_artifacts::pinAncestors(artifact.path, source_ancestors))
                return fail(Error::FileAccess);
            artifact.handle = Handle(CreateFileW(download_artifacts::nativePath(artifact.path).c_str(), GENERIC_READ,
                                                 FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                                 FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
            LARGE_INTEGER bytes{};
            if (!artifact.handle.valid() ||
                !download_artifacts::information(artifact.handle.get(), false, artifact.identity) ||
                !GetFileSizeEx(artifact.handle.get(), &bytes) || bytes.QuadPart <= 0)
                return fail(Error::FileAccess);
            artifact.bytes = static_cast<quint64>(bytes.QuadPart);
            if (artifact.bytes > download_artifacts::max_artifact_bytes ||
                artifact.bytes > youtube_runtime_max_bytes - total_bytes)
                return fail(Error::LimitExceeded);
            total_bytes += artifact.bytes;
            artifact.final_path = download_artifacts::finalPath(artifact.handle.get());
            if (artifact.final_path.isEmpty())
                return fail(Error::FileAccess);
        }
        // Check every quota first, then authenticate every source before
        // creating the private tree. Both steps use the same read-only pins.
        for (const auto& source : sources) {
            const auto result = download_artifacts::hashPinned(source.artifact);
            if (result != download_artifacts::Error::None)
                return fail(artifactError(result));
        }
        if (!download_artifacts::pinAncestors(parent + QLatin1String("/placeholder"), impl->parent_ancestors))
            return fail(Error::FileAccess);
        for (auto& item : impl->directories) {
            if (!download_artifacts::createPrivateDirectory(item.created.path))
                return fail(Error::FileAccess);
            item.pin =
                Handle(CreateFileW(download_artifacts::nativePath(item.created.path).c_str(),
                                   FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                   FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
            if (!item.pin.valid() || !download_artifacts::information(item.pin.get(), true, item.created.identity))
                return fail(Error::FileAccess);
            item.created.final_path = download_artifacts::finalPath(item.pin.get());
            if (item.created.final_path.isEmpty())
                return fail(Error::FileAccess);
        }
        for (std::size_t index = 0; index < sources.size(); ++index) {
            auto& destination = impl->files[index];
            const bool copied = download_artifacts::copyPinnedTemporary(sources[index].artifact, destination.artifact);
            if (!destination.artifact.final_path.isEmpty()) {
                destination.created.path = destination.artifact.path;
                destination.created.identity = destination.artifact.identity;
                destination.created.final_path = destination.artifact.final_path;
            }
            if (!copied)
                return fail(Error::FileAccess);
            // The complete directory inventory is derived from these destinations.
            // Every lexical ancestor is already pinned by that inventory or parent_ancestors.
            const auto result = download_artifacts::pinAndHashUnderPinnedAncestors(destination.artifact);
            if (result != download_artifacts::Error::None)
                return fail(artifactError(result));
            if (!download_artifacts::sameIdentity(destination.created.identity, destination.artifact.identity) ||
                destination.created.final_path != destination.artifact.final_path)
                return fail(Error::FileAccess);
        }
        if (!impl->unchanged())
            return fail(Error::FileAccess);
        auto lease = std::shared_ptr<const WindowsYoutubeRuntime>(new WindowsYoutubeRuntime(std::move(impl)));
        if (error)
            *error = Error::None;
        return lease;
    } catch (...) {
        return fail(Error::FileAccess);
    }
}

QString WindowsYoutubeRuntime::directory() const { return impl_->directory; }
QString WindowsYoutubeRuntime::artifactPath(const WindowsYoutubeRuntimeRole role) const {
    if (!roleIndex(role))
        return {};
    const auto found =
        std::find_if(impl_->files.begin(), impl_->files.end(), [role](const File& item) { return item.role == role; });
    return found == impl_->files.end() ? QString{} : found->artifact.path;
}
bool WindowsYoutubeRuntime::unchanged() const {
    try {
        return impl_->unchanged();
    } catch (...) {
        return false;
    }
}
} // namespace music_player::m0
