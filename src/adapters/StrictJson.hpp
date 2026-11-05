#pragma once

#include <QByteArray>

#include <cstddef>

namespace music_player::m0::detail {
// Lexical UTF-8/JSON string, duplicate-key and depth preflight shared by the
// bounded codecs. Each caller enforces its own byte limit before invoking it.
// The trusted codec selects its nesting bound (1..64, default 4); data cannot raise it.
[[nodiscard]] bool strictJsonPreflight(const QByteArray& payload, std::size_t maximum_depth = 4);
} // namespace music_player::m0::detail
