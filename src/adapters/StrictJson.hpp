#pragma once

#include <QByteArray>

namespace music_player::m0::detail {
// Lexical UTF-8/JSON string, duplicate-key and depth preflight shared by the
// bounded codecs. Each caller enforces its own byte limit before invoking it.
[[nodiscard]] bool strictJsonPreflight(const QByteArray& payload);
} // namespace music_player::m0::detail
