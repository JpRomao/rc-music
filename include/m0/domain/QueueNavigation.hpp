#pragma once

#include "m0/domain/PlaybackTypes.hpp"

#include <cstddef>
#include <optional>
#include <span>

namespace music_player::m0 {

enum class QueueDirection {
    Previous,
    Next,
};

struct QueueNavigationItem final {
    bool available{true};
};

[[nodiscard]] std::optional<std::size_t> queueNavigationTarget(std::span<const QueueNavigationItem> items,
                                                               std::optional<std::size_t> current,
                                                               QueueDirection direction, PlaybackSnapshot snapshot,
                                                               bool seek_in_progress = false) noexcept;

} // namespace music_player::m0
