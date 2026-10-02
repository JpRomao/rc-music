#pragma once

#include "adapters/WindowsDownloadWorkspace.hpp"
#include "m0/ports/DownloadRecoveryReview.hpp"

#include <functional>
#include <memory>

namespace music_player::m0 {

// The production constructor owns detached trusted configuration, not paths
// from a checkpoint/page/QML. All load/assessment and resource teardown occur
// on one private thread, lazily after explicit startReview. No filesystem
// creation, permission repair, saving, cleanup or import is performed here.
class WindowsDownloadRecoveryReview final : public DownloadRecoveryReview {
  public:
    using Work = std::function<DownloadRecoveryReviewOutcome(std::string_view)>;
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
    [[nodiscard]] std::optional<DownloadRecoveryReviewResult> poll(std::uint64_t operation_id) override;
    void shutdown() override;
    [[nodiscard]] bool stopped() const override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
