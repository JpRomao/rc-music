#include "m0/domain/Lyrics.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace music_player::m0 {
namespace {
using Error = LyricsParseError;
LyricsParseResult failure(const Error error) { return {error, {}}; }

Error validateText(const std::string_view text) noexcept {
    for (std::size_t offset = 0; offset < text.size();) {
        const auto first = static_cast<unsigned char>(text[offset]);
        const std::size_t count = first < 0x80                     ? 1
                                  : first >= 0xc2 && first <= 0xdf ? 2
                                  : first >= 0xe0 && first <= 0xef ? 3
                                  : first >= 0xf0 && first <= 0xf4 ? 4
                                                                   : 0;
        if (!count || count > text.size() - offset)
            return Error::InvalidUtf8;
        std::uint32_t scalar = first & (count == 1 ? 0x7fU : count == 2 ? 0x1fU : count == 3 ? 0x0fU : 0x07U);
        for (std::size_t index = 1; index < count; ++index) {
            const auto byte = static_cast<unsigned char>(text[offset + index]);
            if ((byte & 0xc0U) != 0x80U)
                return Error::InvalidUtf8;
            scalar = (scalar << 6U) | (byte & 0x3fU);
        }
        const std::uint32_t minimum = count == 1 ? 0U : count == 2 ? 0x80U : count == 3 ? 0x800U : 0x10000U;
        if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
            return Error::InvalidUtf8;
        if ((scalar < 0x20 && scalar != '\t' && scalar != '\r' && scalar != '\n') || scalar == 0x7f)
            return Error::InvalidFormat;
        offset += count;
    }
    return Error::None;
}
bool digit(const char value) noexcept { return value >= '0' && value <= '9'; }
std::string_view trimHorizontal(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    return text;
}
bool equalAsciiInsensitive(const std::string_view left, const std::string_view right) noexcept {
    return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(), [](char a, char b) {
               if (a >= 'A' && a <= 'Z')
                   a = static_cast<char>(a + ('a' - 'A'));
               return a == b;
           });
}
bool metadataKey(const std::string_view key) noexcept {
    constexpr std::array<std::string_view, 11> keys{"ar", "al",     "ti", "au",   "by",    "re",
                                                    "ve", "length", "id", "tool", "offset"};
    return std::any_of(keys.begin(), keys.end(), [key](const auto known) { return equalAsciiInsensitive(key, known); });
}
struct Timestamp final {
    Error error{Error::InvalidFormat};
    std::int64_t milliseconds{};
};
Timestamp timestamp(const std::string_view tag) noexcept {
    const auto colon = tag.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon > 4 || tag.size() < colon + 3)
        return {};
    const auto minutes_text = tag.substr(0, colon);
    if (!std::all_of(minutes_text.begin(), minutes_text.end(), digit) || !digit(tag[colon + 1]) ||
        !digit(tag[colon + 2]))
        return {};
    std::int64_t minutes{};
    for (const char value : minutes_text)
        minutes = minutes * 10 + value - '0';
    const auto seconds = (tag[colon + 1] - '0') * 10 + tag[colon + 2] - '0';
    if (seconds > 59)
        return {};
    auto remainder = tag.substr(colon + 3);
    std::int64_t fraction{};
    if (!remainder.empty()) {
        if (remainder.front() != '.')
            return {};
        remainder.remove_prefix(1);
        if (remainder.empty() || remainder.size() > 3 || !std::all_of(remainder.begin(), remainder.end(), digit))
            return {};
        for (const char value : remainder)
            fraction = fraction * 10 + value - '0';
        for (std::size_t size = remainder.size(); size < 3; ++size)
            fraction *= 10;
    }
    const auto milliseconds = minutes * 60'000 + seconds * 1000 + fraction;
    return {milliseconds > lyrics_max_timestamp_ms ? Error::LimitExceeded : Error::None, milliseconds};
}
} // namespace

LyricsParseResult parseLrc(std::string_view text) {
    if (text.size() > lyrics_max_input_bytes)
        return failure(Error::LimitExceeded);
    if (const auto error = validateText(text); error != Error::None)
        return failure(error);
    if (text.starts_with("\xef\xbb\xbf"))
        text.remove_prefix(3);

    LyricsDocument document;
    std::size_t expanded_bytes{};
    bool has_plain_line = false;
    while (!text.empty()) {
        const auto end = text.find_first_of("\r\n");
        const auto line = text.substr(0, end);
        if (end == std::string_view::npos)
            text = {};
        else {
            const bool crlf = text[end] == '\r' && end + 1 < text.size() && text[end + 1] == '\n';
            text.remove_prefix(end + (crlf ? 2 : 1));
        }
        auto remaining = trimHorizontal(line);
        if (remaining.starts_with('[')) {
            const auto close = remaining.find(']');
            const auto colon = remaining.find(':');
            if (close != std::string_view::npos && colon != std::string_view::npos && colon < close &&
                metadataKey(remaining.substr(1, colon - 1))) {
                if (!trimHorizontal(remaining.substr(close + 1)).empty())
                    return failure(Error::InvalidFormat);
                if (equalAsciiInsensitive(remaining.substr(1, colon - 1), "offset")) {
                    auto offset = trimHorizontal(remaining.substr(colon + 1, close - colon - 1));
                    if (!offset.empty() && (offset.front() == '+' || offset.front() == '-'))
                        offset.remove_prefix(1);
                    if (offset.empty() ||
                        !std::all_of(offset.begin(), offset.end(), [](const char v) { return v == '0'; }))
                        return failure(Error::InvalidFormat);
                }
                continue;
            }
        }
        std::vector<std::int64_t> times;
        while (remaining.size() > 1 && remaining.front() == '[' &&
               (digit(remaining[1]) || remaining[1] == '+' || remaining[1] == '-')) {
            const auto close = remaining.find(']');
            if (close == std::string_view::npos)
                return failure(Error::InvalidFormat);
            const auto parsed = timestamp(remaining.substr(1, close - 1));
            if (parsed.error != Error::None)
                return failure(parsed.error);
            if (times.size() == lyrics_max_cues - document.lines.size())
                return failure(Error::LimitExceeded);
            times.push_back(parsed.milliseconds);
            remaining = trimHorizontal(remaining.substr(close + 1));
        }
        const auto verse = times.empty() ? line : remaining;
        const auto separator_bytes = has_plain_line ? std::size_t{1} : 0;
        if (separator_bytes > lyrics_max_input_bytes - document.plain_text.size() ||
            verse.size() > lyrics_max_input_bytes - document.plain_text.size() - separator_bytes)
            return failure(Error::LimitExceeded);
        if (has_plain_line)
            document.plain_text.push_back('\n');
        document.plain_text.append(verse);
        has_plain_line = true;
        if (!times.empty() && verse.size() > (lyrics_max_expanded_bytes - expanded_bytes) / times.size())
            return failure(Error::LimitExceeded);
        expanded_bytes += times.size() * verse.size();
        for (const auto time : times)
            document.lines.push_back({time, std::string{verse}});
    }
    while (!document.plain_text.empty() && document.plain_text.back() == '\n')
        document.plain_text.pop_back();

    std::stable_sort(document.lines.begin(), document.lines.end(),
                     [](const auto& left, const auto& right) { return left.timestamp_ms < right.timestamp_ms; });
    std::size_t joins{};
    for (std::size_t index = 1; index < document.lines.size(); ++index)
        if (document.lines[index - 1].timestamp_ms == document.lines[index].timestamp_ms)
            ++joins;
    if (joins > lyrics_max_expanded_bytes - expanded_bytes)
        return failure(Error::LimitExceeded);
    std::vector<LyricsLine> merged;
    merged.reserve(document.lines.size());
    for (auto& cue : document.lines) {
        if (!merged.empty() && merged.back().timestamp_ms == cue.timestamp_ms) {
            merged.back().text.push_back('\n');
            merged.back().text += cue.text;
        } else
            merged.push_back(std::move(cue));
    }
    document.lines = std::move(merged);
    return {Error::None, std::move(document)};
}

std::optional<std::size_t> lyricsLineAt(const LyricsDocument& document, const std::int64_t position_ms) noexcept {
    const auto found = std::upper_bound(document.lines.begin(), document.lines.end(), position_ms,
                                        [](const auto time, const auto& cue) { return time < cue.timestamp_ms; });
    if (found == document.lines.begin())
        return std::nullopt;
    return static_cast<std::size_t>(found - document.lines.begin() - 1);
}

} // namespace music_player::m0
