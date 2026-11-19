// RoomPlaybackFollower (S9.1) over a fake clock, a fake scheduler and a deterministic, silent player.
#include "m0/application/RoomPlaybackFollower.hpp"
#include "m0/domain/ListeningRoomLimits.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace music_player::m0::test {
namespace {

constexpr std::int64_t host_offset_ms = 50'000;
constexpr std::int64_t track_ms = 240'000;

// Plays nothing: the position moves only when the harness moves the clock, at rate 1.0 whatever rate
// was asked (so a nudge never changes the measured drift here).
class FakeBackend final : public PlaybackBackend {
  public:
    explicit FakeBackend(const std::int64_t& now) : now_(now) {}

    [[nodiscard]] const PlaybackSnapshot& snapshot() const noexcept override { return snapshot_; }
    void setEventCallback(PlaybackEventCallback) override {}
    PlaybackCommandResult load(const std::filesystem::path& source_path) override {
        loads.push_back(source_path);
        snapshot_ = {loads_ready ? PlaybackStatus::Ready : PlaybackStatus::Loading, 0, loads_ready ? track_ms : 0,
                     loads_ready, snapshot_.generation + 1};
        updated_at = now_;
        rate = 1.0;
        return PlaybackCommandResult::Accepted;
    }
    void finishLoading() {
        snapshot_.status = PlaybackStatus::Ready;
        snapshot_.duration_ms = track_ms;
        snapshot_.seekable = true;
    }
    PlaybackCommandResult play() override {
        if (!ready())
            return PlaybackCommandResult::NotReady;
        plays.push_back(now_);
        snapshot_.status = PlaybackStatus::Playing;
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult pause() override {
        if (!ready())
            return PlaybackCommandResult::NotReady;
        ++pauses;
        snapshot_.status = PlaybackStatus::Paused;
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult seekTo(const std::int64_t position_ms) override {
        if (!ready())
            return PlaybackCommandResult::NotReady;
        seeks.push_back(position_ms);
        snapshot_.position_ms = std::clamp<std::int64_t>(position_ms, 0, snapshot_.duration_ms);
        updated_at = now_;
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult setVolume(double) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult setMuted(bool) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult unload() override {
        ++unloads;
        snapshot_ = {PlaybackStatus::Empty, 0, 0, false, snapshot_.generation + 1};
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult shutdown() override { return unload(); }
    PlaybackCommandResult setPlaybackRate(const double next) override {
        ++rate_requests;
        if (!rate_supported)
            return PlaybackCommandResult::Unsupported;
        rates.push_back(next);
        rate = next;
        return PlaybackCommandResult::Accepted;
    }
    [[nodiscard]] std::optional<std::int64_t> positionUpdatedAtMs() const noexcept override { return updated_at; }

    // The harness moved the clock by ms.
    void elapse(const std::int64_t ms) {
        if (snapshot_.status != PlaybackStatus::Playing || ms <= 0)
            return;
        snapshot_.position_ms = std::min(snapshot_.position_ms + ms, snapshot_.duration_ms);
        if (!position_frozen)
            updated_at = now_ + ms;
    }
    // The position jumps by ms without anything being asked (spontaneous drift).
    void jump(const std::int64_t ms) { snapshot_.position_ms += ms; }

    std::vector<std::filesystem::path> loads;
    std::vector<std::int64_t> plays;
    std::vector<std::int64_t> seeks;
    std::vector<double> rates;
    int pauses{0};
    int unloads{0};
    int rate_requests{0};
    double rate{1.0};
    bool rate_supported{true};
    bool loads_ready{true};
    // A stalled main thread: the position stops being updated although the track plays.
    bool position_frozen{false};
    std::optional<std::int64_t> updated_at;

  private:
    [[nodiscard]] bool ready() const noexcept {
        return snapshot_.status == PlaybackStatus::Ready || snapshot_.status == PlaybackStatus::Paused ||
               snapshot_.status == PlaybackStatus::Playing;
    }

    const std::int64_t& now_;
    PlaybackSnapshot snapshot_{};
};

struct Harness final {
    struct Task final {
        std::int64_t due{};
        std::uint64_t order{};
        std::function<void()> run;
    };

    // The host's clock is local + host_offset_ms, measured over a 10 ms round trip.
    void syncClock() { follower.addClockSample({now, now + host_offset_ms + 5, now + 10}); }
    [[nodiscard]] std::int64_t hostNow() const { return now + host_offset_ms; }
    [[nodiscard]] std::int64_t expected(const RoomPlayback& playback) const {
        return expectedPositionMs(playback, hostNow() + follower.outputOffsetMs());
    }
    // Moves the local clock, running due tasks in order (the player moves with it).
    void advance(const std::int64_t ms) {
        const auto end = now + ms;
        for (;;) {
            const auto next = std::min_element(tasks.begin(), tasks.end(), [](const Task& a, const Task& b) {
                return a.due != b.due ? a.due < b.due : a.order < b.order;
            });
            if (next == tasks.end() || next->due > end)
                break;
            auto task = std::move(*next);
            tasks.erase(next);
            backend.elapse(task.due - now);
            now = task.due;
            task.run();
        }
        backend.elapse(end - now);
        now = end;
    }
    // One tick every room_drift_check_ms, as the app's timer does.
    void tickFor(const int ticks) {
        for (int index = 0; index < ticks; ++index) {
            advance(room_drift_check_ms);
            follower.tick();
        }
    }

    std::int64_t now{1'000};
    std::uint64_t next_order{0};
    std::vector<Task> tasks;
    FakeBackend backend{now};
    RoomPlaybackFollower follower{backend, [this] { return now; }, room_drift_nudge_ms, room_drift_seek_ms,
                                  [this](const std::int64_t delay_ms, std::function<void()> task) {
                                      tasks.push_back({now + delay_ms, next_order++, std::move(task)});
                                  }};
};

const std::filesystem::path song{"room-cache/aaaa"};
const std::filesystem::path other_song{"room-cache/bbbb"};

RoomPlayback playing(const std::uint64_t item, const std::int64_t anchor_host_ms,
                     const std::int64_t anchor_position_ms) {
    return RoomPlayback{item, true, anchor_host_ms, anchor_position_ms};
}

RoomPlayback paused(const std::uint64_t item, const std::int64_t anchor_host_ms,
                    const std::int64_t anchor_position_ms) {
    return RoomPlayback{item, false, anchor_host_ms, anchor_position_ms};
}

// A track that started 5 s ago (host time) at 10 s, already followed.
RoomPlayback followRunningTrack(Harness& h) {
    h.syncClock();
    const auto playback = playing(1, h.hostNow() - 5'000, 10'000);
    h.follower.apply(playback, song);
    REQUIRE(h.backend.snapshot().status == PlaybackStatus::Playing);
    REQUIRE(h.backend.snapshot().position_ms == 15'000);
    h.backend.seeks.clear();
    return playback;
}

} // namespace

TEST_CASE("A scheduled play does not start before its instant and starts at the anchor position", "[room][follower]") {
    Harness h;
    h.syncClock();
    const auto start_host = h.hostNow() + room_play_lead_ms;
    h.follower.apply(playing(1, start_host, 10'000), song);

    REQUIRE(h.backend.loads == std::vector{song});
    h.advance(room_play_lead_ms - 1);
    CHECK(h.backend.plays.empty());
    CHECK(h.backend.snapshot().status != PlaybackStatus::Playing);

    h.advance(1);
    REQUIRE(h.backend.plays == std::vector<std::int64_t>{start_host - host_offset_ms});
    CHECK(h.backend.loads == std::vector{song});
    CHECK(h.backend.snapshot().status == PlaybackStatus::Playing);
    CHECK(h.backend.snapshot().position_ms == 10'000);
}

TEST_CASE("A positive output offset plays that much earlier", "[room][follower]") {
    Harness h;
    h.syncClock();
    h.follower.setOutputOffsetMs(100);
    const auto start_host = h.hostNow() + room_play_lead_ms;
    h.follower.apply(playing(1, start_host, 10'000), song);

    h.advance(room_play_lead_ms - 100 - 1);
    CHECK(h.backend.plays.empty());
    h.advance(1);
    REQUIRE(h.backend.plays == std::vector<std::int64_t>{start_host - host_offset_ms - 100});
    CHECK(h.backend.snapshot().position_ms == 10'000);
    // 100 ms later the host reaches the anchor; this guest is already 100 ms into it.
    h.advance(100);
    CHECK(h.backend.snapshot().position_ms == 10'100);
}

TEST_CASE("The output offset is clamped to its limits", "[room][follower]") {
    Harness h;
    h.follower.setOutputOffsetMs(maximum_output_offset_ms + 1);
    CHECK(h.follower.outputOffsetMs() == maximum_output_offset_ms);
    h.follower.setOutputOffsetMs(minimum_output_offset_ms - 1);
    CHECK(h.follower.outputOffsetMs() == minimum_output_offset_ms);
}

TEST_CASE("A drift of 200 ms is corrected by one seek to the expected position", "[room][follower]") {
    Harness h;
    const auto playback = followRunningTrack(h);
    h.backend.jump(200);

    // S1: two evaluations in a row above the seek threshold, then one seek.
    h.tickFor(1);
    CHECK(h.backend.seeks.empty());
    h.tickFor(1);
    REQUIRE(h.backend.seeks == std::vector{h.expected(playback)});
    CHECK(h.backend.snapshot().position_ms == h.expected(playback));
    h.tickFor(3);
    CHECK(h.backend.seeks.size() == 1);
    CHECK(h.backend.rates.empty());
}

TEST_CASE("A drift of 60 ms nudges the rate for a limited time, one nudge at a time", "[room][follower]") {
    Harness h;
    followRunningTrack(h);

    SECTION("ahead: slower") {
        h.backend.jump(60);
        h.tickFor(1);
        REQUIRE(h.backend.rates == std::vector{1.0 - room_drift_nudge_rate_delta});
        // The fake player ignores the rate, so the drift stays: no second nudge, no seek.
        h.tickFor(4);
        CHECK(h.backend.rates.size() == 1);
        // (60 - 10) ms at 1% = 5 s after the nudge began.
        h.advance(1'000 - 1);
        CHECK(h.backend.rates.size() == 1);
        h.advance(1);
        CHECK(h.backend.rates == std::vector{1.0 - room_drift_nudge_rate_delta, 1.0});
        CHECK(h.backend.seeks.empty());
    }
    SECTION("behind: faster") {
        h.backend.jump(-60);
        h.tickFor(1);
        CHECK(h.backend.rates == std::vector{1.0 + room_drift_nudge_rate_delta});
        CHECK(h.backend.seeks.empty());
    }
    SECTION("without rate support nothing happens, and the rate is not asked again") {
        h.backend.rate_supported = false;
        h.backend.jump(60);
        h.tickFor(5);
        CHECK(h.backend.rate_requests == 1);
        CHECK(h.backend.rate == 1.0);
        CHECK(h.backend.seeks.empty());
    }
}

TEST_CASE("A nudge ends when the drift falls to half the threshold", "[room][follower]") {
    Harness h;
    followRunningTrack(h);
    h.backend.jump(60);
    h.tickFor(1);
    REQUIRE(h.backend.rates.size() == 1);
    h.backend.jump(-52); // 8 ms left
    h.tickFor(1);
    CHECK(h.backend.rates == std::vector{1.0 - room_drift_nudge_rate_delta, 1.0});
}

TEST_CASE("A drift of 20 ms changes nothing", "[room][follower]") {
    Harness h;
    followRunningTrack(h);
    SECTION("ahead") { h.backend.jump(20); }
    SECTION("behind") { h.backend.jump(-20); }
    h.tickFor(3);
    CHECK(h.backend.rate_requests == 0);
    CHECK(h.backend.seeks.empty());
}

TEST_CASE("Pausing holds the player at the anchor position", "[room][follower]") {
    Harness h;
    followRunningTrack(h);
    h.advance(2'000);
    h.follower.apply(paused(1, h.hostNow(), 12'345), song);
    CHECK(h.backend.snapshot().status == PlaybackStatus::Paused);
    CHECK(h.backend.snapshot().position_ms == 12'345);
    const auto plays = h.backend.plays.size();
    h.tickFor(3);
    CHECK(h.backend.snapshot().status == PlaybackStatus::Paused);
    CHECK(h.backend.snapshot().position_ms == 12'345);
    CHECK(h.backend.plays.size() == plays);
    CHECK(h.backend.loads.size() == 1);
}

TEST_CASE("Without the file the follower waits and enters at the expected position when it arrives",
          "[room][follower]") {
    Harness h;
    h.syncClock();
    const auto playback = playing(1, h.hostNow() - 5'000, 10'000);
    h.follower.apply(playback, std::nullopt);
    CHECK(h.backend.loads.empty());
    h.tickFor(2);
    CHECK(h.backend.loads.empty());
    CHECK(h.backend.plays.empty());

    h.follower.apply(playback, song);
    REQUIRE(h.backend.loads == std::vector{song});
    CHECK(h.backend.snapshot().status == PlaybackStatus::Playing);
    // 5 s before the apply plus the 2 s of waiting, not the anchor position.
    CHECK(h.backend.snapshot().position_ms == 17'000);
    CHECK(h.backend.snapshot().position_ms == h.expected(playback));
}

TEST_CASE("A file still loading is entered once the player is ready", "[room][follower]") {
    Harness h;
    h.syncClock();
    h.backend.loads_ready = false;
    const auto playback = playing(1, h.hostNow() - 1'000, 0);
    h.follower.apply(playback, song);
    CHECK(h.backend.plays.empty());
    h.advance(room_follow_retry_ms);
    CHECK(h.backend.plays.empty());
    h.backend.finishLoading();
    h.advance(room_follow_retry_ms);
    REQUIRE(h.backend.plays.size() == 1);
    CHECK(h.backend.snapshot().position_ms == h.expected(playback));
    CHECK(h.backend.loads.size() == 1);
}

TEST_CASE("Playing waits for the first clock sample", "[room][follower]") {
    Harness h;
    const auto playback = playing(1, h.hostNow() - 3'000, 0);
    h.follower.apply(playback, song);
    h.tickFor(1);
    CHECK(h.backend.plays.empty());
    h.syncClock();
    REQUIRE(h.backend.plays.size() == 1);
    CHECK(h.backend.snapshot().position_ms == h.expected(playback));
}

TEST_CASE("An old position is not evaluated", "[room][follower]") {
    Harness h;
    const auto playback = followRunningTrack(h);
    h.backend.jump(200);
    h.backend.position_frozen = true;
    h.tickFor(3);
    CHECK(h.backend.seeks.empty());
    h.backend.position_frozen = false;
    h.tickFor(2);
    CHECK(h.backend.seeks == std::vector{h.expected(playback)});
}

TEST_CASE("A new item drops the running nudge and the seek evaluations", "[room][follower]") {
    Harness h;
    followRunningTrack(h);
    h.backend.jump(60);
    h.tickFor(1);
    REQUIRE(h.backend.rates.size() == 1);

    h.follower.apply(playing(2, h.hostNow() - 1'000, 0), other_song);
    CHECK(h.backend.rates == std::vector{1.0 - room_drift_nudge_rate_delta, 1.0});
    CHECK(h.backend.loads == std::vector{song, other_song});
    CHECK(h.backend.snapshot().status == PlaybackStatus::Playing);

    h.backend.seeks.clear();
    h.backend.jump(300);
    h.tickFor(1);
    h.follower.apply(playing(3, h.hostNow() - 1'000, 0), song);
    h.backend.seeks.clear();
    h.backend.jump(300);
    h.tickFor(1);
    CHECK(h.backend.seeks.empty());
}

TEST_CASE("The same playback again changes nothing", "[room][follower]") {
    Harness h;
    const auto playback = followRunningTrack(h);
    const auto plays = h.backend.plays.size();
    h.backend.jump(50);
    h.follower.apply(playback, song);
    CHECK(h.backend.seeks.empty());
    CHECK(h.backend.plays.size() == plays);
    CHECK(h.backend.loads.size() == 1);
}

TEST_CASE("An item without a file unloads the previous room file", "[room][follower]") {
    Harness h;
    followRunningTrack(h);
    h.follower.apply(playing(2, h.hostNow(), 0), std::nullopt);
    CHECK(h.backend.unloads == 1);
    CHECK(h.backend.snapshot().status == PlaybackStatus::Empty);
}

TEST_CASE("Stopping unloads the room file and forgets the clock", "[room][follower]") {
    Harness h;
    followRunningTrack(h);
    h.follower.stop();
    CHECK(h.backend.unloads == 1);
    h.backend.plays.clear();
    h.follower.apply(playing(1, h.hostNow() - 1'000, 0), song);
    h.tickFor(2);
    CHECK(h.backend.plays.empty());
}

TEST_CASE("A scheduled start dropped by stop never plays", "[room][follower]") {
    Harness h;
    h.syncClock();
    h.follower.apply(playing(1, h.hostNow() + room_play_lead_ms, 0), song);
    h.follower.stop();
    h.advance(room_play_lead_ms * 2);
    CHECK(h.backend.plays.empty());
}

} // namespace music_player::m0::test
