#pragma once

#include "AppContainerStorageInventory.hpp"
#include "AppContainerStorageSecurity.hpp"

#include <QString>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>

namespace music_player::sandbox_probe {
// Test-only, read-only Registry tree port. Captured roots are locators from the
// live S_OK creator, not provenance, access grants or profile cleanup authority.
// No CLI/JSON/Loaded-path entry point or Registry mutation API is exposed.
class AppContainerStorageRegistry final : public AppContainerStorageInventory::ReadSession {
  public:
    using Inventory = AppContainerStorageInventory;
    using Handle = std::uintptr_t;
    static constexpr std::uint32_t maximum_native_name_units = 32767;
    static constexpr std::uint32_t maximum_anchors = 512;
    struct RootPaths final {
        QString mappings_key{}, storage_key{};
    };
    enum class OpenState { Unknown, Present, Missing, RejectedLink };
    struct OpenResult final {
        OpenState state{OpenState::Unknown};
        std::uint32_t error{Inventory::not_called};
        Handle key{};
        // All transferred keys, including bootstrap/presence keys and keys
        // returned on errors. No implicit closes. At most two entries; a count
        // above the array extent is refused and ownership remains uncertain.
        std::array<Handle, 2> owned_keys{};
        std::uint32_t owned_count{};
        std::uint32_t ntstatus{Inventory::not_called};
    };
    enum class ReservedValue { Unknown, Absent, Present };
    struct KeyInfo final {
        std::uint32_t error{Inventory::not_called};
        // A bounded captured name, never a persistent/native object identity.
        QString native_name{};
        std::uint32_t subkeys{};
        std::uint64_t last_write{};
        // Only reserved SymbolicLinkValue presence/type/size, never its data.
        // Absent is an observation, not proof of an ordinary key or no ABA.
        ReservedValue reserved{ReservedValue::Unknown};
        std::uint32_t reserved_type{}, reserved_bytes{};
    };
    struct EnumResult final {
        Inventory::ChildState state{Inventory::ChildState::Unknown};
        std::uint32_t error{Inventory::not_called};
        QString name{};
    };
    struct WatchResult final {
        std::uint32_t error{Inventory::not_called};
        Handle event{};
    };
    enum class EventState { Unknown, Unchanged, Changed };
    struct PollResult final {
        EventState state{EventState::Unknown};
        std::uint32_t error{Inventory::not_called};
    };
    struct Calls final {
        // Bind only; the first invocation opens the fixed current-user Software
        // anchor. Implementations transfer owned keys, never predefined HKEYs.
        // With two Present keys, owned_keys[0] is its bootstrap witness and
        // key is owned_keys[1]; both are watched and their names compared.
        std::function<OpenResult()> open_software{};
        // Exactly one relative component, with the retained parent key. A
        // qualified native no-follow binding must refuse links before data reads.
        // Missing is typed only after confirmed absence; Win32 error 2 alone
        // cannot distinguish a missing name from an unqualified dangling link.
        std::function<OpenResult(Handle, const QString&)> open_child{};
        // Bounds apply before native allocation as well as to returned names.
        std::function<KeyInfo(Handle, std::uint32_t maximum_native_units)> query_info{};
        std::function<EnumResult(Handle, std::uint32_t index, std::uint32_t maximum_units)> enum_child{};
        // One-shot watches must be armed before info/enum/security capture.
        // Signaled/unknown watches are never silently rearmed.
        std::function<WatchResult(Handle)> watch_once{};
        std::function<PollResult(Handle)> poll_event{};
        AppContainerStorageSecurity::Calls security{};
        std::function<std::uint32_t(Handle)> close_key{}, close_event{};
    };
    // Construction performs no I/O and deep-copies even fromRawData strings.
    // Both paths must have their fixed AppContainer prefix and the same
    // S-1-15-2 SID with exactly seven canonical decimal uint32 subauthorities.
    AppContainerStorageRegistry(RootPaths roots, Calls calls);
    ~AppContainerStorageRegistry() override;
    AppContainerStorageRegistry(const AppContainerStorageRegistry&) = delete;
    AppContainerStorageRegistry& operator=(const AppContainerStorageRegistry&) = delete;

    // A single trusted worker, not thread-safe. Reentrant calls refuse BUSY
    // without calling the port or replacing the active state.
    [[nodiscard]] Inventory::RootResult root(Inventory::Root root) noexcept override;
    [[nodiscard]] Inventory::Metadata readMetadata(Inventory::NodeRef ref,
                                                   std::uint64_t maximum_bytes) noexcept override;
    [[nodiscard]] Inventory::ChildResult nextChild(Inventory::NodeRef parent, std::uint32_t index,
                                                   std::uint32_t maximum_units) noexcept override;
    // Parent/name/watch/SD/enumeration rechecks, with Changed/Unknown sticky.
    // No atomic snapshot, ABA, RegRestoreKey or persistent object-ID claim.
    [[nodiscard]] Inventory::ValidationResult revalidate() noexcept override;
    // Exactly one attempt per key, in reverse acquisition order, before events.
    // Any key-close failure retains every event. Unknown is terminal, with no
    // retry/destructor close. Closed+0 covers only these reading resources.
    [[nodiscard]] Inventory::CloseResult close() noexcept override;
    [[nodiscard]] Inventory::CloseResult closeResult() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::sandbox_probe
