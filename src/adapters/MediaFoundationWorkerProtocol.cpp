#include "adapters/MediaFoundationWorkerProtocol.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace music_player::m0 {
namespace {
constexpr std::array<std::uint8_t, 8> magic{'R', 'C', 'M', 'M', 'F', 'W', 'R', 'Q'};
constexpr std::size_t header_bytes = 38;
constexpr std::size_t maximum_path_units = 8'192;
constexpr std::uint64_t maximum_file_bytes = 1024ULL * 1024 * 1024;
constexpr std::uint64_t maximum_duration_us = 24ULL * 60 * 60 * 1'000'000;

bool asciiEqual(const std::u16string_view value, const std::u16string_view expected) {
    if (value.size() != expected.size())
        return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        auto unit = value[i];
        if (unit >= u'a' && unit <= u'z')
            unit = static_cast<char16_t>(unit - u'a' + u'A');
        if (unit != expected[i])
            return false;
    }
    return true;
}
bool deviceComponent(const std::u16string_view component) {
    auto stem = component.substr(0, component.find(u'.'));
    while (!stem.empty() && stem.back() == u' ')
        stem.remove_suffix(1);
    for (const auto name : {u"CON", u"PRN", u"AUX", u"NUL", u"CONIN$", u"CONOUT$"}) {
        if (asciiEqual(stem, name))
            return true;
    }
    if (stem.size() != 4 || (!asciiEqual(stem.substr(0, 3), u"COM") && !asciiEqual(stem.substr(0, 3), u"LPT")))
        return false;
    const auto suffix = stem[3];
    return (suffix >= u'1' && suffix <= u'9') || suffix == 0x00b9 || suffix == 0x00b2 || suffix == 0x00b3;
}
bool localPath(const std::u16string_view path) {
    if (path.size() < 4 || path.size() > maximum_path_units ||
        !((path[0] >= u'A' && path[0] <= u'Z') || (path[0] >= u'a' && path[0] <= u'z')) || path[1] != u':' ||
        (path[2] != u'\\' && path[2] != u'/'))
        return false;
    std::size_t component_start = 3;
    for (std::size_t i = 3; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == u'\\' || path[i] == u'/') {
            if (i == component_start || path[i - 1] == u'.' || path[i - 1] == u' ' ||
                deviceComponent(path.substr(component_start, i - component_start)))
                return false;
            component_start = i + 1;
            continue;
        }
        const auto unit = path[i];
        if (unit < 0x20 || unit == u':' || unit == u'"' || unit == u'<' || unit == u'>' || unit == u'|' ||
            unit == u'?' || unit == u'*')
            return false;
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (i + 1 >= path.size() || path[i + 1] < 0xdc00 || path[i + 1] > 0xdfff)
                return false;
            ++i;
        } else if (unit >= 0xdc00 && unit <= 0xdfff) {
            return false;
        }
    }
    return true;
}
void append(std::vector<std::uint8_t>& bytes, const std::uint64_t value, const unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}
std::uint64_t read(std::span<const std::uint8_t> bytes, const std::size_t offset, const unsigned count) {
    std::uint64_t result = 0;
    for (unsigned i = 0; i < count; ++i)
        result |= static_cast<std::uint64_t>(bytes[offset + i]) << (i * 8);
    return result;
}
} // namespace

bool validMediaFoundationWorkerRequest(const MediaFoundationWorkerRequest& request) {
    return (request.mode == MediaFoundationWorkerMode::Convert ||
            request.mode == MediaFoundationWorkerMode::Validate) &&
           request.max_input_bytes > 0 && request.max_input_bytes <= maximum_file_bytes &&
           request.max_output_bytes > 0 && request.max_output_bytes <= maximum_file_bytes &&
           request.max_duration_us > 0 && request.max_duration_us <= maximum_duration_us &&
           localPath(request.input_path);
}
std::vector<std::uint8_t> encodeMediaFoundationWorkerRequest(const MediaFoundationWorkerRequest& request) {
    if (!validMediaFoundationWorkerRequest(request))
        return {};
    std::vector<std::uint8_t> bytes;
    bytes.reserve(header_bytes + request.input_path.size() * 2);
    bytes.insert(bytes.end(), magic.begin(), magic.end());
    bytes.push_back(1);
    bytes.push_back(static_cast<std::uint8_t>(request.mode));
    append(bytes, request.max_input_bytes, 8);
    append(bytes, request.max_output_bytes, 8);
    append(bytes, request.max_duration_us, 8);
    append(bytes, request.input_path.size(), 4);
    for (const auto unit : request.input_path)
        append(bytes, unit, 2);
    return bytes;
}
std::optional<MediaFoundationWorkerRequest> decodeMediaFoundationWorkerRequest(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < header_bytes || bytes.size() > media_foundation_worker_max_request_bytes ||
        !std::equal(magic.begin(), magic.end(), bytes.begin()) || bytes[8] != 1)
        return std::nullopt;
    const auto units = read(bytes, 34, 4);
    // Bound the untrusted count before multiplication, allocation or UTF-16 read.
    if (units < 4 || units > maximum_path_units || bytes.size() != header_bytes + units * 2)
        return std::nullopt;
    MediaFoundationWorkerRequest request;
    request.mode = static_cast<MediaFoundationWorkerMode>(bytes[9]);
    request.max_input_bytes = read(bytes, 10, 8);
    request.max_output_bytes = read(bytes, 18, 8);
    request.max_duration_us = read(bytes, 26, 8);
    request.input_path.reserve(static_cast<std::size_t>(units));
    for (std::size_t i = 0; i < units; ++i)
        request.input_path.push_back(static_cast<char16_t>(read(bytes, header_bytes + i * 2, 2)));
    if (!validMediaFoundationWorkerRequest(request))
        return std::nullopt;
    return request;
}
} // namespace music_player::m0
