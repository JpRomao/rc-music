#include "m0/adapters/TagLibMetadataExtractor.hpp"

#include <audioproperties.h>
#include <flacfile.h>
#include <flacproperties.h>
#include <mp4file.h>
#include <mp4properties.h>
#include <mpegfile.h>
#include <mpegheader.h>
#include <mpegproperties.h>
#include <tag.h>
#include <vorbisfile.h>
#include <vorbisproperties.h>
#include <wavfile.h>
#include <wavproperties.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace music_player::m0 {
namespace {

constexpr std::size_t minimum_container_header_size = 12;
constexpr std::size_t container_header_size = 512;
constexpr std::size_t retained_metadata_field_limit_bytes = 16U * 1024U;
using ContainerHeader = std::array<char, container_header_size>;

enum class OggCodec { Unknown, Vorbis, Opus };

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

[[nodiscard]] bool isFlac(const ContainerHeader& header) noexcept {
    return header[0] == 'f' && header[1] == 'L' && header[2] == 'a' && header[3] == 'C';
}

[[nodiscard]] std::uint32_t bigEndian32(const ContainerHeader& header) noexcept {
    return (static_cast<std::uint32_t>(byteAt(header, 0)) << 24U) |
           (static_cast<std::uint32_t>(byteAt(header, 1)) << 16U) |
           (static_cast<std::uint32_t>(byteAt(header, 2)) << 8U) | static_cast<std::uint32_t>(byteAt(header, 3));
}

[[nodiscard]] std::uint64_t bigEndian64(const ContainerHeader& header, const std::size_t offset) noexcept {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value = (value << 8U) | static_cast<std::uint64_t>(byteAt(header, offset + index));
    }
    return value;
}

[[nodiscard]] bool isIsoBaseMedia(const ContainerHeader& header, const std::uintmax_t file_size,
                                  const std::size_t bytes_read) noexcept {
    constexpr std::uint32_t extended_size_marker = 1;
    constexpr std::size_t minimum_box_size = 16;
    constexpr std::size_t minimum_extended_box_size = 24;
    if (header[4] != 'f' || header[5] != 't' || header[6] != 'y' || header[7] != 'p') {
        return false;
    }

    const auto atom_size = bigEndian32(header);
    if (atom_size == extended_size_marker) {
        if (bytes_read < minimum_extended_box_size) {
            return false;
        }
        const auto extended_size = bigEndian64(header, 8);
        return extended_size >= minimum_extended_box_size && extended_size <= file_size;
    }
    if (atom_size == 0) {
        return file_size >= minimum_box_size;
    }
    return atom_size >= minimum_box_size && atom_size <= file_size;
}

[[nodiscard]] bool matches(const ContainerHeader& header, const std::size_t offset, const std::string_view value,
                           const std::size_t bytes_read) noexcept {
    if (offset > bytes_read || value.size() > bytes_read - offset) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (header[offset + index] != value[index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] OggCodec oggCodec(const ContainerHeader& header, const std::size_t bytes_read) noexcept {
    constexpr std::size_t fixed_page_header_size = 27;
    if (bytes_read < fixed_page_header_size || !matches(header, 0, "OggS", bytes_read) || byteAt(header, 4) != 0) {
        return OggCodec::Unknown;
    }

    const auto segment_count = static_cast<std::size_t>(byteAt(header, 26));
    const auto first_packet_offset = fixed_page_header_size + segment_count;
    if (matches(header, first_packet_offset, std::string_view{"\x01vorbis", 7}, bytes_read)) {
        return OggCodec::Vorbis;
    }
    if (matches(header, first_packet_offset, "OpusHead", bytes_read)) {
        return OggCodec::Opus;
    }
    return OggCodec::Unknown;
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

struct ReadTagsResult final {
    MetadataTags tags{};
    std::vector<MetadataDiagnostic> diagnostics{};
};

[[nodiscard]] std::optional<std::string> retainedUtf8(const TagLib::String& value, const MetadataField field,
                                                      std::vector<MetadataDiagnostic>& diagnostics) {
    if (value.isEmpty()) {
        return std::nullopt;
    }
    auto text = value.to8Bit(true);
    if (text.size() > retained_metadata_field_limit_bytes) {
        diagnostics.push_back({.code = MetadataDiagnosticCode::FieldTooLarge,
                               .field = field,
                               .observed_bytes = text.size(),
                               .limit_bytes = retained_metadata_field_limit_bytes});
        return std::nullopt;
    }
    return text;
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

[[nodiscard]] ReadTagsResult readTags(const TagLib::Tag* tag) {
    ReadTagsResult result{};
    if (tag == nullptr) {
        return result;
    }

    result.tags.title = retainedUtf8(tag->title(), MetadataField::Title, result.diagnostics);
    result.tags.artist = retainedUtf8(tag->artist(), MetadataField::Artist, result.diagnostics);
    result.tags.album = retainedUtf8(tag->album(), MetadataField::Album, result.diagnostics);
    if (tag->track() != 0) {
        result.tags.track_number = tag->track();
    }
    if (tag->year() != 0) {
        result.tags.year = tag->year();
    }
    return result;
}

[[nodiscard]] TrackMetadata baseMetadata(const std::filesystem::path& source_path, const std::uintmax_t file_size,
                                         ReadTagsResult read_result) {
    TrackMetadata metadata{};
    metadata.source_path = source_path;
    metadata.file_size_bytes = file_size;
    metadata.display_title = read_result.tags.title.value_or(stemUtf8(source_path));
    metadata.tags = std::move(read_result.tags);
    metadata.diagnostics = std::move(read_result.diagnostics);
    return metadata;
}

[[nodiscard]] bool hasValidAudioProperties(const TagLib::AudioProperties* properties) noexcept {
    return properties != nullptr && properties->lengthInMilliseconds() > 0 && properties->sampleRate() > 0 &&
           properties->channels() > 0;
}

void applyCommonAudioProperties(TrackMetadata& metadata, const TagLib::AudioProperties& properties) {
    metadata.audio.duration_ms = properties.lengthInMilliseconds();
    metadata.audio.sample_rate_hz = static_cast<std::uint32_t>(properties.sampleRate());
    metadata.audio.channels = static_cast<std::uint16_t>(properties.channels());
    if (properties.bitrate() > 0) {
        metadata.audio.bitrate_kbps = static_cast<std::uint32_t>(properties.bitrate());
    }
}

[[nodiscard]] MetadataReadResult readWave(const std::filesystem::path& source_path, const std::uintmax_t file_size) {
    TagLib::RIFF::WAV::File file{source_path.c_str(), true, TagLib::AudioProperties::Average};
    const auto* properties = file.audioProperties();
    if (!file.isValid() || !hasValidAudioProperties(properties) || properties->bitsPerSample() <= 0) {
        return error(MetadataReadErrorCode::InvalidData, "WAV metadata or audio properties are invalid");
    }

    const auto codec = pcmCodecName(properties->format(), properties->bitsPerSample());
    if (codec.empty()) {
        return error(MetadataReadErrorCode::UnsupportedFormat, "WAV encoding is not supported by this slice");
    }

    auto metadata = baseMetadata(source_path, file_size, readTags(file.tag()));
    applyCommonAudioProperties(metadata, *properties);
    metadata.audio.bit_depth = static_cast<std::uint16_t>(properties->bitsPerSample());
    metadata.audio.container = "WAV";
    metadata.audio.codec = codec;
    return metadata;
}

[[nodiscard]] MetadataReadResult readMp3(const std::filesystem::path& source_path, const std::uintmax_t file_size) {
    TagLib::MPEG::File file{source_path.c_str(), true, TagLib::AudioProperties::Average};
    const auto* properties = file.audioProperties();
    if (!file.isValid() || !hasValidAudioProperties(properties)) {
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
    applyCommonAudioProperties(metadata, *properties);
    metadata.audio.container = "MP3";
    metadata.audio.codec = codec;
    return metadata;
}

[[nodiscard]] MetadataReadResult readFlac(const std::filesystem::path& source_path, const std::uintmax_t file_size) {
    TagLib::FLAC::File file{source_path.c_str(), true, TagLib::AudioProperties::Average};
    const auto* properties = file.audioProperties();
    if (!file.isValid() || !hasValidAudioProperties(properties) || properties->bitsPerSample() <= 0) {
        return error(MetadataReadErrorCode::InvalidData, "FLAC metadata or audio properties are invalid");
    }

    auto metadata = baseMetadata(source_path, file_size, readTags(file.tag()));
    applyCommonAudioProperties(metadata, *properties);
    metadata.audio.bit_depth = static_cast<std::uint16_t>(properties->bitsPerSample());
    metadata.audio.container = "FLAC";
    metadata.audio.codec = "FLAC";
    return metadata;
}

[[nodiscard]] MetadataReadResult readM4aAac(const std::filesystem::path& source_path, const std::uintmax_t file_size) {
    TagLib::MP4::File file{source_path.c_str(), true, TagLib::AudioProperties::Average};
    const auto* properties = file.audioProperties();
    if (!file.isValid() || !hasValidAudioProperties(properties)) {
        return error(MetadataReadErrorCode::InvalidData, "MP4 metadata or audio properties are invalid");
    }
    if (properties->isEncrypted()) {
        return error(MetadataReadErrorCode::UnsupportedFormat, "encrypted MP4 audio is not supported");
    }
    if (properties->codec() != TagLib::MP4::Properties::AAC) {
        return error(MetadataReadErrorCode::UnsupportedFormat, "MP4 audio codec is not AAC");
    }

    auto metadata = baseMetadata(source_path, file_size, readTags(file.tag()));
    applyCommonAudioProperties(metadata, *properties);
    metadata.audio.container = "ISO MP4/M4A";
    metadata.audio.codec = "AAC";
    return metadata;
}

[[nodiscard]] MetadataReadResult readVorbis(const std::filesystem::path& source_path, const std::uintmax_t file_size) {
    TagLib::Vorbis::File file{source_path.c_str(), true, TagLib::AudioProperties::Average};
    const auto* properties = file.audioProperties();
    if (!file.isValid() || !hasValidAudioProperties(properties)) {
        return error(MetadataReadErrorCode::InvalidData, "Ogg Vorbis metadata or audio properties are invalid");
    }

    auto metadata = baseMetadata(source_path, file_size, readTags(file.tag()));
    applyCommonAudioProperties(metadata, *properties);
    metadata.audio.container = "Ogg";
    metadata.audio.codec = "Vorbis";
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
        const auto bytes_read = static_cast<std::size_t>(stream.gcount());
        if (bytes_read < minimum_container_header_size) {
            return error(MetadataReadErrorCode::InvalidData, "metadata source has a truncated container header");
        }
        if (isRiffWave(header)) {
            return readWave(source_path, file_size);
        }
        if (isFlac(header)) {
            return readFlac(source_path, file_size);
        }
        if (isIsoBaseMedia(header, file_size, bytes_read)) {
            return readM4aAac(source_path, file_size);
        }
        switch (oggCodec(header, bytes_read)) {
        case OggCodec::Vorbis:
            return readVorbis(source_path, file_size);
        case OggCodec::Opus:
            return error(MetadataReadErrorCode::UnsupportedFormat, "Ogg Opus remains a candidate format");
        case OggCodec::Unknown:
            break;
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
