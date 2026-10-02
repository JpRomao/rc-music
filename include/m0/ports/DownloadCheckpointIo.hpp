#pragma once

#include "m0/ports/DownloadCheckpointStore.hpp"

#include <cstdint>

namespace music_player::m0 {

enum class DownloadCheckpointIoCommand { Accepted, Busy, Closed, InvalidRequest };
enum class DownloadCheckpointOperation { Load, Save };

struct DownloadCheckpointIoResult final {
    std::uint64_t operation_id{0};
    DownloadCheckpointOperation operation{DownloadCheckpointOperation::Load};
    DownloadCheckpointLoadResult load{};
    DownloadCheckpointSaveStatus save_status{DownloadCheckpointSaveStatus::Failed};
};

// Fast, bounded, application-thread API; no store I/O, waits or callbacks here.
// Exactly one operation including an unobserved completion. IDs are nonzero and
// strictly increase; poll for a stale ID must not consume another completion.
// Borrowed dependencies must survive shutdown and observed stopped().
class DownloadCheckpointIo {
  public:
    virtual ~DownloadCheckpointIo() = default;
    [[nodiscard]] virtual DownloadCheckpointIoCommand startLoad(std::uint64_t operation_id) = 0;
    [[nodiscard]] virtual DownloadCheckpointIoCommand startSave(std::uint64_t operation_id,
                                                                const DownloadCheckpoint& checkpoint) = 0;
    [[nodiscard]] virtual std::optional<DownloadCheckpointIoResult> poll(std::uint64_t operation_id) = 0;
    virtual void shutdown() = 0;
    [[nodiscard]] virtual bool stopped() const = 0;
};

} // namespace music_player::m0
