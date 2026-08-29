#include "m0/adapters/QtMultimediaPlaybackBackend.hpp"

#include <QUrl>

#include <algorithm>
#include <cmath>

namespace music_player::m0 {
namespace {

[[nodiscard]] bool snapshotsEqual(const PlaybackSnapshot& left, const PlaybackSnapshot& right) noexcept {
    return left.status == right.status && left.position_ms == right.position_ms &&
           left.duration_ms == right.duration_ms && left.seekable == right.seekable;
}

} // namespace

QtMultimediaPlaybackBackend::QtMultimediaPlaybackBackend(QObject* parent) : QObject(parent) {
    player_.setAudioOutput(&audio_output_);

    connect(&player_, &QMediaPlayer::mediaStatusChanged, this, [this] { refreshSnapshot(); });
    connect(&player_, &QMediaPlayer::playbackStateChanged, this, [this] { refreshSnapshot(); });
    connect(&player_, &QMediaPlayer::durationChanged, this, [this] { refreshSnapshot(); });
    connect(&player_, &QMediaPlayer::positionChanged, this, [this] { refreshSnapshot(); });
    connect(&player_, &QMediaPlayer::seekableChanged, this, [this] { refreshSnapshot(); });
    connect(&player_, &QMediaPlayer::errorOccurred, this, [this] { refreshSnapshot(); });
}

const PlaybackSnapshot& QtMultimediaPlaybackBackend::snapshot() const noexcept { return snapshot_; }

void QtMultimediaPlaybackBackend::load(const std::filesystem::path& source_path) {
    publishSnapshot({PlaybackStatus::Loading, 0, 0, false});
    player_.setSource(QUrl::fromLocalFile(QString::fromStdWString(source_path.wstring())));
}

void QtMultimediaPlaybackBackend::play() { player_.play(); }

void QtMultimediaPlaybackBackend::pause() { player_.pause(); }

void QtMultimediaPlaybackBackend::seekTo(const std::int64_t position_ms) {
    if (snapshot_.seekable) {
        player_.setPosition(std::clamp(position_ms, std::int64_t{0}, snapshot_.duration_ms));
    }
}

void QtMultimediaPlaybackBackend::setVolume(const double volume) {
    if (std::isfinite(volume) && volume >= 0.0 && volume <= 1.0) {
        audio_output_.setVolume(static_cast<float>(volume));
    }
}

void QtMultimediaPlaybackBackend::setMuted(const bool muted) { audio_output_.setMuted(muted); }

void QtMultimediaPlaybackBackend::refreshSnapshot() {
    const auto duration = std::max<std::int64_t>(0, player_.duration());
    const auto position = std::clamp<std::int64_t>(player_.position(), 0, duration);
    const bool seekable = player_.isSeekable();

    if (player_.error() != QMediaPlayer::NoError) {
        publishSnapshot({PlaybackStatus::Error, position, duration, false});
        return;
    }

    PlaybackStatus status = PlaybackStatus::Loading;
    switch (player_.mediaStatus()) {
    case QMediaPlayer::NoMedia:
        status = PlaybackStatus::Empty;
        break;
    case QMediaPlayer::LoadingMedia:
        status = PlaybackStatus::Loading;
        break;
    case QMediaPlayer::LoadedMedia:
    case QMediaPlayer::BufferingMedia:
    case QMediaPlayer::BufferedMedia:
    case QMediaPlayer::StalledMedia:
        if (duration <= 0 || !seekable) {
            status = PlaybackStatus::Loading;
        } else if (player_.playbackState() == QMediaPlayer::PlayingState) {
            status = PlaybackStatus::Playing;
        } else if (player_.playbackState() == QMediaPlayer::PausedState) {
            status = PlaybackStatus::Paused;
        } else {
            status = PlaybackStatus::Ready;
        }
        break;
    case QMediaPlayer::EndOfMedia:
        status = PlaybackStatus::Paused;
        break;
    case QMediaPlayer::InvalidMedia:
        status = PlaybackStatus::Error;
        break;
    }

    publishSnapshot({status, position, duration, seekable});
}

void QtMultimediaPlaybackBackend::publishSnapshot(const PlaybackSnapshot& next) {
    if (snapshotsEqual(snapshot_, next)) {
        return;
    }
    snapshot_ = next;
    emit snapshotChanged();
}

} // namespace music_player::m0
