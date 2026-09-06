#include "m0/adapters/DeterministicPlaybackBackend.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <vector>

namespace music_player::m0::test {

TEST_CASE("deterministic backend starts from the frozen playing snapshot", "[m0][qt][core][backend]") {
    const DeterministicPlaybackBackend backend;

    CHECK(backend.snapshot().status == PlaybackStatus::Playing);
    CHECK(backend.snapshot().position_ms == 84'000);
    CHECK(backend.snapshot().duration_ms == 248'000);
    CHECK(backend.snapshot().seekable);
    CHECK(backend.volume() == Catch::Approx(0.8));
    CHECK_FALSE(backend.muted());
}

TEST_CASE("manual ticks and transport commands are deterministic", "[m0][qt][core][backend]") {
    DeterministicPlaybackBackend backend;

    CHECK(backend.tick().snapshot.position_ms == 84'200);
    backend.pause();
    CHECK(backend.tick().snapshot.position_ms == 84'200);
    CHECK(backend.snapshot().status == PlaybackStatus::Paused);
    backend.play();
    CHECK(backend.tick().snapshot.position_ms == 84'400);
}

TEST_CASE("reaching the end clamps and pauses", "[m0][qt][core][backend][end]") {
    DeterministicPlaybackBackend near_end{{PlaybackStatus::Playing, 247'900, 248'000, true}};

    const auto ended = near_end.tick();

    CHECK(ended.snapshot.position_ms == 248'000);
    CHECK(ended.snapshot.status == PlaybackStatus::Paused);
    CHECK(near_end.tick().snapshot.position_ms == 248'000);
    CHECK(near_end.snapshot().status == PlaybackStatus::Paused);
}

TEST_CASE("tick saturates before signed overflow", "[m0][qt][core][backend][end]") {
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    DeterministicPlaybackBackend backend{
        {.initial_snapshot = {PlaybackStatus::Playing, maximum - 100, maximum, true}, .tick_interval_ms = 200}};

    const auto ended = backend.tick();

    CHECK(ended.snapshot.position_ms == maximum);
    CHECK(ended.snapshot.status == PlaybackStatus::Paused);
}

TEST_CASE("the first seek wins and acknowledgement consumes a tick", "[m0][qt][core][backend][seek]") {
    DeterministicPlaybackBackend backend;

    backend.seekTo(120'000);
    backend.seekTo(160'000);
    CHECK(backend.hasPendingSeek());
    const auto acknowledged = backend.tick();
    CHECK(acknowledged.snapshot.position_ms == 120'000);
    CHECK_FALSE(acknowledged.seek_rejected);
    CHECK_FALSE(backend.hasPendingSeek());
    CHECK(backend.tick().snapshot.position_ms == 120'200);
}

TEST_CASE("backend seek acknowledgement preserves paused playback", "[m0][qt][core][backend][seek]") {
    DeterministicPlaybackBackend backend{{PlaybackStatus::Paused, 84'000, 248'000, true}};

    backend.seekTo(124'000);
    const auto acknowledged = backend.tick();

    CHECK(acknowledged.snapshot.position_ms == 124'000);
    CHECK(acknowledged.snapshot.status == PlaybackStatus::Paused);
    CHECK_FALSE(backend.hasPendingSeek());
}

TEST_CASE("an injected seek rejection preserves the snapshot and reports once", "[m0][qt][core][backend][seek]") {
    DeterministicPlaybackBackend backend;
    backend.rejectNextSeek();
    backend.seekTo(120'000);

    const auto rejected = backend.tick();
    CHECK(rejected.snapshot.position_ms == 84'000);
    CHECK(rejected.seek_rejected);
    const auto following = backend.tick();
    CHECK(following.snapshot.position_ms == 84'200);
    CHECK_FALSE(following.seek_rejected);
}

TEST_CASE("seek clamps to bounds and inactive states ignore transport", "[m0][qt][core][backend][seek]") {
    DeterministicPlaybackBackend backend;
    backend.seekTo(-1);
    REQUIRE(backend.hasPendingSeek());
    CHECK(backend.tick().snapshot.position_ms == 0);

    backend.seekTo(std::numeric_limits<std::int64_t>::max());
    REQUIRE(backend.hasPendingSeek());
    const auto at_end = backend.tick();
    CHECK(at_end.snapshot.position_ms == 248'000);
    CHECK(at_end.snapshot.status == PlaybackStatus::Paused);

    DeterministicPlaybackBackend inactive{{PlaybackStatus::Error, 10, 100, true}};
    inactive.seekTo(50);
    inactive.play();
    inactive.pause();
    CHECK_FALSE(inactive.hasPendingSeek());
    CHECK(inactive.snapshot().status == PlaybackStatus::Error);
    CHECK(inactive.snapshot().position_ms == 10);
}

TEST_CASE("ready and paused snapshots can transition to playing", "[m0][qt][core][backend]") {
    DeterministicPlaybackBackend ready{{PlaybackStatus::Ready, 0, 248'000, true}};
    ready.play();
    CHECK(ready.snapshot().status == PlaybackStatus::Playing);

    DeterministicPlaybackBackend paused{{PlaybackStatus::Paused, 84'000, 248'000, true}};
    paused.play();
    CHECK(paused.snapshot().status == PlaybackStatus::Playing);
}

TEST_CASE("invalid commands are inert while volume and mute remain controllable", "[m0][qt][core][backend]") {
    DeterministicPlaybackBackend backend;
    backend.setVolume(-0.1);
    backend.setVolume(1.1);
    backend.setVolume(std::numeric_limits<double>::quiet_NaN());
    CHECK(backend.volume() == Catch::Approx(0.8));

    backend.setVolume(0.35);
    backend.setMuted(true);
    CHECK(backend.volume() == Catch::Approx(0.35));
    CHECK(backend.muted());
}

TEST_CASE("invalid deterministic backend configuration is rejected", "[m0][qt][core][backend]") {
    CHECK_THROWS_AS(DeterministicPlaybackBackend({.initial_snapshot = {PlaybackStatus::Playing, -1, 248'000, true}}),
                    std::invalid_argument);
    CHECK_THROWS_AS(DeterministicPlaybackBackend({.initial_snapshot = {PlaybackStatus::Playing, 1, 0, true}}),
                    std::invalid_argument);
    CHECK_THROWS_AS(DeterministicPlaybackBackend({.tick_interval_ms = 0}), std::invalid_argument);
    CHECK_THROWS_AS(DeterministicPlaybackBackend({.initial_volume = 1.1}), std::invalid_argument);
}

TEST_CASE("candidate unavailable scenario restores its initial snapshot", "[m0][qt][core][backend]") {
    DeterministicPlaybackBackend backend;
    REQUIRE(backend.enterUnavailableScenario());
    CHECK(backend.snapshot().status == PlaybackStatus::Error);
    CHECK(backend.snapshot().position_ms == 0);
    CHECK(backend.snapshot().duration_ms == 0);
    CHECK_FALSE(backend.snapshot().seekable);
    CHECK(backend.tick().snapshot.status == PlaybackStatus::Error);
    CHECK_FALSE(backend.enterUnavailableScenario());
    REQUIRE(backend.restoreInitialScenario());
    CHECK(backend.snapshot().status == PlaybackStatus::Playing);
    CHECK(backend.snapshot().position_ms == 84'000);
    CHECK(backend.snapshot().duration_ms == 248'000);
    CHECK(backend.snapshot().seekable);
    CHECK_FALSE(backend.restoreInitialScenario());
}

TEST_CASE("track selection starts the requested transport state at zero", "[m0][qt][core][backend][select-track]") {
    for (const auto status : {PlaybackStatus::Ready, PlaybackStatus::Paused, PlaybackStatus::Playing}) {
        CAPTURE(static_cast<int>(status));
        DeterministicPlaybackBackend backend{{PlaybackStatus::Error, 19, 99, false}};
        backend.setVolume(0.35);
        backend.setMuted(true);

        REQUIRE(backend.selectTrack(90'000, status));

        CHECK(backend.snapshot().status == status);
        CHECK(backend.snapshot().position_ms == 0);
        CHECK(backend.snapshot().duration_ms == 90'000);
        CHECK(backend.snapshot().seekable);
        CHECK(backend.volume() == Catch::Approx(0.35));
        CHECK(backend.muted());
        CHECK_FALSE(backend.hasPendingSeek());
        const auto following = backend.tick();
        CHECK(following.snapshot.status == status);
        CHECK(following.snapshot.position_ms == (status == PlaybackStatus::Playing ? 200 : 0));
        CHECK_FALSE(following.seek_rejected);
    }
}

TEST_CASE("track selection accepts the positive duration boundaries", "[m0][qt][core][backend][select-track]") {
    for (const auto duration_ms : {std::int64_t{1}, std::numeric_limits<std::int64_t>::max()}) {
        CAPTURE(duration_ms);
        DeterministicPlaybackBackend backend;

        REQUIRE(backend.selectTrack(duration_ms, PlaybackStatus::Paused));

        CHECK(backend.snapshot().position_ms == 0);
        CHECK(backend.snapshot().duration_ms == duration_ms);
        CHECK(backend.snapshot().seekable);
    }
}

TEST_CASE("invalid track selection preserves the snapshot and pending rejection",
          "[m0][qt][core][backend][select-track]") {
    struct InvalidSelection {
        std::int64_t duration_ms;
        PlaybackStatus status;
    };
    for (const auto input : {
             InvalidSelection{0, PlaybackStatus::Playing},
             InvalidSelection{-1, PlaybackStatus::Playing},
             InvalidSelection{std::numeric_limits<std::int64_t>::min(), PlaybackStatus::Playing},
             InvalidSelection{90'000, PlaybackStatus::Empty},
             InvalidSelection{90'000, PlaybackStatus::Loading},
             InvalidSelection{90'000, PlaybackStatus::Error},
             InvalidSelection{90'000, static_cast<PlaybackStatus>(99)},
         }) {
        CAPTURE(input.duration_ms, static_cast<int>(input.status));
        DeterministicPlaybackBackend backend;
        backend.setVolume(0.35);
        backend.setMuted(true);
        backend.rejectNextSeek();
        backend.seekTo(120'000);
        REQUIRE(backend.hasPendingSeek());
        const auto before = backend.snapshot();

        CHECK_FALSE(backend.selectTrack(input.duration_ms, input.status));

        CHECK(backend.snapshot().status == before.status);
        CHECK(backend.snapshot().position_ms == before.position_ms);
        CHECK(backend.snapshot().duration_ms == before.duration_ms);
        CHECK(backend.snapshot().seekable == before.seekable);
        CHECK(backend.volume() == Catch::Approx(0.35));
        CHECK(backend.muted());
        CHECK(backend.hasPendingSeek());
        const auto rejected = backend.tick();
        CHECK(rejected.seek_rejected);
        CHECK(rejected.snapshot.position_ms == before.position_ms);
        CHECK_FALSE(backend.hasPendingSeek());
        backend.seekTo(60'000);
        const auto acknowledged = backend.tick();
        CHECK_FALSE(acknowledged.seek_rejected);
        CHECK(acknowledged.snapshot.position_ms == 60'000);
    }
}

TEST_CASE("track selection discards the previous item pending seek and rejection",
          "[m0][qt][core][backend][select-track]") {
    for (const bool previous_seek_pending : {false, true}) {
        for (const bool previous_rejection_armed : {false, true}) {
            CAPTURE(previous_seek_pending, previous_rejection_armed);
            DeterministicPlaybackBackend backend;
            if (previous_seek_pending)
                backend.seekTo(120'000);
            if (previous_rejection_armed)
                backend.rejectNextSeek();

            REQUIRE(backend.selectTrack(65'000, PlaybackStatus::Paused));

            CHECK_FALSE(backend.hasPendingSeek());
            const auto following = backend.tick();
            CHECK(following.snapshot.position_ms == 0);
            CHECK_FALSE(following.seek_rejected);
            backend.seekTo(12'000);
            REQUIRE(backend.hasPendingSeek());
            const auto acknowledged = backend.tick();
            CHECK(acknowledged.snapshot.status == PlaybackStatus::Paused);
            CHECK(acknowledged.snapshot.position_ms == 12'000);
            CHECK_FALSE(acknowledged.seek_rejected);
            CHECK_FALSE(backend.hasPendingSeek());
        }
    }
}

TEST_CASE("restoring after track selection and error retains the original seed",
          "[m0][qt][core][backend][select-track]") {
    DeterministicPlaybackBackend backend;
    backend.setVolume(0.35);
    backend.setMuted(true);
    REQUIRE(backend.selectTrack(12'000, PlaybackStatus::Paused));
    backend.seekTo(6'000);
    backend.rejectNextSeek();
    REQUIRE(backend.enterUnavailableScenario());

    REQUIRE(backend.restoreInitialScenario());

    CHECK(backend.snapshot().status == PlaybackStatus::Playing);
    CHECK(backend.snapshot().position_ms == 84'000);
    CHECK(backend.snapshot().duration_ms == 248'000);
    CHECK(backend.snapshot().seekable);
    CHECK(backend.volume() == Catch::Approx(0.35));
    CHECK(backend.muted());
    CHECK_FALSE(backend.hasPendingSeek());
    const auto following = backend.tick();
    CHECK(following.snapshot.position_ms == 84'200);
    CHECK_FALSE(following.seek_rejected);
}

TEST_CASE("playback lifecycle is idempotent and rejects commands after shutdown",
          "[m0][qt][core][backend][lifecycle]") {
    DeterministicPlaybackBackend backend;

    CHECK(backend.unload() == PlaybackCommandResult::Accepted);
    CHECK(backend.unload() == PlaybackCommandResult::Accepted);
    CHECK(backend.snapshot().status == PlaybackStatus::Empty);

    CHECK(backend.load(std::filesystem::path{L"synthetic.wav"}) == PlaybackCommandResult::Accepted);
    CHECK(backend.snapshot().status == PlaybackStatus::Ready);
    CHECK(backend.snapshot().generation == 2);

    CHECK(backend.shutdown() == PlaybackCommandResult::Accepted);
    CHECK(backend.shutdown() == PlaybackCommandResult::Accepted);
    CHECK(backend.snapshot().status == PlaybackStatus::Empty);
    CHECK(backend.load(std::filesystem::path{L"another.wav"}) == PlaybackCommandResult::Shutdown);
    CHECK(backend.play() == PlaybackCommandResult::Shutdown);
    CHECK(backend.pause() == PlaybackCommandResult::Shutdown);
    CHECK(backend.seekTo(10) == PlaybackCommandResult::Shutdown);
    CHECK(backend.setVolume(0.25) == PlaybackCommandResult::Shutdown);
    CHECK(backend.setMuted(true) == PlaybackCommandResult::Shutdown);
    CHECK(backend.unload() == PlaybackCommandResult::Shutdown);
}

TEST_CASE("pure playback events publish the final snapshot before ended exactly once",
          "[m0][qt][core][backend][events][end]") {
    DeterministicPlaybackBackend backend{
        {.initial_snapshot = {PlaybackStatus::Playing, 900, 1'000, true}, .tick_interval_ms = 200}};
    std::vector<PlaybackEvent> events;
    backend.setEventCallback([&](const PlaybackEvent& event) { events.push_back(event); });

    (void)backend.tick();

    REQUIRE(events.size() == 2);
    CHECK(events[0].type == PlaybackEventType::SnapshotChanged);
    CHECK(events[0].snapshot.status == PlaybackStatus::Paused);
    CHECK(events[0].snapshot.position_ms == events[0].snapshot.duration_ms);
    CHECK(events[1].type == PlaybackEventType::Ended);
    CHECK(events[1].snapshot == events[0].snapshot);

    (void)backend.tick();
    CHECK(events.size() == 2);
}

TEST_CASE("reentrant replacement discards ended from the superseded generation",
          "[m0][qt][core][backend][events][generation]") {
    DeterministicPlaybackBackend backend{
        {.initial_snapshot = {PlaybackStatus::Playing, 900, 1'000, true}, .tick_interval_ms = 200}};
    std::vector<PlaybackEvent> events;
    bool replacement_loaded = false;
    backend.setEventCallback([&](const PlaybackEvent& event) {
        events.push_back(event);
        if (!replacement_loaded && event.type == PlaybackEventType::SnapshotChanged &&
            event.snapshot.status == PlaybackStatus::Paused && event.snapshot.position_ms == 1'000) {
            replacement_loaded = true;
            CHECK(backend.load(std::filesystem::path{L"replacement.wav"}) == PlaybackCommandResult::Accepted);
        }
    });

    (void)backend.tick();

    REQUIRE(replacement_loaded);
    CHECK(backend.snapshot().status == PlaybackStatus::Ready);
    CHECK(backend.snapshot().generation == 1);
    CHECK(std::none_of(events.cbegin(), events.cend(), [](const PlaybackEvent& event) {
        return event.type == PlaybackEventType::Ended && event.snapshot.generation == 0;
    }));
}

} // namespace music_player::m0::test
