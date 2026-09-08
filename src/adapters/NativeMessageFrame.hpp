#pragma once

#include <QByteArray>
#include <QByteArrayView>

#include <array>

namespace music_player::m0 {

inline constexpr qsizetype native_message_max_payload_bytes = 256 * 1024;

enum class NativeFrameStatus { Pending, Complete, Invalid, Expired };

struct NativeFrameResult final {
    NativeFrameStatus status{NativeFrameStatus::Pending};
    QByteArray payload{};
};

class NativeMessageFrame final {
  public:
    explicit NativeMessageFrame(qint64 start_ms = 0, qint64 timeout_ms = 5000);

    [[nodiscard]] NativeFrameResult receive(QByteArrayView bytes, qint64 now_ms);
    [[nodiscard]] NativeFrameResult poll(qint64 now_ms);

  private:
    [[nodiscard]] NativeFrameResult terminate(NativeFrameStatus status);

    std::array<char, 4> prefix_{};
    qsizetype prefix_size_{0};
    qsizetype expected_payload_bytes_{0};
    QByteArray payload_{};
    qint64 start_ms_{0};
    qint64 last_ms_{0};
    qint64 timeout_ms_{5000};
    NativeFrameStatus status_{NativeFrameStatus::Pending};
};

[[nodiscard]] QByteArray encodeNativeMessageFrame(QByteArrayView payload);

} // namespace music_player::m0
