#include "WindowsNativeMessagingIo.hpp"

#include "NativeMessageFrame.hpp"

#include <qt_windows.h>

#include <algorithm>
#include <atomic>
#include <thread>

namespace music_player::m0 {
namespace {
bool isPipe(const HANDLE handle) {
    return handle != nullptr && handle != INVALID_HANDLE_VALUE && GetFileType(handle) == FILE_TYPE_PIPE;
}
} // namespace
struct WindowsNativeMessagingIo::Impl {
    HANDLE input;
    HANDLE output;
    bool valid;
    qsizetype maximum_snapshot;
    bool closing{false};
    bool input_terminal{false};
    bool timing_failed{false};
    bool attempted{false};
    qint64 started_ms{0};
    qint64 last_ms{0};
    NativeStdioOutputState state{NativeStdioOutputState::Idle};
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> writer_succeeded{false};
    std::thread writer;
    HANDLE writer_handle{nullptr};

    Impl(const HANDLE in, const HANDLE out, const qsizetype maximum_input_payload_bytes)
        : input(in), output(out),
          valid(maximum_input_payload_bytes > 0 && maximum_input_payload_bytes <= native_message_max_payload_bytes &&
                isPipe(in) && isPipe(out)),
          maximum_snapshot(valid ? maximum_input_payload_bytes + 5 : 0) {}
    bool stopped() const noexcept {
        return !writer.joinable() || WaitForSingleObject(writer_handle, 0) == WAIT_OBJECT_0;
    }
    void cancel() noexcept {
        stop_requested.store(true);
        if (writer.joinable() && !stopped())
            // ERROR_NOT_FOUND is an expected race before/after WriteFile. The
            // caller repeats this until the native thread, not a flag, stops.
            static_cast<void>(CancelSynchronousIo(writer_handle));
    }
};
WindowsNativeMessagingIo::WindowsNativeMessagingIo()
    : WindowsNativeMessagingIo(GetStdHandle(STD_INPUT_HANDLE), GetStdHandle(STD_OUTPUT_HANDLE)) {}
WindowsNativeMessagingIo::WindowsNativeMessagingIo(void* input, void* output)
    : WindowsNativeMessagingIo(input, output, native_message_max_payload_bytes) {}
WindowsNativeMessagingIo::WindowsNativeMessagingIo(const qsizetype maximum_input_payload_bytes)
    : WindowsNativeMessagingIo(GetStdHandle(STD_INPUT_HANDLE), GetStdHandle(STD_OUTPUT_HANDLE),
                               maximum_input_payload_bytes) {}
WindowsNativeMessagingIo::WindowsNativeMessagingIo(void* input, void* output,
                                                   const qsizetype maximum_input_payload_bytes)
    : impl_(std::make_unique<Impl>(input, output, maximum_input_payload_bytes)) {}
WindowsNativeMessagingIo::~WindowsNativeMessagingIo() {
    beginShutdown();
    while (!impl_->stopped()) {
        impl_->cancel();
        Sleep(1);
    }
    if (impl_->writer.joinable())
        impl_->writer.join();
}
bool WindowsNativeMessagingIo::valid() const noexcept { return impl_->valid; }
NativeStdioInput WindowsNativeMessagingIo::readAvailable() {
    if (!impl_->valid)
        return {};
    if (impl_->closing || impl_->input_terminal)
        return {NativeStdioInputState::Closed};
    DWORD available = 0;
    if (!PeekNamedPipe(impl_->input, nullptr, 0, nullptr, &available, nullptr)) {
        impl_->input_terminal = true;
        return {GetLastError() == ERROR_BROKEN_PIPE ? NativeStdioInputState::Closed : NativeStdioInputState::Failed};
    }
    if (available == 0)
        return {NativeStdioInputState::Waiting};
    // A full snapshot includes an already-present trailing byte, even when the
    // valid first frame ends at a former arbitrary chunk boundary. A larger
    // snapshot is refused before allocating/copying anything or admitting it.
    if (available > static_cast<DWORD>(impl_->maximum_snapshot)) {
        impl_->input_terminal = true;
        return {};
    }
    QByteArray bytes{static_cast<qsizetype>(available), Qt::Uninitialized};
    DWORD offset = 0;
    while (offset < available) {
        DWORD received = 0;
        if (!ReadFile(impl_->input, bytes.data() + offset, available - offset, &received, nullptr) || received == 0) {
            impl_->input_terminal = true;
            return {}; // Never publish a partially consumed snapshot.
        }
        offset += received;
    }
    return {NativeStdioInputState::Data, std::move(bytes)};
}
bool WindowsNativeMessagingIo::beginWrite(QByteArray frame, const qint64 now_ms) {
    if (!impl_->valid || impl_->closing || impl_->attempted || frame.isEmpty() ||
        frame.size() > native_stdio_max_response_bytes || now_ms < 0)
        return false;
    impl_->attempted = true;
    impl_->started_ms = impl_->last_ms = now_ms;
    impl_->state = NativeStdioOutputState::Writing;
    try {
        impl_->writer = std::thread([state = impl_.get(), bytes = std::move(frame)] {
            qsizetype offset = 0;
            while (offset < bytes.size() && !state->stop_requested.load()) {
                const auto remaining = std::min<qsizetype>(4096, bytes.size() - offset);
                DWORD written = 0;
                if (!WriteFile(state->output, bytes.constData() + offset, static_cast<DWORD>(remaining), &written,
                               nullptr) ||
                    written == 0 || written > static_cast<DWORD>(remaining))
                    return;
                offset += written;
            }
            state->writer_succeeded.store(offset == bytes.size() && !state->stop_requested.load());
        });
        impl_->writer_handle = impl_->writer.native_handle();
    } catch (...) {
        impl_->state = NativeStdioOutputState::Failed;
        return false;
    }
    return true;
}
void WindowsNativeMessagingIo::poll(const qint64 now_ms) {
    if (impl_->state != NativeStdioOutputState::Writing)
        return;
    if (now_ms < impl_->last_ms || static_cast<quint64>(now_ms) - static_cast<quint64>(impl_->started_ms) >= 2000) {
        impl_->timing_failed = true;
        impl_->closing = true;
    }
    impl_->last_ms = now_ms;
    if (impl_->closing)
        impl_->cancel();
    if (!impl_->stopped())
        return;
    if (impl_->writer.joinable())
        impl_->writer.join(); // Native HANDLE is signaled; no running work waits here.
    impl_->state = !impl_->timing_failed && !impl_->closing && impl_->writer_succeeded.load()
                       ? NativeStdioOutputState::Completed
                       : NativeStdioOutputState::Failed;
}
void WindowsNativeMessagingIo::beginShutdown() noexcept {
    impl_->closing = true;
    impl_->cancel();
}
bool WindowsNativeMessagingIo::stopped() const noexcept { return impl_->stopped(); }
NativeStdioOutputState WindowsNativeMessagingIo::outputState() const noexcept { return impl_->state; }
} // namespace music_player::m0
