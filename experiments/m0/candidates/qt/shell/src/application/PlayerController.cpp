#include "m0/application/PlayerController.hpp"

#include <algorithm>
#include <cmath>

namespace music_player::m0 {
namespace {

[[nodiscard]] PlaybackSnapshot normalizedSnapshot(PlaybackSnapshot snapshot) noexcept {
    snapshot.duration_ms = std::max(snapshot.duration_ms, std::int64_t{0});
    snapshot.position_ms = std::clamp(snapshot.position_ms, std::int64_t{0}, snapshot.duration_ms);
    snapshot.seekable = snapshot.seekable && snapshot.duration_ms > 0;
    return snapshot;
}

} // namespace

PlayerController::PlayerController(PlaybackBackend& playback_backend, PlatformIntegration& platform,
                                   PlaybackSnapshot initial_snapshot)
    : playback_backend_(playback_backend), platform_(platform),
      confirmed_snapshot_(normalizedSnapshot(initial_snapshot)),
      displayed_position_ms_(confirmed_snapshot_.position_ms) {}

const Preferences& PlayerController::preferences() const noexcept { return preferences_; }

const PlaybackSnapshot& PlayerController::confirmedSnapshot() const noexcept { return confirmed_snapshot_; }

std::int64_t PlayerController::displayedPositionMs() const noexcept { return displayed_position_ms_; }

bool PlayerController::seekPreviewActive() const noexcept { return seek_preview_active_; }

bool PlayerController::seekPending() const noexcept { return seek_pending_; }

void PlayerController::beginSeekPreview(const std::int64_t position_ms) {
    if (!confirmed_snapshot_.seekable || confirmed_snapshot_.duration_ms <= 0 || seek_pending_) {
        return;
    }

    seek_preview_active_ = true;
    preview_position_ms_ = std::clamp(position_ms, std::int64_t{0}, confirmed_snapshot_.duration_ms);
    displayed_position_ms_ = preview_position_ms_;
}

void PlayerController::updateSeekPreview(const std::int64_t position_ms) {
    if (!seek_preview_active_ || seek_pending_) {
        return;
    }

    preview_position_ms_ = std::clamp(position_ms, std::int64_t{0}, confirmed_snapshot_.duration_ms);
    displayed_position_ms_ = preview_position_ms_;
}

void PlayerController::commitSeekPreview() {
    if (!seek_preview_active_ || seek_pending_) {
        return;
    }

    seek_pending_ = true;
    playback_backend_.seekTo(preview_position_ms_);
}

void PlayerController::rejectPendingSeek() {
    if (!seek_preview_active_) {
        return;
    }

    seek_pending_ = false;
    seek_preview_active_ = false;
    displayed_position_ms_ = confirmed_snapshot_.position_ms;
}

void PlayerController::applyBackendSnapshot(PlaybackSnapshot snapshot) {
    snapshot = normalizedSnapshot(snapshot);
    confirmed_snapshot_ = snapshot;

    if (seek_pending_ && snapshot.position_ms == preview_position_ms_) {
        seek_pending_ = false;
        seek_preview_active_ = false;
    }

    if (seek_preview_active_) {
        displayed_position_ms_ = preview_position_ms_;
        return;
    }

    displayed_position_ms_ = snapshot.position_ms;
}

void PlayerController::setTheme(const ThemeMode theme) { preferences_.theme = theme; }

void PlayerController::setMotionMode(const MotionMode motion) { preferences_.motion = motion; }

bool PlayerController::setSkipBackwardSeconds(const int seconds) {
    if (seconds < 1 || seconds > 300) {
        return false;
    }

    preferences_.skip_backward_seconds = seconds;
    return true;
}

bool PlayerController::setSkipForwardSeconds(const int seconds) {
    if (seconds < 1 || seconds > 300) {
        return false;
    }

    preferences_.skip_forward_seconds = seconds;
    return true;
}

bool PlayerController::setVolume(const double volume) {
    if (!std::isfinite(volume) || volume < 0.0 || volume > 1.0) {
        return false;
    }

    preferences_.volume = volume;
    if (volume > 0.0) {
        last_nonzero_volume_ = volume;
    }
    playback_backend_.setVolume(volume);
    return true;
}

void PlayerController::toggleMute() {
    preferences_.muted = !preferences_.muted;
    if (!preferences_.muted && preferences_.volume == 0.0) {
        preferences_.volume = last_nonzero_volume_;
        playback_backend_.setVolume(preferences_.volume);
    }
    playback_backend_.setMuted(preferences_.muted);
}

void PlayerController::setCloseBehavior(const CloseBehavior behavior) { preferences_.close_behavior = behavior; }

void PlayerController::setRestoreLastSession(const bool restore) { preferences_.restore_last_session = restore; }

bool PlayerController::setStartMinimized(const bool start_minimized) {
    if (start_minimized && !platform_.trayAvailable()) {
        return false;
    }

    preferences_.start_minimized = start_minimized;
    return true;
}

void PlayerController::setQueueEndBehavior(const QueueEndBehavior behavior) {
    preferences_.queue_end_behavior = behavior;
}

CloseDisposition PlayerController::requestClose() {
    if (preferences_.close_behavior == CloseBehavior::Exit) {
        platform_.requestExit();
        return CloseDisposition::ExitRequested;
    }

    if (platform_.trayAvailable()) {
        platform_.hideWindow();
        return CloseDisposition::HiddenToTray;
    }

    platform_.minimizeWindow();
    return CloseDisposition::Minimized;
}

void PlayerController::requestRestore() { platform_.showAndActivateWindow(); }

} // namespace music_player::m0
