#pragma once

#include "AppContainerStorageInventory.hpp"

#include <memory>

namespace music_player::sandbox_probe {
// Test-only composition of two trusted read ports, not creation provenance or
// permission to adopt/mutate/clean a profile. Construction binds only.
class AppContainerStorageSession final : public AppContainerStorageInventory::ReadSession {
  public:
    using Inventory = AppContainerStorageInventory;
    static constexpr std::uint32_t maximum_refs = Inventory::maximum_objects + 1;
    static constexpr std::uint32_t maximum_identities = Inventory::maximum_objects;
    AppContainerStorageSession(std::unique_ptr<Inventory::ReadSession> files,
                               std::unique_ptr<Inventory::ReadSession> registry);
    ~AppContainerStorageSession() override;
    AppContainerStorageSession(const AppContainerStorageSession&) = delete;
    AppContainerStorageSession& operator=(const AppContainerStorageSession&) = delete;

    // A single trusted worker only. Reentrant calls return BUSY, do not call
    // either port and do not destroy the active session; not thread-safe.
    [[nodiscard]] Inventory::RootResult root(Inventory::Root root) noexcept override;
    [[nodiscard]] Inventory::Metadata readMetadata(Inventory::NodeRef ref,
                                                   std::uint64_t maximum_bytes) noexcept override;
    [[nodiscard]] Inventory::ChildResult nextChild(Inventory::NodeRef parent, std::uint32_t index,
                                                   std::uint32_t maximum_units) noexcept override;
    [[nodiscard]] Inventory::ValidationResult revalidate() noexcept override;
    // File then Registry; each terminal/unknown result is attempted once.
    // Closed+0 requires both ports to acknowledge. Only aggregate BUSY with no
    // unknown result permits explicit retry of the still-unattempted port.
    // Unknown aggregate conservatively freezes both results, including BUSY.
    // Destruction never calls a port or invents an ACK; uncertain ports stay
    // retained until process exit. Closure covers read resources only.
    [[nodiscard]] Inventory::CloseResult close() noexcept override;
    [[nodiscard]] Inventory::CloseResult closeResult() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::sandbox_probe
