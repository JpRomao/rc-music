#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace music_player::m0 {

inline constexpr std::size_t lyrics_max_input_bytes = 256 * 1024;
inline constexpr std::size_t lyrics_max_expanded_bytes = 1024 * 1024;
inline constexpr std::size_t lyrics_max_cues = 4096;
inline constexpr std::int64_t lyrics_max_timestamp_ms = 24 * 60 * 60 * 1000;

struct LyricsLine final {
    std::int64_t timestamp_ms{};
    std::string text{};
    bool operator==(const LyricsLine&) const = default;
};

struct LyricsDocument final {
    // Sorted unique timestamps. Simultaneous lines are joined in source order
    // with a newline; an empty cue clears the previously displayed verse.
    std::vector<LyricsLine> lines{};
    // Untimed display fallback, in source order, without LRC tags/metadata.
    std::string plain_text{};
    bool operator==(const LyricsDocument&) const = default;
};

enum class LyricsParseError { None, InvalidUtf8, InvalidFormat, LimitExceeded };
struct LyricsParseResult final {
    LyricsParseError error{LyricsParseError::InvalidFormat};
    LyricsDocument document{};
};

// Pure bounded parsing: optional UTF-8 BOM, CR/LF/CRLF, mm:ss with optional
// 1-3 decimal digits, multiple timestamps and metadata. UTF-8 is validated
// before copying. NUL/control bytes and malformed timestamp tags are refused.
// Metadata is omitted; nonzero offset is explicitly unsupported/refused.
// The cue limit applies before joining simultaneous lines; the expanded byte
// limit also includes joining separators. Failure returns an empty document.
[[nodiscard]] LyricsParseResult parseLrc(std::string_view text);

// Select the latest cue at/before the requested position. A position before
// the first cue (including a negative position) has no active line. No I/O,
// clock or playback state is owned here: seeks/pauses use the supplied position.
[[nodiscard]] std::optional<std::size_t> lyricsLineAt(const LyricsDocument& document,
                                                      std::int64_t position_ms) noexcept;

} // namespace music_player::m0
