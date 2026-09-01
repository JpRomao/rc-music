#pragma once

#include "m0/domain/SessionSnapshot.hpp"

namespace music_player::m0 {

enum class SessionLoadStatus {
    Missing,
    Loaded,
    RecoveredFromBackup,
    InvalidUsingDefaults,
    FutureVersion,
};

enum class SessionSaveStatus {
    Saved,
    Failed,
    BlockedByFutureVersion,
};

struct SessionLoadResult final {
    SessionSnapshot snapshot{};
    SessionLoadStatus status{SessionLoadStatus::Missing};
};

struct SessionSaveRequest final {
    SessionSnapshot snapshot{};
    bool replace_future_version{false};
};

class SessionStore {
  public:
    virtual ~SessionStore() = default;

    [[nodiscard]] virtual SessionLoadResult load() = 0;
    [[nodiscard]] virtual SessionSaveStatus save(const SessionSaveRequest& request) = 0;
};

} // namespace music_player::m0
