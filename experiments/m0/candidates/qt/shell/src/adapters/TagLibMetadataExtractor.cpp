#include "m0/adapters/TagLibMetadataExtractor.hpp"

#include <tag.h>
#include <wavfile.h>
#include <wavproperties.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

namespace music_player::m0 {
namespace {

constexpr std::size_t riff_wave_header_size = 12;

[[nodiscard]] MetadataReadError error(const MetadataReadErrorCode code, std::string message) {
    return {.code = code, .message = std::move(message)};
}

[[nodiscard]] bool isRiffWave(const std::array<char, riff_wave_header_size>& header) noexcept {
    return header[0] == 'R' && header[1] == 'I' && header[2] == 'F' && header[3] == 'F' && header[8] == 'W' &&
           header[9] == 'A' && header[10] == 'V' && header[11] == 'E';
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
        std::array<char, riff_wave_header_size> header{};
        stream.read(header.data(), static_cast<std::streamsize>(header.size()));
        if (stream.gcount() != static_cast<std::streamsize>(header.size())) {
            return error(MetadataReadErrorCode::InvalidData, "metadata source has a truncated container header");
        }
        if (!isRiffWave(header)) {
            return error(MetadataReadErrorCode::UnsupportedFormat, "metadata source container is not supported");
        }

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

        MetadataTags tags{};
        if (const auto* tag = file.tag(); tag != nullptr) {
            tags.title = optionalUtf8(tag->title());
            tags.artist = optionalUtf8(tag->artist());
            tags.album = optionalUtf8(tag->album());
            if (tag->track() != 0) {
                tags.track_number = tag->track();
            }
            if (tag->year() != 0) {
                tags.year = tag->year();
            }
        }

        TrackMetadata metadata{};
        metadata.source_path = source_path;
        metadata.file_size_bytes = file_size;
        metadata.display_title = tags.title.value_or(stemUtf8(source_path));
        metadata.tags = std::move(tags);
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
    } catch (const std::exception&) {
        return error(MetadataReadErrorCode::ReadFailed, "metadata reader failed safely");
    } catch (...) {
        return error(MetadataReadErrorCode::ReadFailed, "metadata reader failed safely");
    }
}

} // namespace music_player::m0
