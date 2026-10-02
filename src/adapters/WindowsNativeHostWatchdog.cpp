#include "WindowsNativeHostWatchdog.hpp"

#include <qt_windows.h>

#include <thread>

namespace music_player::m0 {
struct WindowsNativeHostWatchdog::Impl {
    HANDLE completed{nullptr};
    std::thread worker;
    explicit Impl(const unsigned long timeout_ms) {
        if (timeout_ms == 0 || timeout_ms > 15'000)
            return;
        completed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (completed == nullptr)
            return;
        try {
            worker = std::thread([event = completed, timeout_ms] {
                if (WaitForSingleObject(event, timeout_ms) != WAIT_OBJECT_0) {
                    // A stuck Win32 call or teardown cannot keep this ephemeral
                    // host alive. No browser, owner app, or supplied PID is used.
                    static_cast<void>(TerminateProcess(GetCurrentProcess(), 2));
                }
            });
        } catch (...) {
            CloseHandle(completed);
            completed = nullptr;
        }
    }
    ~Impl() {
        if (completed == nullptr)
            return;
        // The watchdog is the outermost lifetime: this event is signaled only
        // after all protected I/O, proxy and Qt resources have been destroyed.
        static_cast<void>(SetEvent(completed));
        worker.join();
        CloseHandle(completed);
    }
};
WindowsNativeHostWatchdog::WindowsNativeHostWatchdog(const unsigned long timeout_ms)
    : impl_(std::make_unique<Impl>(timeout_ms)) {}
WindowsNativeHostWatchdog::~WindowsNativeHostWatchdog() = default;
bool WindowsNativeHostWatchdog::valid() const noexcept { return impl_->completed != nullptr; }
} // namespace music_player::m0
