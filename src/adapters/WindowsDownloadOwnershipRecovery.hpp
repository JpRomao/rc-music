#pragma once

#include "adapters/DownloadOwnership.hpp"
#include "adapters/WindowsDownloadWorkspace.hpp"

namespace music_player::m0 {

enum class DownloadOwnershipAssessment { Unverified, OwnedPartial, OwnedFinalCandidate, Missing, Conflict, Unreadable };

// Worker-only, read-only assessment using current trusted parent configuration.
// Results are observations, not authority to delete/import/execute or approval
// of MP3 content. No handle/capability or journal-supplied path escapes this API.
// This does not defend against a malicious same-user process or administrator.
class WindowsDownloadOwnershipRecovery final {
  public:
    [[nodiscard]] static DownloadOwnershipAssessment assess(const WindowsDownloadWorkspaceConfig& config,
                                                            const DownloadOwnershipRecord& record) noexcept;
};

} // namespace music_player::m0
