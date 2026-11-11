#pragma once

#include "m0/ports/DownloadCheckpointStore.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace music_player::m0 {

// Adapter evidence, not a domain media source or permission to act on a path.
struct DownloadFileIdentity final {
    std::uint64_t volume_serial{};
    std::array<std::uint8_t, 16> file_id{};
    friend bool operator==(const DownloadFileIdentity&, const DownloadFileIdentity&) = default;
};

enum class DownloadOwnershipStage { Planned, WorkspaceReady, PublicationPrepared, Published };

struct DownloadOwnershipRecord final {
    std::string job_id{};
    std::string workspace_id{}; // Locally generated canonical UUID v4, never a supplied filename.
    DownloadOwnershipStage stage{DownloadOwnershipStage::Planned};
    DownloadFileIdentity staging_parent{}, publish_parent{};
    std::optional<DownloadFileIdentity> job_directory{}, input{}, output{};
    std::uint64_t maximum_workspace_bytes{512ULL * 1024 * 1024};
    std::uint64_t input_bytes{}, output_bytes{};
    std::string output_sha256{};
    bool cleanup_complete{false};
    friend bool operator==(const DownloadOwnershipRecord&, const DownloadOwnershipRecord&) = default;
};

struct DownloadOwnershipJournal final {
    static constexpr std::size_t maximum_records = 128;
    std::vector<DownloadOwnershipRecord> records{};
    friend bool operator==(const DownloadOwnershipJournal&, const DownloadOwnershipJournal&) = default;
};

[[nodiscard]] bool validDownloadOwnershipRecord(const DownloadOwnershipRecord&) noexcept;
[[nodiscard]] bool validDownloadOwnershipJournal(const DownloadOwnershipJournal&) noexcept;
[[nodiscard]] bool mayReplaceDownloadOwnershipRecord(const DownloadOwnershipRecord& previous,
                                                     const DownloadOwnershipRecord& next) noexcept;

struct DownloadOwnershipLoadResult final {
    DownloadCheckpointLoadStatus status{DownloadCheckpointLoadStatus::Missing};
    std::optional<DownloadOwnershipJournal> journal{};
};

// Synchronous, worker-only. record is an atomic bounded upsert of one record,
// never an eviction. Unknown data and identity/state regressions are preserved.
// Failed after commit is ambiguous and must not be promoted by readback.
class DownloadOwnershipStore {
  public:
    virtual ~DownloadOwnershipStore() = default;
    [[nodiscard]] virtual DownloadOwnershipLoadResult load() = 0;
    [[nodiscard]] virtual DownloadCheckpointSaveStatus record(const DownloadOwnershipRecord&) = 0;
    // Rewrite only an exact, already-cleaned row. Recheck the physical proof
    // under the writer lock, and never create a missing row during a retry.
    [[nodiscard]] virtual DownloadCheckpointSaveStatus acknowledgeCleanedRecord(const DownloadOwnershipRecord&,
                                                                                const std::function<bool()>&) {
        return DownloadCheckpointSaveStatus::Failed;
    }
};

} // namespace music_player::m0
