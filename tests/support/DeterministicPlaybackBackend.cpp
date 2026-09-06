#include "m0/adapters/DeterministicPlaybackBackend.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace music_player::m0 {

DeterministicPlaybackBackend::DeterministicPlaybackBackend(DeterministicPlaybackConfig config)
    : config_(config), snapshot_(config.initial_snapshot), volume_(config.initial_volume),
      muted_(config.initially_muted), generation_(snapshot_.generation) {
    if (snapshot_.duration_ms <= 0 || snapshot_.position_ms < 0 || snapshot_.position_ms > snapshot_.duration_ms ||
        config_.tick_interval_ms <= 0 || !std::isfinite(volume_) || volume_ < 0.0 || volume_ > 1.0) {
        throw std::invalid_argument{"invalid deterministic playback configuration"};
    }
}

const PlaybackSnapshot& DeterministicPlaybackBackend::snapshot() const noexcept { return snapshot_; }

void DeterministicPlaybackBackend::setEventCallback(PlaybackEventCallback callback) {
    event_callback_ = std::move(callback);
}

double DeterministicPlaybackBackend::volume() const noexcept { return volume_; }

bool DeterministicPlaybackBackend::muted() const noexcept { return muted_; }

bool DeterministicPlaybackBackend::hasPendingSeek() const noexcept { return pending_seek_.has_value(); }

PlaybackCommandResult DeterministicPlaybackBackend::load(const std::filesystem::path& source_path) {
    if (shutdown_) {
        return PlaybackCommandResult::Shutdown;
    }
    if (source_path.empty()) {
        return PlaybackCommandResult::InvalidArgument;
    }

    ++generation_;
    if (generation_ == 0) {
        ++generation_;
    }
    pending_seek_.reset();
    reject_next_seek_ = false;
    ended_emitted_ = false;
    publishSnapshot({PlaybackStatus::Ready, 0, config_.initial_snapshot.duration_ms, true, generation_});
    return PlaybackCommandResult::Accepted;
}

PlaybackCommandResult DeterministicPlaybackBackend::play() {
    if (shutdown_) {
        return PlaybackCommandResult::Shutdown;
    }
    if (snapshot_.status == PlaybackStatus::Playing) {
        return PlaybackCommandResult::Accepted;
    }
    if (snapshot_.status != PlaybackStatus::Ready && snapshot_.status != PlaybackStatus::Paused) {
        return PlaybackCommandResult::NotReady;
    }

    auto next = snapshot_;
    if (next.position_ms >= next.duration_ms) {
        next.position_ms = 0;
        ended_emitted_ = false;
    }
    next.status = PlaybackStatus::Playing;
    publishSnapshot(next);
    return PlaybackCommandResult::Accepted;
}

PlaybackCommandResult DeterministicPlaybackBackend::pause() {
    if (shutdown_) {
        return PlaybackCommandResult::Shutdown;
    }
    if (snapshot_.status == PlaybackStatus::Paused || snapshot_.status == PlaybackStatus::Ready) {
        return PlaybackCommandResult::Accepted;
    }
    if (snapshot_.status != PlaybackStatus::Playing) {
        return PlaybackCommandResult::NotReady;
    }

    auto next = snapshot_;
    next.status = PlaybackStatus::Paused;
    publishSnapshot(next);
    return PlaybackCommandResult::Accepted;
}

PlaybackCommandResult DeterministicPlaybackBackend::seekTo(const std::int64_t position_ms) {
    if (shutdown_) {
        return PlaybackCommandResult::Shutdown;
    }
    if (snapshot_.status == PlaybackStatus::Empty || snapshot_.status == PlaybackStatus::Loading ||
        snapshot_.status == PlaybackStatus::Error) {
        return PlaybackCommandResult::NotReady;
    }
    if (!snapshot_.seekable) {
        return PlaybackCommandResult::NotSeekable;
    }
    if (pending_seek_.has_value()) {
        return PlaybackCommandResult::Busy;
    }
    pending_seek_ = std::clamp(position_ms, std::int64_t{0}, snapshot_.duration_ms);
    return PlaybackCommandResult::Accepted;
}

PlaybackCommandResult DeterministicPlaybackBackend::setVolume(const double volume) {
    if (shutdown_) {
        return PlaybackCommandResult::Shutdown;
    }
    if (!std::isfinite(volume) || volume < 0.0 || volume > 1.0) {
        return PlaybackCommandResult::InvalidArgument;
    }
    volume_ = volume;
    return PlaybackCommandResult::Accepted;
}

PlaybackCommandResult DeterministicPlaybackBackend::setMuted(const bool muted) {
    if (shutdown_) {
        return PlaybackCommandResult::Shutdown;
    }
    muted_ = muted;
    return PlaybackCommandResult::Accepted;
}

PlaybackCommandResult DeterministicPlaybackBackend::unload() {
    if (shutdown_) {
        return PlaybackCommandResult::Shutdown;
    }
    if (snapshot_.status == PlaybackStatus::Empty) {
        return PlaybackCommandResult::Accepted;
    }

    ++generation_;
    if (generation_ == 0) {
        ++generation_;
    }
    pending_seek_.reset();
    reject_next_seek_ = false;
    ended_emitted_ = false;
    publishSnapshot({PlaybackStatus::Empty, 0, 0, false, generation_});
    return PlaybackCommandResult::Accepted;
}

PlaybackCommandResult DeterministicPlaybackBackend::shutdown() {
    if (shutdown_) {
        return PlaybackCommandResult::Accepted;
    }

    ++generation_;
    if (generation_ == 0) {
        ++generation_;
    }
    shutdown_ = true;
    pending_seek_.reset();
    reject_next_seek_ = false;
    ended_emitted_ = false;
    publishSnapshot({PlaybackStatus::Empty, 0, 0, false, generation_});
    return PlaybackCommandResult::Accepted;
}

bool DeterministicPlaybackBackend::selectTrack(const std::int64_t duration_ms, const PlaybackStatus status) {
    if (shutdown_ || duration_ms <= 0 ||
        (status != PlaybackStatus::Ready && status != PlaybackStatus::Paused && status != PlaybackStatus::Playing)) {
        return false;
    }
    ++generation_;
    if (generation_ == 0) {
        ++generation_;
    }
    pending_seek_.reset();
    reject_next_seek_ = false;
    ended_emitted_ = false;
    publishSnapshot({status, 0, duration_ms, true, generation_});
    return true;
}

void DeterministicPlaybackBackend::rejectNextSeek() noexcept { reject_next_seek_ = true; }

DeterministicTickResult DeterministicPlaybackBackend::tick() {
    if (pending_seek_.has_value()) {
        const bool rejected = reject_next_seek_;
        const auto target = *pending_seek_;
        pending_seek_.reset();
        reject_next_seek_ = false;
        if (!rejected) {
            if (target < snapshot_.duration_ms) {
                ended_emitted_ = false;
            }
            if (target == snapshot_.duration_ms && snapshot_.status == PlaybackStatus::Playing) {
                finishCurrentGenerationAtEnd();
            } else {
                auto next = snapshot_;
                next.position_ms = target;
                publishSnapshot(next);
            }
        }
        return {.snapshot = snapshot_, .seek_rejected = rejected};
    }

    if (snapshot_.status == PlaybackStatus::Playing) {
        const std::int64_t remaining_ms = snapshot_.duration_ms - snapshot_.position_ms;
        if (config_.tick_interval_ms >= remaining_ms) {
            finishCurrentGenerationAtEnd();
        } else {
            auto next = snapshot_;
            next.position_ms += config_.tick_interval_ms;
            publishSnapshot(next);
        }
    }
    return {.snapshot = snapshot_};
}

bool DeterministicPlaybackBackend::enterUnavailableScenario() {
    if (shutdown_ || snapshot_.status == PlaybackStatus::Error) {
        return false;
    }
    pending_seek_.reset();
    reject_next_seek_ = false;
    ended_emitted_ = false;
    publishSnapshot({PlaybackStatus::Error, 0, 0, false, generation_, PlaybackErrorCode::SourceUnavailable});
    return true;
}

bool DeterministicPlaybackBackend::restoreInitialScenario() {
    if (shutdown_ || snapshot_.status != PlaybackStatus::Error) {
        return false;
    }
    auto restored = config_.initial_snapshot;
    restored.generation = generation_;
    pending_seek_.reset();
    reject_next_seek_ = false;
    ended_emitted_ = false;
    publishSnapshot(restored);
    return true;
}

void DeterministicPlaybackBackend::publishSnapshot(const PlaybackSnapshot& next) {
    if (snapshot_ == next) {
        return;
    }
    snapshot_ = next;
    publishEvent(PlaybackEventType::SnapshotChanged);
}

void DeterministicPlaybackBackend::finishCurrentGenerationAtEnd() {
    auto final_snapshot = snapshot_;
    final_snapshot.status = PlaybackStatus::Paused;
    final_snapshot.position_ms = final_snapshot.duration_ms;
    if (ended_emitted_) {
        publishSnapshot(final_snapshot);
        return;
    }

    const auto ended_generation = generation_;
    ended_emitted_ = true;
    publishSnapshot(final_snapshot);
    if (!shutdown_ && generation_ == ended_generation && ended_emitted_ && snapshot_ == final_snapshot) {
        publishEvent(PlaybackEventType::Ended);
    }
}

void DeterministicPlaybackBackend::publishEvent(const PlaybackEventType type) {
    if (!event_callback_) {
        return;
    }
    const auto callback = event_callback_;
    const PlaybackEvent event{type, snapshot_};
    callback(event);
}

} // namespace music_player::m0
