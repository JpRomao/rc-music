#pragma once

#include "m0/application/DownloadService.hpp"
#include "m0/ports/AsyncDownloadSubmission.hpp"
#include "m0/ports/DownloadCheckpointIo.hpp"

namespace music_player::m0 {

enum class DownloadAdmissionControllerState {
    NotStarted,
    Loading,
    Ready,
    AdmissionPending,
    AdmissionFailed,
    Blocked,
    Stopping,
    Stopped
};

// Single application-thread owner of admission, restoration and checkpoints.
// Trusted job controls may use the service on that same thread, never submit or
// restore behind this owner. Ports cannot reenter service mutations directly.
// Borrowed service/IO/ports outlive shutdown/drain. No destructor I/O or join.
// Admission and final shutdown checkpoints are persisted; this is not a media
// ownership journal, queue+import transaction or automatic recovery/retry.
class DownloadAdmissionController final : public AsyncDownloadSubmission {
  public:
    DownloadAdmissionController(DownloadService& service, DownloadCheckpointIo& io);
    ~DownloadAdmissionController() override;
    DownloadAdmissionController(const DownloadAdmissionController&) = delete;
    DownloadAdmissionController& operator=(const DownloadAdmissionController&) = delete;
    [[nodiscard]] bool start();
    void tick(std::int64_t now_ms);
    [[nodiscard]] DownloadAdmissionResult begin(const DownloadRequest& request) override;
    [[nodiscard]] std::optional<DownloadSubmissionResult> poll(std::string_view request_id) override;
    [[nodiscard]] bool retryAdmission();
    [[nodiscard]] bool retryStartup();
    void shutdown();
    // Physical service/IO drain, NOT successful persistence. The application
    // checks saveStatus() separately before approving its own normal exit.
    [[nodiscard]] bool stopped() const;
    [[nodiscard]] DownloadAdmissionControllerState state() const;
    [[nodiscard]] std::optional<DownloadCheckpointLoadStatus> loadStatus() const;
    [[nodiscard]] std::optional<DownloadCheckpointSaveStatus> saveStatus() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace music_player::m0
