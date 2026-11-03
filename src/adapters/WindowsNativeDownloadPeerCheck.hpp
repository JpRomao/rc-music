#pragma once

#include "adapters/NativeDownloadPeerCheck.hpp"

namespace music_player::m0 {
// Inert until begin(). Owns a non-inheritable duplicate of the borrowed pipe;
// PID/token/image queries execute in its private worker. Polling observes native
// thread/process readiness with zero wait.
// Normal owners drain stopped() before destruction. Exceptional destruction
// cancels/detaches an unfinished worker whose shared state owns all resources.
class WindowsNativeDownloadPeerCheck final : public NativeDownloadPeerCheck {
  public:
    WindowsNativeDownloadPeerCheck();
    ~WindowsNativeDownloadPeerCheck() override;
    WindowsNativeDownloadPeerCheck(const WindowsNativeDownloadPeerCheck&) = delete;
    WindowsNativeDownloadPeerCheck& operator=(const WindowsNativeDownloadPeerCheck&) = delete;

    [[nodiscard]] bool begin(qintptr borrowed_pipe, NativeDownloadPeerSide side,
                             const QStringList& expected_images) override;
    [[nodiscard]] NativeDownloadPeerState poll() override;
    void shutdown() noexcept override;
    [[nodiscard]] bool stopped() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
