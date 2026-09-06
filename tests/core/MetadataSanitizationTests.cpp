#include "m0/domain/MetadataSanitization.hpp"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>
#include <string_view>

namespace music_player::m0::test {
namespace {
std::string utf8(const std::u8string_view text) { return {reinterpret_cast<const char*>(text.data()), text.size()}; }
} // namespace

TEST_CASE("metadata text preserves visible Unicode and plain markup without transliteration",
          "[metadata-sanitization]") {
    const auto text =
        utf8(u8"Canção 日本語 <b>literal</b> اَلْعَرَبِيَّةُ 👩‍👩‍👧‍👦 é "
             u8"می‌خواهم");
    const auto result = sanitizeMetadataText(text);
    CHECK(result.text == text);
    CHECK(result.disposition == MetadataTextDisposition::Unchanged);
}

TEST_CASE("metadata text collapses whitespace and controls into a single trimmed line", "[metadata-sanitization]") {
    auto text = utf8(u8"\u3000 Canção\t\r\n 日本語\u00a0 \u0085 Álbum\u2028\u2029fim \u2009");
    text.insert(text.find("fim"), std::string("\0\x1b\x7f", 3));
    const auto result = sanitizeMetadataText(text);
    CHECK(result.text == utf8(u8"Canção 日本語 Álbum fim"));
    CHECK(result.disposition == MetadataTextDisposition::Sanitized);
}

TEST_CASE("metadata text removes explicit direction overrides and invisible layout markers",
          "[metadata-sanitization]") {
    const auto result = sanitizeMetadataText(
        utf8(u8"\ufeffA\u200bB\u2060C\u061c\u200e\u200f\u202a\u202b\u202c\u202d\u202e\u2066\u2067\u2068\u2069"));
    CHECK(result.text == "ABC");
    CHECK(result.disposition == MetadataTextDisposition::Sanitized);
}

TEST_CASE("metadata text treats missing and semantically empty values as absent", "[metadata-sanitization]") {
    for (const auto& text :
         {std::string{}, std::string(" \t\r\n"), utf8(u8"\u00a0\u3000\u200b\ufeff"), std::string("\0\x1b", 2)}) {
        const auto result = sanitizeMetadataText(text);
        CHECK(result.text.empty());
        CHECK(result.disposition == MetadataTextDisposition::Empty);
    }
}

TEST_CASE("metadata text rejects malformed UTF8 atomically without leaking a partial prefix",
          "[metadata-sanitization]") {
    for (const auto& invalid : {std::string("\x80"), std::string("\xc0\xaf"), std::string("\xe0\x80\xaf"),
                                std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80"),
                                std::string("\xf5\x80\x80\x80"), std::string("\xe2\x82"), std::string("\xc2X")}) {
        const auto result = sanitizeMetadataText("prefix" + invalid);
        CHECK(result.text.empty());
        CHECK(result.disposition == MetadataTextDisposition::InvalidEncoding);
    }
}

TEST_CASE("metadata text accepts Unicode scalar boundaries and normalization is idempotent",
          "[metadata-sanitization]") {
    const std::string text = "\xc2\xa1\xe0\xa0\x80\xed\x9f\xbf\xee\x80\x80\xf0\x90\x80\x80\xf4\x8f\xbf\xbf";
    CHECK(sanitizeMetadataText(text).text == text);
    const auto first = sanitizeMetadataText("  A\t B\n");
    const auto second = sanitizeMetadataText(first.text);
    CHECK(second.text == "A B");
    CHECK(second.disposition == MetadataTextDisposition::Unchanged);
}

TEST_CASE("metadata track numbers preserve padded decimals and totals without signed conversion",
          "[metadata-sanitization]") {
    for (const auto text : {"7", "007", " 7 ", "7/12", "007/0012", "7/2"})
        CHECK(parseMetadataTrackNumber(text) == 7U);
    CHECK(parseMetadataTrackNumber("4294967295") == std::numeric_limits<std::uint32_t>::max());
    CHECK(parseMetadataTrackNumber("1/4294967295") == 1U);
}

TEST_CASE("metadata track numbers reject missing malformed signed and overflowing values", "[metadata-sanitization]") {
    for (const auto text : {"", " ", "0", "-1", "+7", "7junk", "7.5", "7/0", "7/-1", "7/", "/7", "7/12/13", "7 2",
                            "7\t2", "4294967296", "1/4294967296"})
        CHECK_FALSE(parseMetadataTrackNumber(text).has_value());
    CHECK_FALSE(parseMetadataTrackNumber(std::string("7\0", 2)).has_value());
    CHECK_FALSE(parseMetadataTrackNumber(utf8(u8"７")).has_value());
}

TEST_CASE("metadata year preserves bare and calendar ISO dates including legacy timestamp precision",
          "[metadata-sanitization]") {
    for (const auto text :
         {"2026", " 2026 ", "2026-09", "2026-09-04", "2026-09-04T12", "2026-09-04T12:34", "2026-09-04T12:34:56",
          "2026-09-04T12:34:56.123Z", "2026-09-04T12:34:56-03:00", "2026-09-04T12:34Z"})
        CHECK(parseMetadataYear(text) == 2026U);
    CHECK(parseMetadataYear("1") == 1U);
    CHECK(parseMetadataYear("0001") == 1U);
    CHECK(parseMetadataYear("9999") == 9999U);
    CHECK(parseMetadataYear("2000-02-29") == 2000U);
    CHECK(parseMetadataYear("2024-02-29") == 2024U);
}

TEST_CASE("metadata year rejects invalid calendar dates and partial or signed parses", "[metadata-sanitization]") {
    for (const auto text : {"",
                            "0",
                            "0000",
                            "10000",
                            "-1",
                            "+2026",
                            "2026junk",
                            "2026-00",
                            "2026-13",
                            "2026-2",
                            "2026-02-29",
                            "1900-02-29",
                            "2026-04-31",
                            "2026-01-00",
                            "2026-09-04T",
                            "2026-09-04T24:00",
                            "2026-09-04T12:60",
                            "2026-09-04T12:34:60",
                            "2026-09-04T12:34:56.",
                            "2026-09-04T12:34:56+03",
                            "2026-09-04T12:34:56+25:00",
                            "2026-09-04extra",
                            "2026/09/04",
                            "4294967295"})
        CHECK_FALSE(parseMetadataYear(text).has_value());
    CHECK_FALSE(parseMetadataYear(std::string("2026\0", 5)).has_value());
}

} // namespace music_player::m0::test
