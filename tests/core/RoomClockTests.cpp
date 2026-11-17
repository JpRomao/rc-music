#include "m0/domain/ListeningRoomLimits.hpp"
#include "m0/domain/RoomClock.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace music_player::m0;

TEST_CASE("clock offset comes from the minimum round-trip sample", "[room-clock]") {
    RoomClockEstimator clock;
    REQUIRE_FALSE(clock.offsetMs().has_value());
    REQUIRE_FALSE(clock.roundTripMs().has_value());
    clock.add({0, 1050, 100});
    clock.add({200, 1220, 240});
    CHECK(clock.offsetMs() == 1000);
    CHECK(clock.roundTripMs() == 40);
}

TEST_CASE("clock keeps only the last samples", "[room-clock]") {
    RoomClockEstimator clock;
    clock.add({0, 5, 2}); // RTT 2, offset 4: the best, but it ages out
    for (std::uint32_t i = 0; i < room_clock_samples; ++i) {
        clock.add({100, 1000, 110});
    }
    CHECK(clock.roundTripMs() == 10);
    CHECK(clock.offsetMs() == 1000 - 105);
}

TEST_CASE("clock ignores negative round trips and breaks ties by recency", "[room-clock]") {
    RoomClockEstimator clock;
    clock.add({100, 0, 50});
    CHECK_FALSE(clock.offsetMs().has_value());
    clock.add({0, 500, 20});
    clock.add({100, 700, 120});
    CHECK(clock.offsetMs() == 700 - 110);
}

TEST_CASE("clock offset floors odd sums toward negative infinity", "[room-clock]") {
    RoomClockEstimator clock;
    clock.add({0, 10, 5}); // sum 5 -> floor 2
    CHECK(clock.offsetMs() == 8);
    RoomClockEstimator negative;
    negative.add({-5, 0, 0}); // sum -5 -> floor -3
    CHECK(negative.offsetMs() == 3);
}

TEST_CASE("expected position follows the host clock", "[room-clock]") {
    const RoomPlayback playing{1, true, 5000, 30000};
    CHECK(expectedPositionMs(playing, 5250) == 30250);
    CHECK(expectedPositionMs({1, false, 5000, 30000}, 9000) == 30000);
    CHECK(expectedPositionMs({std::nullopt, true, 5000, 30000}, 5100) == 30100);
    CHECK(expectedPositionMs({1, true, 5000, 100}, 4000) == 0);
}

TEST_CASE("drift classification uses the magnitude and exact boundaries", "[room-clock]") {
    CHECK(classifyDrift(30, 40, 120) == DriftCorrection::None);
    CHECK(classifyDrift(-39, 40, 120) == DriftCorrection::None);
    CHECK(classifyDrift(40, 40, 120) == DriftCorrection::Nudge);
    CHECK(classifyDrift(-60, 40, 120) == DriftCorrection::Nudge);
    CHECK(classifyDrift(119, 40, 120) == DriftCorrection::Nudge);
    CHECK(classifyDrift(120, 40, 120) == DriftCorrection::Seek);
    CHECK(classifyDrift(-200, 40, 120) == DriftCorrection::Seek);
}
