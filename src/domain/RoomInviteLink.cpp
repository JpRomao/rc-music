#include "m0/domain/RoomInviteLink.hpp"

#include <algorithm>
#include <cstddef>
#include <span>

namespace music_player::m0 {
namespace {
constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string encode(const std::span<const std::uint8_t> bytes) {
    std::string out;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const std::uint8_t byte : bytes) {
        buffer = (buffer << 8) | byte;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out += alphabet[(buffer >> bits) & 0x3F];
        }
        buffer &= (1U << bits) - 1U;
    }
    if (bits > 0)
        out += alphabet[(buffer << (6 - bits)) & 0x3F];
    return out;
}

// Fills exactly out.size() bytes; rejects bad characters, wrong length and non-zero trailing bits.
bool decode(const std::string_view text, const std::span<std::uint8_t> out) {
    if (text.size() != (out.size() * 8 + 5) / 6)
        return false;
    std::uint32_t buffer = 0;
    int bits = 0;
    std::size_t written = 0;
    for (const char c : text) {
        const std::size_t value = alphabet.find(c);
        if (value == std::string_view::npos)
            return false;
        buffer = (buffer << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (written >= out.size())
                return false;
            out[written++] = static_cast<std::uint8_t>((buffer >> bits) & 0xFF);
            buffer &= (1U << bits) - 1U;
        }
    }
    return written == out.size() && buffer == 0;
}

char lowerAscii(const char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool equalsIgnoreCase(const std::string_view a, const std::string_view b) {
    return std::ranges::equal(a, b, [](const char x, const char y) { return lowerAscii(x) == lowerAscii(y); });
}

std::string_view trimmed(std::string_view text) {
    if (text.ends_with("\r\n"))
        text.remove_suffix(2);
    else if (text.ends_with('\n'))
        text.remove_suffix(1);
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    return text;
}
} // namespace

std::string formatInviteLink(const RoomInviteLink& link, const std::string_view worker_origin) {
    std::string out(worker_origin);
    out += "/j/";
    out += encode(link.room_id);
    out += '#';
    out += encode(link.secret);
    out += '.';
    out += encode(link.host_fingerprint);
    return out;
}

std::optional<RoomInviteLink> parseInviteLink(const std::string_view text, const std::string_view worker_origin) {
    std::string_view rest = trimmed(text);
    const std::size_t scheme_end = worker_origin.find("://");
    if (scheme_end == std::string_view::npos)
        return std::nullopt;
    const std::string_view scheme = worker_origin.substr(0, scheme_end + 3);
    const std::string_view host = worker_origin.substr(scheme_end + 3);
    if (host.empty() || rest.size() < worker_origin.size())
        return std::nullopt;
    if (rest.substr(0, scheme.size()) != scheme || !equalsIgnoreCase(rest.substr(scheme.size(), host.size()), host))
        return std::nullopt;
    rest.remove_prefix(worker_origin.size());
    if (!rest.starts_with("/j/"))
        return std::nullopt;
    rest.remove_prefix(3);

    const std::size_t hash = rest.find('#');
    if (hash == std::string_view::npos)
        return std::nullopt;
    const std::string_view room = rest.substr(0, hash);
    const std::string_view fragment = rest.substr(hash + 1);
    const std::size_t dot = fragment.find('.');
    if (dot == std::string_view::npos)
        return std::nullopt;

    RoomInviteLink link;
    if (!decode(room, link.room_id) || !decode(fragment.substr(0, dot), link.secret) ||
        !decode(fragment.substr(dot + 1), link.host_fingerprint))
        return std::nullopt;
    return link;
}

} // namespace music_player::m0
