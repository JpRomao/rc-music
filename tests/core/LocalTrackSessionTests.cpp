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

    PlaybackCommandResult pause() override {
        ++pause_calls;
        if (pause_result == PlaybackCommandResult::Accepted) {
            snapshot_.status = PlaybackStatus::Paused;
        }
        return pause_result;
    }
    PlaybackCommandResult seekTo(std::int64_t) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult setVolume(double) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult setMuted(bool) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult unload() override {
        ++unload_calls;
        if (unload_result == PlaybackCommandResult::Accepted) {
            ++snapshot_.generation;
            snapshot_ = {PlaybackStatus::Empty, 0, 0, false, snapshot_.generation};
        }
        return unload_result;
    }
    PlaybackCommandResult shutdown() override { return PlaybackCommandResult::Accepted; }

    PlaybackSnapshot snapshot_{};
    PlaybackEventCallback callback_{};
    PlaybackCommandResult load_result{PlaybackCommandResult::Accepted};
    PlaybackCommandResult play_result{PlaybackCommandResult::Accepted};
    PlaybackCommandResult pause_result{PlaybackCommandResult::Accepted};
    PlaybackCommandResult unload_result{PlaybackCommandResult::Accepted};
    std::vector<std::filesystem::path> loaded_paths{};
    int play_calls{0};
    int pause_calls{0};
    int unload_calls{0};
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

    CHECK(
        session.applyPlaybackEvent({PlaybackEventType::SnapshotChanged, {PlaybackStatus::Ready, 0, 2'000, true, 0}}) ==
        LocalTrackPlaybackEventResult::Ignored);
    CHECK(playback.play_calls == 0);

    const PlaybackEvent ready{PlaybackEventType::SnapshotChanged, {PlaybackStatus::Ready, 0, 2'000, true, 1}};
    CHECK(session.applyPlaybackEvent(ready) == LocalTrackPlaybackEventResult::Applied);
    CHECK(session.applyPlaybackEvent(ready) == LocalTrackPlaybackEventResult::Applied);

    CHECK(playback.play_calls == 1);
}

TEST_CASE("a local track can stay ready without autoplaying", "[m0][qt][core][local-track][playback][FR-011]") {
    RecordingPlaybackBackend playback;
    ScriptedMetadataReader metadata;
    LocalTrackSession session{playback, metadata};
    REQUIRE(session.open(L"C:\\Music\\ready-next.wav", LocalTrackStartIntent::StayReady));

    const PlaybackEvent ready{PlaybackEventType::SnapshotChanged, {PlaybackStatus::Ready, 0, 2'000, true, 1}};
    CHECK(session.applyPlaybackEvent(ready) == LocalTrackPlaybackEventResult::Applied);
    CHECK(session.applyPlaybackEvent(ready) == LocalTrackPlaybackEventResult::Applied);

    CHECK(playback.play_calls == 0);
    CHECK(playback.pause_calls == 0);
}

TEST_CASE("a restored local track becomes paused without autoplay and preserves its confirmed position",
          "[m0][qt][core][local-track][playback][FR-043][regression]") {
    RecordingPlaybackBackend playback;
    ScriptedMetadataReader metadata;
    LocalTrackSession session{playback, metadata};
    REQUIRE(session.open(L"C:\\Music\\restored.wav", LocalTrackStartIntent::RestorePaused));

    const PlaybackEvent ready{PlaybackEventType::SnapshotChanged, {PlaybackStatus::Ready, 750, 2'000, true, 1}};
    playback.snapshot_ = ready.snapshot;
    CHECK(session.applyPlaybackEvent(ready) == LocalTrackPlaybackEventResult::Applied);
    CHECK(session.applyPlaybackEvent(ready) == LocalTrackPlaybackEventResult::Applied);

    CHECK(session.startIntent() == LocalTrackStartIntent::RestorePaused);
    CHECK(playback.play_calls == 0);
    CHECK(playback.pause_calls == 1);
    CHECK(playback.snapshot_.status == PlaybackStatus::Paused);
    CHECK(playback.snapshot_.position_ms == 750);
}

TEST_CASE("play now promotes only the pending start of the current local track",
          "[m0][qt][core][local-track][playback][FR-016]") {
    RecordingPlaybackBackend playback;
    ScriptedMetadataReader metadata;
    LocalTrackSession session{playback, metadata};
    REQUIRE(session.open(L"C:\\Music\\queued.wav", LocalTrackStartIntent::StayReady));

    REQUIRE(session.promotePendingStartToAutoplay());
    CHECK(session.startIntent() == LocalTrackStartIntent::Autoplay);
    CHECK(
        session.applyPlaybackEvent({PlaybackEventType::SnapshotChanged, {PlaybackStatus::Ready, 0, 2'000, true, 1}}) ==
        LocalTrackPlaybackEventResult::Applied);

    CHECK(playback.play_calls == 1);
    CHECK_FALSE(session.promotePendingStartToAutoplay());
}

TEST_CASE("a current-generation playback error cancels pending autoplay",
          "[m0][qt][core][local-track][playback][error]") {
    RecordingPlaybackBackend playback;
    ScriptedMetadataReader metadata;
    LocalTrackSession session{playback, metadata};
    REQUIRE(session.open(L"C:\\Music\\current.wav"));
    const auto error = GENERATE(PlaybackErrorCode::SourceUnavailable, PlaybackErrorCode::InvalidMedia,
                                PlaybackErrorCode::AccessDenied);

    CHECK(session.applyPlaybackEvent(
              {PlaybackEventType::SnapshotChanged, {PlaybackStatus::Error, 0, 0, false, 1, error}}) ==
          LocalTrackPlaybackEventResult::OccurrenceUnavailable);
    CHECK(
        session.applyPlaybackEvent({PlaybackEventType::SnapshotChanged, {PlaybackStatus::Ready, 0, 2'000, true, 1}}) ==
        LocalTrackPlaybackEventResult::Applied);

    CHECK(playback.play_calls == 0);
}

TEST_CASE("output interruptions preserve the source and cancel pending autoplay",
          "[m0][qt][core][local-track][playback][FR-038]") {
    RecordingPlaybackBackend playback;
    ScriptedMetadataReader metadata;
    LocalTrackSession session{playback, metadata};
    const std::filesystem::path source{L"C:\\Music\\current.wav"};
    REQUIRE(session.open(source));
    const auto error = GENERATE(PlaybackErrorCode::AudioOutputUnavailable, PlaybackErrorCode::AudioOutputChanged);
    CHECK(session.applyPlaybackEvent(
              {PlaybackEventType::SnapshotChanged, {PlaybackStatus::Error, 750, 2'000, false, 1, error}}) ==
          LocalTrackPlaybackEventResult::Applied);
    CHECK(session.hasTrack());
    CHECK(session.sourcePath() == source);
    CHECK(session.applyPlaybackEvent(
              {PlaybackEventType::SnapshotChanged, {PlaybackStatus::Ready, 750, 2'000, true, 1}}) ==
          LocalTrackPlaybackEventResult::Applied);
    CHECK(playback.play_calls == 0);
}

TEST_CASE("a removed source volume stops the generation until a new load", "[m0][qt][core][local-track][FR-038]") {
    RecordingPlaybackBackend playback;
    ScriptedMetadataReader metadata;
    LocalTrackSession session{playback, metadata};
    const std::filesystem::path source{L"C:\\Music\\current.wav"};
    REQUIRE(session.open(source));
    const PlaybackEvent removal{PlaybackEventType::SnapshotChanged,
                                {PlaybackStatus::Error, 0, 0, false, 1, PlaybackErrorCode::SourceVolumeRemoved}};
    CHECK(session.applyPlaybackEvent(removal) == LocalTrackPlaybackEventResult::OccurrenceUnavailable);
    CHECK(session.sourcePath() == source);
    CHECK_FALSE(session.promotePendingStartToAutoplay());
    CHECK(
        session.applyPlaybackEvent({PlaybackEventType::SnapshotChanged, {PlaybackStatus::Ready, 0, 2'000, true, 1}}) ==
        LocalTrackPlaybackEventResult::Ignored);
    CHECK(session.applyPlaybackEvent({PlaybackEventType::Ended, {PlaybackStatus::Ready, 2'000, 2'000, true, 1}}) ==
          LocalTrackPlaybackEventResult::Ignored);
    CHECK(playback.play_calls == 0);

    REQUIRE(session.open(source, LocalTrackStartIntent::RestorePaused));
    CHECK(session.applyPlaybackEvent(removal) == LocalTrackPlaybackEventResult::Ignored);
    CHECK(
        session.applyPlaybackEvent({PlaybackEventType::SnapshotChanged, {PlaybackStatus::Ready, 0, 2'000, true, 2}}) ==
        LocalTrackPlaybackEventResult::Applied);
    CHECK(playback.pause_calls == 1);
    CHECK(playback.play_calls == 0);
}

TEST_CASE("current terminal events are typed and stale terminal events are ignored",
          "[m0][qt][core][local-track][playback][FR-011][FR-016]") {
    RecordingPlaybackBackend playback;
    ScriptedMetadataReader metadata;
    LocalTrackSession session{playback, metadata};
    REQUIRE(session.open(L"C:\\Music\\current.wav", LocalTrackStartIntent::StayReady));

    CHECK(session.startIntent() == LocalTrackStartIntent::StayReady);
    CHECK(session.applyPlaybackEvent({PlaybackEventType::Ended, {PlaybackStatus::Paused, 2'000, 2'000, true, 0}}) ==
          LocalTrackPlaybackEventResult::Ignored);
    CHECK(session.applyPlaybackEvent({PlaybackEventType::Ended, {PlaybackStatus::Paused, 2'000, 2'000, true, 2}}) ==
          LocalTrackPlaybackEventResult::Ignored);
    CHECK(session.applyPlaybackEvent({PlaybackEventType::Ended, {PlaybackStatus::Paused, 2'000, 2'000, true, 1}}) ==
          LocalTrackPlaybackEventResult::Ended);
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

TEST_CASE("a rejected load exposes its typed backend result without mutating the session",
          "[m0][qt][core][local-track][error]") {
    const auto rejection =
        GENERATE(PlaybackCommandResult::Busy, PlaybackCommandResult::NotReady, PlaybackCommandResult::InvalidArgument);
    RecordingPlaybackBackend playback;
    ScriptedMetadataReader metadata;
    LocalTrackSession session{playback, metadata};
    const std::filesystem::path current{L"C:\\Music\\current.wav"};
    REQUIRE(session.open(current));
    playback.load_result = rejection;

    CHECK(session.openWithResult(L"C:\\Music\\rejected.wav") == rejection);

    CHECK(session.hasTrack());
    CHECK(session.sourcePath() == current);
    CHECK(metadata.requests.size() == 1);
    CHECK(metadata.cancellations.empty());
}

TEST_CASE("unloading a local track is atomic and invalidates its pending work",
          "[m0][qt][core][local-track][playback][FR-042]") {
    RecordingPlaybackBackend playback;
    ScriptedMetadataReader metadata;
    LocalTrackSession session{playback, metadata};
    const std::filesystem::path source{L"C:\\Music\\current.wav"};
    REQUIRE(session.open(source));
    playback.unload_result = PlaybackCommandResult::Busy;

    CHECK_FALSE(session.unload());
    CHECK(session.hasTrack());
    CHECK(session.sourcePath() == source);
    CHECK(session.metadataStatus() == LocalTrackMetadataStatus::Pending);
    CHECK(metadata.cancellations.empty());

    playback.unload_result = PlaybackCommandResult::Accepted;
    REQUIRE(session.unload());
    CHECK_FALSE(session.hasTrack());
    CHECK(session.sourcePath().empty());
    CHECK(session.displayTitle().empty());
    CHECK(session.metadataStatus() == LocalTrackMetadataStatus::Idle);
    CHECK(session.playbackGeneration() == 2);
    CHECK(metadata.cancellations == std::vector<MetadataGeneration>{MetadataGeneration{1}});
    CHECK(
        session.applyPlaybackEvent({PlaybackEventType::SnapshotChanged, {PlaybackStatus::Ready, 0, 2'000, true, 1}}) ==
        LocalTrackPlaybackEventResult::Ignored);
    CHECK(session.unload());
    CHECK(playback.unload_calls == 2);
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

TEST_CASE("stale metadata completion leaves the current track pending",
          "[m0][qt][core][local-track][metadata][recovery][FR-024][MR-017]") {
    RecordingPlaybackBackend playback;
    ScriptedMetadataReader metadata;
    LocalTrackSession session{playback, metadata};
    const std::filesystem::path first{L"C:\\Music\\first.wav"};
    const std::filesystem::path current{L"C:\\Music\\current.wav"};
    REQUIRE(session.open(first));
    REQUIRE(session.open(current));
    metadata.next_poll = {
        MetadataPollStatus::Completed,
        MetadataReadCompletion{MetadataGeneration{1}, first, MetadataReadResult{taggedMetadata(first)}},
    };

    CHECK_FALSE(session.pollMetadata());
    CHECK(session.metadataGeneration() == MetadataGeneration{2});
    CHECK(session.metadataStatus() == LocalTrackMetadataStatus::Pending);
    CHECK(session.displayTitle() == "current");
    CHECK_FALSE(session.trackMetadata().has_value());

    metadata.next_poll = {
        MetadataPollStatus::Completed,
        MetadataReadCompletion{MetadataGeneration{2}, current, MetadataReadResult{taggedMetadata(current)}},
    };
    REQUIRE(session.pollMetadata());
    CHECK(session.metadataStatus() == LocalTrackMetadataStatus::Ready);
    CHECK(session.displayTitle() == "Embedded title");
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
    CHECK(session.metadataErrorCode() == MetadataReadErrorCode::InvalidData);
    CHECK_FALSE(session.trackMetadata().has_value());
}

TEST_CASE("a typed metadata error belongs only to its accepted current track",
          "[m0][qt][core][local-track][metadata][error][recovery]") {
    RecordingPlaybackBackend playback;
    ScriptedMetadataReader metadata;
    LocalTrackSession session{playback, metadata};
    const std::filesystem::path first{L"C:\\Music\\first.wav"};
    const std::filesystem::path current{L"C:\\Music\\current.wav"};
    const std::filesystem::path final{L"C:\\Music\\final.wav"};
    REQUIRE(session.open(first));
    metadata.next_poll = {
        MetadataPollStatus::Completed,
        MetadataReadCompletion{MetadataGeneration{1}, first,
                               MetadataReadResult{MetadataReadError{MetadataReadErrorCode::UnsupportedFormat,
                                                                    "untrusted adapter detail"}}},
    };
    REQUIRE(session.pollMetadata());
    REQUIRE(session.metadataErrorCode() == MetadataReadErrorCode::UnsupportedFormat);

    playback.load_result = PlaybackCommandResult::Busy;
    CHECK_FALSE(session.open(L"C:\\Music\\rejected.wav"));
    CHECK(session.metadataErrorCode() == MetadataReadErrorCode::UnsupportedFormat);

    playback.load_result = PlaybackCommandResult::Accepted;
    REQUIRE(session.open(current));
    CHECK_FALSE(session.metadataErrorCode().has_value());
    metadata.next_poll = {
        MetadataPollStatus::Completed,
        MetadataReadCompletion{
            MetadataGeneration{1}, first,
            MetadataReadResult{MetadataReadError{MetadataReadErrorCode::ReadFailed, "stale adapter detail"}}},
    };
    CHECK_FALSE(session.pollMetadata());
    CHECK(session.metadataStatus() == LocalTrackMetadataStatus::Pending);
    CHECK_FALSE(session.metadataErrorCode().has_value());

    metadata.next_poll = {
        MetadataPollStatus::Completed,
        MetadataReadCompletion{MetadataGeneration{2}, current, MetadataReadResult{taggedMetadata(current)}},
    };
    REQUIRE(session.pollMetadata());
    CHECK(session.metadataStatus() == LocalTrackMetadataStatus::Ready);
    CHECK_FALSE(session.metadataErrorCode().has_value());

    REQUIRE(session.open(final));
    metadata.next_poll = {
        MetadataPollStatus::Completed,
        MetadataReadCompletion{MetadataGeneration{3}, final,
                               MetadataReadResult{MetadataReadError{MetadataReadErrorCode::SourceUnavailable,
                                                                    "do not retain this message"}}},
    };
    REQUIRE(session.pollMetadata());
    REQUIRE(session.metadataErrorCode() == MetadataReadErrorCode::SourceUnavailable);
    REQUIRE(session.unload());
    CHECK_FALSE(session.metadataErrorCode().has_value());
}

} // namespace
} // namespace music_player::m0::test
