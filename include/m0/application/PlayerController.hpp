#pragma once

#include "m0/domain/PlaybackTypes.hpp"
#include "m0/ports/PlatformIntegration.hpp"
#include "m0/ports/PlaybackBackend.hpp"

#include <cstdint>
#include <optional>

namespace music_player::m0 {

class PlayerController final {
  public:
    PlayerController(PlaybackBackend& playback_backend, PlatformIntegration& platform,
                     PlaybackSnapshot initial_snapshot = {});
    PlayerController(PlaybackBackend& playback_backend, PlatformIntegration& platform,
                     PlaybackSnapshot initial_snapshot, Preferences initial_preferences);

    [[nodiscard]] const Preferences& preferences() const noexcept;
    [[nodiscard]] const PlaybackSnapshot& confirmedSnapshot() const noexcept;
    [[nodiscard]] PlaybackStatus effectivePlaybackStatus() const noexcept;
    [[nodiscard]] std::int64_t displayedPositionMs() const noexcept;
    [[nodiscard]] bool seekPreviewActive() const noexcept;
    [[nodiscard]] bool seekPending() const noexcept;

    [[nodiscard]] bool play();
    [[nodiscard]] bool pause();
    [[nodiscard]] bool togglePlayback();
    [[nodiscard]] bool skipBackward();
    [[nodiscard]] bool skipForward();
    void beginSeekPreview(std::int64_t position_ms);
    void updateSeekPreview(std::int64_t position_ms);
    bool commitSeekPreview();
    void rejectPendingSeek();
    void applyBackendSnapshot(PlaybackSnapshot snapshot);

    void setTheme(ThemeMode theme);
    void setMotionMode(MotionMode motion);
    [[nodiscard]] bool setSkipBackwardSeconds(int seconds);
    [[nodiscard]] bool setSkipForwardSeconds(int seconds);
    [[nodiscard]] bool setVolume(double volume);
    bool toggleMute();
    void setCloseBehavior(CloseBehavior behavior);
    void setRestoreLastSession(bool restore);
    [[nodiscard]] bool setStartMinimized(bool start_minimized);
    void setQueueEndBehavior(QueueEndBehavior behavior);
    void setRepeatQueueEnabled(bool enabled);
    void setShuffleQueueEnabled(bool enabled);
    [[nodiscard]] bool setWindowPlacement(WindowPlacement placement);

    [[nodiscard]] CloseDisposition requestClose();
    [[nodiscard]] CloseDisposition requestHide();
    void requestExit();
    void requestRestore();

  private:
    [[nodiscard]] bool requestTransport(PlaybackStatus target);
    [[nodiscard]] bool issueTransportCommand(PlaybackStatus target);

    PlaybackBackend& playback_backend_;
    PlatformIntegration& platform_;
    Preferences preferences_{};
    PlaybackSnapshot confirmed_snapshot_{};
    std::int64_t displayed_position_ms_{0};
    std::int64_t preview_position_ms_{0};
    double last_nonzero_volume_{Preferences{}.volume};
    bool seek_preview_active_{false};
    bool seek_pending_{false};
    std::optional<PlaybackStatus> desired_transport_status_{};
    std::optional<PlaybackStatus> in_flight_transport_status_{};
    bool exit_requested_{false};
};

} // namespace music_player::m0
