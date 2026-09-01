#include "m0/application/PlayerController.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <vector>

namespace music_player::m0::test {
namespace {

class RecordingPlaybackBackend final : public PlaybackBackend {
  public:
    [[nodiscard]] const PlaybackSnapshot& snapshot() const noexcept override { return snapshot_; }

    void setEventCallback(PlaybackEventCallback) override {}

    PlaybackCommandResult load(const std::filesystem::path&) override { return PlaybackCommandResult::Accepted; }

    PlaybackCommandResult play() override {
        ++play_requests;
        return play_result;
    }

    PlaybackCommandResult pause() override {
        ++pause_requests;
        return pause_result;
    }

    PlaybackCommandResult seekTo(const std::int64_t position_ms) override {
        seek_requests.push_back(position_ms);
        return seek_result;
    }

    PlaybackCommandResult setVolume(const double volume) override {
        volume_requests.push_back(volume);
        if (volume_result == PlaybackCommandResult::Accepted) {
            backend_volume = volume;
        }
        return volume_result;
    }

    PlaybackCommandResult setMuted(const bool muted) override {
        mute_requests.push_back(muted);
        if (mute_result == PlaybackCommandResult::Accepted) {
            backend_muted = muted;
        }
        return mute_result;
    }

    PlaybackCommandResult unload() override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult shutdown() override { return PlaybackCommandResult::Accepted; }

    int play_requests{0};
    int pause_requests{0};
    PlaybackCommandResult play_result{PlaybackCommandResult::Accepted};
    PlaybackCommandResult pause_result{PlaybackCommandResult::Accepted};
    PlaybackCommandResult seek_result{PlaybackCommandResult::Accepted};
    PlaybackCommandResult volume_result{PlaybackCommandResult::Accepted};
    PlaybackCommandResult mute_result{PlaybackCommandResult::Accepted};
    double backend_volume{0.8};
    bool backend_muted{false};
    std::vector<std::int64_t> seek_requests{};
    std::vector<double> volume_requests{};
    std::vector<bool> mute_requests{};

  private:
    PlaybackSnapshot snapshot_{};
};

class RecordingPlatformIntegration final : public PlatformIntegration {
  public:
    explicit RecordingPlatformIntegration(const bool tray_available) : tray_available_(tray_available) {}

    [[nodiscard]] bool trayAvailable() const noexcept override { return tray_available_; }

    void hideWindow() override { ++hide_calls; }

    void minimizeWindow() override { ++minimize_calls; }

    void requestExit() override { ++exit_calls; }

    void showAndActivateWindow() override { ++restore_calls; }

    bool tray_available_;
    int hide_calls{0};
    int minimize_calls{0};
    int exit_calls{0};
    int restore_calls{0};
};

[[nodiscard]] PlaybackSnapshot playingSnapshot() {
    return PlaybackSnapshot{
        .status = PlaybackStatus::Playing,
        .position_ms = 84'000,
        .duration_ms = 248'000,
        .seekable = true,
    };
}

[[nodiscard]] Preferences persistedPreferences() {
    return Preferences{
        .theme = ThemeMode::Dark,
        .motion = MotionMode::Reduced,
        .volume = 0.35,
        .muted = true,
        .skip_backward_seconds = 7,
        .skip_forward_seconds = 20,
        .close_behavior = CloseBehavior::Exit,
        .restore_last_session = false,
        .start_minimized = true,
        .queue_end_behavior = QueueEndBehavior::StopClearSelection,
    };
}

void checkUnchangedPreferences(const Preferences& actual, const Preferences& expected) {
    CHECK(actual == expected);
    CHECK(actual.theme == expected.theme);
    CHECK(actual.motion == expected.motion);
    CHECK(actual.volume == expected.volume);
    CHECK(actual.muted == expected.muted);
    CHECK(actual.skip_backward_seconds == expected.skip_backward_seconds);
    CHECK(actual.skip_forward_seconds == expected.skip_forward_seconds);
    CHECK(actual.close_behavior == expected.close_behavior);
    CHECK(actual.restore_last_session == expected.restore_last_session);
    CHECK(actual.start_minimized == expected.start_minimized);
    CHECK(actual.queue_end_behavior == expected.queue_end_behavior);
}

TEST_CASE("controller rejects an invalid initial preferences snapshot", "[m0][qt][core][preferences]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    Preferences invalid = persistedPreferences();

    const auto invalid_case = GENERATE_COPY(
        std::pair{0, std::numeric_limits<double>::quiet_NaN()}, std::pair{0, std::numeric_limits<double>::infinity()},
        std::pair{0, -0.01}, std::pair{0, 1.01}, std::pair{1, 0.0}, std::pair{1, 301.0}, std::pair{2, 0.0},
        std::pair{2, 301.0}, std::pair{3, 0.0}, std::pair{4, 0.0}, std::pair{5, 0.0}, std::pair{6, 0.0});

    switch (invalid_case.first) {
    case 0:
        invalid.volume = invalid_case.second;
        break;
    case 1:
        invalid.skip_backward_seconds = static_cast<int>(invalid_case.second);
        break;
    case 2:
        invalid.skip_forward_seconds = static_cast<int>(invalid_case.second);
        break;
    case 3:
        invalid.theme = static_cast<ThemeMode>(99);
        break;
    case 4:
        invalid.motion = static_cast<MotionMode>(99);
        break;
    case 5:
        invalid.close_behavior = static_cast<CloseBehavior>(99);
        break;
    case 6:
        invalid.queue_end_behavior = static_cast<QueueEndBehavior>(99);
        break;
    }

    CHECK_THROWS_AS(PlayerController(backend, platform, playingSnapshot(), invalid), std::invalid_argument);
    CHECK(backend.volume_requests.empty());
    CHECK(backend.mute_requests.empty());
}

TEST_CASE("toggle playback maps active states to exactly one backend command", "[m0][qt][core][transport]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    PlayerController controller{backend, platform, playingSnapshot()};

    CHECK(controller.togglePlayback());
    CHECK(backend.pause_requests == 1);
    CHECK(backend.play_requests == 0);

    controller.applyBackendSnapshot({PlaybackStatus::Paused, 84'000, 248'000, true});
    CHECK(controller.togglePlayback());
    CHECK(backend.play_requests == 1);

    controller.applyBackendSnapshot({PlaybackStatus::Ready, 84'000, 248'000, true});
    CHECK(controller.togglePlayback());
    CHECK(backend.play_requests == 2);
}

TEST_CASE("toggle playback is inert for inactive states", "[m0][qt][core][transport]") {
    for (const auto status : {PlaybackStatus::Empty, PlaybackStatus::Loading, PlaybackStatus::Error}) {
        RecordingPlaybackBackend backend;
        RecordingPlatformIntegration platform{true};
        PlayerController controller{backend, platform, {status, 0, 248'000, false}};

        CHECK_FALSE(controller.togglePlayback());
        CHECK(backend.play_requests == 0);
        CHECK(backend.pause_requests == 0);
        CHECK(controller.confirmedSnapshot().status == status);
    }
}

TEST_CASE("toggle playback reports backend command rejection", "[m0][qt][core][transport][error]") {
    const auto status = GENERATE(PlaybackStatus::Ready, PlaybackStatus::Paused, PlaybackStatus::Playing);
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    backend.play_result = PlaybackCommandResult::Busy;
    backend.pause_result = PlaybackCommandResult::Shutdown;
    PlayerController controller{backend, platform, {status, 84'000, 248'000, true}};

    CHECK_FALSE(controller.togglePlayback());
    CHECK(backend.play_requests == (status == PlaybackStatus::Playing ? 0 : 1));
    CHECK(backend.pause_requests == (status == PlaybackStatus::Playing ? 1 : 0));
    CHECK(controller.confirmedSnapshot().status == status);
}

TEST_CASE("skip actions use confirmed position preferences and publish one pending seek",
          "[m0][qt][core][transport][seek]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    PlayerController forward{backend, platform, playingSnapshot()};

    CHECK(forward.setSkipForwardSeconds(7));
    CHECK(forward.skipForward());
    CHECK(forward.seekPreviewActive());
    CHECK(forward.seekPending());
    CHECK(forward.displayedPositionMs() == 91'000);
    REQUIRE(backend.seek_requests.size() == 1);
    CHECK(backend.seek_requests.front() == 91'000);

    RecordingPlaybackBackend backward_backend;
    PlayerController backward{backward_backend, platform, {PlaybackStatus::Playing, 2'000, 248'000, true}};
    CHECK(backward.skipBackward());
    CHECK(backward.displayedPositionMs() == 0);
    REQUIRE(backward_backend.seek_requests.size() == 1);
    CHECK(backward_backend.seek_requests.front() == 0);
}

TEST_CASE("skip clamps without overflow", "[m0][qt][core][transport][seek]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    PlayerController controller{backend,
                                platform,
                                {PlaybackStatus::Playing, std::numeric_limits<std::int64_t>::max() - 100,
                                 std::numeric_limits<std::int64_t>::max(), true}};

    CHECK(controller.skipForward());
    REQUIRE(backend.seek_requests.size() == 1);
    CHECK(backend.seek_requests.front() == std::numeric_limits<std::int64_t>::max());
}

TEST_CASE("skip is inert when seeking is unavailable or already active", "[m0][qt][core][transport][seek]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    PlayerController unavailable{backend, platform, {PlaybackStatus::Playing, 84'000, 248'000, false}};

    CHECK_FALSE(unavailable.skipForward());
    CHECK_FALSE(unavailable.seekPreviewActive());
    CHECK_FALSE(unavailable.seekPending());
    CHECK(unavailable.displayedPositionMs() == 84'000);
    CHECK(backend.seek_requests.empty());

    PlayerController previewing{backend, platform, playingSnapshot()};
    previewing.beginSeekPreview(100'000);
    CHECK_FALSE(previewing.skipBackward());
    CHECK(previewing.displayedPositionMs() == 100'000);
    CHECK_FALSE(previewing.seekPending());
    CHECK(backend.seek_requests.empty());

    previewing.commitSeekPreview();
    CHECK_FALSE(previewing.skipForward());
    REQUIRE(backend.seek_requests.size() == 1);
    CHECK(backend.seek_requests.front() == 100'000);
}

TEST_CASE("skip reports a rejected seek and restores the confirmed position",
          "[m0][qt][core][transport][seek][error]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    backend.seek_result = PlaybackCommandResult::NotSeekable;
    PlayerController controller{backend, platform, playingSnapshot()};

    CHECK_FALSE(controller.skipForward());
    REQUIRE(backend.seek_requests.size() == 1);
    CHECK(backend.seek_requests.front() == 89'000);
    CHECK_FALSE(controller.seekPreviewActive());
    CHECK_FALSE(controller.seekPending());
    CHECK(controller.displayedPositionMs() == 84'000);
}

TEST_CASE("skip rejects inactive states even when a snapshot claims to be seekable",
          "[m0][qt][core][transport][seek][regression]") {
    for (const auto status : {PlaybackStatus::Empty, PlaybackStatus::Loading, PlaybackStatus::Error}) {
        RecordingPlatformIntegration platform{true};
        for (const bool forward : {false, true}) {
            RecordingPlaybackBackend backend;
            PlayerController controller{backend, platform, {status, 84'000, 248'000, true}};

            const bool accepted = forward ? controller.skipForward() : controller.skipBackward();
            CHECK_FALSE(accepted);
            CHECK_FALSE(controller.seekPreviewActive());
            CHECK_FALSE(controller.seekPending());
            CHECK(controller.displayedPositionMs() == 84'000);
            CHECK(backend.seek_requests.empty());
        }
    }
}

TEST_CASE("controller starts from the frozen M0 defaults", "[m0][qt][core][defaults]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    const PlayerController controller{backend, platform};

    CHECK(controller.confirmedSnapshot().status == PlaybackStatus::Empty);
    CHECK(controller.displayedPositionMs() == 0);
    CHECK(controller.preferences().theme == ThemeMode::System);
    CHECK(controller.preferences().motion == MotionMode::System);
    CHECK(controller.preferences().volume == Catch::Approx(0.8));
    CHECK_FALSE(controller.preferences().muted);
    CHECK(controller.preferences().skip_backward_seconds == 5);
    CHECK(controller.preferences().skip_forward_seconds == 5);
    CHECK(controller.preferences().close_behavior == CloseBehavior::MinimizeToTray);
    CHECK(controller.preferences().restore_last_session);
    CHECK_FALSE(controller.preferences().start_minimized);
    CHECK(controller.preferences().queue_end_behavior == QueueEndBehavior::StopKeepSelection);
}

TEST_CASE("controller starts with the complete persisted preference snapshot", "[m0][qt][core][preferences][restore]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{false};
    const PlaybackSnapshot playback = playingSnapshot();
    const Preferences persisted = persistedPreferences();

    const PlayerController controller{backend, platform, playback, persisted};

    CHECK(controller.confirmedSnapshot() == playback);
    checkUnchangedPreferences(controller.preferences(), persisted);
    CHECK(controller.preferences().start_minimized);
}

TEST_CASE("controller normalizes invalid snapshot bounds at its public boundary", "[m0][qt][core][snapshot]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    PlayerController controller{backend, platform,
                                PlaybackSnapshot{
                                    .status = PlaybackStatus::Playing,
                                    .position_ms = -10,
                                    .duration_ms = -1,
                                    .seekable = true,
                                }};

    CHECK(controller.confirmedSnapshot().position_ms == 0);
    CHECK(controller.confirmedSnapshot().duration_ms == 0);
    CHECK_FALSE(controller.confirmedSnapshot().seekable);
    CHECK(controller.displayedPositionMs() == 0);

    controller.applyBackendSnapshot(PlaybackSnapshot{
        .status = PlaybackStatus::Paused,
        .position_ms = 300,
        .duration_ms = 200,
        .seekable = true,
    });

    CHECK(controller.confirmedSnapshot().position_ms == 200);
    CHECK(controller.confirmedSnapshot().duration_ms == 200);
    CHECK(controller.confirmedSnapshot().seekable);
    CHECK(controller.displayedPositionMs() == 200);
}

TEST_CASE("backend snapshots older than the confirmed generation are ignored",
          "[m0][qt][core][snapshot][generation][regression]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    auto initial = playingSnapshot();
    initial.generation = 7;
    PlayerController controller{backend, platform, initial};
    controller.beginSeekPreview(124'000);
    controller.commitSeekPreview();
    REQUIRE(controller.seekPending());

    controller.applyBackendSnapshot(PlaybackSnapshot{
        .status = PlaybackStatus::Error,
        .position_ms = 0,
        .duration_ms = 0,
        .seekable = false,
        .generation = 6,
        .error = PlaybackErrorCode::BackendFailure,
    });

    CHECK(controller.confirmedSnapshot() == initial);
    CHECK(controller.seekPreviewActive());
    CHECK(controller.seekPending());
    CHECK(controller.displayedPositionMs() == 124'000);
}

TEST_CASE("a newer playback generation clears seek state owned by the previous track",
          "[m0][qt][core][snapshot][generation][seek][regression]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    auto initial = playingSnapshot();
    initial.generation = 7;
    PlayerController controller{backend, platform, initial};
    controller.beginSeekPreview(124'000);
    REQUIRE(controller.commitSeekPreview());
    REQUIRE(controller.seekPreviewActive());
    REQUIRE(controller.seekPending());

    controller.applyBackendSnapshot(PlaybackSnapshot{
        .status = PlaybackStatus::Playing,
        .position_ms = 10'000,
        .duration_ms = 100'000,
        .seekable = true,
        .generation = 8,
    });

    CHECK_FALSE(controller.seekPreviewActive());
    CHECK_FALSE(controller.seekPending());
    CHECK(controller.displayedPositionMs() == 10'000);
    controller.beginSeekPreview(20'000);
    CHECK(controller.seekPreviewActive());
}

TEST_CASE("seek preview owns the displayed position until backend acknowledgement", "[m0][qt][core][seek]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    PlayerController controller{backend, platform, playingSnapshot()};

    controller.beginSeekPreview(124'000);

    REQUIRE(controller.seekPreviewActive());
    CHECK(controller.displayedPositionMs() == 124'000);

    controller.applyBackendSnapshot(PlaybackSnapshot{
        .status = PlaybackStatus::Playing,
        .position_ms = 84'200,
        .duration_ms = 248'000,
        .seekable = true,
    });
    CHECK(controller.displayedPositionMs() == 124'000);

    controller.commitSeekPreview();
    REQUIRE(controller.seekPending());
    controller.applyBackendSnapshot(PlaybackSnapshot{
        .status = PlaybackStatus::Playing,
        .position_ms = 124'000,
        .duration_ms = 248'000,
        .seekable = true,
    });
    CHECK_FALSE(controller.seekPreviewActive());
    CHECK_FALSE(controller.seekPending());
    CHECK(controller.displayedPositionMs() == 124'000);
}

TEST_CASE("seek release clamps to duration and sends exactly one backend command", "[m0][qt][core][seek]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    PlayerController controller{backend, platform, playingSnapshot()};

    controller.beginSeekPreview(300'000);
    controller.commitSeekPreview();
    controller.commitSeekPreview();

    REQUIRE(backend.seek_requests.size() == 1);
    CHECK(backend.seek_requests.front() == 248'000);
}

TEST_CASE("committing an unchanged seek completes without waiting for a backend event",
          "[m0][qt][core][seek][regression]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    PlayerController controller{backend, platform, playingSnapshot()};

    controller.beginSeekPreview(84'000);
    REQUIRE(controller.seekPreviewActive());

    CHECK(controller.commitSeekPreview());
    CHECK_FALSE(controller.seekPreviewActive());
    CHECK_FALSE(controller.seekPending());
    CHECK(controller.displayedPositionMs() == 84'000);
    CHECK(backend.seek_requests.empty());
}

TEST_CASE("rejected seek commit clears optimistic preview state", "[m0][qt][core][seek][error]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    backend.seek_result = PlaybackCommandResult::Busy;
    PlayerController controller{backend, platform, playingSnapshot()};

    controller.beginSeekPreview(124'000);
    REQUIRE(controller.seekPreviewActive());
    REQUIRE(controller.displayedPositionMs() == 124'000);

    CHECK_FALSE(controller.commitSeekPreview());
    REQUIRE(backend.seek_requests.size() == 1);
    CHECK(backend.seek_requests.front() == 124'000);
    CHECK_FALSE(controller.seekPreviewActive());
    CHECK_FALSE(controller.seekPending());
    CHECK(controller.displayedPositionMs() == 84'000);
}

TEST_CASE("seek clamps at zero and is ignored when the source is not seekable", "[m0][qt][core][seek]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    PlayerController controller{backend, platform, playingSnapshot()};

    controller.beginSeekPreview(-1'000);
    controller.commitSeekPreview();

    REQUIRE(backend.seek_requests.size() == 1);
    CHECK(backend.seek_requests.front() == 0);

    controller.applyBackendSnapshot(PlaybackSnapshot{
        .status = PlaybackStatus::Paused,
        .position_ms = 0,
        .duration_ms = 248'000,
        .seekable = false,
    });
    controller.beginSeekPreview(124'000);
    controller.commitSeekPreview();

    CHECK(backend.seek_requests.size() == 1);
    CHECK_FALSE(controller.seekPreviewActive());
    CHECK(controller.confirmedSnapshot().status == PlaybackStatus::Paused);
}

TEST_CASE("only the target snapshot acknowledges a pending seek", "[m0][qt][core][seek]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    PlayerController controller{backend, platform, playingSnapshot()};

    controller.beginSeekPreview(124'000);
    controller.commitSeekPreview();
    controller.applyBackendSnapshot(PlaybackSnapshot{
        .status = PlaybackStatus::Playing,
        .position_ms = 84'200,
        .duration_ms = 248'000,
        .seekable = true,
    });

    CHECK(controller.seekPending());
    CHECK(controller.displayedPositionMs() == 124'000);
}

TEST_CASE("non-transport snapshots cancel seek preview and pending state", "[m0][qt][core][seek][error]") {
    const PlaybackStatus status = GENERATE(PlaybackStatus::Empty, PlaybackStatus::Loading, PlaybackStatus::Error);
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    PlayerController controller{backend, platform, playingSnapshot()};

    controller.beginSeekPreview(124'000);
    controller.commitSeekPreview();
    REQUIRE(controller.seekPreviewActive());
    REQUIRE(controller.seekPending());

    controller.applyBackendSnapshot({status, 0, 0, false});

    CHECK_FALSE(controller.seekPreviewActive());
    CHECK_FALSE(controller.seekPending());
    CHECK(controller.displayedPositionMs() == 0);
}

TEST_CASE("a rejected seek restores the last confirmed position", "[m0][qt][core][seek][error]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    PlayerController controller{backend, platform, playingSnapshot()};

    controller.beginSeekPreview(124'000);
    controller.commitSeekPreview();
    controller.rejectPendingSeek();

    CHECK_FALSE(controller.seekPreviewActive());
    CHECK_FALSE(controller.seekPending());
    CHECK(controller.displayedPositionMs() == 84'000);
}

TEST_CASE("seek acknowledgement preserves paused playback", "[m0][qt][core][seek][pause]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    auto paused = playingSnapshot();
    paused.status = PlaybackStatus::Paused;
    PlayerController controller{backend, platform, paused};

    controller.beginSeekPreview(124'000);
    controller.commitSeekPreview();
    controller.applyBackendSnapshot(PlaybackSnapshot{
        .status = PlaybackStatus::Paused,
        .position_ms = 124'000,
        .duration_ms = 248'000,
        .seekable = true,
    });

    CHECK(controller.confirmedSnapshot().status == PlaybackStatus::Paused);
}

TEST_CASE("preference commands update the frozen theme and skip ranges", "[m0][qt][core][preferences]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    PlayerController controller{backend, platform};

    controller.setTheme(ThemeMode::Dark);
    controller.setMotionMode(MotionMode::Reduced);
    REQUIRE(controller.setSkipBackwardSeconds(7));
    REQUIRE(controller.setSkipForwardSeconds(20));
    controller.setRestoreLastSession(false);
    controller.setQueueEndBehavior(QueueEndBehavior::StopClearSelection);

    CHECK(controller.preferences().theme == ThemeMode::Dark);
    CHECK(controller.preferences().motion == MotionMode::Reduced);
    CHECK(controller.preferences().skip_backward_seconds == 7);
    CHECK(controller.preferences().skip_forward_seconds == 20);
    CHECK_FALSE(controller.preferences().restore_last_session);
    CHECK(controller.preferences().queue_end_behavior == QueueEndBehavior::StopClearSelection);

    CHECK_FALSE(controller.setSkipBackwardSeconds(0));
    CHECK_FALSE(controller.setSkipForwardSeconds(301));
    CHECK(controller.preferences().skip_backward_seconds == 7);
    CHECK(controller.preferences().skip_forward_seconds == 20);
}

TEST_CASE("volume and mute update state and the playback port", "[m0][qt][core][preferences][mute]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    PlayerController controller{backend, platform};

    REQUIRE(controller.setVolume(0.35));
    controller.toggleMute();

    CHECK(controller.preferences().volume == Catch::Approx(0.35));
    CHECK(controller.preferences().muted);
    REQUIRE(backend.volume_requests.size() == 1);
    CHECK(backend.volume_requests.front() == Catch::Approx(0.35));
    REQUIRE(backend.mute_requests.size() == 1);
    CHECK(backend.mute_requests.front());

    CHECK_FALSE(controller.setVolume(-0.1));
    CHECK_FALSE(controller.setVolume(1.1));
    CHECK_FALSE(controller.setVolume(std::numeric_limits<double>::quiet_NaN()));
    CHECK(controller.preferences().volume == Catch::Approx(0.35));
    CHECK(backend.volume_requests.size() == 1);
}

TEST_CASE("rejected volume command preserves preferences and the last accepted nonzero volume",
          "[m0][qt][core][preferences][volume][error]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    PlayerController controller{backend, platform};
    backend.volume_result = PlaybackCommandResult::Busy;

    CHECK_FALSE(controller.setVolume(0.35));
    CHECK(controller.preferences().volume == Catch::Approx(0.8));

    backend.volume_result = PlaybackCommandResult::Accepted;
    REQUIRE(controller.setVolume(0.0));
    REQUIRE(controller.toggleMute());
    REQUIRE(controller.toggleMute());

    CHECK_FALSE(controller.preferences().muted);
    CHECK(controller.preferences().volume == Catch::Approx(0.8));
    CHECK((backend.volume_requests == std::vector<double>{0.35, 0.0, 0.8}));
}

TEST_CASE("rejected mute command preserves preferences", "[m0][qt][core][preferences][mute][error]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    PlayerController controller{backend, platform};
    backend.mute_result = PlaybackCommandResult::Shutdown;
    const auto expected = controller.preferences();

    CHECK_FALSE(controller.toggleMute());

    checkUnchangedPreferences(controller.preferences(), expected);
    CHECK(backend.mute_requests == std::vector<bool>{true});
}

TEST_CASE("rejected unmute sequence preserves zero-volume preferences", "[m0][qt][core][preferences][mute][error]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    PlayerController controller{backend, platform};
    REQUIRE(controller.setVolume(0.35));
    REQUIRE(controller.toggleMute());
    REQUIRE(controller.setVolume(0.0));
    const auto expected = controller.preferences();

    SECTION("when restoring the logical volume is rejected") {
        backend.volume_result = PlaybackCommandResult::Busy;

        CHECK_FALSE(controller.toggleMute());

        checkUnchangedPreferences(controller.preferences(), expected);
        CHECK(backend.mute_requests == std::vector<bool>{true});
    }

    SECTION("when the unmute command is rejected after restoring the backend volume") {
        backend.mute_result = PlaybackCommandResult::Shutdown;

        CHECK_FALSE(controller.toggleMute());

        checkUnchangedPreferences(controller.preferences(), expected);
        CHECK((backend.mute_requests == std::vector<bool>{true, false}));
        CHECK((backend.volume_requests == std::vector<double>{0.35, 0.0, 0.35, 0.0}));
        CHECK(backend.backend_volume == 0.0);
        CHECK(backend.backend_muted);
    }
}

TEST_CASE("unmute restores the last nonzero logical volume", "[m0][qt][core][preferences][mute]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    PlayerController controller{backend, platform};

    REQUIRE(controller.setVolume(0.35));
    REQUIRE(controller.setVolume(0.0));
    controller.toggleMute();
    controller.toggleMute();

    CHECK_FALSE(controller.preferences().muted);
    CHECK(controller.preferences().volume == Catch::Approx(0.35));
    REQUIRE(backend.volume_requests.size() == 3);
    CHECK(backend.volume_requests.back() == Catch::Approx(0.35));
}

TEST_CASE("unmute restores the loaded nonzero volume or the safe default after loaded zero",
          "[m0][qt][core][preferences][mute][restore]") {
    const double loaded_volume = GENERATE(0.0, 0.35);
    const double expected_unmuted_volume = loaded_volume > 0.0 ? loaded_volume : Preferences{}.volume;
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    Preferences persisted = persistedPreferences();
    persisted.volume = loaded_volume;
    persisted.muted = true;
    PlayerController controller{backend, platform, {}, persisted};

    if (loaded_volume > 0.0)
        REQUIRE(controller.setVolume(0.0));
    REQUIRE(controller.toggleMute());

    CHECK_FALSE(controller.preferences().muted);
    CHECK(controller.preferences().volume == Catch::Approx(expected_unmuted_volume));
    REQUIRE(backend.volume_requests.size() == (loaded_volume > 0.0 ? 2 : 1));
    CHECK(backend.volume_requests.back() == Catch::Approx(expected_unmuted_volume));
    CHECK(backend.mute_requests == std::vector<bool>{false});
}

TEST_CASE("start minimized setter requires tray only while enabling", "[m0][qt][core][preferences][tray]") {
    RecordingPlaybackBackend backend;

    SECTION("enabling is rejected when tray is unavailable") {
        RecordingPlatformIntegration platform{false};
        PlayerController controller{backend, platform};

        CHECK_FALSE(controller.setStartMinimized(true));
        CHECK_FALSE(controller.preferences().start_minimized);
    }

    SECTION("disabling remains available after tray capability is lost") {
        RecordingPlatformIntegration platform{true};
        PlayerController controller{backend, platform};
        REQUIRE(controller.setStartMinimized(true));
        platform.tray_available_ = false;

        REQUIRE(controller.setStartMinimized(false));
        CHECK_FALSE(controller.preferences().start_minimized);
    }
}

TEST_CASE("explicit hide uses the tray regardless of the close preference", "[m0][qt][core][lifecycle][tray]") {
    const auto behavior = GENERATE(CloseBehavior::MinimizeToTray, CloseBehavior::Exit);
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    PlayerController controller{backend, platform};
    controller.setCloseBehavior(behavior);
    const auto preferences = controller.preferences();

    CHECK(controller.requestHide() == CloseDisposition::HiddenToTray);

    CHECK(platform.hide_calls == 1);
    CHECK(platform.minimize_calls == 0);
    CHECK(platform.exit_calls == 0);
    CHECK(platform.restore_calls == 0);
    checkUnchangedPreferences(controller.preferences(), preferences);
}

TEST_CASE("explicit hide minimizes without a tray regardless of the close preference",
          "[m0][qt][core][lifecycle][tray]") {
    const auto behavior = GENERATE(CloseBehavior::MinimizeToTray, CloseBehavior::Exit);
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{false};
    PlayerController controller{backend, platform};
    controller.setCloseBehavior(behavior);
    const auto preferences = controller.preferences();

    CHECK(controller.requestHide() == CloseDisposition::Minimized);

    CHECK(platform.hide_calls == 0);
    CHECK(platform.minimize_calls == 1);
    CHECK(platform.exit_calls == 0);
    CHECK(platform.restore_calls == 0);
    checkUnchangedPreferences(controller.preferences(), preferences);
}

TEST_CASE("explicit exit ignores the close preference and tray availability", "[m0][qt][core][lifecycle][tray]") {
    const bool tray_available = GENERATE(false, true);
    const auto behavior = GENERATE(CloseBehavior::MinimizeToTray, CloseBehavior::Exit);
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{tray_available};
    PlayerController controller{backend, platform};
    controller.setCloseBehavior(behavior);
    const auto preferences = controller.preferences();

    controller.requestExit();

    CHECK(platform.exit_calls == 1);
    CHECK(platform.hide_calls == 0);
    CHECK(platform.minimize_calls == 0);
    CHECK(platform.restore_calls == 0);
    checkUnchangedPreferences(controller.preferences(), preferences);
}

TEST_CASE("close hides to an available tray without exiting", "[m0][qt][core][close][tray]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    PlayerController controller{backend, platform};

    const auto disposition = controller.requestClose();

    CHECK(disposition == CloseDisposition::HiddenToTray);
    CHECK(platform.hide_calls == 1);
    CHECK(platform.minimize_calls == 0);
    CHECK(platform.exit_calls == 0);
}

TEST_CASE("close minimizes when tray is unavailable", "[m0][qt][core][close][tray]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{false};
    PlayerController controller{backend, platform};

    const auto disposition = controller.requestClose();

    CHECK(disposition == CloseDisposition::Minimized);
    CHECK(platform.hide_calls == 0);
    CHECK(platform.minimize_calls == 1);
    CHECK(platform.exit_calls == 0);
}

TEST_CASE("exit close preference requests a real application exit", "[m0][qt][core][close]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    PlayerController controller{backend, platform};
    controller.setCloseBehavior(CloseBehavior::Exit);

    const auto disposition = controller.requestClose();

    CHECK(disposition == CloseDisposition::ExitRequested);
    CHECK(platform.hide_calls == 0);
    CHECK(platform.minimize_calls == 0);
    CHECK(platform.exit_calls == 1);
}

TEST_CASE("restore activates the window through the platform port", "[m0][qt][core][tray]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{true};
    PlayerController controller{backend, platform, playingSnapshot()};

    controller.requestRestore();

    CHECK(platform.restore_calls == 1);
    CHECK(controller.confirmedSnapshot().position_ms == 84'000);
}

TEST_CASE("window lifecycle preserves playback preferences and a pending seek", "[m0][qt][core][lifecycle][seek]") {
    const bool tray_available = GENERATE(false, true);
    const auto behavior = GENERATE(CloseBehavior::MinimizeToTray, CloseBehavior::Exit);
    const auto status = GENERATE(PlaybackStatus::Ready, PlaybackStatus::Paused, PlaybackStatus::Playing);
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration platform{tray_available};
    const PlaybackSnapshot initial{status, 84'000, 248'000, true};
    PlayerController controller{backend, platform, initial};
    controller.setTheme(ThemeMode::Dark);
    controller.setMotionMode(MotionMode::Reduced);
    REQUIRE(controller.setVolume(0.35));
    controller.toggleMute();
    REQUIRE(controller.setSkipBackwardSeconds(7));
    REQUIRE(controller.setSkipForwardSeconds(20));
    controller.setCloseBehavior(behavior);
    controller.setRestoreLastSession(false);
    REQUIRE(controller.setStartMinimized(tray_available));
    controller.setQueueEndBehavior(QueueEndBehavior::StopClearSelection);
    controller.beginSeekPreview(124'000);
    controller.commitSeekPreview();
    REQUIRE(controller.seekPreviewActive());
    REQUIRE(controller.seekPending());
    const auto preferences = controller.preferences();

    const auto check_preserved = [&] {
        checkUnchangedPreferences(controller.preferences(), preferences);
        CHECK(controller.confirmedSnapshot().status == initial.status);
        CHECK(controller.confirmedSnapshot().position_ms == initial.position_ms);
        CHECK(controller.confirmedSnapshot().duration_ms == initial.duration_ms);
        CHECK(controller.confirmedSnapshot().seekable == initial.seekable);
        CHECK(controller.displayedPositionMs() == 124'000);
        CHECK(controller.seekPreviewActive());
        CHECK(controller.seekPending());
        CHECK(backend.play_requests == 0);
        CHECK(backend.pause_requests == 0);
        CHECK(backend.seek_requests == std::vector<std::int64_t>{124'000});
        CHECK(backend.volume_requests == std::vector<double>{0.35});
        CHECK(backend.mute_requests == std::vector<bool>{true});
    };

    const auto hidden = tray_available ? CloseDisposition::HiddenToTray : CloseDisposition::Minimized;
    CHECK(controller.requestHide() == hidden);
    check_preserved();
    controller.requestRestore();
    check_preserved();
    const auto closed = behavior == CloseBehavior::Exit ? CloseDisposition::ExitRequested : hidden;
    CHECK(controller.requestClose() == closed);
    check_preserved();
    controller.requestExit();
    check_preserved();

    CHECK(platform.restore_calls == 1);
    const int hides = behavior == CloseBehavior::Exit ? 1 : 2;
    CHECK(platform.hide_calls == (tray_available ? hides : 0));
    CHECK(platform.minimize_calls == (tray_available ? 0 : hides));
    CHECK(platform.exit_calls == (behavior == CloseBehavior::Exit ? 2 : 1));

    // The original seek target must still acknowledge normally after all window actions.
    controller.applyBackendSnapshot({status, 124'000, 248'000, true});
    CHECK_FALSE(controller.seekPending());
    CHECK_FALSE(controller.seekPreviewActive());
    CHECK(controller.confirmedSnapshot().status == status);
    CHECK(controller.confirmedSnapshot().position_ms == 124'000);
    CHECK(controller.displayedPositionMs() == 124'000);
}

} // namespace
} // namespace music_player::m0::test
