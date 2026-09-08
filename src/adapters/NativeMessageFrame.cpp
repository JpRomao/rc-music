#include "NativeMessageFrame.hpp"

#include <QtEndian>

#include <algorithm>
#include <cstring>
#include <utility>

namespace music_player::m0 {

NativeMessageFrame::NativeMessageFrame(qint64 start_ms, qint64 timeout_ms)
    : start_ms_(start_ms), last_ms_(start_ms), timeout_ms_(timeout_ms),
      status_(timeout_ms > 0 ? NativeFrameStatus::Pending : NativeFrameStatus::Invalid) {}

NativeFrameResult NativeMessageFrame::receive(QByteArrayView bytes, qint64 now_ms) {
    if (status_ != NativeFrameStatus::Pending)
        return terminate(NativeFrameStatus::Invalid);

    const auto timing = poll(now_ms);
    if (timing.status != NativeFrameStatus::Pending)
        return timing;

    // Subtract retained bytes before inspecting/copying potentially hostile input sizes.
    if (bytes.size() > native_message_max_payload_bytes + 4 - prefix_size_ - payload_.size())
        return terminate(NativeFrameStatus::Invalid);

    if (prefix_size_ < 4) {
        const auto count = std::min(qsizetype{4} - prefix_size_, bytes.size());
        if (count > 0)
            std::memcpy(prefix_.data() + prefix_size_, bytes.data(), static_cast<std::size_t>(count));
        prefix_size_ += count;
        bytes = bytes.sliced(count);
        if (prefix_size_ < 4)
            return {};

        const auto length = qFromLittleEndian<quint32>(prefix_.data());
        if (length == 0 || length > static_cast<quint32>(native_message_max_payload_bytes))
            return terminate(NativeFrameStatus::Invalid);
        expected_payload_bytes_ = static_cast<qsizetype>(length);
    }

    if (bytes.size() > expected_payload_bytes_ - payload_.size())
        return terminate(NativeFrameStatus::Invalid);

    if (!bytes.isEmpty()) {
        // One bounded reservation prevents QByteArray's incremental growth from exceeding the frame body.
        if (payload_.isEmpty())
            payload_.reserve(expected_payload_bytes_);
        payload_.append(bytes.data(), bytes.size());
    }
    if (payload_.size() != expected_payload_bytes_)
        return {};

    status_ = NativeFrameStatus::Complete;
    return {status_, std::move(payload_)};
}

NativeFrameResult NativeMessageFrame::poll(qint64 now_ms) {
    if (status_ != NativeFrameStatus::Pending)
        return {status_, {}};
    if (now_ms < last_ms_)
        return terminate(NativeFrameStatus::Invalid);

    last_ms_ = now_ms;
    // Monotonic ordering makes unsigned subtraction exact even across the signed clock limits.
    const auto elapsed = static_cast<quint64>(now_ms) - static_cast<quint64>(start_ms_);
    if (elapsed >= static_cast<quint64>(timeout_ms_))
        return terminate(NativeFrameStatus::Expired);
    return {};
}

NativeFrameResult NativeMessageFrame::terminate(NativeFrameStatus status) {
    status_ = status;
    payload_ = {};
    return {status_, {}};
}

QByteArray encodeNativeMessageFrame(QByteArrayView payload) {
    if (payload.isEmpty() || payload.size() > native_message_max_payload_bytes)
        return {};

    QByteArray frame(payload.size() + 4, Qt::Uninitialized);
    qToLittleEndian<quint32>(static_cast<quint32>(payload.size()), frame.data());
    std::memcpy(frame.data() + 4, payload.data(), static_cast<std::size_t>(payload.size()));
    return frame;
}

} // namespace music_player::m0
