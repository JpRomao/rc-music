#include "m0/domain/RoomInviteLink.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace music_player::m0::test {
namespace {
const std::string origin = "https://rc-music-room.example.workers.dev";
const std::string room_text = "AAECAwQFBgcICQoLDA0ODw";
const std::string secret_text = "EBESExQVFhcYGRobHB0eHw";
const std::string fingerprint_text = "ICEiIyQlJicoKSorLC0uLzAxMjM0NTY3ODk6Ozw9Pj8";
const std::string good = origin + "/j/" + room_text + "#" + secret_text + "." + fingerprint_text;

RoomInviteLink sample() {
    RoomInviteLink link;
    for (std::size_t i = 0; i < 16; ++i) {
        link.room_id[i] = static_cast<std::uint8_t>(i);
        link.secret[i] = static_cast<std::uint8_t>(16 + i);
    }
    for (std::size_t i = 0; i < 32; ++i)
        link.host_fingerprint[i] = static_cast<std::uint8_t>(32 + i);
    return link;
}

bool parses(const std::string& text) { return parseInviteLink(text, origin).has_value(); }
} // namespace

TEST_CASE("invite links format and parse back with fixed values", "[core][listening-room][invite]") {
    REQUIRE(formatInviteLink(sample(), origin) == good);
    const auto parsed = parseInviteLink(good, origin);
    REQUIRE(parsed.has_value());
    REQUIRE(*parsed == sample());

    RoomInviteLink ones;
    ones.room_id.fill(0xFF);
    ones.secret.fill(0xFF);
    ones.host_fingerprint.fill(0xFF);
    const std::string text = formatInviteLink(ones, origin);
    REQUIRE(text ==
            origin + "/j/_____________________w#_____________________w.__________________________________________8");
    REQUIRE(parseInviteLink(text, origin) == ones);
}

TEST_CASE("invite links accept surrounding blanks and one final line break", "[core][listening-room][invite]") {
    REQUIRE(parses("  \t" + good + " \t"));
    REQUIRE(parses(good + "\n"));
    REQUIRE(parses(good + "\r\n"));
    REQUIRE(parses("  " + good + "  \n"));
    REQUIRE_FALSE(parses(good + "\n\n"));
    REQUIRE_FALSE(parses("\n" + good));
    REQUIRE_FALSE(parses(good + " x"));
    REQUIRE_FALSE(parses(""));
}

TEST_CASE("invite links reject another origin or scheme", "[core][listening-room][invite]") {
    const std::string tail = good.substr(origin.size());
    REQUIRE_FALSE(parses("https://other.example.workers.dev" + tail));
    REQUIRE_FALSE(parses("http://rc-music-room.example.workers.dev" + tail));
    REQUIRE_FALSE(parses(origin + ".evil.com" + tail));
    REQUIRE_FALSE(parses(origin + ":8443" + tail));
    REQUIRE_FALSE(parses("https://user@rc-music-room.example.workers.dev" + tail));
    REQUIRE_FALSE(parses(origin + "/" + tail));
    REQUIRE_FALSE(parses("HTTPS://rc-music-room.example.workers.dev" + tail));
    REQUIRE(parses("https://RC-Music-Room.Example.Workers.Dev" + tail));
}

TEST_CASE("invite links reject wrong paths and queries", "[core][listening-room][invite]") {
    const std::string fragment = "#" + secret_text + "." + fingerprint_text;
    REQUIRE_FALSE(parses(origin + "/x/" + room_text + fragment));
    REQUIRE_FALSE(parses(origin + "/j/a/" + room_text + fragment));
    REQUIRE_FALSE(parses(origin + "/j/" + room_text + "?a=b" + fragment));
    REQUIRE_FALSE(parses(origin + "/j/" + room_text + fragment + "?a=b"));
}

TEST_CASE("invite links reject malformed base64url", "[core][listening-room][invite]") {
    const auto with = [&](const std::string& room, const std::string& secret, const std::string& fp) {
        return parses(origin + "/j/" + room + "#" + secret + "." + fp);
    };
    REQUIRE(with(room_text, secret_text, fingerprint_text));
    REQUIRE_FALSE(with(room_text + "==", secret_text, fingerprint_text));
    REQUIRE_FALSE(with(room_text, secret_text + "=", fingerprint_text));
    REQUIRE_FALSE(with("AAECAwQFBgcICQoLDA0OD+", secret_text, fingerprint_text));
    REQUIRE_FALSE(with(room_text, "EBESExQVFhcYGRobHB0eH!", fingerprint_text));
    REQUIRE_FALSE(with(room_text.substr(1), secret_text, fingerprint_text));
    REQUIRE_FALSE(with(room_text + "A", secret_text, fingerprint_text));
    REQUIRE_FALSE(with(room_text, secret_text, fingerprint_text.substr(1)));
    REQUIRE_FALSE(with(room_text, secret_text, fingerprint_text + "A"));
    REQUIRE_FALSE(with("", secret_text, fingerprint_text));
    // Non-canonical: last symbol carries non-zero trailing bits ('x' instead of 'w'; '9' instead of '8').
    REQUIRE_FALSE(with("AAECAwQFBgcICQoLDA0ODx", secret_text, fingerprint_text));
    REQUIRE_FALSE(with(room_text, secret_text, "ICEiIyQlJicoKSorLC0uLzAxMjM0NTY3ODk6Ozw9Pj9"));
}

TEST_CASE("invite links reject missing hash, missing dot and extra parts", "[core][listening-room][invite]") {
    const std::string head = origin + "/j/" + room_text;
    REQUIRE_FALSE(parses(head));
    REQUIRE_FALSE(parses(head + "#" + secret_text));
    REQUIRE_FALSE(parses(head + "#" + secret_text + fingerprint_text));
    REQUIRE_FALSE(parses(head + "#" + secret_text + "." + fingerprint_text + ".AA"));
    REQUIRE_FALSE(parses(head + "#" + secret_text + "." + fingerprint_text + "#x"));
    REQUIRE_FALSE(parses(head + "#" + secret_text + "." + fingerprint_text + "/"));
}
} // namespace music_player::m0::test
