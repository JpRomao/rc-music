#pragma once

#include <QByteArray>
#include <QString>

#include <array>

namespace music_player::m0::native_download_ipc {
inline constexpr std::array<char, 8> magic{'R', 'C', 'M', 'D', 'L', 'I', 'P', 'C'};
inline constexpr char version = 1;
inline constexpr qsizetype origin_bytes = 52;
inline constexpr qsizetype preamble_bytes = static_cast<qsizetype>(magic.size()) + 1 + origin_bytes;
inline constexpr qsizetype response_max_payload_bytes = 4'096;
static_assert(preamble_bytes == 61);

[[nodiscard]] inline QByteArray preamble(const QString& origin) {
    const auto bytes = origin.toUtf8();
    if (bytes.size() != origin_bytes)
        return {};
    QByteArray result{magic.data(), static_cast<qsizetype>(magic.size())};
    result.append(version);
    result.append(bytes);
    return result;
}
} // namespace music_player::m0::native_download_ipc
