#include "m0/domain/LyricsVariants.hpp"
#include "m0/domain/MetadataSanitization.hpp"

#include <string_view>
#include <unordered_set>

namespace music_player::m0 {
namespace {
bool metadata(const std::string& value, const std::size_t maximum, const bool optional) {
    if (value.size() > maximum || (!optional && value.empty()))
        return false;
    return value.empty() || sanitizeMetadataText(value).disposition == MetadataTextDisposition::Unchanged;
}
bool meaningful(const LyricsDocument& document) {
    return !document.lines.empty() || document.plain_text.find_first_not_of(" \t\r\n") != std::string::npos;
}
std::string padded(const std::int64_t value, const std::size_t width) {
    auto result = std::to_string(value);
    if (result.size() < width)
        result.insert(0, width - result.size(), '0');
    return result;
}
} // namespace
std::string_view lyricsVariantKindName(const LyricsVariantKind kind) {
    switch (kind) {
    case LyricsVariantKind::Original:
        return "original";
    case LyricsVariantKind::Romanized:
        return "romanized";
    case LyricsVariantKind::Translation:
        return "translation";
    }
    return {};
}
std::optional<LyricsVariantKind> lyricsVariantKindFromName(const std::string_view name) {
    for (const auto kind :
         {LyricsVariantKind::Original, LyricsVariantKind::Romanized, LyricsVariantKind::Translation}) {
        if (name == lyricsVariantKindName(kind))
            return kind;
    }
    return {};
}
bool isValidLyricsVariants(const LyricsVariants& variants) {
    if (variants.size() > maximum_lyrics_variants)
        return false;
    std::unordered_set<std::uint64_t> ids;
    std::size_t bytes{};
    for (const auto& variant : variants) {
        if (variant.id == 0 || !ids.insert(variant.id).second ||
            !metadata(variant.name, maximum_lyrics_variant_name_bytes, false) ||
            !metadata(variant.language, maximum_lyrics_variant_tag_bytes, true) ||
            !metadata(variant.script, maximum_lyrics_variant_tag_bytes, true) ||
            !metadata(variant.source, maximum_lyrics_variant_source_bytes, true) ||
            lyricsVariantKindName(variant.kind).empty() || variant.content.size() > lyrics_max_input_bytes ||
            variant.content.size() > maximum_lyrics_variants_content_bytes - bytes)
            return false;
        const auto parsed = parseLrc(variant.content);
        if (parsed.error != LyricsParseError::None || !meaningful(parsed.document))
            return false;
        bytes += variant.content.size();
    }
    return true;
}
std::optional<std::string> lyricsDocumentAsLrc(const LyricsDocument& document) {
    if (!meaningful(document) || document.lines.size() > lyrics_max_cues)
        return {};
    if (document.lines.empty()) {
        const auto parsed = parseLrc(document.plain_text);
        if (parsed.error != LyricsParseError::None || !parsed.document.lines.empty() ||
            parsed.document.plain_text != document.plain_text)
            return {};
        return document.plain_text;
    }
    std::string result;
    std::int64_t previous{-1};
    for (const auto& line : document.lines) {
        if (line.timestamp_ms < 0 || line.timestamp_ms > lyrics_max_timestamp_ms || line.timestamp_ms <= previous)
            return {};
        previous = line.timestamp_ms;
        const auto prefix = "[" + padded(line.timestamp_ms / 60'000, 2) + ":" +
                            padded((line.timestamp_ms / 1000) % 60, 2) + "." + padded(line.timestamp_ms % 1000, 3) +
                            "]";
        std::string_view remaining = line.text;
        while (true) {
            const auto end = remaining.find('\n');
            const auto part = remaining.substr(0, end);
            if (prefix.size() + 1 > lyrics_max_input_bytes - result.size() ||
                part.size() > lyrics_max_input_bytes - result.size() - prefix.size() - 1)
                return {};
            result += prefix;
            result.append(part);
            result += '\n';
            if (end == std::string_view::npos)
                break;
            remaining.remove_prefix(end + 1);
        }
    }
    const auto parsed = parseLrc(result);
    if (parsed.error != LyricsParseError::None || parsed.document.lines != document.lines)
        return {};
    return result;
}
} // namespace music_player::m0
