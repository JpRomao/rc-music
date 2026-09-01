#include "m0/domain/LocalFileQueue.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <unordered_set>
#include <utility>

namespace music_player::m0 {

namespace {

using OccurrenceIdValues = std::unordered_set<std::uint64_t>;

[[nodiscard]] std::uint64_t followingOccurrenceId(const std::uint64_t occurrence_id) noexcept {
    return occurrence_id == std::numeric_limits<std::uint64_t>::max() ? 1 : occurrence_id + 1;
}

[[nodiscard]] OccurrenceIdValues occupiedOccurrenceIds(const std::span<const LocalFileQueueItem> items,
                                                       const std::size_t additional_capacity) {
    OccurrenceIdValues result;
    result.reserve(items.size() + additional_capacity);
    for (const auto& item : items) {
        result.insert(item.occurrence_id.value);
    }
    return result;
}

[[nodiscard]] LocalQueueOccurrenceId nextOccurrenceId(std::uint64_t& candidate, OccurrenceIdValues& occupied_ids) {
    for (;;) {
        if (candidate == 0) {
            candidate = 1;
        }
        const LocalQueueOccurrenceId result{candidate};
        candidate = followingOccurrenceId(candidate);
        if (occupied_ids.insert(result.value).second) {
            return result;
        }
    }
}

} // namespace

bool isValidLocalFileQueueSnapshot(const LocalFileQueueSnapshot& snapshot) {
    if (snapshot.items.empty()) {
        return !snapshot.current_occurrence_id.has_value();
    }

    OccurrenceIdValues occurrence_ids;
    occurrence_ids.reserve(snapshot.items.size());
    for (const auto& item : snapshot.items) {
        if (item.occurrence_id.value == 0 || item.source_path.empty() ||
            !occurrence_ids.insert(item.occurrence_id.value).second) {
            return false;
        }
    }

    return !snapshot.current_occurrence_id.has_value() ||
           occurrence_ids.contains(snapshot.current_occurrence_id->value);
}

bool LocalFileQueue::replace(std::vector<std::filesystem::path> source_paths) {
    if (source_paths.empty() || std::ranges::any_of(source_paths, [](const std::filesystem::path& source_path) {
            return source_path.empty();
        })) {
        return false;
    }

    std::vector<LocalFileQueueItem> replacement;
    replacement.reserve(source_paths.size());
    auto candidate = next_occurrence_id_;
    auto occupied_ids = occupiedOccurrenceIds(items_, source_paths.size());
    for (auto& source_path : source_paths) {
        replacement.push_back({nextOccurrenceId(candidate, occupied_ids), std::move(source_path), true});
    }

    items_ = std::move(replacement);
    current_occurrence_id_ = items_.front().occurrence_id;
    next_occurrence_id_ = candidate;
    return true;
}

bool LocalFileQueue::append(std::vector<std::filesystem::path> source_paths) {
    return insert(std::move(source_paths), items_.size());
}

bool LocalFileQueue::insert(std::vector<std::filesystem::path> source_paths, const std::size_t insertion_index) {
    if (insertion_index > items_.size() || source_paths.empty() ||
        std::ranges::any_of(source_paths,
                            [](const std::filesystem::path& source_path) { return source_path.empty(); })) {
        return false;
    }

    std::vector<LocalFileQueueItem> appended_items;
    appended_items.reserve(source_paths.size());
    auto candidate = next_occurrence_id_;
    auto occupied_ids = occupiedOccurrenceIds(items_, source_paths.size());
    for (auto& source_path : source_paths) {
        appended_items.push_back({nextOccurrenceId(candidate, occupied_ids), std::move(source_path), true});
    }

    items_.reserve(items_.size() + appended_items.size());
    items_.insert(std::next(items_.begin(), static_cast<std::ptrdiff_t>(insertion_index)),
                  std::make_move_iterator(appended_items.begin()), std::make_move_iterator(appended_items.end()));
    next_occurrence_id_ = candidate;
    return true;
}

bool LocalFileQueue::move(const LocalQueueOccurrenceId occurrence_id, const std::size_t destination_index) noexcept {
    if (destination_index >= items_.size()) {
        return false;
    }

    const auto source = std::ranges::find(items_, occurrence_id, &LocalFileQueueItem::occurrence_id);
    if (source == items_.end()) {
        return false;
    }
    const auto source_index = static_cast<std::size_t>(std::distance(items_.begin(), source));
    if (source_index == destination_index) {
        return false;
    }

    if (source_index < destination_index) {
        std::rotate(source, std::next(source),
                    std::next(items_.begin(), static_cast<std::ptrdiff_t>(destination_index + 1)));
    } else {
        const auto destination = std::next(items_.begin(), static_cast<std::ptrdiff_t>(destination_index));
        std::rotate(destination, source, std::next(source));
    }
    return true;
}

bool LocalFileQueue::remove(const LocalQueueOccurrenceId occurrence_id) noexcept {
    const auto item = std::ranges::find(items_, occurrence_id, &LocalFileQueueItem::occurrence_id);
    if (item == items_.end()) {
        return false;
    }
    if (current_occurrence_id_ == occurrence_id) {
        current_occurrence_id_.reset();
    }
    items_.erase(item);
    return true;
}

bool LocalFileQueue::clear() noexcept {
    if (items_.empty()) {
        return false;
    }
    items_.clear();
    current_occurrence_id_.reset();
    return true;
}

bool LocalFileQueue::select(const std::size_t index) noexcept {
    if (index >= items_.size() || !items_[index].available) {
        return false;
    }
    current_occurrence_id_ = items_[index].occurrence_id;
    return true;
}

bool LocalFileQueue::markUnavailable(const std::size_t index) noexcept {
    if (index >= items_.size() || !items_[index].available) {
        return false;
    }
    items_[index].available = false;
    return true;
}

bool LocalFileQueue::clearSelection() noexcept {
    if (!current_occurrence_id_.has_value()) {
        return false;
    }
    current_occurrence_id_.reset();
    return true;
}

LocalFileQueueSnapshot LocalFileQueue::snapshot() const {
    LocalFileQueueSnapshot result{
        .items = {},
        .current_occurrence_id = current_occurrence_id_,
    };
    result.items.reserve(items_.size());
    for (const auto& item : items_) {
        result.items.push_back({item.occurrence_id, item.source_path});
    }
    return result;
}

bool LocalFileQueue::restore(LocalFileQueueSnapshot snapshot) {
    if (!isValidLocalFileQueueSnapshot(snapshot)) {
        return false;
    }

    std::vector<LocalFileQueueItem> restored_items;
    restored_items.reserve(snapshot.items.size());
    std::uint64_t highest_occurrence_id = 0;
    for (auto& item : snapshot.items) {
        highest_occurrence_id = std::max(highest_occurrence_id, item.occurrence_id.value);
        restored_items.push_back({item.occurrence_id, std::move(item.source_path), true});
    }

    const auto restored_next_occurrence_id =
        highest_occurrence_id == 0 ? 1 : followingOccurrenceId(highest_occurrence_id);
    items_ = std::move(restored_items);
    current_occurrence_id_ = snapshot.current_occurrence_id;
    next_occurrence_id_ = restored_next_occurrence_id;
    return true;
}

bool LocalFileQueue::empty() const noexcept { return items_.empty(); }

std::size_t LocalFileQueue::size() const noexcept { return items_.size(); }

std::optional<LocalQueueOccurrenceId> LocalFileQueue::currentOccurrenceId() const noexcept {
    return current_occurrence_id_;
}

std::optional<std::size_t> LocalFileQueue::currentIndex() const noexcept {
    if (!current_occurrence_id_.has_value()) {
        return std::nullopt;
    }
    const auto current = std::ranges::find(items_, *current_occurrence_id_, &LocalFileQueueItem::occurrence_id);
    if (current == items_.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(items_.begin(), current));
}

std::span<const LocalFileQueueItem> LocalFileQueue::items() const noexcept { return items_; }

} // namespace music_player::m0
