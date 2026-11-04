#pragma once

#include "AppContainerStorageInventory.hpp"

#include <functional>
#include <memory>

namespace music_player::sandbox_probe {
// Test-only orchestration of one inventory observation. This type neither owns
// nor removes profiles. Source is responsible for live creation/fence checks.
class AppContainerStorageInventoryRun final {
  public:
    using Inventory = AppContainerStorageInventory;
    using Recorder = std::function<bool(const Inventory::Report&)>;
    enum class RecordState { NotAttempted, Saved, Failed, Exception, Missing };
    struct Result final {
        Inventory::Report report{};
        // Retains the read scope, including unknown closure. The lease's own
        // explicit-close/terminal-Unknown/no-destructor-ACK contract applies.
        std::shared_ptr<Inventory::ReadLease> lease{};
        Inventory::ValidationResult post_validation{};
        Inventory::CloseResult closure{};
        RecordState record_state{RecordState::NotAttempted};
        bool inspection_called{false}, record_called{false};
        bool post_validation_called{false}, close_called{false};
        // Observation, evidence persistence and read closure are distinct.
        // None is creation provenance or permission to remove a profile.
        bool observed_and_revalidated{false}, read_resources_closed{false};
    };
    AppContainerStorageInventoryRun() = delete;
    // Single trusted worker. Recorder borrows the snapshot only for its call
    // and runs before final revalidation/close, with the read lease still held.
    // Missing Recorder refuses before Source.begin. Failure/throw while saving
    // still reaches explicit close. Refused provenance is never revalidated.
    // No retry, owner close, profile API, serialization or native I/O binding.
    [[nodiscard]] static Result execute(Inventory::Source& source, const Recorder& recorder,
                                        Inventory::Limits limits = {}) noexcept;
};
} // namespace music_player::sandbox_probe
