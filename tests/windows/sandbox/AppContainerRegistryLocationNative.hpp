#pragma once

#include "AppContainerRegistryLocationQuery.hpp"

namespace music_player::sandbox_probe {
// Bind only. No token or Registry call is made until the returned port is used.
// One helper invocation owns this port; ambiguous native outputs are retained
// until process exit, without retries or destructor-based acknowledgements.
[[nodiscard]] AppContainerRegistryLocationQuery::Calls appContainerRegistryLocationNativeCalls();
} // namespace music_player::sandbox_probe
