#pragma once

#include "m0/domain/PlaybackTypes.hpp"
#include "m0/ports/PlaybackBackend.hpp"

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QObject>
#include <QTimer>

#include <filesystem>

namespace music_player::m0 {

class QtMultimediaPlaybackBackend final : public QObject, public PlaybackBackend {
    Q_OBJECT

  public:
    explicit QtMultimediaPlaybackBackend(QObject* parent = nullptr);
    ~QtMultimediaPlaybackBackend() override;

    [[nodiscard]] const PlaybackSnapshot& snapshot() const noexcept override;
    void setEventCallback(PlaybackEventCallback callback) override;
    PlaybackCommandResult load(const std::filesystem::path& source_path) override;
    PlaybackCommandResult play() override;
    PlaybackCommandResult pause() override;
    PlaybackCommandResult seekTo(std::int64_t position_ms) override;
    PlaybackCommandResult setVolume(double volume) override;
    PlaybackCommandResult setMuted(bool muted) override;
    PlaybackCommandResult unload() override;
    PlaybackCommandResult shutdown() override;

  private:
    void createPlayerForCurrentGeneration();
    void connectPlayerSignals(QMediaPlayer& player);
    void refreshSnapshot(const QMediaPlayer* signal_player, std::uint64_t signal_generation);
    void validateLoadedAudioTrack();
    void publishSnapshot(const PlaybackSnapshot& next);
    void publishEvent(PlaybackEventType type);
    void invalidateCurrentSource();
    void advanceGeneration() noexcept;

    QAudioOutput audio_output_;
    QTimer audio_track_validation_timer_;
    QMediaPlayer* player_{nullptr};
    PlaybackSnapshot snapshot_{};
    std::uint64_t generation_{0};
    PlaybackEventCallback event_callback_{};
    bool ended_emitted_{false};
    bool repositioning_from_end_{false};
    bool missing_audio_rejected_{false};
    bool shutdown_{false};
};

} // namespace music_player::m0
