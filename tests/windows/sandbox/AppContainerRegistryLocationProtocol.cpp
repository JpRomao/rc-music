#include "AppContainerRegistryLocationProtocol.hpp"

#include <algorithm>
#include <utility>

namespace music_player::sandbox_registry_location {
namespace {
constexpr std::array<std::uint8_t, 8> request_magic{'R', 'C', 'M', 'R', 'L', 'Q', '0', '1'};
constexpr std::array<std::uint8_t, 8> reply_magic{'R', 'C', 'M', 'R', 'L', 'P', '0', '1'};
constexpr std::uint32_t known_presence_bits = (std::uint32_t{1} << 14) - 1;
using Fields = std::array<std::optional<std::uint32_t>, 13>;

bool validSid(const ProfileSid& sid) noexcept {
    constexpr std::array<std::uint8_t, 12> prefix{1, 8, 0, 0, 0, 0, 0, 15, 2, 0, 0, 0};
    return std::equal(prefix.begin(), prefix.end(), sid.begin());
}
std::uint32_t read(const std::span<const std::uint8_t> bytes, const std::size_t at, const unsigned count) noexcept {
    std::uint32_t value = 0;
    for (unsigned index = 0; index < count; ++index)
        value |= std::uint32_t{bytes[at + index]} << (index * 8);
    return value;
}
void write(std::vector<std::uint8_t>& bytes, const std::size_t at, const std::uint32_t value,
           const unsigned count) noexcept {
    for (unsigned index = 0; index < count; ++index)
        bytes[at + index] = static_cast<std::uint8_t>(value >> (index * 8));
}
bool high(const char16_t unit) noexcept { return unit >= 0xd800 && unit <= 0xdbff; }
bool low(const char16_t unit) noexcept { return unit >= 0xdc00 && unit <= 0xdfff; }
template <typename Unit> bool validName(const std::size_t count, Unit unit) noexcept {
    if (count > maximum_native_name_units)
        return false;
    for (std::size_t index = 0; index < count; ++index) {
        const auto current = unit(index);
        if (current == 0 || low(current))
            return false;
        if (high(current) && (++index == count || !low(unit(index))))
            return false;
    }
    return true;
}
bool validName(const std::u16string& name) noexcept {
    return validName(name.size(), [&name](const std::size_t index) { return name[index]; });
}
Fields fields(const Reply& value) noexcept {
    const auto& token = value.token_errors;
    return {token.token_query_error, token.failed_information_class, token.process_token_close,
            token.thread_token_open, token.thread_token_close,       value.registry_location_hresult,
            value.name_size_status,  value.name_size_win32,          value.name_size_required,
            value.name_read_status,  value.name_read_win32,          value.name_read_required,
            value.key_close_error};
}
void setFields(Reply& value, const Fields& data) noexcept {
    value.token_errors = {data[0], data[1], data[2], data[3], data[4]};
    value.registry_location_hresult = data[5];
    value.name_size_status = data[6];
    value.name_size_win32 = data[7];
    value.name_size_required = data[8];
    value.name_read_status = data[9];
    value.name_read_win32 = data[10];
    value.name_read_required = data[11];
    value.key_close_error = data[12];
}
bool tokenAdmitted(const TokenErrors& token) noexcept {
    return token.token_query_error == 0U && token.failed_information_class == 0U && token.process_token_close == 0U &&
           token.thread_token_open == 1008U && !token.thread_token_close;
}
bool hasQueryData(const Reply& value) noexcept {
    return value.name_size_status || value.name_size_win32 || value.name_size_required || value.name_read_status ||
           value.name_read_win32 || value.name_read_required;
}
bool nameCaptured(const Reply& value) noexcept {
    const auto actual = static_cast<std::uint32_t>(4 + 2 * value.native_name.size());
    return !value.native_name.empty() && value.registry_location_hresult == 0U &&
           (value.name_size_status == 0xc0000023U || value.name_size_status == 0x80000005U) && value.name_size_win32 &&
           value.name_size_required && *value.name_size_required >= 6U && *value.name_size_required <= 65538U &&
           value.name_read_status == 0U && value.name_read_win32 == 0U && value.name_read_required == actual &&
           actual <= *value.name_size_required;
}
bool validReply(const Reply& value) noexcept {
    if ((value.session_package_sid && !validSid(*value.session_package_sid)) || !validName(value.native_name))
        return false;
    if (value.outcome == Outcome::RefusedToken)
        return !value.registry_location_hresult && !hasQueryData(value) && !value.key_close_error &&
               !value.uncertain_output && value.native_name.empty();
    if (!value.session_package_sid || !tokenAdmitted(value.token_errors))
        return false;
    if (!value.registry_location_hresult)
        return value.outcome == Outcome::CloseUnknown && value.uncertain_output && !hasQueryData(value) &&
               !value.key_close_error && value.native_name.empty();
    switch (value.outcome) {
    case Outcome::CapturedName:
        return !value.uncertain_output && value.key_close_error == 0U && nameCaptured(value);
    case Outcome::NativeApiError:
        return !value.uncertain_output && value.native_name.empty() &&
               (!value.key_close_error || value.key_close_error == 0U);
    case Outcome::CloseUnknown:
        return (value.uncertain_output || !value.key_close_error || value.key_close_error != 0U) &&
               (value.native_name.empty() || nameCaptured(value));
    case Outcome::RefusedToken:
        break;
    }
    return false;
}
bool envelope(const std::span<const std::uint8_t> bytes, const std::array<std::uint8_t, 8>& magic) noexcept {
    return std::equal(magic.begin(), magic.end(), bytes.begin()) && read(bytes, 8, 2) == 1;
}
} // namespace

std::vector<std::uint8_t> encodeRequest(const Request& value) {
    if (!validSid(value.package_sid))
        return {};
    std::vector<std::uint8_t> bytes(request_frame_bytes, 0);
    std::copy(request_magic.begin(), request_magic.end(), bytes.begin());
    write(bytes, 8, 1, 2);
    std::copy(value.package_sid.begin(), value.package_sid.end(), bytes.begin() + 12);
    std::copy(value.nonce.begin(), value.nonce.end(), bytes.begin() + 52);
    return bytes;
}
std::optional<Request> decodeRequest(const std::span<const std::uint8_t> bytes) {
    if (bytes.size() != request_frame_bytes || !envelope(bytes, request_magic) || read(bytes, 10, 2) != 0)
        return std::nullopt;
    Request value;
    std::copy_n(bytes.begin() + 12, value.package_sid.size(), value.package_sid.begin());
    std::copy_n(bytes.begin() + 52, value.nonce.size(), value.nonce.begin());
    return validSid(value.package_sid) ? std::optional<Request>{value} : std::nullopt;
}
std::vector<std::uint8_t> encodeReply(const Reply& value) {
    if (!validReply(value))
        return {};
    const auto count = value.native_name.size();
    if (count > (maximum_frame_bytes - reply_header_bytes) / 2)
        return {};
    std::vector<std::uint8_t> bytes(reply_header_bytes + 2 * count, 0);
    std::copy(reply_magic.begin(), reply_magic.end(), bytes.begin());
    write(bytes, 8, 1, 2);
    write(bytes, 10, static_cast<std::uint8_t>(value.outcome), 1);
    write(bytes, 11, value.uncertain_output ? 1 : 0, 1);
    std::copy(value.nonce.begin(), value.nonce.end(), bytes.begin() + 16);
    std::uint32_t mask = value.session_package_sid ? 1 : 0;
    if (value.session_package_sid)
        std::copy(value.session_package_sid->begin(), value.session_package_sid->end(), bytes.begin() + 48);
    const auto data = fields(value);
    for (std::size_t index = 0; index < data.size(); ++index)
        if (data[index]) {
            mask |= std::uint32_t{1} << (index + 1);
            write(bytes, 88 + 4 * index, *data[index], 4);
        }
    write(bytes, 12, mask, 4);
    write(bytes, 140, static_cast<std::uint32_t>(count), 4);
    for (std::size_t index = 0; index < count; ++index)
        write(bytes, reply_header_bytes + 2 * index, value.native_name[index], 2);
    return bytes;
}
std::optional<Reply> decodeReply(const std::span<const std::uint8_t> bytes) {
    if (bytes.size() < reply_header_bytes || bytes.size() > maximum_frame_bytes || !envelope(bytes, reply_magic) ||
        bytes[10] < static_cast<std::uint8_t>(Outcome::RefusedToken) ||
        bytes[10] > static_cast<std::uint8_t>(Outcome::CloseUnknown) || bytes[11] > 1)
        return std::nullopt;
    const auto mask = read(bytes, 12, 4);
    const auto count = read(bytes, 140, 4);
    if ((mask & ~known_presence_bits) || count > maximum_native_name_units ||
        count > (maximum_frame_bytes - reply_header_bytes) / 2 || bytes.size() != reply_header_bytes + 2 * count)
        return std::nullopt;
    // The wire size, count and every code unit are checked before allocating a
    // string. Optional scalars and SID are fixed-size stack values.
    if (!validName(count, [bytes](const std::size_t index) {
            return static_cast<char16_t>(read(bytes, reply_header_bytes + 2 * index, 2));
        }))
        return std::nullopt;
    Reply value;
    value.outcome = static_cast<Outcome>(bytes[10]);
    value.uncertain_output = bytes[11] == 1;
    std::copy_n(bytes.begin() + 16, value.nonce.size(), value.nonce.begin());
    ProfileSid sid{};
    std::copy_n(bytes.begin() + 48, sid.size(), sid.begin());
    if (mask & 1)
        value.session_package_sid = sid;
    else if (std::any_of(sid.begin(), sid.end(), [](const auto byte) { return byte != 0; }))
        return std::nullopt;
    Fields data{};
    for (std::size_t index = 0; index < data.size(); ++index) {
        const auto raw = read(bytes, 88 + 4 * index, 4);
        if (mask & (std::uint32_t{1} << (index + 1)))
            data[index] = raw;
        else if (raw != 0)
            return std::nullopt;
    }
    setFields(value, data);
    value.native_name.resize(count);
    for (std::size_t index = 0; index < count; ++index)
        value.native_name[index] = static_cast<char16_t>(read(bytes, reply_header_bytes + 2 * index, 2));
    return validReply(value) ? std::optional<Reply>{std::move(value)} : std::nullopt;
}
bool matchesRequest(const Request& request, const Reply& reply) noexcept {
    return validSid(request.package_sid) && validReply(reply) && reply.session_package_sid == request.package_sid &&
           reply.nonce == request.nonce;
}
} // namespace music_player::sandbox_registry_location
