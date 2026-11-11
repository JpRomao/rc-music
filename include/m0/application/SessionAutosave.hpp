#pragma once

#include "m0/ports/SessionStore.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>

namespace music_player::m0 {

enum class SessionAutosaveNotice {
    None,
    SaveFailed,
    SaveIncomplete,
    FutureVersionBlocked,
};

struct SessionAutosaveOptions final {
    std::chrono::milliseconds structural_debounce{500};
    std::chrono::milliseconds maximum_dirty_age{5'000};
    std::chrono::milliseconds position_checkpoint{10'000};
    std::function<std::chrono::steady_clock::time_point()> now{[] { return std::chrono::steady_clock::now(); }};
};

class SessionAutosave final {
  public:
    using TimePoint = std::chrono::steady_clock::time_point;

    SessionAutosave(SessionWriter& writer, SessionSnapshot initial, SessionAutosaveOptions options = {});

    [[nodiscard]] bool scheduleStructural(const SessionSnapshot& snapshot);
    // Update only the latest ledger, even when the saved queue was not restored
    // into the presenter. The existing write suspension still requires retry.
    [[nodiscard]] bool scheduleDownloadImportRetirement(std::span<const std::string> job_ids);
    [[nodiscard]] bool observeConfirmedPosition(std::int64_t position_ms);
    [[nodiscard]] bool checkpointConfirmedPosition(std::int64_t position_ms);
    void scheduleRepair();
    void requestSaveNow();
    void poll();
    [[nodiscard]] SessionSaveStatus flush();
    [[nodiscard]] bool retry();
    [[nodiscard]] bool confirmFutureVersionReplacement();

    [[nodiscard]] bool dirty() const noexcept;
    [[nodiscard]] bool writeActive() const noexcept;
    [[nodiscard]] bool needsPolling() const noexcept;
    [[nodiscard]] SessionRevision latestRevision() const noexcept;
    // Last exact Saved acknowledgement for this owner's submitted write. Zero
    // means none; clean initial/load state and recoverable Failed are not ACKs.
    // A superseded save can advance this without cleaning newer dirty state.
    [[nodiscard]] SessionRevision savedRevision() const noexcept;
    [[nodiscard]] SessionAutosaveNotice notice() const noexcept;
    [[nodiscard]] std::optional<TimePoint> nextWakeup() const noexcept;

  private:
    [[nodiscard]] bool positionIsValid(std::int64_t position_ms) const noexcept;
    void armStructuralWrite(TimePoint now);
    void tryStartDueWrite();
    void applyCompletion(const SessionWriteCompletion& completion, bool replaced_future_version);
    [[nodiscard]] SessionWriteRequest latestRequest() const;

    SessionWriter& writer_;
    SessionSnapshot latest_{};
    SessionAutosaveOptions options_{};
    SessionRevision latest_revision_{0};
    SessionRevision saved_revision_{0};
    std::optional<SessionRevision> active_revision_;
    std::optional<bool> active_replaces_future_version_;
    std::optional<TimePoint> immediate_due_;
    std::optional<TimePoint> structural_due_;
    std::optional<TimePoint> position_due_;
    std::optional<TimePoint> first_structural_dirty_at_;
    SessionAutosaveNotice notice_{SessionAutosaveNotice::None};
    bool dirty_{false};
    bool replace_future_version_{false};
    bool retry_after_active_{false};
    bool automatic_rearming_suspended_{false};
};

} // namespace music_player::m0
