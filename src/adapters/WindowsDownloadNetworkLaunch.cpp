#include "WindowsDownloadNetworkLaunch.hpp"
#include "WindowsDownloadArtifactPins.hpp"

#include <QDir>
#include <QUuid>

#include <algorithm>
#include <vector>

namespace music_player::m0 {
namespace {
using namespace download_artifacts;
constexpr std::array<const char*, download_launch_artifact_count> leaves{"RCMusicDownloadWorker.exe",
                                                                         "rcmusic-download-network.dll",
                                                                         "Qt6Core.dll",
                                                                         "Qt6Network.dll",
                                                                         "msvcp140.dll",
                                                                         "msvcp140_1.dll",
                                                                         "vcruntime140.dll",
                                                                         "vcruntime140_1.dll",
                                                                         "qrcmusicschanneloffline.dll"};

bool exactContents(const QString& directory) {
    WIN32_FIND_DATAW entry{};
    const HANDLE search = FindFirstFileW(nativePath(directory + "/*").c_str(), &entry);
    if (search == INVALID_HANDLE_VALUE)
        return false;
    struct SearchGuard final {
        HANDLE handle;
        ~SearchGuard() { FindClose(handle); }
    } guard{search};
    std::array<bool, download_launch_artifact_count> seen{};
    bool valid = true;
    DWORD last = ERROR_SUCCESS;
    std::size_t count = 0;
    do {
        const auto name = QString::fromWCharArray(entry.cFileName);
        if (name != "." && name != "..") {
            ++count;
            const auto found = std::find_if(leaves.begin(), leaves.end(), [&name](const char* leaf) {
                return name.compare(QString::fromLatin1(leaf), Qt::CaseInsensitive) == 0;
            });
            if (count > leaves.size() || found == leaves.end() ||
                (entry.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
                valid = false;
                break;
            }
            const auto index = static_cast<std::size_t>(found - leaves.begin());
            if (seen[index]) {
                valid = false;
                break;
            }
            seen[index] = true;
        }
        if (!FindNextFileW(search, &entry)) {
            last = GetLastError();
            break;
        }
    } while (true);
    return valid && last == ERROR_NO_MORE_FILES && count == leaves.size();
}

} // namespace

struct WindowsDownloadNetworkLaunch::Impl final {
    QString directory{};
    Handle directory_pin{};
    FILE_ID_INFO directory_identity{};
    QString directory_final{};
    std::vector<Handle> ancestors{};
    std::array<Artifact, download_launch_artifact_count> artifacts{};
    QString system_root{};
    ~Impl() {
        // Drop only our pins, then delete only recorded regular-file identities.
        // No recursive removal, no traversal of unknown names or reparse points.
        // A tampered/nonempty directory is intentionally left behind.
        for (auto& artifact : artifacts)
            artifact.handle = Handle{};
        ancestors.clear();
        directory_pin = Handle{};
        try {
            if (directory_final.isEmpty())
                return;
            Handle check(CreateFileW(nativePath(directory).c_str(), FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY,
                                     FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                     FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
            FILE_ID_INFO identity{};
            if (!check.valid() || !information(check.get(), true, identity) ||
                !sameIdentity(identity, directory_identity) || finalPath(check.get()) != directory_final)
                return;
            for (const auto& artifact : artifacts) {
                if (!artifact.final_path.isEmpty())
                    static_cast<void>(removeOwned(artifact.path, false, artifact.identity, artifact.final_path));
            }
            check = Handle{};
            static_cast<void>(removeOwned(directory, true, directory_identity, directory_final));
        } catch (...) {
            // Allocation/inspection failure cannot escape a destructor. Keep
            // residuals; never fall back to recursive or path-only deletion.
        }
    }
};
WindowsDownloadNetworkLaunch::WindowsDownloadNetworkLaunch(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
WindowsDownloadNetworkLaunch::~WindowsDownloadNetworkLaunch() = default;
std::shared_ptr<const WindowsDownloadNetworkLaunch>
WindowsDownloadNetworkLaunch::prepare(const WindowsDownloadNetworkLaunchConfig& config,
                                      WindowsDownloadLaunchError* error) {
    const auto fail = [error](WindowsDownloadLaunchError cause) -> std::shared_ptr<const WindowsDownloadNetworkLaunch> {
        if (error)
            *error = cause;
        return {};
    };
    try {
        auto impl = std::make_unique<Impl>();
        QString parent;
        if (!validPath(config.runtime_parent, parent))
            return fail(WindowsDownloadLaunchError::InvalidConfiguration);
        constexpr qsizetype directory_suffix_units = 1 + 16 + 36; // slash, fixed prefix, UUID
        const auto largest_leaf = *std::max_element(leaves.begin(), leaves.end(), [](const char* a, const char* b) {
            return std::char_traits<char>::length(a) < std::char_traits<char>::length(b);
        });
        const auto tail_units =
            directory_suffix_units + 1 + static_cast<qsizetype>(std::char_traits<char>::length(largest_leaf));
        if (parent.size() > download_process_max_path_units - tail_units)
            return fail(WindowsDownloadLaunchError::InvalidConfiguration);
        std::array<Artifact, download_launch_artifact_count> sources{};
        for (std::size_t index = 0; index < sources.size(); ++index) {
            auto& source = sources[index];
            if (!validPath(config.artifacts[index].path, source.path) || !validPin(config.artifacts[index].sha256))
                return fail(WindowsDownloadLaunchError::InvalidConfiguration);
            source.sha256 = config.artifacts[index].sha256;
        }
        std::vector<Handle> source_ancestors;
        for (auto& source : sources) {
            const auto result = pinAndHash(source, source_ancestors);
            if (result != Error::None)
                return fail(result == Error::HashMismatch ? WindowsDownloadLaunchError::HashMismatch
                                                          : WindowsDownloadLaunchError::FileAccess);
        }
        // Pin the existing parent and all ancestors before creating a leaf.
        if (!pinAncestors(parent + "/placeholder", impl->ancestors))
            return fail(WindowsDownloadLaunchError::FileAccess);
        std::array<wchar_t, 32768> system{};
        const UINT count = GetWindowsDirectoryW(system.data(), static_cast<UINT>(system.size()));
        if (count == 0 || count >= system.size() ||
            !validPath(QString::fromWCharArray(system.data(), count), impl->system_root))
            return fail(WindowsDownloadLaunchError::FileAccess);
        impl->directory = parent + "/rcmusic-network-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!createPrivateDirectory(impl->directory))
            return fail(WindowsDownloadLaunchError::FileAccess);
        impl->directory_pin = Handle(CreateFileW(
            nativePath(impl->directory).c_str(), FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        if (!impl->directory_pin.valid() || !information(impl->directory_pin.get(), true, impl->directory_identity))
            return fail(WindowsDownloadLaunchError::FileAccess);
        impl->directory_final = finalPath(impl->directory_pin.get());
        if (impl->directory_final.isEmpty())
            return fail(WindowsDownloadLaunchError::FileAccess);
        for (std::size_t index = 0; index < sources.size(); ++index) {
            auto& artifact = impl->artifacts[index];
            artifact.path = impl->directory + '/' + QString::fromLatin1(leaves[index]);
            artifact.sha256 = sources[index].sha256;
            if (!copyPinned(sources[index], artifact))
                return fail(WindowsDownloadLaunchError::FileAccess);
            const auto identity = artifact.identity;
            const auto final_path = artifact.final_path;
            const auto result = pinAndHash(artifact, impl->ancestors);
            if (result != Error::None || !sameIdentity(identity, artifact.identity) ||
                final_path != artifact.final_path)
                return fail(result == Error::HashMismatch ? WindowsDownloadLaunchError::HashMismatch
                                                          : WindowsDownloadLaunchError::FileAccess);
        }
        if (!exactContents(impl->directory))
            return fail(WindowsDownloadLaunchError::FileAccess);
        auto lease =
            std::shared_ptr<const WindowsDownloadNetworkLaunch>(new WindowsDownloadNetworkLaunch(std::move(impl)));
        if (error)
            *error = WindowsDownloadLaunchError::None;
        return lease;
    } catch (...) {
        return fail(WindowsDownloadLaunchError::FileAccess);
    }
}
QString WindowsDownloadNetworkLaunch::directory() const { return impl_->directory; }
bool WindowsDownloadNetworkLaunch::unchanged() const {
    FILE_ID_INFO identity{};
    return information(impl_->directory_pin.get(), true, identity) &&
           sameIdentity(identity, impl_->directory_identity) &&
           finalPath(impl_->directory_pin.get()) == impl_->directory_final && exactContents(impl_->directory) &&
           std::all_of(impl_->artifacts.begin(), impl_->artifacts.end(), download_artifacts::unchanged);
}
WindowsDownloadProcessConfig WindowsDownloadNetworkLaunch::processConfig() const {
    WindowsDownloadProcessConfig config;
    config.executable = impl_->artifacts[0].path;
    config.working_directory = impl_->directory;
    QProcessEnvironment environment;
    environment.insert("PATH", "");
    environment.insert("SystemRoot", impl_->system_root);
    config.environment = environment;
    return config;
}
WindowsDownloadTlsRuntimeConfig WindowsDownloadNetworkLaunch::tlsConfig() const {
    const auto& core = impl_->artifacts[2];
    const auto& network = impl_->artifacts[3];
    const auto& plugin = impl_->artifacts[8];
    return {core.path, core.sha256, network.path, network.sha256, plugin.path, plugin.sha256};
}
} // namespace music_player::m0
