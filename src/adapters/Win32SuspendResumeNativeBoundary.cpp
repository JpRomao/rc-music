#include "adapters/WindowsPowerEvents.hpp"

#include <qt_windows.h>

#include <memory>
#include <utility>

namespace music_player::m0 {
namespace {

class Win32SuspendResumeNativeBoundary final : public SuspendResumeNativeBoundary {
  public:
    ~Win32SuspendResumeNativeBoundary() override { shutdown(); }

    [[nodiscard]] bool bind(std::uintptr_t native_window_handle) noexcept override {
        shutdown();
        if (native_window_handle == 0)
            return false;
        notification_ = RegisterSuspendResumeNotification(reinterpret_cast<HANDLE>(native_window_handle),
                                                          DEVICE_NOTIFY_WINDOW_HANDLE);
        return notification_ != nullptr;
    }

    void shutdown() noexcept override {
        const auto notification = std::exchange(notification_, nullptr);
        if (notification != nullptr)
            static_cast<void>(UnregisterSuspendResumeNotification(notification));
    }

  private:
    HPOWERNOTIFY notification_{nullptr};
};

} // namespace

std::unique_ptr<SuspendResumeNativeBoundary> makeWin32SuspendResumeNativeBoundary() {
    return std::make_unique<Win32SuspendResumeNativeBoundary>();
}

} // namespace music_player::m0
