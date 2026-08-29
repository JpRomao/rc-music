#include "m0/adapters/TagLibMetadataExtractor.hpp"

#include <mpegfile.h>
#include <mpegheader.h>
#include <mpegproperties.h>
#include <tag.h>
#include <wavfile.h>
#include <wavproperties.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace music_player::m0 {
namespace {

constexpr std::size_t container_header_size = 12;
using ContainerHeader = std::array<char, container_header_size>;

[[nodiscard]] MetadataReadError error(const MetadataReadErrorCode code, std::string message) {
    return {.code = code, .message = std::move(message)};
}

[[nodiscard]] std::uint8_t byteAt(const ContainerHeader& header, const std::size_t index) noexcept {
    return static_cast<std::uint8_t>(header[index]);
}

[[nodiscard]] bool isRiffWave(const ContainerHeader& header) noexcept {
    return header[0] == 'R' && header[1] == 'I' && header[2] == 'F' && header[3] == 'F' && header[8] == 'W' &&
           header[9] == 'A' && header[10] == 'V' && header[11] == 'E';
}

[[nodiscard]] bool hasId3v2Signature(const ContainerHeader& header) noexcept {
    return header[0] == 'I' && header[1] == 'D' && header[2] == '3';
}

[[nodiscard]] bool hasValidId3v2Header(const ContainerHeader& header, const std::uintmax_t file_size) noexcept {
    if (!hasId3v2Signature(header)) {
        return false;
    }

    const auto major_version = byteAt(header, 3);
    const auto revision = byteAt(header, 4);
    if (major_version < 2 || major_version > 4 || revision == 0xffU) {
        return false;
    }
    for (std::size_t index = 6; index < 10; ++index) {
        if ((byteAt(header, index) & 0x80U) != 0) {
            return false;
        }
    }

    const auto tag_size = (static_cast<std::uintmax_t>(byteAt(header, 6)) << 21U) |
                          (static_cast<std::uintmax_t>(byteAt(header, 7)) << 14U) |
                          (static_cast<std::uintmax_t>(byteAt(header, 8)) << 7U) |
                          static_cast<std::uintmax_t>(byteAt(header, 9));
    const bool has_footer = (byteAt(header, 5) & 0x10U) != 0;
    if (has_footer && major_version != 4) {
        return false;
    }
    const std::uintmax_t fixed_size = 10U + (has_footer ? 10U : 0U);
    return file_size >= fixed_size && tag_size <= file_size - fixed_size;
}

[[nodiscard]] bool hasMpegLayer3FrameSync(const ContainerHeader& header) noexcept {
    const auto first = byteAt(header, 0);
    const auto second = byteAt(header, 1);
    const auto third = byteAt(header, 2);
    const auto version_bits = static_cast<std::uint8_t>((second >> 3U) & 0x03U);
    const auto layer_bits = static_cast<std::uint8_t>((second >> 1U) & 0x03U);
    const auto bitrate_index = static_cast<std::uint8_t>((third >> 4U) & 0x0fU);
    const auto sample_rate_index = static_cast<std::uint8_t>((third >> 2U) & 0x03U);

    return first == 0xffU && (second & 0xe0U) == 0xe0U && version_bits != 0x01U && layer_bits == 0x01U &&
           bitrate_index != 0x00U && bitrate_index != 0x0fU && sample_rate_index != 0x03U;
}

[[nodiscard]] std::optional<std::string> optionalUtf8(const TagLib::String& value) {
    if (value.isEmpty()) {
        return std::nullopt;
    }
    return value.to8Bit(true);
}

[[nodiscard]] std::string stemUtf8(const std::filesystem::path& source_path) {
    const auto value = source_path.stem().u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] std::string pcmCodecName(const int format, const int bits_per_sample) {
    if (format != 1) {
        return {};
    }
    if (bits_per_sample == 8) {
        return "PCM unsigned 8-bit";
    }
    return "PCM signed " + std::to_string(bits_per_sample) + "-bit little-endian";
}

[[nodiscard]] std::string mp3CodecName(const TagLib::MPEG::Header::Version version, const int layer) {
    if (layer != 3) {
        return {};
    }

    switch (version) {
    case TagLib::MPEG::Header::Version1:
        return "MPEG-1 Audio Layer III";
    case TagLib::MPEG::Header::Version2:
        return "MPEG-2 Audio Layer III";
    case TagLib::MPEG::Header::Version2_5:
        return "MPEG-2.5 Audio Layer III";
    case TagLib::MPEG::Header::Version4:
        return {};
    }
    return {};
}

[[nodiscard]] MetadataTags readTags(const TagLib::Tag* tag) {
    MetadataTags tags{};
    if (tag == nullptr) {
        return tags;
    }

    tags.title = optionalUtf8(tag->title());
    tags.artist = optionalUtf8(tag->artist());
    tags.album = optionalUtf8(tag->album());
    if (tag->track() != 0) {
        tags.track_number = tag->track();
    }
    if (tag->year() != 0) {
        tags.year = tag->year();
    }
    return tags;
}

[[nodiscard]] TrackMetadata baseMetadata(const std::filesystem::path& source_path, const std::uintmax_t file_size,
                                         MetadataTags tags) {
    TrackMetadata metadata{};
    metadata.source_path = source_path;
    metadata.file_size_bytes = file_size;
    metadata.display_title = tags.title.value_or(stemUtf8(source_path));
    metadata.tags = std::move(tags);
    return metadata;
}

[[nodiscard]] MetadataReadResult readWave(const std::filesystem::path& source_path, const std::uintmax_t file_size) {
    TagLib::RIFF::WAV::File file{source_path.c_str(), true, TagLib::AudioProperties::Average};
    const auto* properties = file.audioProperties();
    if (!file.isValid() || properties == nullptr || properties->lengthInMilliseconds() <= 0 ||
        properties->sampleRate() <= 0 || properties->channels() <= 0 || properties->bitsPerSample() <= 0) {
        return error(MetadataReadErrorCode::InvalidData, "WAV metadata or audio properties are invalid");
    }

    const auto codec = pcmCodecName(properties->format(), properties->bitsPerSample());
    if (codec.empty()) {
        return error(MetadataReadErrorCode::UnsupportedFormat, "WAV encoding is not supported by this slice");
    }

    auto metadata = baseMetadata(source_path, file_size, readTags(file.tag()));
    metadata.audio.duration_ms = properties->lengthInMilliseconds();
    metadata.audio.sample_rate_hz = static_cast<std::uint32_t>(properties->sampleRate());
    metadata.audio.channels = static_cast<std::uint16_t>(properties->channels());
    metadata.audio.bit_depth = static_cast<std::uint16_t>(properties->bitsPerSample());
    if (properties->bitrate() > 0) {
        metadata.audio.bitrate_kbps = static_cast<std::uint32_t>(properties->bitrate());
    }
    metadata.audio.container = "WAV";
    metadata.audio.codec = codec;
    return metadata;
}

[[nodiscard]] MetadataReadResult readMp3(const std::filesystem::path& source_path, const std::uintmax_t file_size) {
    TagLib::MPEG::File file{source_path.c_str(), true, TagLib::AudioProperties::Average};
    const auto* properties = file.audioProperties();
    if (!file.isValid() || properties == nullptr || properties->lengthInMilliseconds() <= 0 ||
        properties->sampleRate() <= 0 || properties->channels() <= 0) {
        return error(MetadataReadErrorCode::InvalidData, "MP3 metadata or audio properties are invalid");
    }
    if (properties->isADTS()) {
        return error(MetadataReadErrorCode::UnsupportedFormat, "ADTS is not MP3");
    }

    const auto codec = mp3CodecName(properties->version(), properties->layer());
    if (codec.empty()) {
        return error(MetadataReadErrorCode::UnsupportedFormat, "MPEG encoding is not MP3 Layer III");
    }

    auto metadata = baseMetadata(source_path, file_size, readTags(file.tag()));
    metadata.audio.duration_ms = properties->lengthInMilliseconds();
    metadata.audio.sample_rate_hz = static_cast<std::uint32_t>(properties->sampleRate());
    metadata.audio.channels = static_cast<std::uint16_t>(properties->channels());
    if (properties->bitrate() > 0) {
        metadata.audio.bitrate_kbps = static_cast<std::uint32_t>(properties->bitrate());
    }
    metadata.audio.container = "MP3";
    metadata.audio.codec = codec;
    return metadata;
}

} // namespace

MetadataReadResult TagLibMetadataExtractor::read(const std::filesystem::path& source_path) const {
    try {
        std::error_code file_error;
        const auto status = std::filesystem::status(source_path, file_error);
        if (file_error || !std::filesystem::is_regular_file(status)) {
            return error(MetadataReadErrorCode::SourceUnavailable, "metadata source is not a readable regular file");
        }

        const auto file_size = std::filesystem::file_size(source_path, file_error);
        if (file_error) {
            return error(MetadataReadErrorCode::SourceUnavailable, "metadata source size could not be read");
        }
        if (file_size == 0) {
            return error(MetadataReadErrorCode::InvalidData, "metadata source is empty");
        }

        std::ifstream stream{source_path, std::ios::binary};
        if (!stream) {
            return error(MetadataReadErrorCode::SourceUnavailable, "metadata source could not be opened");
        }
        ContainerHeader header{};
        stream.read(header.data(), static_cast<std::streamsize>(header.size()));
        if (stream.gcount() != static_cast<std::streamsize>(header.size())) {
            return error(MetadataReadErrorCode::InvalidData, "metadata source has a truncated container header");
        }
        if (isRiffWave(header)) {
            return readWave(source_path, file_size);
        }
        if (hasId3v2Signature(header)) {
            if (!hasValidId3v2Header(header, file_size)) {
                return error(MetadataReadErrorCode::InvalidData, "metadata source has an invalid ID3v2 header");
            }
            return readMp3(source_path, file_size);
        }
        if (hasMpegLayer3FrameSync(header)) {
            return readMp3(source_path, file_size);
        }
        return error(MetadataReadErrorCode::UnsupportedFormat, "metadata source container is not supported");
    } catch (const std::exception&) {
        return error(MetadataReadErrorCode::ReadFailed, "metadata reader failed safely");
    } catch (...) {
        return error(MetadataReadErrorCode::ReadFailed, "metadata reader failed safely");
    }
}

} // namespace music_player::m0
