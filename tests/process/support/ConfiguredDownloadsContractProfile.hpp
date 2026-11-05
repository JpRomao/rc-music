#pragma once

namespace music_player::m0 {
std::optional<WindowsPlayerDownloadProfile> configuredDownloadsContractProfile(const QString& root);
inline std::optional<WindowsPlayerDownloadProfile> configuredPlayerDownloadsBuildProfile(const QString& root) {
    return configuredDownloadsContractProfile(root);
}
} // namespace music_player::m0
