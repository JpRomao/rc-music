#include "m0/application/RoomPlaybackFollower.hpp"

#include "m0/domain/ListeningRoomLimits.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace music_player::m0 {
namespace {

std::int64_t magnitude(const std::int64_t value) noexcept { return value < 0 ? -value : value; }

bool loaded(const PlaybackStatus status) noexcept {
    return status == PlaybackStatus::Ready || status == PlaybackStatus::Paused || status == PlaybackStatus::Playing;
}

} // namespace

// Scheduled tasks hold a weak reference to this token, so one that runs after the follower is gone does
// nothing.
struct RoomPlaybackFollower::Alive final {
    RoomPlaybackFollower* follower{};
};

RoomPlaybackFollower::RoomPlaybackFollower(PlaybackBackend& backend, std::function<std::int64_t()> local_clock_ms,
                                           const std::int64_t nudge_ms, const std::int64_t seek_ms,
                                           RoomFollowerSchedule schedule)
    : backend_(backend), local_clock_ms_(std::move(local_clock_ms)), nudge_ms_(nudge_ms), seek_ms_(seek_ms),
      schedule_(std::move(schedule)), alive_(std::make_shared<Alive>(Alive{this})) {
    if (!local_clock_ms_ || !schedule_)
        throw std::invalid_argument{"the room playback follower needs a clock and a scheduler"};
    if (nudge_ms_ <= 0 || nudge_ms_ >= seek_ms_)
        throw std::invalid_argument{"the room playback follower needs 0 < nudge_ms < seek_ms"};
}

RoomPlaybackFollower::~RoomPlaybackFollower() = default;

void RoomPlaybackFollower::setOutputOffsetMs(const std::int64_t offset_ms) {
    const auto clamped = std::clamp<std::int64_t>(offset_ms, minimum_output_offset_ms, maximum_output_offset_ms);
    if (clamped == offset_ms_)
        return;
    offset_ms_ = clamped;
    if (start_pending_) {
        ++epoch_;
        start_pending_ = false;
        enter();
    }
}

std::int64_t RoomPlaybackFollower::outputOffsetMs() const noexcept { return offset_ms_; }

void RoomPlaybackFollower::apply(const RoomPlayback& playback, std::optional<std::filesystem::path> local_file) {
    if (playback_ == playback && file_ == local_file)
        return;
    playback_ = playback;
    file_ = std::move(local_file);
    reset();
    if (!playback.current_item || !file_) {
        unloadRoomFile();
        return;
    }
    enter();
}

std::optional<std::int64_t> RoomPlaybackFollower::lastDriftMs() const noexcept { return last_drift_ms_; }

void RoomPlaybackFollower::tick() {
    last_drift_ms_.reset();
    if (held_ || !playback_ || !playback_->playing || !file_ || start_pending_ || retry_pending_ || !ownsBackend())
        return;
    const auto& snapshot = backend_.snapshot();
    const auto expected = expectedNowMs();
    if (!expected)
        return;
    if (snapshot.status != PlaybackStatus::Playing) {
        // The room plays but this player does not (it was not ready, or it stopped): enter again, unless
        // the track is over and the host is about to move on.
        if (loaded(snapshot.status) && *expected < snapshot.duration_ms)
            enter();
        return;
    }
    const auto updated = backend_.positionUpdatedAtMs();
    if (updated && local_clock_ms_() - *updated > room_drift_stale_position_ms)
        return; // a stalled main thread kept the position from moving: not a real drift
    const auto drift = estimatedPositionMs() - *expected;
    last_drift_ms_ = drift;
    if (nudge_direction_ != 0 && (magnitude(drift) <= nudge_ms_ / 2 || (drift > 0) == (nudge_direction_ > 0)))
        endNudge();
    // S1's thresholds are exclusive ("acima de"); classifyDrift's are inclusive.
    switch (classifyDrift(drift, nudge_ms_ + 1, seek_ms_ + 1)) {
    case DriftCorrection::Seek:
        if (++seek_votes_ < 2)
            return;
        seek_votes_ = 0;
        endNudge();
        static_cast<void>(backend_.seekTo(*expected));
        return;
    case DriftCorrection::Nudge:
        seek_votes_ = 0;
        if (nudge_direction_ == 0)
            startNudge(drift);
        return;
    case DriftCorrection::None:
        seek_votes_ = 0;
        return;
    }
}

void RoomPlaybackFollower::addClockSample(const ClockSample& sample) {
    const bool known = clock_.offsetMs().has_value();
    clock_.add(sample);
    // A play that was waiting for the host's time can start now.
    if (!known && clock_.offsetMs() && playback_ && playback_->playing && file_ && !retry_pending_)
        enter();
}

void RoomPlaybackFollower::setHeld(const bool held) {
    if (held == held_)
        return;
    if (held) {
        reset(); // before held_: a nudge it owns goes back to 1.0
        held_ = true;
        return;
    }
    held_ = false;
    reset();
    enter();
}

bool RoomPlaybackFollower::held() const noexcept { return held_; }

void RoomPlaybackFollower::stop() {
    reset();
    unloadRoomFile();
    playback_.reset();
    file_.reset();
    clock_ = {};
}

void RoomPlaybackFollower::reset() {
    last_drift_ms_.reset();
    ++epoch_;
    start_pending_ = false;
    retry_pending_ = false;
    seek_votes_ = 0;
    endNudge();
}

void RoomPlaybackFollower::enter() {
    if (held_ || !playback_ || !playback_->current_item || !file_)
        return;
    if (loaded_file_ != file_ || !ownsBackend()) {
        endNudge();
        if (backend_.load(*file_) != PlaybackCommandResult::Accepted)
            return; // waits for the next apply
        loaded_file_ = file_;
        loaded_generation_ = backend_.snapshot().generation;
    }
    const auto& snapshot = backend_.snapshot();
    if (snapshot.status == PlaybackStatus::Loading) {
        retry();
        return;
    }
    if (!loaded(snapshot.status))
        return; // the file could not be opened; the next apply tries again
    if (!playback_->playing) {
        static_cast<void>(backend_.pause());
        if (backend_.snapshot().position_ms != playback_->anchor_position_ms)
            static_cast<void>(backend_.seekTo(playback_->anchor_position_ms));
        return;
    }
    const auto host_now = hostNowMs();
    if (!host_now)
        return; // addClockSample enters once the host's time is known
    const auto wait_ms = playback_->anchor_host_ms - offset_ms_ - *host_now;
    if (wait_ms > 0) {
        // Ready at the anchor position, silent until the start instant.
        if (backend_.snapshot().status == PlaybackStatus::Playing)
            static_cast<void>(backend_.pause());
        if (backend_.snapshot().position_ms != playback_->anchor_position_ms)
            static_cast<void>(backend_.seekTo(playback_->anchor_position_ms));
        start_pending_ = true;
        const std::weak_ptr<Alive> alive = alive_;
        schedule_(wait_ms, [alive, epoch = epoch_] {
            if (const auto self = alive.lock())
                self->follower->start(epoch);
        });
        return;
    }
    const auto expected = expectedPositionMs(*playback_, *host_now + offset_ms_);
    if (backend_.snapshot().status == PlaybackStatus::Playing &&
        magnitude(estimatedPositionMs() - expected) <= seek_ms_)
        return; // already there: the ticks correct the rest without a gap
    static_cast<void>(backend_.seekTo(expected));
    static_cast<void>(backend_.play());
}

void RoomPlaybackFollower::start(const std::uint64_t epoch) {
    if (epoch != epoch_ || !start_pending_)
        return;
    start_pending_ = false;
    if (!playback_ || !ownsBackend())
        return;
    if (const auto expected = expectedNowMs();
        expected && magnitude(backend_.snapshot().position_ms - *expected) > nudge_ms_)
        static_cast<void>(backend_.seekTo(*expected));
    static_cast<void>(backend_.play());
}

void RoomPlaybackFollower::retry() {
    retry_pending_ = true;
    const std::weak_ptr<Alive> alive = alive_;
    schedule_(room_follow_retry_ms, [alive, epoch = epoch_] {
        const auto self = alive.lock();
        if (!self || epoch != self->follower->epoch_)
            return;
        self->follower->retry_pending_ = false;
        self->follower->enter();
    });
}

void RoomPlaybackFollower::startNudge(const std::int64_t drift_ms) {
    if (!rate_supported_)
        return;
    // Behind (negative drift): faster. Ahead: slower.
    const int direction = drift_ms < 0 ? 1 : -1;
    const auto result = backend_.setPlaybackRate(1.0 + direction * room_drift_nudge_rate_delta);
    if (result == PlaybackCommandResult::Unsupported) {
        rate_supported_ = false;
        return;
    }
    if (result != PlaybackCommandResult::Accepted)
        return;
    nudge_direction_ = direction;
    // The time 1% takes to bring the drift down to half the threshold, never more than the cap.
    const auto to_cover = static_cast<double>(magnitude(drift_ms) - nudge_ms_ / 2);
    const auto duration =
        std::min<std::int64_t>(std::llround(to_cover / room_drift_nudge_rate_delta), room_drift_nudge_max_ms);
    const std::weak_ptr<Alive> alive = alive_;
    schedule_(std::max<std::int64_t>(duration, 1), [alive, nudge = ++nudge_epoch_] {
        if (const auto self = alive.lock(); self && nudge == self->follower->nudge_epoch_)
            self->follower->endNudge();
    });
}

void RoomPlaybackFollower::endNudge() {
    if (nudge_direction_ == 0)
        return;
    nudge_direction_ = 0;
    ++nudge_epoch_;
    if (ownsBackend())
        static_cast<void>(backend_.setPlaybackRate(1.0));
}

void RoomPlaybackFollower::unloadRoomFile() {
    if (loaded_file_ && ownsBackend())
        static_cast<void>(backend_.unload());
    loaded_file_.reset();
}

bool RoomPlaybackFollower::ownsBackend() const noexcept {
    return loaded_file_.has_value() && backend_.snapshot().generation == loaded_generation_;
}

std::optional<std::int64_t> RoomPlaybackFollower::hostNowMs() const {
    const auto offset = clock_.offsetMs();
    if (!offset)
        return std::nullopt;
    return local_clock_ms_() + *offset;
}

std::optional<std::int64_t> RoomPlaybackFollower::expectedNowMs() const {
    const auto host_now = hostNowMs();
    if (!playback_ || !host_now)
        return std::nullopt;
    return expectedPositionMs(*playback_, *host_now + offset_ms_);
}

// While playing, the position the backend last reported plus the time since (it reports in steps).
std::int64_t RoomPlaybackFollower::estimatedPositionMs() const {
    const auto& snapshot = backend_.snapshot();
    const auto updated = backend_.positionUpdatedAtMs();
    if (snapshot.status != PlaybackStatus::Playing || !updated)
        return snapshot.position_ms;
    return snapshot.position_ms + std::max<std::int64_t>(local_clock_ms_() - *updated, 0);
}

} // namespace music_player::m0
