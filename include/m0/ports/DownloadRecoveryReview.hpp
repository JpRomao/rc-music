#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace music_player::m0 {

enum class DownloadRecoveryReviewOutcome {
    Unverified,
    OwnedPartial,
    OwnedFinalCandidate,
    Missing,
    Conflict,
    Unreadable,
    HistoryMissing,
    HistoryInvalid,
    HistoryFuture,
    Discarded,
    DiscardIncomplete,
    DiscardUnconfirmed,
    CleanupCandidate,    // Read-only: cleanup was recorded and no expected workspace or publication remains.
    PlannedWithoutFiles, // Read-only: a Planned row has neither its expected workspace nor its publication.
    PlannedClosureUnconfirmed
};
enum class DownloadRecoveryReviewAction { ReviewFiles, DiscardPartials, ConfirmPlannedWithoutFiles };
enum class DownloadRecoveryReviewCommand { Accepted, Busy, Closed, InvalidRequest };
struct DownloadRecoveryReviewResult final {
    std::uint64_t operation_id{};
    std::string job_id{};
    DownloadRecoveryReviewOutcome outcome{DownloadRecoveryReviewOutcome::Unreadable};
    DownloadRecoveryReviewAction action{DownloadRecoveryReviewAction::ReviewFiles};
};

// Fast application-thread port. One slot including an unobserved completion;
// nonzero, strictly increasing operation IDs. Wrong poll IDs consume nothing.
// Only a bounded trusted job ID is accepted, never a path or page-supplied name.
// Observations confer no authority to import, delete, resume or execute media.
// Explicit discard is separate from an observation: implementations must freshly
// verify trusted ownership and never delete a final artifact. Discarded requires
// physical cleanup or verified absence, plus the matching durable journal
// acknowledgement. The Planned confirmation performs no file removal. Failed
// or uncertain cleanup must remain typed, not be promoted by a later readback.
// All operations share the slot; every result repeats its exact action.
// shutdown closes admission without waiting; stopped acknowledges all private
// resources drained. Borrowed owners survive that acknowledgement. No I/O in
// a destructor is a substitute for this protocol on the application thread.
class DownloadRecoveryReview {
  public:
    virtual ~DownloadRecoveryReview() = default;
    [[nodiscard]] virtual DownloadRecoveryReviewCommand startReview(std::uint64_t operation_id,
                                                                    std::string_view job_id) = 0;
    [[nodiscard]] virtual bool supportsDiscardPartials() const { return false; }
    [[nodiscard]] virtual DownloadRecoveryReviewCommand startDiscardPartials(std::uint64_t, std::string_view) {
        return DownloadRecoveryReviewCommand::InvalidRequest;
    }
    [[nodiscard]] virtual DownloadRecoveryReviewCommand startConfirmPlannedWithoutFiles(std::uint64_t,
                                                                                        std::string_view) {
        return DownloadRecoveryReviewCommand::InvalidRequest;
    }
    [[nodiscard]] virtual std::optional<DownloadRecoveryReviewResult> poll(std::uint64_t operation_id) = 0;
    virtual void shutdown() = 0;
    [[nodiscard]] virtual bool stopped() const = 0;
};
} // namespace music_player::m0
