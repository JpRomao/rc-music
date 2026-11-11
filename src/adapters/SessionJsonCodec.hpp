#pragma once

#include "m0/domain/SessionSnapshot.hpp"

#include <QByteArray>
#include <QByteArrayView>
#include <QtTypes>

#include <optional>

namespace music_player::m0 {

enum class SessionJsonDecodeStatus {
    Valid,
    Invalid,
    FutureVersion,
};

struct SessionJsonDecodeResult final {
    SessionJsonDecodeStatus status{SessionJsonDecodeStatus::Invalid};
    std::optional<SessionSnapshot> snapshot{std::nullopt};
};

class SessionJsonCodec final {
  public:
    static constexpr int schema_version = 3;
    static constexpr qsizetype maximum_document_size = 16 * 1024 * 1024;

    // Invalid snapshots and documents above the safety boundary produce no bytes.
    // Encoding writes v2 until a retired import ID requires v3. v1 has no import ledger.
    [[nodiscard]] static QByteArray encode(const SessionSnapshot& snapshot);
    [[nodiscard]] static SessionJsonDecodeResult decode(QByteArrayView bytes);
};

} // namespace music_player::m0
