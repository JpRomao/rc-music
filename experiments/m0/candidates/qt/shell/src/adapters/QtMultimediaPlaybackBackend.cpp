#include "m0/adapters/QtMultimediaPlaybackBackend.hpp"

#include <QUrl>

#include <algorithm>
#include <cmath>
#include <utility>

namespace music_player::m0 {
namespace {

constexpr int audio_track_validation_grace_ms = 100;

[[nodiscard]] PlaybackErrorCode errorCode(const QMediaPlayer::Error error) noexcept {
    switch (error) {
    case QMediaPlayer::NoError:
        return PlaybackErrorCode::None;
    case QMediaPlayer::ResourceError:
        return PlaybackErrorCode::SourceUnavailable;
    case QMediaPlayer::FormatError:
        return PlaybackErrorCode::InvalidMedia;
    case QMediaPlayer::NetworkError:
        return PlaybackErrorCode::NetworkFailure;
    case QMediaPlayer::AccessDeniedError:
        return PlaybackErrorCode::AccessDenied;
    }
    return PlaybackErrorCode::BackendFailure;
}

[[nodiscard]] QString localFilePath(const std::filesystem::path& source_path) {
#ifdef Q_OS_WIN
    return QString::fromStdWString(source_path.native());
#else
    const auto& native = source_path.native();
    return QString::fromUtf8(native.data(), static_cast<qsizetype>(native.size()));
#endif
}

} // namespace

QtMultimediaPlaybackBackend::QtMultimediaPlaybackBackend(QObject* parent) : QObject(parent) {
    audio_track_validation_timer_.setSingleShot(true);
    audio_track_validation_timer_.setInterval(audio_track_validation_grace_ms);
    connect(&audio_track_validation_timer_, &QTimer::timeout, this, [this] { validateLoadedAudioTrack(); });
}

QtMultimediaPlaybackBackend::~QtMultimediaPlaybackBackend() {
    const auto players = findChildren<QMediaPlayer*>(QString{}, Qt::FindDirectChildrenOnly);
    for (QMediaPlayer* const player : players) {
        player->setAudioOutput(nullptr);
        delete player;
    }
    player_ = nullptr;
}

void QtMultimediaPlaybackBackend::createPlayerForCurrentGeneration() {
    player_ = new QMediaPlayer(this);
    player_->setAudioOutput(&audio_output_);
    connectPlayerSignals(*player_);
}

void QtMultimediaPlaybackBackend::connectPlayerSignals(QMediaPlayer& player) {
    const auto signal_generation = generation_;
    QMediaPlayer* const signal_player = &player;
    connect(&player, &QMediaPlayer::mediaStatusChanged, this,
            [this, signal_player, signal_generation] { refreshSnapshot(signal_player, signal_generation); });
    connect(&player, &QMediaPlayer::playbackStateChanged, this,
            [this, signal_player, signal_generation] { refreshSnapshot(signal_player, signal_generation); });
    connect(&player, &QMediaPlayer::durationChanged, this,
            [this, signal_player, signal_generation] { refreshSnapshot(signal_player, signal_generation); });
    connect(&player, &QMediaPlayer::positionChanged, this,
            [this, signal_player, signal_generation] { refreshSnapshot(signal_player, signal_generation); });
    connect(&player, &QMediaPlayer::seekableChanged, this,
            [this, signal_player, signal_generation] { refreshSnapshot(signal_player, signal_generation); });
    connect(&player, &QMediaPlayer::hasAudioChanged, this,
            [this, signal_player, signal_generation] { refreshSnapshot(signal_player, signal_generation); });
    connect(&player, &QMediaPlayer::errorOccurred, this,
            [this, signal_player, signal_generation] { refreshSnapshot(signal_player, signal_generation); });
}

const PlaybackSnapshot& QtMultimediaPlaybackBackend::snapshot() const noexcept { return snapshot_; }

void QtMultimediaPlaybackBackend::setEventCallback(PlaybackEventCallback callback) {
    event_callback_ = std::move(callback);
}

PlaybackCommandResult QtMultimediaPlaybackBackend::load(const std::filesystem::path& source_path) {
    if (shutdown_) {
        return PlaybackCommandResult::Shutdown;
    }
    if (source_path.empty()) {
        return PlaybackCommandResult::InvalidArgument;
    }

    invalidateCurrentSource();
    advanceGeneration();
    ended_emitted_ = false;
    repositioning_from_end_ = false;
    missing_audio_rejected_ = false;
    createPlayerForCurrentGeneration();
    QMediaPlayer* const load_player = player_;
    const auto load_generation = generation_;
    publishSnapshot({PlaybackStatus::Loading, 0, 0, false, generation_});
    if (!shutdown_ && generation_ == load_generation && player_ == load_player) {
        load_player->setSource(QUrl::fromLocalFile(localFilePath(source_path)));
    }
    return PlaybackCommandResult::Accepted;
}

PlaybackCommandResult QtMultimediaPlaybackBackend::play() {
    if (shutdown_) {
        return PlaybackCommandResult::Shutdown;
    }
    if (snapshot_.status == PlaybackStatus::Playing) {
        return PlaybackCommandResult::Accepted;
    }
    if (snapshot_.status != PlaybackStatus::Ready && snapshot_.status != PlaybackStatus::Paused) {
        return PlaybackCommandResult::NotReady;
    }
    if (player_ == nullptr) {
        return PlaybackCommandResult::NotReady;
    }
    QMediaPlayer* const command_player = player_;
    const auto command_generation = generation_;
    if (snapshot_.duration_ms > 0 && snapshot_.position_ms >= snapshot_.duration_ms) {
        repositioning_from_end_ = true;
        command_player->setPosition(0);
        if (shutdown_ || generation_ != command_generation || player_ != command_player) {
            return PlaybackCommandResult::Accepted;
        }
        repositioning_from_end_ = false;
        ended_emitted_ = false;
    }
    command_player->play();
    return PlaybackCommandResult::Accepted;
}

PlaybackCommandResult QtMultimediaPlaybackBackend::pause() {
    if (shutdown_) {
        return PlaybackCommandResult::Shutdown;
    }
    if (snapshot_.status == PlaybackStatus::Paused || snapshot_.status == PlaybackStatus::Ready) {
        return PlaybackCommandResult::Accepted;
    }
    if (snapshot_.status != PlaybackStatus::Playing) {
        return PlaybackCommandResult::NotReady;
    }
    if (player_ == nullptr) {
        return PlaybackCommandResult::NotReady;
    }
    player_->pause();
    return PlaybackCommandResult::Accepted;
}

PlaybackCommandResult QtMultimediaPlaybackBackend::seekTo(const std::int64_t position_ms) {
    if (shutdown_) {
        return PlaybackCommandResult::Shutdown;
    }
    if (snapshot_.status != PlaybackStatus::Ready && snapshot_.status != PlaybackStatus::Paused &&
        snapshot_.status != PlaybackStatus::Playing) {
        return PlaybackCommandResult::NotReady;
    }
    if (!snapshot_.seekable) {
        return PlaybackCommandResult::NotSeekable;
    }
    if (player_ == nullptr) {
        return PlaybackCommandResult::NotReady;
    }

    const auto target = std::clamp(position_ms, std::int64_t{0}, snapshot_.duration_ms);
    QMediaPlayer* const command_player = player_;
    const auto command_generation = generation_;
    const bool repositioning_from_end = ended_emitted_ && target < snapshot_.duration_ms;
    if (repositioning_from_end) {
        repositioning_from_end_ = true;
    }
    command_player->setPosition(target);
    if (shutdown_ || generation_ != command_generation || player_ != command_player) {
        return PlaybackCommandResult::Accepted;
    }
    if (repositioning_from_end) {
        repositioning_from_end_ = false;
        ended_emitted_ = false;
    }
    return PlaybackCommandResult::Accepted;
}

PlaybackCommandResult QtMultimediaPlaybackBackend::setVolume(const double volume) {
    if (shutdown_) {
        return PlaybackCommandResult::Shutdown;
    }
    if (!std::isfinite(volume) || volume < 0.0 || volume > 1.0) {
        return PlaybackCommandResult::InvalidArgument;
    }
    audio_output_.setVolume(static_cast<float>(volume));
    return PlaybackCommandResult::Accepted;
}

PlaybackCommandResult QtMultimediaPlaybackBackend::setMuted(const bool muted) {
    if (shutdown_) {
        return PlaybackCommandResult::Shutdown;
    }
    audio_output_.setMuted(muted);
    return PlaybackCommandResult::Accepted;
}

PlaybackCommandResult QtMultimediaPlaybackBackend::unload() {
    if (shutdown_) {
        return PlaybackCommandResult::Shutdown;
    }
    if (snapshot_.status == PlaybackStatus::Empty && player_ == nullptr) {
        return PlaybackCommandResult::Accepted;
    }

    invalidateCurrentSource();
    advanceGeneration();
    ended_emitted_ = false;
    repositioning_from_end_ = false;
    publishSnapshot({PlaybackStatus::Empty, 0, 0, false, generation_});
    return PlaybackCommandResult::Accepted;
}

PlaybackCommandResult QtMultimediaPlaybackBackend::shutdown() {
    if (shutdown_) {
        return PlaybackCommandResult::Accepted;
    }

    invalidateCurrentSource();
    advanceGeneration();
    ended_emitted_ = false;
    repositioning_from_end_ = false;
    shutdown_ = true;
    publishSnapshot({PlaybackStatus::Empty, 0, 0, false, generation_});
    return PlaybackCommandResult::Accepted;
}

void QtMultimediaPlaybackBackend::invalidateCurrentSource() {
    audio_track_validation_timer_.stop();
    missing_audio_rejected_ = false;
    if (player_ == nullptr) {
        return;
    }

    QMediaPlayer* const retired_player = player_;
    player_ = nullptr;
    disconnect(retired_player, nullptr, this, nullptr);
    retired_player->stop();
    retired_player->setSource({});
    retired_player->setAudioOutput(nullptr);
    retired_player->deleteLater();
}

void QtMultimediaPlaybackBackend::advanceGeneration() noexcept {
    ++generation_;
    if (generation_ == 0) {
        ++generation_;
    }
}

void QtMultimediaPlaybackBackend::refreshSnapshot(const QMediaPlayer* const signal_player,
                                                  const std::uint64_t signal_generation) {
    if (shutdown_ || signal_player == nullptr || signal_player != player_ || signal_generation != generation_) {
        return;
    }

    const auto duration = std::max<std::int64_t>(0, player_->duration());
    const auto position = std::clamp<std::int64_t>(player_->position(), 0, duration);
    const bool seekable = player_->isSeekable();

    if (player_->error() != QMediaPlayer::NoError) {
        audio_track_validation_timer_.stop();
        publishSnapshot({PlaybackStatus::Error, position, duration, false, generation_, errorCode(player_->error())});
        return;
    }

    PlaybackStatus status = PlaybackStatus::Loading;
    switch (player_->mediaStatus()) {
    case QMediaPlayer::NoMedia:
        audio_track_validation_timer_.stop();
        status = PlaybackStatus::Empty;
        break;
    case QMediaPlayer::LoadingMedia:
        audio_track_validation_timer_.stop();
        status = PlaybackStatus::Loading;
        break;
    case QMediaPlayer::LoadedMedia:
    case QMediaPlayer::BufferingMedia:
    case QMediaPlayer::BufferedMedia:
    case QMediaPlayer::StalledMedia:
        if (!player_->hasAudio()) {
            if (missing_audio_rejected_) {
                publishSnapshot(
                    {PlaybackStatus::Error, position, duration, false, generation_, PlaybackErrorCode::InvalidMedia});
                return;
            }
            if (!audio_track_validation_timer_.isActive()) {
                audio_track_validation_timer_.start();
            }
            publishSnapshot({PlaybackStatus::Loading, position, duration, false, generation_});
            return;
        }
        audio_track_validation_timer_.stop();
        missing_audio_rejected_ = false;
        if (player_->playbackState() == QMediaPlayer::PlayingState) {
            status = PlaybackStatus::Playing;
        } else if (player_->playbackState() == QMediaPlayer::PausedState) {
            status = PlaybackStatus::Paused;
        } else if (snapshot_.status == PlaybackStatus::Playing && duration > 0 && position >= duration) {
            return;
        } else if (snapshot_.status == PlaybackStatus::Paused || (duration > 0 && position >= duration)) {
            status = PlaybackStatus::Paused;
        } else {
            status = PlaybackStatus::Ready;
        }
        break;
    case QMediaPlayer::EndOfMedia: {
        audio_track_validation_timer_.stop();
        if (repositioning_from_end_ && position < duration) {
            publishSnapshot({PlaybackStatus::Paused, position, duration, seekable, generation_});
            return;
        }
        if (ended_emitted_) {
            publishSnapshot({PlaybackStatus::Paused, duration, duration, seekable, generation_});
            return;
        }
        ended_emitted_ = true;
        QMediaPlayer* const ended_player = player_;
        publishSnapshot({PlaybackStatus::Paused, duration, duration, seekable, generation_});
        if (!shutdown_ && generation_ == signal_generation && player_ == ended_player &&
            snapshot_.generation == signal_generation && snapshot_.status == PlaybackStatus::Paused &&
            snapshot_.position_ms == snapshot_.duration_ms) {
            publishEvent(PlaybackEventType::Ended);
        }
        return;
    }
    case QMediaPlayer::InvalidMedia:
        audio_track_validation_timer_.stop();
        publishSnapshot(
            {PlaybackStatus::Error, position, duration, false, generation_, PlaybackErrorCode::InvalidMedia});
        return;
    }

    publishSnapshot({status, position, duration, seekable, generation_});
}

void QtMultimediaPlaybackBackend::validateLoadedAudioTrack() {
    if (shutdown_ || player_ == nullptr || player_->error() != QMediaPlayer::NoError || player_->hasAudio()) {
        return;
    }

    switch (player_->mediaStatus()) {
    case QMediaPlayer::LoadedMedia:
    case QMediaPlayer::BufferingMedia:
    case QMediaPlayer::BufferedMedia:
    case QMediaPlayer::StalledMedia:
        break;
    case QMediaPlayer::NoMedia:
    case QMediaPlayer::LoadingMedia:
    case QMediaPlayer::EndOfMedia:
    case QMediaPlayer::InvalidMedia:
        return;
    }

    missing_audio_rejected_ = true;
    const auto duration = std::max<std::int64_t>(0, player_->duration());
    const auto position = std::clamp<std::int64_t>(player_->position(), 0, duration);
    publishSnapshot({PlaybackStatus::Error, position, duration, false, generation_, PlaybackErrorCode::InvalidMedia});
}

void QtMultimediaPlaybackBackend::publishSnapshot(const PlaybackSnapshot& next) {
    if (snapshot_ == next) {
        return;
    }
    snapshot_ = next;
    publishEvent(PlaybackEventType::SnapshotChanged);
}

void QtMultimediaPlaybackBackend::publishEvent(const PlaybackEventType type) {
    if (!event_callback_) {
        return;
    }
    const auto callback = event_callback_;
    const PlaybackEvent event{type, snapshot_};
    callback(event);
}

} // namespace music_player::m0
