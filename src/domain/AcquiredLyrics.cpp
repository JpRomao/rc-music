#include "m0/domain/AcquiredLyrics.hpp"

#include <limits>
#include <unordered_set>
#include <utility>

namespace music_player::m0 {
namespace {
using Status = AcquiredLyricsMergeStatus;

struct CanonicalContent final {
    Status status{Status::Invalid};
    std::string text{};
};

CanonicalContent canonicalContent(const LyricsVariant& variant) {
    if (variant.name.size() > maximum_lyrics_variant_name_bytes ||
        variant.language.size() > maximum_lyrics_variant_tag_bytes ||
        variant.script.size() > maximum_lyrics_variant_tag_bytes ||
        variant.source.size() > maximum_lyrics_variant_source_bytes || variant.content.size() > lyrics_max_input_bytes)
        return {Status::LimitExceeded, {}};

    const auto parsed = parseLrc(variant.content);
    if (parsed.error != LyricsParseError::None)
        return {parsed.error == LyricsParseError::LimitExceeded ? Status::LimitExceeded : Status::Invalid, {}};

    // Use the catalogue's existing validation contract while deliberately
    // discarding the untrusted draft ID. No metadata or content is normalized.
    auto checked = variant;
    checked.id = 1;
    if (!isValidLyricsVariants({checked}))
        return {};
    auto canonical = lyricsDocumentAsLrc(parsed.document);
    if (!canonical)
        return {Status::LimitExceeded, {}};
    return {Status::Unchanged, std::move(*canonical)};
}

bool sameForm(const LyricsVariant& left, const LyricsVariant& right) {
    return left.kind == right.kind && left.language == right.language && left.script == right.script;
}

std::uint64_t followingId(const std::uint64_t id) {
    return id == std::numeric_limits<std::uint64_t>::max() ? 1 : id + 1;
}
} // namespace

AcquiredLyricsMergeResult mergeAcquiredLyrics(const LyricsVariants& saved, const LyricsVariants& drafts,
                                              const bool keep_distinct_versions) {
    if (!isValidLyricsVariants(saved) || drafts.empty())
        return {};
    if (drafts.size() > maximum_lyrics_variants)
        return {Status::LimitExceeded, {}};

    std::vector<std::string> saved_content, draft_content;
    saved_content.reserve(maximum_lyrics_variants);
    draft_content.reserve(drafts.size());
    std::unordered_set<std::uint64_t> occupied_ids;
    std::uint64_t highest_id{};
    std::size_t content_bytes{};
    for (const auto& variant : saved) {
        const auto canonical = canonicalContent(variant);
        if (canonical.status != Status::Unchanged)
            return {};
        saved_content.push_back(canonical.text);
        occupied_ids.insert(variant.id);
        if (variant.id > highest_id)
            highest_id = variant.id;
        content_bytes += variant.content.size();
    }
    for (const auto& draft : drafts) {
        auto canonical = canonicalContent(draft);
        if (canonical.status != Status::Unchanged)
            return {canonical.status, {}};
        draft_content.push_back(std::move(canonical.text));
    }

    auto variants = saved;
    auto next_id = followingId(highest_id);
    bool added = false;
    for (std::size_t draft_index = 0; draft_index < drafts.size(); ++draft_index) {
        const auto& draft = drafts[draft_index];
        bool form_exists = false, identical = false;
        for (std::size_t index = 0; index < variants.size(); ++index) {
            if (!sameForm(variants[index], draft))
                continue;
            form_exists = true;
            if (saved_content[index] == draft_content[draft_index]) {
                identical = true;
                break;
            }
        }
        if (identical)
            continue;
        if (form_exists && !keep_distinct_versions)
            return {Status::Conflict, {}};
        if (variants.size() == maximum_lyrics_variants ||
            draft.content.size() > maximum_lyrics_variants_content_bytes - content_bytes)
            return {Status::LimitExceeded, {}};

        while (occupied_ids.contains(next_id))
            next_id = followingId(next_id);
        auto addition = draft;
        addition.id = next_id;
        occupied_ids.insert(next_id);
        next_id = followingId(next_id);
        content_bytes += addition.content.size();
        variants.push_back(std::move(addition));
        saved_content.push_back(draft_content[draft_index]);
        added = true;
    }
    return {added ? Status::Added : Status::Unchanged, std::move(variants)};
}
} // namespace music_player::m0
