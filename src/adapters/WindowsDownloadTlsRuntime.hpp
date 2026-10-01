#pragma once

#include <QByteArray>
#include <QString>

#include <memory>

namespace music_player::m0 {

inline constexpr quint64 download_tls_max_artifact_bytes = 128ULL * 1024 * 1024;

// Trusted composition only. Pins identify this reviewed Qt/plugin build, never
// values supplied by a page, extension, environment variable or media request.
// Each SHA256 is exactly 64 lowercase ASCII hex characters.
struct WindowsDownloadTlsRuntimeConfig final {
    QString core_path{};
    QByteArray core_sha256{};
    QString network_path{};
    QByteArray network_sha256{};
    QString plugin_path{};
    QByteArray plugin_sha256{};
};

enum class WindowsDownloadTlsRuntimeError {
    None,
    InvalidConfiguration,
    MissingApplication,
    FileAccess,
    HashMismatch,
    RuntimeMismatch,
    PreexistingBackend,
    PluginLoad,
    UnexpectedBackend,
    Activation,
    AlreadyAttempted
};

// New DNS/TLS worker process only, after QCoreApplication construction and before
// any TLS use. Never select a backend in the player. Activation is one-shot and
// fail-closed: failure requires terminating the worker, not retry/fallback.
// Successful lifetime retains read-only, share-read-only artifact/ancestor
// handles and the absolute QPluginLoader. This does not control DLL loading
// before main, sandbox Windows, or approve deployment/licensing/network gates.
class WindowsDownloadTlsRuntime final {
  public:
    WindowsDownloadTlsRuntime();
    ~WindowsDownloadTlsRuntime();

    [[nodiscard]] bool activate(const WindowsDownloadTlsRuntimeConfig& config);
    [[nodiscard]] WindowsDownloadTlsRuntimeError error() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace music_player::m0
