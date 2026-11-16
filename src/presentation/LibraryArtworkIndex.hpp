#pragma once

#include "m0/domain/LocalLibrary.hpp"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace music_player::m0 {
// Record ID to file path for the cover reader. The library binding updates it
// on the UI thread after each confirmed change; image threads only read it.
// Unavailable records are left out, so no read is attempted for them.
class LibraryArtworkIndex final {
  public:
    void update(const LibrarySnapshot& snapshot);
    [[nodiscard]] std::optional<std::filesystem::path> find(std::uint64_t record_id) const;

  private:
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, std::filesystem::path> paths_;
};
} // namespace music_player::m0
