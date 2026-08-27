#include "presentation/PlayerPresenter.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace music_player::m0 {
namespace {

[[noreturn]] void invalidMapping(const char* field) {
    throw std::invalid_argument{std::string{"unknown fixture mapping: "} + field};
}

ThemeMode themeMode(const QString& value) {
    if (value == QStringLiteral("system"))
        return ThemeMode::System;
    if (value == QStringLiteral("light"))
        return ThemeMode::Light;
    if (value == QStringLiteral("dark"))
        return ThemeMode::Dark;
    invalidMapping("theme");
}

MotionMode motionMode(const QString& value) {
    if (value == QStringLiteral("system"))
        return MotionMode::System;
    if (value == QStringLiteral("reduced"))
        return MotionMode::Reduced;
    invalidMapping("motion");
}

CloseBehavior closeBehavior(const QString& value) {
    if (value == QStringLiteral("minimize_to_tray"))
        return CloseBehavior::MinimizeToTray;
    if (value == QStringLiteral("exit"))
        return CloseBehavior::Exit;
    invalidMapping("close_behavior");
}

QueueEndBehavior queueEndBehavior(const QString& value) {
    if (value == QStringLiteral("stop_keep_selection"))
        return QueueEndBehavior::StopKeepSelection;
    if (value == QStringLiteral("stop_clear_selection"))
        return QueueEndBehavior::StopClearSelection;
    invalidMapping("queue_end_behavior");
}

PlayerPresenter::PlaybackState mapPlaybackState(const QString& value) {
    if (value == QStringLiteral("unloaded"))
        return PlayerPresenter::PlaybackState::Unloaded;
    if (value == QStringLiteral("loading"))
        return PlayerPresenter::PlaybackState::Loading;
    if (value == QStringLiteral("ready"))
        return PlayerPresenter::PlaybackState::Ready;
    if (value == QStringLiteral("paused"))
        return PlayerPresenter::PlaybackState::Paused;
    if (value == QStringLiteral("playing"))
        return PlayerPresenter::PlaybackState::Playing;
    if (value == QStringLiteral("error"))
        return PlayerPresenter::PlaybackState::Error;
    invalidMapping("playback_state");
}

PlayerPresenter::PlaybackState mapPlaybackState(const PlaybackStatus status) {
    switch (status) {
    case PlaybackStatus::Empty:
        return PlayerPresenter::PlaybackState::Unloaded;
    case PlaybackStatus::Loading:
        return PlayerPresenter::PlaybackState::Loading;
    case PlaybackStatus::Ready:
        return PlayerPresenter::PlaybackState::Ready;
    case PlaybackStatus::Paused:
        return PlayerPresenter::PlaybackState::Paused;
    case PlaybackStatus::Playing:
        return PlayerPresenter::PlaybackState::Playing;
    case PlaybackStatus::Error:
        return PlayerPresenter::PlaybackState::Error;
    }
    invalidMapping("domain playback status");
}

PlaybackStatus mapDomainPlaybackStatus(const QString& value) {
    if (value == QStringLiteral("unloaded"))
        return PlaybackStatus::Empty;
    if (value == QStringLiteral("loading"))
        return PlaybackStatus::Loading;
    if (value == QStringLiteral("ready"))
        return PlaybackStatus::Ready;
    if (value == QStringLiteral("paused"))
        return PlaybackStatus::Paused;
    if (value == QStringLiteral("playing"))
        return PlaybackStatus::Playing;
    if (value == QStringLiteral("error"))
        return PlaybackStatus::Error;
    invalidMapping("playback_state");
}

DeterministicPlaybackConfig backendConfig(const FixtureBundle& bundle) {
    const FixtureState* playing = nullptr;
    for (const FixtureState& state : bundle.states) {
        if (state.state_id == QStringLiteral("playing")) {
            playing = &state;
            break;
        }
    }
    if (playing == nullptr || !playing->position_ms.has_value() || !playing->duration_ms.has_value()) {
        invalidMapping("playing state snapshot");
    }
    return {
        .initial_snapshot = {mapDomainPlaybackStatus(playing->playback_state), *playing->position_ms,
                             *playing->duration_ms, playing->seekable},
        .tick_interval_ms = bundle.fake_backend.snapshots.interval_ms,
        .initial_volume = static_cast<double>(bundle.preferences.defaults.volume_percent) / 100.0,
        .initially_muted = bundle.preferences.defaults.muted,
    };
}

const FixtureState& stateById(const FixtureBundle& bundle, const QString& id) {
    for (const FixtureState& state : bundle.states) {
        if (state.state_id == id)
            return state;
    }
    invalidMapping("state_id");
}

} // namespace

PlayerPresenter::PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform, QObject* parent)
    : QObject(parent), bundle_(std::move(bundle)), backend_(backendConfig(bundle_)), controller_(backend_, platform),
      queue_model_(this), timer_(this), state_(stateById(bundle_, bundle_.default_state_id)) {
    const FixtureDefaults& defaults = bundle_.preferences.defaults;
    const ThemeMode theme = themeMode(defaults.theme);
    const MotionMode motion = motionMode(defaults.motion);
    const CloseBehavior close = closeBehavior(defaults.close_behavior);
    const QueueEndBehavior queue_end = queueEndBehavior(defaults.queue_end_behavior);
    for (const FixtureState& fixture_state : bundle_.states) {
        static_cast<void>(mapPlaybackState(fixture_state.playback_state));
    }
    static_cast<void>(stateById(bundle_, QStringLiteral("playing")));
    if (defaults.volume_percent < 0 || defaults.volume_percent > 100 || defaults.skip_backward_seconds < 1 ||
        defaults.skip_backward_seconds > 300 || defaults.skip_forward_seconds < 1 ||
        defaults.skip_forward_seconds > 300 || (defaults.start_minimized && !platform.trayAvailable())) {
        invalidMapping("preference default");
    }
    controller_.setTheme(theme);
    controller_.setMotionMode(motion);
    if (!controller_.setVolume(static_cast<double>(defaults.volume_percent) / 100.0) ||
        !controller_.setSkipBackwardSeconds(defaults.skip_backward_seconds) ||
        !controller_.setSkipForwardSeconds(defaults.skip_forward_seconds)) {
        invalidMapping("numeric preference default");
    }
    if (controller_.preferences().muted != defaults.muted)
        controller_.toggleMute();
    controller_.setCloseBehavior(close);
    controller_.setRestoreLastSession(defaults.restore_last_session);
    if (!controller_.setStartMinimized(defaults.start_minimized))
        invalidMapping("start_minimized");
    controller_.setQueueEndBehavior(queue_end);
    timer_.setInterval(static_cast<int>(bundle_.fake_backend.snapshots.interval_ms));
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, [this] { static_cast<void>(advanceOneTick()); });
}

QString PlayerPresenter::stateId() const { return state_.state_id; }
PlayerPresenter::PlaybackState PlayerPresenter::playbackState() const {
    return mapPlaybackState(controller_.confirmedSnapshot().status);
}
bool PlayerPresenter::queueLoaded() const noexcept { return state_.queue_loaded; }
bool PlayerPresenter::hasCurrentOccurrence() const noexcept { return state_.current_occurrence_id.has_value(); }
QString PlayerPresenter::currentOccurrenceId() const { return state_.current_occurrence_id.value_or(QString{}); }
bool PlayerPresenter::hasContextOccurrence() const noexcept { return state_.context_occurrence_id.has_value(); }
QString PlayerPresenter::contextOccurrenceId() const { return state_.context_occurrence_id.value_or(QString{}); }
bool PlayerPresenter::hasPosition() const noexcept { return state_.position_ms.has_value(); }
qint64 PlayerPresenter::positionMs() const noexcept { return controller_.displayedPositionMs(); }
qint64 PlayerPresenter::confirmedPositionMs() const noexcept { return controller_.confirmedSnapshot().position_ms; }
bool PlayerPresenter::seekPreviewActive() const noexcept { return controller_.seekPreviewActive(); }
bool PlayerPresenter::seekPending() const noexcept { return controller_.seekPending(); }
bool PlayerPresenter::hasDuration() const noexcept { return state_.duration_ms.has_value(); }
qint64 PlayerPresenter::durationMs() const noexcept {
    return hasDuration() ? controller_.confirmedSnapshot().duration_ms : 0;
}
bool PlayerPresenter::seekable() const noexcept { return queueLoaded() && controller_.confirmedSnapshot().seekable; }
QString PlayerPresenter::assetId() const { return state_.asset_id; }
bool PlayerPresenter::hasMessage() const noexcept { return state_.message_key.has_value(); }
QString PlayerPresenter::messageKey() const { return state_.message_key.value_or(QString{}); }
QString PlayerPresenter::primaryActionId() const { return state_.primary_action_id; }
bool PlayerPresenter::hasError() const noexcept { return state_.error_code.has_value(); }
QString PlayerPresenter::errorCode() const { return state_.error_code.value_or(QString{}); }
const QueueItem* PlayerPresenter::currentTrack() const noexcept {
    if (!state_.current_occurrence_id.has_value())
        return nullptr;
    for (const QueueItem& item : bundle_.queue) {
        if (item.occurrence_id == *state_.current_occurrence_id)
            return &item;
    }
    return nullptr;
}
const QueueItem* PlayerPresenter::contextTrack() const noexcept {
    if (!state_.context_occurrence_id.has_value())
        return nullptr;
    for (const QueueItem& item : bundle_.queue) {
        if (item.occurrence_id == *state_.context_occurrence_id)
            return &item;
    }
    return nullptr;
}
bool PlayerPresenter::hasCurrentTrack() const noexcept { return currentTrack() != nullptr; }
QString PlayerPresenter::currentTitle() const {
    const QueueItem* const track = currentTrack();
    return track != nullptr ? track->title : QString{};
}
QString PlayerPresenter::currentArtist() const {
    const QueueItem* const track = currentTrack();
    return track != nullptr ? track->artist : QString{};
}
QString PlayerPresenter::currentAlbum() const {
    const QueueItem* const track = currentTrack();
    return track != nullptr ? track->album : QString{};
}
bool PlayerPresenter::hasContextTrack() const noexcept { return contextTrack() != nullptr; }
QString PlayerPresenter::contextTitle() const {
    const QueueItem* const track = contextTrack();
    return track != nullptr ? track->title : QString{};
}
QString PlayerPresenter::contextArtist() const {
    const QueueItem* const track = contextTrack();
    return track != nullptr ? track->artist : QString{};
}
QString PlayerPresenter::contextAlbum() const {
    const QueueItem* const track = contextTrack();
    return track != nullptr ? track->album : QString{};
}
QueueListModel* PlayerPresenter::queueModel() noexcept { return &queue_model_; }
QAbstractItemModel* PlayerPresenter::queueModelObject() noexcept { return &queue_model_; }
const Preferences& PlayerPresenter::preferences() const noexcept { return controller_.preferences(); }
double PlayerPresenter::volume() const noexcept { return controller_.preferences().volume; }
bool PlayerPresenter::muted() const noexcept { return controller_.preferences().muted; }
int PlayerPresenter::skipBackwardSeconds() const noexcept { return controller_.preferences().skip_backward_seconds; }
int PlayerPresenter::skipForwardSeconds() const noexcept { return controller_.preferences().skip_forward_seconds; }
bool PlayerPresenter::togglePlayback() {
    const PlaybackStatus before = controller_.confirmedSnapshot().status;
    if (!controller_.togglePlayback())
        return false;
    controller_.applyBackendSnapshot(backend_.snapshot());
    if (before == controller_.confirmedSnapshot().status)
        return false;
    reconcileTimer();
    emit playbackChanged();
    return true;
}
bool PlayerPresenter::skipBackward() {
    if (!controller_.skipBackward())
        return false;
    reconcileTimer();
    emit playbackChanged();
    return true;
}
bool PlayerPresenter::skipForward() {
    if (!controller_.skipForward())
        return false;
    reconcileTimer();
    emit playbackChanged();
    return true;
}
bool PlayerPresenter::beginSeekPreview(const qint64 position_ms) {
    const PlaybackStatus status = controller_.confirmedSnapshot().status;
    const bool transport_active =
        status == PlaybackStatus::Ready || status == PlaybackStatus::Paused || status == PlaybackStatus::Playing;
    if (!queueLoaded() || !transport_active || !seekable() || controller_.seekPreviewActive() ||
        controller_.seekPending())
        return false;
    const auto before = controller_.displayedPositionMs();
    const bool was_active = controller_.seekPreviewActive();
    controller_.beginSeekPreview(position_ms);
    if (!controller_.seekPreviewActive())
        return false;
    if (!was_active || before != controller_.displayedPositionMs())
        emit playbackChanged();
    return true;
}
bool PlayerPresenter::updateSeekPreview(const qint64 position_ms) {
    if (!controller_.seekPreviewActive() || controller_.seekPending())
        return false;
    const auto before = controller_.displayedPositionMs();
    controller_.updateSeekPreview(position_ms);
    if (before != controller_.displayedPositionMs())
        emit playbackChanged();
    return true;
}
bool PlayerPresenter::commitSeekPreview() {
    if (!controller_.seekPreviewActive() || controller_.seekPending())
        return false;
    controller_.commitSeekPreview();
    if (!controller_.seekPending())
        return false;
    reconcileTimer();
    emit playbackChanged();
    return true;
}
bool PlayerPresenter::setVolume(const double volume) {
    if (volume == controller_.preferences().volume || !controller_.setVolume(volume))
        return false;
    emit preferencesChanged();
    return true;
}
bool PlayerPresenter::toggleMute() {
    controller_.toggleMute();
    emit preferencesChanged();
    return true;
}
bool PlayerPresenter::enterFrozenUnavailableScenarioForTest() {
    if (!queueLoaded() || state_.state_id != QStringLiteral("playing") || !backend_.enterUnavailableScenario())
        return false;
    controller_.applyBackendSnapshot(backend_.snapshot());
    state_ = stateById(bundle_, QStringLiteral("error"));
    reconcileTimer();
    emit stateChanged();
    emit playbackChanged();
    return true;
}
bool PlayerPresenter::skipUnavailable() {
    if (state_.state_id != QStringLiteral("error") || !backend_.restoreInitialScenario())
        return false;
    controller_.applyBackendSnapshot(backend_.snapshot());
    state_ = stateById(bundle_, QStringLiteral("playing"));
    state_.position_ms = backend_.snapshot().position_ms;
    state_.duration_ms = backend_.snapshot().duration_ms;
    state_.seekable = backend_.snapshot().seekable;
    reconcileTimer();
    emit stateChanged();
    emit playbackChanged();
    return true;
}
bool PlayerPresenter::advanceOneTick() {
    if (!queueLoaded())
        return false;
    const auto before = controller_.confirmedSnapshot();
    const auto before_displayed = controller_.displayedPositionMs();
    const bool before_preview = controller_.seekPreviewActive();
    const bool before_pending = controller_.seekPending();
    const DeterministicTickResult result = backend_.tick();
    controller_.applyBackendSnapshot(result.snapshot);
    if (result.seek_rejected)
        controller_.rejectPendingSeek();
    const auto& after = controller_.confirmedSnapshot();
    const bool changed = result.seek_rejected || before.status != after.status ||
                         before.position_ms != after.position_ms || before.duration_ms != after.duration_ms ||
                         before.seekable != after.seekable || before_displayed != controller_.displayedPositionMs() ||
                         before_preview != controller_.seekPreviewActive() ||
                         before_pending != controller_.seekPending();
    reconcileTimer();
    if (changed)
        emit playbackChanged();
    if (result.seek_rejected)
        emit seekRejected();
    return changed;
}
void PlayerPresenter::rejectNextSeekForTest() noexcept { backend_.rejectNextSeek(); }
void PlayerPresenter::reconcileTimer() {
    const bool needed = queueLoaded() && (controller_.confirmedSnapshot().status == PlaybackStatus::Playing ||
                                          controller_.seekPending());
    if (needed && !timer_.isActive())
        timer_.start();
    else if (!needed && timer_.isActive())
        timer_.stop();
}
bool PlayerPresenter::loadFrozenQueue() {
    if (queue_model_.loaded() || bundle_.queue.isEmpty())
        return false;
    controller_.applyBackendSnapshot(backend_.snapshot());
    state_ = stateById(bundle_, QStringLiteral("playing"));
    state_.position_ms = backend_.snapshot().position_ms;
    state_.duration_ms = backend_.snapshot().duration_ms;
    state_.seekable = backend_.snapshot().seekable;
    if (!queue_model_.loadOnce(bundle_.queue))
        return false;
    reconcileTimer();
    emit stateChanged();
    emit playbackChanged();
    return true;
}

} // namespace music_player::m0
