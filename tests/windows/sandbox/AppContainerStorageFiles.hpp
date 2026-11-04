#pragma once

#include "AppContainerStorageInventory.hpp"
#include "AppContainerStorageSecurity.hpp"

#include <QString>

#include <cstdint>
#include <functional>
#include <memory>

namespace music_player::sandbox_probe {
// Read-only filesystem portion of a future trusted inventory session. Paths
// come from that live creator's captured locator, never CLI/JSON/Loaded data.
// This class grants no creation provenance, mutation or profile authority.
class AppContainerStorageFiles final {
  public:
    using Inventory = AppContainerStorageInventory;
    using Handle = std::uintptr_t;
    struct RootPaths final {
        QString package_root{}, ac{};
    };
    struct Calls final {
        AppContainerStorageSecurity::Calls security{};
        // One attempt per owned native file handle; returns a raw Win32 error.
        std::function<std::uint32_t(Handle)> close_handle{};
        // Trusted test scheduling hook. No authority or native-open override;
        // empty in systemCalls(). Invoked immediately before an owned open.
        std::function<void(const QString& intended_path)> before_open{};
    };
    [[nodiscard]] static Calls systemCalls(); // Binds only, no native I/O.
    explicit AppContainerStorageFiles(RootPaths roots);
    AppContainerStorageFiles(RootPaths roots, Calls calls);
    ~AppContainerStorageFiles();
    AppContainerStorageFiles(const AppContainerStorageFiles&) = delete;
    AppContainerStorageFiles& operator=(const AppContainerStorageFiles&) = delete;

    // One trusted worker, not thread-safe. Reentrant operations refuse BUSY.
    // Only PackageRoot/AC are supported; Registry roots never fall back here.
    [[nodiscard]] Inventory::RootResult root(Inventory::Root root);
    [[nodiscard]] Inventory::Metadata readMetadata(Inventory::NodeRef ref, std::uint64_t maximum_bytes);
    [[nodiscard]] Inventory::ChildResult nextChild(Inventory::NodeRef parent, std::uint32_t index,
                                                   std::uint32_t maximum_units);
    // Confirms captured identities/metadata/name bindings and enumerated names.
    // Changed/Unknown are sticky. This is not an atomic namespace snapshot.
    [[nodiscard]] Inventory::ValidationResult revalidate() noexcept;
    // Closed+error0 acknowledges this adapter's handles only. Destruction never
    // calls close_handle or turns abandoned/uncertain handles into an ACK.
    [[nodiscard]] Inventory::CloseResult close() noexcept;
    [[nodiscard]] Inventory::CloseResult closeResult() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::sandbox_probe
