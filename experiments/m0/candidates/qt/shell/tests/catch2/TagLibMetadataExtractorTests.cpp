#include "m0/adapters/SingleWorkerMetadataReader.hpp"
#include "m0/adapters/TagLibMetadataExtractor.hpp"

#include <catch2/catch_test_macros.hpp>

#include <id3v2tag.h>
#include <mpegfile.h>
#include <tag.h>

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
#include <thread>
#include <variant>
#include <vector>

#ifndef M0_TEST_AUDIO_CORPUS_ROOT
#error "M0_TEST_AUDIO_CORPUS_ROOT must identify the authenticated M0 audio corpus"
#endif

namespace music_player::m0::test {
namespace {

using namespace std::chrono_literals;

const std::filesystem::path corpus_root{M0_TEST_AUDIO_CORPUS_ROOT};

[[nodiscard]] std::filesystem::path fixture(const std::filesystem::path& relative_path) {
    return corpus_root / relative_path;
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
        std::filesystem::path temporary_filename;
    };

    constexpr std::size_t truncated_size = 64;
    const ScopedTemporaryDirectory directory;
    const TagLibMetadataExtractor reader;

    for (const auto& codec_case : {
             TruncatedCodecCase{"fixtures/valid/mp3-cbr-stereo.mp3", "truncated-mp3-cbr.mp3"},
             TruncatedCodecCase{"fixtures/valid/m4a-aac-lc.m4a", "truncated-m4a-aac.m4a"},
             TruncatedCodecCase{"fixtures/valid/flac-16-stereo.flac", "truncated-flac.flac"},
             TruncatedCodecCase{"fixtures/valid/ogg-vorbis.ogg", "truncated-ogg-vorbis.ogg"},
         }) {
        CAPTURE(codec_case.relative_path.string());
        const auto authenticated = readBytes(fixture(codec_case.relative_path));
        REQUIRE(authenticated.size() > truncated_size);

        const ScopedTemporaryFile truncated{temporaryPath(directory, codec_case.temporary_filename)};
        writeBytes(truncated.path(),
                   std::vector<std::uint8_t>{authenticated.begin(), authenticated.begin() + truncated_size});

        std::optional<MetadataReadResult> result;
        REQUIRE_NOTHROW(result.emplace(reader.read(truncated.path())));
        REQUIRE(result.has_value());
        const auto& read_error = requireError(*result);
        CHECK(read_error.code == MetadataReadErrorCode::InvalidData);
        CHECK_FALSE(read_error.message.empty());
    }
}

TEST_CASE("MR-012 preserves one metadata field exactly at the retained byte limit",
          "[m0][qt][metadata][taglib][limits][MR-012]") {
    constexpr std::size_t field_limit_bytes = 16U * 1024U;
    const ScopedTemporaryDirectory directory;
    const ScopedTemporaryFile source{temporaryPath(directory, "exact-field-limit.mp3")};
    const auto title = utf8TextFieldWithSize(field_limit_bytes);
    writeBytes(source.path(), readBytes(fixture("fixtures/valid/mp3-cbr-stereo.mp3")));
    writeMp3Title(source.path(), title);

    const auto result = TagLibMetadataExtractor{}.read(source.path());
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
    constexpr std::size_t field_limit_bytes = 16U * 1024U;
    const ScopedTemporaryDirectory directory;
    const ScopedTemporaryFile source{temporaryPath(directory, "above-field-limit.mp3")};
    writeBytes(source.path(), readBytes(fixture("fixtures/valid/mp3-cbr-stereo.mp3")));
    writeMp3Title(source.path(), utf8TextFieldWithSize(field_limit_bytes + 1));

    const auto result = TagLibMetadataExtractor{}.read(source.path());
    const auto& metadata = requireMetadata(result);

    CHECK_FALSE(metadata.tags.title.has_value());
    CHECK(metadata.display_title == "above-field-limit");
    REQUIRE(metadata.diagnostics.size() == 1);
    CHECK(metadata.diagnostics.front().code == MetadataDiagnosticCode::FieldTooLarge);
    CHECK(metadata.diagnostics.front().field == MetadataField::Title);
    CHECK(metadata.diagnostics.front().observed_bytes == field_limit_bytes + 1);
    CHECK(metadata.diagnostics.front().limit_bytes == field_limit_bytes);
    REQUIRE(metadata.audio.duration_ms.has_value());
    CHECK(*metadata.audio.duration_ms > 0);
}

TEST_CASE("MR-101 composes the asynchronous reader with the real TagLib extractor",
          "[m0][qt][metadata][taglib][metadata-reader][MR-101]") {
    const auto source = fixture("fixtures/valid/wav-pcm-mono.wav");
    SingleWorkerMetadataReader reader{std::make_unique<TagLibMetadataExtractor>()};

    REQUIRE(reader.startRead({MetadataGeneration{1}, source}) == MetadataCommandResult::Accepted);
    const auto completion = waitForCompletion(reader);

    REQUIRE(completion.has_value());
    CHECK(completion->generation == MetadataGeneration{1});
    CHECK(completion->source_path == source);
    const auto& metadata = requireMetadata(completion->result);
    CHECK(metadata.display_title == "wav-pcm-mono");
    REQUIRE(metadata.audio.duration_ms.has_value());
    CHECK(*metadata.audio.duration_ms == 1'000);
}

} // namespace music_player::m0::test
