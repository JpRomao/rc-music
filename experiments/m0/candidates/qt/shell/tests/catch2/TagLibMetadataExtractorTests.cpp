#include "m0/adapters/TagLibMetadataExtractor.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <variant>

#ifndef M0_TEST_AUDIO_CORPUS_ROOT
#error "M0_TEST_AUDIO_CORPUS_ROOT must identify the authenticated M0 audio corpus"
#endif

namespace music_player::m0::test {
namespace {

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

} // namespace music_player::m0::test
