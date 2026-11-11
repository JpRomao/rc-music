#pragma once

#include "m0/domain/PlaybackTypes.hpp"
#include "m0/ports/AudioOutputSelection.hpp"
#include "m0/ports/PlaybackBackend.hpp"

#include <QAudioDevice>
#include <QAudioOutput>
#include <QList>
#include <QMediaPlayer>
#include <QObject>
#include <QString>
#include <QTimer>

#include <filesystem>
#include <functional>
#include <memory>

namespace music_player::m0 {

struct QtAudioOutputDevice {
    // An empty ID represents the absence of an output device.
    QByteArray id;
    QAudioDevice device;
    QString description{};
};

class QtAudioOutputDevices {
  public:
    virtual ~QtAudioOutputDevices() = default;
    [[nodiscard]] virtual QtAudioOutputDevice defaultDevice() const = 0;
    [[nodiscard]] virtual QList<QtAudioOutputDevice> devices() const {
        const auto device = defaultDevice();
        if (device.id.isEmpty()) {
            return {};
        }
        return {device};
    }
    virtual void applyDevice(QAudioOutput& output, const QtAudioOutputDevice& device) = 0;
    virtual void setChangeHandler(std::function<void()> handler) = 0;
};

class QtMultimediaPlaybackBackend final : public QObject, public PlaybackBackend, public AudioOutputSelection {
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
    [[nodiscard]] std::vector<AudioOutputDevice> audioOutputs() const override;
    [[nodiscard]] std::string selectedAudioOutputId() const override;
    bool selectAudioOutput(std::string_view id) override;
    void setAudioOutputsChangedCallback(std::function<void()> callback) override;
    // Receives Windows volume notifications on the backend's thread.
    void sourceVolumeRemoved(std::uint32_t drive_mask);

  private:
    void createPlayerForCurrentGeneration();
    void connectPlayerSignals(QMediaPlayer& player);
    [[nodiscard]] QtAudioOutputDevice configuredOutputDevice() const;
    void reconcileAudioOutputs();
    bool interruptIfOutputChanged();
    void refreshSnapshot(const QMediaPlayer* signal_player, std::uint64_t signal_generation);
    void validateLoadedAudioTrack();
    void publishSnapshot(const PlaybackSnapshot& next);
    void publishEvent(PlaybackEventType type);
    void invalidateCurrentSource();
    void advanceGeneration() noexcept;

    QAudioOutput audio_output_;
    std::unique_ptr<QtAudioOutputDevices> audio_devices_;
    QByteArray selected_output_id_;
    QByteArray active_output_id_;
    std::function<void()> audio_outputs_changed_;
    QTimer audio_track_validation_timer_;
    QMediaPlayer* player_{nullptr};
    PlaybackSnapshot snapshot_{};
    std::uint64_t generation_{0};
    std::uint32_t source_drive_mask_{0};
    PlaybackEventCallback event_callback_{};
    bool source_volume_removal_pending_{false};
    bool ended_emitted_{false};
    bool repositioning_from_end_{false};
    bool missing_audio_rejected_{false};
    bool shutdown_{false};
};

} // namespace music_player::m0
