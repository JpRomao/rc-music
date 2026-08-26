#include "m0/adapters/DeterministicPlaybackBackend.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <stdexcept>

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

} // namespace music_player::m0::test
