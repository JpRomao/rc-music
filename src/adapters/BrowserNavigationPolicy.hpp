#pragma once

#include <QString>

namespace music_player::m0 {
// Navigation policy only: no network resolution, remote download permission or
// transfer authority. The blank document is reserved for native initialization.
[[nodiscard]] bool browserNavigationAllowed(const QString& url, bool internal_blank = false);
} // namespace music_player::m0
