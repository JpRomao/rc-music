#pragma once

#include "adapters/WindowsDownloadWorkspace.hpp"
#include "m0/ports/DownloadRecoveryReview.hpp"

#include <functional>
#include <memory>

namespace music_player::m0 {

// The production constructor owns detached trusted configuration, not paths
// from a checkpoint/page/QML. A private thread exists after construction, but
// factory/load/actions run only after an explicit operation. Work/factory
// resources are destroyed there before stopped. Review is read-only;
// discard freshly verifies and removes only owned partials before acknowledging
// the journal. Confirming an absent Planned job only revalidates the absence and
// acknowledges the journal. No creation, permission repair or import.
class WindowsDownloadRecoveryReview final : public DownloadRecoveryReview {
  public:
    using Work = std::function<DownloadRecoveryReviewOutcome(DownloadRecoveryReviewAction, std::string_view)>;
    using Factory = std::function<Work()>;
    WindowsDownloadRecoveryReview(QString journal_directory, WindowsDownloadWorkspaceConfig config);
    // Deterministic contract seam. Factory/work/captures are private-worker
    // owned; callbacks may block there, never in start/poll/shutdown/stopped.
    explicit WindowsDownloadRecoveryReview(Factory factory);
    ~WindowsDownloadRecoveryReview() override;
    WindowsDownloadRecoveryReview(const WindowsDownloadRecoveryReview&) = delete;
    WindowsDownloadRecoveryReview& operator=(const WindowsDownloadRecoveryReview&) = delete;
    [[nodiscard]] DownloadRecoveryReviewCommand startReview(std::uint64_t operation_id,
                                                            std::string_view job_id) override;
    [[nodiscard]] bool supportsDiscardPartials() const override { return true; }
    [[nodiscard]] DownloadRecoveryReviewCommand startDiscardPartials(std::uint64_t operation_id,
                                                                     std::string_view job_id) override;
    [[nodiscard]] DownloadRecoveryReviewCommand startConfirmPlannedWithoutFiles(std::uint64_t operation_id,
                                                                                std::string_view job_id) override;
    [[nodiscard]] std::optional<DownloadRecoveryReviewResult> poll(std::uint64_t operation_id) override;
    void shutdown() override;
    [[nodiscard]] bool stopped() const override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
