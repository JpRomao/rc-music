#include "m0/domain/LyricsVariants.hpp"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace music_player::m0;
namespace {
LyricsVariant original() {
    return {.id = 1,
            .name = "Original 日本語",
            .language = "ja",
            .script = "Jpan",
            .source = "User import",
            .content = "[00:01.001]独自のテスト\n[00:05.000]次の行"};
}
} // namespace

TEST_CASE("lyrics variants preserve original and romanized forms independently", "[lyrics-variants]") {
    auto first = original();
    auto romanized = first;
    romanized.id = 2;
    romanized.name = "Romanizada";
    romanized.script = "Latn";
    romanized.kind = LyricsVariantKind::Romanized;
    romanized.content = "[00:01.001]Test line\n[00:05.000]Other line";
    const LyricsVariants variants{first, romanized};
    REQUIRE(isValidLyricsVariants(variants));
    REQUIRE(variants.front() == first);
    REQUIRE(variants.back() == romanized);
    REQUIRE(variants[0].content != variants[1].content);
}
TEST_CASE("lyrics variants accept empty catalogs and one plain form in any script", "[lyrics-variants]") {
    REQUIRE(isValidLyricsVariants({}));
    auto value = original();
    value.content = "Texto de teste sem tempos\n日本語のテスト";
    REQUIRE(isValidLyricsVariants({value}));
    value.language.clear();
    value.script.clear();
    value.source.clear();
    REQUIRE(isValidLyricsVariants({value}));
}
TEST_CASE("lyrics variant IDs distinguish duplicate names and text", "[lyrics-variants]") {
    auto a = original(), b = a;
    b.id = std::numeric_limits<std::uint64_t>::max();
    REQUIRE(isValidLyricsVariants({a, b}));
    b.id = a.id;
    REQUIRE_FALSE(isValidLyricsVariants({a, b}));
    a.id = 0;
    REQUIRE_FALSE(isValidLyricsVariants({a}));
}
TEST_CASE("lyrics variants refuse malformed text and unsupported kinds", "[lyrics-variants]") {
    auto value = original();
    const auto good = value;
    value.name = "";
    REQUIRE_FALSE(isValidLyricsVariants({value}));
    value = good;
    value.name = " Name ";
    REQUIRE_FALSE(isValidLyricsVariants({value}));
    value = good;
    value.name = "Two\nlines";
    REQUIRE_FALSE(isValidLyricsVariants({value}));
    value = good;
    value.name = std::string("\xc0\xaf", 2);
    REQUIRE_FALSE(isValidLyricsVariants({value}));
    value = good;
    value.kind = static_cast<LyricsVariantKind>(3);
    REQUIRE_FALSE(isValidLyricsVariants({value}));
    value = good;
    value.content = std::string("A\0B", 3);
    REQUIRE_FALSE(isValidLyricsVariants({value}));
    value.content = "[00:xy]Bad time";
    REQUIRE_FALSE(isValidLyricsVariants({value}));
    value.content = "[offset:1]\n[00:00]Unsupported offset";
    REQUIRE_FALSE(isValidLyricsVariants({value}));
    value.content = " \t\n";
    REQUIRE_FALSE(isValidLyricsVariants({value}));
}
TEST_CASE("lyrics metadata limits are checked without truncating Unicode", "[lyrics-variants]") {
    auto value = original();
    value.name = std::string(maximum_lyrics_variant_name_bytes, 'a');
    value.language = std::string(maximum_lyrics_variant_tag_bytes, 'b');
    value.script = std::string(maximum_lyrics_variant_tag_bytes, 'c');
    value.source = std::string(maximum_lyrics_variant_source_bytes, 'd');
    REQUIRE(isValidLyricsVariants({value}));
    SECTION("name") { value.name += 'a'; }
    SECTION("language") { value.language += 'b'; }
    SECTION("script") { value.script += 'c'; }
    SECTION("source") { value.source += 'd'; }
    REQUIRE_FALSE(isValidLyricsVariants({value}));
}
TEST_CASE("lyrics content and collection limits reject excess without partial acceptance", "[lyrics-variants]") {
    auto value = original();
    value.content = std::string(lyrics_max_input_bytes, 'a');
    REQUIRE(isValidLyricsVariants({value}));
    LyricsVariants variants;
    for (std::size_t index = 0; index < maximum_lyrics_variants; ++index) {
        value.id = index + 1;
        variants.push_back(value);
    }
    REQUIRE(isValidLyricsVariants(variants));
    value.id = maximum_lyrics_variants + 1;
    variants.push_back(value);
    REQUIRE_FALSE(isValidLyricsVariants(variants));
    value.content += 'a';
    REQUIRE_FALSE(isValidLyricsVariants({value}));
}
TEST_CASE("lyrics document serialization preserves milliseconds and simultaneous multiline cues", "[lyrics-variants]") {
    const auto parsed =
        parseLrc("[00:01.001]日本語\n[00:01.001]Second simultaneous line\n[02:03.456]Last\n[03:00.000]");
    REQUIRE(parsed.error == LyricsParseError::None);
    const auto serialized = lyricsDocumentAsLrc(parsed.document);
    REQUIRE(serialized);
    const auto restored = parseLrc(*serialized);
    REQUIRE(restored.error == LyricsParseError::None);
    REQUIRE(restored.document.lines == parsed.document.lines);
    REQUIRE(lyricsLineAt(restored.document, 1001) == std::optional<std::size_t>{0});
    REQUIRE(restored.document.lines.back().text.empty());
}
TEST_CASE("lyrics document serialization leaves untimed text untimed", "[lyrics-variants]") {
    const auto parsed = parseLrc("Plain 日本語\n[Chorus]\nA test & <tag>");
    REQUIRE(parsed.error == LyricsParseError::None);
    REQUIRE(parsed.document.lines.empty());
    const auto serialized = lyricsDocumentAsLrc(parsed.document);
    REQUIRE(serialized);
    REQUIRE(*serialized == parsed.document.plain_text);
    REQUIRE(parseLrc(*serialized).document.lines.empty());
}
TEST_CASE("lyrics serialization refuses invalid or unrepresentable documents", "[lyrics-variants]") {
    REQUIRE_FALSE(lyricsDocumentAsLrc({}));
    SECTION("negative time") { REQUIRE_FALSE(lyricsDocumentAsLrc({{{-1, "line"}}, "line"})); }
    SECTION("excess time") { REQUIRE_FALSE(lyricsDocumentAsLrc({{{lyrics_max_timestamp_ms + 1, "line"}}, "line"})); }
    SECTION("descending times") { REQUIRE_FALSE(lyricsDocumentAsLrc({{{2000, "A"}, {1000, "B"}}, "A\nB"})); }
    SECTION("duplicate times violate merged document contract") {
        REQUIRE_FALSE(lyricsDocumentAsLrc({{{1000, "A"}, {1000, "B"}}, "A\nB"}));
    }
    SECTION("text would be silently trimmed") { REQUIRE_FALSE(lyricsDocumentAsLrc({{{0, " A "}}, " A "})); }
    SECTION("excess bytes") { REQUIRE_FALSE(lyricsDocumentAsLrc({{}, std::string(lyrics_max_input_bytes + 1, 'a')})); }
}
