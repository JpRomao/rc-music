#pragma once

#include "AppContainerStorageRegistry.hpp"

namespace music_player::sandbox_probe {
// Test-only Windows binding. Construction binds calls and allocates bookkeeping;
// it does not open keys, create events or close resources. One trusted worker.
// Every acquired key/event is transferred to the reader for explicit closure.
// Native names are captured locators, never persistent object identities.
// OPEN_LINK protects the fixed immediate Software component; the predefined
// HKCU_LOCAL_SETTINGS resolution is a Windows locator, not a no-redirection or
// provenance certificate. Native reopen uses absolute name + DONT_REPARSE.
// Reg* query/notify/security on Native keys still requires separate runtime
// qualification. The known Classes-link preflight is no universal link proof.
// Name/watch comparisons promise no atomic snapshot, ABA or RegRestoreKey
// detection. Refusal statuses do not themselves prove a physical key type.
[[nodiscard]] AppContainerStorageRegistry::Calls appContainerRegistryNativeCalls();
} // namespace music_player::sandbox_probe
