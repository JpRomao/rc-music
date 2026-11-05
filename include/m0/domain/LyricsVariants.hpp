#pragma once

#include "m0/domain/Lyrics.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace music_player::m0 {
inline constexpr std::size_t maximum_lyrics_variants = 16;
inline constexpr std::size_t maximum_lyrics_variant_name_bytes = 128;
inline constexpr std::size_t maximum_lyrics_variant_tag_bytes = 64;
inline constexpr std::size_t maximum_lyrics_variant_source_bytes = 2048;
inline constexpr std::size_t maximum_lyrics_variants_content_bytes = 4 * 1024 * 1024;

enum class LyricsVariantKind { Original, Romanized };
struct LyricsVariant final {
    std::uint64_t id{};
    std::string name{}, language{}, script{}, source{}, content{};
    LyricsVariantKind kind{LyricsVariantKind::Original};
    bool generated{};
    [[nodiscard]] bool operator==(const LyricsVariant&) const = default;
};
using LyricsVariants = std::vector<LyricsVariant>;

// Bounded UTF-8/single-line metadata; content is independently parsed as LRC
// or plain text. IDs are nonzero and unique, while duplicate text is allowed.
[[nodiscard]] bool isValidLyricsVariants(const LyricsVariants& variants);
// Serializes synchronized cues without inventing times; otherwise plain text.
// Refuses values that cannot round-trip through the existing bounded parser.
[[nodiscard]] std::optional<std::string> lyricsDocumentAsLrc(const LyricsDocument& document);
} // namespace music_player::m0
