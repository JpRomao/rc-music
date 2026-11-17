#include "m0/domain/ListenerProfile.hpp"
#include "m0/domain/ListeningRoomLimits.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace music_player::m0::test {
namespace {
std::string repeated(const std::string& unit, const std::size_t count) {
    std::string result;
    for (std::size_t index = 0; index < count; ++index)
        result += unit;
    return result;
}
} // namespace

TEST_CASE("nicknames are trimmed and validated as a single line", "[core][listening-room][profile]") {
    REQUIRE(canonicalNickname("  Ana  ") == "Ana");
    REQUIRE(canonicalNickname("João 日本") == "João 日本");
    REQUIRE_FALSE(canonicalNickname("").has_value());
    REQUIRE_FALSE(canonicalNickname("   ").has_value());
    REQUIRE_FALSE(canonicalNickname("a\nb").has_value());
    REQUIRE_FALSE(canonicalNickname("a\xE2\x80\x8E"
                                    "b")
                      .has_value()); // U+200E LEFT-TO-RIGHT MARK
    REQUIRE_FALSE(canonicalNickname("\xC3").has_value());
}

TEST_CASE("nickname length counts code points", "[core][listening-room][profile]") {
    REQUIRE(canonicalNickname(repeated("日", 32)) == repeated("日", 32));
    REQUIRE_FALSE(canonicalNickname(repeated("日", 33)).has_value());
}

TEST_CASE("initial avatar is a bundled one chosen from the random value", "[core][listening-room][profile]") {
    const auto avatar = initialAvatar(41);
    REQUIRE(avatar.kind == AvatarKind::Bundled);
    REQUIRE(avatar.bundled_index == 41 % bundled_avatar_count);
    REQUIRE(avatar.custom_sha256.empty());
}

TEST_CASE("listener profile validation checks nickname and avatar", "[core][listening-room][profile]") {
    const std::string hash(64, 'a');
    REQUIRE(isValidListenerProfile({"Ana", {AvatarKind::Bundled, 19, {}}}));
    REQUIRE(isValidListenerProfile({"Ana", {AvatarKind::Custom, 0, hash}}));
    REQUIRE_FALSE(isValidListenerProfile({"Ana", {AvatarKind::Bundled, 20, {}}}));
    REQUIRE_FALSE(isValidListenerProfile({"Ana", {AvatarKind::Custom, 0, {}}}));
    REQUIRE_FALSE(isValidListenerProfile({"Ana", {AvatarKind::Custom, 0, std::string(63, 'a')}}));
    REQUIRE_FALSE(isValidListenerProfile({"Ana", {AvatarKind::Custom, 0, std::string(64, 'g')}}));
    REQUIRE_FALSE(isValidListenerProfile({" Ana", {AvatarKind::Bundled, 0, {}}}));
    REQUIRE_FALSE(isValidListenerProfile({"", {AvatarKind::Bundled, 0, {}}}));
}

TEST_CASE("listening room limits match the plan", "[core][listening-room][profile]") {
    STATIC_REQUIRE(maximum_room_listeners == 10);
    STATIC_REQUIRE(minimum_output_offset_ms == -500);
    STATIC_REQUIRE(room_cache_quota_bytes == 1073741824ull);
}

} // namespace music_player::m0::test
