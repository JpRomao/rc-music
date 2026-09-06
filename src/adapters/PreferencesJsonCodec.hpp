#pragma once

#include "m0/domain/PlaybackTypes.hpp"

#include <QByteArray>
#include <QByteArrayView>

#include <optional>

namespace music_player::m0 {

enum class PreferencesJsonDecodeStatus {
    Valid,
    Invalid,
    FutureVersion,
};

struct PreferencesJsonDecodeResult final {
    PreferencesJsonDecodeStatus status{PreferencesJsonDecodeStatus::Invalid};
    std::optional<Preferences> preferences{std::nullopt};
};

class PreferencesJsonCodec final {
  public:
    static constexpr int schema_version = 2;

    // Domain-invalid values cannot be represented by schema v2 and produce no bytes.
    [[nodiscard]] static QByteArray encode(const Preferences& preferences);
    [[nodiscard]] static PreferencesJsonDecodeResult decode(QByteArrayView bytes);
};

} // namespace music_player::m0
