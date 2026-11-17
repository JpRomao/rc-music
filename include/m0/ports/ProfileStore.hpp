#pragma once

#include "m0/domain/ListenerProfile.hpp"

#include <optional>

namespace music_player::m0 {
enum class ProfileLoadStatus { Missing, Loaded, RecoveredFromBackup, Invalid, FutureVersion, Unavailable };
enum class ProfileSaveStatus { Saved, Failed, BlockedByFutureVersion };

struct ProfileLoadResult final {
    std::optional<ListenerProfile> profile{};
    ProfileLoadStatus status{ProfileLoadStatus::Missing};
};

// Synchronous storage port for the listener profile; same states as PlaylistStore.
// Loading never repairs files, and only Saved acknowledges a completed write.
class ProfileStore {
  public:
    virtual ~ProfileStore() = default;
    [[nodiscard]] virtual ProfileLoadResult load() = 0;
    [[nodiscard]] virtual ProfileSaveStatus save(const ListenerProfile& profile) = 0;
};
} // namespace music_player::m0
