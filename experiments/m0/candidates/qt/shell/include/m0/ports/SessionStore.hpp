#pragma once

#include "m0/domain/SessionSnapshot.hpp"

#include <cstdint>
#include <optional>

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

using SessionRevision = std::uint64_t;

struct SessionWriteRequest final {
    SessionRevision revision{0};
    SessionSnapshot snapshot{};
    bool replace_future_version{false};
};

struct SessionWriteCompletion final {
    SessionRevision revision{0};
    SessionSaveStatus status{SessionSaveStatus::Failed};
    std::optional<bool> requested_snapshot_recoverable_after_failure{};
};

enum class SessionWriterCommand {
    Accepted,
    Busy,
    Shutdown,
};

class SessionWriter {
  public:
    virtual ~SessionWriter() = default;

    [[nodiscard]] virtual SessionWriterCommand start(SessionWriteRequest request) = 0;
    [[nodiscard]] virtual std::optional<SessionWriteCompletion> poll() = 0;
    [[nodiscard]] virtual std::optional<SessionWriteCompletion> flush(SessionWriteRequest request) = 0;
    [[nodiscard]] virtual bool shutdown() = 0;
};

} // namespace music_player::m0
