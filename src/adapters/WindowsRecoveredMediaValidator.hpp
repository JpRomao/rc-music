#pragma once

#include "adapters/WindowsDownloadOwnershipRecovery.hpp"
#include "adapters/WindowsLocalMp3Validator.hpp"

#include <memory>

namespace music_player::m0 {
enum class RecoveredMediaValidationState { Idle, Validating, Stopping, Validated, Failed, Cancelled };
struct RecoveredMediaValidationUpdate final {
    RecoveredMediaValidationState state{RecoveredMediaValidationState::Idle};
    DownloadOwnershipAssessment ownership{DownloadOwnershipAssessment::Unverified};
    DownloadFailure error{DownloadFailure::None};
};

// Slow, worker-only composition on one Qt event-loop thread. Trusted current
// parents and reviewed local tools only; never page paths, UI calls or URLs.
// Fresh ownership pins survive probe/decode, physical stop and pipe drain.
// A successful candidate remains pinned for the owner's deferred handoff.
// This validates media, NOT durable publication/import. PublicationPrepared is
// never rewritten or promoted to Published/Saved. No journal or media writes.
// Owners poll until terminal before destruction; destructor fallback may block
// on the worker, and must never substitute for normal drain on the UI thread.
class WindowsRecoveredMediaValidator final {
  public:
    WindowsRecoveredMediaValidator(WindowsDownloadWorkspaceConfig workspace, WindowsLocalMp3ValidatorConfig tools);
    ~WindowsRecoveredMediaValidator();
    WindowsRecoveredMediaValidator(const WindowsRecoveredMediaValidator&) = delete;
    WindowsRecoveredMediaValidator& operator=(const WindowsRecoveredMediaValidator&) = delete;
    [[nodiscard]] bool start(const DownloadOwnershipRecord& record, qint64 now_ms);
    void cancel();
    [[nodiscard]] RecoveredMediaValidationUpdate poll(qint64 now_ms);
    // Borrowed while this worker-owned object lives. No QML/serialization. A
    // delayed handoff must freshly revalidate; retain this owner through Saved.
    [[nodiscard]] const WindowsPinnedDownloadFinal* validatedCandidate() const;

  private:
    void stop(DownloadFailure error, bool cancelled = false);
    void finish(RecoveredMediaValidationState state, DownloadFailure error);
    WindowsDownloadWorkspaceConfig workspace_;
    WindowsLocalMp3ValidatorConfig tools_;
    RecoveredMediaValidationUpdate update_{};
    qint64 started_ms_{0};
    qint64 previous_ms_{0};
    bool launch_pending_{false};
    bool cancel_requested_{false};
    // Destruction order is deliberate: drain process before releasing pins.
    std::unique_ptr<WindowsPinnedDownloadFinal> candidate_{};
    std::unique_ptr<WindowsLocalMp3Validator> validator_{};
};
} // namespace music_player::m0
