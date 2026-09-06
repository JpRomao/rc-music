#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace music_player::m0 {

enum class MetadataTextDisposition { Unchanged, Sanitized, Empty, InvalidEncoding };

struct SanitizedMetadataText final {
    std::string text;
    MetadataTextDisposition disposition{MetadataTextDisposition::Unchanged};
};

// Pure display policy. Callers apply the original byte limit before normalization.
[[nodiscard]] SanitizedMetadataText sanitizeMetadataText(std::string_view value);
// Strict numeric tags: no signs, embedded controls, partial parses or overflow.
[[nodiscard]] std::optional<std::uint32_t> parseMetadataTrackNumber(std::string_view value) noexcept;
// Bare year or calendar ISO date/time precision used by the supported tag formats.
[[nodiscard]] std::optional<std::uint32_t> parseMetadataYear(std::string_view value) noexcept;

} // namespace music_player::m0
