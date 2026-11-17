#include "m0/domain/RoomClock.hpp"

#include "m0/domain/ListeningRoomLimits.hpp"

#include <algorithm>

namespace music_player::m0 {

namespace {

std::int64_t floorHalf(const std::int64_t sum) noexcept {
    return sum >> 1; // arithmetic shift floors toward negative infinity
}

} // namespace

void RoomClockEstimator::add(const ClockSample& sample) {
    if (sample.received_local_ms < sample.sent_local_ms) {
        return;
    }
    samples_.push_back(sample);
    while (samples_.size() > room_clock_samples) {
        samples_.pop_front();
    }
}

const ClockSample* RoomClockEstimator::best() const {
    const ClockSample* found = nullptr;
    for (const auto& s : samples_) {
        if (found == nullptr ||
            s.received_local_ms - s.sent_local_ms <= found->received_local_ms - found->sent_local_ms) {
            found = &s;
        }
    }
    return found;
}

std::optional<std::int64_t> RoomClockEstimator::offsetMs() const {
    const auto* s = best();
    if (s == nullptr) {
        return std::nullopt;
    }
    return s->host_ms - floorHalf(s->sent_local_ms + s->received_local_ms);
}

std::optional<std::int64_t> RoomClockEstimator::roundTripMs() const {
    const auto* s = best();
    if (s == nullptr) {
        return std::nullopt;
    }
    return s->received_local_ms - s->sent_local_ms;
}

std::int64_t expectedPositionMs(const RoomPlayback& playback, const std::int64_t host_now_ms) noexcept {
    const auto position = playback.playing ? playback.anchor_position_ms + (host_now_ms - playback.anchor_host_ms)
                                           : playback.anchor_position_ms;
    return std::max<std::int64_t>(position, 0);
}

DriftCorrection classifyDrift(const std::int64_t drift_ms, const std::int64_t nudge_ms,
                              const std::int64_t seek_ms) noexcept {
    const auto magnitude = drift_ms < 0 ? -drift_ms : drift_ms;
    if (magnitude >= seek_ms) {
        return DriftCorrection::Seek;
    }
    return magnitude >= nudge_ms ? DriftCorrection::Nudge : DriftCorrection::None;
}

} // namespace music_player::m0
