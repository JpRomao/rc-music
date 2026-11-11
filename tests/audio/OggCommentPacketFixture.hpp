#pragma once

#include <vorbisfile.h>
#include <xiphcomment.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace music_player::m0::test {

[[nodiscard]] inline bool writeOggWithCommentPacketSize(const std::filesystem::path& source,
                                                        const std::filesystem::path& destination,
                                                        const std::size_t packet_bytes) {
    if (!std::filesystem::copy_file(source, destination))
        return false;
    TagLib::Vorbis::File file{destination.c_str()};
    auto* tag = file.tag();
    if (!file.isValid() || tag == nullptr)
        return false;

    constexpr std::string_view key = "RC_MUSIC_TEST_PADDING";
    constexpr std::size_t vorbis_header_bytes = 7;
    constexpr std::size_t field_length_bytes = 4;
    const auto unpadded_packet_bytes = vorbis_header_bytes + tag->render().size();
    const auto padding_field_overhead = field_length_bytes + key.size() + 1;
    if (packet_bytes <= unpadded_packet_bytes + padding_field_overhead)
        return false;
    const std::string value(packet_bytes - unpadded_packet_bytes - padding_field_overhead, 'x');
    tag->addField(TagLib::String{key.data(), TagLib::String::Latin1}, TagLib::String{value, TagLib::String::Latin1},
                  false);
    return file.save();
}

} // namespace music_player::m0::test
