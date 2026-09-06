#pragma once

#include "m0/ports/PreferencesStore.hpp"

#include <chrono>
#include <functional>
#include <optional>

namespace music_player::m0 {

enum class PreferencesAutosaveNotice {
    None,
    SaveFailed,
    FutureVersionBlocked,
};

struct PreferencesAutosaveOptions final {
    std::chrono::milliseconds debounce{500};
    std::function<std::chrono::steady_clock::time_point()> now{[] { return std::chrono::steady_clock::now(); }};
};

class PreferencesAutosave final {
  public:
    using TimePoint = std::chrono::steady_clock::time_point;

    PreferencesAutosave(PreferencesWriter& writer, Preferences initial, PreferencesAutosaveOptions options = {});

    [[nodiscard]] bool schedule(const Preferences& preferences);
    void scheduleRepair();
    void requestSaveNow();
    void poll();
    [[nodiscard]] PreferencesSaveStatus flush();
    [[nodiscard]] bool retry();
    [[nodiscard]] bool confirmFutureVersionReplacement();

    [[nodiscard]] bool dirty() const noexcept;
    [[nodiscard]] bool writeActive() const noexcept;
    [[nodiscard]] bool needsPolling() const noexcept;
    [[nodiscard]] PreferencesRevision latestRevision() const noexcept;
    [[nodiscard]] PreferencesAutosaveNotice notice() const noexcept;

  private:
    void tryStartDueWrite();
    void applyCompletion(const PreferencesWriteCompletion& completion, bool replaced_future_version);
    [[nodiscard]] PreferencesWriteRequest latestRequest() const;

    PreferencesWriter& writer_;
    Preferences latest_{};
    PreferencesAutosaveOptions options_{};
    PreferencesRevision latest_revision_{0};
    std::optional<PreferencesRevision> active_revision_;
    std::optional<bool> active_replaces_future_version_;
    std::optional<TimePoint> due_;
    PreferencesAutosaveNotice notice_{PreferencesAutosaveNotice::None};
    bool dirty_{false};
    bool replace_future_version_{false};
    bool retry_after_active_{false};
};

} // namespace music_player::m0
