#include "m0/domain/QueueNavigation.hpp"

namespace music_player::m0 {

std::optional<std::size_t> queueNavigationTarget(std::span<const QueueNavigationItem> items,
                                                 std::optional<std::size_t> current, QueueDirection direction,
                                                 PlaybackSnapshot snapshot, bool seek_in_progress) noexcept {
    if (!current || *current >= items.size() || seek_in_progress)
        return std::nullopt;

    if (snapshot.status != PlaybackStatus::Ready && snapshot.status != PlaybackStatus::Paused &&
        snapshot.status != PlaybackStatus::Playing)
        return std::nullopt;

    switch (direction) {
    case QueueDirection::Previous:
        if (snapshot.position_ms > 3'000 && snapshot.seekable)
            return current;
        for (std::size_t index = *current; index > 0;) {
            --index;
            if (items[index].available)
                return index;
        }
        break;
    case QueueDirection::Next:
        for (std::size_t index = *current + 1; index < items.size(); ++index) {
            if (items[index].available)
                return index;
        }
        break;
    }

    return std::nullopt;
}

} // namespace music_player::m0
