#pragma once

#include "m0/domain/ListeningRoom.hpp"
#include "m0/domain/RoomClock.hpp"
#include "m0/ports/PlaybackBackend.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace music_player::m0 {

// Runs task once after delay_ms on the follower's thread (QTimer::singleShot in the app).
using RoomFollowerSchedule = std::function<void(std::int64_t delay_ms, std::function<void()> task)>;

// A guest's player following the host in time (ADR-0022, S9.1). Qt-free; every call and every scheduled
// task runs on one thread. Times: local_clock_ms is the guest's clock (std::chrono::steady_clock ms in
// the app, the same clock as PlaybackBackend::positionUpdatedAtMs); the host's clock is local + the
// offset of the RoomClockEstimator fed by addClockSample.
//
// Output offset: a positive offset plays earlier. The follower aims at
// expectedPositionMs(playback, host_now + offset), and a scheduled start happens at anchor_host - offset.
//
// apply(playback, file) takes the room's playback and the local copy of its current item (nullopt while
// it is missing). Nothing happens when both equal the last ones. Otherwise accumulated corrections are
// dropped (a running nudge goes back to 1.0) and the follower enters the new playback:
// - no current item, or no file: the room file it had loaded is unloaded; it waits;
// - the file is loaded once (the backend keeps it while its generation is the follower's); while the
//   backend loads, it looks again every room_follow_retry_ms;
// - paused: pause, then seek to anchor_position_ms;
// - playing without a clock sample yet: it waits for addClockSample;
// - playing, start still ahead: it pauses at the anchor position and plays at the start instant;
// - playing, start passed: it seeks to the expected position (unless it is already playing this file
//   within the seek threshold) and plays.
//
// tick() (every room_drift_check_ms) compares the backend position (moved forward by the time since
// positionUpdatedAtMs while playing) with the expected one. The thresholds are exclusive (S1: "acima
// de"): |drift| > seek_ms on two evaluations in a row -> one seek; |drift| > nudge_ms -> rate
// 1 -+ room_drift_nudge_rate_delta (faster when behind) until the drift falls to nudge_ms / 2 or
// crosses zero, and for at most the time 1% needs to get there (capped at room_drift_nudge_max_ms);
// one nudge at a time. A backend that answers Unsupported is never asked again: it is corrected by
// seeks only. An evaluation is discarded when the position is older than room_drift_stale_position_ms.
// Never a periodic seek.
class RoomPlaybackFollower final {
  public:
    // nudge_ms and seek_ms: room_drift_nudge_ms and room_drift_seek_ms in the app. Throws without a
    // clock or a scheduler, or when nudge_ms is not below seek_ms.
    RoomPlaybackFollower(PlaybackBackend& backend, std::function<std::int64_t()> local_clock_ms, std::int64_t nudge_ms,
                         std::int64_t seek_ms, RoomFollowerSchedule schedule);
    ~RoomPlaybackFollower();
    RoomPlaybackFollower(const RoomPlaybackFollower&) = delete;
    RoomPlaybackFollower& operator=(const RoomPlaybackFollower&) = delete;

    // Clamped to [minimum_output_offset_ms, maximum_output_offset_ms]. A pending start moves at once;
    // a playing track is corrected by the next ticks.
    void setOutputOffsetMs(std::int64_t offset_ms);
    [[nodiscard]] std::int64_t outputOffsetMs() const noexcept;
    void apply(const RoomPlayback& playback, std::optional<std::filesystem::path> local_file);
    void tick();
    void addClockSample(const ClockSample& sample);
    // Held: the player belongs to someone else (the local queue has not let go of it yet). The follower
    // keeps the room's playback, file and clock but never touches the player; pending starts, retries and
    // nudges are dropped (a nudge it owns goes back to 1.0). Released, it enters the room at once.
    void setHeld(bool held);
    [[nodiscard]] bool held() const noexcept;
    // Leaves the room: pending work is dropped, the room file is unloaded and the clock is forgotten.
    void stop();

  private:
    struct Alive;

    void reset();
    void enter();
    void start(std::uint64_t epoch);
    void retry();
    void startNudge(std::int64_t drift_ms);
    void endNudge();
    void unloadRoomFile();
    [[nodiscard]] bool ownsBackend() const noexcept;
    [[nodiscard]] std::optional<std::int64_t> hostNowMs() const;
    [[nodiscard]] std::optional<std::int64_t> expectedNowMs() const;
    [[nodiscard]] std::int64_t estimatedPositionMs() const;

    PlaybackBackend& backend_;
    std::function<std::int64_t()> local_clock_ms_;
    std::int64_t nudge_ms_;
    std::int64_t seek_ms_;
    RoomFollowerSchedule schedule_;
    std::int64_t offset_ms_{0};
    RoomClockEstimator clock_;
    std::optional<RoomPlayback> playback_;
    std::optional<std::filesystem::path> file_;
    std::optional<std::filesystem::path> loaded_file_;
    std::uint64_t loaded_generation_{0};
    // Bumped by every new entry and by stop(): scheduled starts and retries of older ones do nothing.
    std::uint64_t epoch_{0};
    bool start_pending_{false};
    bool retry_pending_{false};
    int seek_votes_{0};
    // -1 slower, +1 faster, 0 none. nudge_epoch_ cancels the time limit of an ended nudge.
    int nudge_direction_{0};
    std::uint64_t nudge_epoch_{0};
    bool rate_supported_{true};
    bool held_{false};
    std::shared_ptr<Alive> alive_;
};

} // namespace music_player::m0
