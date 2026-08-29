#include "m0/domain/LocalFileQueue.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace music_player::m0 {

bool LocalFileQueue::replace(std::vector<std::filesystem::path> source_paths) {
    if (source_paths.empty() || std::ranges::any_of(source_paths, [](const std::filesystem::path& source_path) {
            return source_path.empty();
        })) {
        return false;
    }

    std::vector<LocalFileQueueItem> replacement;
    replacement.reserve(source_paths.size());
    for (auto& source_path : source_paths) {
        replacement.push_back({nextOccurrenceId(), std::move(source_path), true});
    }

    items_ = std::move(replacement);
    current_index_ = 0;
    return true;
}

bool LocalFileQueue::select(const std::size_t index) noexcept {
    if (index >= items_.size() || !items_[index].available) {
        return false;
    }
    current_index_ = index;
    return true;
}

bool LocalFileQueue::markUnavailable(const std::size_t index) noexcept {
    if (index >= items_.size() || !items_[index].available) {
        return false;
    }
    items_[index].available = false;
    return true;
}

bool LocalFileQueue::empty() const noexcept { return items_.empty(); }

std::size_t LocalFileQueue::size() const noexcept { return items_.size(); }

std::optional<std::size_t> LocalFileQueue::currentIndex() const noexcept { return current_index_; }

std::span<const LocalFileQueueItem> LocalFileQueue::items() const noexcept { return items_; }

LocalQueueOccurrenceId LocalFileQueue::nextOccurrenceId() noexcept {
    const LocalQueueOccurrenceId result{next_occurrence_id_};
    next_occurrence_id_ =
        next_occurrence_id_ == std::numeric_limits<std::uint64_t>::max() ? 1 : next_occurrence_id_ + 1;
    return result;
}

} // namespace music_player::m0
