#pragma once

#include "m0/application/DownloadService.hpp"
#include "m0/ports/AsyncDownloadSubmission.hpp"
#include "m0/ports/DownloadCheckpointIo.hpp"
#include "m0/ports/SessionStore.hpp"

namespace music_player::m0 {

enum class DownloadAdmissionControllerState {
    NotStarted,
    Loading,
    Ready,
    AdmissionPending,
    AdmissionFailed,
    Blocked,
    Stopping,
    Stopped,
    FinalSaveFailed
};

enum class DownloadFinalSavePolicy { CloseAfterAttempt, HoldForDecision };
enum class DownloadStartupRecoveryAction { None, RetryCheckpoint, ReloadSession };

// Single application-thread owner of admission, restoration and checkpoints.
// Trusted job controls may use the service on that same thread, never submit or
// restore behind this owner. Ports cannot reenter service mutations directly.
// Borrowed service/IO/ports outlive shutdown/drain. No destructor I/O or join.
// Admission and final shutdown checkpoints are persisted; this is not a media
// ownership journal, queue+import transaction or automatic recovery/retry.
class DownloadAdmissionController final : public AsyncDownloadSubmission {
  public:
    DownloadAdmissionController(DownloadService& service, DownloadCheckpointIo& io,
                                DownloadFinalSavePolicy final_save_policy = DownloadFinalSavePolicy::CloseAfterAttempt);
    // Durable startup composition: copy only a bounded receipt ledger from a
    // valid Loaded primary or an exactly empty Missing session. Backup/defaults/
    // future history blocks ingress; retrying download I/O cannot upgrade it.
    // This is store evidence, never the current unsaved presenter snapshot.
    DownloadAdmissionController(DownloadService& service, DownloadCheckpointIo& io,
                                const SessionLoadResult& initial_session,
                                DownloadFinalSavePolicy final_save_policy = DownloadFinalSavePolicy::CloseAfterAttempt);
    ~DownloadAdmissionController() override;
    DownloadAdmissionController(const DownloadAdmissionController&) = delete;
    DownloadAdmissionController& operator=(const DownloadAdmissionController&) = delete;
    [[nodiscard]] bool start();
    void tick(std::int64_t now_ms);
    [[nodiscard]] DownloadAdmissionResult begin(const DownloadRequest& request) override;
    [[nodiscard]] std::optional<DownloadSubmissionResult> poll(std::string_view request_id) override;
    [[nodiscard]] bool retryAdmission();
    // The startup session ledger is a detached, immutable snapshot. Retrying
    // checkpoint I/O cannot replace untrusted session evidence in this owner.
    [[nodiscard]] DownloadStartupRecoveryAction startupRecoveryAction() const;
    [[nodiscard]] bool retryStartup();
    // HoldForDecision only, after physical service drain. Retry saves the final
    // history, never confirms a failed admission or restarts/imports its jobs.
    [[nodiscard]] bool retryFinalSave();
    [[nodiscard]] bool closeAfterFinalSaveFailure();
    void shutdown();
    // Physical service/IO drain, NOT successful persistence. The application
    // checks saveStatus() separately before approving its own normal exit.
    [[nodiscard]] bool stopped() const;
    [[nodiscard]] DownloadAdmissionControllerState state() const;
    [[nodiscard]] std::optional<DownloadCheckpointLoadStatus> loadStatus() const;
    [[nodiscard]] std::optional<DownloadCheckpointSaveStatus> saveStatus() const;
    // Separate from file loadStatus: valid documents may still conflict. No
    // URLs/media paths or executable recovery actions are exposed in this report.
    [[nodiscard]] DownloadRecoveryReport recoveryReport() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace music_player::m0
