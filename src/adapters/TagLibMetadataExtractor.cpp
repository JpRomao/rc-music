#include "m0/adapters/TagLibMetadataExtractor.hpp"
#include "m0/domain/ArtworkLimits.hpp"
#include "m0/domain/MetadataSanitization.hpp"

#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QImageReader>
#include <QSize>

#include <apetag.h>
#include <audioproperties.h>
#include <flacfile.h>
#include <flacproperties.h>
#include <id3v1tag.h>
#include <id3v2frame.h>
#include <id3v2tag.h>
#include <infotag.h>
#include <mp4file.h>
#include <mp4properties.h>
#include <mp4tag.h>
#include <mpegfile.h>
#include <mpegheader.h>
#include <mpegproperties.h>
#include <tag.h>
#include <tfile.h>
#include <tpropertymap.h>
#include <tvariant.h>
#include <vorbisfile.h>
#include <vorbisproperties.h>
#include <wavfile.h>
#include <wavproperties.h>
#include <xiphcomment.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <initializer_list>
#include <memory>
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
constexpr std::array flac_marker{'f', 'L', 'a', 'C'};
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
    return std::equal(flac_marker.begin(), flac_marker.end(), header.begin());
}

[[nodiscard]] bool hasFlacMarkerAt(std::ifstream& stream, const ContainerHeader& header, const std::size_t bytes_read,
                                   const std::uintmax_t offset) {
    if (offset <= bytes_read && flac_marker.size() <= bytes_read - offset) {
        return std::equal(flac_marker.begin(), flac_marker.end(), header.data() + offset);
    }
    std::array<char, flac_marker.size()> marker{};
    stream.clear(); // The initial probe may have reached EOF.
    stream.seekg(static_cast<std::streamoff>(offset));
    return stream.read(marker.data(), static_cast<std::streamsize>(marker.size())) && marker == flac_marker;
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

[[nodiscard]] std::optional<std::uintmax_t> id3v2EndOffset(const ContainerHeader& header,
                                                           const std::uintmax_t file_size) noexcept {
    const auto major_version = byteAt(header, 3);
    const auto revision = byteAt(header, 4);
    if (major_version < 2 || major_version > 4 || revision == 0xffU) {
        return std::nullopt;
    }
    for (std::size_t index = 6; index < 10; ++index) {
        if ((byteAt(header, index) & 0x80U) != 0) {
            return std::nullopt;
        }
    }

    const auto tag_size = (static_cast<std::uintmax_t>(byteAt(header, 6)) << 21U) |
                          (static_cast<std::uintmax_t>(byteAt(header, 7)) << 14U) |
                          (static_cast<std::uintmax_t>(byteAt(header, 8)) << 7U) |
                          static_cast<std::uintmax_t>(byteAt(header, 9));
    const bool has_footer = (byteAt(header, 5) & 0x10U) != 0;
    if (has_footer && major_version != 4) {
        return std::nullopt;
    }
    const std::uintmax_t fixed_size = 10U + (has_footer ? 10U : 0U);
    if (file_size < fixed_size || tag_size > file_size - fixed_size)
        return std::nullopt;
    return fixed_size + tag_size;
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

[[nodiscard]] std::optional<TagLib::ByteVector> embeddedArtwork(const TagLib::File& file) {
    const auto pictures = file.complexProperties("PICTURE");
    if (pictures.isEmpty()) {
        return std::nullopt;
    }

    std::optional<TagLib::ByteVector> first_valid_picture;
    for (const auto& picture : pictures) {
        bool data_valid = false;
        auto data = picture.value("data").toByteVector(&data_valid);
        if (!data_valid) {
            continue;
        }
        if (!first_valid_picture.has_value()) {
            first_valid_picture = data;
        }
        bool type_valid = false;
        const auto type = picture.value("pictureType").toString(&type_valid);
        if (type_valid && type == "Front Cover") {
            return data;
        }
    }
    if (first_valid_picture.has_value()) {
        return first_valid_picture;
    }
    return TagLib::ByteVector{};
}

void recordArtworkDiagnostic(TrackMetadata& metadata, const ArtworkDiagnosticCode code, const std::size_t encoded_bytes,
                             const std::uint32_t width_pixels = 0, const std::uint32_t height_pixels = 0) {
    metadata.artwork_diagnostics.push_back(
        {.code = code, .encoded_bytes = encoded_bytes, .width_pixels = width_pixels, .height_pixels = height_pixels});
}

[[nodiscard]] ArtworkDiagnosticCode diagnosticCode(const ArtworkLimitViolation violation) noexcept {
    switch (violation) {
    case ArtworkLimitViolation::EncodedBytes:
        return ArtworkDiagnosticCode::EncodedBytesExceeded;
    case ArtworkLimitViolation::AxisPixels:
        return ArtworkDiagnosticCode::AxisPixelsExceeded;
    case ArtworkLimitViolation::PixelCount:
        return ArtworkDiagnosticCode::PixelCountExceeded;
    case ArtworkLimitViolation::InvalidDimensions:
    case ArtworkLimitViolation::None:
        return ArtworkDiagnosticCode::InvalidData;
    }
    return ArtworkDiagnosticCode::InvalidData;
}

[[nodiscard]] QSize artworkThumbnailSize(QSize source_size) {
    source_size.scale(static_cast<int>(ArtworkLimits::thumbnail_max_axis_pixels),
                      static_cast<int>(ArtworkLimits::thumbnail_max_axis_pixels), Qt::KeepAspectRatio);
    // Integer aspect-ratio scaling can round a valid thin image's short axis to zero.
    source_size.setWidth(std::max(1, source_size.width()));
    source_size.setHeight(std::max(1, source_size.height()));
    return source_size;
}

void applyArtwork(TrackMetadata& metadata, const std::optional<TagLib::ByteVector>& encoded_artwork) {
    if (!encoded_artwork.has_value()) {
        return;
    }

    const auto encoded_bytes = static_cast<std::size_t>(encoded_artwork->size());
    const auto encoded_violation = validateArtworkLimits(encoded_bytes, 1, 1);
    if (encoded_violation != ArtworkLimitViolation::None) {
        recordArtworkDiagnostic(metadata, diagnosticCode(encoded_violation), encoded_bytes);
        return;
    }

    const auto bytes = QByteArray::fromRawData(encoded_artwork->data(), static_cast<qsizetype>(encoded_bytes));
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) {
        recordArtworkDiagnostic(metadata, ArtworkDiagnosticCode::InvalidData, encoded_bytes);
        return;
    }

    QImageReader reader(&buffer);
    reader.setAutoDetectImageFormat(true);
    reader.setAutoTransform(true);
    if (!reader.canRead()) {
        recordArtworkDiagnostic(metadata, ArtworkDiagnosticCode::InvalidData, encoded_bytes);
        return;
    }
    const auto format = reader.format().toLower();
    if (format != QByteArrayLiteral("png") && format != QByteArrayLiteral("jpeg") &&
        format != QByteArrayLiteral("jpg")) {
        recordArtworkDiagnostic(metadata, ArtworkDiagnosticCode::InvalidData, encoded_bytes);
        return;
    }

    const auto source_size = reader.size();
    if (!source_size.isValid() || source_size.width() <= 0 || source_size.height() <= 0) {
        recordArtworkDiagnostic(metadata, ArtworkDiagnosticCode::InvalidData, encoded_bytes);
        return;
    }
    const auto width_pixels = static_cast<std::uint32_t>(source_size.width());
    const auto height_pixels = static_cast<std::uint32_t>(source_size.height());
    const auto dimensions_violation = validateArtworkLimits(encoded_bytes, width_pixels, height_pixels);
    if (dimensions_violation != ArtworkLimitViolation::None) {
        recordArtworkDiagnostic(metadata, diagnosticCode(dimensions_violation), encoded_bytes, width_pixels,
                                height_pixels);
        return;
    }

    if (source_size.width() > static_cast<int>(ArtworkLimits::thumbnail_max_axis_pixels) ||
        source_size.height() > static_cast<int>(ArtworkLimits::thumbnail_max_axis_pixels)) {
        reader.setScaledSize(artworkThumbnailSize(source_size));
    }

    auto image = reader.read();
    if (image.isNull()) {
        recordArtworkDiagnostic(metadata, ArtworkDiagnosticCode::InvalidData, encoded_bytes, width_pixels,
                                height_pixels);
        return;
    }
    if (image.width() > static_cast<int>(ArtworkLimits::thumbnail_max_axis_pixels) ||
        image.height() > static_cast<int>(ArtworkLimits::thumbnail_max_axis_pixels)) {
        image = image.scaled(artworkThumbnailSize(image.size()), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    image = image.convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull()) {
        recordArtworkDiagnostic(metadata, ArtworkDiagnosticCode::InvalidData, encoded_bytes, width_pixels,
                                height_pixels);
        return;
    }

    ArtworkThumbnail thumbnail{};
    thumbnail.width_pixels = static_cast<std::uint32_t>(image.width());
    thumbnail.height_pixels = static_cast<std::uint32_t>(image.height());
    const auto row_bytes = static_cast<std::size_t>(image.width()) * 4U;
    auto pixels = std::make_shared<std::vector<std::uint8_t>>(row_bytes * static_cast<std::size_t>(image.height()));
    for (int row = 0; row < image.height(); ++row) {
        std::memcpy(pixels->data() + static_cast<std::size_t>(row) * row_bytes, image.constScanLine(row), row_bytes);
    }
    thumbnail.rgba8888 = std::move(pixels);
    metadata.artwork = std::move(thumbnail);
}

void recordValueDiagnostic(std::vector<MetadataDiagnostic>& diagnostics, const MetadataDiagnosticCode code,
                           const MetadataField field) {
    if (std::none_of(diagnostics.begin(), diagnostics.end(), [=](const MetadataDiagnostic& diagnostic) {
            return diagnostic.code == code && diagnostic.field == field;
        })) {
        diagnostics.push_back({.code = code, .field = field});
    }
}

[[nodiscard]] bool originalFieldFits(const std::string_view value, const MetadataField field,
                                     std::vector<MetadataDiagnostic>& diagnostics) {
    if (value.size() <= MetadataRetentionBudget::field_limit_bytes) {
        return true;
    }
    if (std::none_of(diagnostics.begin(), diagnostics.end(), [=](const MetadataDiagnostic& diagnostic) {
            return diagnostic.code == MetadataDiagnosticCode::FieldTooLarge && diagnostic.field == field;
        })) {
        diagnostics.push_back({.code = MetadataDiagnosticCode::FieldTooLarge,
                               .field = field,
                               .observed_bytes = value.size(),
                               .limit_bytes = MetadataRetentionBudget::field_limit_bytes});
    }
    return false;
}

[[nodiscard]] std::optional<std::string> retainedUtf8(const TagLib::String& value, const MetadataField field,
                                                      MetadataRetentionBudget& budget,
                                                      std::vector<MetadataDiagnostic>& diagnostics) {
    if (value.isEmpty()) {
        return std::nullopt;
    }
    const auto original = value.to8Bit(true);
    if (!originalFieldFits(original, field, diagnostics)) {
        return std::nullopt;
    }
    auto sanitized = sanitizeMetadataText(original);
    if (sanitized.disposition == MetadataTextDisposition::Empty ||
        sanitized.disposition == MetadataTextDisposition::InvalidEncoding) {
        if (!original.empty()) {
            recordValueDiagnostic(diagnostics, MetadataDiagnosticCode::InvalidValue, field);
        }
        return std::nullopt;
    }
    if (auto diagnostic = budget.tryRetain(field, sanitized.text.size()); diagnostic.has_value()) {
        diagnostics.push_back(*diagnostic);
        return std::nullopt;
    }
    if (sanitized.disposition == MetadataTextDisposition::Sanitized) {
        recordValueDiagnostic(diagnostics, MetadataDiagnosticCode::TextSanitized, field);
    }
    return std::move(sanitized.text);
}

[[nodiscard]] TagLib::PropertyMap numericProperties(const TagLib::Tag& tag) {
    if (const auto* id3 = dynamic_cast<const TagLib::ID3v2::Tag*>(&tag); id3 != nullptr) {
        TagLib::PropertyMap properties;
        // TextIdentificationFrame::asProperties() replaces the ISO 'T' in DATE
        // with a space. Read the public frames to validate the original grammar.
        for (const auto& [frame_id, key] : {std::pair{"TRCK", "TRACKNUMBER"}, std::pair{"TDRC", "DATE"}}) {
            const auto entry = id3->frameListMap().find(frame_id);
            if (entry == id3->frameListMap().end()) {
                continue;
            }
            for (const auto* frame : entry->second) {
                properties.insert(key, frame->toStringList());
            }
        }
        return properties;
    }
    if (const auto* legacy = dynamic_cast<const TagLib::ID3v1::Tag*>(&tag); legacy != nullptr) {
        TagLib::PropertyMap properties;
        // ID3v1::properties() has already converted the year through signed toInt().
        // render() is an in-memory public API snapshot, not another file read.
        const auto rendered = legacy->render();
        if (rendered.size() == 128 && rendered.mid(0, 3) == "TAG") {
            auto year = std::string{rendered.data() + 93, 4};
            while (!year.empty() && year.back() == '\0') {
                year.pop_back();
            }
            if (!year.empty()) {
                properties.insert("DATE", TagLib::StringList{TagLib::String{year, TagLib::String::Latin1}});
            }
        }
        if (legacy->track() != 0) {
            properties.insert("TRACKNUMBER", TagLib::StringList{TagLib::String::number(legacy->track())});
        }
        return properties;
    }

    auto properties = tag.properties();
    if (const auto* mp4 = dynamic_cast<const TagLib::MP4::Tag*>(&tag); mp4 != nullptr && mp4->contains("trkn")) {
        // The MP4 trkn atom stores unsigned 16-bit number/total. TagLib 2.3.1
        // parseIntPair reads both using ByteVector::toShort(), projecting the top
        // half to negative int values. Recover only this known binary format.
        const auto pair = mp4->item("trkn").toIntPair();
        const auto number = static_cast<std::uint16_t>(pair.first);
        const auto total = static_cast<std::uint16_t>(pair.second);
        auto track = std::to_string(number);
        if (total != 0) {
            track += "/" + std::to_string(total);
        }
        properties.replace("TRACKNUMBER", TagLib::StringList{TagLib::String{track, TagLib::String::Latin1}});
    }
    return properties;
}

void readNumber(const TagLib::PropertyMap& properties, const std::initializer_list<const char*> keys,
                const MetadataField field, std::optional<std::uint32_t>& destination, MetadataRetentionBudget& budget,
                std::vector<MetadataDiagnostic>& diagnostics) {
    if (destination.has_value()) {
        return;
    }
    for (const auto* key : keys) {
        const auto entry = properties.find(key);
        if (entry == properties.end()) {
            continue;
        }
        for (const auto& value : entry->second) {
            const auto original = value.to8Bit(true);
            if (original.empty() || !originalFieldFits(original, field, diagnostics)) {
                continue;
            }
            const auto parsed =
                field == MetadataField::TrackNumber ? parseMetadataTrackNumber(original) : parseMetadataYear(original);
            if (!parsed.has_value()) {
                recordValueDiagnostic(diagnostics, MetadataDiagnosticCode::InvalidValue, field);
                continue;
            }
            if (auto diagnostic = budget.tryRetain(field, sizeof(std::uint32_t)); diagnostic.has_value()) {
                diagnostics.push_back(*diagnostic);
                return;
            }
            destination = parsed;
            return;
        }
    }
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

[[nodiscard]] ReadTagsResult readTags(const TagLib::Tag* tag,
                                      const std::initializer_list<const TagLib::Tag*> numeric_tags) {
    ReadTagsResult result{};
    if (tag == nullptr) {
        return result;
    }

    MetadataRetentionBudget budget;
    result.tags.title = retainedUtf8(tag->title(), MetadataField::Title, budget, result.diagnostics);
    result.tags.artist = retainedUtf8(tag->artist(), MetadataField::Artist, budget, result.diagnostics);
    result.tags.album = retainedUtf8(tag->album(), MetadataField::Album, budget, result.diagnostics);
    // TagUnion::properties() picks one whole tag and loses per-field fallback.
    // Concrete file readers supply their public subtags in native priority order.
    for (const auto* numeric_tag : numeric_tags) {
        if (numeric_tag == nullptr) {
            continue;
        }
        const auto properties = numericProperties(*numeric_tag);
        const bool has_vorbis_aliases = dynamic_cast<const TagLib::Ogg::XiphComment*>(numeric_tag) != nullptr;
        readNumber(properties,
                   has_vorbis_aliases ? std::initializer_list<const char*>{"TRACKNUMBER", "TRACKNUM"}
                                      : std::initializer_list<const char*>{"TRACKNUMBER"},
                   MetadataField::TrackNumber, result.tags.track_number, budget, result.diagnostics);
        readNumber(properties,
                   has_vorbis_aliases ? std::initializer_list<const char*>{"DATE", "YEAR"}
                                      : std::initializer_list<const char*>{"DATE"},
                   MetadataField::Year, result.tags.year, budget, result.diagnostics);
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

    auto metadata = baseMetadata(source_path, file_size, readTags(file.tag(), {file.ID3v2Tag(), file.InfoTag()}));
    applyArtwork(metadata, embeddedArtwork(file));
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

    auto metadata = baseMetadata(
        source_path, file_size, readTags(file.tag(), {file.ID3v2Tag(false), file.APETag(false), file.ID3v1Tag(false)}));
    applyArtwork(metadata, embeddedArtwork(file));
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

    auto metadata =
        baseMetadata(source_path, file_size,
                     readTags(file.tag(), {file.xiphComment(false), file.ID3v2Tag(false), file.ID3v1Tag(false)}));
    applyArtwork(metadata, embeddedArtwork(file));
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

    auto metadata = baseMetadata(source_path, file_size, readTags(file.tag(), {file.tag()}));
    applyArtwork(metadata, embeddedArtwork(file));
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

    auto metadata = baseMetadata(source_path, file_size, readTags(file.tag(), {file.tag()}));
    applyArtwork(metadata, embeddedArtwork(file));
    applyCommonAudioProperties(metadata, *properties);
    metadata.audio.container = "Ogg";
    metadata.audio.codec = "Vorbis";
    return metadata;
}

} // namespace

TagLibMetadataExtractor::TagLibMetadataExtractor(TagLibMetadataExtractorOptions options)
    : options_{std::move(options)} {}

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

        std::optional<std::filesystem::file_time_type> last_modified_time;
        try {
            std::error_code modification_time_error;
            const auto value = options_.last_write_time
                                   ? options_.last_write_time(source_path, modification_time_error)
                                   : std::filesystem::last_write_time(source_path, modification_time_error);
            if (!modification_time_error) {
                last_modified_time = value;
            }
        } catch (...) {
            last_modified_time.reset();
        }

        const auto completed = [&](MetadataReadResult result) {
            if (auto* metadata = std::get_if<TrackMetadata>(&result); metadata != nullptr) {
                metadata->last_modified_time = last_modified_time;
            }
            return result;
        };

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
            return completed(readWave(source_path, file_size));
        }
        if (isFlac(header)) {
            return completed(readFlac(source_path, file_size));
        }
        if (isIsoBaseMedia(header, file_size, bytes_read)) {
            return completed(readM4aAac(source_path, file_size));
        }
        switch (oggCodec(header, bytes_read)) {
        case OggCodec::Vorbis:
            return completed(readVorbis(source_path, file_size));
        case OggCodec::Opus:
            return error(MetadataReadErrorCode::UnsupportedFormat, "Ogg Opus remains a candidate format");
        case OggCodec::Unknown:
            break;
        }
        if (hasId3v2Signature(header)) {
            const auto tag_end = id3v2EndOffset(header, file_size);
            if (!tag_end) {
                return error(MetadataReadErrorCode::InvalidData, "metadata source has an invalid ID3v2 header");
            }
            // A leading ID3 tag does not identify the codec. Probe only at its
            // validated end, so a marker in the tag cannot masquerade as FLAC.
            if (file_size - *tag_end >= flac_marker.size() && hasFlacMarkerAt(stream, header, bytes_read, *tag_end)) {
                return completed(readFlac(source_path, file_size));
            }
            return completed(readMp3(source_path, file_size));
        }
        if (hasMpegLayer3FrameSync(header)) {
            return completed(readMp3(source_path, file_size));
        }
        return error(MetadataReadErrorCode::UnsupportedFormat, "metadata source container is not supported");
    } catch (const std::exception&) {
        return error(MetadataReadErrorCode::ReadFailed, "metadata reader failed safely");
    } catch (...) {
        return error(MetadataReadErrorCode::ReadFailed, "metadata reader failed safely");
    }
}

} // namespace music_player::m0
