#pragma once

#include "adapters/DownloadMediaProfile.hpp"
#include "adapters/WindowsPrivateDownloadDocument.hpp"

namespace music_player::m0 {
using WindowsDownloadMediaProfileFaultPoint = WindowsPrivateDownloadFaultPoint;
using WindowsDownloadMediaProfileStoreOptions = WindowsPrivateDownloadDocumentOptions;

// One bounded, worker-only slot in an existing trusted private directory.
// This stores evidence only: no AppContainer/profile API, adoption, process
// launch or cleanup authority. Active records are never evicted. Failed may
// follow a commit and is never upgraded to Saved by load/readback.
class WindowsDownloadMediaProfileStore final : public DownloadMediaProfileStore {
  public:
    static constexpr qsizetype maximum_file_size = 8 * 1024;
    explicit WindowsDownloadMediaProfileStore(QString directory, WindowsDownloadMediaProfileStoreOptions options = {});
    [[nodiscard]] std::unique_ptr<DownloadMediaProfileLease> acquireLease() override;
    [[nodiscard]] DownloadMediaProfileLoadResult load() override;
    [[nodiscard]] DownloadCheckpointSaveStatus record(const DownloadMediaProfileRecord& record) override;

  private:
    QString directory_;
    WindowsDownloadMediaProfileStoreOptions options_;
};
} // namespace music_player::m0
