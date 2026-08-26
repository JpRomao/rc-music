#pragma once

#include "m0/adapters/fixture/FixtureRepository.hpp"

#include <QJsonObject>

namespace music_player::m0::detail {

// Internal trust-boundary seam. Production callers must authenticate both byte streams before decoding.
[[nodiscard]] FixtureBundle decodeAuthenticatedDocuments(const QJsonObject& fixture, const QJsonObject& queue);

} // namespace music_player::m0::detail
