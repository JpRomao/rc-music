#pragma once

#include "WindowsDownloadProcess.hpp"
#include "WindowsDownloadTlsRuntime.hpp"

#include <array>
#include <memory>

namespace music_player::m0 {

enum class WindowsDownloadLaunchRole : std::size_t {
    Bootstrap,
    Payload,
    Core,
    Network,
    Msvcp140,
    Msvcp140_1,
    Vcruntime140,
    Vcruntime140_1,
    Plugin,
    Count
};
inline constexpr std::size_t download_launch_artifact_count =
    static_cast<std::size_t>(WindowsDownloadLaunchRole::Count);
struct WindowsDownloadLaunchSource final {
    QString path{};
    QByteArray sha256{};
};
struct WindowsDownloadNetworkLaunchConfig final {
    // Existing trusted local parent, never supplied by a page or a request.
    QString runtime_parent{};
    std::array<WindowsDownloadLaunchSource, download_launch_artifact_count> artifacts{};
};
enum class WindowsDownloadLaunchError { None, InvalidConfiguration, FileAccess, HashMismatch };

// Prepare only on a worker: authenticates sources, copies into a new private
// flat directory, authenticates and holds read-only destination handles. This
// is not a sandbox nor protection against a compromised same-user host/OS.
// The composition must retain this capability through native exit + pipe ACK.
class WindowsDownloadNetworkLaunch final {
  public:
    static std::shared_ptr<const WindowsDownloadNetworkLaunch> prepare(const WindowsDownloadNetworkLaunchConfig& config,
                                                                       WindowsDownloadLaunchError* error = nullptr);
    ~WindowsDownloadNetworkLaunch();
    WindowsDownloadNetworkLaunch(const WindowsDownloadNetworkLaunch&) = delete;
    WindowsDownloadNetworkLaunch& operator=(const WindowsDownloadNetworkLaunch&) = delete;
    [[nodiscard]] QString directory() const;
    [[nodiscard]] bool unchanged() const;
    [[nodiscard]] WindowsDownloadProcessConfig processConfig() const;
    [[nodiscard]] WindowsDownloadTlsRuntimeConfig tlsConfig() const;

  private:
    struct Impl;
    explicit WindowsDownloadNetworkLaunch(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
