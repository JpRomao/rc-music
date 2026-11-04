#pragma once

#include "m0/ports/DownloadCheckpointStore.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace music_player::m0 {
enum class DownloadMediaProfileStage { Planned, Created, InUse, CleanupPending, Removed, Abandoned };
using DownloadMediaProfileSid = std::array<std::uint8_t, 40>;

// Private adapter evidence, never permission to create/adopt/remove a profile.
// No arbitrary profile name, path, URL or browser data is retained.
struct DownloadMediaProfileRecord final {
    std::string job_id{};
    std::string operation_id{};
    DownloadMediaProfileStage stage{DownloadMediaProfileStage::Planned};
    std::optional<DownloadMediaProfileSid> sid{};
    bool operator==(const DownloadMediaProfileRecord&) const = default;
};

[[nodiscard]] bool validDownloadMediaProfileRecord(const DownloadMediaProfileRecord&) noexcept;
[[nodiscard]] bool mayReplaceDownloadMediaProfileRecord(const DownloadMediaProfileRecord& previous,
                                                        const DownloadMediaProfileRecord& next) noexcept;
// Empty for an invalid UUID; the bounded name is derived, never supplied.
[[nodiscard]] std::string downloadMediaProfileName(std::string_view operation_id);

struct DownloadMediaProfileLoadResult final {
    DownloadCheckpointLoadStatus status{DownloadCheckpointLoadStatus::Missing};
    std::optional<DownloadMediaProfileRecord> record{};
};

// Exclusive live owner of the profile-operation slot, distinct from a short
// document writer lock. Destruction releases admission only, never a profile.
class DownloadMediaProfileLease {
  public:
    virtual ~DownloadMediaProfileLease() = default;
};

// Synchronous worker-only one-slot journal. Pending operations are not evicted.
// A matching replay must perform a new acknowledged write, not a readback.
class DownloadMediaProfileStore {
  public:
    virtual ~DownloadMediaProfileStore() = default;
    [[nodiscard]] virtual std::unique_ptr<DownloadMediaProfileLease> acquireLease() { return {}; }
    [[nodiscard]] virtual DownloadMediaProfileLoadResult load() = 0;
    [[nodiscard]] virtual DownloadCheckpointSaveStatus record(const DownloadMediaProfileRecord&) = 0;
};
} // namespace music_player::m0
