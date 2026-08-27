#pragma once

#include "m0/domain/PlaybackTypes.hpp"
#include "m0/ports/PlatformIntegration.hpp"
#include "m0/ports/PlaybackBackend.hpp"

#include <cstdint>

namespace music_player::m0 {

class PlayerController final {
  public:
    PlayerController(PlaybackBackend& playback_backend, PlatformIntegration& platform,
                     PlaybackSnapshot initial_snapshot = {});

    [[nodiscard]] const Preferences& preferences() const noexcept;
    [[nodiscard]] const PlaybackSnapshot& confirmedSnapshot() const noexcept;
    [[nodiscard]] std::int64_t displayedPositionMs() const noexcept;
    [[nodiscard]] bool seekPreviewActive() const noexcept;
    [[nodiscard]] bool seekPending() const noexcept;

    [[nodiscard]] bool togglePlayback();
    [[nodiscard]] bool skipBackward();
    [[nodiscard]] bool skipForward();
    void beginSeekPreview(std::int64_t position_ms);
    void updateSeekPreview(std::int64_t position_ms);
    void commitSeekPreview();
    void rejectPendingSeek();
    void applyBackendSnapshot(PlaybackSnapshot snapshot);

    void setTheme(ThemeMode theme);
    void setMotionMode(MotionMode motion);
    [[nodiscard]] bool setSkipBackwardSeconds(int seconds);
    [[nodiscard]] bool setSkipForwardSeconds(int seconds);
    [[nodiscard]] bool setVolume(double volume);
    void toggleMute();
    void setCloseBehavior(CloseBehavior behavior);
    void setRestoreLastSession(bool restore);
    [[nodiscard]] bool setStartMinimized(bool start_minimized);
    void setQueueEndBehavior(QueueEndBehavior behavior);

    [[nodiscard]] CloseDisposition requestClose();
    [[nodiscard]] CloseDisposition requestHide();
    void requestExit();
    void requestRestore();

  private:
    PlaybackBackend& playback_backend_;
    PlatformIntegration& platform_;
    Preferences preferences_{};
    PlaybackSnapshot confirmed_snapshot_{};
    std::int64_t displayed_position_ms_{0};
    std::int64_t preview_position_ms_{0};
    double last_nonzero_volume_{0.8};
    bool seek_preview_active_{false};
    bool seek_pending_{false};
};

} // namespace music_player::m0
