#pragma once

#include <QByteArray>
#include <QtGlobal>

#include <memory>

namespace music_player::m0 {
enum class NativeStdioInputState { Waiting, Data, Closed, Failed };
struct NativeStdioInput final {
    NativeStdioInputState state{NativeStdioInputState::Failed};
    QByteArray bytes{};
};
enum class NativeStdioOutputState { Idle, Writing, Completed, Failed };
inline constexpr qsizetype native_stdio_max_response_bytes = 4096 + 4;

// Short-lived host process only, never a UI-thread adapter. Handles are borrowed
// binary Win32 byte pipes, not CRT descriptors. Input has one exclusive reader;
// output owns its buffer until the native writer thread is physically stopped.
// The host's independent process watchdog also bounds a stalled Peek/Read or a
// provider that does not honour synchronous cancellation. No logs/flush/echo.
class WindowsNativeMessagingIo final {
  public:
    WindowsNativeMessagingIo();
    WindowsNativeMessagingIo(void* input_handle, void* output_handle);
    ~WindowsNativeMessagingIo();
    WindowsNativeMessagingIo(const WindowsNativeMessagingIo&) = delete;
    WindowsNativeMessagingIo& operator=(const WindowsNativeMessagingIo&) = delete;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] NativeStdioInput readAvailable();
    [[nodiscard]] bool beginWrite(QByteArray frame, qint64 now_ms);
    void poll(qint64 now_ms);
    void beginShutdown() noexcept;
    [[nodiscard]] bool stopped() const noexcept;
    [[nodiscard]] NativeStdioOutputState outputState() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
