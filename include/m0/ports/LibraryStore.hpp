#pragma once

#include "m0/domain/LocalLibrary.hpp"

namespace music_player::m0 {
enum class LibraryLoadStatus { Missing, Loaded, RecoveredFromBackup, Invalid, FutureVersion, Unavailable };
enum class LibrarySaveStatus { Saved, Failed, BlockedByFutureVersion };

struct LibraryLoadResult final {
    LibrarySnapshot snapshot{};
    LibraryLoadStatus status{LibraryLoadStatus::Missing};
};

// Synchronous storage port; the application runs it on its private worker.
// Loading never repairs files, and only Saved acknowledges a completed write.
class LibraryStore {
  public:
    virtual ~LibraryStore() = default;
    [[nodiscard]] virtual LibraryLoadResult load() = 0;
    [[nodiscard]] virtual LibrarySaveStatus save(const LibrarySnapshot& snapshot) = 0;
};
} // namespace music_player::m0
