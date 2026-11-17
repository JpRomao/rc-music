#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace music_player::m0 {

// Invite link: "<origin>/j/<room>#<secret>.<host fingerprint>", every value base64url
// without padding. The fragment is never sent to the server by browsers.
struct RoomInviteLink final {
    std::array<std::uint8_t, 16> room_id{};
    std::array<std::uint8_t, 16> secret{};
    std::array<std::uint8_t, 32> host_fingerprint{};

    bool operator==(const RoomInviteLink&) const = default;
};

// worker_origin is "scheme://host" with no trailing slash, e.g. "https://x.workers.dev".
[[nodiscard]] std::string formatInviteLink(const RoomInviteLink& link, std::string_view worker_origin);

// Strict parse. Accepts surrounding ASCII spaces/tabs and one final "\n" or "\r\n". The
// scheme must match exactly and the host case-insensitively (ASCII); path, query, userinfo
// and port beyond the origin are rejected, as are non-canonical base64url encodings.
[[nodiscard]] std::optional<RoomInviteLink> parseInviteLink(std::string_view text, std::string_view worker_origin);

} // namespace music_player::m0
