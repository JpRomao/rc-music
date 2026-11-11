#include "FlacApplicationBlocksFixture.hpp"
#include "Id3PaddedMp3Fixture.hpp"
#include "OggCommentPacketFixture.hpp"
#include "m0/adapters/SingleWorkerMetadataReader.hpp"
#include "m0/adapters/TagLibMetadataExtractor.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QBuffer>
#include <QByteArray>
#include <QImage>

#include <apetag.h>
#include <attachedpictureframe.h>
#include <flacfile.h>
#include <id3v1tag.h>
#include <id3v2tag.h>
#include <mp4file.h>
#include <mp4tag.h>
#include <mpegfile.h>
#include <tag.h>
#include <tpropertymap.h>
#include <tvariant.h>
#include <vorbisfile.h>
#include <wavfile.h>
#include <xiphcomment.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <variant>
#include <vector>

#ifndef M0_TEST_AUDIO_CORPUS_ROOT
#error "M0_TEST_AUDIO_CORPUS_ROOT must identify the authenticated M0 audio corpus"
#endif
#ifndef M0_TEST_AUDIO_PRODUCT_CORPUS_ROOT
#error "M0_TEST_AUDIO_PRODUCT_CORPUS_ROOT must identify the authenticated product corpus"
#endif

namespace music_player::m0::test {
namespace {

using namespace std::chrono_literals;

const std::filesystem::path corpus_root{M0_TEST_AUDIO_CORPUS_ROOT};

[[nodiscard]] std::filesystem::path fixture(const std::filesystem::path& relative_path) {
    return corpus_root / relative_path;
}

[[nodiscard]] std::filesystem::path productFixture(const std::filesystem::path& relative_path) {
    return std::filesystem::path{M0_TEST_AUDIO_PRODUCT_CORPUS_ROOT} / relative_path;
}

[[nodiscard]] const TrackMetadata& requireMetadata(const MetadataReadResult& result) {
    REQUIRE(std::holds_alternative<TrackMetadata>(result));
    return std::get<TrackMetadata>(result);
}

[[nodiscard]] const MetadataReadError& requireError(const MetadataReadResult& result) {
    REQUIRE(std::holds_alternative<MetadataReadError>(result));
    return std::get<MetadataReadError>(result);
}

class ScopedTemporaryFile final {
  public:
    explicit ScopedTemporaryFile(std::filesystem::path path) : path_{std::move(path)} {}

    ~ScopedTemporaryFile() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    ScopedTemporaryFile(const ScopedTemporaryFile&) = delete;
    ScopedTemporaryFile& operator=(const ScopedTemporaryFile&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  private:
    std::filesystem::path path_;
};

[[nodiscard]] std::filesystem::path createExclusiveTemporaryDirectory() {
    static std::atomic_uint64_t sequence{0};
    const auto parent = std::filesystem::temp_directory_path();
    for (std::uint32_t attempt = 0; attempt < 100; ++attempt) {
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto suffix = sequence.fetch_add(1, std::memory_order_relaxed);
        const auto candidate =
            parent / ("music-player-m0-metadata-" + std::to_string(timestamp) + "-" + std::to_string(suffix));
        std::error_code create_error;
        if (std::filesystem::create_directory(candidate, create_error)) {
            return candidate;
        }
        if (create_error && create_error != std::errc::file_exists) {
            throw std::filesystem::filesystem_error{"could not create metadata test directory", candidate,
                                                    create_error};
        }
    }
    throw std::runtime_error{"could not allocate an exclusive metadata test directory"};
}

class ScopedTemporaryDirectory final {
  public:
    ScopedTemporaryDirectory() : path_{createExclusiveTemporaryDirectory()} {}

    ~ScopedTemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    ScopedTemporaryDirectory(const ScopedTemporaryDirectory&) = delete;
    ScopedTemporaryDirectory& operator=(const ScopedTemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  private:
    std::filesystem::path path_;
};

[[nodiscard]] std::filesystem::path temporaryPath(const ScopedTemporaryDirectory& directory,
                                                  const std::filesystem::path& filename) {
    return directory.path() / filename;
}

[[nodiscard]] std::vector<std::uint8_t> readBytes(const std::filesystem::path& source) {
    std::ifstream stream{source, std::ios::binary};
    REQUIRE(stream);
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

void writeBytes(const std::filesystem::path& destination, const std::vector<std::uint8_t>& bytes) {
    std::ofstream stream{destination, std::ios::binary | std::ios::trunc};
    REQUIRE(stream);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream);
}

struct OggCommentPacketLayout final {
    std::size_t size{};
    std::size_t pages{};
    std::uint8_t terminating_lace{};
};

[[nodiscard]] OggCommentPacketLayout readOggCommentPacketLayout(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    OggCommentPacketLayout comment;
    std::size_t packet_index = 0;
    while (true) {
        std::array<std::uint8_t, 27> page{};
        REQUIRE(stream.read(reinterpret_cast<char*>(page.data()), static_cast<std::streamsize>(page.size())));
        REQUIRE(std::equal(page.begin(), page.begin() + 4, "OggS"));
        const auto segment_count = page.back();
        REQUIRE(segment_count > 0);
        std::array<std::uint8_t, 255> segments{};
        REQUIRE(stream.read(reinterpret_cast<char*>(segments.data()), segment_count));

        bool comment_on_page = false;
        std::streamoff page_payload = 0;
        for (std::size_t index = 0; index < segment_count; ++index) {
            const auto lace = segments[index];
            page_payload += lace;
            if (packet_index == 1) {
                if (!comment_on_page) {
                    ++comment.pages;
                    comment_on_page = true;
                }
                comment.size += lace;
                if (lace < 255) {
                    comment.terminating_lace = lace;
                    return comment;
                }
            }
            if (lace < 255) {
                ++packet_index;
            }
        }
        REQUIRE(stream.seekg(page_payload, std::ios::cur));
    }
}

[[nodiscard]] std::string utf8TextFieldWithSize(const std::size_t size_bytes) {
    constexpr std::string_view multibyte_character{"\xC3\xA9"};
    std::string value;
    value.reserve(size_bytes);
    while (value.size() + multibyte_character.size() <= size_bytes) {
        value.append(multibyte_character);
    }
    value.append(size_bytes - value.size(), 'a');
    REQUIRE(value.size() == size_bytes);
    return value;
}

[[nodiscard]] bool hasSameText(const std::string& left, const std::string& right) { return left == right; }

void writeMp3Title(const std::filesystem::path& destination, const std::string& title) {
    TagLib::MPEG::File file{destination.c_str()};
    REQUIRE(file.isValid());
    auto* tag = file.ID3v2Tag(true);
    REQUIRE(tag != nullptr);
    tag->setTitle(TagLib::String{title, TagLib::String::UTF8});
    REQUIRE(file.save(TagLib::MPEG::File::ID3v2, TagLib::MPEG::File::StripOthers, TagLib::ID3v2::v4,
                      TagLib::MPEG::File::DoNotDuplicate));
}

[[nodiscard]] TagLib::PropertyMap numericProperties(const std::string& track, const std::string& year) {
    TagLib::PropertyMap values;
    values.insert("TRACKNUMBER", TagLib::StringList{TagLib::String{track, TagLib::String::UTF8}});
    values.insert("DATE", TagLib::StringList{TagLib::String{year, TagLib::String::UTF8}});
    return values;
}

template <typename TagLibFile>
void writeProperties(const std::filesystem::path& destination, const TagLib::PropertyMap& values) {
    TagLibFile file{destination.c_str()};
    REQUIRE(file.isValid());
    REQUIRE(file.setProperties(values).isEmpty());
    REQUIRE(file.save());
}

void writeMp3Properties(const std::filesystem::path& destination, const TagLib::PropertyMap& values) {
    TagLib::MPEG::File file{destination.c_str()};
    REQUIRE(file.isValid());
    REQUIRE(file.ID3v2Tag(true)->setProperties(values).isEmpty());
    REQUIRE(file.save(TagLib::MPEG::File::ID3v2, TagLib::MPEG::File::StripOthers, TagLib::ID3v2::v4,
                      TagLib::MPEG::File::DoNotDuplicate));
}

[[nodiscard]] std::size_t diagnosticCount(const TrackMetadata& metadata, const MetadataDiagnosticCode code,
                                          const MetadataField field) {
    return static_cast<std::size_t>(std::count_if(
        metadata.diagnostics.begin(), metadata.diagnostics.end(),
        [=](const MetadataDiagnostic& diagnostic) { return diagnostic.code == code && diagnostic.field == field; }));
}

[[nodiscard]] std::vector<std::uint8_t> onePixelPng() {
    const auto bytes = QByteArray::fromBase64(QByteArrayLiteral(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII="));
    REQUIRE_FALSE(bytes.isEmpty());
    return {reinterpret_cast<const std::uint8_t*>(bytes.constData()),
            reinterpret_cast<const std::uint8_t*>(bytes.constData() + bytes.size())};
}

[[nodiscard]] std::vector<std::uint8_t> encodedImage(const std::uint32_t width_pixels,
                                                     const std::uint32_t height_pixels, const char* format) {
    QImage image{static_cast<int>(width_pixels), static_cast<int>(height_pixels), QImage::Format_Grayscale8};
    REQUIRE_FALSE(image.isNull());

    image.fill(Qt::black);
    QByteArray encoded;
    QBuffer destination{&encoded};
    REQUIRE(destination.open(QIODevice::WriteOnly));
    REQUIRE(image.save(&destination, format));
    REQUIRE_FALSE(encoded.isEmpty());
    return {reinterpret_cast<const std::uint8_t*>(encoded.constData()),
            reinterpret_cast<const std::uint8_t*>(encoded.constData() + encoded.size())};
}

void writeMp3Artwork(const std::filesystem::path& destination, const std::vector<std::uint8_t>& artwork,
                     const std::string& mime_type = "image/png") {
    TagLib::MPEG::File file{destination.c_str()};
    REQUIRE(file.isValid());
    auto* tag = file.ID3v2Tag(true);
    REQUIRE(tag != nullptr);
    tag->removeFrames("APIC");
    auto* frame = new TagLib::ID3v2::AttachedPictureFrame;
    frame->setType(TagLib::ID3v2::AttachedPictureFrame::FrontCover);
    frame->setMimeType(TagLib::String{mime_type, TagLib::String::Latin1});
    frame->setPicture(
        TagLib::ByteVector{reinterpret_cast<const char*>(artwork.data()), static_cast<unsigned int>(artwork.size())});
    tag->addFrame(frame);
    REQUIRE(file.save(TagLib::MPEG::File::ID3v2, TagLib::MPEG::File::StripOthers, TagLib::ID3v2::v4,
                      TagLib::MPEG::File::DoNotDuplicate));
}

[[nodiscard]] std::vector<std::uint8_t> withoutId3Tags(std::vector<std::uint8_t> bytes) {
    REQUIRE(bytes.size() >= 10);
    REQUIRE(bytes[0] == 'I');
    REQUIRE(bytes[1] == 'D');
    REQUIRE(bytes[2] == '3');
    for (std::size_t index = 6; index < 10; ++index) {
        REQUIRE((bytes[index] & 0x80U) == 0);
    }

    const auto tag_size = (static_cast<std::size_t>(bytes[6]) << 21U) | (static_cast<std::size_t>(bytes[7]) << 14U) |
                          (static_cast<std::size_t>(bytes[8]) << 7U) | static_cast<std::size_t>(bytes[9]);
    auto audio_offset = 10U + tag_size;
    if ((bytes[5] & 0x10U) != 0) {
        audio_offset += 10U;
    }
    REQUIRE(audio_offset < bytes.size());
    bytes.erase(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(audio_offset));

    if (bytes.size() >= 128 && bytes[bytes.size() - 128] == 'T' && bytes[bytes.size() - 127] == 'A' &&
        bytes[bytes.size() - 126] == 'G') {
        bytes.resize(bytes.size() - 128);
    }
    return bytes;
}

[[nodiscard]] std::optional<MetadataReadCompletion> waitForCompletion(MetadataReader& reader,
                                                                      const std::chrono::milliseconds timeout = 2s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto completion = reader.tryTakeCompletion(); completion.has_value()) {
            return completion;
        }
        std::this_thread::sleep_for(1ms);
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("Unknown audio properties are absent instead of false zero values", "[m0][qt][metadata][taglib][FR-022]") {
    const AudioStreamProperties properties;

    CHECK_FALSE(properties.duration_ms.has_value());
    CHECK_FALSE(properties.sample_rate_hz.has_value());
    CHECK_FALSE(properties.channels.has_value());
    CHECK_FALSE(properties.bit_depth.has_value());
    CHECK_FALSE(properties.bitrate_kbps.has_value());
}

TEST_CASE("FR-022 reports the source modification time without inventing a timestamp",
          "[m0][qt][metadata][taglib][filesystem][FR-022]") {
    const ScopedTemporaryDirectory directory;
    const ScopedTemporaryFile source{temporaryPath(directory, "modified-at.wav")};
    writeBytes(source.path(), readBytes(fixture("fixtures/valid/wav-pcm-mono.wav")));

    const auto requested_time = std::filesystem::file_time_type::clock::now() - 24h;
    std::error_code write_error;
    std::filesystem::last_write_time(source.path(), requested_time, write_error);
    REQUIRE_FALSE(write_error);
    std::error_code read_error;
    const auto expected_time = std::filesystem::last_write_time(source.path(), read_error);
    REQUIRE_FALSE(read_error);

    const auto result = TagLibMetadataExtractor{}.read(source.path());
    const auto& metadata = requireMetadata(result);

    REQUIRE(metadata.last_modified_time.has_value());
    CHECK(*metadata.last_modified_time == expected_time);
}

TEST_CASE("FR-022 keeps readable metadata when the optional modification time is unavailable",
          "[m0][qt][metadata][taglib][filesystem][FR-022]") {
    TagLibMetadataExtractorOptions options;
    SECTION("filesystem reports an error code") {
        options.last_write_time = [](const std::filesystem::path&, std::error_code& error) {
            error = std::make_error_code(std::errc::permission_denied);
            return std::filesystem::file_time_type{};
        };
    }
    SECTION("filesystem accessor throws") {
        options.last_write_time = [](const std::filesystem::path&,
                                     std::error_code&) -> std::filesystem::file_time_type {
            throw std::filesystem::filesystem_error{"mtime unavailable", std::make_error_code(std::errc::io_error)};
        };
    }
    TagLibMetadataExtractor reader{std::move(options)};

    const auto result = reader.read(fixture("fixtures/valid/wav-pcm-mono.wav"));
    const auto& metadata = requireMetadata(result);

    CHECK_FALSE(metadata.last_modified_time.has_value());
    CHECK(metadata.display_title == "wav-pcm-mono");
    REQUIRE(metadata.audio.duration_ms.has_value());
    CHECK(*metadata.audio.duration_ms == 1'000);
}

TEST_CASE("MR-014 and MR-015 enforce the retained metadata aggregate boundary without overflow",
          "[m0][qt][metadata][limits][MR-014][MR-015]") {
    MetadataRetentionBudget budget;

    for (std::size_t index = 0;
         index < MetadataRetentionBudget::total_limit_bytes / MetadataRetentionBudget::field_limit_bytes; ++index) {
        CAPTURE(index);
        CHECK_FALSE(budget.tryRetain(MetadataField::Title, MetadataRetentionBudget::field_limit_bytes).has_value());
    }
    CHECK(budget.retainedBytes() == MetadataRetentionBudget::total_limit_bytes);

    const auto diagnostic = budget.tryRetain(MetadataField::Artist, 1);
    REQUIRE(diagnostic.has_value());
    CHECK(diagnostic->code == MetadataDiagnosticCode::RetainedMetadataTooLarge);
    CHECK(diagnostic->field == MetadataField::Artist);
    CHECK(diagnostic->observed_bytes == MetadataRetentionBudget::total_limit_bytes + 1);
    CHECK(diagnostic->limit_bytes == MetadataRetentionBudget::total_limit_bytes);
    CHECK(budget.retainedBytes() == MetadataRetentionBudget::total_limit_bytes);
}

TEST_CASE("MR-101 reads WAV PCM properties, absent tags, and title fallback", "[m0][qt][metadata][taglib][MR-101]") {
    const TagLibMetadataExtractor reader;

    const auto result = reader.read(fixture("fixtures/valid/wav-pcm-mono.wav"));
    const auto& metadata = requireMetadata(result);

    REQUIRE(metadata.audio.duration_ms.has_value());
    REQUIRE(metadata.audio.sample_rate_hz.has_value());
    REQUIRE(metadata.audio.channels.has_value());
    CHECK(*metadata.audio.duration_ms == 1'000);
    CHECK(*metadata.audio.sample_rate_hz == 48'000);
    CHECK(*metadata.audio.channels == 1);
    REQUIRE(metadata.audio.bit_depth.has_value());
    CHECK(*metadata.audio.bit_depth == 16);
    CHECK(metadata.audio.container == "WAV");
    CHECK(metadata.audio.codec == "PCM signed 16-bit little-endian");
    CHECK(metadata.file_size_bytes == 96'044);
    CHECK_FALSE(metadata.tags.title.has_value());
    CHECK_FALSE(metadata.tags.artist.has_value());
    CHECK_FALSE(metadata.tags.album.has_value());
    CHECK_FALSE(metadata.tags.track_number.has_value());
    CHECK_FALSE(metadata.tags.year.has_value());
    CHECK(metadata.display_title == "wav-pcm-mono");
}

TEST_CASE("MR-102 recognizes WAV content stored with an MP3 extension", "[m0][qt][metadata][taglib][MR-102]") {
    const TagLibMetadataExtractor reader;

    const auto result = reader.read(fixture("fixtures/edge/wrong-extension-wav-as-mp3.mp3"));
    const auto& metadata = requireMetadata(result);

    CHECK(metadata.audio.container == "WAV");
    CHECK(metadata.audio.codec == "PCM signed 16-bit little-endian");
    REQUIRE(metadata.audio.duration_ms.has_value());
    REQUIRE(metadata.audio.sample_rate_hz.has_value());
    REQUIRE(metadata.audio.channels.has_value());
    CHECK(*metadata.audio.duration_ms == 1'000);
    CHECK(*metadata.audio.sample_rate_hz == 48'000);
    CHECK(*metadata.audio.channels == 1);
    REQUIRE(metadata.audio.bit_depth.has_value());
    CHECK(*metadata.audio.bit_depth == 16);
    CHECK(metadata.display_title == "wrong-extension-wav-as-mp3");
}

TEST_CASE("MR-001 and MR-018 read authenticated MP3 CBR and VBR tags and properties by content",
          "[m0][qt][metadata][taglib][mp3][MR-001][MR-018]") {
    const TagLibMetadataExtractor reader;

    struct Mp3Case final {
        std::filesystem::path relative_path;
        std::uint32_t minimum_bitrate_kbps;
        std::uint32_t maximum_bitrate_kbps;
    };

    for (const auto& mp3_case : {
             Mp3Case{"fixtures/valid/mp3-cbr-stereo.mp3", 120, 136},
             Mp3Case{"fixtures/valid/mp3-vbr-stereo.mp3", 32, 96},
         }) {
        CAPTURE(mp3_case.relative_path.string());
        const auto result = reader.read(fixture(mp3_case.relative_path));
        const auto& metadata = requireMetadata(result);

        CHECK(metadata.audio.container == "MP3");
        CHECK(metadata.audio.codec == "MPEG-1 Audio Layer III");
        REQUIRE(metadata.audio.duration_ms.has_value());
        CHECK(*metadata.audio.duration_ms >= 1'950);
        CHECK(*metadata.audio.duration_ms <= 2'050);
        REQUIRE(metadata.audio.sample_rate_hz.has_value());
        CHECK(*metadata.audio.sample_rate_hz == 48'000);
        REQUIRE(metadata.audio.channels.has_value());
        CHECK(*metadata.audio.channels == 2);
        CHECK_FALSE(metadata.audio.bit_depth.has_value());
        REQUIRE(metadata.audio.bitrate_kbps.has_value());
        CHECK(*metadata.audio.bitrate_kbps >= mp3_case.minimum_bitrate_kbps);
        CHECK(*metadata.audio.bitrate_kbps <= mp3_case.maximum_bitrate_kbps);
        CHECK(metadata.tags.title == std::optional<std::string>{"Canção determinística 日本語 🎵"});
        CHECK(metadata.tags.artist == std::optional<std::string>{"João – Codex"});
        CHECK(metadata.tags.album == std::optional<std::string>{"Álbum de teste Ω"});
        CHECK(metadata.tags.year == std::optional<std::uint32_t>{2026});
        CHECK_FALSE(metadata.tags.track_number.has_value());
        CHECK(metadata.display_title == "Canção determinística 日本語 🎵");
    }
}

TEST_CASE("MR-001 preserves Unicode ID3v2 tags from the authenticated MP3 corpus",
          "[m0][qt][metadata][taglib][mp3][unicode][MR-001]") {
    const TagLibMetadataExtractor reader;
    const auto source = fixture("fixtures/valid/mp3-unicode-tags.mp3");

    const auto result = reader.read(source);
    const auto& metadata = requireMetadata(result);

    CHECK(metadata.source_path == source);
    CHECK(metadata.file_size_bytes == 24'966);
    CHECK(metadata.tags.title == std::optional<std::string>{"Canção determinística 日本語 🎵"});
    CHECK(metadata.tags.artist == std::optional<std::string>{"João – Codex"});
    CHECK(metadata.tags.album == std::optional<std::string>{"Álbum de teste Ω"});
    CHECK(metadata.tags.year == std::optional<std::uint32_t>{2026});
    CHECK(metadata.display_title == "Canção determinística 日本語 🎵");
    CHECK(metadata.audio.container == "MP3");
    CHECK(metadata.audio.codec == "MPEG-1 Audio Layer III");
    REQUIRE(metadata.audio.duration_ms.has_value());
    CHECK(*metadata.audio.duration_ms >= 1'950);
    CHECK(*metadata.audio.duration_ms <= 2'050);
    REQUIRE(metadata.audio.bitrate_kbps.has_value());
    CHECK(*metadata.audio.bitrate_kbps >= 88);
    CHECK(*metadata.audio.bitrate_kbps <= 104);
}

TEST_CASE("MR-001 and MR-018 read the authenticated basic codec matrix",
          "[m0][qt][metadata][taglib][codec-matrix][MR-001][MR-018]") {
    struct CodecCase final {
        std::filesystem::path relative_path;
        std::filesystem::path neutral_filename;
        std::uintmax_t file_size_bytes;
        std::int64_t duration_tolerance_ms;
        std::string container;
        std::string codec;
        std::optional<std::uint16_t> bit_depth;
    };

    const ScopedTemporaryDirectory directory;
    const TagLibMetadataExtractor reader;
    for (const auto& codec_case : {
             CodecCase{"fixtures/valid/m4a-aac-lc.m4a", "m4a-content.audio", 33'400, 25, "ISO MP4/M4A", "AAC",
                       std::nullopt},
             CodecCase{"fixtures/valid/flac-16-stereo.flac", "flac-content.audio", 60'531, 20, "FLAC", "FLAC",
                       std::uint16_t{16}},
             CodecCase{"fixtures/valid/ogg-vorbis.ogg", "vorbis-content.audio", 12'605, 20, "Ogg", "Vorbis",
                       std::nullopt},
         }) {
        CAPTURE(codec_case.relative_path.string());
        const ScopedTemporaryFile neutral_extension{temporaryPath(directory, codec_case.neutral_filename)};
        writeBytes(neutral_extension.path(), readBytes(fixture(codec_case.relative_path)));

        const auto result = reader.read(neutral_extension.path());
        const auto& metadata = requireMetadata(result);

        CHECK(metadata.file_size_bytes == codec_case.file_size_bytes);
        CHECK(metadata.audio.container == codec_case.container);
        CHECK(metadata.audio.codec == codec_case.codec);
        REQUIRE(metadata.audio.duration_ms.has_value());
        CHECK(*metadata.audio.duration_ms >= 2'000 - codec_case.duration_tolerance_ms);
        CHECK(*metadata.audio.duration_ms <= 2'000 + codec_case.duration_tolerance_ms);
        REQUIRE(metadata.audio.sample_rate_hz.has_value());
        CHECK(*metadata.audio.sample_rate_hz == 48'000);
        REQUIRE(metadata.audio.channels.has_value());
        CHECK(*metadata.audio.channels == 2);
        CHECK(metadata.audio.bit_depth == codec_case.bit_depth);
        REQUIRE(metadata.audio.bitrate_kbps.has_value());
        CHECK(*metadata.audio.bitrate_kbps > 0);
        CHECK(metadata.tags.title == std::optional<std::string>{"Canção determinística 日本語 🎵"});
        CHECK(metadata.tags.artist == std::optional<std::string>{"João – Codex"});
        CHECK(metadata.tags.album == std::optional<std::string>{"Álbum de teste Ω"});
        CHECK(metadata.tags.year == std::optional<std::uint32_t>{2026});
        CHECK_FALSE(metadata.tags.track_number.has_value());
        CHECK(metadata.display_title == "Canção determinística 日本語 🎵");
    }
}

TEST_CASE("Opus and ALAC candidates return explicit unsupported metadata outcomes",
          "[m0][qt][metadata][taglib][candidate-codec]") {
    const TagLibMetadataExtractor reader;
    for (const auto& relative_path : {
             std::filesystem::path{"fixtures/candidate/opus-candidate.opus"},
             std::filesystem::path{"fixtures/candidate/m4a-alac-candidate.m4a"},
         }) {
        CAPTURE(relative_path.string());
        const auto result = reader.read(fixture(relative_path));
        const auto& read_error = requireError(result);

        CHECK(read_error.code == MetadataReadErrorCode::UnsupportedFormat);
        CHECK_FALSE(read_error.message.empty());
    }
}

TEST_CASE("FR-005 recognizes FLAC beyond a leading ID3 tag despite its MP3 extension",
          "[m0][qt][metadata][taglib][content-detection][id3-prefixed-flac][FR-005]") {
    constexpr std::size_t flac_offset = 610;
    const auto canonical = productFixture("fixtures/valid/flac-16-stereo.flac");
    const auto source = productFixture("fixtures/edge/id3-prefixed-flac-as-mp3.mp3");
    const auto canonical_bytes = readBytes(canonical);
    const auto source_bytes = readBytes(source);
    REQUIRE(source_bytes.size() == flac_offset + canonical_bytes.size());
    CHECK(std::equal(source_bytes.begin() + flac_offset, source_bytes.end(), canonical_bytes.begin()));
    const std::vector<std::uint8_t> marker{'f', 'L', 'a', 'C'};
    const auto tag_end = source_bytes.begin() + flac_offset;
    REQUIRE(std::search(source_bytes.begin(), tag_end, marker.begin(), marker.end()) != tag_end);

    const TagLibMetadataExtractor reader;
    const auto canonical_result = reader.read(canonical);
    const auto& baseline = requireMetadata(canonical_result);
    const auto result = reader.read(source);
    const auto& metadata = requireMetadata(result);

    CHECK(metadata.source_path == source);
    CHECK(metadata.file_size_bytes == source_bytes.size());
    CHECK(metadata.audio.container == "FLAC");
    CHECK(metadata.audio.codec == "FLAC");
    CHECK(metadata.audio.duration_ms == baseline.audio.duration_ms);
    CHECK(metadata.audio.sample_rate_hz == baseline.audio.sample_rate_hz);
    CHECK(metadata.audio.channels == baseline.audio.channels);
    CHECK(metadata.audio.bit_depth == baseline.audio.bit_depth);
    CHECK(metadata.tags.title == baseline.tags.title);
    CHECK(metadata.tags.artist == baseline.tags.artist);
    CHECK(metadata.tags.album == baseline.tags.album);
    CHECK(metadata.display_title == baseline.display_title);
    CHECK(readBytes(source) == source_bytes);
}

TEST_CASE("FR-005 respects the ID3v2.4 footer before a FLAC stream",
          "[m0][qt][metadata][taglib][content-detection][id3-prefixed-flac][FR-005]") {
    const ScopedTemporaryDirectory directory;
    const ScopedTemporaryFile source{temporaryPath(directory, "id3-footer-flac.audio")};
    const auto canonical = readBytes(productFixture("fixtures/valid/flac-16-stereo.flac"));
    std::vector<std::uint8_t> bytes{
        'I', 'D', '3', 4,   0,   0x10, 0,   0,   0,   20,  // ID3v2.4 header
        'P', 'R', 'I', 'V', 0,   0,    0,   10,  0,   0,   // PRIV frame header
        'o', 'w', 'n', 'e', 'r', 0,    'f', 'L', 'a', 'C', // owner and private bytes
        '3', 'D', 'I', 4,   0,   0x10, 0,   0,   0,   20,  // ID3v2.4 footer
    };
    bytes.insert(bytes.end(), canonical.begin(), canonical.end());
    writeBytes(source.path(), bytes);

    const TagLibMetadataExtractor reader;
    const auto result = reader.read(source.path());
    const auto& metadata = requireMetadata(result);

    CHECK(metadata.audio.container == "FLAC");
    CHECK(metadata.audio.codec == "FLAC");
    CHECK(metadata.audio.sample_rate_hz == std::optional<std::uint32_t>{48'000});
    CHECK(readBytes(source.path()) == bytes);
}

TEST_CASE("MR-103 rejects malformed ID3 lengths before falling back to a FLAC signature",
          "[m0][qt][metadata][taglib][hostile][id3-prefixed-flac][MR-103]") {
    const TagLibMetadataExtractor reader;
    for (const auto& relative_path : {
             std::filesystem::path{"fixtures/hostile/id3-prefixed-flac-size-past-eof.mp3"},
             std::filesystem::path{"fixtures/hostile/id3-prefixed-flac-invalid-size-byte.mp3"},
         }) {
        CAPTURE(relative_path.string());
        const auto source = productFixture(relative_path);
        const auto bytes = readBytes(source);
        const auto result = reader.read(source);
        const auto& read_error = requireError(result);
        CHECK(read_error.code == MetadataReadErrorCode::InvalidData);
        CHECK_FALSE(read_error.message.empty());
        CHECK(readBytes(source) == bytes);
    }

    const auto valid = reader.read(productFixture("fixtures/valid/flac-16-stereo.flac"));
    CHECK(requireMetadata(valid).audio.codec == "FLAC");
}

TEST_CASE("ISO BMFF content with an extended ftyp size is recognized before metadata validation",
          "[m0][qt][metadata][taglib][content-detection][extended-ftyp]") {
    const ScopedTemporaryDirectory directory;
    const ScopedTemporaryFile source{temporaryPath(directory, "extended-ftyp.audio")};
    writeBytes(source.path(), {
                                  0x00, 0x00, 0x00, 0x01, 'f', 't', 'y', 'p', 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x18, 'M', '4', 'A', ' ', 0x00, 0x00, 0x00, 0x00,
                              });
    const TagLibMetadataExtractor reader;

    const auto result = reader.read(source.path());
    const auto& read_error = requireError(result);

    CHECK(read_error.code == MetadataReadErrorCode::InvalidData);
    CHECK_FALSE(read_error.message.empty());
}

TEST_CASE("MR-003 MR-018 and MR-019 recognize tagless MP3 content and use the Unicode stem fallback",
          "[m0][qt][metadata][taglib][mp3][fallback][MR-003][MR-018][MR-019]") {
    const ScopedTemporaryDirectory directory;
    const ScopedTemporaryFile temporary{
        temporaryPath(directory, std::filesystem::path{L"music-player-m0-MP3 sem tags 日本語.audio"})};
    writeBytes(temporary.path(), withoutId3Tags(readBytes(fixture("fixtures/valid/mp3-cbr-stereo.mp3"))));
    const TagLibMetadataExtractor reader;

    const auto result = reader.read(temporary.path());
    const auto& metadata = requireMetadata(result);

    CHECK(metadata.audio.container == "MP3");
    CHECK(metadata.audio.codec == "MPEG-1 Audio Layer III");
    CHECK_FALSE(metadata.tags.title.has_value());
    CHECK_FALSE(metadata.tags.artist.has_value());
    CHECK_FALSE(metadata.tags.album.has_value());
    CHECK_FALSE(metadata.tags.track_number.has_value());
    CHECK_FALSE(metadata.tags.year.has_value());
    CHECK(metadata.display_title == "music-player-m0-MP3 sem tags 日本語");
}

TEST_CASE("MR-103 rejects truncated and random MP3-shaped input safely",
          "[m0][qt][metadata][taglib][mp3][hostile][MR-103]") {
    const ScopedTemporaryDirectory directory;
    const auto authenticated = readBytes(fixture("fixtures/valid/mp3-cbr-stereo.mp3"));
    REQUIRE(authenticated.size() > 10);
    const ScopedTemporaryFile truncated{temporaryPath(directory, "music-player-m0-truncated.mp3")};
    writeBytes(truncated.path(), std::vector<std::uint8_t>{authenticated.begin(), authenticated.begin() + 10});

    const ScopedTemporaryFile random{temporaryPath(directory, "music-player-m0-random.mp3")};
    writeBytes(random.path(), {0xff, 0xfb, 0x90, 0x64, 0x52, 0x41, 0x4e, 0x44, 0x4f, 0x4d, 0x00, 0x01});

    const TagLibMetadataExtractor reader;
    for (const auto& source : {truncated.path(), random.path()}) {
        CAPTURE(source.string());
        std::optional<MetadataReadResult> result;

        REQUIRE_NOTHROW(result.emplace(reader.read(source)));
        REQUIRE(result.has_value());
        const auto& read_error = requireError(*result);
        CHECK((read_error.code == MetadataReadErrorCode::InvalidData ||
               read_error.code == MetadataReadErrorCode::UnsupportedFormat));
        CHECK_FALSE(read_error.message.empty());
    }
}

TEST_CASE("FR-023 accepts an ID3 tag body at the metadata read limit",
          "[m0][qt][metadata][taglib][mp3][metadata-read-limit][FR-023]") {
    constexpr std::size_t read_limit_bytes = 32U * 1024U * 1024U;
    const ScopedTemporaryDirectory directory;
    const ScopedTemporaryFile padded{temporaryPath(directory, "id3-at-read-limit.mp3")};
    const auto authenticated = fixture("fixtures/valid/mp3-cbr-stereo.mp3");
    REQUIRE(writeMp3WithPaddedId3(authenticated, padded.path(), read_limit_bytes));
    const auto padded_size = std::filesystem::file_size(padded.path());
    const auto padded_timestamp = std::filesystem::last_write_time(padded.path());

    const TagLibMetadataExtractor reader;
    const auto baseline_result = reader.read(authenticated);
    const auto& baseline = requireMetadata(baseline_result);
    const auto result = reader.read(padded.path());
    const auto& metadata = requireMetadata(result);
    CHECK(metadata.file_size_bytes == padded_size);
    CHECK(metadata.audio.codec == baseline.audio.codec);
    CHECK(metadata.audio.duration_ms == baseline.audio.duration_ms);
    CHECK(metadata.audio.sample_rate_hz == baseline.audio.sample_rate_hz);
    CHECK(metadata.audio.channels == baseline.audio.channels);
    CHECK(metadata.tags.title == baseline.tags.title);
    CHECK(metadata.tags.artist == baseline.tags.artist);
    CHECK(metadata.tags.album == baseline.tags.album);
    CHECK(std::filesystem::file_size(padded.path()) == padded_size);
    CHECK(std::filesystem::last_write_time(padded.path()) == padded_timestamp);
}

TEST_CASE("FR-023 rejects an ID3 tag body one byte over the metadata read limit and recovers",
          "[m0][qt][metadata][taglib][mp3][hostile][metadata-read-limit][FR-023]") {
    constexpr std::size_t read_limit_bytes = 32U * 1024U * 1024U;
    const ScopedTemporaryDirectory directory;
    const ScopedTemporaryFile padded{temporaryPath(directory, "id3-above-read-limit.mp3")};
    const auto authenticated = fixture("fixtures/valid/mp3-cbr-stereo.mp3");
    REQUIRE(writeMp3WithPaddedId3(authenticated, padded.path(), read_limit_bytes + 1U));
    const auto padded_size = std::filesystem::file_size(padded.path());
    const auto padded_timestamp = std::filesystem::last_write_time(padded.path());

    const TagLibMetadataExtractor reader;
    const auto result = reader.read(padded.path());
    const auto& error = requireError(result);
    CHECK(error.code == MetadataReadErrorCode::InvalidData);
    CHECK(error.message.find("32 MiB") != std::string::npos);
    CHECK(std::filesystem::file_size(padded.path()) == padded_size);
    CHECK(std::filesystem::last_write_time(padded.path()) == padded_timestamp);

    const auto recovered = reader.read(authenticated);
    CHECK(requireMetadata(recovered).audio.codec == "MPEG-1 Audio Layer III");
}

TEST_CASE("FR-023 accepts several FLAC application blocks within the cumulative metadata read budget",
          "[m0][qt][metadata][taglib][flac][metadata-read-budget][FR-023]") {
    constexpr std::size_t block_bytes = 15U * 1024U * 1024U;
    const ScopedTemporaryDirectory directory;
    const ScopedTemporaryFile augmented{temporaryPath(directory, "flac-within-read-budget.flac")};
    const auto authenticated = fixture("fixtures/valid/flac-16-stereo.flac");
    REQUIRE(writeFlacWithApplicationBlocks(authenticated, augmented.path(), 4, block_bytes));

    const TagLibMetadataExtractor reader;
    const auto baseline_result = reader.read(authenticated);
    const auto& baseline = requireMetadata(baseline_result);
    const auto result = reader.read(augmented.path());
    const auto& metadata = requireMetadata(result);
    CHECK(metadata.file_size_bytes == std::filesystem::file_size(augmented.path()));
    CHECK(metadata.audio.codec == baseline.audio.codec);
    CHECK(metadata.audio.duration_ms == baseline.audio.duration_ms);
    CHECK(metadata.audio.sample_rate_hz == baseline.audio.sample_rate_hz);
    CHECK(metadata.audio.channels == baseline.audio.channels);
    CHECK(metadata.tags.title == baseline.tags.title);
    CHECK(metadata.tags.artist == baseline.tags.artist);
}

TEST_CASE("FR-023 rejects FLAC application blocks over the cumulative metadata read budget and recovers",
          "[m0][qt][metadata][taglib][flac][hostile][metadata-read-budget][FR-023]") {
    constexpr std::size_t block_bytes = 15U * 1024U * 1024U;
    const ScopedTemporaryDirectory directory;
    const ScopedTemporaryFile augmented{temporaryPath(directory, "flac-above-read-budget.flac")};
    const auto authenticated = fixture("fixtures/valid/flac-16-stereo.flac");
    REQUIRE(writeFlacWithApplicationBlocks(authenticated, augmented.path(), 5, block_bytes));
    const auto augmented_size = std::filesystem::file_size(augmented.path());
    const auto augmented_timestamp = std::filesystem::last_write_time(augmented.path());

    const TagLibMetadataExtractor reader;
    const auto result = reader.read(augmented.path());
    const auto& error = requireError(result);
    CHECK(error.code == MetadataReadErrorCode::InvalidData);
    CHECK(error.message.find("64 MiB") != std::string::npos);
    CHECK(std::filesystem::file_size(augmented.path()) == augmented_size);
    CHECK(std::filesystem::last_write_time(augmented.path()) == augmented_timestamp);

    const auto recovered = reader.read(authenticated);
    CHECK(requireMetadata(recovered).audio.codec == "FLAC");
}

TEST_CASE("FR-023 rejects numerous tiny FLAC application blocks over the metadata read count",
          "[m0][qt][metadata][taglib][flac][hostile][metadata-read-budget][FR-023]") {
    const ScopedTemporaryDirectory directory;
    const ScopedTemporaryFile augmented{temporaryPath(directory, "flac-above-read-count.flac")};
    const auto authenticated = fixture("fixtures/valid/flac-16-stereo.flac");
    REQUIRE(writeFlacWithApplicationBlocks(authenticated, augmented.path(), 33'000, 4));
    REQUIRE(std::filesystem::file_size(augmented.path()) < 1024U * 1024U);

    const TagLibMetadataExtractor reader;
    const auto result = reader.read(augmented.path());
    const auto& error = requireError(result);
    CHECK(error.code == MetadataReadErrorCode::InvalidData);
    CHECK(error.message.find("65536") != std::string::npos);

    const auto recovered = reader.read(authenticated);
    CHECK(requireMetadata(recovered).audio.codec == "FLAC");
}

TEST_CASE("FR-023 reads metadata for a PCM WAV larger than the cumulative metadata read budget",
          "[m0][qt][metadata][taglib][wav][metadata-read-budget][FR-023]") {
    constexpr std::uint32_t pcm_bytes = 65U * 1024U * 1024U;
    const ScopedTemporaryDirectory directory;
    const ScopedTemporaryFile extended{temporaryPath(directory, "large-pcm.wav")};
    auto bytes = readBytes(fixture("fixtures/valid/wav-pcm-mono.wav"));
    REQUIRE(bytes.size() == 96'044);
    REQUIRE(std::equal(bytes.begin() + 36, bytes.begin() + 40, "data"));
    const auto write_size = [&](const std::size_t offset, const std::uint32_t size) {
        for (std::size_t index = 0; index < 4; ++index) {
            bytes[offset + index] = static_cast<std::uint8_t>(size >> (index * 8U));
        }
    };
    write_size(4, pcm_bytes + 36);
    write_size(40, pcm_bytes);
    writeBytes(extended.path(), bytes);
    std::filesystem::resize_file(extended.path(), pcm_bytes + 44);

    const auto result = TagLibMetadataExtractor{}.read(extended.path());
    const auto& metadata = requireMetadata(result);
    CHECK(metadata.file_size_bytes == pcm_bytes + 44);
    CHECK(metadata.audio.sample_rate_hz == std::optional<std::uint32_t>{48'000});
    CHECK(metadata.audio.channels == std::optional<std::uint16_t>{1});
    CHECK(metadata.audio.duration_ms == std::optional<std::int64_t>{static_cast<std::int64_t>(pcm_bytes) / 96});
}

TEST_CASE("FR-023 accepts continued Ogg Vorbis comments at the header packet limit",
          "[m0][qt][metadata][taglib][ogg][vorbis][ogg-header-packet-limit][FR-023]") {
    constexpr std::size_t packet_limit_bytes = 30U * 1024U * 1024U;
    const ScopedTemporaryDirectory directory;
    const ScopedTemporaryFile padded{temporaryPath(directory, "vorbis-at-packet-limit.ogg")};
    const auto authenticated = fixture("fixtures/valid/ogg-vorbis.ogg");
    REQUIRE(writeOggWithCommentPacketSize(authenticated, padded.path(), packet_limit_bytes));
    const auto layout = readOggCommentPacketLayout(padded.path());
    REQUIRE(layout.size == packet_limit_bytes);
    REQUIRE(layout.pages > 1);

    const TagLibMetadataExtractor reader;
    const auto baseline_result = reader.read(authenticated);
    const auto& baseline = requireMetadata(baseline_result);
    const auto result = reader.read(padded.path());
    const auto& metadata = requireMetadata(result);
    CHECK(metadata.audio.codec == "Vorbis");
    CHECK(metadata.audio.duration_ms == baseline.audio.duration_ms);
    CHECK(metadata.audio.sample_rate_hz == baseline.audio.sample_rate_hz);
    CHECK(metadata.audio.channels == baseline.audio.channels);
    CHECK(metadata.tags.title == baseline.tags.title);
}

TEST_CASE("FR-023 rejects a continued Ogg Vorbis header packet over the limit and recovers",
          "[m0][qt][metadata][taglib][ogg][vorbis][hostile][ogg-header-packet-limit][FR-023]") {
    constexpr std::size_t packet_limit_bytes = 30U * 1024U * 1024U;
    const ScopedTemporaryDirectory directory;
    const ScopedTemporaryFile padded{temporaryPath(directory, "vorbis-above-packet-limit.ogg")};
    const auto authenticated = fixture("fixtures/valid/ogg-vorbis.ogg");
    REQUIRE(writeOggWithCommentPacketSize(authenticated, padded.path(), packet_limit_bytes + 1U));
    const auto layout = readOggCommentPacketLayout(padded.path());
    REQUIRE(layout.size == packet_limit_bytes + 1U);
    REQUIRE(layout.pages > 1);

    const TagLibMetadataExtractor reader;
    const auto result = reader.read(padded.path());
    const auto& error = requireError(result);
    CHECK(error.code == MetadataReadErrorCode::InvalidData);
    CHECK(error.message == "Ogg Vorbis header packet exceeds 30 MiB");
    const auto recovered = reader.read(authenticated);
    CHECK(requireMetadata(recovered).audio.codec == "Vorbis");
}

TEST_CASE("FR-023 accepts a zero lace terminating a continued Ogg Vorbis comment packet",
          "[m0][qt][metadata][taglib][ogg][vorbis][ogg-header-packet-limit][FR-023]") {
    constexpr std::size_t packet_bytes = 256U * 255U;
    const ScopedTemporaryDirectory directory;
    const ScopedTemporaryFile padded{temporaryPath(directory, "vorbis-zero-terminating-lace.ogg")};
    const auto authenticated = fixture("fixtures/valid/ogg-vorbis.ogg");
    REQUIRE(writeOggWithCommentPacketSize(authenticated, padded.path(), packet_bytes));
    const auto layout = readOggCommentPacketLayout(padded.path());
    REQUIRE(layout.size == packet_bytes);
    REQUIRE(layout.pages > 1);
    REQUIRE(layout.terminating_lace == 0);

    const auto result = TagLibMetadataExtractor{}.read(padded.path());
    CHECK(requireMetadata(result).audio.codec == "Vorbis");
}

TEST_CASE("MR-103 returns typed errors for empty and truncated input without crashing",
          "[m0][qt][metadata][taglib][MR-103]") {
    const TagLibMetadataExtractor reader;

    for (const auto& relative_path : {
             std::filesystem::path{"fixtures/hostile/empty.bin"},
             std::filesystem::path{"fixtures/hostile/truncated-wav-pcm-mono.wav"},
         }) {
        CAPTURE(relative_path.string());
        std::optional<MetadataReadResult> result;

        REQUIRE_NOTHROW(result.emplace(reader.read(fixture(relative_path))));
        REQUIRE(result.has_value());
        const auto& error = requireError(*result);
        CHECK(error.code == MetadataReadErrorCode::InvalidData);
        CHECK_FALSE(error.message.empty());
    }
}

TEST_CASE("MR-103 rejects structurally recognizable truncated codec fixtures safely",
          "[m0][qt][metadata][taglib][hostile][codec-matrix][MR-103]") {
    struct TruncatedCodecCase final {
        std::filesystem::path relative_path;
        std::filesystem::path truncated_relative_path;
    };

    constexpr std::size_t truncated_size = 64;
    const TagLibMetadataExtractor reader;

    for (const auto& codec_case : {
             TruncatedCodecCase{"fixtures/valid/mp3-cbr-stereo.mp3", "fixtures/hostile/truncated-mp3-cbr.mp3"},
             TruncatedCodecCase{"fixtures/valid/m4a-aac-lc.m4a", "fixtures/hostile/truncated-m4a-aac.m4a"},
             TruncatedCodecCase{"fixtures/valid/flac-16-stereo.flac", "fixtures/hostile/truncated-flac.flac"},
             TruncatedCodecCase{"fixtures/valid/ogg-vorbis.ogg", "fixtures/hostile/truncated-ogg-vorbis.ogg"},
         }) {
        CAPTURE(codec_case.relative_path.string());
        const auto authenticated = readBytes(fixture(codec_case.relative_path));
        REQUIRE(authenticated.size() > truncated_size);

        const auto truncated = productFixture(codec_case.truncated_relative_path);
        const auto truncated_bytes = readBytes(truncated);
        REQUIRE(truncated_bytes.size() == truncated_size);
        REQUIRE(std::equal(truncated_bytes.begin(), truncated_bytes.end(), authenticated.begin()));

        std::optional<MetadataReadResult> result;
        REQUIRE_NOTHROW(result.emplace(reader.read(truncated)));
        REQUIRE(result.has_value());
        const auto& read_error = requireError(*result);
        CHECK(read_error.code == MetadataReadErrorCode::InvalidData);
        CHECK_FALSE(read_error.message.empty());
    }
}

TEST_CASE("FR-021 extracts bounded RGBA thumbnails from valid PNG and JPEG embedded MP3 artwork",
          "[m0][qt][metadata][taglib][artwork][FR-021]") {
    struct EncodedArtworkCase final {
        std::filesystem::path filename;
        std::vector<std::uint8_t> bytes;
        std::string mime_type;
        std::uint32_t expected_width;
        std::uint32_t expected_height;
    };

    const ScopedTemporaryDirectory directory;
    const std::vector<EncodedArtworkCase> artwork_cases{
        {"embedded-cover-png.mp3", onePixelPng(), "image/png", 1, 1},
        {"embedded-cover-jpeg.mp3", encodedImage(2, 1, "JPEG"), "image/jpeg", 2, 1},
    };
    for (const auto& artwork_case : artwork_cases) {
        CAPTURE(artwork_case.filename.string(), artwork_case.mime_type);
        const ScopedTemporaryFile source{temporaryPath(directory, artwork_case.filename)};
        writeBytes(source.path(), readBytes(fixture("fixtures/valid/mp3-cbr-stereo.mp3")));
        writeMp3Artwork(source.path(), artwork_case.bytes, artwork_case.mime_type);

        const auto result = TagLibMetadataExtractor{}.read(source.path());
        const auto& metadata = requireMetadata(result);

        REQUIRE(metadata.artwork.has_value());
        CHECK(metadata.artwork->width_pixels == artwork_case.expected_width);
        CHECK(metadata.artwork->height_pixels == artwork_case.expected_height);
        REQUIRE(metadata.artwork->rgba8888 != nullptr);
        CHECK(metadata.artwork->rgba8888->size() ==
              static_cast<std::size_t>(artwork_case.expected_width) * artwork_case.expected_height * 4U);
        CHECK(metadata.artwork_diagnostics.empty());
        REQUIRE(metadata.audio.duration_ms.has_value());
        CHECK(*metadata.audio.duration_ms > 0);
    }
}

TEST_CASE("FR-021 extracts embedded artwork across the authenticated local codec matrix",
          "[m0][qt][metadata][taglib][artwork][codec-matrix][FR-021]") {
    struct ArtworkCase final {
        std::filesystem::path relative_path;
        std::string expected_container;
        std::string expected_codec;
    };

    for (const auto& artwork_case : {
             ArtworkCase{"fixtures/artwork/m4a-aac-art.m4a", "ISO MP4/M4A", "AAC"},
             ArtworkCase{"fixtures/artwork/flac-art.flac", "FLAC", "FLAC"},
             ArtworkCase{"fixtures/artwork/ogg-vorbis-art.ogg", "Ogg", "Vorbis"},
         }) {
        CAPTURE(artwork_case.relative_path.string());
        const auto result = TagLibMetadataExtractor{}.read(productFixture(artwork_case.relative_path));
        const auto& metadata = requireMetadata(result);

        REQUIRE(metadata.artwork.has_value());
        CHECK(metadata.artwork->width_pixels == 1);
        CHECK(metadata.artwork->height_pixels == 1);
        REQUIRE(metadata.artwork->rgba8888 != nullptr);
        CHECK(metadata.artwork->rgba8888->size() == 4);
        CHECK(metadata.artwork_diagnostics.empty());
        CHECK(metadata.audio.container == artwork_case.expected_container);
        CHECK(metadata.audio.codec == artwork_case.expected_codec);
        REQUIRE(metadata.audio.duration_ms.has_value());
        CHECK(*metadata.audio.duration_ms >= 1'975);
        CHECK(*metadata.audio.duration_ms <= 2'025);
        REQUIRE(metadata.audio.sample_rate_hz.has_value());
        CHECK(*metadata.audio.sample_rate_hz == 48'000);
        REQUIRE(metadata.audio.channels.has_value());
        CHECK(*metadata.audio.channels == 2);
        CHECK(metadata.tags.title == std::optional<std::string>{"Canção determinística 日本語 🎵"});
        CHECK(metadata.tags.artist == std::optional<std::string>{"João – Codex"});
        CHECK(metadata.tags.album == std::optional<std::string>{"Álbum de teste Ω"});
    }
}

TEST_CASE("MR-005 discards corrupt embedded MP3 artwork without failing metadata or audio properties",
          "[m0][qt][metadata][taglib][artwork][MR-005]") {
    const ScopedTemporaryDirectory directory;
    const ScopedTemporaryFile source{temporaryPath(directory, "corrupt-cover.mp3")};
    writeBytes(source.path(), readBytes(fixture("fixtures/valid/mp3-cbr-stereo.mp3")));
    writeMp3Artwork(source.path(), {'n', 'o', 't', '-', 'a', 'n', '-', 'i', 'm', 'a', 'g', 'e'});

    const auto result = TagLibMetadataExtractor{}.read(source.path());
    const auto& metadata = requireMetadata(result);

    CHECK_FALSE(metadata.artwork.has_value());
    REQUIRE(metadata.artwork_diagnostics.size() == 1);
    CHECK(metadata.artwork_diagnostics.front().code == ArtworkDiagnosticCode::InvalidData);
    CHECK(metadata.artwork_diagnostics.front().encoded_bytes == 12);
    REQUIRE(metadata.audio.duration_ms.has_value());
    CHECK(*metadata.audio.duration_ms > 0);
}

TEST_CASE("NFR-LIMIT-001 scales embedded MP3 artwork down while preserving its aspect ratio",
          "[m0][qt][metadata][taglib][artwork][limits][NFR-LIMIT-001]") {
    const ScopedTemporaryDirectory directory;
    const ScopedTemporaryFile source{temporaryPath(directory, "large-cover.mp3")};
    writeBytes(source.path(), readBytes(fixture("fixtures/valid/mp3-cbr-stereo.mp3")));
    writeMp3Artwork(source.path(), encodedImage(2'048, 1'024, "PNG"));

    const auto result = TagLibMetadataExtractor{}.read(source.path());
    const auto& metadata = requireMetadata(result);

    REQUIRE(metadata.artwork.has_value());
    CHECK(metadata.artwork->width_pixels == 1'024);
    CHECK(metadata.artwork->height_pixels == 512);
    REQUIRE(metadata.artwork->rgba8888 != nullptr);
    CHECK(metadata.artwork->rgba8888->size() == 1'024U * 512U * 4U);
    CHECK(metadata.artwork_diagnostics.empty());
    REQUIRE(metadata.audio.duration_ms.has_value());
    CHECK(*metadata.audio.duration_ms > 0);
}

TEST_CASE("MR-008 rejects embedded MP3 artwork above the encoded byte limit before image decoding",
          "[m0][qt][metadata][taglib][artwork][limits][MR-008]") {
    constexpr std::size_t encoded_byte_limit = 10U * 1'024U * 1'024U;
    const ScopedTemporaryDirectory directory;
    const ScopedTemporaryFile source{temporaryPath(directory, "oversized-cover.mp3")};
    writeBytes(source.path(), readBytes(fixture("fixtures/valid/mp3-cbr-stereo.mp3")));
    writeMp3Artwork(source.path(), std::vector<std::uint8_t>(encoded_byte_limit + 1U, 0x5a));

    const auto result = TagLibMetadataExtractor{}.read(source.path());
    const auto& metadata = requireMetadata(result);

    CHECK_FALSE(metadata.artwork.has_value());
    REQUIRE(metadata.artwork_diagnostics.size() == 1);
    CHECK(metadata.artwork_diagnostics.front().code == ArtworkDiagnosticCode::EncodedBytesExceeded);
    CHECK(metadata.artwork_diagnostics.front().encoded_bytes == encoded_byte_limit + 1U);
    CHECK(metadata.artwork_diagnostics.front().width_pixels == 0);
    CHECK(metadata.artwork_diagnostics.front().height_pixels == 0);
    REQUIRE(metadata.audio.duration_ms.has_value());
    CHECK(*metadata.audio.duration_ms > 0);
}

TEST_CASE("MR-006 MR-007 MR-009 and MR-010 decode canonical embedded artwork below and at every limit",
          "[m0][qt][metadata][taglib][artwork][limits][MR-006][MR-007][MR-009][MR-010]") {
    struct AcceptedArtworkCase final {
        const char* filename;
        std::uint32_t thumbnail_width;
        std::uint32_t thumbnail_height;
    };
    const TagLibMetadataExtractor reader;
    const auto source_result = reader.read(fixture("fixtures/valid/mp3-cbr-stereo.mp3"));
    const auto& source_metadata = requireMetadata(source_result);
    for (const auto& artwork_case : {
             AcceptedArtworkCase{"mp3-png-small.mp3", 1, 1},
             AcceptedArtworkCase{"mp3-png-bytes-below.mp3", 1, 1},
             AcceptedArtworkCase{"mp3-png-bytes-at.mp3", 1, 1},
             AcceptedArtworkCase{"mp3-png-width-below.mp3", 1'024, 1},
             AcceptedArtworkCase{"mp3-png-width-at.mp3", 1'024, 1},
             AcceptedArtworkCase{"mp3-png-height-below.mp3", 1, 1'024},
             AcceptedArtworkCase{"mp3-png-height-at.mp3", 1, 1'024},
             AcceptedArtworkCase{"mp3-png-pixels-below.mp3", 1'023, 1'024},
             AcceptedArtworkCase{"mp3-png-pixels-at.mp3", 1'024, 1'024},
         }) {
        DYNAMIC_SECTION(artwork_case.filename) {
            CAPTURE(artwork_case.filename);
            std::optional<MetadataReadResult> result;
            REQUIRE_NOTHROW(result.emplace(
                reader.read(productFixture(std::filesystem::path{"fixtures/artwork"} / artwork_case.filename))));
            const auto& metadata = requireMetadata(*result);
            REQUIRE(metadata.artwork.has_value());
            CHECK(metadata.artwork->width_pixels == artwork_case.thumbnail_width);
            CHECK(metadata.artwork->height_pixels == artwork_case.thumbnail_height);
            REQUIRE(metadata.artwork->rgba8888 != nullptr);
            CHECK(metadata.artwork->rgba8888->size() ==
                  static_cast<std::size_t>(artwork_case.thumbnail_width) * artwork_case.thumbnail_height * 4U);
            CHECK(metadata.artwork_diagnostics.empty());
            CHECK(metadata.audio.duration_ms == source_metadata.audio.duration_ms);
            CHECK(metadata.audio.sample_rate_hz == source_metadata.audio.sample_rate_hz);
            CHECK(metadata.audio.channels == source_metadata.audio.channels);
            CHECK(metadata.tags.title == std::optional<std::string>{"Canção determinística 日本語 🎵"});
        }
    }
}

TEST_CASE("Canonical embedded artwork above each limit is rejected while metadata remains readable",
          "[m0][qt][metadata][taglib][artwork][limits][MR-005][MR-008][MR-011]") {
    struct RejectedArtworkCase final {
        const char* filename;
        ArtworkDiagnosticCode diagnostic;
        std::uint32_t width;
        std::uint32_t height;
    };
    const TagLibMetadataExtractor reader;
    const auto source_result = reader.read(fixture("fixtures/valid/mp3-cbr-stereo.mp3"));
    const auto& source_metadata = requireMetadata(source_result);
    for (const auto& artwork_case : {
             RejectedArtworkCase{"mp3-png-bytes-above.mp3", ArtworkDiagnosticCode::EncodedBytesExceeded, 0, 0},
             RejectedArtworkCase{"mp3-png-width-above.mp3", ArtworkDiagnosticCode::AxisPixelsExceeded, 4'097, 1},
             RejectedArtworkCase{"mp3-png-height-above.mp3", ArtworkDiagnosticCode::AxisPixelsExceeded, 1, 4'097},
             RejectedArtworkCase{"mp3-png-pixels-above.mp3", ArtworkDiagnosticCode::PixelCountExceeded, 4'001, 4'000},
             RejectedArtworkCase{"mp3-invalid-art.mp3", ArtworkDiagnosticCode::InvalidData, 0, 0},
         }) {
        DYNAMIC_SECTION(artwork_case.filename) {
            CAPTURE(artwork_case.filename);
            std::optional<MetadataReadResult> result;
            REQUIRE_NOTHROW(result.emplace(
                reader.read(productFixture(std::filesystem::path{"fixtures/artwork"} / artwork_case.filename))));
            const auto& metadata = requireMetadata(*result);
            CHECK_FALSE(metadata.artwork.has_value());
            REQUIRE(metadata.artwork_diagnostics.size() == 1);
            CHECK(metadata.artwork_diagnostics.front().code == artwork_case.diagnostic);
            CHECK(metadata.artwork_diagnostics.front().width_pixels == artwork_case.width);
            CHECK(metadata.artwork_diagnostics.front().height_pixels == artwork_case.height);
            CHECK(metadata.audio.duration_ms == source_metadata.audio.duration_ms);
            CHECK(metadata.audio.sample_rate_hz == source_metadata.audio.sample_rate_hz);
            CHECK(metadata.audio.channels == source_metadata.audio.channels);
            CHECK(metadata.tags.title == std::optional<std::string>{"Canção determinística 日本語 🎵"});
        }
    }
}

TEST_CASE("MR-011 rejects real embedded MP3 artwork beyond dimension and pixel boundaries",
          "[m0][qt][metadata][taglib][artwork][limits][MR-011]") {
    struct BoundaryCase final {
        std::filesystem::path filename;
        std::uint32_t width_pixels;
        std::uint32_t height_pixels;
        ArtworkDiagnosticCode diagnostic;
    };

    const ScopedTemporaryDirectory directory;
    for (const auto& boundary_case : {
             BoundaryCase{"axis-limit-cover.mp3", 4'097, 1, ArtworkDiagnosticCode::AxisPixelsExceeded},
             BoundaryCase{"pixel-limit-cover.mp3", 4'001, 4'000, ArtworkDiagnosticCode::PixelCountExceeded},
         }) {
        CAPTURE(boundary_case.filename.string(), boundary_case.width_pixels, boundary_case.height_pixels);
        const ScopedTemporaryFile source{temporaryPath(directory, boundary_case.filename)};
        writeBytes(source.path(), readBytes(fixture("fixtures/valid/mp3-cbr-stereo.mp3")));
        const auto encoded_artwork = encodedImage(boundary_case.width_pixels, boundary_case.height_pixels, "PNG");
        writeMp3Artwork(source.path(), encoded_artwork);

        const auto result = TagLibMetadataExtractor{}.read(source.path());
        const auto& metadata = requireMetadata(result);

        CHECK_FALSE(metadata.artwork.has_value());
        REQUIRE(metadata.artwork_diagnostics.size() == 1);
        CHECK(metadata.artwork_diagnostics.front().code == boundary_case.diagnostic);
        CHECK(metadata.artwork_diagnostics.front().encoded_bytes == encoded_artwork.size());
        CHECK(metadata.artwork_diagnostics.front().width_pixels == boundary_case.width_pixels);
        CHECK(metadata.artwork_diagnostics.front().height_pixels == boundary_case.height_pixels);
        REQUIRE(metadata.audio.duration_ms.has_value());
        CHECK(*metadata.audio.duration_ms > 0);
    }
}

TEST_CASE("NFR-LIMIT-001 discards an embedded image format outside the artwork allowlist without failing metadata",
          "[m0][qt][metadata][taglib][artwork][limits][NFR-LIMIT-001]") {
    const ScopedTemporaryDirectory directory;
    const ScopedTemporaryFile source{temporaryPath(directory, "bmp-cover.mp3")};
    writeBytes(source.path(), readBytes(fixture("fixtures/valid/mp3-cbr-stereo.mp3")));
    writeMp3Artwork(source.path(), encodedImage(2, 2, "BMP"), "image/bmp");

    const auto result = TagLibMetadataExtractor{}.read(source.path());
    const auto& metadata = requireMetadata(result);

    CHECK_FALSE(metadata.artwork.has_value());
    REQUIRE(metadata.artwork_diagnostics.size() == 1);
    REQUIRE(metadata.audio.duration_ms.has_value());
    CHECK(*metadata.audio.duration_ms > 0);
}

TEST_CASE("MR-012 preserves one metadata field exactly at the retained byte limit",
          "[m0][qt][metadata][taglib][limits][MR-012]") {
    constexpr std::size_t field_limit_bytes = MetadataRetentionBudget::field_limit_bytes;
    const auto title = utf8TextFieldWithSize(field_limit_bytes);

    const auto result = TagLibMetadataExtractor{}.read(productFixture("fixtures/tags/mp3-title-at.mp3"));
    const auto& metadata = requireMetadata(result);

    REQUIRE(metadata.tags.title.has_value());
    CHECK(metadata.tags.title->size() == title.size());
    CHECK(hasSameText(*metadata.tags.title, title));
    CHECK(metadata.display_title.size() == title.size());
    CHECK(hasSameText(metadata.display_title, title));
    CHECK(metadata.diagnostics.empty());
    REQUIRE(metadata.audio.duration_ms.has_value());
    CHECK(*metadata.audio.duration_ms > 0);
}

TEST_CASE("MR-013 discards one metadata field above the retained byte limit without failing the read",
          "[m0][qt][metadata][taglib][limits][MR-013]") {
    constexpr std::size_t field_limit_bytes = MetadataRetentionBudget::field_limit_bytes;

    const auto result = TagLibMetadataExtractor{}.read(productFixture("fixtures/tags/mp3-title-above.mp3"));
    const auto& metadata = requireMetadata(result);

    CHECK_FALSE(metadata.tags.title.has_value());
    CHECK(metadata.display_title == "mp3-title-above");
    REQUIRE(metadata.diagnostics.size() == 1);
    CHECK(metadata.diagnostics.front().code == MetadataDiagnosticCode::FieldTooLarge);
    CHECK(metadata.diagnostics.front().field == MetadataField::Title);
    CHECK(metadata.diagnostics.front().observed_bytes == field_limit_bytes + 1);
    CHECK(metadata.diagnostics.front().limit_bytes == field_limit_bytes);
    REQUIRE(metadata.audio.duration_ms.has_value());
    CHECK(*metadata.audio.duration_ms > 0);
}

TEST_CASE("NFR-LIMIT-002 enforces canonical UTF-8 text boundaries independently for every field",
          "[m0][qt][metadata][taglib][limits][NFR-LIMIT-002]") {
    struct TextCase final {
        const char* filename;
        MetadataField field;
        std::size_t bytes;
    };
    const TagLibMetadataExtractor reader;
    const auto source_result = reader.read(fixture("fixtures/valid/mp3-cbr-stereo.mp3"));
    const auto& original = requireMetadata(source_result);
    for (const auto& text_case : {
             TextCase{"mp3-title-below.mp3", MetadataField::Title, 16'383},
             TextCase{"mp3-artist-below.mp3", MetadataField::Artist, 16'383},
             TextCase{"mp3-artist-at.mp3", MetadataField::Artist, 16'384},
             TextCase{"mp3-artist-above.mp3", MetadataField::Artist, 16'385},
             TextCase{"mp3-album-below.mp3", MetadataField::Album, 16'383},
             TextCase{"mp3-album-at.mp3", MetadataField::Album, 16'384},
             TextCase{"mp3-album-above.mp3", MetadataField::Album, 16'385},
         }) {
        DYNAMIC_SECTION(text_case.filename) {
            const auto result =
                reader.read(productFixture(std::filesystem::path{"fixtures/tags"} / text_case.filename));
            const auto& metadata = requireMetadata(result);
            const auto& field = text_case.field == MetadataField::Title    ? metadata.tags.title
                                : text_case.field == MetadataField::Artist ? metadata.tags.artist
                                                                           : metadata.tags.album;
            if (text_case.bytes <= 16'384) {
                REQUIRE(field.has_value());
                CHECK(*field == utf8TextFieldWithSize(text_case.bytes));
                CHECK(metadata.diagnostics.empty());
            } else {
                CHECK_FALSE(field.has_value());
                REQUIRE(metadata.diagnostics.size() == 1);
                CHECK(metadata.diagnostics.front().code == MetadataDiagnosticCode::FieldTooLarge);
                CHECK(metadata.diagnostics.front().field == text_case.field);
                CHECK(metadata.diagnostics.front().observed_bytes == text_case.bytes);
                CHECK(metadata.diagnostics.front().limit_bytes == 16'384);
            }
            if (text_case.field != MetadataField::Title) {
                CHECK(metadata.tags.title == original.tags.title);
                CHECK(metadata.display_title == original.display_title);
            }
            if (text_case.field != MetadataField::Artist) {
                CHECK(metadata.tags.artist == original.tags.artist);
            }
            if (text_case.field != MetadataField::Album) {
                CHECK(metadata.tags.album == original.tags.album);
            }
            CHECK(metadata.audio.duration_ms == original.audio.duration_ms);
            CHECK(metadata.audio.sample_rate_hz == original.audio.sample_rate_hz);
            CHECK(metadata.audio.channels == original.audio.channels);
        }
    }
}

TEST_CASE("NFR-LIMIT-002 retains the largest aggregate reachable through the supported metadata fields",
          "[m0][qt][metadata][taglib][limits][NFR-LIMIT-002]") {
    const auto result = TagLibMetadataExtractor{}.read(productFixture("fixtures/tags/mp3-all-fields-at.mp3"));
    const auto& metadata = requireMetadata(result);
    const auto text = utf8TextFieldWithSize(16'384);
    CHECK(metadata.tags.title == std::optional<std::string>{text});
    CHECK(metadata.tags.artist == std::optional<std::string>{text});
    CHECK(metadata.tags.album == std::optional<std::string>{text});
    CHECK(metadata.tags.track_number == std::optional<std::uint32_t>{1});
    CHECK(metadata.tags.year == std::optional<std::uint32_t>{2026});
    CHECK(metadata.display_title == text);
    CHECK(metadata.diagnostics.empty());
}

TEST_CASE("FR-023 sanitizes display text while preserving Unicode and multi-value joins",
          "[m0][qt][metadata][taglib][FR-023][sanitization]") {
    const ScopedTemporaryDirectory directory;
    const ScopedTemporaryFile source{temporaryPath(directory, "sanitized.mp3")};
    writeBytes(source.path(), readBytes(fixture("fixtures/valid/mp3-cbr-stereo.mp3")));
    TagLib::PropertyMap values;
    values.insert("TITLE", TagLib::StringList{TagLib::String{" \tCanção\n日本語 🎵\r\n ", TagLib::String::UTF8}});
    values.insert("ARTIST", TagLib::StringList{TagLib::String{"João", TagLib::String::UTF8},
                                               TagLib::String{"Ana", TagLib::String::UTF8}});
    values.insert("ALBUM", TagLib::StringList{TagLib::String{"Álbum\xE2\x80\xAE seguro", TagLib::String::UTF8}});
    writeMp3Properties(source.path(), values);

    const auto result = TagLibMetadataExtractor{}.read(source.path());
    const auto& metadata = requireMetadata(result);
    CHECK(metadata.tags.title == std::optional<std::string>{"Canção 日本語 🎵"});
    CHECK(metadata.display_title == "Canção 日本語 🎵");
    CHECK(metadata.tags.artist == std::optional<std::string>{"João / Ana"});
    CHECK(metadata.tags.album == std::optional<std::string>{"Álbum seguro"});
    CHECK(diagnosticCount(metadata, MetadataDiagnosticCode::TextSanitized, MetadataField::Title) == 1);
    CHECK(diagnosticCount(metadata, MetadataDiagnosticCode::TextSanitized, MetadataField::Album) == 1);
    for (const auto& diagnostic : metadata.diagnostics) {
        CHECK(diagnostic.observed_bytes == 0);
        CHECK(diagnostic.limit_bytes == 0);
    }
    CHECK(metadata.audio.duration_ms.value_or(0) > 0);
}

TEST_CASE("FR-023 discards semantically empty text and keeps the filename fallback",
          "[m0][qt][metadata][taglib][FR-023][sanitization]") {
    const ScopedTemporaryDirectory directory;
    const ScopedTemporaryFile source{temporaryPath(directory, "empty-display.mp3")};
    writeBytes(source.path(), readBytes(fixture("fixtures/valid/mp3-cbr-stereo.mp3")));
    writeMp3Title(source.path(), " \t\r\n\xE2\x80\xAE\xE2\x80\x8B");
    const auto result = TagLibMetadataExtractor{}.read(source.path());
    const auto& metadata = requireMetadata(result);
    CHECK_FALSE(metadata.tags.title.has_value());
    CHECK(metadata.display_title == "empty-display");
    CHECK(diagnosticCount(metadata, MetadataDiagnosticCode::InvalidValue, MetadataField::Title) == 1);
    CHECK(metadata.audio.duration_ms.value_or(0) > 0);
}

TEST_CASE("FR-023 applies the original text byte limit before whitespace normalization",
          "[m0][qt][metadata][taglib][FR-023][sanitization]") {
    const ScopedTemporaryDirectory directory;
    const ScopedTemporaryFile source{temporaryPath(directory, "original-limit.mp3")};
    writeBytes(source.path(), readBytes(fixture("fixtures/valid/mp3-cbr-stereo.mp3")));
    writeMp3Title(source.path(), std::string(MetadataRetentionBudget::field_limit_bytes, ' ') + "X");
    const auto result = TagLibMetadataExtractor{}.read(source.path());
    const auto& metadata = requireMetadata(result);
    CHECK_FALSE(metadata.tags.title.has_value());
    CHECK(diagnosticCount(metadata, MetadataDiagnosticCode::FieldTooLarge, MetadataField::Title) == 1);
    CHECK(diagnosticCount(metadata, MetadataDiagnosticCode::TextSanitized, MetadataField::Title) == 0);
}

TEST_CASE("FR-023 validates original ID3 numeric text instead of wrapped or partial getters",
          "[m0][qt][metadata][taglib][FR-023][sanitization]") {
    const ScopedTemporaryDirectory directory;
    for (const auto& invalid : {"-1", "+7", "7junk", "4294967296", "999999999999999999999999", "7/0", "7/-9"}) {
        CAPTURE(invalid);
        const ScopedTemporaryFile source{temporaryPath(directory, "invalid-numbers.mp3")};
        writeBytes(source.path(), readBytes(fixture("fixtures/valid/mp3-cbr-stereo.mp3")));
        writeMp3Properties(source.path(), numericProperties(invalid, "2026-02-30"));
        const auto result = TagLibMetadataExtractor{}.read(source.path());
        const auto& metadata = requireMetadata(result);
        CHECK_FALSE(metadata.tags.track_number.has_value());
        CHECK_FALSE(metadata.tags.year.has_value());
        CHECK(diagnosticCount(metadata, MetadataDiagnosticCode::InvalidValue, MetadataField::TrackNumber) == 1);
        CHECK(diagnosticCount(metadata, MetadataDiagnosticCode::InvalidValue, MetadataField::Year) == 1);
        CHECK(metadata.audio.duration_ms.value_or(0) > 0);
    }
}

TEST_CASE("FR-023 preserves positive uint32 track numbers and calendar date precision in ID3",
          "[m0][qt][metadata][taglib][FR-023][sanitization]") {
    const ScopedTemporaryDirectory directory;
    for (const auto& date :
         {"2024", "2024-02", "2024-02-29", "2024-02-29T12", "2024-02-29T12:34", "2024-02-29T12:34:56.123-03:00"}) {
        CAPTURE(date);
        const ScopedTemporaryFile source{temporaryPath(directory, "valid-numbers.mp3")};
        writeBytes(source.path(), readBytes(fixture("fixtures/valid/mp3-cbr-stereo.mp3")));
        writeMp3Properties(source.path(), numericProperties("4294967295/4294967295", date));
        const auto result = TagLibMetadataExtractor{}.read(source.path());
        const auto& metadata = requireMetadata(result);
        CHECK(metadata.tags.track_number == std::optional<std::uint32_t>{4'294'967'295U});
        CHECK(metadata.tags.year == std::optional<std::uint32_t>{2024});
        CHECK(metadata.diagnostics.empty());
    }
}

TEST_CASE("FR-023 validates Vorbis numbers and preserves valid alias fallback per field",
          "[m0][qt][metadata][taglib][FR-023][sanitization]") {
    const ScopedTemporaryDirectory directory;
    for (const bool flac : {false, true}) {
        CAPTURE(flac);
        const ScopedTemporaryFile source{temporaryPath(directory, flac ? "numeric.flac" : "numeric.ogg")};
        writeBytes(source.path(),
                   readBytes(fixture(flac ? "fixtures/valid/flac-16-stereo.flac" : "fixtures/valid/ogg-vorbis.ogg")));
        auto values = numericProperties("-1", "2026-invalid");
        values.insert("TRACKNUM", TagLib::StringList{"7/12"});
        values.insert("YEAR", TagLib::StringList{"2024-02-29T12:34Z"});
        if (flac) {
            writeProperties<TagLib::FLAC::File>(source.path(), values);
        } else {
            writeProperties<TagLib::Vorbis::File>(source.path(), values);
        }
        const auto result = TagLibMetadataExtractor{}.read(source.path());
        const auto& metadata = requireMetadata(result);
        CHECK(metadata.tags.track_number == std::optional<std::uint32_t>{7});
        CHECK(metadata.tags.year == std::optional<std::uint32_t>{2024});
        CHECK(diagnosticCount(metadata, MetadataDiagnosticCode::InvalidValue, MetadataField::TrackNumber) == 1);
        CHECK(diagnosticCount(metadata, MetadataDiagnosticCode::InvalidValue, MetadataField::Year) == 1);
    }
}

TEST_CASE("FR-023 preserves MP4 track pairs and rejects invalid original date text",
          "[m0][qt][metadata][taglib][FR-023][sanitization]") {
    const ScopedTemporaryDirectory directory;
    for (const bool valid : {false, true}) {
        CAPTURE(valid);
        const ScopedTemporaryFile source{temporaryPath(directory, "numeric.m4a")};
        writeBytes(source.path(), readBytes(fixture("fixtures/valid/m4a-aac-lc.m4a")));
        writeProperties<TagLib::MP4::File>(source.path(), numericProperties("7/12", valid ? "2024-02-29" : "2024oops"));
        const auto result = TagLibMetadataExtractor{}.read(source.path());
        const auto& metadata = requireMetadata(result);
        CHECK(metadata.tags.track_number == std::optional<std::uint32_t>{7});
        CHECK(metadata.tags.year == (valid ? std::optional<std::uint32_t>{2024} : std::nullopt));
        CHECK(diagnosticCount(metadata, MetadataDiagnosticCode::InvalidValue, MetadataField::Year) == (valid ? 0 : 1));
    }
}

TEST_CASE("FR-023 preserves MPEG subtag fallback independently and deduplicates invalid numeric diagnostics",
          "[m0][qt][metadata][taglib][FR-023][sanitization]") {
    const ScopedTemporaryDirectory directory;
    const ScopedTemporaryFile source{temporaryPath(directory, "mixed-tags.mp3")};
    writeBytes(source.path(), readBytes(fixture("fixtures/valid/mp3-cbr-stereo.mp3")));
    {
        TagLib::MPEG::File file{source.path().c_str()};
        REQUIRE(file.isValid());
        auto values = numericProperties("-1", "2024bad");
        values.insert("TITLE", TagLib::StringList{"Primary title"});
        REQUIRE(file.ID3v2Tag(true)->setProperties(values).isEmpty());
        REQUIRE(file.APETag(true)->setProperties(numericProperties("7/12", "-2")).isEmpty());
        file.ID3v1Tag(true)->setYear(1998);
        file.ID3v1Tag(true)->setTrack(9);
        file.ID3v1Tag(true)->setArtist("Legacy artist");
        REQUIRE(file.save(TagLib::MPEG::File::AllTags, TagLib::MPEG::File::StripNone, TagLib::ID3v2::v4,
                          TagLib::MPEG::File::DoNotDuplicate));
    }
    const auto result = TagLibMetadataExtractor{}.read(source.path());
    const auto& metadata = requireMetadata(result);
    CHECK(metadata.tags.title == std::optional<std::string>{"Primary title"});
    CHECK(metadata.tags.artist == std::optional<std::string>{"Legacy artist"});
    CHECK(metadata.tags.track_number == std::optional<std::uint32_t>{7});
    CHECK(metadata.tags.year == std::optional<std::uint32_t>{1998});
    CHECK(diagnosticCount(metadata, MetadataDiagnosticCode::InvalidValue, MetadataField::TrackNumber) == 1);
    CHECK(diagnosticCount(metadata, MetadataDiagnosticCode::InvalidValue, MetadataField::Year) == 1);
}

TEST_CASE("FR-023 preserves the unsigned MP4 binary track pair across the TagLib signed projection",
          "[m0][qt][metadata][taglib][FR-023][sanitization]") {
    const ScopedTemporaryDirectory directory;
    for (const int track : {32'767, 32'768, 65'535}) {
        CAPTURE(track);
        const ScopedTemporaryFile source{temporaryPath(directory, "unsigned-track.m4a")};
        writeBytes(source.path(), readBytes(fixture("fixtures/valid/m4a-aac-lc.m4a")));
        {
            TagLib::MP4::File file{source.path().c_str()};
            REQUIRE(file.isValid());
            file.tag()->setItem("trkn", TagLib::MP4::Item{track, 65'535});
            REQUIRE(file.save());
        }
        {
            TagLib::MP4::File reopened{source.path().c_str()};
            REQUIRE(reopened.isValid());
            const auto pair = reopened.tag()->item("trkn").toIntPair();
            // TagLib 2.3.1 reads the unsigned 16-bit atom through toShort().
            REQUIRE(static_cast<std::uint16_t>(pair.first) == static_cast<std::uint16_t>(track));
            REQUIRE(static_cast<std::uint16_t>(pair.second) == 65'535);
        }
        const auto result = TagLibMetadataExtractor{}.read(source.path());
        const auto& metadata = requireMetadata(result);
        CHECK(metadata.tags.track_number == std::optional<std::uint32_t>{static_cast<std::uint32_t>(track)});
        CHECK(diagnosticCount(metadata, MetadataDiagnosticCode::InvalidValue, MetadataField::TrackNumber) == 0);
    }
}

TEST_CASE("FR-023 rejects malformed legacy ID3v1 years before unsigned conversion",
          "[m0][qt][metadata][taglib][FR-023][sanitization]") {
    const ScopedTemporaryDirectory directory;
    for (const std::string raw_year : {"-1  ", "20xx", "1998"}) {
        CAPTURE(raw_year);
        const ScopedTemporaryFile source{temporaryPath(directory, "legacy.mp3")};
        auto bytes = withoutId3Tags(readBytes(fixture("fixtures/valid/mp3-cbr-stereo.mp3")));
        std::vector<std::uint8_t> tag(128, 0);
        tag[0] = 'T';
        tag[1] = 'A';
        tag[2] = 'G';
        std::copy(raw_year.begin(), raw_year.end(), tag.begin() + 93);
        tag[126] = 7;
        bytes.insert(bytes.end(), tag.begin(), tag.end());
        writeBytes(source.path(), bytes);
        const auto result = TagLibMetadataExtractor{}.read(source.path());
        const auto& metadata = requireMetadata(result);
        CHECK(metadata.tags.track_number == std::optional<std::uint32_t>{7});
        CHECK(metadata.tags.year == (raw_year == "1998" ? std::optional<std::uint32_t>{1998} : std::nullopt));
        CHECK(diagnosticCount(metadata, MetadataDiagnosticCode::InvalidValue, MetadataField::Year) ==
              (raw_year == "1998" ? 0 : 1));
    }
}

TEST_CASE("MR-101 composes the asynchronous reader with the real TagLib extractor",
          "[m0][qt][metadata][taglib][metadata-reader][MR-101]") {
    const auto source = fixture("fixtures/valid/wav-pcm-mono.wav");
    const auto caller_thread = std::this_thread::get_id();
    std::atomic_bool modification_time_read_on_worker{false};
    auto extractor = std::make_unique<TagLibMetadataExtractor>(TagLibMetadataExtractorOptions{
        .last_write_time =
            [&](const std::filesystem::path& path, std::error_code& error) {
                modification_time_read_on_worker.store(std::this_thread::get_id() != caller_thread,
                                                       std::memory_order_relaxed);
                return std::filesystem::last_write_time(path, error);
            },
    });
    SingleWorkerMetadataReader reader{std::move(extractor)};

    REQUIRE(reader.startRead({MetadataGeneration{1}, source}) == MetadataCommandResult::Accepted);
    const auto completion = waitForCompletion(reader);

    REQUIRE(completion.has_value());
    CHECK(completion->generation == MetadataGeneration{1});
    CHECK(completion->source_path == source);
    const auto& metadata = requireMetadata(completion->result);
    CHECK(modification_time_read_on_worker.load(std::memory_order_relaxed));
    CHECK(metadata.last_modified_time.has_value());
    CHECK(metadata.display_title == "wav-pcm-mono");
    REQUIRE(metadata.audio.duration_ms.has_value());
    CHECK(*metadata.audio.duration_ms == 1'000);
}

} // namespace music_player::m0::test
