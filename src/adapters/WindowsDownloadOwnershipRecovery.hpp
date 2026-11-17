#pragma once

#include "adapters/DownloadOwnership.hpp"
#include "adapters/WindowsDownloadWorkspace.hpp"

#include <memory>
#include <string_view>

namespace music_player::m0 {

struct ArchivedDownloadJobEvidence;
class WindowsDownloadOwnershipStore;

enum class DownloadOwnershipAssessment { Unverified, OwnedPartial, OwnedFinalCandidate, Missing, Conflict, Unreadable };
enum class DownloadOwnershipCapacity {
    Available,
    Full,
    HistoryUnavailable,
    ReclaimUnconfirmed,
    PriorAttemptUnresolved
};

enum class DownloadOwnershipDiscardOutcome {
    Discarded,
    Missing,
    Unverified,
    OwnedFinalCandidate,
    Conflict,
    Unreadable,
    Incomplete,
    Unconfirmed
};

// Worker-only, read-only lifetime capability. Owns all ancestor/workspace/file
// pins; its path is only input to a trusted local media validator, never proof
// that MP3 is valid or permission to import/delete/publish. No QObject/QML API.
class WindowsPinnedDownloadFinal final {
  public:
    ~WindowsPinnedDownloadFinal();
    WindowsPinnedDownloadFinal(const WindowsPinnedDownloadFinal&) = delete;
    WindowsPinnedDownloadFinal& operator=(const WindowsPinnedDownloadFinal&) = delete;
    [[nodiscard]] QString pathForValidation() const;
    // Read only the already-pinned final object, without reopening its name.
    // Full recovery assessment and media/import approval remain separate gates.
    [[nodiscard]] std::shared_ptr<const WindowsDownloadReadInput> readInput() const;
    [[nodiscard]] const DownloadOwnershipRecord& record() const;
    [[nodiscard]] DownloadOwnershipAssessment revalidate() const noexcept;

  private:
    struct Impl;
    explicit WindowsPinnedDownloadFinal(std::unique_ptr<Impl> impl);
    friend class WindowsDownloadOwnershipRecovery;
    std::unique_ptr<Impl> impl_;
};

struct DownloadFinalPinResult final {
    DownloadOwnershipAssessment assessment{DownloadOwnershipAssessment::Unverified};
    std::unique_ptr<WindowsPinnedDownloadFinal> candidate{};
};

// Worker-only recovery using current trusted parent configuration. Assessment
// observations never authorize deletion/import/execution or approve MP3 media;
// explicit discard must acquire its own fresh proof. Assessment/discard expose
// no handles or paths. pinFinal separately retains the fresh proof for trusted
// worker-only validation; no journal-supplied path is accepted or exported.
// This does not defend against a malicious same-user process or administrator.
class WindowsDownloadOwnershipRecovery final {
  public:
    // Reuse a job ID only after proving the prior attempt left no files.
    // Otherwise retire proven-clean archived rows until `headroom` rows are
    // free (one per parallel lane, at most parallel_reserve). Callers in this
    // process take turns, so two lanes never race for the same row.
    [[nodiscard]] static DownloadOwnershipCapacity ensureCapacity(const WindowsDownloadWorkspaceConfig& config,
                                                                  WindowsDownloadOwnershipStore& store,
                                                                  const ArchivedDownloadJobEvidence& evidence,
                                                                  std::string_view job_id = {},
                                                                  std::size_t headroom = 1) noexcept;
    // Only the prior-attempt part of ensureCapacity, for a new attempt inside
    // the same job: Available when there is no row for job_id or it was a
    // proven-clean unpublished attempt that is now retired.
    [[nodiscard]] static DownloadOwnershipCapacity retirePriorAttempt(const WindowsDownloadWorkspaceConfig& config,
                                                                      WindowsDownloadOwnershipStore& store,
                                                                      std::string_view job_id) noexcept;
    [[nodiscard]] static DownloadOwnershipAssessment assess(const WindowsDownloadWorkspaceConfig& config,
                                                            const DownloadOwnershipRecord& record) noexcept;
    // Read-only review of a cleaned record under freshly pinned configured roots.
    [[nodiscard]] static bool cleanedWithoutFiles(const WindowsDownloadWorkspaceConfig& config,
                                                  const DownloadOwnershipRecord& record) noexcept;
    // Planned rows supply names and root identities, but own no physical files.
    [[nodiscard]] static bool plannedWithoutFiles(const WindowsDownloadWorkspaceConfig& config,
                                                  const DownloadOwnershipRecord& record) noexcept;
    // Recheck absence and acknowledge the exact Planned row. Never deletes files.
    [[nodiscard]] static DownloadOwnershipDiscardOutcome
    confirmPlannedWithoutFiles(const WindowsDownloadWorkspaceConfig& config, const DownloadOwnershipRecord& record,
                               DownloadOwnershipStore& store) noexcept;
    [[nodiscard]] static DownloadFinalPinResult pinFinal(const WindowsDownloadWorkspaceConfig& config,
                                                         const DownloadOwnershipRecord& record) noexcept;
    // Separate explicit worker-only action: freshly verify the entire workspace,
    // discard only owned temporaries by handle, and confirm cleanup in the store.
    // Never deletes a final candidate or repairs/creates any filesystem object.
    [[nodiscard]] static DownloadOwnershipDiscardOutcome discardPartials(const WindowsDownloadWorkspaceConfig& config,
                                                                         const DownloadOwnershipRecord& record,
                                                                         DownloadOwnershipStore& store) noexcept;

  private:
    [[nodiscard]] static DownloadOwnershipCapacity retirePrior(const WindowsDownloadWorkspaceConfig& config,
                                                               WindowsDownloadOwnershipStore& store,
                                                               const DownloadOwnershipRecord& prior);
};

} // namespace music_player::m0
