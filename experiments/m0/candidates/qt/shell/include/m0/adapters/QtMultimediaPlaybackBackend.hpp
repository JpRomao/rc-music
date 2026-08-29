#pragma once

#include "m0/domain/PlaybackTypes.hpp"
#include "m0/ports/PlaybackBackend.hpp"

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QObject>

#include <filesystem>

namespace music_player::m0 {

class QtMultimediaPlaybackBackend final : public QObject, public PlaybackBackend {
    Q_OBJECT

  public:
    explicit QtMultimediaPlaybackBackend(QObject* parent = nullptr);

    [[nodiscard]] const PlaybackSnapshot& snapshot() const noexcept;
    void load(const std::filesystem::path& source_path);

    void play() override;
    void pause() override;
    void seekTo(std::int64_t position_ms) override;
    void setVolume(double volume) override;
    void setMuted(bool muted) override;

  signals:
    void snapshotChanged();

  private:
    void refreshSnapshot();
    void publishSnapshot(const PlaybackSnapshot& next);

    QAudioOutput audio_output_;
    QMediaPlayer player_;
    PlaybackSnapshot snapshot_{};
};

} // namespace music_player::m0
