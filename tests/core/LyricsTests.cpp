#include "m0/domain/Lyrics.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <limits>
#include <string>

namespace music_player::m0::test {

TEST_CASE("lyrics parse timestamp precision metadata BOM and multiple tags without Qt", "[lyrics][core]") {
    const auto result = parseLrc("\xef\xbb\xbf[ar:Artist]\r\n[ti:Title]\r\n[00:05.01][00:15.123]Refrain\r\n"
                                 "[00:01.2]First line\r[00:20]Last line\n");
    REQUIRE(result.error == LyricsParseError::None);
    CHECK(result.document.lines ==
          std::vector<LyricsLine>{{1200, "First line"}, {5010, "Refrain"}, {15123, "Refrain"}, {20000, "Last line"}});
    CHECK(result.document.plain_text == "Refrain\nFirst line\nLast line");
}

TEST_CASE("lyrics join simultaneous verses in stable source order and retain empty cues", "[lyrics][core]") {
    const auto result = parseLrc("[00:03]First\n[00:01]Intro\n[00:03]Second\n[00:04]\n");
    REQUIRE(result.error == LyricsParseError::None);
    CHECK(result.document.lines == std::vector<LyricsLine>{{1000, "Intro"}, {3000, "First\nSecond"}, {4000, ""}});
    CHECK(result.document.plain_text == "First\nIntro\nSecond");
}

TEST_CASE("lyrics select the active verse across pause and backward or forward seeks", "[lyrics][core]") {
    const LyricsDocument document{{{1000, "First"}, {2500, "Second"}, {5000, ""}}, {}};
    CHECK_FALSE(lyricsLineAt(document, -1));
    CHECK_FALSE(lyricsLineAt(document, 0));
    CHECK_FALSE(lyricsLineAt(document, 999));
    CHECK(lyricsLineAt(document, 1000) == std::optional<std::size_t>{0});
    CHECK(lyricsLineAt(document, 2499) == std::optional<std::size_t>{0});
    CHECK(lyricsLineAt(document, 2500) == std::optional<std::size_t>{1});
    CHECK(lyricsLineAt(document, 5000) == std::optional<std::size_t>{2});
    CHECK(lyricsLineAt(document, 1200) == std::optional<std::size_t>{0});
    CHECK(lyricsLineAt(document, std::numeric_limits<std::int64_t>::max()) == std::optional<std::size_t>{2});
    CHECK_FALSE(lyricsLineAt({}, 1000));
}

TEST_CASE("lyrics preserve original UTF8 text and allow an unsynchronized fallback", "[lyrics][core]") {
    const std::string text = "Letra original\n\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e \xf0\x9f\x8e\xb5";
    const auto result = parseLrc(text);
    REQUIRE(result.error == LyricsParseError::None);
    CHECK(result.document.lines.empty());
    CHECK(result.document.plain_text == text);
    CHECK_FALSE(lyricsLineAt(result.document, 100000));
}

TEST_CASE("lyrics omit metadata and support empty text without a fake verse", "[lyrics][core]") {
    for (const auto text : {std::string_view{}, std::string_view{"[ar:Artist]\n[by:Editor]\n[offset:0]\n"}}) {
        const auto result = parseLrc(text);
        REQUIRE(result.error == LyricsParseError::None);
        CHECK(result.document == LyricsDocument{});
    }
}

TEST_CASE("lyrics reject malformed UTF8 before returning any partial document", "[lyrics][core]") {
    const auto suffix = GENERATE(std::string{"\xc0\xaf", 2}, std::string{"\x80", 1}, std::string{"\xe2\x82", 2},
                                 std::string{"\xed\xa0\x80", 3}, std::string{"\xf4\x90\x80\x80", 4});
    const auto result = parseLrc(std::string{"[00:01]Good\n[00:02]"} + suffix);
    CHECK(result.error == LyricsParseError::InvalidUtf8);
    CHECK(result.document == LyricsDocument{});
}

TEST_CASE("lyrics reject NUL and unsafe ASCII controls instead of silently truncating", "[lyrics][core]") {
    const auto control = GENERATE('\0', '\x01', '\x1b', '\x7f');
    std::string text{"[00:01]Good\n[00:02]Bad"};
    text.push_back(control);
    const auto result = parseLrc(text);
    CHECK(result.error == LyricsParseError::InvalidFormat);
    CHECK(result.document == LyricsDocument{});
}

TEST_CASE("lyrics reject malformed timestamps and unsupported nonzero offsets", "[lyrics][core]") {
    const auto text = GENERATE("[00:60]Bad", "[00:00.1234]Bad", "[00:00.]Bad", "[1:2]Bad", "[-1:00]Bad",
                               "[99999:00]Bad", "[00:01Good", "[00:01][00:61]Bad", "[offset:100]\n[00:01]Bad",
                               "[offset:-100]\n[00:01]Bad", "[offset:x]\n[00:01]Bad");
    const auto result = parseLrc(text);
    CHECK(result.error == LyricsParseError::InvalidFormat);
    CHECK(result.document == LyricsDocument{});
}

TEST_CASE("lyrics bound input before parsing and accept the inclusive plain input size", "[lyrics][core]") {
    const auto accepted = parseLrc(std::string(lyrics_max_input_bytes, 'a'));
    REQUIRE(accepted.error == LyricsParseError::None);
    CHECK(accepted.document.plain_text.size() == lyrics_max_input_bytes);
    const auto refused = parseLrc(std::string(lyrics_max_input_bytes + 1, 'a'));
    CHECK(refused.error == LyricsParseError::LimitExceeded);
    CHECK(refused.document == LyricsDocument{});
}

TEST_CASE("lyrics bound expanded cue count before merging repeated timestamps", "[lyrics][core]") {
    std::string text;
    for (std::size_t index = 0; index < lyrics_max_cues; ++index)
        text += "[00:01]a\n";
    const auto accepted = parseLrc(text);
    REQUIRE(accepted.error == LyricsParseError::None);
    REQUIRE(accepted.document.lines.size() == 1);
    CHECK(accepted.document.lines[0].text.size() == 2 * lyrics_max_cues - 1);
    const auto refused = parseLrc(text + "[00:01]a\n");
    CHECK(refused.error == LyricsParseError::LimitExceeded);
    CHECK(refused.document == LyricsDocument{});
}

TEST_CASE("lyrics bound timestamp expansion bytes before copying text", "[lyrics][core]") {
    std::string tags;
    for (unsigned index = 0; index < 16; ++index)
        tags += "[00:" + std::string(index < 10 ? "0" : "") + std::to_string(index) + "]";
    const std::string verse(lyrics_max_expanded_bytes / 16, 'a');
    const auto accepted = parseLrc(tags + verse);
    REQUIRE(accepted.error == LyricsParseError::None);
    REQUIRE(accepted.document.lines.size() == 16);
    CHECK(accepted.document.lines[0].text == verse);
    const auto refused = parseLrc(tags + verse + 'a');
    CHECK(refused.error == LyricsParseError::LimitExceeded);
    CHECK(refused.document == LyricsDocument{});
}

TEST_CASE("lyrics account for simultaneous-line separators within the expanded byte limit", "[lyrics][core]") {
    std::string tags;
    for (unsigned index = 0; index < 16; ++index)
        tags += "[00:00]";
    const auto result = parseLrc(tags + std::string(lyrics_max_expanded_bytes / 16, 'a'));
    CHECK(result.error == LyricsParseError::LimitExceeded);
    CHECK(result.document == LyricsDocument{});
}

TEST_CASE("lyrics accept twenty four hours exactly and refuse later timestamps", "[lyrics][core]") {
    const auto accepted = parseLrc("[1440:00]End");
    REQUIRE(accepted.error == LyricsParseError::None);
    REQUIRE(accepted.document.lines.size() == 1);
    CHECK(accepted.document.lines.front().timestamp_ms == lyrics_max_timestamp_ms);
    const auto refused = parseLrc("[1440:00.001]Too late");
    CHECK(refused.error == LyricsParseError::LimitExceeded);
    CHECK(refused.document == LyricsDocument{});
}

} // namespace music_player::m0::test
