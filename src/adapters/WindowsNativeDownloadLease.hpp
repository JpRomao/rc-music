#pragma once

#include <QString>

namespace music_player::m0 {
// Cooperative Windows owner reservation, not browser/process authentication.
// Retain this object until the app owner and its workers have fully drained;
// closing ingress alone must not make another owner eligible to start.
class WindowsNativeDownloadLease final {
  public:
    WindowsNativeDownloadLease() = default;
    ~WindowsNativeDownloadLease();
    WindowsNativeDownloadLease(const WindowsNativeDownloadLease&) = delete;
    WindowsNativeDownloadLease& operator=(const WindowsNativeDownloadLease&) = delete;

    [[nodiscard]] bool acquire(const QString& endpoint);
    void reset() noexcept;

  private:
    void* marker_{nullptr};
};
} // namespace music_player::m0
