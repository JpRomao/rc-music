#include "presentation/PlayerPresenter.hpp"

#include "presentation/ArtworkImageProvider.hpp"

#include <QMetaObject>

#include <algorithm>
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

std::vector<QueueNavigationItem> navigationItems(const FixtureBundle& bundle) {
    std::vector<QueueNavigationItem> entries;
    entries.reserve(static_cast<std::size_t>(bundle.queue.size()));
    for (const QueueItem& item : bundle.queue)
        entries.push_back({item.available});
    return entries;
}

std::vector<QueueNavigationItem> navigationItems(const LocalFileQueue& queue) {
    std::vector<QueueNavigationItem> entries;
    entries.reserve(queue.size());
    for (const LocalFileQueueItem& item : queue.items())
        entries.push_back({item.available});
    return entries;
}

QString fromUtf8(const std::string& value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QString localPlaybackErrorCode(const PlaybackErrorCode error) {
    switch (error) {
    case PlaybackErrorCode::SourceUnavailable:
        return QStringLiteral("source_unavailable");
    case PlaybackErrorCode::InvalidMedia:
        return QStringLiteral("invalid_media");
    case PlaybackErrorCode::AccessDenied:
        return QStringLiteral("access_denied");
    case PlaybackErrorCode::None:
    case PlaybackErrorCode::NetworkFailure:
    case PlaybackErrorCode::BackendFailure:
        return {};
    }
    return {};
}

} // namespace

PlayerPresenter::PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform, QObject* parent)
    : QObject(parent), bundle_(std::move(bundle)), navigation_items_(navigationItems(bundle_)), platform_(platform),
      tray_available_(platform.trayAvailable()),
      owned_backend_(std::make_unique<DeterministicPlaybackBackend>(backendConfig(bundle_))), backend_(*owned_backend_),
      controller_(backend_, platform), queue_model_(this), local_queue_model_(this), timer_(this),
      state_(stateById(bundle_, bundle_.default_state_id)), artwork_cache_(std::make_shared<ArtworkImageCache>()) {
    initializeDefaults();
    timer_.setInterval(static_cast<int>(bundle_.fake_backend.snapshots.interval_ms));
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, [this] { static_cast<void>(advanceOneTick()); });
}

PlayerPresenter::PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform, PlaybackBackend& playback_backend,
                                 MetadataReader& metadata_reader, AudioFilePicker audio_file_picker, QObject* parent)
    : PlayerPresenter(std::move(bundle), platform, playback_backend, metadata_reader, std::move(audio_file_picker),
                      std::make_shared<ArtworkImageCache>(), parent) {}

PlayerPresenter::PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform, PlaybackBackend& playback_backend,
                                 MetadataReader& metadata_reader, AudioFilePicker audio_file_picker,
                                 std::shared_ptr<ArtworkImageCache> artwork_cache, QObject* parent)
    : QObject(parent), bundle_(std::move(bundle)), navigation_items_(navigationItems(bundle_)), platform_(platform),
      tray_available_(platform.trayAvailable()), owned_backend_(), backend_(playback_backend),
      controller_(backend_, platform, backend_.snapshot()), queue_model_(this), local_queue_model_(this), timer_(this),
      state_(stateById(bundle_, bundle_.default_state_id)), audio_file_picker_(std::move(audio_file_picker)),
      local_track_session_(std::make_unique<LocalTrackSession>(backend_, metadata_reader)),
      artwork_cache_(std::move(artwork_cache)), uses_frozen_fixture_(false) {
    if (!audio_file_picker_)
        throw std::invalid_argument{"audio file picker is required"};
    if (artwork_cache_ == nullptr)
        throw std::invalid_argument{"artwork image cache is required"};
    initializeDefaults();
    timer_.setInterval(25);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, [this] { pollMetadata(); });
    backend_.setEventCallback([this](const PlaybackEvent& event) {
        const PlaybackEvent copy = event;
        QMetaObject::invokeMethod(this, [this, copy] { handlePlaybackEvent(copy); }, Qt::QueuedConnection);
    });
}

PlayerPresenter::~PlayerPresenter() {
    timer_.stop();
    if (!uses_frozen_fixture_) {
        backend_.setEventCallback({});
        if (local_track_session_ != nullptr)
            static_cast<void>(local_track_session_->cancelMetadata());
    }
}

void PlayerPresenter::initializeDefaults() {
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
        defaults.skip_forward_seconds > 300 || (defaults.start_minimized && !platform_.trayAvailable())) {
        invalidMapping("preference default");
    }
    controller_.setTheme(theme);
    controller_.setMotionMode(motion);
    if (!controller_.setVolume(static_cast<double>(defaults.volume_percent) / 100.0) ||
        !controller_.setSkipBackwardSeconds(defaults.skip_backward_seconds) ||
        !controller_.setSkipForwardSeconds(defaults.skip_forward_seconds)) {
        invalidMapping("numeric preference default");
    }
    if (controller_.preferences().muted != defaults.muted && !controller_.toggleMute())
        invalidMapping("muted preference default");
    controller_.setCloseBehavior(close);
    controller_.setRestoreLastSession(defaults.restore_last_session);
    if (!controller_.setStartMinimized(defaults.start_minimized))
        invalidMapping("start_minimized");
    controller_.setQueueEndBehavior(queue_end);
}

QString PlayerPresenter::stateId() const {
    if (uses_frozen_fixture_)
        return state_.state_id;
    if (controller_.confirmedSnapshot().status == PlaybackStatus::Error)
        return QStringLiteral("error");
    return local_track_session_->hasTrack() ? QStringLiteral("playing") : QStringLiteral("empty");
}
PlayerPresenter::PlaybackState PlayerPresenter::playbackState() const {
    return mapPlaybackState(controller_.confirmedSnapshot().status);
}
bool PlayerPresenter::queueLoaded() const noexcept {
    return uses_frozen_fixture_ ? state_.queue_loaded : !local_queue_.empty();
}
bool PlayerPresenter::hasCurrentOccurrence() const noexcept {
    return uses_frozen_fixture_ ? state_.current_occurrence_id.has_value()
                                : local_queue_.currentOccurrenceId().has_value();
}
QString PlayerPresenter::currentOccurrenceId() const {
    if (uses_frozen_fixture_)
        return state_.current_occurrence_id.value_or(QString{});
    const auto current = local_queue_.currentOccurrenceId();
    return current.has_value() ? LocalQueueListModel::occurrenceId(*current) : QString{};
}
int PlayerPresenter::currentQueueIndex() const noexcept {
    if (!uses_frozen_fixture_) {
        const auto current = local_queue_.currentIndex();
        return current.has_value() ? static_cast<int>(*current) : -1;
    }
    if (state_.current_occurrence_id.has_value()) {
        for (qsizetype index = 0; index < bundle_.queue.size(); ++index) {
            if (bundle_.queue.at(index).occurrence_id == *state_.current_occurrence_id)
                return static_cast<int>(index);
        }
    }
    return -1;
}
bool PlayerPresenter::hasContextOccurrence() const noexcept {
    return uses_frozen_fixture_ && state_.context_occurrence_id.has_value();
}
QString PlayerPresenter::contextOccurrenceId() const {
    return uses_frozen_fixture_ ? state_.context_occurrence_id.value_or(QString{}) : QString{};
}
bool PlayerPresenter::hasPosition() const noexcept {
    return uses_frozen_fixture_ ? state_.position_ms.has_value() : local_track_session_->hasTrack();
}
qint64 PlayerPresenter::positionMs() const noexcept { return controller_.displayedPositionMs(); }
qint64 PlayerPresenter::confirmedPositionMs() const noexcept { return controller_.confirmedSnapshot().position_ms; }
bool PlayerPresenter::seekPreviewActive() const noexcept { return controller_.seekPreviewActive(); }
bool PlayerPresenter::seekPending() const noexcept { return controller_.seekPending(); }
bool PlayerPresenter::hasDuration() const noexcept {
    return uses_frozen_fixture_ ? state_.duration_ms.has_value() : localDurationMs().has_value();
}
qint64 PlayerPresenter::durationMs() const noexcept {
    if (uses_frozen_fixture_)
        return hasDuration() ? controller_.confirmedSnapshot().duration_ms : 0;
    return localDurationMs().value_or(0);
}
bool PlayerPresenter::seekable() const noexcept {
    return (uses_frozen_fixture_ ? queueLoaded() : local_track_session_->hasTrack()) &&
           controller_.confirmedSnapshot().seekable;
}
std::optional<std::size_t> PlayerPresenter::navigationTarget(const QueueDirection direction) const noexcept {
    const int current = currentQueueIndex();
    if (!queueLoaded() || current < 0)
        return std::nullopt;
    const auto current_index = static_cast<std::size_t>(current);
    if (!uses_frozen_fixture_ && !navigation_items_[current_index].available &&
        controller_.confirmedSnapshot().status == PlaybackStatus::Error && !controller_.seekPreviewActive() &&
        !controller_.seekPending()) {
        if (direction == QueueDirection::Next) {
            return nextAvailableLocalIndex();
        }
        for (std::size_t index = current_index; index > 0;) {
            --index;
            if (navigation_items_[index].available)
                return index;
        }
        return std::nullopt;
    }
    return queueNavigationTarget(navigation_items_, static_cast<std::size_t>(current), direction,
                                 controller_.confirmedSnapshot(),
                                 controller_.seekPreviewActive() || controller_.seekPending());
}
bool PlayerPresenter::canGoPrevious() const noexcept { return navigationTarget(QueueDirection::Previous).has_value(); }
bool PlayerPresenter::canGoNext() const noexcept { return navigationTarget(QueueDirection::Next).has_value(); }
QString PlayerPresenter::assetId() const {
    if (uses_frozen_fixture_)
        return state_.asset_id;
    return stateById(bundle_, local_track_session_->hasTrack() ? QStringLiteral("playing") : QStringLiteral("empty"))
        .asset_id;
}
bool PlayerPresenter::hasMessage() const noexcept {
    return uses_frozen_fixture_ ? state_.message_key.has_value() : !local_track_session_->hasTrack();
}
QString PlayerPresenter::messageKey() const {
    return uses_frozen_fixture_
               ? state_.message_key.value_or(QString{})
               : (local_track_session_->hasTrack() ? QString{} : state_.message_key.value_or(QString{}));
}
QString PlayerPresenter::primaryActionId() const {
    if (uses_frozen_fixture_)
        return state_.primary_action_id;
    if (hasError() || !local_track_session_->hasTrack())
        return QStringLiteral("open-audio");
    return QStringLiteral("play-pause");
}
bool PlayerPresenter::hasError() const noexcept {
    return uses_frozen_fixture_ ? state_.error_code.has_value()
                                : controller_.confirmedSnapshot().status == PlaybackStatus::Error;
}
QString PlayerPresenter::errorCode() const {
    if (uses_frozen_fixture_)
        return state_.error_code.value_or(QString{});
    return hasError() ? QStringLiteral("playback_error") : QString{};
}
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
bool PlayerPresenter::hasCurrentTrack() const noexcept {
    return uses_frozen_fixture_ ? currentTrack() != nullptr : local_track_session_->hasTrack();
}
QString PlayerPresenter::currentTitle() const {
    if (!uses_frozen_fixture_)
        return fromUtf8(local_track_session_->displayTitle());
    const QueueItem* const track = currentTrack();
    return track != nullptr ? track->title : QString{};
}
QString PlayerPresenter::currentArtist() const {
    if (!uses_frozen_fixture_)
        return fromUtf8(local_track_session_->artist());
    const QueueItem* const track = currentTrack();
    return track != nullptr ? track->artist : QString{};
}
QString PlayerPresenter::currentAlbum() const {
    if (!uses_frozen_fixture_)
        return fromUtf8(local_track_session_->album());
    const QueueItem* const track = currentTrack();
    return track != nullptr ? track->album : QString{};
}
QUrl PlayerPresenter::currentArtworkSource() const { return artwork_cache_->currentSource(); }
bool PlayerPresenter::hasContextTrack() const noexcept {
    return uses_frozen_fixture_ ? contextTrack() != nullptr : hasError() && local_track_session_->hasTrack();
}
QString PlayerPresenter::contextTitle() const {
    if (!uses_frozen_fixture_)
        return hasContextTrack() ? currentTitle() : QString{};
    const QueueItem* const track = contextTrack();
    return track != nullptr ? track->title : QString{};
}
QString PlayerPresenter::contextArtist() const {
    if (!uses_frozen_fixture_)
        return hasContextTrack() ? currentArtist() : QString{};
    const QueueItem* const track = contextTrack();
    return track != nullptr ? track->artist : QString{};
}
QString PlayerPresenter::contextAlbum() const {
    if (!uses_frozen_fixture_)
        return hasContextTrack() ? currentAlbum() : QString{};
    const QueueItem* const track = contextTrack();
    return track != nullptr ? track->album : QString{};
}
QueueListModel* PlayerPresenter::queueModel() noexcept { return &queue_model_; }
QAbstractItemModel* PlayerPresenter::queueModelObject() noexcept {
    return uses_frozen_fixture_ ? static_cast<QAbstractItemModel*>(&queue_model_)
                                : static_cast<QAbstractItemModel*>(&local_queue_model_);
}
const Preferences& PlayerPresenter::preferences() const noexcept { return controller_.preferences(); }
double PlayerPresenter::volume() const noexcept { return controller_.preferences().volume; }
bool PlayerPresenter::muted() const noexcept { return controller_.preferences().muted; }
int PlayerPresenter::skipBackwardSeconds() const noexcept { return controller_.preferences().skip_backward_seconds; }
int PlayerPresenter::skipForwardSeconds() const noexcept { return controller_.preferences().skip_forward_seconds; }
PlayerPresenter::ThemePreference PlayerPresenter::themePreference() const {
    switch (preferences().theme) {
    case ThemeMode::System:
        return ThemePreference::SystemTheme;
    case ThemeMode::Light:
        return ThemePreference::LightTheme;
    case ThemeMode::Dark:
        return ThemePreference::DarkTheme;
    }
    invalidMapping("theme preference");
}
PlayerPresenter::MotionPreference PlayerPresenter::motionPreference() const {
    switch (preferences().motion) {
    case MotionMode::System:
        return MotionPreference::SystemMotion;
    case MotionMode::Reduced:
        return MotionPreference::ReducedMotion;
    }
    invalidMapping("motion preference");
}
PlayerPresenter::ClosePreference PlayerPresenter::closePreference() const {
    switch (preferences().close_behavior) {
    case CloseBehavior::MinimizeToTray:
        return ClosePreference::MinimizeToTray;
    case CloseBehavior::Exit:
        return ClosePreference::Exit;
    }
    invalidMapping("close preference");
}
PlayerPresenter::QueueEndPreference PlayerPresenter::queueEndPreference() const {
    switch (preferences().queue_end_behavior) {
    case QueueEndBehavior::StopKeepSelection:
        return QueueEndPreference::StopKeepSelection;
    case QueueEndBehavior::StopClearSelection:
        return QueueEndPreference::StopClearSelection;
    }
    invalidMapping("queue end preference");
}
bool PlayerPresenter::restoreLastSession() const noexcept { return preferences().restore_last_session; }
bool PlayerPresenter::startMinimized() const noexcept { return preferences().start_minimized; }
bool PlayerPresenter::trayAvailable() const noexcept { return tray_available_; }
bool PlayerPresenter::exiting() const noexcept { return exiting_; }
PlayerPresenter::WindowNotice PlayerPresenter::windowNotice() const noexcept { return window_notice_; }
bool PlayerPresenter::trayNoticeEnabled() const noexcept { return tray_notice_enabled_; }
bool PlayerPresenter::usesFrozenFixture() const noexcept { return uses_frozen_fixture_; }
bool PlayerPresenter::openAudio() {
    if (exiting_)
        return false;
    if (uses_frozen_fixture_) {
        return loadFrozenQueue();
    }

    const auto selected_paths = audio_file_picker_();
    auto candidate = local_queue_;
    const std::size_t first_inserted = candidate.size();
    const bool queue_was_empty = candidate.empty();
    if (!candidate.append(selected_paths) ||
        !local_track_session_->open(candidate.items()[first_inserted].source_path, LocalTrackStartIntent::Autoplay)) {
        return false;
    }
    if (!candidate.select(first_inserted))
        throw std::logic_error{"an accepted opened local occurrence could not be selected"};

    local_queue_ = std::move(candidate);
    local_recovery_intent_.reset();
    navigation_items_ = navigationItems(local_queue_);
    const bool model_updated = queue_was_empty
                                   ? local_queue_model_.replace(local_queue_)
                                   : local_queue_model_.append(local_queue_.items().subspan(first_inserted));
    if (!model_updated)
        throw std::logic_error{"accepted opened local queue could not be projected"};
    artwork_cache_->beginGeneration(local_track_session_->metadataGeneration());
    controller_.applyBackendSnapshot(backend_.snapshot());
    reconcileTimer();
    emit stateChanged();
    emit playbackChanged();
    return true;
}
bool PlayerPresenter::addAudio() {
    if (uses_frozen_fixture_ || exiting_)
        return false;

    const auto selected_paths = audio_file_picker_();
    auto candidate = local_queue_;
    const std::size_t first_inserted = candidate.size();
    if (!candidate.append(selected_paths))
        return false;

    const bool needs_current_track = !local_track_session_->hasTrack();
    if (needs_current_track) {
        if (!local_track_session_->open(candidate.items()[first_inserted].source_path,
                                        LocalTrackStartIntent::Autoplay)) {
            return false;
        }
        if (!candidate.select(first_inserted))
            throw std::logic_error{"an accepted appended local occurrence could not be selected"};
    }

    local_queue_ = std::move(candidate);
    navigation_items_ = navigationItems(local_queue_);
    if (!local_queue_model_.append(local_queue_.items().subspan(first_inserted)))
        throw std::logic_error{"accepted local queue append could not be projected"};
    if (needs_current_track) {
        local_recovery_intent_.reset();
        artwork_cache_->beginGeneration(local_track_session_->metadataGeneration());
        controller_.applyBackendSnapshot(backend_.snapshot());
        reconcileTimer();
    }
    emit stateChanged();
    emit playbackChanged();
    if (needs_current_track)
        emit queueFocusRequested(static_cast<int>(first_inserted));
    return true;
}
bool PlayerPresenter::playQueueOccurrence(const QString& occurrence_id) {
    if (uses_frozen_fixture_ || exiting_)
        return false;
    const auto target = localOccurrenceIndex(occurrence_id);
    if (!target.has_value() || !local_queue_.items()[*target].available)
        return false;

    const auto current = local_queue_.currentIndex();
    if (current == target) {
        switch (controller_.confirmedSnapshot().status) {
        case PlaybackStatus::Loading:
            return local_track_session_->promotePendingStartToAutoplay();
        case PlaybackStatus::Ready:
        case PlaybackStatus::Paused:
            return togglePlayback();
        case PlaybackStatus::Playing:
            return true;
        case PlaybackStatus::Empty:
        case PlaybackStatus::Error:
            break;
        }
    }

    const PlaybackSnapshot before = controller_.confirmedSnapshot();
    if (!loadLocalOccurrence(*target, LocalTrackStartIntent::Autoplay))
        return false;
    controller_.applyBackendSnapshot(backend_.snapshot());
    reconcileTimer();
    emit stateChanged();
    if (controller_.confirmedSnapshot() != before)
        emit playbackChanged();
    return true;
}
bool PlayerPresenter::moveQueueOccurrence(const QString& occurrence_id, const int destination_index) {
    if (uses_frozen_fixture_ || exiting_ || destination_index < 0)
        return false;
    const auto source_index = localOccurrenceIndex(occurrence_id);
    const auto destination = static_cast<std::size_t>(destination_index);
    if (!source_index.has_value() || destination >= local_queue_.size())
        return false;
    const auto occurrence = local_queue_.items()[*source_index].occurrence_id;
    if (!local_queue_.move(occurrence, destination))
        return false;
    if (!local_queue_model_.move(*source_index, destination))
        throw std::logic_error{"accepted local queue move could not be projected"};
    navigation_items_ = navigationItems(local_queue_);
    emit stateChanged();
    emit playbackChanged();
    emit queueFocusRequested(destination_index);
    return true;
}
bool PlayerPresenter::removeQueueOccurrence(const QString& occurrence_id) {
    if (uses_frozen_fixture_ || exiting_)
        return false;
    const auto source_index = localOccurrenceIndex(occurrence_id);
    if (!source_index.has_value())
        return false;
    const LocalQueueOccurrenceId removed_occurrence = local_queue_.items()[*source_index].occurrence_id;
    const bool removes_current = local_queue_.currentOccurrenceId() == std::optional{removed_occurrence};

    auto candidate = local_queue_;
    if (!candidate.remove(removed_occurrence))
        throw std::logic_error{"resolved local occurrence could not be removed"};

    if (removes_current) {
        std::optional<std::size_t> next_index;
        for (std::size_t index = *source_index + 1; index < local_queue_.size(); ++index) {
            if (local_queue_.items()[index].available) {
                next_index = index;
                break;
            }
        }

        if (next_index.has_value()) {
            auto start_intent = local_track_session_->startIntent();
            switch (controller_.confirmedSnapshot().status) {
            case PlaybackStatus::Playing:
                start_intent = LocalTrackStartIntent::Autoplay;
                break;
            case PlaybackStatus::Ready:
            case PlaybackStatus::Paused:
                start_intent = LocalTrackStartIntent::StayReady;
                break;
            case PlaybackStatus::Error:
                start_intent = local_recovery_intent_.value_or(start_intent);
                break;
            case PlaybackStatus::Empty:
            case PlaybackStatus::Loading:
                break;
            }
            if (!local_track_session_->open(local_queue_.items()[*next_index].source_path, start_intent))
                return false;
            const std::size_t adjusted_next = *next_index - 1;
            if (!candidate.select(adjusted_next))
                throw std::logic_error{"accepted local removal successor could not be selected"};
            local_recovery_intent_.reset();
            artwork_cache_->beginGeneration(local_track_session_->metadataGeneration());
        } else {
            if (!local_track_session_->unload())
                return false;
            local_recovery_intent_.reset();
            artwork_cache_->beginGeneration(MetadataGeneration{});
        }
    }

    local_queue_ = std::move(candidate);
    if (!local_queue_model_.remove(*source_index))
        throw std::logic_error{"accepted local queue removal could not be projected"};
    navigation_items_ = navigationItems(local_queue_);
    controller_.applyBackendSnapshot(backend_.snapshot());
    reconcileTimer();
    emit stateChanged();
    emit playbackChanged();
    const auto current = local_queue_.currentIndex();
    const int focus_index =
        removes_current && current.has_value()
            ? static_cast<int>(*current)
            : (local_queue_.empty() ? -1 : static_cast<int>(std::min(*source_index, local_queue_.size() - 1)));
    emit queueFocusRequested(focus_index);
    return true;
}
bool PlayerPresenter::clearQueue() {
    if (uses_frozen_fixture_ || exiting_ || local_queue_.empty() || !local_track_session_->unload())
        return false;
    if (!local_queue_.clear())
        throw std::logic_error{"a non-empty local queue could not be cleared"};
    if (!local_queue_model_.clear())
        throw std::logic_error{"an accepted local queue clear could not be projected"};
    navigation_items_.clear();
    local_recovery_intent_.reset();
    artwork_cache_->beginGeneration(MetadataGeneration{});
    controller_.applyBackendSnapshot(backend_.snapshot());
    reconcileTimer();
    emit stateChanged();
    emit playbackChanged();
    emit queueFocusRequested(-1);
    return true;
}
void PlayerPresenter::handlePlaybackEvent(PlaybackEvent event) {
    if (uses_frozen_fixture_ || exiting_)
        return;
    const PlaybackSnapshot before = controller_.confirmedSnapshot();
    const auto result = local_track_session_->applyPlaybackEvent(event);
    if (result == LocalTrackPlaybackEventResult::Ignored)
        return;

    controller_.applyBackendSnapshot(event.snapshot);
    bool state_projection_changed = false;
    if (result == LocalTrackPlaybackEventResult::OccurrenceUnavailable) {
        const auto current = local_queue_.currentIndex();
        if (!current.has_value())
            throw std::logic_error{"a current local playback error requires a current queue occurrence"};

        const auto occurrence_id = local_queue_.items()[*current].occurrence_id;
        if (local_queue_.items()[*current].available) {
            const QString error_code = localPlaybackErrorCode(event.snapshot.error);
            if (error_code.isEmpty() || !local_queue_.markUnavailable(*current) ||
                !local_queue_model_.markUnavailable(occurrence_id, error_code)) {
                throw std::logic_error{"a typed local playback error could not be projected"};
            }
            navigation_items_[*current].available = false;
            state_projection_changed = true;
        }

        auto start_intent = local_recovery_intent_.value_or(local_track_session_->startIntent());
        if (!local_recovery_intent_.has_value()) {
            if (before.status == PlaybackStatus::Ready || before.status == PlaybackStatus::Paused)
                start_intent = LocalTrackStartIntent::StayReady;
            else if (before.status == PlaybackStatus::Playing)
                start_intent = LocalTrackStartIntent::Autoplay;
        }
        local_recovery_intent_ = start_intent;
        if (const auto next = nextAvailableLocalIndex(); next.has_value() && loadLocalOccurrence(*next, start_intent)) {
            state_projection_changed = true;
        }
    } else if (result == LocalTrackPlaybackEventResult::Ended) {
        if (const auto next = nextAvailableLocalIndex();
            next.has_value() && loadLocalOccurrence(*next, LocalTrackStartIntent::Autoplay)) {
            state_projection_changed = true;
        } else if (!next.has_value() && preferences().queue_end_behavior == QueueEndBehavior::StopClearSelection &&
                   local_track_session_->unload()) {
            if (!local_queue_.clearSelection())
                throw std::logic_error{"an ended local queue could not clear its current occurrence"};
            local_recovery_intent_.reset();
            artwork_cache_->beginGeneration(MetadataGeneration{});
            state_projection_changed = true;
        }
    }

    if (state_projection_changed)
        controller_.applyBackendSnapshot(backend_.snapshot());
    reconcileTimer();
    const bool playback_changed = controller_.confirmedSnapshot() != before;
    if (state_projection_changed || playback_changed)
        emit stateChanged();
    if (playback_changed)
        emit playbackChanged();
}
void PlayerPresenter::pollMetadata() {
    if (uses_frozen_fixture_ || exiting_)
        return;
    const auto duration_before = localDurationMs();
    if (!local_track_session_->pollMetadata())
        return;
    const auto& metadata = local_track_session_->trackMetadata();
    if (metadata.has_value() && metadata->artwork.has_value())
        static_cast<void>(artwork_cache_->publish(local_track_session_->metadataGeneration(), *metadata->artwork));
    const auto current = local_queue_.currentIndex();
    if (current.has_value()) {
        static_cast<void>(local_queue_model_.updateMetadata(local_queue_.items()[*current].occurrence_id,
                                                            currentTitle(), currentArtist(), currentAlbum(),
                                                            localDurationMs()));
    }
    reconcileTimer();
    emit stateChanged();
    if (duration_before != localDurationMs())
        emit playbackChanged();
}
std::optional<std::int64_t> PlayerPresenter::localDurationMs() const noexcept {
    if (uses_frozen_fixture_ || !local_track_session_->hasTrack())
        return std::nullopt;
    if (controller_.confirmedSnapshot().duration_ms > 0)
        return controller_.confirmedSnapshot().duration_ms;
    const auto& metadata = local_track_session_->trackMetadata();
    if (metadata.has_value() && metadata->audio.duration_ms.has_value() && *metadata->audio.duration_ms > 0)
        return metadata->audio.duration_ms;
    return std::nullopt;
}
DeterministicPlaybackBackend* PlayerPresenter::fixtureBackend() noexcept { return owned_backend_.get(); }
const DeterministicPlaybackBackend* PlayerPresenter::fixtureBackend() const noexcept { return owned_backend_.get(); }
void PlayerPresenter::reportTrayUnavailable() {
    if (exiting_ || window_notice_ == WindowNotice::TrayUnavailableNotice)
        return;
    window_notice_ = WindowNotice::TrayUnavailableNotice;
    emit lifecycleChanged();
}
bool PlayerPresenter::requestClose() {
    if (exiting_)
        return false;
    refreshPlatformAvailability();
    if (exiting_)
        return false;
    if (closePreference() == ClosePreference::Exit)
        return requestExit();
    if (window_notice_ == WindowNotice::TrayCloseNotice)
        return false;
    if (tray_available_ && tray_notice_enabled_ && !tray_notice_seen_) {
        window_notice_ = WindowNotice::TrayCloseNotice;
        emit lifecycleChanged();
        return true;
    }
    return requestHide();
}
bool PlayerPresenter::requestHide() {
    if (exiting_)
        return false;
    refreshPlatformAvailability();
    if (exiting_)
        return false;
    const auto disposition = controller_.requestHide();
    if (exiting_)
        return true;
    auto next_notice = window_notice_;
    if (disposition == CloseDisposition::Minimized)
        next_notice = WindowNotice::TrayUnavailableNotice;
    else if (window_notice_ == WindowNotice::TrayCloseNotice)
        next_notice = WindowNotice::NoNotice;
    if (window_notice_ != next_notice) {
        window_notice_ = next_notice;
        emit lifecycleChanged();
    }
    return true;
}
bool PlayerPresenter::requestRestore() {
    if (exiting_)
        return false;
    controller_.requestRestore();
    return true;
}
bool PlayerPresenter::requestExit() {
    if (exiting_)
        return false;
    exiting_ = true;
    window_notice_ = WindowNotice::NoNotice;
    timer_.stop();
    emit lifecycleChanged();
    controller_.requestExit();
    return true;
}
bool PlayerPresenter::confirmWindowNotice() {
    if (exiting_ || window_notice_ == WindowNotice::NoNotice)
        return false;
    if (window_notice_ == WindowNotice::TrayCloseNotice) {
        tray_notice_seen_ = true;
        return requestHide();
    }
    return cancelWindowNotice();
}
bool PlayerPresenter::cancelWindowNotice() {
    if (exiting_ || window_notice_ == WindowNotice::NoNotice)
        return false;
    window_notice_ = WindowNotice::NoNotice;
    emit lifecycleChanged();
    return true;
}
bool PlayerPresenter::setTrayNoticeEnabled(const bool enabled) {
    if (exiting_ || tray_notice_enabled_ == enabled)
        return false;
    tray_notice_enabled_ = enabled;
    emit lifecycleChanged();
    return true;
}
void PlayerPresenter::refreshPlatformAvailability() {
    const bool available = platform_.trayAvailable();
    if (available == tray_available_)
        return;
    tray_available_ = available;
    if (!available && startMinimized()) {
        static_cast<void>(controller_.setStartMinimized(false));
        emit preferencesChanged();
    }
    emit platformChanged();
}
bool PlayerPresenter::togglePlayback() {
    const PlaybackSnapshot before = controller_.confirmedSnapshot();
    if (uses_frozen_fixture_ && before.status != PlaybackStatus::Playing && before.duration_ms > 0 &&
        before.position_ms >= before.duration_ms)
        return false;
    if (!controller_.togglePlayback())
        return false;
    controller_.applyBackendSnapshot(backend_.snapshot());
    const bool changed = before != controller_.confirmedSnapshot();
    if (changed) {
        reconcileTimer();
        emit playbackChanged();
    }
    return uses_frozen_fixture_ ? changed : true;
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
bool PlayerPresenter::previousTrack() { return navigate(QueueDirection::Previous); }
bool PlayerPresenter::nextTrack() { return navigate(QueueDirection::Next); }
std::optional<std::size_t> PlayerPresenter::localOccurrenceIndex(const QString& occurrence_id) const noexcept {
    const auto items = local_queue_.items();
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (LocalQueueListModel::occurrenceId(items[index].occurrence_id) == occurrence_id)
            return index;
    }
    return std::nullopt;
}
std::optional<std::size_t> PlayerPresenter::nextAvailableLocalIndex() const noexcept {
    const auto current = local_queue_.currentIndex();
    if (!current.has_value())
        return std::nullopt;
    for (std::size_t index = *current + 1; index < local_queue_.size(); ++index) {
        if (local_queue_.items()[index].available)
            return index;
    }
    return std::nullopt;
}
bool PlayerPresenter::loadLocalOccurrence(const std::size_t index, const LocalTrackStartIntent start_intent) {
    if (index >= local_queue_.size() || !local_queue_.items()[index].available ||
        !local_track_session_->open(local_queue_.items()[index].source_path, start_intent)) {
        return false;
    }
    if (!local_queue_.select(index))
        throw std::logic_error{"an accepted local occurrence could not be selected"};
    local_recovery_intent_.reset();
    artwork_cache_->beginGeneration(local_track_session_->metadataGeneration());
    return true;
}
bool PlayerPresenter::navigate(const QueueDirection direction) {
    const auto target = navigationTarget(direction);
    if (!target.has_value())
        return false;
    if (*target == static_cast<std::size_t>(currentQueueIndex()))
        return beginSeekPreview(0) && commitSeekPreview();

    if (!uses_frozen_fixture_) {
        const bool recovering_terminal_source =
            controller_.confirmedSnapshot().status == PlaybackStatus::Error &&
            !navigation_items_[static_cast<std::size_t>(currentQueueIndex())].available;
        const auto start_intent = recovering_terminal_source
                                      ? local_recovery_intent_.value_or(local_track_session_->startIntent())
                                      : (controller_.confirmedSnapshot().status == PlaybackStatus::Playing
                                             ? LocalTrackStartIntent::Autoplay
                                             : LocalTrackStartIntent::StayReady);
        if (!loadLocalOccurrence(*target, start_intent)) {
            return false;
        }
        controller_.applyBackendSnapshot(backend_.snapshot());
        reconcileTimer();
        emit stateChanged();
        emit playbackChanged();
        return true;
    }

    const QueueItem& track = bundle_.queue.at(static_cast<qsizetype>(*target));
    auto* const fixture_backend = fixtureBackend();
    if (fixture_backend == nullptr || !track.duration_ms.has_value() ||
        !fixture_backend->selectTrack(*track.duration_ms, controller_.confirmedSnapshot().status))
        return false;
    controller_.applyBackendSnapshot(backend_.snapshot());
    state_ = stateById(bundle_, QStringLiteral("playing"));
    state_.current_occurrence_id = track.occurrence_id;
    state_.position_ms = backend_.snapshot().position_ms;
    state_.duration_ms = backend_.snapshot().duration_ms;
    state_.seekable = backend_.snapshot().seekable;
    reconcileTimer();
    emit stateChanged();
    emit playbackChanged();
    return true;
}
bool PlayerPresenter::beginSeekPreview(const qint64 position_ms) {
    const PlaybackStatus status = controller_.confirmedSnapshot().status;
    const bool transport_active =
        status == PlaybackStatus::Ready || status == PlaybackStatus::Paused || status == PlaybackStatus::Playing;
    const bool has_transport_source = uses_frozen_fixture_ ? queueLoaded() : local_track_session_->hasTrack();
    if (!has_transport_source || !transport_active || !seekable() || controller_.seekPreviewActive() ||
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
    ++seek_commit_attempt_count_;
    if (!controller_.seekPreviewActive() || controller_.seekPending())
        return false;
    if (!controller_.commitSeekPreview())
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
    if (!controller_.toggleMute())
        return false;
    emit preferencesChanged();
    return true;
}
bool PlayerPresenter::setThemePreference(const ThemePreference theme) {
    ThemeMode value;
    switch (theme) {
    case ThemePreference::SystemTheme:
        value = ThemeMode::System;
        break;
    case ThemePreference::LightTheme:
        value = ThemeMode::Light;
        break;
    case ThemePreference::DarkTheme:
        value = ThemeMode::Dark;
        break;
    default:
        return false;
    }
    if (value == preferences().theme)
        return false;
    controller_.setTheme(value);
    emit preferencesChanged();
    return true;
}
bool PlayerPresenter::setMotionPreference(const MotionPreference motion) {
    MotionMode value;
    switch (motion) {
    case MotionPreference::SystemMotion:
        value = MotionMode::System;
        break;
    case MotionPreference::ReducedMotion:
        value = MotionMode::Reduced;
        break;
    default:
        return false;
    }
    if (value == preferences().motion)
        return false;
    controller_.setMotionMode(value);
    emit preferencesChanged();
    return true;
}
bool PlayerPresenter::setSkipBackwardSeconds(const int seconds) {
    if (seconds == skipBackwardSeconds() || !controller_.setSkipBackwardSeconds(seconds))
        return false;
    emit preferencesChanged();
    return true;
}
bool PlayerPresenter::setSkipForwardSeconds(const int seconds) {
    if (seconds == skipForwardSeconds() || !controller_.setSkipForwardSeconds(seconds))
        return false;
    emit preferencesChanged();
    return true;
}
bool PlayerPresenter::setClosePreference(const ClosePreference behavior) {
    CloseBehavior value;
    switch (behavior) {
    case ClosePreference::MinimizeToTray:
        value = CloseBehavior::MinimizeToTray;
        break;
    case ClosePreference::Exit:
        value = CloseBehavior::Exit;
        break;
    default:
        return false;
    }
    if (value == preferences().close_behavior)
        return false;
    controller_.setCloseBehavior(value);
    emit preferencesChanged();
    return true;
}
bool PlayerPresenter::setRestoreLastSession(const bool restore) {
    if (restore == restoreLastSession())
        return false;
    controller_.setRestoreLastSession(restore);
    emit preferencesChanged();
    return true;
}
bool PlayerPresenter::setStartMinimized(const bool minimized) {
    if (minimized == startMinimized() || !controller_.setStartMinimized(minimized))
        return false;
    emit preferencesChanged();
    return true;
}
bool PlayerPresenter::setQueueEndPreference(const QueueEndPreference behavior) {
    QueueEndBehavior value;
    switch (behavior) {
    case QueueEndPreference::StopKeepSelection:
        value = QueueEndBehavior::StopKeepSelection;
        break;
    case QueueEndPreference::StopClearSelection:
        value = QueueEndBehavior::StopClearSelection;
        break;
    default:
        return false;
    }
    if (value == preferences().queue_end_behavior)
        return false;
    controller_.setQueueEndBehavior(value);
    emit preferencesChanged();
    return true;
}
bool PlayerPresenter::enterFrozenUnavailableScenario() {
    auto* const fixture_backend = fixtureBackend();
    if (fixture_backend == nullptr || !queueLoaded() || state_.state_id != QStringLiteral("playing") ||
        !fixture_backend->enterUnavailableScenario())
        return false;
    controller_.applyBackendSnapshot(backend_.snapshot());
    state_ = stateById(bundle_, QStringLiteral("error"));
    reconcileTimer();
    emit stateChanged();
    emit playbackChanged();
    return true;
}
bool PlayerPresenter::skipUnavailable() {
    auto* const fixture_backend = fixtureBackend();
    if (fixture_backend == nullptr || state_.state_id != QStringLiteral("error") ||
        !fixture_backend->restoreInitialScenario())
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
    auto* const fixture_backend = fixtureBackend();
    if (fixture_backend == nullptr || !queueLoaded())
        return false;
    const auto before = controller_.confirmedSnapshot();
    const auto before_displayed = controller_.displayedPositionMs();
    const bool before_preview = controller_.seekPreviewActive();
    const bool before_pending = controller_.seekPending();
    const DeterministicTickResult result = fixture_backend->tick();
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
void PlayerPresenter::rejectNextSeekForTest() noexcept {
    if (auto* const fixture_backend = fixtureBackend(); fixture_backend != nullptr)
        fixture_backend->rejectNextSeek();
}
std::size_t PlayerPresenter::seekCommitAttemptCountForTest() const noexcept { return seek_commit_attempt_count_; }
void PlayerPresenter::reconcileTimer() {
    const bool needed =
        uses_frozen_fixture_
            ? !exiting_ && queueLoaded() &&
                  (controller_.confirmedSnapshot().status == PlaybackStatus::Playing || controller_.seekPending())
            : !exiting_ && local_track_session_->metadataStatus() == LocalTrackMetadataStatus::Pending;
    if (needed && !timer_.isActive())
        timer_.start();
    else if (!needed && timer_.isActive())
        timer_.stop();
}
bool PlayerPresenter::loadFrozenQueue() {
    if (!uses_frozen_fixture_ || queue_model_.loaded() || bundle_.queue.isEmpty())
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
