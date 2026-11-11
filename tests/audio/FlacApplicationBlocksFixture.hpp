#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string_view>

namespace music_player::m0::test {

[[nodiscard]] inline bool writeFlacWithApplicationBlocks(const std::filesystem::path& source,
                                                         const std::filesystem::path& destination,
                                                         const std::size_t count, const std::size_t body_size) {
    constexpr std::size_t fixture_size = 60'531;
    constexpr std::size_t header_size = 4;
    constexpr unsigned int last_block_flag = 0x80U;
    constexpr unsigned int application_block = 2U;
    constexpr std::string_view flac_marker = "fLaC";
    constexpr std::string_view application_id = "RCMU";
    constexpr auto maximum_size = static_cast<std::uintmax_t>(std::numeric_limits<std::streamoff>::max());
    if (count == 0 || body_size < application_id.size() || body_size > 0xffffffU ||
        count > (maximum_size - fixture_size) / (body_size + header_size) ||
        std::filesystem::file_size(source) != fixture_size || std::filesystem::exists(destination)) {
        return false;
    }

    std::ifstream input{source, std::ios::binary};
    std::array<char, fixture_size> original{};
    if (!input.read(original.data(), original.size()) ||
        !std::equal(flac_marker.begin(), flac_marker.end(), original.begin())) {
        return false;
    }

    std::size_t offset = flac_marker.size();
    while (true) {
        if (original.size() - offset < header_size)
            return false;
        const auto read_byte = [&](const std::size_t index) {
            return static_cast<std::size_t>(static_cast<unsigned char>(original[index]));
        };
        const auto metadata_header = offset;
        const auto size = (read_byte(offset + 1) << 16U) | (read_byte(offset + 2) << 8U) | read_byte(offset + 3);
        offset += header_size;
        if (size >= original.size() - offset)
            return false;
        offset += size;
        if ((read_byte(metadata_header) & last_block_flag) != 0) {
            original[metadata_header] = static_cast<char>(read_byte(metadata_header) & ~last_block_flag);
            break;
        }
    }

    std::ofstream output{destination, std::ios::binary};
    output.write(original.data(), static_cast<std::streamsize>(offset));
    for (std::size_t index = 0; index < count && output; ++index) {
        const auto flags = application_block | (index + 1 == count ? last_block_flag : 0U);
        const std::array<char, header_size> header{
            static_cast<char>(flags), static_cast<char>((body_size >> 16U) & 0xffU),
            static_cast<char>((body_size >> 8U) & 0xffU), static_cast<char>(body_size & 0xffU)};
        output.write(header.data(), header.size());
        output.write(application_id.data(), application_id.size());
        // Leave a zero-filled gap without allocating a buffer for the whole block.
        if (body_size > application_id.size())
            output.seekp(static_cast<std::streamoff>(body_size - application_id.size()), std::ios::cur);
    }
    output.write(original.data() + offset, static_cast<std::streamsize>(original.size() - offset));
    output.close();
    return output && std::filesystem::file_size(destination) == fixture_size + count * (body_size + header_size);
}

} // namespace music_player::m0::test
