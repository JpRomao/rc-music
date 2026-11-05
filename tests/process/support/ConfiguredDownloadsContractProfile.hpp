#pragma once

#include "app/PlayerDownloadsBuildProfile.hpp"

namespace music_player::m0 {
NativeHostBuildProfiles configuredDownloadsContractNativeProfiles();
inline NativeHostBuildProfiles nativeHostBuildProfiles() { return configuredDownloadsContractNativeProfiles(); }
std::optional<WindowsPlayerDownloadProfile> configuredDownloadsContractProfile(const QString& root);
inline std::optional<WindowsPlayerDownloadProfile> configuredPlayerDownloadsBuildProfile(const QString& root) {
    return configuredDownloadsContractProfile(root);
}
} // namespace music_player::m0
