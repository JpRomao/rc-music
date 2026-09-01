#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QStringList>

namespace music_player::m0 {

inline constexpr qsizetype open_sources_max_path_count = 1'000;
inline constexpr qsizetype open_sources_max_payload_bytes = 1'048'576;

struct OpenSourcesCommand final {
    QStringList paths{};
};

enum class OpenSourcesCommandError {
    None,
    TooManyPaths,
    PayloadTooLarge,
    MalformedPayload,
    InvalidSchema,
    UnsupportedVersion,
    UnsupportedCommand,
    InvalidPath,
};

struct OpenSourcesEncodingResult final {
    OpenSourcesCommandError error{OpenSourcesCommandError::None};
    QByteArray payload{};
};

struct OpenSourcesDecodingResult final {
    OpenSourcesCommandError error{OpenSourcesCommandError::None};
    OpenSourcesCommand command{};
};

[[nodiscard]] OpenSourcesEncodingResult encodeOpenSourcesCommand(const OpenSourcesCommand& command);
[[nodiscard]] OpenSourcesDecodingResult decodeOpenSourcesCommand(const QByteArray& payload);

} // namespace music_player::m0

Q_DECLARE_METATYPE(music_player::m0::OpenSourcesCommandError)
