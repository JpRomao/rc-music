#pragma once

#include "m0/application/DownloadCheckpoint.hpp"

namespace music_player::m0 {

enum class DownloadCheckpointLoadStatus { Missing, Loaded, Invalid, Unreadable, FutureVersion };
enum class DownloadCheckpointSaveStatus { Saved, Failed, BlockedByExistingData };

struct DownloadCheckpointLoadResult final {
    std::optional<DownloadCheckpoint> checkpoint{};
    DownloadCheckpointLoadStatus status{DownloadCheckpointLoadStatus::Missing};
};

// Synchronous storage belongs exclusively to a worker. No fallback replacement
// of unknown data; Failed may follow a commit and must never become Saved by readback.
class DownloadCheckpointStore {
  public:
    virtual ~DownloadCheckpointStore() = default;
    [[nodiscard]] virtual DownloadCheckpointLoadResult load() = 0;
    [[nodiscard]] virtual DownloadCheckpointSaveStatus save(const DownloadCheckpoint& checkpoint) = 0;
};

} // namespace music_player::m0
