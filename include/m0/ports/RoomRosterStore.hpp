#pragma once

#include "m0/domain/RoomRoster.hpp"

namespace music_player::m0 {
enum class RoomRosterLoadStatus { Missing, Loaded, RecoveredFromBackup, Invalid, FutureVersion, Unavailable };
enum class RoomRosterSaveStatus { Saved, Failed, BlockedByFutureVersion };

// The snapshot is defaultRoomRoster() for every status except Loaded and RecoveredFromBackup.
// For Invalid, FutureVersion and Unavailable the caller decides what to do (typically run with the
// default roster in memory and refuse to save until the file is dealt with); load never repairs files.
struct RoomRosterLoadResult final {
    RoomRosterSnapshot snapshot{defaultRoomRoster()};
    RoomRosterLoadStatus status{RoomRosterLoadStatus::Missing};
};

// Synchronous storage port for roles, known listeners and the persistent invite.
// Same states as ProfileStore; only Saved acknowledges a completed write.
class RoomRosterStore {
  public:
    virtual ~RoomRosterStore() = default;
    [[nodiscard]] virtual RoomRosterLoadResult load() = 0;
    [[nodiscard]] virtual RoomRosterSaveStatus save(const RoomRosterSnapshot& snapshot) = 0;
};
} // namespace music_player::m0
