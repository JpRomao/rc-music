#include "LibraryArtworkIndex.hpp"

namespace music_player::m0 {
void LibraryArtworkIndex::update(const LibrarySnapshot& snapshot) {
    std::unordered_map<std::uint64_t, std::filesystem::path> paths;
    paths.reserve(snapshot.records.size());
    for (const auto& record : snapshot.records)
        if (record.available)
            paths.emplace(record.id, record.source_path);
    std::lock_guard lock(mutex_);
    paths_ = std::move(paths);
}
void LibraryArtworkIndex::replace(std::vector<std::pair<std::uint64_t, std::filesystem::path>> entries) {
    std::unordered_map<std::uint64_t, std::filesystem::path> paths;
    paths.reserve(entries.size());
    for (auto& entry : entries)
        paths.emplace(entry.first, std::move(entry.second));
    std::lock_guard lock(mutex_);
    paths_ = std::move(paths);
}
std::optional<std::filesystem::path> LibraryArtworkIndex::find(const std::uint64_t record_id) const {
    std::lock_guard lock(mutex_);
    const auto found = paths_.find(record_id);
    if (found == paths_.end())
        return std::nullopt;
    return found->second;
}
} // namespace music_player::m0
