#pragma once

#include "adapters/WindowsConfiguredDownloadExecution.hpp"
#include "presentation/WindowsPlayerDownloads.hpp"

#include <QString>

#include <optional>

namespace music_player::m0 {
// Reviewed C++ build composition only. Paths must already exist; neither the
// player nor this provider creates directories, repairs ACLs or discovers tools.
// Borrowed option dependencies must outlive the player download owner.
struct WindowsPlayerDownloadProfile final {
    WindowsDownloadExecutionProfile execution{};
    QString checkpoint_directory{};
    WindowsPlayerDownloadsOptions options{};
};

// Trusted compiled Native identities only. Missing support or two disabled
// channels returns nullopt. Providers may use this directly without repeating
// origins/endpoints/image allowlists; peer-check callbacks remain unconfigured.
[[nodiscard]] std::optional<WindowsPlayerNativeIngressOptions> compiledPlayerNativeIngressProfiles();

// No configured header means no composition. Building optional modules alone
// does not enable admission, native listeners, tools or remote acquisition.
// Active provider channels must match the compiled identities. Origins compare
// exactly; pipe names and image allowsets ignore case (and image ordering).
// A compiled active channel may be omitted by the provider as an opt-out.
[[nodiscard]] std::optional<WindowsPlayerDownloadProfile> playerDownloadsBuildProfile(const QString& config_root);
} // namespace music_player::m0
