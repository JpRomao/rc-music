#include "PlayerDownloadsBuildProfile.hpp"

#ifdef RC_MUSIC_NATIVE_BUILD_PROFILES_HEADER
#include RC_MUSIC_NATIVE_BUILD_PROFILES_HEADER
#endif

#ifdef RC_MUSIC_PLAYER_DOWNLOAD_PROFILE_HEADER
#include RC_MUSIC_PLAYER_DOWNLOAD_PROFILE_HEADER
#endif

#include <algorithm>
#include <utility>

namespace music_player::m0 {
namespace {
bool active(const NativeHostConfiguration& profile) {
    return !profile.allowed_origin.isEmpty() || !profile.endpoint_name.isEmpty();
}
bool sameImageAllowset(const QStringList& left, const QStringList& right) {
    const auto contains = [](const QStringList& images, const QString& expected) {
        return std::any_of(images.cbegin(), images.cend(),
                           [&](const QString& actual) { return actual.compare(expected, Qt::CaseInsensitive) == 0; });
    };
    return std::all_of(left.cbegin(), left.cend(), [&](const QString& image) { return contains(right, image); }) &&
           std::all_of(right.cbegin(), right.cend(), [&](const QString& image) { return contains(left, image); });
}
bool matches(const NativeHostConfiguration& actual, const NativeHostConfiguration& compiled) {
    // A provider may decline a compiled channel. It cannot enable a different
    // identity or broaden either peer's allowed executable image paths.
    return !active(actual) || (active(compiled) && actual.allowed_origin == compiled.allowed_origin &&
                               actual.endpoint_name.compare(compiled.endpoint_name, Qt::CaseInsensitive) == 0 &&
                               sameImageAllowset(actual.expected_owner_images, compiled.expected_owner_images) &&
                               sameImageAllowset(actual.expected_host_images, compiled.expected_host_images));
}
} // namespace

std::optional<WindowsPlayerNativeIngressOptions> compiledPlayerNativeIngressProfiles() {
#ifdef RC_MUSIC_NATIVE_BUILD_PROFILES_HEADER
    auto profiles = nativeHostBuildProfiles();
    if (active(profiles.principal) || active(profiles.youtube))
        return WindowsPlayerNativeIngressOptions{.principal = std::move(profiles.principal),
                                                 .youtube = std::move(profiles.youtube)};
#endif
    return std::nullopt;
}
std::optional<WindowsPlayerDownloadProfile> playerDownloadsBuildProfile(const QString& config_root) {
#ifdef RC_MUSIC_PLAYER_DOWNLOAD_PROFILE_HEADER
    auto profile = configuredPlayerDownloadsBuildProfile(config_root);
    if (profile && profile->options.native_ingress) {
        const auto& actual = *profile->options.native_ingress;
        if (active(actual.youtube) && !profile->execution.youtube)
            return std::nullopt;
        if (!active(actual.principal) && !active(actual.youtube))
            return profile;
        const auto compiled = compiledPlayerNativeIngressProfiles();
        if (!compiled || !matches(actual.principal, compiled->principal) || !matches(actual.youtube, compiled->youtube))
            return std::nullopt;
    }
    return profile;
#else
    static_cast<void>(config_root);
    return std::nullopt;
#endif
}
} // namespace music_player::m0
