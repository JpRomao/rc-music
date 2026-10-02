#pragma once

#include <memory>

namespace music_player::m0 {
// Independent, short-lived native host process watchdog. Must be created before
// every protected resource and destroyed last. Expiry terminates ONLY this host
// process with code 2; it never targets the persistent player or another PID.
class WindowsNativeHostWatchdog final {
  public:
    explicit WindowsNativeHostWatchdog(unsigned long timeout_ms = 15'000);
    ~WindowsNativeHostWatchdog();
    WindowsNativeHostWatchdog(const WindowsNativeHostWatchdog&) = delete;
    WindowsNativeHostWatchdog& operator=(const WindowsNativeHostWatchdog&) = delete;
    [[nodiscard]] bool valid() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
