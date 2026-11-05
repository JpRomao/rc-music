#pragma once

void configuredDownloadsProfileObserved();

namespace music_player::m0 {
inline std::optional<WindowsPlayerDownloadProfile> configuredPlayerDownloadsBuildProfile(const QString&) {
    configuredDownloadsProfileObserved();
    return std::nullopt;
}
} // namespace music_player::m0
