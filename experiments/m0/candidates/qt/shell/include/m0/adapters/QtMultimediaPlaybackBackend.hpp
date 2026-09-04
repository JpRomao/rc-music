#pragma once

#include "m0/domain/PlaybackTypes.hpp"
#include "m0/ports/PlaybackBackend.hpp"

#include <QAudioDevice>
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QObject>
#include <QTimer>

#include <filesystem>
#include <functional>
#include <memory>

namespace music_player::m0 {

struct QtAudioOutputDevice {
    // An empty ID represents the absence of an output device.
    QByteArray id;
    QAudioDevice device;
};

class QtAudioOutputDevices {
  public:
    virtual ~QtAudioOutputDevices() = default;
    [[nodiscard]] virtual QtAudioOutputDevice defaultDevice() const = 0;
    virtual void applyDevice(QAudioOutput& output, const QtAudioOutputDevice& device) = 0;
    virtual void setChangeHandler(std::function<void()> handler) = 0;
};

class QtMultimediaPlaybackBackend final : public QObject, public PlaybackBackend {
    Q_OBJECT

  public:
    explicit QtMultimediaPlaybackBackend(QObject* parent = nullptr);
    explicit QtMultimediaPlaybackBackend(std::unique_ptr<QtAudioOutputDevices> devices, QObject* parent = nullptr);
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
    bool interruptIfDefaultOutputChanged();
    void refreshSnapshot(const QMediaPlayer* signal_player, std::uint64_t signal_generation);
    void validateLoadedAudioTrack();
    void publishSnapshot(const PlaybackSnapshot& next);
    void publishEvent(PlaybackEventType type);
    void invalidateCurrentSource();
    void advanceGeneration() noexcept;

    QAudioOutput audio_output_;
    std::unique_ptr<QtAudioOutputDevices> audio_devices_;
    QByteArray active_output_id_;
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
