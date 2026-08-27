#include "m0/application/PlayerController.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstdint>
#include <limits>
#include <vector>

namespace music_player::m0::test {
namespace {

class RecordingPlaybackBackend final : public PlaybackBackend {
  public:
    void play() override { ++play_requests; }

    void pause() override { ++pause_requests; }

    void seekTo(const std::int64_t position_ms) override { seek_requests.push_back(position_ms); }

    void setVolume(const double volume) override { volume_requests.push_back(volume); }

    void setMuted(const bool muted) override { mute_requests.push_back(muted); }

    int play_requests{0};
    int pause_requests{0};
    std::vector<std::int64_t> seek_requests{};
    std::vector<double> volume_requests{};
    std::vector<bool> mute_requests{};
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

TEST_CASE("start minimized is rejected without an available tray", "[m0][qt][core][preferences][tray]") {
    RecordingPlaybackBackend backend;
    RecordingPlatformIntegration unavailable_platform{false};
    PlayerController unavailable_controller{backend, unavailable_platform};

    CHECK_FALSE(unavailable_controller.setStartMinimized(true));
    CHECK_FALSE(unavailable_controller.preferences().start_minimized);

    RecordingPlatformIntegration available_platform{true};
    PlayerController available_controller{backend, available_platform};
    REQUIRE(available_controller.setStartMinimized(true));
    CHECK(available_controller.preferences().start_minimized);
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

} // namespace
} // namespace music_player::m0::test
