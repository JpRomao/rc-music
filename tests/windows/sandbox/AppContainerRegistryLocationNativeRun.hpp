#pragma once

#include "AppContainerRegistryLocationRun.hpp"

#include <memory>

namespace music_player::m0 {
class WindowsMediaFoundationLaunch;
}

namespace music_player::sandbox_probe {
// Bind only to an already prepared, pinned runtime. Construction neither
// starts a process nor acquires a profile/fence ticket. Trusted composition
// owns the source-image pin and the live S_OK owner's exact capability.
//
// Before start, reject a missing/changed runtime, different runtime paths,
// environment/capability or an inherited read input. Such refusal publishes
// Failed/InvalidConfiguration without calling the native process or changing
// the fence. The Runner fixes the request, arguments and resource limits.
//
// The port retains its process and runtime, destroying the process first.
// A terminal update alone is not a stop ACK: the Runner also checks the exact
// capability's fence. It retains this port on uncertainty, without destructor
// cleanup or an inferred profile-removal acknowledgement.
[[nodiscard]] std::shared_ptr<AppContainerRegistryLocationRun::Port>
appContainerRegistryLocationNativeRun(std::shared_ptr<const music_player::m0::WindowsMediaFoundationLaunch> runtime);
} // namespace music_player::sandbox_probe
