#pragma once

#include "m0/domain/PlaybackTypes.hpp"

#include <cstdint>
#include <optional>

namespace music_player::m0 {

enum class PreferencesLoadStatus {
    Missing,
    Loaded,
    RecoveredFromBackup,
    InvalidUsingDefaults,
    FutureVersion,
};

enum class PreferencesSaveStatus {
    Saved,
    Failed,
    BlockedByFutureVersion,
};

struct PreferencesLoadResult final {
    Preferences preferences{};
    PreferencesLoadStatus status{PreferencesLoadStatus::Missing};
};

struct PreferencesSaveRequest final {
    Preferences preferences{};
    bool replace_future_version{false};
};

class PreferencesStore {
  public:
    virtual ~PreferencesStore() = default;

    [[nodiscard]] virtual PreferencesLoadResult load() = 0;
    [[nodiscard]] virtual PreferencesSaveStatus save(const PreferencesSaveRequest& request) = 0;
};

using PreferencesRevision = std::uint64_t;

struct PreferencesWriteRequest final {
    PreferencesRevision revision{0};
    Preferences preferences{};
    bool replace_future_version{false};
};

struct PreferencesWriteCompletion final {
    PreferencesRevision revision{0};
    PreferencesSaveStatus status{PreferencesSaveStatus::Failed};
};

enum class PreferencesWriterCommand {
    Accepted,
    Busy,
    Shutdown,
};

class PreferencesWriter {
  public:
    virtual ~PreferencesWriter() = default;

    [[nodiscard]] virtual PreferencesWriterCommand start(PreferencesWriteRequest request) = 0;
    [[nodiscard]] virtual std::optional<PreferencesWriteCompletion> poll() = 0;
    [[nodiscard]] virtual std::optional<PreferencesWriteCompletion> flush(PreferencesWriteRequest request) = 0;
    [[nodiscard]] virtual bool shutdown() = 0;
};

} // namespace music_player::m0
