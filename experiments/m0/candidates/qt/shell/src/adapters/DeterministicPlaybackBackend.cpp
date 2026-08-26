#include "m0/adapters/DeterministicPlaybackBackend.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace music_player::m0 {

DeterministicPlaybackBackend::DeterministicPlaybackBackend(DeterministicPlaybackConfig config)
    : config_(config), snapshot_(config.initial_snapshot), volume_(config.initial_volume),
      muted_(config.initially_muted) {
    if (snapshot_.duration_ms <= 0 || snapshot_.position_ms < 0 || snapshot_.position_ms > snapshot_.duration_ms ||
        config_.tick_interval_ms <= 0 || !std::isfinite(volume_) || volume_ < 0.0 || volume_ > 1.0) {
        throw std::invalid_argument{"invalid deterministic playback configuration"};
    }
}

const PlaybackSnapshot& DeterministicPlaybackBackend::snapshot() const noexcept { return snapshot_; }

double DeterministicPlaybackBackend::volume() const noexcept { return volume_; }

bool DeterministicPlaybackBackend::muted() const noexcept { return muted_; }

bool DeterministicPlaybackBackend::hasPendingSeek() const noexcept { return pending_seek_.has_value(); }

void DeterministicPlaybackBackend::play() {
    if ((snapshot_.status == PlaybackStatus::Ready || snapshot_.status == PlaybackStatus::Paused) &&
        snapshot_.position_ms < snapshot_.duration_ms) {
        snapshot_.status = PlaybackStatus::Playing;
    }
}

void DeterministicPlaybackBackend::pause() {
    if (snapshot_.status == PlaybackStatus::Playing) {
        snapshot_.status = PlaybackStatus::Paused;
    }
}

void DeterministicPlaybackBackend::seekTo(const std::int64_t position_ms) {
    if (!snapshot_.seekable || pending_seek_.has_value() || snapshot_.status == PlaybackStatus::Empty ||
        snapshot_.status == PlaybackStatus::Loading || snapshot_.status == PlaybackStatus::Error) {
        return;
    }
    pending_seek_ = std::clamp(position_ms, std::int64_t{0}, snapshot_.duration_ms);
}

void DeterministicPlaybackBackend::setVolume(const double volume) {
    if (std::isfinite(volume) && volume >= 0.0 && volume <= 1.0) {
        volume_ = volume;
    }
}

void DeterministicPlaybackBackend::setMuted(const bool muted) { muted_ = muted; }

void DeterministicPlaybackBackend::rejectNextSeek() noexcept { reject_next_seek_ = true; }

DeterministicTickResult DeterministicPlaybackBackend::tick() {
    if (pending_seek_.has_value()) {
        const bool rejected = reject_next_seek_;
        if (!rejected) {
            snapshot_.position_ms = *pending_seek_;
            if (snapshot_.position_ms == snapshot_.duration_ms && snapshot_.status == PlaybackStatus::Playing) {
                snapshot_.status = PlaybackStatus::Paused;
            }
        }
        pending_seek_.reset();
        reject_next_seek_ = false;
        return {.snapshot = snapshot_, .seek_rejected = rejected};
    }

    if (snapshot_.status == PlaybackStatus::Playing) {
        const std::int64_t remaining_ms = snapshot_.duration_ms - snapshot_.position_ms;
        if (config_.tick_interval_ms >= remaining_ms) {
            snapshot_.position_ms = snapshot_.duration_ms;
            snapshot_.status = PlaybackStatus::Paused;
        } else {
            snapshot_.position_ms += config_.tick_interval_ms;
        }
    }
    return {.snapshot = snapshot_};
}

} // namespace music_player::m0
