#include "m0/application/PlayerController.hpp"
#include "m0/domain/DownloadDirectory.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace music_player::m0 {
namespace {

[[nodiscard]] PlaybackSnapshot normalizedSnapshot(PlaybackSnapshot snapshot) noexcept {
    snapshot.duration_ms = std::max(snapshot.duration_ms, std::int64_t{0});
    snapshot.position_ms = std::clamp(snapshot.position_ms, std::int64_t{0}, snapshot.duration_ms);
    snapshot.seekable = snapshot.seekable && snapshot.duration_ms > 0;
    return snapshot;
}

[[nodiscard]] bool supportsTransport(const PlaybackStatus status) noexcept {
    return status == PlaybackStatus::Ready || status == PlaybackStatus::Paused || status == PlaybackStatus::Playing;
}

[[nodiscard]] Preferences validatedPreferences(Preferences preferences) {
    const bool valid_theme = preferences.theme == ThemeMode::System || preferences.theme == ThemeMode::Light ||
                             preferences.theme == ThemeMode::Dark;
    const bool valid_motion = preferences.motion == MotionMode::System || preferences.motion == MotionMode::Reduced;
    const bool valid_close = preferences.close_behavior == CloseBehavior::MinimizeToTray ||
                             preferences.close_behavior == CloseBehavior::Exit;
    const bool valid_queue_end = preferences.queue_end_behavior == QueueEndBehavior::StopKeepSelection ||
                                 preferences.queue_end_behavior == QueueEndBehavior::StopClearSelection;
    const bool valid_volume =
        std::isfinite(preferences.volume) && preferences.volume >= 0.0 && preferences.volume <= 1.0;
    const bool valid_skips = preferences.skip_backward_seconds >= 1 && preferences.skip_backward_seconds <= 300 &&
                             preferences.skip_forward_seconds >= 1 && preferences.skip_forward_seconds <= 300;
    const bool valid_window_placement = !preferences.window_placement || (preferences.window_placement->width > 0 &&
                                                                          preferences.window_placement->height > 0);
    if (!valid_theme || !valid_motion || !valid_close || !valid_queue_end || !valid_volume || !valid_skips ||
        !valid_window_placement) {
        throw std::invalid_argument{"invalid initial preferences"};
    }
    return preferences;
}

} // namespace

PlayerController::PlayerController(PlaybackBackend& playback_backend, PlatformIntegration& platform,
                                   PlaybackSnapshot initial_snapshot)
    : PlayerController(playback_backend, platform, initial_snapshot, Preferences{}) {}

PlayerController::PlayerController(PlaybackBackend& playback_backend, PlatformIntegration& platform,
                                   PlaybackSnapshot initial_snapshot, Preferences initial_preferences)
    : playback_backend_(playback_backend), platform_(platform), preferences_(validatedPreferences(initial_preferences)),
      confirmed_snapshot_(normalizedSnapshot(initial_snapshot)),
      displayed_position_ms_(confirmed_snapshot_.position_ms),
      last_nonzero_volume_(preferences_.volume > 0.0 ? preferences_.volume : Preferences{}.volume) {}

const Preferences& PlayerController::preferences() const noexcept { return preferences_; }

const PlaybackSnapshot& PlayerController::confirmedSnapshot() const noexcept { return confirmed_snapshot_; }

std::int64_t PlayerController::displayedPositionMs() const noexcept { return displayed_position_ms_; }

bool PlayerController::seekPreviewActive() const noexcept { return seek_preview_active_; }

bool PlayerController::seekPending() const noexcept { return seek_pending_; }

PlaybackStatus PlayerController::effectivePlaybackStatus() const noexcept {
    return desired_transport_status_.value_or(confirmed_snapshot_.status);
}

bool PlayerController::requestTransport(const PlaybackStatus target) {
    const PlaybackStatus status = effectivePlaybackStatus();
    const bool valid_transition =
        (target == PlaybackStatus::Playing && (status == PlaybackStatus::Ready || status == PlaybackStatus::Paused)) ||
        (target == PlaybackStatus::Paused && status == PlaybackStatus::Playing);
    if (!valid_transition) {
        return false;
    }

    desired_transport_status_ = target;
    if (in_flight_transport_status_.has_value())
        return true;
    return issueTransportCommand(target);
}

bool PlayerController::issueTransportCommand(const PlaybackStatus target) {
    in_flight_transport_status_ = target;
    PlaybackCommandResult result{PlaybackCommandResult::InvalidArgument};
    if (target == PlaybackStatus::Playing) {
        result = playback_backend_.play();
    } else if (target == PlaybackStatus::Paused) {
        result = playback_backend_.pause();
    }
    if (result == PlaybackCommandResult::Accepted)
        return true;

    if (in_flight_transport_status_ == std::optional{target})
        in_flight_transport_status_.reset();
    if (desired_transport_status_ == std::optional{target})
        desired_transport_status_.reset();
    return false;
}

bool PlayerController::play() { return requestTransport(PlaybackStatus::Playing); }

bool PlayerController::pause() { return requestTransport(PlaybackStatus::Paused); }

bool PlayerController::togglePlayback() {
    switch (effectivePlaybackStatus()) {
    case PlaybackStatus::Playing:
        return pause();
    case PlaybackStatus::Ready:
    case PlaybackStatus::Paused:
        return play();
    case PlaybackStatus::Empty:
    case PlaybackStatus::Loading:
    case PlaybackStatus::Error:
        return false;
    }
    return false;
}

bool PlayerController::skipBackward() {
    if (!supportsTransport(confirmed_snapshot_.status) || !confirmed_snapshot_.seekable || seek_preview_active_ ||
        seek_pending_) {
        return false;
    }

    const auto delta_ms = static_cast<std::int64_t>(preferences_.skip_backward_seconds) * 1'000;
    const auto target =
        confirmed_snapshot_.position_ms <= delta_ms ? std::int64_t{0} : confirmed_snapshot_.position_ms - delta_ms;
    beginSeekPreview(target);
    return commitSeekPreview();
}

bool PlayerController::skipForward() {
    if (!supportsTransport(confirmed_snapshot_.status) || !confirmed_snapshot_.seekable || seek_preview_active_ ||
        seek_pending_) {
        return false;
    }

    const auto delta_ms = static_cast<std::int64_t>(preferences_.skip_forward_seconds) * 1'000;
    const auto remaining_ms = confirmed_snapshot_.duration_ms - confirmed_snapshot_.position_ms;
    const auto target =
        remaining_ms <= delta_ms ? confirmed_snapshot_.duration_ms : confirmed_snapshot_.position_ms + delta_ms;
    beginSeekPreview(target);
    return commitSeekPreview();
}

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

bool PlayerController::commitSeekPreview() {
    if (!seek_preview_active_ || seek_pending_) {
        return false;
    }

    if (preview_position_ms_ == confirmed_snapshot_.position_ms) {
        seek_preview_active_ = false;
        displayed_position_ms_ = confirmed_snapshot_.position_ms;
        return true;
    }

    seek_pending_ = true;
    if (playback_backend_.seekTo(preview_position_ms_) == PlaybackCommandResult::Accepted) {
        return true;
    }

    seek_pending_ = false;
    seek_preview_active_ = false;
    displayed_position_ms_ = confirmed_snapshot_.position_ms;
    return false;
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
    if (snapshot.generation < confirmed_snapshot_.generation) {
        return;
    }

    const bool generation_changed = snapshot.generation != confirmed_snapshot_.generation;
    snapshot = normalizedSnapshot(snapshot);
    confirmed_snapshot_ = snapshot;
    const bool reached_end = snapshot.status == PlaybackStatus::Paused && snapshot.duration_ms > 0 &&
                             snapshot.position_ms == snapshot.duration_ms;
    std::optional<PlaybackStatus> compensating_transport_command;

    if (generation_changed || !supportsTransport(snapshot.status) || reached_end) {
        desired_transport_status_.reset();
        in_flight_transport_status_.reset();
    } else {
        if (in_flight_transport_status_ == std::optional{snapshot.status})
            in_flight_transport_status_.reset();
        if (!in_flight_transport_status_.has_value() && desired_transport_status_.has_value()) {
            if (desired_transport_status_ == std::optional{snapshot.status}) {
                desired_transport_status_.reset();
            } else {
                compensating_transport_command = desired_transport_status_;
            }
        }
    }

    if (generation_changed) {
        seek_pending_ = false;
        seek_preview_active_ = false;
        displayed_position_ms_ = snapshot.position_ms;
    }

    if (!supportsTransport(snapshot.status) || !snapshot.seekable) {
        seek_pending_ = false;
        seek_preview_active_ = false;
        displayed_position_ms_ = snapshot.position_ms;
    } else {
        if (seek_pending_ && snapshot.position_ms == preview_position_ms_) {
            seek_pending_ = false;
            seek_preview_active_ = false;
        }

        displayed_position_ms_ = seek_preview_active_ ? preview_position_ms_ : snapshot.position_ms;
    }

    if (compensating_transport_command.has_value())
        static_cast<void>(issueTransportCommand(*compensating_transport_command));
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

    if (playback_backend_.setVolume(volume) != PlaybackCommandResult::Accepted) {
        return false;
    }

    preferences_.volume = volume;
    if (volume > 0.0) {
        last_nonzero_volume_ = volume;
    }
    return true;
}

bool PlayerController::toggleMute() {
    const bool target_muted = !preferences_.muted;
    const double original_volume = preferences_.volume;
    double target_volume = preferences_.volume;
    if (!target_muted && target_volume == 0.0) {
        target_volume = last_nonzero_volume_;
        if (playback_backend_.setVolume(target_volume) != PlaybackCommandResult::Accepted) {
            return false;
        }
    }

    if (playback_backend_.setMuted(target_muted) != PlaybackCommandResult::Accepted) {
        if (target_volume != original_volume) {
            (void)playback_backend_.setVolume(original_volume);
        }
        return false;
    }

    preferences_.muted = target_muted;
    preferences_.volume = target_volume;
    return true;
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

void PlayerController::setRepeatQueueEnabled(const bool enabled) { preferences_.repeat_queue_enabled = enabled; }

void PlayerController::setShuffleQueueEnabled(const bool enabled) { preferences_.shuffle_queue_enabled = enabled; }

bool PlayerController::setDownloadDirectory(std::string directory) {
    if ((!directory.empty() && !validDownloadDirectory(directory)) || directory == preferences_.download_directory)
        return false;
    preferences_.download_directory = std::move(directory);
    return true;
}

bool PlayerController::setWindowPlacement(const WindowPlacement placement) {
    if (placement.width <= 0 || placement.height <= 0)
        return false;
    preferences_.window_placement = placement;
    return true;
}

CloseDisposition PlayerController::requestClose() {
    if (preferences_.close_behavior == CloseBehavior::Exit) {
        requestExit();
        return CloseDisposition::ExitRequested;
    }

    return requestHide();
}

CloseDisposition PlayerController::requestHide() {
    if (platform_.trayAvailable()) {
        platform_.hideWindow();
        return CloseDisposition::HiddenToTray;
    }

    platform_.minimizeWindow();
    return CloseDisposition::Minimized;
}

void PlayerController::requestExit() {
    if (exit_requested_) {
        return;
    }

    exit_requested_ = true;
    static_cast<void>(playback_backend_.shutdown());
    platform_.requestExit();
}

void PlayerController::requestRestore() { platform_.showAndActivateWindow(); }

} // namespace music_player::m0
