#include "m0/domain/AcquiredLyrics.hpp"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>

using namespace music_player::m0;
namespace {
LyricsVariant original(const std::uint64_t id = 1) {
    return {.id = id,
            .name = "Original 日本語",
            .language = "ja",
            .script = "Jpan",
            .source = "Synthetic provider",
            .content = "[00:01.001]独自のテスト\n[00:05.000]次の行"};
}

AcquiredLyricsMergeResult mergePreservingInputs(const LyricsVariants& saved, const LyricsVariants& drafts,
                                                const bool keep_distinct_versions = false) {
    const auto saved_before = saved;
    const auto drafts_before = drafts;
    const auto result = mergeAcquiredLyrics(saved, drafts, keep_distinct_versions);
    REQUIRE(saved == saved_before);
    REQUIRE(drafts == drafts_before);
    return result;
}

void requireFailure(const AcquiredLyricsMergeResult& result, const AcquiredLyricsMergeStatus expected) {
    REQUIRE(result.status == expected);
    REQUIRE(result.variants.empty());
}

LyricsVariants distinctForms(const std::size_t count, const bool maximum_content = false) {
    LyricsVariants result;
    for (std::size_t index = 0; index < count; ++index) {
        auto value = original(index + 1);
        value.language = "synthetic-" + std::to_string(index);
        if (maximum_content)
            value.content = std::string(lyrics_max_input_bytes, 'a');
        result.push_back(value);
    }
    return result;
}
} // namespace

TEST_CASE("acquired lyrics preserve original and romanized Unicode forms with their times", "[acquired-lyrics]") {
    const LyricsVariants saved{original(7)};
    auto romanized = original(0);
    romanized.name = "Romanizada";
    romanized.kind = LyricsVariantKind::Romanized;
    romanized.script = "Latn";
    romanized.source = "Other synthetic provider";
    romanized.content = "[00:01.001]Dokujino tesuto\n[00:05.000]Tsugino gyō";
    const auto result = mergePreservingInputs(saved, {romanized});
    REQUIRE(result.status == AcquiredLyricsMergeStatus::Added);
    REQUIRE(result.variants.size() == 2);
    REQUIRE(result.variants.front() == saved.front());
    romanized.id = 8;
    REQUIRE(result.variants.back() == romanized);
    REQUIRE(parseLrc(result.variants.back().content).document.lines == parseLrc(romanized.content).document.lines);
    REQUIRE(isValidLyricsVariants(result.variants));
}

TEST_CASE("acquired lyrics accept one untimed form without inferring a language or script", "[acquired-lyrics]") {
    auto draft = original(0);
    draft.language.clear();
    draft.script.clear();
    draft.source.clear();
    draft.content = "Texto sintético\n日本語のテスト";
    const auto result = mergePreservingInputs({}, {draft});
    REQUIRE(result.status == AcquiredLyricsMergeStatus::Added);
    REQUIRE(result.variants.size() == 1);
    draft.id = 1;
    REQUIRE(result.variants.front() == draft);
    REQUIRE(parseLrc(result.variants.front().content).document.lines.empty());
}

TEST_CASE("acquired lyrics use kind language and script as exact independent form keys", "[acquired-lyrics]") {
    const LyricsVariants saved{original(9)};
    auto draft = original(0);
    SECTION("kind") { draft.kind = LyricsVariantKind::Romanized; }
    SECTION("language") { draft.language = "JA"; }
    SECTION("script") { draft.script = "jpan"; }
    const auto result = mergePreservingInputs(saved, {draft});
    REQUIRE(result.status == AcquiredLyricsMergeStatus::Added);
    REQUIRE(result.variants.size() == 2);
    REQUIRE(result.variants.front() == saved.front());
    draft.id = 10;
    REQUIRE(result.variants.back() == draft);
}

TEST_CASE("acquired lyrics deduplicate canonical LRC independent of names providers and flags", "[acquired-lyrics]") {
    auto existing = original(42);
    existing.content = "[ar:Synthetic metadata]\r\n[00:01.0]独自のテスト\r\n[00:05]次の行\r\n";
    auto draft = original(0);
    draft.name = "Another label";
    draft.source = "Different provider";
    draft.generated = true;
    draft.content = "[00:01.000]独自のテスト\n[00:05.000]次の行";
    REQUIRE(lyricsDocumentAsLrc(parseLrc(existing.content).document) ==
            lyricsDocumentAsLrc(parseLrc(draft.content).document));
    const LyricsVariants saved{existing};
    const auto result = mergePreservingInputs(saved, {draft});
    REQUIRE(result.status == AcquiredLyricsMergeStatus::Unchanged);
    REQUIRE(result.variants == saved);
}

TEST_CASE("acquired lyrics reject divergent content of the same form atomically by default", "[acquired-lyrics]") {
    const LyricsVariants saved{original(42)};
    auto draft = original(0);
    draft.content = "[00:01.001]Conteúdo divergente";
    requireFailure(mergePreservingInputs(saved, {draft}), AcquiredLyricsMergeStatus::Conflict);
}

TEST_CASE("acquired lyrics keep confirmed divergent versions without replacing saved content", "[acquired-lyrics]") {
    const LyricsVariants saved{original(42)};
    auto draft = original(0);
    draft.content = "[00:01.001]Conteúdo divergente\n[00:05.000]Outra versão";
    const auto result = mergePreservingInputs(saved, {draft}, true);
    REQUIRE(result.status == AcquiredLyricsMergeStatus::Added);
    REQUIRE(result.variants.size() == 2);
    REQUIRE(result.variants.front() == saved.front());
    draft.id = 43;
    REQUIRE(result.variants.back() == draft);
    REQUIRE(isValidLyricsVariants(result.variants));
    draft.content = "[ar:Ignored]\r\n[00:01.001]Conteúdo divergente\r\n[00:05]Outra versão\r\n";
    const auto retried = mergePreservingInputs(result.variants, {draft}, true);
    REQUIRE(retried.status == AcquiredLyricsMergeStatus::Unchanged);
    REQUIRE(retried.variants == result.variants);
    const auto retried_without_confirmation = mergePreservingInputs(result.variants, {draft});
    REQUIRE(retried_without_confirmation.status == AcquiredLyricsMergeStatus::Unchanged);
    REQUIRE(retried_without_confirmation.variants == result.variants);
}

TEST_CASE("acquired lyrics deduplicate canonical duplicates within one batch", "[acquired-lyrics]") {
    auto first = original(0), second = first;
    first.content = "[00:01.000]同じ行\n[00:05.000]次の行";
    second.content = "[ti:Ignored]\r\n[00:01]同じ行\r\n[00:05.0]次の行\r\n";
    second.name = "Other label";
    second.source = "Other provider";
    const auto result = mergePreservingInputs({}, {first, second});
    REQUIRE(result.status == AcquiredLyricsMergeStatus::Added);
    REQUIRE(result.variants.size() == 1);
    first.id = 1;
    REQUIRE(result.variants.front() == first);
}

TEST_CASE("acquired lyrics require confirmation for divergent drafts in the same batch", "[acquired-lyrics]") {
    auto first = original(0), second = first;
    second.content = "Outra versão sintética";
    SECTION("default conflict is atomic") {
        requireFailure(mergePreservingInputs({}, {first, second}), AcquiredLyricsMergeStatus::Conflict);
    }
    SECTION("explicit keep both is idempotent") {
        const auto result = mergePreservingInputs({}, {first, second}, true);
        REQUIRE(result.status == AcquiredLyricsMergeStatus::Added);
        REQUIRE(result.variants.size() == 2);
        first.id = 1;
        second.id = 2;
        const LyricsVariants expected{first, second};
        REQUIRE(result.variants == expected);
        const auto retried = mergePreservingInputs(result.variants, {second, first}, true);
        REQUIRE(retried.status == AcquiredLyricsMergeStatus::Unchanged);
        REQUIRE(retried.variants == result.variants);
    }
}

TEST_CASE("acquired lyrics match any existing content when several versions share the form key", "[acquired-lyrics]") {
    auto first = original(7), second = original(9), draft = original(0);
    second.content = "Outra versão sintética";
    const LyricsVariants saved{first, second};
    REQUIRE(isValidLyricsVariants(saved));
    SECTION("matching first despite later divergence") {
        const auto result = mergePreservingInputs(saved, {draft});
        REQUIRE(result.status == AcquiredLyricsMergeStatus::Unchanged);
        REQUIRE(result.variants == saved);
    }
    SECTION("matching last despite earlier divergence") {
        draft.content = second.content;
        const auto result = mergePreservingInputs(saved, {draft});
        REQUIRE(result.status == AcquiredLyricsMergeStatus::Unchanged);
        REQUIRE(result.variants == saved);
    }
    SECTION("none match") {
        draft.content = "Terceira versão sintética";
        requireFailure(mergePreservingInputs(saved, {draft}), AcquiredLyricsMergeStatus::Conflict);
    }
}

TEST_CASE("acquired lyrics discard all candidate additions after a later conflict", "[acquired-lyrics]") {
    const LyricsVariants saved{original(10)};
    auto different_form = original(0), conflict = original(0);
    different_form.script = "Latn";
    different_form.kind = LyricsVariantKind::Romanized;
    conflict.content = "Divergência posterior";
    requireFailure(mergePreservingInputs(saved, {different_form, conflict}), AcquiredLyricsMergeStatus::Conflict);
}

TEST_CASE("acquired lyrics ignore draft IDs and allocate after the largest saved ID", "[acquired-lyrics]") {
    auto existing = original(42), earlier = original(7);
    earlier.script = "Latn";
    const LyricsVariants saved{existing, earlier};
    auto first = original(0), second = original(0), third = original(42);
    first.language = "en";
    second.language = "pt";
    third.language = "es";
    const auto result = mergePreservingInputs(saved, {first, second, third});
    REQUIRE(result.status == AcquiredLyricsMergeStatus::Added);
    REQUIRE(result.variants.size() == 5);
    REQUIRE(result.variants[0] == existing);
    REQUIRE(result.variants[1] == earlier);
    REQUIRE(result.variants[2].id == 43);
    REQUIRE(result.variants[3].id == 44);
    REQUIRE(result.variants[4].id == 45);
    REQUIRE(isValidLyricsVariants(result.variants));
}

TEST_CASE("acquired lyrics wrap ID allocation without zero or occupied IDs", "[acquired-lyrics]") {
    auto highest = original(std::numeric_limits<std::uint64_t>::max());
    auto one = original(1), two = original(2);
    one.language = "en";
    two.language = "pt";
    auto first = original(std::numeric_limits<std::uint64_t>::max()), second = first;
    first.language = "es";
    second.language = "fr";
    const LyricsVariants saved{highest, two, one};
    const auto result = mergePreservingInputs(saved, {first, second});
    REQUIRE(result.status == AcquiredLyricsMergeStatus::Added);
    REQUIRE(result.variants.size() == 5);
    REQUIRE(result.variants[0] == highest);
    REQUIRE(result.variants[1] == two);
    REQUIRE(result.variants[2] == one);
    REQUIRE(result.variants[3].id == 3);
    REQUIRE(result.variants[4].id == 4);
    REQUIRE(isValidLyricsVariants(result.variants));
}

TEST_CASE("acquired lyrics do not reject duplicate batches by raw combined counts", "[acquired-lyrics]") {
    const auto saved = distinctForms(maximum_lyrics_variants);
    auto drafts = saved;
    for (auto& draft : drafts)
        draft.id = 0;
    const auto result = mergePreservingInputs(saved, drafts);
    REQUIRE(result.status == AcquiredLyricsMergeStatus::Unchanged);
    REQUIRE(result.variants == saved);
}

TEST_CASE("acquired lyrics apply the final variant limit after deduplication", "[acquired-lyrics]") {
    const auto saved = distinctForms(maximum_lyrics_variants - 1);
    auto duplicate = saved.front(), addition = original(0);
    addition.language = "new-form";
    const auto result = mergePreservingInputs(saved, {duplicate, addition, addition});
    REQUIRE(result.status == AcquiredLyricsMergeStatus::Added);
    REQUIRE(result.variants.size() == maximum_lyrics_variants);
    REQUIRE(result.variants.back().id == maximum_lyrics_variants);
    REQUIRE(result.variants.back().language == addition.language);
    REQUIRE(isValidLyricsVariants(result.variants));
}

TEST_CASE("acquired lyrics refuse additions beyond the catalogue limit atomically", "[acquired-lyrics]") {
    const auto saved = distinctForms(maximum_lyrics_variants);
    auto draft = original(0);
    draft.language = "new-form";
    requireFailure(mergePreservingInputs(saved, {draft}), AcquiredLyricsMergeStatus::LimitExceeded);
    draft = saved.front();
    draft.content = "Confirmed divergence also needs capacity";
    requireFailure(mergePreservingInputs(saved, {draft}, true), AcquiredLyricsMergeStatus::LimitExceeded);
}

TEST_CASE("acquired lyrics bound draft count independently from duplicate elimination", "[acquired-lyrics]") {
    requireFailure(mergePreservingInputs({}, {}), AcquiredLyricsMergeStatus::Invalid);
    const LyricsVariants drafts(maximum_lyrics_variants + 1, original(0));
    requireFailure(mergePreservingInputs({}, drafts), AcquiredLyricsMergeStatus::LimitExceeded);
}

TEST_CASE("acquired lyrics refuse an invalid saved catalogue without repairing or mutating it", "[acquired-lyrics]") {
    LyricsVariants saved{original(1)};
    SECTION("zero ID") { saved.front().id = 0; }
    SECTION("duplicate ID") { saved.push_back(saved.front()); }
    SECTION("invalid metadata") { saved.front().name = " Two lines\n"; }
    SECTION("invalid UTF8") { saved.front().content = std::string("\xc0\xaf", 2); }
    SECTION("over limit") { saved = distinctForms(maximum_lyrics_variants + 1); }
    REQUIRE_FALSE(isValidLyricsVariants(saved));
    requireFailure(mergePreservingInputs(saved, {original(0)}), AcquiredLyricsMergeStatus::Invalid);
}

TEST_CASE("acquired lyrics reject malformed draft content and metadata without partial output", "[acquired-lyrics]") {
    auto draft = original(0);
    SECTION("empty name") { draft.name.clear(); }
    SECTION("trimmed metadata") { draft.name = " Name "; }
    SECTION("multiline metadata") { draft.source = "Two\nlines"; }
    SECTION("invalid metadata UTF8") { draft.language = std::string("\xc0\xaf", 2); }
    SECTION("unknown kind") { draft.kind = static_cast<LyricsVariantKind>(3); }
    SECTION("invalid content UTF8") { draft.content = std::string("\xc0\xaf", 2); }
    SECTION("embedded NUL") { draft.content = std::string("A\0B", 3); }
    SECTION("invalid timestamp") { draft.content = "[00:xy]Invalid"; }
    SECTION("nonzero offset") { draft.content = "[offset:1]\n[00:01]Unsupported"; }
    SECTION("empty content") { draft.content.clear(); }
    SECTION("blank content") { draft.content = " \t\n"; }
    SECTION("metadata only") { draft.content = "[ar:Synthetic]\n[ti:Fixture]"; }
    auto earlier = original(0);
    earlier.language = "en";
    requireFailure(mergePreservingInputs({}, {earlier, draft}, true), AcquiredLyricsMergeStatus::Invalid);
}

TEST_CASE("acquired lyrics accept exact metadata and content byte limits without truncation", "[acquired-lyrics]") {
    auto draft = original(0);
    draft.name = std::string(maximum_lyrics_variant_name_bytes, 'a');
    draft.language = std::string(maximum_lyrics_variant_tag_bytes, 'b');
    draft.script = std::string(maximum_lyrics_variant_tag_bytes, 'c');
    draft.source = std::string(maximum_lyrics_variant_source_bytes, 'd');
    draft.content = std::string(lyrics_max_input_bytes, 'e');
    const auto result = mergePreservingInputs({}, {draft});
    REQUIRE(result.status == AcquiredLyricsMergeStatus::Added);
    REQUIRE(result.variants.size() == 1);
    draft.id = 1;
    REQUIRE(result.variants.front() == draft);
    REQUIRE(isValidLyricsVariants(result.variants));
}

TEST_CASE("acquired lyrics report metadata content and parser resource excess as limits", "[acquired-lyrics]") {
    auto draft = original(0);
    SECTION("name") { draft.name = std::string(maximum_lyrics_variant_name_bytes + 1, 'a'); }
    SECTION("language") { draft.language = std::string(maximum_lyrics_variant_tag_bytes + 1, 'b'); }
    SECTION("script") { draft.script = std::string(maximum_lyrics_variant_tag_bytes + 1, 'c'); }
    SECTION("source") { draft.source = std::string(maximum_lyrics_variant_source_bytes + 1, 'd'); }
    SECTION("content") { draft.content = std::string(lyrics_max_input_bytes + 1, 'e'); }
    SECTION("timestamp") { draft.content = "[1440:00.001]Excess time"; }
    SECTION("cue count") {
        draft.content.clear();
        for (std::size_t index = 0; index <= lyrics_max_cues; ++index)
            draft.content += "[00:01]a\n";
    }
    requireFailure(mergePreservingInputs({}, {draft}, true), AcquiredLyricsMergeStatus::LimitExceeded);
}

TEST_CASE("acquired lyrics retain a full four MiB catalogue when every draft is a duplicate", "[acquired-lyrics]") {
    const auto saved = distinctForms(maximum_lyrics_variants, true);
    REQUIRE(saved.size() * saved.front().content.size() == maximum_lyrics_variants_content_bytes);
    REQUIRE(isValidLyricsVariants(saved));
    auto drafts = saved;
    for (auto& draft : drafts)
        draft.id = std::numeric_limits<std::uint64_t>::max();
    const auto result = mergePreservingInputs(saved, drafts);
    REQUIRE(result.status == AcquiredLyricsMergeStatus::Unchanged);
    REQUIRE(result.variants == saved);
}
