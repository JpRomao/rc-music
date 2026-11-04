#pragma once

#include <QString>

namespace music_player::m0 {
// Canonical direct-media URL policy shared by extension and embedded-browser
// boundaries. Validation never normalizes signed URL/query bytes. DNS,
// connections and redirects require their own checks in the transport.
[[nodiscard]] bool isAllowedDirectMediaUrl(const QString& raw);
} // namespace music_player::m0
