#pragma once

#include "m0/domain/PlaylistLibrary.hpp"

namespace music_player::m0 {
enum class PlaylistLoadStatus { Missing, Loaded, RecoveredFromBackup, Invalid, FutureVersion, Unavailable };
enum class PlaylistSaveStatus { Saved, Failed, BlockedByFutureVersion };

struct PlaylistLoadResult final {
    PlaylistLibrarySnapshot snapshot{};
    PlaylistLoadStatus status{PlaylistLoadStatus::Missing};
};

// Synchronous storage port; the application runs it on its private worker.
// Loading never repairs files, and only Saved acknowledges a completed write.
class PlaylistStore {
  public:
    virtual ~PlaylistStore() = default;
    [[nodiscard]] virtual PlaylistLoadResult load() = 0;
    [[nodiscard]] virtual PlaylistSaveStatus save(const PlaylistLibrarySnapshot& snapshot) = 0;
};
} // namespace music_player::m0
