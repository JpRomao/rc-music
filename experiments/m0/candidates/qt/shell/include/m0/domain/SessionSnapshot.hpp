#pragma once

#include "m0/domain/LocalFileQueue.hpp"

#include <cstdint>

namespace music_player::m0 {

struct SessionSnapshot final {
    LocalFileQueueSnapshot queue{};
    std::int64_t position_ms{0};

    [[nodiscard]] bool operator==(const SessionSnapshot&) const = default;
};

[[nodiscard]] bool isValidSessionSnapshot(const SessionSnapshot& snapshot);

} // namespace music_player::m0
