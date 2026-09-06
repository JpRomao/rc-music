#include "presentation/SystemMediaControlsBinding.hpp"

#include "presentation/PlayerPresenter.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace music_player::m0 {
namespace {

[[nodiscard]] SystemMediaSnapshot snapshotFor(const PlayerPresenter& presenter) {
    SystemMediaSnapshot snapshot;
    if (presenter.exiting())
        return snapshot;

    const auto playback_state = presenter.playbackState();
    switch (playback_state) {
    case PlayerPresenter::PlaybackState::Unloaded:
    case PlayerPresenter::PlaybackState::Error:
        return snapshot;
    case PlayerPresenter::PlaybackState::Loading:
        snapshot.status = SystemMediaPlaybackStatus::Changing;
        break;
    case PlayerPresenter::PlaybackState::Ready:
        snapshot.status = SystemMediaPlaybackStatus::Stopped;
        snapshot.play_enabled = true;
        break;
    case PlayerPresenter::PlaybackState::Paused:
        snapshot.status = SystemMediaPlaybackStatus::Paused;
        snapshot.play_enabled = true;
        break;
    case PlayerPresenter::PlaybackState::Playing:
        snapshot.status = SystemMediaPlaybackStatus::Playing;
        snapshot.pause_enabled = true;
        break;
    }

    if (!presenter.hasCurrentTrack())
        return snapshot;

    snapshot.title = presenter.currentTitle().toStdU16String();
    snapshot.artist = presenter.currentArtist().toStdU16String();
    snapshot.album = presenter.currentAlbum().toStdU16String();

    const bool transport_active = playback_state == PlayerPresenter::PlaybackState::Ready ||
                                  playback_state == PlayerPresenter::PlaybackState::Paused ||
                                  playback_state == PlayerPresenter::PlaybackState::Playing;
    if (!transport_active)
        return snapshot;

    snapshot.previous_enabled = presenter.canGoPrevious();
    snapshot.next_enabled = presenter.canGoNext();
    snapshot.duration_ms = std::max<std::int64_t>(0, presenter.durationMs());
    snapshot.position_ms = std::clamp<std::int64_t>(presenter.confirmedPositionMs(), 0, snapshot.duration_ms);
    snapshot.seek_enabled =
        presenter.seekable() && snapshot.duration_ms > 0 && !presenter.seekPreviewActive() && !presenter.seekPending();
    return snapshot;
}

} // namespace

SystemMediaControlsBinding::SystemMediaControlsBinding(PlayerPresenter& presenter, SystemMediaControls& controls,
                                                       QObject* const parent)
    : QObject(parent), presenter_(presenter), controls_(controls) {
    connect(&presenter_, &PlayerPresenter::stateChanged, this, &SystemMediaControlsBinding::refresh);
    connect(&presenter_, &PlayerPresenter::playbackChanged, this, &SystemMediaControlsBinding::refresh);
    connect(&presenter_, &PlayerPresenter::lifecycleChanged, this, &SystemMediaControlsBinding::refresh);
    controls_.setCommandHandler([this](const SystemMediaCommand command) { dispatch(command); });
    refresh();
}

SystemMediaControlsBinding::~SystemMediaControlsBinding() {
    controls_.setCommandHandler({});
    controls_.shutdown();
}

void SystemMediaControlsBinding::refresh() {
    const SystemMediaSnapshot snapshot = snapshotFor(presenter_);
    if (published_snapshot_ == snapshot)
        return;
    if (controls_.publish(snapshot))
        published_snapshot_ = snapshot;
}

void SystemMediaControlsBinding::dispatch(const SystemMediaCommand command) {
    switch (command.kind) {
    case SystemMediaCommandKind::Play:
        static_cast<void>(presenter_.play());
        break;
    case SystemMediaCommandKind::Pause:
        static_cast<void>(presenter_.pause());
        break;
    case SystemMediaCommandKind::Previous:
        static_cast<void>(presenter_.previousTrack());
        break;
    case SystemMediaCommandKind::Next:
        static_cast<void>(presenter_.nextTrack());
        break;
    case SystemMediaCommandKind::Seek:
        if (presenter_.beginSeekPreview(command.position_ms))
            static_cast<void>(presenter_.commitSeekPreview());
        break;
    }
}

} // namespace music_player::m0
