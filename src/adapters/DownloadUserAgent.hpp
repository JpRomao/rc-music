#pragma once

#include <QByteArray>

#include <algorithm>

namespace music_player::m0 {
inline constexpr qsizetype download_max_user_agent_bytes = 512;

// One public resolver hint, never a general request-header interface.
[[nodiscard]] inline bool validDownloadUserAgent(const QByteArray& value) {
    return value.size() <= download_max_user_agent_bytes &&
           std::all_of(value.cbegin(), value.cend(), [](unsigned char byte) { return byte >= 0x20 && byte <= 0x7e; });
}
} // namespace music_player::m0
