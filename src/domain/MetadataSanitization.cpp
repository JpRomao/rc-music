#include "m0/domain/MetadataSanitization.hpp"

#include <charconv>
#include <chrono>
#include <system_error>
#include <utility>

namespace music_player::m0 {
namespace {

bool nextScalar(const std::string_view value, std::size_t& offset, std::uint32_t& scalar) noexcept {
    const auto first = static_cast<unsigned char>(value[offset]);
    const std::size_t count = first < 0x80                     ? 1
                              : first >= 0xc2 && first <= 0xdf ? 2
                              : first >= 0xe0 && first <= 0xef ? 3
                              : first >= 0xf0 && first <= 0xf4 ? 4
                                                               : 0;
    if (count == 0 || count > value.size() - offset)
        return false;
    scalar = first & (count == 1 ? 0x7fU : count == 2 ? 0x1fU : count == 3 ? 0x0fU : 0x07U);
    for (std::size_t index = 1; index < count; ++index) {
        const auto byte = static_cast<unsigned char>(value[offset + index]);
        if ((byte & 0xc0U) != 0x80U)
            return false;
        scalar = (scalar << 6U) | (byte & 0x3fU);
    }
    const std::uint32_t minimum = count == 1 ? 0 : count == 2 ? 0x80 : count == 3 ? 0x800 : 0x10000;
    if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
        return false;
    offset += count;
    return true;
}

bool separator(const std::uint32_t scalar) noexcept {
    return scalar <= 0x20 || (scalar >= 0x7f && scalar <= 0x9f) || scalar == 0xa0 || scalar == 0x1680 ||
           (scalar >= 0x2000 && scalar <= 0x200a) || scalar == 0x2028 || scalar == 0x2029 || scalar == 0x202f ||
           scalar == 0x205f || scalar == 0x3000;
}

bool invisibleLayoutControl(const std::uint32_t scalar) noexcept {
    // Keep ZWNJ/ZWJ and variation selectors: they are meaningful in scripts and emoji.
    return scalar == 0x061c || scalar == 0x200b || scalar == 0x200e || scalar == 0x200f ||
           (scalar >= 0x202a && scalar <= 0x202e) || scalar == 0x2060 || (scalar >= 0x2066 && scalar <= 0x2069) ||
           scalar == 0xfeff;
}

std::string_view trimNumeric(const std::string_view value) noexcept {
    constexpr std::string_view whitespace = " \t\r\n\v\f";
    const auto first = value.find_first_not_of(whitespace);
    if (first == std::string_view::npos)
        return {};
    return value.substr(first, value.find_last_not_of(whitespace) - first + 1);
}

std::optional<std::uint32_t> unsignedDecimal(const std::string_view value) noexcept {
    if (value.empty())
        return std::nullopt;
    for (const char digit : value) {
        if (digit < '0' || digit > '9')
            return std::nullopt;
    }
    std::uint32_t result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        return std::nullopt;
    return result;
}

bool timeComponent(const std::string_view time, std::size_t& offset, const std::uint32_t maximum) noexcept {
    if (time.size() - offset < 2)
        return false;
    const auto component = unsignedDecimal(time.substr(offset, 2));
    if (!component.has_value() || *component > maximum)
        return false;
    offset += 2;
    return true;
}

bool calendarTime(const std::string_view time) noexcept {
    std::size_t offset = 0;
    if (!timeComponent(time, offset, 23))
        return false;
    bool has_seconds = false;
    if (offset < time.size() && time[offset] == ':') {
        ++offset;
        if (!timeComponent(time, offset, 59))
            return false;
        if (offset < time.size() && time[offset] == ':') {
            ++offset;
            if (!timeComponent(time, offset, 59))
                return false;
            has_seconds = true;
        }
    }
    if (has_seconds && offset < time.size() && time[offset] == '.') {
        const auto first_fraction = ++offset;
        while (offset < time.size() && time[offset] >= '0' && time[offset] <= '9')
            ++offset;
        if (first_fraction == offset)
            return false;
    }
    if (offset == time.size())
        return true;
    if (time[offset] == 'Z')
        return offset + 1 == time.size();
    if (time[offset] != '+' && time[offset] != '-')
        return false;
    ++offset;
    if (!timeComponent(time, offset, 23) || offset == time.size() || time[offset++] != ':')
        return false;
    return timeComponent(time, offset, 59) && offset == time.size();
}

} // namespace

SanitizedMetadataText sanitizeMetadataText(const std::string_view value) {
    std::string text;
    text.reserve(value.size());
    bool pending_space = false;
    std::size_t offset = 0;
    while (offset < value.size()) {
        const auto start = offset;
        std::uint32_t scalar{};
        if (!nextScalar(value, offset, scalar))
            return {{}, MetadataTextDisposition::InvalidEncoding};
        if (invisibleLayoutControl(scalar))
            continue;
        if (separator(scalar)) {
            pending_space = !text.empty();
            continue;
        }
        if (pending_space)
            text.push_back(' ');
        pending_space = false;
        text.append(value.substr(start, offset - start));
    }
    const auto disposition = text.empty()    ? MetadataTextDisposition::Empty
                             : text == value ? MetadataTextDisposition::Unchanged
                                             : MetadataTextDisposition::Sanitized;
    return {std::move(text), disposition};
}

std::optional<std::uint32_t> parseMetadataTrackNumber(const std::string_view value) noexcept {
    const auto text = trimNumeric(value);
    const auto slash = text.find('/');
    const auto number = unsignedDecimal(text.substr(0, slash));
    if (!number.has_value() || *number == 0)
        return std::nullopt;
    if (slash != std::string_view::npos) {
        const auto total = unsignedDecimal(text.substr(slash + 1));
        if (!total.has_value() || *total == 0)
            return std::nullopt;
    }
    return number;
}

std::optional<std::uint32_t> parseMetadataYear(const std::string_view value) noexcept {
    const auto text = trimNumeric(value);
    const auto year = unsignedDecimal(text.substr(0, 4));
    if (!year.has_value() || *year == 0 || *year > 9999)
        return std::nullopt;
    if (text.size() <= 4)
        return year;
    if (text.size() < 7 || text[4] != '-')
        return std::nullopt;
    const auto month = unsignedDecimal(text.substr(5, 2));
    if (!month.has_value() || *month == 0 || *month > 12)
        return std::nullopt;
    if (text.size() == 7)
        return year;
    if (text.size() < 10 || text[7] != '-')
        return std::nullopt;
    const auto day = unsignedDecimal(text.substr(8, 2));
    if (!day.has_value())
        return std::nullopt;
    const auto date = std::chrono::year{static_cast<int>(*year)} / std::chrono::month{*month} / std::chrono::day{*day};
    if (!date.ok())
        return std::nullopt;
    if (text.size() == 10)
        return year;
    if (text[10] != 'T' || !calendarTime(text.substr(11)))
        return std::nullopt;
    return year;
}

} // namespace music_player::m0
