#include "adapters/MediaFoundationWorkerProtocol.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace music_player::m0 {
namespace {
constexpr std::array<std::uint8_t, 8> magic{'R', 'C', 'M', 'M', 'F', 'W', 'R', 'Q'};
constexpr std::uint64_t maximum_file_bytes = 1024ULL * 1024 * 1024;
constexpr std::uint64_t maximum_duration_us = 24ULL * 60 * 60 * 1'000'000;
static_assert(media_foundation_worker_max_request_bytes == 42);

void append(std::vector<std::uint8_t>& bytes, const std::uint64_t value) {
    for (unsigned index = 0; index < 8; ++index)
        bytes.push_back(static_cast<std::uint8_t>(value >> (index * 8)));
}
std::uint64_t read(std::span<const std::uint8_t> bytes, const std::size_t offset) {
    std::uint64_t value = 0;
    for (unsigned index = 0; index < 8; ++index)
        value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8);
    return value;
}
} // namespace

bool validMediaFoundationWorkerRequest(const MediaFoundationWorkerRequest& request) {
    // Accept only positive pointer-width capability values, excluding all
    // negative Win32 pseudo-handles without interpreting them as objects.
    return (request.mode == MediaFoundationWorkerMode::Convert ||
            request.mode == MediaFoundationWorkerMode::Validate) &&
           request.input_handle > 0 &&
           request.input_handle <= static_cast<std::uint64_t>(std::numeric_limits<std::intptr_t>::max()) &&
           request.max_input_bytes > 0 && request.max_input_bytes <= maximum_file_bytes &&
           request.max_output_bytes > 0 && request.max_output_bytes <= maximum_file_bytes &&
           request.max_duration_us > 0 && request.max_duration_us <= maximum_duration_us;
}
std::vector<std::uint8_t> encodeMediaFoundationWorkerRequest(const MediaFoundationWorkerRequest& request) {
    if (!validMediaFoundationWorkerRequest(request))
        return {};
    std::vector<std::uint8_t> bytes;
    bytes.reserve(media_foundation_worker_max_request_bytes);
    bytes.insert(bytes.end(), magic.begin(), magic.end());
    bytes.push_back(2);
    bytes.push_back(static_cast<std::uint8_t>(request.mode));
    append(bytes, request.max_input_bytes);
    append(bytes, request.max_output_bytes);
    append(bytes, request.max_duration_us);
    append(bytes, request.input_handle);
    return bytes;
}
std::optional<MediaFoundationWorkerRequest> decodeMediaFoundationWorkerRequest(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != media_foundation_worker_max_request_bytes ||
        !std::equal(magic.begin(), magic.end(), bytes.begin()) || bytes[8] != 2)
        return std::nullopt;
    MediaFoundationWorkerRequest request{.mode = static_cast<MediaFoundationWorkerMode>(bytes[9]),
                                         .input_handle = read(bytes, 34),
                                         .max_input_bytes = read(bytes, 10),
                                         .max_output_bytes = read(bytes, 18),
                                         .max_duration_us = read(bytes, 26)};
    return validMediaFoundationWorkerRequest(request) ? std::optional{request} : std::nullopt;
}
} // namespace music_player::m0
