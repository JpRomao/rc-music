#include "m0/adapters/QtMultimediaPlaybackBackend.hpp"

#include <QMediaDevices>
#include <QPointer>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

namespace music_player::m0 {
namespace {

constexpr int audio_track_validation_grace_ms = 100;

class SystemQtAudioOutputDevices final : public QtAudioOutputDevices {
  public:
    SystemQtAudioOutputDevices() {
        QObject::connect(&devices_, &QMediaDevices::audioOutputsChanged, &devices_, [this] {
            const auto callback = handler_;
            if (callback) {
                callback();
            }
        });
    }

    [[nodiscard]] QtAudioOutputDevice defaultDevice() const override {
        const auto device = QMediaDevices::defaultAudioOutput();
        return device.isNull() ? QtAudioOutputDevice{} : QtAudioOutputDevice{device.id(), device, device.description()};
    }

    [[nodiscard]] QList<QtAudioOutputDevice> devices() const override {
        const auto outputs = QMediaDevices::audioOutputs();
        QList<QtAudioOutputDevice> devices;
        devices.reserve(outputs.size());
        for (const auto& device : outputs) {
            devices.append({device.id(), device, device.description()});
        }
        return devices;
    }

    void applyDevice(QAudioOutput& output, const QtAudioOutputDevice& device) override {
        output.setDevice(device.device);
    }

    void setChangeHandler(std::function<void()> handler) override { handler_ = std::move(handler); }

  private:
    QMediaDevices devices_;
    std::function<void()> handler_;
};

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

[[nodiscard]] std::int64_t steadyNowMs() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

[[nodiscard]] std::uint32_t sourceDriveMask(const std::filesystem::path& source_path) noexcept {
#ifdef Q_OS_WIN
    const auto& path = source_path.native();
    const bool extended = path.starts_with(L"\\\\?\\");
    const std::size_t offset = extended ? 4 : 0;
    if (path.size() < offset + 3 || path[offset + 1] != L':')
        return 0;
    const auto separator = path[offset + 2];
    if (separator != L'\\' && (extended || separator != L'/'))
        return 0;
    const auto drive = path[offset];
    if (drive >= L'A' && drive <= L'Z')
        return std::uint32_t{1} << (drive - L'A');
    if (drive >= L'a' && drive <= L'z')
        return std::uint32_t{1} << (drive - L'a');
#else
    static_cast<void>(source_path);
#endif
    return 0;
}

} // namespace

QtMultimediaPlaybackBackend::QtMultimediaPlaybackBackend(QObject* parent)
    : QtMultimediaPlaybackBackend(std::make_unique<SystemQtAudioOutputDevices>(), parent) {}

QtMultimediaPlaybackBackend::QtMultimediaPlaybackBackend(std::unique_ptr<QtAudioOutputDevices> devices, QObject* parent)
    : QObject(parent), audio_devices_(devices ? std::move(devices) : std::make_unique<SystemQtAudioOutputDevices>()) {
    audio_track_validation_timer_.setSingleShot(true);
    audio_track_validation_timer_.setInterval(audio_track_validation_grace_ms);
    connect(&audio_track_validation_timer_, &QTimer::timeout, this, [this] { validateLoadedAudioTrack(); });
    audio_devices_->setChangeHandler([guard = QPointer<QtMultimediaPlaybackBackend>{this}] {
        if (guard) {
            guard->reconcileAudioOutputs();
        }
    });
}

QtMultimediaPlaybackBackend::~QtMultimediaPlaybackBackend() {
    shutdown_ = true;
    audio_devices_->setChangeHandler({});
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
    connect(&player, &QMediaPlayer::positionChanged, this, [this, signal_player, signal_generation] {
        if (signal_player == player_ && signal_generation == generation_)
            position_updated_at_ms_ = steadyNowMs();
        refreshSnapshot(signal_player, signal_generation);
    });
    connect(&player, &QMediaPlayer::seekableChanged, this,
            [this, signal_player, signal_generation] { refreshSnapshot(signal_player, signal_generation); });
    connect(&player, &QMediaPlayer::hasAudioChanged, this,
            [this, signal_player, signal_generation] { refreshSnapshot(signal_player, signal_generation); });
    connect(&player, &QMediaPlayer::errorOccurred, this,
            [this, signal_player, signal_generation] { refreshSnapshot(signal_player, signal_generation); });
}

const PlaybackSnapshot& QtMultimediaPlaybackBackend::snapshot() const noexcept { return snapshot_; }

bool QtMultimediaPlaybackBackend::sourceVolumeRemovalObserved(const std::uint64_t generation) const noexcept {
    return generation == generation_ &&
           (source_volume_removal_pending_ ||
            (snapshot_.generation == generation && snapshot_.error == PlaybackErrorCode::SourceVolumeRemoved));
}

void QtMultimediaPlaybackBackend::setEventCallback(PlaybackEventCallback callback) {
    event_callback_ = std::move(callback);
}

std::vector<AudioOutputDevice> QtMultimediaPlaybackBackend::audioOutputs() const {
    std::vector<AudioOutputDevice> result;
    const auto devices = audio_devices_->devices();
    result.reserve(static_cast<std::size_t>(devices.size()));
    for (const auto& device : devices) {
        if (!device.id.isEmpty()) {
            result.push_back({device.id.toHex().toStdString(), device.description.toUtf8().toStdString()});
        }
    }
    return result;
}

std::string QtMultimediaPlaybackBackend::selectedAudioOutputId() const {
    return selected_output_id_.toHex().toStdString();
}

bool QtMultimediaPlaybackBackend::selectAudioOutput(const std::string_view id) {
    if (shutdown_ || QThread::currentThread() != thread()) {
        return false;
    }

    QByteArray selected;
    if (!id.empty()) {
        const auto devices = audio_devices_->devices();
        const auto found = std::find_if(devices.cbegin(), devices.cend(),
                                        [id](const auto& device) { return device.id.toHex().toStdString() == id; });
        if (found == devices.cend()) {
            return false;
        }
        selected = found->id;
    }
    if (selected_output_id_ != selected) {
        selected_output_id_ = std::move(selected);
        reconcileAudioOutputs();
    }
    return true;
}

void QtMultimediaPlaybackBackend::setAudioOutputsChangedCallback(std::function<void()> callback) {
    if (!shutdown_) {
        audio_outputs_changed_ = std::move(callback);
    }
}

void QtMultimediaPlaybackBackend::sourceVolumeRemoved(const std::uint32_t drive_mask) {
    if (QThread::currentThread() != thread() || shutdown_ || player_ == nullptr || source_volume_removal_pending_ ||
        (drive_mask & source_drive_mask_) == 0)
        return;

    // Suppress decoder notifications as soon as Windows reports the loss.
    // Leave decoder teardown and application callbacks outside the native filter.
    source_volume_removal_pending_ = true;
    const auto removal_generation = generation_;
    const bool queued = QMetaObject::invokeMethod(
        this,
        [this, removal_generation] {
            if (shutdown_ || !source_volume_removal_pending_ || removal_generation != generation_)
                return;
            auto failed = snapshot_;
            failed.status = PlaybackStatus::Error;
            failed.seekable = false;
            failed.error = PlaybackErrorCode::SourceVolumeRemoved;
            const QPointer<QtMultimediaPlaybackBackend> guard{this};
            invalidateCurrentSource();
            if (guard && !shutdown_ && removal_generation == generation_)
                publishSnapshot(failed);
        },
        Qt::QueuedConnection);
    if (!queued)
        source_volume_removal_pending_ = false;
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
    const auto output_device = configuredOutputDevice();
    active_output_id_ = output_device.id;
    if (active_output_id_.isEmpty()) {
        publishSnapshot({PlaybackStatus::Error, 0, 0, false, generation_, PlaybackErrorCode::AudioOutputUnavailable});
        return PlaybackCommandResult::Accepted;
    }
    audio_devices_->applyDevice(audio_output_, output_device);
    source_drive_mask_ = sourceDriveMask(source_path);
    createPlayerForCurrentGeneration();
    QMediaPlayer* const load_player = player_;
    const auto load_generation = generation_;
    const QPointer<QtMultimediaPlaybackBackend> guard{this};
    publishSnapshot({PlaybackStatus::Loading, 0, 0, false, generation_});
    if (guard && !shutdown_ && !source_volume_removal_pending_ && generation_ == load_generation &&
        player_ == load_player) {
        load_player->setSource(QUrl::fromLocalFile(localFilePath(source_path)));
    }
    return PlaybackCommandResult::Accepted;
}

PlaybackCommandResult QtMultimediaPlaybackBackend::play() {
    if (shutdown_) {
        return PlaybackCommandResult::Shutdown;
    }
    if (source_volume_removal_pending_ || interruptIfOutputChanged()) {
        return PlaybackCommandResult::NotReady;
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
        if (shutdown_ || source_volume_removal_pending_ || generation_ != command_generation ||
            player_ != command_player) {
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
    if (source_volume_removal_pending_ || interruptIfOutputChanged()) {
        return PlaybackCommandResult::NotReady;
    }
    if (snapshot_.status == PlaybackStatus::Paused) {
        return PlaybackCommandResult::Accepted;
    }
    if (snapshot_.status != PlaybackStatus::Ready && snapshot_.status != PlaybackStatus::Playing) {
        return PlaybackCommandResult::NotReady;
    }
    if (player_ == nullptr) {
        return PlaybackCommandResult::NotReady;
    }
    if (snapshot_.status == PlaybackStatus::Ready) {
        PlaybackSnapshot paused = snapshot_;
        paused.status = PlaybackStatus::Paused;
        publishSnapshot(paused);
        return PlaybackCommandResult::Accepted;
    }
    player_->pause();
    return PlaybackCommandResult::Accepted;
}

PlaybackCommandResult QtMultimediaPlaybackBackend::seekTo(const std::int64_t position_ms) {
    if (shutdown_) {
        return PlaybackCommandResult::Shutdown;
    }
    if (source_volume_removal_pending_ || interruptIfOutputChanged()) {
        return PlaybackCommandResult::NotReady;
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

PlaybackCommandResult QtMultimediaPlaybackBackend::setPlaybackRate(const double rate) {
    if (shutdown_) {
        return PlaybackCommandResult::Shutdown;
    }
    if (!std::isfinite(rate) || rate <= 0.0) {
        return PlaybackCommandResult::InvalidArgument;
    }
    if (player_ == nullptr || source_volume_removal_pending_) {
        return PlaybackCommandResult::NotReady;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    // S1: with pitch compensation (Qt's default) every change away from 1.0 is followed by about 115 ms
    // of silence and the sound lags 104 to 116 ms while the rate differs from 1.0. Without it the pitch
    // moves with the speed (1% = about 17 cents) and there is no gap.
    if (rate != 1.0) {
        switch (player_->pitchCompensationAvailability()) {
        case QMediaPlayer::PitchCompensationAvailability::AlwaysOn:
            return PlaybackCommandResult::Unsupported;
        case QMediaPlayer::PitchCompensationAvailability::Available:
            if (player_->pitchCompensation()) {
                player_->setPitchCompensation(false);
            }
            if (player_->pitchCompensation()) {
                return PlaybackCommandResult::Unsupported;
            }
            break;
        case QMediaPlayer::PitchCompensationAvailability::Unavailable:
            break;
        }
    }
    player_->setPlaybackRate(rate);
    return PlaybackCommandResult::Accepted;
#else
    return rate == 1.0 ? PlaybackCommandResult::Accepted : PlaybackCommandResult::Unsupported;
#endif
}

std::optional<std::int64_t> QtMultimediaPlaybackBackend::positionUpdatedAtMs() const noexcept {
    return position_updated_at_ms_;
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

    shutdown_ = true;
    audio_devices_->setChangeHandler({});
    audio_outputs_changed_ = {};
    invalidateCurrentSource();
    advanceGeneration();
    ended_emitted_ = false;
    repositioning_from_end_ = false;
    publishSnapshot({PlaybackStatus::Empty, 0, 0, false, generation_});
    return PlaybackCommandResult::Accepted;
}

QtAudioOutputDevice QtMultimediaPlaybackBackend::configuredOutputDevice() const {
    if (selected_output_id_.isEmpty()) {
        return audio_devices_->defaultDevice();
    }
    const auto devices = audio_devices_->devices();
    const auto found = std::find_if(devices.cbegin(), devices.cend(),
                                    [this](const auto& device) { return device.id == selected_output_id_; });
    return found == devices.cend() ? QtAudioOutputDevice{} : *found;
}

void QtMultimediaPlaybackBackend::reconcileAudioOutputs() {
    if (shutdown_) {
        return;
    }
    const QPointer<QtMultimediaPlaybackBackend> guard{this};
    // Publish the route's transport outcome before observers update the choices.
    interruptIfOutputChanged();
    if (guard && !shutdown_) {
        const auto callback = audio_outputs_changed_;
        if (callback) {
            callback();
        }
    }
}

bool QtMultimediaPlaybackBackend::interruptIfOutputChanged() {
    if (shutdown_ || source_volume_removal_pending_ || player_ == nullptr ||
        snapshot_.status == PlaybackStatus::Empty || snapshot_.status == PlaybackStatus::Error) {
        return false;
    }
    const auto current_device = configuredOutputDevice();
    if (current_device.id == active_output_id_) {
        return false;
    }

    PlaybackSnapshot failed = snapshot_;
    failed.status = PlaybackStatus::Error;
    failed.seekable = false;
    failed.error =
        current_device.id.isEmpty() ? PlaybackErrorCode::AudioOutputUnavailable : PlaybackErrorCode::AudioOutputChanged;
    invalidateCurrentSource();
    publishSnapshot(failed);
    return true;
}

void QtMultimediaPlaybackBackend::invalidateCurrentSource() {
    source_drive_mask_ = 0;
    source_volume_removal_pending_ = false;
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
    position_updated_at_ms_.reset();
    ++generation_;
    if (generation_ == 0) {
        ++generation_;
    }
}

void QtMultimediaPlaybackBackend::refreshSnapshot(const QMediaPlayer* const signal_player,
                                                  const std::uint64_t signal_generation) {
    if (shutdown_ || source_volume_removal_pending_ || signal_player == nullptr || signal_player != player_ ||
        signal_generation != generation_) {
        return;
    }
    if (interruptIfOutputChanged()) {
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
        const QPointer<QtMultimediaPlaybackBackend> guard{this};
        publishSnapshot({PlaybackStatus::Paused, duration, duration, seekable, generation_});
        if (guard && !shutdown_ && !source_volume_removal_pending_ && generation_ == signal_generation &&
            player_ == ended_player && snapshot_.generation == signal_generation &&
            snapshot_.status == PlaybackStatus::Paused && snapshot_.position_ms == snapshot_.duration_ms) {
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
    if (shutdown_ || source_volume_removal_pending_ || player_ == nullptr ||
        player_->error() != QMediaPlayer::NoError || player_->hasAudio()) {
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
