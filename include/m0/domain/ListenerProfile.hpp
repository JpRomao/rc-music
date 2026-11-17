#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace music_player::m0 {

inline constexpr std::uint32_t bundled_avatar_count = 20;

enum class AvatarKind : std::uint8_t { Bundled, Custom };

struct AvatarChoice final {
    AvatarKind kind{AvatarKind::Bundled};
    std::uint32_t bundled_index{};
    std::string custom_sha256{};
    bool operator==(const AvatarChoice&) const = default;
};

struct ListenerProfile final {
    std::string nickname{};
    AvatarChoice avatar{};
    bool operator==(const ListenerProfile&) const = default;
};

// Trims surrounding spaces and requires the single-line metadata policy (valid UTF-8, no controls,
// no bidi/invisible layout characters) with 1..maximum_nickname_code_points code points.
[[nodiscard]] std::optional<std::string> canonicalNickname(std::string_view raw);
[[nodiscard]] AvatarChoice initialAvatar(std::uint64_t random_value) noexcept;
[[nodiscard]] bool isValidListenerProfile(const ListenerProfile& profile);

} // namespace music_player::m0
