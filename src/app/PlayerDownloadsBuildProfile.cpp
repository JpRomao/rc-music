#include "PlayerDownloadsBuildProfile.hpp"

#ifdef RC_MUSIC_PLAYER_DOWNLOAD_PROFILE_HEADER
#include RC_MUSIC_PLAYER_DOWNLOAD_PROFILE_HEADER
#endif

namespace music_player::m0 {
std::optional<WindowsPlayerDownloadProfile> playerDownloadsBuildProfile(const QString& config_root) {
#ifdef RC_MUSIC_PLAYER_DOWNLOAD_PROFILE_HEADER
    auto profile = configuredPlayerDownloadsBuildProfile(config_root);
    if (profile && profile->options.native_ingress) {
        const auto& youtube = profile->options.native_ingress->youtube;
        if ((!youtube.allowed_origin.isEmpty() || !youtube.endpoint_name.isEmpty()) && !profile->execution.youtube)
            return std::nullopt;
    }
    return profile;
#else
    static_cast<void>(config_root);
    return std::nullopt;
#endif
}
} // namespace music_player::m0
