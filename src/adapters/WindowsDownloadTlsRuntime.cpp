#include "adapters/WindowsDownloadTlsRuntime.hpp"
#include "adapters/WindowsDownloadArtifactPins.hpp"

#include <QAbstractSocket>
#include <QCoreApplication>
#include <QLibrary>
#include <QPluginLoader>
#include <QSslSocket>

#include <array>
#include <cstring>
#include <vector>

namespace music_player::m0 {
namespace {
using download_artifacts::Artifact;
using download_artifacts::Handle;
using download_artifacts::moduleMatches;
using download_artifacts::pinAndHash;
using download_artifacts::validPath;
using download_artifacts::validPin;

static_assert(download_tls_max_artifact_bytes == download_artifacts::max_artifact_bytes);
constexpr auto backendName = "rcmusic-schannel-offline";
constexpr auto requiredQtVersion = "6.11.2";
} // namespace

struct WindowsDownloadTlsRuntime::Impl final {
    WindowsDownloadTlsRuntimeError error{WindowsDownloadTlsRuntimeError::None};
    bool attempted{false};
    std::vector<Handle> ancestors{};
    std::array<Artifact, 3> artifacts{};
    QPluginLoader loader{};
};

WindowsDownloadTlsRuntime::WindowsDownloadTlsRuntime() : impl_(std::make_unique<Impl>()) {}
WindowsDownloadTlsRuntime::~WindowsDownloadTlsRuntime() = default;

bool WindowsDownloadTlsRuntime::activate(const WindowsDownloadTlsRuntimeConfig& config) {
    if (impl_->attempted) {
        impl_->error = WindowsDownloadTlsRuntimeError::AlreadyAttempted;
        return false;
    }
    impl_->attempted = true;
    if (!QCoreApplication::instance()) {
        impl_->error = WindowsDownloadTlsRuntimeError::MissingApplication;
        return false;
    }
    QCoreApplication::setLibraryPaths({});
    const auto fail = [this](WindowsDownloadTlsRuntimeError error) {
        impl_->error = error;
        return false;
    };
    try {
        auto& artifacts = impl_->artifacts;
        if (!validPath(config.core_path, artifacts[0].path) || !validPin(config.core_sha256) ||
            !validPath(config.network_path, artifacts[1].path) || !validPin(config.network_sha256) ||
            !validPath(config.plugin_path, artifacts[2].path) || !validPin(config.plugin_sha256))
            return fail(WindowsDownloadTlsRuntimeError::InvalidConfiguration);
        artifacts[0].sha256 = config.core_sha256;
        artifacts[1].sha256 = config.network_sha256;
        artifacts[2].sha256 = config.plugin_sha256;
        for (auto& artifact : artifacts) {
            const auto error = pinAndHash(artifact, impl_->ancestors);
            if (error != download_artifacts::Error::None)
                return fail(error == download_artifacts::Error::HashMismatch
                                ? WindowsDownloadTlsRuntimeError::HashMismatch
                                : WindowsDownloadTlsRuntimeError::FileAccess);
        }
        if (std::strcmp(QT_VERSION_STR, requiredQtVersion) != 0 || std::strcmp(qVersion(), requiredQtVersion) != 0 ||
            !moduleMatches(reinterpret_cast<const void*>(&qVersion), artifacts[0]) ||
            !moduleMatches(&QAbstractSocket::staticMetaObject, artifacts[1]))
            return fail(WindowsDownloadTlsRuntimeError::RuntimeMismatch);
        if (!QCoreApplication::libraryPaths().isEmpty())
            return fail(WindowsDownloadTlsRuntimeError::Activation);
        // Clearing search paths does not unload registered Qt backend instances.
        // Check BEFORE manual load, even if the existing name is our own name.
        if (!QSslSocket::availableBackends().isEmpty())
            return fail(WindowsDownloadTlsRuntimeError::PreexistingBackend);
        impl_->loader.setLoadHints(QLibrary::PreventUnloadHint);
        impl_->loader.setFileName(artifacts[2].path);
        const auto* instance = impl_->loader.instance();
        if (!instance)
            return fail(WindowsDownloadTlsRuntimeError::PluginLoad);
        if (!moduleMatches(instance->metaObject(), artifacts[2]))
            return fail(WindowsDownloadTlsRuntimeError::RuntimeMismatch);
        const auto expected_backend = QString::fromLatin1(backendName);
        if (QSslSocket::availableBackends() != QStringList{expected_backend})
            return fail(WindowsDownloadTlsRuntimeError::UnexpectedBackend);
        if (!QCoreApplication::libraryPaths().isEmpty() || !QSslSocket::setActiveBackend(expected_backend) ||
            QSslSocket::activeBackend() != expected_backend || !QSslSocket::supportsSsl())
            return fail(WindowsDownloadTlsRuntimeError::Activation);
        impl_->error = WindowsDownloadTlsRuntimeError::None;
        return true;
    } catch (...) {
        // No free diagnostics or fallback. The caller must stop this worker.
        return fail(WindowsDownloadTlsRuntimeError::FileAccess);
    }
}

WindowsDownloadTlsRuntimeError WindowsDownloadTlsRuntime::error() const noexcept { return impl_->error; }

} // namespace music_player::m0
