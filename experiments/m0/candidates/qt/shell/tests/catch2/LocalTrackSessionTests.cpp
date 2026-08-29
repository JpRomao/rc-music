#include "m0/application/LocalTrackSession.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace music_player::m0::test {
namespace {

class RecordingPlaybackBackend final : public PlaybackBackend {
  public:
    [[nodiscard]] const PlaybackSnapshot& snapshot() const noexcept override { return snapshot_; }
    void setEventCallback(PlaybackEventCallback callback) override { callback_ = std::move(callback); }

    PlaybackCommandResult load(const std::filesystem::path& source_path) override {
        loaded_paths.push_back(source_path);
        if (load_result != PlaybackCommandResult::Accepted) {
            return load_result;
        }
        ++snapshot_.generation;
        snapshot_ = {PlaybackStatus::Loading, 0, 0, false, snapshot_.generation};
        return PlaybackCommandResult::Accepted;
    }

    PlaybackCommandResult play() override {
        ++play_calls;
        if (play_result == PlaybackCommandResult::Accepted) {
            snapshot_.status = PlaybackStatus::Playing;
        }
        return play_result;
    }

    PlaybackCommandResult pause() override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult seekTo(std::int64_t) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult setVolume(double) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult setMuted(bool) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult unload() override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult shutdown() override { return PlaybackCommandResult::Accepted; }

    PlaybackSnapshot snapshot_{};
    PlaybackEventCallback callback_{};
    PlaybackCommandResult load_result{PlaybackCommandResult::Accepted};
    PlaybackCommandResult play_result{PlaybackCommandResult::Accepted};
    std::vector<std::filesystem::path> loaded_paths{};
    int play_calls{0};
};

class ScriptedMetadataReader final : public MetadataReader {
  public:
    MetadataCommandResult startRead(MetadataReadRequest request) override {
        requests.push_back(std::move(request));
        return start_result;
    }

    MetadataCommandResult cancelRead(const MetadataGeneration generation) override {
        cancellations.push_back(generation);
        return cancel_result;
    }

    [[nodiscard]] MetadataPollResult poll(const MetadataGeneration generation) override {
        polls.push_back(generation);
        return next_poll;
    }

    [[nodiscard]] std::optional<MetadataReadCompletion> tryTakeCompletion() override { return std::nullopt; }
    MetadataCommandResult shutdown() override { return MetadataCommandResult::Accepted; }

    MetadataCommandResult start_result{MetadataCommandResult::Accepted};
    MetadataCommandResult cancel_result{MetadataCommandResult::Accepted};
    MetadataPollResult next_poll{MetadataPollStatus::Pending, std::nullopt};
    std::vector<MetadataReadRequest> requests{};
    std::vector<MetadataGeneration> cancellations{};
    std::vector<MetadataGeneration> polls{};
};

[[nodiscard]] TrackMetadata taggedMetadata(const std::filesystem::path& path) {
    TrackMetadata metadata{};
    metadata.source_path = path;
    metadata.display_title = "Embedded title";
    metadata.tags.artist = "Embedded artist";
    metadata.tags.album = "Embedded album";
    metadata.audio.duration_ms = 2'000;
    return metadata;
}

TEST_CASE("opening a local track exposes a fallback and starts bounded metadata work", "[m0][qt][core][local-track]") {
    RecordingPlaybackBackend playback;
    ScriptedMetadataReader metadata;
    LocalTrackSession session{playback, metadata};
    const std::filesystem::path path{L"C:\\M\u00FAsica\\Abertura_\u65E5\u672C\u8A9E.wav"};

    REQUIRE(session.open(path));

    CHECK(session.hasTrack());
    CHECK(session.sourcePath() == path);
    CHECK(session.displayTitle() == "Abertura_\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
    CHECK(session.artist().empty());
    CHECK(session.album().empty());
    CHECK(session.metadataStatus() == LocalTrackMetadataStatus::Pending);
    CHECK(session.playbackGeneration() == 1);
    CHECK(session.metadataGeneration() == MetadataGeneration{1});
    CHECK(playback.loaded_paths == std::vector<std::filesystem::path>{path});
    REQUIRE(metadata.requests.size() == 1);
    CHECK(metadata.requests.front().generation == MetadataGeneration{1});
    CHECK(metadata.requests.front().source_path == path);
}

TEST_CASE("the current ready generation autoplays once and stale events are ignored",
          "[m0][qt][core][local-track][playback]") {
    RecordingPlaybackBackend playback;
    ScriptedMetadataReader metadata;
    LocalTrackSession session{playback, metadata};
    REQUIRE(session.open(L"C:\\Music\\current.wav"));

    session.applyPlaybackEvent({PlaybackEventType::SnapshotChanged, {PlaybackStatus::Ready, 0, 2'000, true, 0}});
    CHECK(playback.play_calls == 0);

    const PlaybackEvent ready{PlaybackEventType::SnapshotChanged, {PlaybackStatus::Ready, 0, 2'000, true, 1}};
    session.applyPlaybackEvent(ready);
    session.applyPlaybackEvent(ready);

    CHECK(playback.play_calls == 1);
}

TEST_CASE("a transient current-generation error does not consume pending autoplay",
          "[m0][qt][core][local-track][playback][recovery]") {
    RecordingPlaybackBackend playback;
    ScriptedMetadataReader metadata;
    LocalTrackSession session{playback, metadata};
    REQUIRE(session.open(L"C:\\Music\\current.wav"));

    session.applyPlaybackEvent(
        {PlaybackEventType::SnapshotChanged, {PlaybackStatus::Error, 0, 0, false, 1, PlaybackErrorCode::InvalidMedia}});
    session.applyPlaybackEvent({PlaybackEventType::SnapshotChanged, {PlaybackStatus::Ready, 0, 2'000, true, 1}});
    session.applyPlaybackEvent({PlaybackEventType::SnapshotChanged, {PlaybackStatus::Ready, 0, 2'000, true, 1}});

    CHECK(playback.play_calls == 1);
}

TEST_CASE("a rejected load preserves the active local track and does not start new metadata work",
          "[m0][qt][core][local-track][error]") {
    RecordingPlaybackBackend playback;
    ScriptedMetadataReader metadata;
    LocalTrackSession session{playback, metadata};
    const std::filesystem::path current{L"C:\\Music\\current.wav"};
    REQUIRE(session.open(current));
    playback.load_result = PlaybackCommandResult::Busy;

    CHECK_FALSE(session.open(L"C:\\Music\\rejected.wav"));

    CHECK(session.sourcePath() == current);
    CHECK(session.displayTitle() == "current");
    CHECK(metadata.requests.size() == 1);
    CHECK(metadata.cancellations.empty());
}

TEST_CASE("replacing a local track cancels its metadata generation", "[m0][qt][core][local-track][metadata]") {
    RecordingPlaybackBackend playback;
    ScriptedMetadataReader metadata;
    LocalTrackSession session{playback, metadata};
    REQUIRE(session.open(L"C:\\Music\\first.wav"));
    REQUIRE(session.open(L"C:\\Music\\second.wav"));

    CHECK(metadata.cancellations == std::vector<MetadataGeneration>{MetadataGeneration{1}});
    REQUIRE(metadata.requests.size() == 2);
    CHECK(metadata.requests.back().generation == MetadataGeneration{2});
    CHECK(session.metadataGeneration() == MetadataGeneration{2});
    CHECK(session.playbackGeneration() == 2);
    CHECK(session.displayTitle() == "second");
}

TEST_CASE("completed metadata replaces the filename fallback only for the current request",
          "[m0][qt][core][local-track][metadata][FR-024]") {
    RecordingPlaybackBackend playback;
    ScriptedMetadataReader metadata;
    LocalTrackSession session{playback, metadata};
    const std::filesystem::path path{L"C:\\Music\\fallback.wav"};
    REQUIRE(session.open(path));
    metadata.next_poll = {
        MetadataPollStatus::Completed,
        MetadataReadCompletion{MetadataGeneration{1}, path, MetadataReadResult{taggedMetadata(path)}},
    };

    REQUIRE(session.pollMetadata());

    CHECK(session.metadataStatus() == LocalTrackMetadataStatus::Ready);
    CHECK(session.displayTitle() == "Embedded title");
    CHECK(session.artist() == "Embedded artist");
    CHECK(session.album() == "Embedded album");
    REQUIRE(session.trackMetadata().has_value());
    CHECK(session.trackMetadata()->audio.duration_ms == 2'000);

    RecordingPlaybackBackend stale_playback;
    ScriptedMetadataReader stale_metadata;
    LocalTrackSession stale_session{stale_playback, stale_metadata};
    REQUIRE(stale_session.open(path));
    stale_metadata.next_poll = {
        MetadataPollStatus::Completed,
        MetadataReadCompletion{MetadataGeneration{9}, path, MetadataReadResult{taggedMetadata(path)}},
    };
    REQUIRE(stale_session.pollMetadata());
    CHECK(stale_session.metadataGeneration() == MetadataGeneration{1});
    CHECK(stale_session.metadataStatus() == LocalTrackMetadataStatus::Failed);
    CHECK(stale_session.displayTitle() == "fallback");

    RecordingPlaybackBackend wrong_source_playback;
    ScriptedMetadataReader wrong_source_reader;
    LocalTrackSession wrong_source_session{wrong_source_playback, wrong_source_reader};
    REQUIRE(wrong_source_session.open(path));
    auto wrong_source = taggedMetadata(L"C:\\Music\\another.wav");
    wrong_source_reader.next_poll = {
        MetadataPollStatus::Completed,
        MetadataReadCompletion{MetadataGeneration{1}, path, MetadataReadResult{std::move(wrong_source)}},
    };
    REQUIRE(wrong_source_session.pollMetadata());
    CHECK(wrong_source_session.metadataStatus() == LocalTrackMetadataStatus::Failed);
    CHECK(wrong_source_session.displayTitle() == "fallback");
}

TEST_CASE("metadata failures and deadlines preserve the immediate filename fallback",
          "[m0][qt][core][local-track][metadata][error]") {
    const auto terminal =
        GENERATE(MetadataPollStatus::Expired, MetadataPollStatus::Cancelled, MetadataPollStatus::Shutdown);
    RecordingPlaybackBackend playback;
    ScriptedMetadataReader metadata;
    LocalTrackSession session{playback, metadata};
    REQUIRE(session.open(L"C:\\Music\\fallback.wav"));
    metadata.next_poll = {terminal, std::nullopt};

    REQUIRE(session.pollMetadata());

    CHECK(session.displayTitle() == "fallback");
    if (terminal == MetadataPollStatus::Expired) {
        CHECK(session.metadataStatus() == LocalTrackMetadataStatus::Expired);
    } else if (terminal == MetadataPollStatus::Cancelled) {
        CHECK(session.metadataStatus() == LocalTrackMetadataStatus::Cancelled);
    } else {
        CHECK(session.metadataStatus() == LocalTrackMetadataStatus::Failed);
    }
}

TEST_CASE("a typed metadata read error is terminal without discarding the selected source",
          "[m0][qt][core][local-track][metadata][error]") {
    RecordingPlaybackBackend playback;
    ScriptedMetadataReader metadata;
    LocalTrackSession session{playback, metadata};
    const std::filesystem::path path{L"C:\\Music\\fallback.wav"};
    REQUIRE(session.open(path));
    metadata.next_poll = {
        MetadataPollStatus::Completed,
        MetadataReadCompletion{MetadataGeneration{1}, path,
                               MetadataReadResult{MetadataReadError{MetadataReadErrorCode::InvalidData, "invalid"}}},
    };

    REQUIRE(session.pollMetadata());

    CHECK(session.hasTrack());
    CHECK(session.sourcePath() == path);
    CHECK(session.displayTitle() == "fallback");
    CHECK(session.metadataStatus() == LocalTrackMetadataStatus::Failed);
    CHECK_FALSE(session.trackMetadata().has_value());
}

} // namespace
} // namespace music_player::m0::test
