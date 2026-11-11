#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>

namespace music_player::m0::test {

[[nodiscard]] inline bool writeMp3WithPaddedId3(const std::filesystem::path& source,
                                                const std::filesystem::path& destination,
                                                const std::size_t tag_body_size) {
    constexpr std::array<char, 10> fixture_header{'I', 'D', '3', 3, 0, 0, 0, 0, 2, 28};
    constexpr std::size_t fixture_body_size = 284;
    constexpr std::size_t fixture_file_size = 33'318;
    constexpr std::size_t maximum_id3_body_size = (1U << 28U) - 1U;
    if (tag_body_size < fixture_body_size || tag_body_size > maximum_id3_body_size ||
        std::filesystem::file_size(source) != fixture_file_size || std::filesystem::exists(destination)) {
        return false;
    }

    std::ifstream input{source, std::ios::binary};
    std::array<char, fixture_header.size()> header{};
    std::array<char, fixture_body_size> tag_body{};
    if (!input.read(header.data(), header.size()) || header != fixture_header ||
        !input.read(tag_body.data(), tag_body.size())) {
        return false;
    }

    for (std::size_t index = 6; index < header.size(); ++index) {
        const auto shift = (header.size() - 1U - index) * 7U;
        header[index] = static_cast<char>((tag_body_size >> shift) & 0x7fU);
    }

    std::ofstream output{destination, std::ios::binary};
    output.write(header.data(), header.size());
    output.write(tag_body.data(), tag_body.size());
    // Leave a zero-filled gap without constructing a buffer of the padded size.
    output.seekp(static_cast<std::streamoff>(header.size() + tag_body_size));
    output << input.rdbuf();
    output.close();
    return output && !input.bad() &&
           std::filesystem::file_size(destination) == fixture_file_size - fixture_body_size + tag_body_size;
}

} // namespace music_player::m0::test
