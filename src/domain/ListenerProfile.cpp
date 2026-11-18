#include "m0/domain/ListenerProfile.hpp"

#include "m0/domain/ListeningRoomLimits.hpp"
#include "m0/domain/MetadataSanitization.hpp"

#include <algorithm>

namespace music_player::m0 {
namespace {

bool lowerHex64(const std::string& value) noexcept {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](const char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

} // namespace

std::optional<std::string> canonicalNickname(const std::string_view raw) {
    const auto first = raw.find_first_not_of(' ');
    if (first == std::string_view::npos)
        return std::nullopt;
    const auto trimmed = raw.substr(first, raw.find_last_not_of(' ') - first + 1);
    // Unchanged means valid UTF-8 and no controls, separators other than plain spaces, or layout marks.
    if (sanitizeMetadataText(trimmed).disposition != MetadataTextDisposition::Unchanged)
        return std::nullopt;
    const auto code_points = static_cast<std::size_t>(std::count_if(
        trimmed.begin(), trimmed.end(), [](const char c) { return (static_cast<unsigned char>(c) & 0xc0U) != 0x80U; }));
    if (code_points > maximum_nickname_code_points)
        return std::nullopt;
    return std::string{trimmed};
}

AvatarChoice initialAvatar(const std::uint64_t random_value) noexcept {
    return {AvatarKind::Bundled, static_cast<std::uint32_t>(random_value % bundled_avatar_count), {}};
}

bool isValidAvatarChoice(const AvatarChoice& avatar) noexcept {
    if (avatar.kind == AvatarKind::Bundled)
        return avatar.bundled_index < bundled_avatar_count && avatar.custom_sha256.empty();
    return avatar.kind == AvatarKind::Custom && avatar.bundled_index == 0 && lowerHex64(avatar.custom_sha256);
}

bool isValidListenerProfile(const ListenerProfile& profile) {
    const auto nickname = canonicalNickname(profile.nickname);
    if (!nickname.has_value() || *nickname != profile.nickname)
        return false;
    return isValidAvatarChoice(profile.avatar);
}

} // namespace music_player::m0
