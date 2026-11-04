#pragma once

#include "WindowsDownloadProcess.hpp"

#include <memory>

namespace music_player::m0 {
class WindowsDownloadAppContainer;
struct WindowsMediaFoundationLaunchConfig final {
    QString runtime_parent{};
    QString source_executable{};
    QByteArray source_sha256{};
    // Trusted, already provisioned ordinary AppContainer profile SID. The
    // capability validates SID shape, not profile existence. No capabilities.
    std::shared_ptr<const WindowsDownloadAppContainer> app_container{};
    bool require_app_container{false};
};
enum class WindowsMediaFoundationLaunchError { None, InvalidConfiguration, FileAccess, HashMismatch };

// Worker-only preparation of a reviewed, pinned, single-executable profile.
// Hashes establish consistency with trusted composition, not publisher trust
// or a sandbox. Keep the lease until process/pipes stop and owners are destroyed.
class WindowsMediaFoundationLaunch final {
  public:
    static std::shared_ptr<const WindowsMediaFoundationLaunch>
    prepare(const WindowsMediaFoundationLaunchConfig& config, WindowsMediaFoundationLaunchError* error = nullptr);
    ~WindowsMediaFoundationLaunch();
    WindowsMediaFoundationLaunch(const WindowsMediaFoundationLaunch&) = delete;
    WindowsMediaFoundationLaunch& operator=(const WindowsMediaFoundationLaunch&) = delete;
    [[nodiscard]] QString directory() const;
    [[nodiscard]] bool unchanged() const;
    [[nodiscard]] WindowsDownloadProcessConfig processConfig() const;

  private:
    struct Impl;
    explicit WindowsMediaFoundationLaunch(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
