#pragma once

#include "adapters/DownloadOwnership.hpp"
#include "adapters/WindowsDownloadWorkspace.hpp"

namespace music_player::m0 {

enum class DownloadOwnershipAssessment { Unverified, OwnedPartial, OwnedFinalCandidate, Missing, Conflict, Unreadable };

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

// Worker-only recovery using current trusted parent configuration. Assessment
// observations never authorize deletion/import/execution or approve MP3 media;
// explicit discard must acquire its own fresh proof. No handle/capability or
// journal-supplied path escapes this API.
// This does not defend against a malicious same-user process or administrator.
class WindowsDownloadOwnershipRecovery final {
  public:
    [[nodiscard]] static DownloadOwnershipAssessment assess(const WindowsDownloadWorkspaceConfig& config,
                                                            const DownloadOwnershipRecord& record) noexcept;
    // Separate explicit worker-only action: freshly verify the entire workspace,
    // discard only owned temporaries by handle, and confirm cleanup in the store.
    // Never deletes a final candidate or repairs/creates any filesystem object.
    [[nodiscard]] static DownloadOwnershipDiscardOutcome discardPartials(const WindowsDownloadWorkspaceConfig& config,
                                                                         const DownloadOwnershipRecord& record,
                                                                         DownloadOwnershipStore& store) noexcept;
};

} // namespace music_player::m0
