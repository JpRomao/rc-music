#pragma once

#include "AppContainerStorageFiles.hpp"
#include "AppContainerStorageRegistry.hpp"
#include "adapters/WindowsNativeMediaProfile.hpp"

#include <memory>

namespace music_player::sandbox_probe {
// Test-only factory for LiveSource. A locator supplies names only; the live
// creator/fence checks remain in LiveSource. Binding performs no native I/O.
// All retained path fields are copied, including fromRawData QString buffers.
[[nodiscard]] std::unique_ptr<AppContainerStorageInventory::ReadSession>
appContainerStorageNativeSession(const music_player::m0::WindowsNativeMediaProfile::StorageLocation& location,
                                 AppContainerStorageFiles::Calls files, AppContainerStorageRegistry::Calls registry);
[[nodiscard]] std::unique_ptr<AppContainerStorageInventory::ReadSession>
appContainerStorageNativeSession(const music_player::m0::WindowsNativeMediaProfile::StorageLocation& location);
} // namespace music_player::sandbox_probe
