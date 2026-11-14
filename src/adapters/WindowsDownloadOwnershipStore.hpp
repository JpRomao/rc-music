#pragma once

#include "adapters/DownloadOwnership.hpp"
#include "adapters/WindowsPrivateDownloadDocument.hpp"

namespace music_player::m0 {

using WindowsDownloadOwnershipFaultPoint = WindowsPrivateDownloadFaultPoint;
using WindowsDownloadOwnershipStoreOptions = WindowsPrivateDownloadDocumentOptions;

// Synchronous worker-only metadata store in an existing app-owned private
// directory. Its paths and fixed filenames come only from trusted composition.
// Unknown data, conflicting identities and regressions are never replaced.
// A record is evidence, not permission to execute, import or remove media.
// Failed may follow commit; it is not promoted to Saved by load/readback.
class WindowsDownloadOwnershipStore final : public DownloadOwnershipStore {
  public:
    static constexpr qsizetype maximum_file_size = 256 * 1024;
    explicit WindowsDownloadOwnershipStore(QString config_directory, WindowsDownloadOwnershipStoreOptions options = {});
    [[nodiscard]] DownloadOwnershipLoadResult load() override;
    [[nodiscard]] DownloadCheckpointSaveStatus record(const DownloadOwnershipRecord& record) override;
    [[nodiscard]] DownloadCheckpointSaveStatus
    acknowledgeCleanedRecord(const DownloadOwnershipRecord& expected,
                             const std::function<bool()>& still_absent) override;
    [[nodiscard]] DownloadCheckpointSaveStatus markPlannedCleaned(const DownloadOwnershipRecord& expected,
                                                                  const std::function<bool()>& still_absent) override;

  private:
    friend class WindowsDownloadOwnershipRecovery;
    // Compare the exact settled record and recheck its pinned proof while the
    // journal is locked. Only a full journal can release one record.
    [[nodiscard]] DownloadCheckpointSaveStatus reclaimAtCapacity(const DownloadOwnershipRecord& expected,
                                                                 const std::function<bool()>& still_reclaimable);
    [[nodiscard]] DownloadCheckpointSaveStatus retireUnpublishedAttempt(const DownloadOwnershipRecord& expected,
                                                                        const std::function<bool()>& still_absent);
    enum class CleanedRecordChange { Acknowledge, MarkPlannedCleaned, ReclaimAtCapacity, RetireUnpublishedAttempt };
    [[nodiscard]] DownloadCheckpointSaveStatus changeCleanedRecord(const DownloadOwnershipRecord& expected,
                                                                   const std::function<bool()>& still_verified,
                                                                   CleanedRecordChange change);
    QString directory_;
    WindowsDownloadOwnershipStoreOptions options_;
};

} // namespace music_player::m0
