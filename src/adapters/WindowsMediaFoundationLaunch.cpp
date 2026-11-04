#include "WindowsMediaFoundationLaunch.hpp"
#include "WindowsDownloadAppContainer.hpp"
#include "WindowsDownloadArtifactPins.hpp"

#include <QScopeGuard>
#include <QUuid>

#include <ShlObj.h>

#include <array>

namespace music_player::m0 {
namespace {
using namespace download_artifacts;
const QString worker_leaf = QStringLiteral("RCMusicMediaWorker.exe");

bool exactContents(const QString& directory) {
    WIN32_FIND_DATAW entry{};
    const HANDLE search = FindFirstFileW(nativePath(directory + "/*").c_str(), &entry);
    if (search == INVALID_HANDLE_VALUE)
        return false;
    struct Guard final {
        HANDLE handle;
        ~Guard() { FindClose(handle); }
    } guard{search};
    std::size_t count = 0;
    DWORD last = ERROR_SUCCESS;
    do {
        const auto name = QString::fromWCharArray(entry.cFileName);
        if (name != "." && name != "..") {
            ++count;
            if (count > 1 || name.compare(worker_leaf, Qt::CaseInsensitive) != 0 ||
                (entry.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
                return false;
        }
        if (!FindNextFileW(search, &entry)) {
            last = GetLastError();
            break;
        }
    } while (true);
    return last == ERROR_NO_MORE_FILES && count == 1;
}
} // namespace

struct WindowsMediaFoundationLaunch::Impl final {
    QString directory{};
    Handle directory_pin{};
    FILE_ID_INFO directory_identity{};
    QString directory_final{};
    std::vector<Handle> ancestors{};
    Artifact worker{};
    QString system_root{};
    QString local_app_data{};
    std::shared_ptr<const WindowsDownloadAppContainer> app_container{};
    ~Impl() {
        worker.handle = Handle{};
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
            if (!worker.final_path.isEmpty())
                static_cast<void>(removeOwned(worker.path, false, worker.identity, worker.final_path));
            check = Handle{};
            static_cast<void>(removeOwned(directory, true, directory_identity, directory_final));
        } catch (...) {
            // Preserve uncertain residuals; never follow an unknown entry or
            // substitute recursive/path-only deletion for the recorded identity.
        }
    }
};
WindowsMediaFoundationLaunch::WindowsMediaFoundationLaunch(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
WindowsMediaFoundationLaunch::~WindowsMediaFoundationLaunch() = default;
std::shared_ptr<const WindowsMediaFoundationLaunch>
WindowsMediaFoundationLaunch::prepare(const WindowsMediaFoundationLaunchConfig& config,
                                      WindowsMediaFoundationLaunchError* error) {
    const auto fail =
        [error](const WindowsMediaFoundationLaunchError cause) -> std::shared_ptr<const WindowsMediaFoundationLaunch> {
        if (error)
            *error = cause;
        return {};
    };
    try {
        QString parent;
        Artifact source;
        if ((config.require_app_container && !config.app_container) || !validPath(config.runtime_parent, parent) ||
            !validPath(config.source_executable, source.path) || !validPin(config.source_sha256))
            return fail(WindowsMediaFoundationLaunchError::InvalidConfiguration);
        const auto prefix = QStringLiteral("rcmusic-media-");
        const auto tail = 1 + prefix.size() + 36 + 1 + worker_leaf.size();
        if (parent.size() > download_process_max_path_units - tail)
            return fail(WindowsMediaFoundationLaunchError::InvalidConfiguration);
        source.sha256 = config.source_sha256;
        std::vector<Handle> source_ancestors;
        const auto authenticated = pinAndHash(source, source_ancestors);
        if (authenticated != Error::None)
            return fail(authenticated == Error::HashMismatch ? WindowsMediaFoundationLaunchError::HashMismatch
                                                             : WindowsMediaFoundationLaunchError::FileAccess);
        auto impl = std::make_unique<Impl>();
        impl->app_container = config.app_container;
        if (!pinAncestors(parent + "/placeholder", impl->ancestors))
            return fail(WindowsMediaFoundationLaunchError::FileAccess);
        std::array<wchar_t, 32'768> system{};
        const auto count = GetWindowsDirectoryW(system.data(), static_cast<UINT>(system.size()));
        if (!count || count >= system.size() ||
            !validPath(QString::fromWCharArray(system.data(), count), impl->system_root))
            return fail(WindowsMediaFoundationLaunchError::FileAccess);
        if (impl->app_container) {
            // Windows needs the current-user base for AppContainer redirection.
            // Retrieve it without inheriting parent values or creating a folder.
            PWSTR local_app_data = nullptr;
            const auto release = qScopeGuard([&] { CoTaskMemFree(local_app_data); });
            if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY, nullptr, &local_app_data)) ||
                !local_app_data || !validPath(QString::fromWCharArray(local_app_data), impl->local_app_data))
                return fail(WindowsMediaFoundationLaunchError::FileAccess);
        }
        impl->directory = parent + '/' + prefix + QUuid::createUuid().toString(QUuid::WithoutBraces);
        const bool created = impl->app_container
                                 ? createPrivateDirectory(impl->directory, impl->app_container->sidBytes())
                                 : createPrivateDirectory(impl->directory);
        if (!created)
            return fail(WindowsMediaFoundationLaunchError::FileAccess);
        impl->directory_pin = Handle(CreateFileW(
            nativePath(impl->directory).c_str(),
            FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY | (impl->app_container ? READ_CONTROL : 0), FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        if (!impl->directory_pin.valid() || !information(impl->directory_pin.get(), true, impl->directory_identity))
            return fail(WindowsMediaFoundationLaunchError::FileAccess);
        impl->directory_final = finalPath(impl->directory_pin.get());
        if (impl->directory_final.isEmpty() ||
            (impl->app_container && !runtimeDaclMatches(impl->directory_pin.get(), impl->app_container->sidBytes())))
            return fail(WindowsMediaFoundationLaunchError::FileAccess);
        impl->worker.path = impl->directory + '/' + worker_leaf;
        impl->worker.sha256 = source.sha256;
        const bool copied = impl->app_container ? copyPinned(source, impl->worker, impl->app_container->sidBytes())
                                                : copyPinned(source, impl->worker);
        if (!copied)
            return fail(WindowsMediaFoundationLaunchError::FileAccess);
        const auto identity = impl->worker.identity;
        const auto final_path = impl->worker.final_path;
        const auto pinned = pinAndHash(impl->worker, impl->ancestors);
        if (pinned != Error::None || !sameIdentity(identity, impl->worker.identity) ||
            final_path != impl->worker.final_path || !exactContents(impl->directory) ||
            (impl->app_container && (!runtimeDaclMatches(impl->directory_pin.get(), impl->app_container->sidBytes()) ||
                                     !runtimeDaclMatches(impl->worker.handle.get(), impl->app_container->sidBytes()))))
            return fail(pinned == Error::HashMismatch ? WindowsMediaFoundationLaunchError::HashMismatch
                                                      : WindowsMediaFoundationLaunchError::FileAccess);
        auto lease =
            std::shared_ptr<const WindowsMediaFoundationLaunch>(new WindowsMediaFoundationLaunch(std::move(impl)));
        if (error)
            *error = WindowsMediaFoundationLaunchError::None;
        return lease;
    } catch (...) {
        return fail(WindowsMediaFoundationLaunchError::FileAccess);
    }
}
QString WindowsMediaFoundationLaunch::directory() const { return impl_->directory; }
bool WindowsMediaFoundationLaunch::unchanged() const {
    FILE_ID_INFO identity{};
    return information(impl_->directory_pin.get(), true, identity) &&
           sameIdentity(identity, impl_->directory_identity) &&
           finalPath(impl_->directory_pin.get()) == impl_->directory_final && exactContents(impl_->directory) &&
           download_artifacts::unchanged(impl_->worker) &&
           (!impl_->app_container ||
            (runtimeDaclMatches(impl_->directory_pin.get(), impl_->app_container->sidBytes()) &&
             runtimeDaclMatches(impl_->worker.handle.get(), impl_->app_container->sidBytes())));
}
WindowsDownloadProcessConfig WindowsMediaFoundationLaunch::processConfig() const {
    WindowsDownloadProcessConfig config;
    config.executable = impl_->worker.path;
    config.working_directory = impl_->directory;
    QProcessEnvironment environment;
    environment.insert("PATH", "");
    environment.insert("SystemRoot", impl_->system_root);
    if (impl_->app_container)
        environment.insert("LOCALAPPDATA", impl_->local_app_data);
    config.environment = environment;
    config.app_container = impl_->app_container;
    config.require_app_container = impl_->app_container != nullptr;
    return config;
}
} // namespace music_player::m0
