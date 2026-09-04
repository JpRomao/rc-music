#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace music_player::m0 {

struct LocalQueueOccurrenceId final {
    std::uint64_t value{0};

    [[nodiscard]] bool operator==(const LocalQueueOccurrenceId&) const noexcept = default;
};

struct LocalFileQueueSnapshotItem final {
    LocalQueueOccurrenceId occurrence_id{};
    std::filesystem::path source_path{};

    [[nodiscard]] bool operator==(const LocalFileQueueSnapshotItem&) const = default;
};

struct LocalFileQueueSnapshot final {
    std::vector<LocalFileQueueSnapshotItem> items{};
    std::optional<LocalQueueOccurrenceId> current_occurrence_id{};

    [[nodiscard]] bool operator==(const LocalFileQueueSnapshot&) const = default;
};

[[nodiscard]] bool isValidLocalFileQueueSnapshot(const LocalFileQueueSnapshot& snapshot);

struct LocalFileQueueItem final {
    LocalQueueOccurrenceId occurrence_id{};
    std::filesystem::path source_path{};
    bool available{true};
};

class LocalFileQueue final {
  public:
    [[nodiscard]] bool replace(std::vector<std::filesystem::path> source_paths);
    [[nodiscard]] bool append(std::vector<std::filesystem::path> source_paths);
    [[nodiscard]] bool insert(std::vector<std::filesystem::path> source_paths, std::size_t insertion_index);
    [[nodiscard]] bool move(LocalQueueOccurrenceId occurrence_id, std::size_t destination_index) noexcept;
    [[nodiscard]] bool remove(LocalQueueOccurrenceId occurrence_id) noexcept;
    [[nodiscard]] bool clear() noexcept;
    [[nodiscard]] bool select(std::size_t index) noexcept;
    [[nodiscard]] bool selectUnavailableForRetry(std::size_t index) noexcept;
    [[nodiscard]] bool markUnavailable(std::size_t index) noexcept;
    [[nodiscard]] bool markAvailable(std::size_t index) noexcept;
    [[nodiscard]] bool clearSelection() noexcept;
    [[nodiscard]] LocalFileQueueSnapshot snapshot() const;
    // Replaces the identity universe and is intended for bootstrap before occurrence ids are exposed.
    [[nodiscard]] bool restore(LocalFileQueueSnapshot snapshot);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::optional<LocalQueueOccurrenceId> currentOccurrenceId() const noexcept;
    [[nodiscard]] std::optional<std::size_t> currentIndex() const noexcept;
    [[nodiscard]] std::span<const LocalFileQueueItem> items() const noexcept;

  private:
    std::vector<LocalFileQueueItem> items_{};
    std::optional<LocalQueueOccurrenceId> current_occurrence_id_{};
    std::uint64_t next_occurrence_id_{1};
};

} // namespace music_player::m0
