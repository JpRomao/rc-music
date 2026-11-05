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

// No configured header means no composition. Building optional modules alone
// does not enable admission, native listeners, tools or remote acquisition.
[[nodiscard]] std::optional<WindowsPlayerDownloadProfile> playerDownloadsBuildProfile(const QString& config_root);
} // namespace music_player::m0
