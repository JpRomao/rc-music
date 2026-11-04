#pragma once

#include "AppContainerStorageInventory.hpp"
#include "adapters/WindowsMediaProfileOwner.hpp"
#include "adapters/WindowsNativeMediaProfile.hpp"

#include <functional>
#include <memory>

namespace music_player::sandbox_probe {
// Probe-only bridge. Borrowed owner/backend outlive this source AND its leases.
// The factory only allocates/binds a session; native I/O starts in its methods.
// Its locator argument is borrowed for that call only; the session must own
// copies of any fields it retains.
// Neither a captured locator DTO nor a loaded journal can mint this source.
class AppContainerStorageLiveSource final : public AppContainerStorageInventory::Source {
  public:
    using Factory = std::function<std::unique_ptr<AppContainerStorageInventory::ReadSession>(
        const music_player::m0::WindowsNativeMediaProfile::StorageLocation&)>;
    AppContainerStorageLiveSource(music_player::m0::WindowsMediaProfileOwner& owner,
                                  music_player::m0::WindowsNativeMediaProfile& backend, Factory factory);
    ~AppContainerStorageLiveSource();
    AppContainerStorageLiveSource(const AppContainerStorageLiveSource&) = delete;
    AppContainerStorageLiveSource& operator=(const AppContainerStorageLiveSource&) = delete;
    [[nodiscard]] AppContainerStorageInventory::BeginResult begin() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::sandbox_probe
