#include "presentation/PlayerPresenter.hpp"

#include "boundary/FilesystemPath.hpp"
#include "m0/adapters/SingleWorkerLocalSourceDiscovery.hpp"
#include "m0/ports/AudioOutputSelection.hpp"
#include "presentation/ArtworkImageProvider.hpp"

#include <QDateTime>
#include <QMetaObject>
#include <QPointer>
#include <QRandomGenerator>
#include <QScopeGuard>
#include <QThread>
#include <QTimeZone>
#include <QVariantMap>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace music_player::m0 {
namespace {

constexpr std::int64_t restored_position_tolerance_ms = 50;

[[nodiscard]] PlayerPresenter::PersistenceWarningScope warningScopeForSources(const bool preferences,
                                                                              const bool session) noexcept {
    if (preferences && session)
        return PlayerPresenter::PersistenceWarningScope::CombinedPersistence;
    if (preferences)
        return PlayerPresenter::PersistenceWarningScope::PreferencePersistence;
    if (session)
        return PlayerPresenter::PersistenceWarningScope::SessionPersistence;
    return PlayerPresenter::PersistenceWarningScope::NoPersistenceWarningScope;
}

[[nodiscard]] int persistenceWarningPriority(const PlayerPresenter::PreferenceWarning warning) noexcept {
    switch (warning) {
    case PlayerPresenter::PreferenceWarning::NoPreferenceWarning:
        return 0;
    case PlayerPresenter::PreferenceWarning::RecoveredFromBackup:
        return 1;
    case PlayerPresenter::PreferenceWarning::UsingDefaults:
        return 2;
    case PlayerPresenter::PreferenceWarning::SaveFailed:
        return 3;
    case PlayerPresenter::PreferenceWarning::FutureVersion:
        return 4;
    }
    return 0;
}

[[noreturn]] void invalidMapping(const char* field) {
    throw std::invalid_argument{std::string{"unknown fixture mapping: "} + field};
}

#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
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
#endif

[[nodiscard]] bool validPreferences(const Preferences& preferences) noexcept {
    switch (preferences.theme) {
    case ThemeMode::System:
    case ThemeMode::Light:
    case ThemeMode::Dark:
        break;
    default:
        return false;
    }
    switch (preferences.motion) {
    case MotionMode::System:
    case MotionMode::Reduced:
        break;
    default:
        return false;
    }
    switch (preferences.close_behavior) {
    case CloseBehavior::MinimizeToTray:
    case CloseBehavior::Exit:
        break;
    default:
        return false;
    }
    switch (preferences.queue_end_behavior) {
    case QueueEndBehavior::StopKeepSelection:
    case QueueEndBehavior::StopClearSelection:
        break;
    default:
        return false;
    }
    const bool valid_window_placement = !preferences.window_placement || (preferences.window_placement->width > 0 &&
                                                                          preferences.window_placement->height > 0);
    return std::isfinite(preferences.volume) && preferences.volume >= 0.0 && preferences.volume <= 1.0 &&
           preferences.skip_backward_seconds >= 1 && preferences.skip_backward_seconds <= 300 &&
           preferences.skip_forward_seconds >= 1 && preferences.skip_forward_seconds <= 300 && valid_window_placement;
}

#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
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
#endif

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

#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
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
#endif

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

QString displayPath(const std::filesystem::path& value) {
#ifdef Q_OS_WIN
    return QString::fromStdWString(value.native());
#else
    const auto& native = value.native();
    return QString::fromUtf8(native.data(), static_cast<qsizetype>(native.size()));
#endif
}

QString metadataFieldId(const MetadataField field) {
    switch (field) {
    case MetadataField::Title:
        return QStringLiteral("title");
    case MetadataField::Artist:
        return QStringLiteral("artist");
    case MetadataField::Album:
        return QStringLiteral("album");
    case MetadataField::TrackNumber:
        return QStringLiteral("track_number");
    case MetadataField::Year:
        return QStringLiteral("year");
    }
    return QStringLiteral("unknown");
}

QString metadataDiagnosticId(const MetadataDiagnosticCode code) {
    switch (code) {
    case MetadataDiagnosticCode::FieldTooLarge:
        return QStringLiteral("metadata_field_too_large");
    case MetadataDiagnosticCode::RetainedMetadataTooLarge:
        return QStringLiteral("metadata_total_too_large");
    case MetadataDiagnosticCode::TextSanitized:
        return QStringLiteral("metadata_text_sanitized");
    case MetadataDiagnosticCode::InvalidValue:
        return QStringLiteral("metadata_invalid_value");
    }
    return QStringLiteral("metadata_invalid_data");
}

QString metadataReadErrorId(const MetadataReadErrorCode code) {
    switch (code) {
    case MetadataReadErrorCode::SourceUnavailable:
        return QStringLiteral("source_unavailable");
    case MetadataReadErrorCode::UnsupportedFormat:
        return QStringLiteral("unsupported_format");
    case MetadataReadErrorCode::InvalidData:
        return QStringLiteral("invalid_data");
    case MetadataReadErrorCode::ReadFailed:
        return QStringLiteral("read_failed");
    case MetadataReadErrorCode::Cancelled:
        return QStringLiteral("cancelled");
    }
    return QStringLiteral("read_failed");
}

QString artworkDiagnosticId(const ArtworkDiagnosticCode code) {
    switch (code) {
    case ArtworkDiagnosticCode::InvalidData:
        return QStringLiteral("artwork_invalid_data");
    case ArtworkDiagnosticCode::EncodedBytesExceeded:
        return QStringLiteral("artwork_encoded_bytes_exceeded");
    case ArtworkDiagnosticCode::AxisPixelsExceeded:
        return QStringLiteral("artwork_axis_pixels_exceeded");
    case ArtworkDiagnosticCode::PixelCountExceeded:
        return QStringLiteral("artwork_pixel_count_exceeded");
    }
    return QStringLiteral("artwork_invalid_data");
}

QVariant unsignedVariant(const std::uint64_t value) {
    return QVariant::fromValue<qulonglong>(static_cast<qulonglong>(value));
}

QString unsignedDecimal(const std::uint64_t value) { return QString::number(static_cast<qulonglong>(value)); }

QString modifiedUtc(const std::filesystem::file_time_type value) {
    const auto file_now = std::filesystem::file_time_type::clock::now();
    const auto system_now = std::chrono::system_clock::now();
    const auto offset = std::chrono::duration_cast<std::chrono::system_clock::duration>(value - file_now);
    const auto system_time = std::chrono::time_point_cast<std::chrono::milliseconds>(system_now + offset);
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(system_time.time_since_epoch());
    const QDateTime timestamp =
        QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(milliseconds.count()), QTimeZone::UTC);
    return timestamp.isValid() ? timestamp.toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss'Z'")) : QString{};
}

bool isAudioOutputError(const PlaybackErrorCode error) noexcept {
    return error == PlaybackErrorCode::AudioOutputUnavailable || error == PlaybackErrorCode::AudioOutputChanged;
}

bool isSourceVolumeRemoval(const PlaybackEvent& event) noexcept {
    return event.type == PlaybackEventType::SnapshotChanged && event.snapshot.status == PlaybackStatus::Error &&
           event.snapshot.error == PlaybackErrorCode::SourceVolumeRemoved;
}

bool preservesPositionForRecovery(const PlaybackErrorCode error) noexcept {
    return isAudioOutputError(error) || error == PlaybackErrorCode::SourceVolumeRemoved ||
           error == PlaybackErrorCode::SourceUnavailable || error == PlaybackErrorCode::InvalidMedia ||
           error == PlaybackErrorCode::AccessDenied;
}

QString localPlaybackErrorCode(const PlaybackErrorCode error) {
    switch (error) {
    case PlaybackErrorCode::SourceUnavailable:
        return QStringLiteral("source_unavailable");
    case PlaybackErrorCode::SourceVolumeRemoved:
        return QStringLiteral("source_volume_removed");
    case PlaybackErrorCode::InvalidMedia:
        return QStringLiteral("invalid_media");
    case PlaybackErrorCode::AccessDenied:
        return QStringLiteral("access_denied");
    case PlaybackErrorCode::NetworkFailure:
        return QStringLiteral("network_failure");
    case PlaybackErrorCode::BackendFailure:
        return QStringLiteral("backend_failure");
    case PlaybackErrorCode::AudioOutputUnavailable:
        return QStringLiteral("audio_output_unavailable");
    case PlaybackErrorCode::AudioOutputChanged:
        return QStringLiteral("audio_output_changed");
    case PlaybackErrorCode::None:
        return {};
    }
    return {};
}

QString localPlaybackErrorMessageKey(const PlaybackErrorCode error) {
    switch (error) {
    case PlaybackErrorCode::SourceUnavailable:
        return QStringLiteral("source_unavailable_error");
    case PlaybackErrorCode::SourceVolumeRemoved:
        return QStringLiteral("source_volume_removed_error");
    case PlaybackErrorCode::InvalidMedia:
        return QStringLiteral("invalid_media_error");
    case PlaybackErrorCode::AccessDenied:
        return QStringLiteral("access_denied_error");
    case PlaybackErrorCode::AudioOutputUnavailable:
        return QStringLiteral("audio_output_unavailable_error");
    case PlaybackErrorCode::AudioOutputChanged:
        return QStringLiteral("audio_output_changed_error");
    case PlaybackErrorCode::None:
    case PlaybackErrorCode::NetworkFailure:
    case PlaybackErrorCode::BackendFailure:
        return QStringLiteral("backend_failure_error");
    }
    return QStringLiteral("backend_failure_error");
}

void addSaturated(std::uint64_t& target, const std::uint64_t increment) noexcept {
    target = increment > std::numeric_limits<std::uint64_t>::max() - target ? std::numeric_limits<std::uint64_t>::max()
                                                                            : target + increment;
}

} // namespace

#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
PlayerPresenter::PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform, QObject* parent)
    : QObject(parent), fixture_bundle_(std::make_unique<FixtureBundle>(std::move(bundle))),
      navigation_items_(navigationItems(*fixture_bundle_)), platform_(platform),
      tray_available_(platform.trayAvailable()),
      default_apps_settings_supported_(platform.defaultAppsSettingsSupported()),
      default_apps_settings_available_(default_apps_settings_supported_ && platform.defaultAppsSettingsAvailable()),
      owned_backend_(std::make_unique<DeterministicPlaybackBackend>(backendConfig(*fixture_bundle_))),
      backend_(*owned_backend_), controller_(backend_, platform), local_queue_model_(this), timer_(this),
      state_(stateById(*fixture_bundle_, fixture_bundle_->default_state_id)),
      artwork_cache_(std::make_shared<ArtworkImageCache>()), uses_frozen_fixture_(true) {
    initializeDefaults();
    timer_.setInterval(static_cast<int>(fixture_bundle_->fake_backend.snapshots.interval_ms));
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, [this] {
        pollPersistence();
        if (!exit_committed_ && applicationPollingEnabled())
            static_cast<void>(advanceOneTick());
    });
}

PlayerPresenter::PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform,
                                 PlayerPresenterPreferences preferences, QObject* parent)
    : QObject(parent), fixture_bundle_(std::make_unique<FixtureBundle>(std::move(bundle))),
      navigation_items_(navigationItems(*fixture_bundle_)), platform_(platform),
      tray_available_(platform.trayAvailable()),
      default_apps_settings_supported_(platform.defaultAppsSettingsSupported()),
      default_apps_settings_available_(default_apps_settings_supported_ && platform.defaultAppsSettingsAvailable()),
      owned_backend_(std::make_unique<DeterministicPlaybackBackend>(backendConfig(*fixture_bundle_))),
      backend_(*owned_backend_), controller_(backend_, platform, PlaybackSnapshot{}, preferences.initial),
      local_queue_model_(this), timer_(this), state_(stateById(*fixture_bundle_, fixture_bundle_->default_state_id)),
      artwork_cache_(std::make_shared<ArtworkImageCache>()), uses_frozen_fixture_(true) {
    initializePersistedPreferences(preferences);
    timer_.setInterval(static_cast<int>(fixture_bundle_->fake_backend.snapshots.interval_ms));
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, [this] {
        pollPersistence();
        if (!exit_committed_ && applicationPollingEnabled())
            static_cast<void>(advanceOneTick());
    });
    reconcileTimer();
}
#endif

PlayerPresenter::PlayerPresenter(PlatformIntegration& platform, PlaybackBackend& playback_backend,
                                 MetadataReader& metadata_reader, AudioFilePicker audio_file_picker, QObject* parent)
    : PlayerPresenter(platform, playback_backend, metadata_reader, std::move(audio_file_picker),
                      std::make_shared<ArtworkImageCache>(), parent) {}

PlayerPresenter::PlayerPresenter(PlatformIntegration& platform, PlaybackBackend& playback_backend,
                                 MetadataReader& metadata_reader, AudioFilePicker audio_file_picker,
                                 PlayerPresenterPreferences preferences, QObject* parent)
    : PlayerPresenter(platform, playback_backend, metadata_reader, std::move(audio_file_picker),
                      std::make_shared<ArtworkImageCache>(), std::move(preferences), parent) {}

PlayerPresenter::PlayerPresenter(PlatformIntegration& platform, PlaybackBackend& playback_backend,
                                 MetadataReader& metadata_reader, AudioFilePicker audio_file_picker,
                                 std::shared_ptr<ArtworkImageCache> artwork_cache, QObject* parent)
    : QObject(parent), platform_(platform), tray_available_(platform.trayAvailable()),
      default_apps_settings_supported_(platform.defaultAppsSettingsSupported()),
      default_apps_settings_available_(default_apps_settings_supported_ && platform.defaultAppsSettingsAvailable()),
      backend_(playback_backend), controller_(backend_, platform, backend_.snapshot()), local_queue_model_(this),
      timer_(this), audio_file_picker_(std::move(audio_file_picker)),
      local_track_session_(std::make_unique<LocalTrackSession>(backend_, metadata_reader)),
      artwork_cache_(std::move(artwork_cache)),
      owned_source_discovery_(std::make_unique<SingleWorkerLocalSourceDiscovery>()),
      source_discovery_(owned_source_discovery_.get()) {
    initializeLocalMode();
}

PlayerPresenter::PlayerPresenter(PlatformIntegration& platform, PlaybackBackend& playback_backend,
                                 MetadataReader& metadata_reader, AudioFilePicker audio_file_picker,
                                 std::shared_ptr<ArtworkImageCache> artwork_cache,
                                 PlayerPresenterPreferences preferences, QObject* parent)
    : QObject(parent), platform_(platform), tray_available_(platform.trayAvailable()),
      default_apps_settings_supported_(platform.defaultAppsSettingsSupported()),
      default_apps_settings_available_(default_apps_settings_supported_ && platform.defaultAppsSettingsAvailable()),
      backend_(playback_backend), controller_(backend_, platform, backend_.snapshot(), preferences.initial),
      local_queue_model_(this), timer_(this), audio_file_picker_(std::move(audio_file_picker)),
      local_track_session_(std::make_unique<LocalTrackSession>(backend_, metadata_reader)),
      artwork_cache_(std::move(artwork_cache)),
      owned_source_discovery_(std::make_unique<SingleWorkerLocalSourceDiscovery>()),
      source_discovery_(owned_source_discovery_.get()) {
    initializeLocalMode(&preferences);
}

PlayerPresenter::PlayerPresenter(PlatformIntegration& platform, PlaybackBackend& playback_backend,
                                 MetadataReader& metadata_reader, AudioFilePicker audio_file_picker,
                                 LocalSourceDiscovery& source_discovery, QObject* parent)
    : PlayerPresenter(platform, playback_backend, metadata_reader, std::move(audio_file_picker), source_discovery,
                      std::make_shared<ArtworkImageCache>(), parent) {}

PlayerPresenter::PlayerPresenter(PlatformIntegration& platform, PlaybackBackend& playback_backend,
                                 MetadataReader& metadata_reader, AudioFilePicker audio_file_picker,
                                 LocalSourceDiscovery& source_discovery,
                                 std::shared_ptr<ArtworkImageCache> artwork_cache, QObject* parent)
    : QObject(parent), platform_(platform), tray_available_(platform.trayAvailable()),
      default_apps_settings_supported_(platform.defaultAppsSettingsSupported()),
      default_apps_settings_available_(default_apps_settings_supported_ && platform.defaultAppsSettingsAvailable()),
      backend_(playback_backend), controller_(backend_, platform, backend_.snapshot()), local_queue_model_(this),
      timer_(this), audio_file_picker_(std::move(audio_file_picker)),
      local_track_session_(std::make_unique<LocalTrackSession>(backend_, metadata_reader)),
      artwork_cache_(std::move(artwork_cache)), source_discovery_(&source_discovery) {
    initializeLocalMode();
}

void PlayerPresenter::initializeLocalMode(const PlayerPresenterPreferences* preferences) {
    queue_shuffle_random_.seed(QRandomGenerator::global()->generate());
    if (!audio_file_picker_)
        throw std::invalid_argument{"audio file picker is required"};
    if (artwork_cache_ == nullptr)
        throw std::invalid_argument{"artwork image cache is required"};
    if (source_discovery_ == nullptr)
        throw std::invalid_argument{"local source discovery is required"};
    if (preferences == nullptr) {
        const Preferences& defaults = controller_.preferences();
        if (backend_.setVolume(defaults.volume) != PlaybackCommandResult::Accepted ||
            backend_.setMuted(defaults.muted) != PlaybackCommandResult::Accepted)
            invalidMapping("default audio preferences");
    } else {
        initializePersistedPreferences(*preferences);
    }
    backend_.setEventCallback([this](const PlaybackEvent& event) {
        const PlaybackEvent copy = event;
        QMetaObject::invokeMethod(this, [this, copy] { handlePlaybackEvent(copy); }, Qt::QueuedConnection);
    });
    audio_output_selection_ = dynamic_cast<AudioOutputSelection*>(&backend_);
    if (audio_output_selection_ != nullptr) {
        audio_output_selection_->setAudioOutputsChangedCallback(
            [this] { QMetaObject::invokeMethod(this, [this] { emit audioOutputsChanged(); }, Qt::QueuedConnection); });
    }
    if (preferences != nullptr)
        initializePersistedSession(*preferences);
    initializeShuffleRound();
    timer_.setInterval(25);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, [this] {
        pollPersistence();
        if (applicationPollingEnabled()) {
            if (!exiting_)
                pollMetadata();
            pollSourceImport();
        }
    });
    reconcileTimer();
}

PlayerPresenter::~PlayerPresenter() {
    if (audio_output_selection_ != nullptr)
        audio_output_selection_->setAudioOutputsChangedCallback({});
    file_dialog_cancellation_active_ = true;
    cancelAudioFileSelection();
    cancelFileLocation();
    timer_.stop();
    if (!uses_frozen_fixture_) {
        if (sourceImportActive() && source_discovery_ != nullptr)
            static_cast<void>(source_discovery_->cancel(source_import_generation_));
        if (owned_source_discovery_ != nullptr)
            static_cast<void>(owned_source_discovery_->shutdown());
        backend_.setEventCallback({});
        if (local_track_session_ != nullptr)
            static_cast<void>(local_track_session_->cancelMetadata());
    }
}

#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
void PlayerPresenter::initializeDefaults() {
    const FixtureDefaults& defaults = fixture_bundle_->preferences.defaults;
    const ThemeMode theme = themeMode(defaults.theme);
    const MotionMode motion = motionMode(defaults.motion);
    const CloseBehavior close = closeBehavior(defaults.close_behavior);
    const QueueEndBehavior queue_end = queueEndBehavior(defaults.queue_end_behavior);
    for (const FixtureState& fixture_state : fixture_bundle_->states) {
        static_cast<void>(mapPlaybackState(fixture_state.playback_state));
    }
    static_cast<void>(stateById(*fixture_bundle_, QStringLiteral("playing")));
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
#endif

void PlayerPresenter::initializePersistedPreferences(const PlayerPresenterPreferences& preferences) {
    if (preferences.autosave == nullptr)
        throw std::invalid_argument{"preferences autosave is required"};
    if (!preferences.now)
        throw std::invalid_argument{"preferences exit clock is required"};
    if (preferences.exit_flush_timeout < std::chrono::milliseconds::zero())
        throw std::invalid_argument{"preferences exit flush timeout cannot be negative"};
    if (!validPreferences(preferences.initial))
        invalidMapping("persisted preferences");
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_) {
        for (const FixtureState& fixture_state : fixture_bundle_->states)
            static_cast<void>(mapPlaybackState(fixture_state.playback_state));
        static_cast<void>(stateById(*fixture_bundle_, QStringLiteral("playing")));
    }
#endif

    if (backend_.setVolume(preferences.initial.volume) != PlaybackCommandResult::Accepted ||
        backend_.setMuted(preferences.initial.muted) != PlaybackCommandResult::Accepted) {
        invalidMapping("persisted audio preferences");
    }

    preferences_autosave_ = preferences.autosave;
    preference_exit_flush_timeout_ = preferences.exit_flush_timeout;
    preference_now_ = preferences.now;
    switch (preferences.load_status) {
    case PreferencesLoadStatus::Missing:
    case PreferencesLoadStatus::Loaded:
        preference_persistence_warning_ = PreferenceWarning::NoPreferenceWarning;
        break;
    case PreferencesLoadStatus::RecoveredFromBackup:
        preference_persistence_warning_ = PreferenceWarning::RecoveredFromBackup;
        preferences_autosave_->scheduleRepair();
        break;
    case PreferencesLoadStatus::InvalidUsingDefaults:
        preference_persistence_warning_ = PreferenceWarning::UsingDefaults;
        break;
    case PreferencesLoadStatus::FutureVersion:
        preferences_future_version_pending_ = true;
        preference_persistence_warning_ = PreferenceWarning::FutureVersion;
        break;
    default:
        invalidMapping("preferences load status");
    }
    projectPersistenceWarning();
}

void PlayerPresenter::initializePersistedSession(const PlayerPresenterPreferences& preferences) {
    const PlayerPresenterSession& session = preferences.session;
    if (session.autosave == nullptr)
        return;
    if (!session.now)
        throw std::invalid_argument{"session persistence clock is required"};
    if (!isValidSessionSnapshot(session.initial))
        throw std::invalid_argument{"persisted session is invalid"};

    session_autosave_ = session.autosave;
    session_now_ = session.now;
    download_import_persistence_ready_ =
        session.load_status == SessionLoadStatus::Missing || session.load_status == SessionLoadStatus::Loaded;
    switch (session.load_status) {
    case SessionLoadStatus::Missing:
        return;
    case SessionLoadStatus::InvalidUsingDefaults:
        session_persistence_warning_ = PreferenceWarning::UsingDefaults;
        projectPersistenceWarning();
        return;
    case SessionLoadStatus::FutureVersion:
        session_future_version_pending_ = true;
        session_persistence_warning_ = PreferenceWarning::FutureVersion;
        projectPersistenceWarning();
        return;
    case SessionLoadStatus::Loaded:
        break;
    case SessionLoadStatus::RecoveredFromBackup:
        session_autosave_->scheduleRepair();
        session_persistence_warning_ = PreferenceWarning::RecoveredFromBackup;
        break;
    default:
        throw std::invalid_argument{"unknown persisted session load status"};
    }
    projectPersistenceWarning();

    download_import_receipts_ = session.initial.download_import_receipts;
    download_import_revisions_.resize(download_import_receipts_.size(), 0);
    retired_download_import_job_ids_ = session.initial.retired_download_import_job_ids;
    if (!preferences.initial.restore_last_session || session.initial.queue.items.empty())
        return;
    if (!local_queue_.restore(session.initial.queue) || !local_queue_model_.replace(local_queue_))
        throw std::logic_error{"a valid persisted local queue could not be restored"};
    navigation_items_ = navigationItems(local_queue_);

    const auto current = local_queue_.currentIndex();
    if (!current.has_value())
        return;
    if (!local_track_session_->open(local_queue_.items()[*current].source_path, LocalTrackStartIntent::RestorePaused)) {
        return;
    }

    restored_position_ = RestoredPosition{.generation = local_track_session_->playbackGeneration(),
                                          .target_ms = session.initial.position_ms};
    artwork_cache_->beginGeneration(local_track_session_->metadataGeneration());
    controller_.applyBackendSnapshot(backend_.snapshot());
}

SessionSnapshot PlayerPresenter::currentSessionSnapshot() const {
    SessionSnapshot result{.queue = local_queue_.snapshot(),
                           .position_ms = currentSessionPosition(),
                           .download_import_receipts = download_import_receipts_,
                           .retired_download_import_job_ids = retired_download_import_job_ids_};
    // All later queue snapshots preserve the proposed retirement while the
    // live ledger still owns its receipt and capacity until Saved.
    for (const auto& pending : download_import_retirements_) {
        std::erase_if(result.download_import_receipts,
                      [&](const auto& receipt) { return receipt.job_id == pending.job_id; });
        result.retired_download_import_job_ids.push_back(pending.job_id);
    }
    return result;
}

std::int64_t PlayerPresenter::currentSessionPosition() const {
    if (!local_queue_.currentOccurrenceId() || local_track_session_ == nullptr || !local_track_session_->hasTrack())
        return 0;
    if (restored_position_ && restored_position_->generation == local_track_session_->playbackGeneration())
        return restored_position_->target_ms;
    return std::max(controller_.confirmedSnapshot().position_ms, std::int64_t{0});
}

bool PlayerPresenter::downloadImportRetirementPending() const noexcept {
    return session_autosave_ && !download_import_retirements_.empty() &&
           download_import_retirements_.back().revision > session_autosave_->savedRevision();
}

void PlayerPresenter::commitSavedDownloadImportRetirements() noexcept {
    if (!session_autosave_ || download_import_retirements_.empty())
        return;
    const auto saved = session_autosave_->savedRevision();
    const auto pending =
        std::ranges::find_if(download_import_retirements_, [saved](const auto& item) { return item.revision > saved; });
    for (auto item = download_import_retirements_.begin(); item != pending; ++item) {
        const auto receipt = std::ranges::find(download_import_receipts_, item->job_id, &DownloadImportReceipt::job_id);
        if (receipt != download_import_receipts_.end()) {
            download_import_revisions_.erase(download_import_revisions_.begin() +
                                             (receipt - download_import_receipts_.begin()));
            download_import_receipts_.erase(receipt);
        }
        retired_download_import_job_ids_.push_back(std::move(item->job_id));
    }
    download_import_retirements_.erase(download_import_retirements_.begin(), pending);
}

void PlayerPresenter::discardStaleRestoredSessionState() {
    if (local_track_session_ == nullptr)
        return;
    const std::uint64_t generation = local_track_session_->playbackGeneration();
    if (restored_position_.has_value() && restored_position_->generation != generation)
        restored_position_.reset();
    if (restored_eof_generation_.has_value() && *restored_eof_generation_ != generation)
        restored_eof_generation_.reset();
}

bool PlayerPresenter::hasSourceVolumeRecovery() const noexcept {
    return local_track_session_ != nullptr && restored_position_ && restored_position_->source_volume_recovery &&
           restored_position_->generation == local_track_session_->playbackGeneration();
}

void PlayerPresenter::cancelRestoredPositionForExplicitTransport() {
    discardStaleRestoredSessionState();
    if (!restored_position_.has_value() && !restored_eof_generation_.has_value())
        return;
    restored_position_.reset();
    restored_eof_generation_.reset();
    observeSessionPosition(true);
}

void PlayerPresenter::sessionStructureDidChange() {
    if (shuffleQueueEnabled() && !uses_frozen_fixture_) {
        if (const auto current = local_queue_.currentOccurrenceId(); current.has_value())
            shuffle_round_visited_.insert(current->value);
    }
    discardStaleRestoredSessionState();
    if (session_autosave_ == nullptr || uses_frozen_fixture_ || exiting_)
        return;
    static_cast<void>(session_autosave_->scheduleStructural(currentSessionSnapshot()));
    reconcileTimer();
}

void PlayerPresenter::observeSessionPosition(const bool checkpoint) {
    discardStaleRestoredSessionState();
    if (session_autosave_ == nullptr || uses_frozen_fixture_ || exiting_ || restored_position_.has_value())
        return;
    const auto position_ms = currentSessionPosition();
    if (checkpoint)
        static_cast<void>(session_autosave_->checkpointConfirmedPosition(position_ms));
    else
        static_cast<void>(session_autosave_->observeConfirmedPosition(position_ms));
    reconcileTimer();
}

void PlayerPresenter::reconcileRestoredPosition() {
    if (!restored_position_.has_value())
        return;
    const PlaybackSnapshot& snapshot = controller_.confirmedSnapshot();
    if (snapshot.generation != restored_position_->generation)
        return;
    const auto recovery_error_pending = [this, &snapshot] {
        const auto& current = backend_.snapshot();
        return current.generation == snapshot.generation && current.status == PlaybackStatus::Error &&
               preservesPositionForRecovery(current.error);
    };
    // A command can fail before its queued error event reaches this presenter.
    if (recovery_error_pending())
        return;
    if (snapshot.status == PlaybackStatus::Error || snapshot.status == PlaybackStatus::Empty) {
        // Missing or unreadable restored media can be located again without losing its saved target.
        if (snapshot.status == PlaybackStatus::Error && preservesPositionForRecovery(snapshot.error))
            return;
        restored_position_.reset();
        return;
    }
    if (snapshot.status != PlaybackStatus::Ready && snapshot.status != PlaybackStatus::Paused)
        return;

    RestoredPosition& restored = *restored_position_;
    if (restored.target_ms > 0 && snapshot.duration_ms == 0)
        return;
    restored.target_ms = std::clamp(restored.target_ms, std::int64_t{0}, snapshot.duration_ms);
    const std::int64_t position_delta = snapshot.position_ms >= restored.target_ms
                                            ? snapshot.position_ms - restored.target_ms
                                            : restored.target_ms - snapshot.position_ms;
    if (!snapshot.seekable || position_delta <= restored_position_tolerance_ms) {
        if (snapshot.seekable && snapshot.duration_ms > 0 &&
            snapshot.duration_ms - restored.target_ms <= restored_position_tolerance_ms) {
            restored_eof_generation_ = restored.generation;
        }
        restored_position_.reset();
        if (session_autosave_ != nullptr)
            static_cast<void>(session_autosave_->checkpointConfirmedPosition(snapshot.position_ms));
        return;
    }
    if (!restored.seek_requested) {
        if (backend_.seekTo(restored.target_ms) == PlaybackCommandResult::Accepted)
            restored.seek_requested = true;
        else if (!recovery_error_pending()) {
            restored_position_.reset();
            if (session_autosave_ != nullptr)
                static_cast<void>(session_autosave_->checkpointConfirmedPosition(snapshot.position_ms));
        }
    }
}

QString PlayerPresenter::stateId() const {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return state_.state_id;
#endif
    if (controller_.confirmedSnapshot().status == PlaybackStatus::Error)
        return QStringLiteral("error");
    return local_track_session_->hasTrack() ? QStringLiteral("playing") : QStringLiteral("empty");
}
PlayerPresenter::PlaybackState PlayerPresenter::playbackState() const {
    return mapPlaybackState(controller_.confirmedSnapshot().status);
}
bool PlayerPresenter::queueLoaded() const noexcept {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return state_.queue_loaded;
#endif
    return !local_queue_.empty();
}
bool PlayerPresenter::hasCurrentOccurrence() const noexcept {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return state_.current_occurrence_id.has_value();
#endif
    return local_queue_.currentOccurrenceId().has_value();
}
QString PlayerPresenter::currentOccurrenceId() const {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return state_.current_occurrence_id.value_or(QString{});
#endif
    const auto current = local_queue_.currentOccurrenceId();
    return current.has_value() ? LocalQueueListModel::occurrenceId(*current) : QString{};
}
int PlayerPresenter::currentQueueIndex() const noexcept {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_) {
        if (state_.current_occurrence_id.has_value()) {
            for (qsizetype index = 0; index < fixture_bundle_->queue.size(); ++index) {
                if (fixture_bundle_->queue.at(index).occurrence_id == *state_.current_occurrence_id)
                    return static_cast<int>(index);
            }
        }
        return -1;
    }
#endif
    const auto current = local_queue_.currentIndex();
    return current.has_value() ? static_cast<int>(*current) : -1;
}
bool PlayerPresenter::hasContextOccurrence() const noexcept {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return state_.context_occurrence_id.has_value();
#endif
    return false;
}
QString PlayerPresenter::contextOccurrenceId() const {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return state_.context_occurrence_id.value_or(QString{});
#endif
    return QString{};
}
bool PlayerPresenter::hasPosition() const noexcept {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return state_.position_ms.has_value();
#endif
    return local_track_session_->hasTrack();
}
qint64 PlayerPresenter::positionMs() const noexcept { return controller_.displayedPositionMs(); }
qint64 PlayerPresenter::confirmedPositionMs() const noexcept { return controller_.confirmedSnapshot().position_ms; }
bool PlayerPresenter::seekPreviewActive() const noexcept { return controller_.seekPreviewActive(); }
bool PlayerPresenter::seekPending() const noexcept { return controller_.seekPending(); }
bool PlayerPresenter::hasDuration() const noexcept {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return state_.duration_ms.has_value();
#endif
    return localDurationMs().has_value();
}
qint64 PlayerPresenter::durationMs() const noexcept {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return hasDuration() ? controller_.confirmedSnapshot().duration_ms : 0;
#endif
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
    if (!uses_frozen_fixture_ && direction == QueueDirection::Next && repeatQueueEnabled() &&
        !controller_.seekPreviewActive() && !controller_.seekPending()) {
        const auto status = controller_.confirmedSnapshot().status;
        if (status == PlaybackStatus::Ready || status == PlaybackStatus::Paused || status == PlaybackStatus::Playing)
            return nextAvailableLocalIndex();
    }
    return queueNavigationTarget(navigation_items_, static_cast<std::size_t>(current), direction,
                                 controller_.confirmedSnapshot(),
                                 controller_.seekPreviewActive() || controller_.seekPending());
}
bool PlayerPresenter::canGoPrevious() const noexcept { return navigationTarget(QueueDirection::Previous).has_value(); }
bool PlayerPresenter::canGoNext() const noexcept { return navigationTarget(QueueDirection::Next).has_value(); }
QString PlayerPresenter::assetId() const {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return state_.asset_id;
#endif
    return local_track_session_->hasTrack() ? QStringLiteral("cover-track") : QStringLiteral("cover-placeholder");
}
bool PlayerPresenter::hasMessage() const noexcept {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return state_.message_key.has_value();
#endif
    return !local_track_session_->hasTrack();
}
QString PlayerPresenter::messageKey() const {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return state_.message_key.value_or(QString{});
#endif
    return local_track_session_->hasTrack() ? QString{} : QStringLiteral("empty_title");
}
QString PlayerPresenter::primaryActionId() const {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return state_.primary_action_id;
#endif
    if (hasError())
        return errorActionId();
    if (!local_track_session_->hasTrack())
        return QStringLiteral("open-audio");
    return QStringLiteral("play-pause");
}
bool PlayerPresenter::hasError() const noexcept {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return state_.error_code.has_value();
#endif
    return controller_.confirmedSnapshot().status == PlaybackStatus::Error;
}
QString PlayerPresenter::errorCode() const {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return state_.error_code.value_or(QString{});
#endif
    return hasError() ? localPlaybackErrorCode(controller_.confirmedSnapshot().error) : QString{};
}
QString PlayerPresenter::errorMessageKey() const {
    if (!hasError())
        return {};
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return messageKey();
#endif
    return localPlaybackErrorMessageKey(controller_.confirmedSnapshot().error);
}
QString PlayerPresenter::errorActionId() const {
    if (!hasError())
        return {};
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return primaryActionId();
#endif
    return isAudioOutputError(controller_.confirmedSnapshot().error) ? QStringLiteral("recover-audio-output")
                                                                     : QStringLiteral("open-audio");
}
QString PlayerPresenter::retryingOccurrenceId() const {
    if (!occurrence_retry_.has_value() || local_track_session_ == nullptr ||
        controller_.confirmedSnapshot().status != PlaybackStatus::Loading ||
        occurrence_retry_->generation != local_track_session_->playbackGeneration() ||
        local_queue_.currentOccurrenceId() != std::optional{occurrence_retry_->occurrence_id})
        return {};
    return LocalQueueListModel::occurrenceId(occurrence_retry_->occurrence_id);
}
bool PlayerPresenter::canRetryCurrentOccurrence() const noexcept {
    if (uses_frozen_fixture_ || exiting_ || !hasError())
        return false;
    const auto current = local_queue_.currentIndex();
    return current.has_value() && !local_queue_.items()[*current].available;
}

void PlayerPresenter::setAudioFilesPicker(AudioFilesPicker* picker) {
    if (download_append_in_progress_)
        return;
    if (audio_files_picker_ == picker || file_dialog_cancellation_active_)
        return;
    const QPointer<PlayerPresenter> guard{this};
    cancelAudioFileSelection();
    if (!guard)
        return;
    audio_files_picker_ = picker;
    audio_file_selection_failed_ = false;
    emit stateChanged();
}

bool PlayerPresenter::selectingAudioFiles() const noexcept { return audio_file_selection_.has_value(); }
bool PlayerPresenter::audioFileSelectionFailed() const noexcept { return audio_file_selection_failed_; }

void PlayerPresenter::cancelAudioFileSelection() noexcept {
    if (!audio_file_selection_.has_value())
        return;
    audio_file_selection_.reset();
    if (audio_files_picker_ != nullptr) {
        const QPointer<PlayerPresenter> guard{this};
        const bool already_cancelling = std::exchange(file_dialog_cancellation_active_, true);
        audio_files_picker_->cancel();
        if (guard)
            file_dialog_cancellation_active_ = already_cancelling;
    }
}

bool PlayerPresenter::beginAudioFileSelection(const bool append_only) {
    if (download_append_in_progress_)
        return false;
    if (uses_frozen_fixture_ || exiting_ || file_dialog_cancellation_active_ || audio_files_picker_ == nullptr ||
        selectingAudioFiles() || file_location_request_.has_value())
        return false;
    const auto generation = audio_file_selection_generation_ == std::numeric_limits<std::uint64_t>::max()
                                ? 1
                                : audio_file_selection_generation_ + 1;
    audio_file_selection_generation_ = generation;
    audio_file_selection_ = AudioFileSelectionRequest{generation, append_only};
    audio_file_selection_failed_ = false;
    auto* const picker = audio_files_picker_;
    const QPointer<PlayerPresenter> guard{this};
    emit stateChanged();
    if (!guard || !audio_file_selection_.has_value() || audio_file_selection_->generation != generation ||
        audio_files_picker_ != picker)
        return false;
    bool accepted = false;
    try {
        accepted = picker->pick([guard, generation](AudioFilesSelectionResult result) {
            if (!guard)
                return;
            QMetaObject::invokeMethod(
                guard,
                [guard, generation, result = std::move(result)]() mutable {
                    if (guard)
                        guard->finishAudioFileSelection(generation, std::move(result));
                },
                Qt::QueuedConnection);
        });
    } catch (...) {
        accepted = false;
    }
    if (guard && !accepted)
        finishAudioFileSelection(generation, {AudioFilesSelectionStatus::Failed, {}});
    return accepted;
}

void PlayerPresenter::finishAudioFileSelection(const std::uint64_t generation, AudioFilesSelectionResult result) {
    if (!audio_file_selection_.has_value() || audio_file_selection_->generation != generation)
        return;
    const bool append_only = audio_file_selection_->append_only;
    audio_file_selection_.reset();
    if (exiting_)
        return;
    const QPointer<PlayerPresenter> guard{this};
    bool changed = false;
    if (result.status != AudioFilesSelectionStatus::Cancelled) {
        const bool valid_paths = result.status == AudioFilesSelectionStatus::Selected && !result.source_paths.empty() &&
                                 std::ranges::all_of(result.source_paths, [](const std::filesystem::path& path) {
                                     return path.is_absolute() && !path.filename().empty() &&
                                            path.native().find(std::filesystem::path::value_type{}) ==
                                                std::filesystem::path::string_type::npos;
                                 });
        if (valid_paths)
            changed = append_only ? addSelectedAudio(result.source_paths) : openSelectedAudio(result.source_paths);
        if (!guard || audio_file_selection_generation_ != generation)
            return;
        audio_file_selection_failed_ = !changed;
    }
    emit stateChanged();
    // A state observer may exit, delete us or start another selection before this notification.
    if (guard && !exiting_ && !selectingAudioFiles() && audio_file_selection_generation_ == generation)
        emit audioFileSelectionFinished(changed);
}

void PlayerPresenter::setAudioFileLocator(AudioFileLocator* locator) {
    if (download_append_in_progress_)
        return;
    if (audio_file_locator_ == locator || file_dialog_cancellation_active_)
        return;
    const QPointer<PlayerPresenter> guard{this};
    cancelFileLocation();
    if (!guard)
        return;
    audio_file_locator_ = locator;
    file_location_error_.reset();
    emit stateChanged();
}

bool PlayerPresenter::fileLocationAvailable() const noexcept {
    return !uses_frozen_fixture_ && audio_file_locator_ != nullptr;
}

QString PlayerPresenter::locatingOccurrenceId() const {
    return file_location_request_.has_value() ? LocalQueueListModel::occurrenceId(file_location_request_->occurrence_id)
                                              : QString{};
}

QString PlayerPresenter::fileLocationErrorOccurrenceId() const {
    return file_location_error_.has_value() ? LocalQueueListModel::occurrenceId(*file_location_error_) : QString{};
}

bool PlayerPresenter::canLocateCurrentOccurrence() const noexcept {
    return fileLocationAvailable() && canRetryCurrentOccurrence();
}

void PlayerPresenter::cancelFileLocation() noexcept {
    if (!file_location_request_.has_value())
        return;
    file_location_request_.reset();
    if (audio_file_locator_ != nullptr) {
        const QPointer<PlayerPresenter> guard{this};
        const bool already_cancelling = std::exchange(file_dialog_cancellation_active_, true);
        audio_file_locator_->cancel();
        if (guard)
            file_dialog_cancellation_active_ = already_cancelling;
    }
}
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
const QueueItem* PlayerPresenter::currentTrack() const noexcept {
    if (!state_.current_occurrence_id.has_value())
        return nullptr;
    for (const QueueItem& item : fixture_bundle_->queue) {
        if (item.occurrence_id == *state_.current_occurrence_id)
            return &item;
    }
    return nullptr;
}
const QueueItem* PlayerPresenter::contextTrack() const noexcept {
    if (!state_.context_occurrence_id.has_value())
        return nullptr;
    for (const QueueItem& item : fixture_bundle_->queue) {
        if (item.occurrence_id == *state_.context_occurrence_id)
            return &item;
    }
    return nullptr;
}
const FixtureTrackDetails* PlayerPresenter::currentFixtureTrackDetails() const noexcept {
    const QueueItem* const track = currentTrack();
    return track != nullptr && track->occurrence_id == fixture_bundle_->track_details.occurrence_id
               ? &fixture_bundle_->track_details
               : nullptr;
}
#endif

const TrackMetadata* PlayerPresenter::currentLocalTrackMetadata() const noexcept {
    if (uses_frozen_fixture_ || local_track_session_ == nullptr)
        return nullptr;
    const auto& metadata = local_track_session_->trackMetadata();
    return metadata.has_value() ? &*metadata : nullptr;
}
bool PlayerPresenter::hasCurrentTrack() const noexcept {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return currentTrack() != nullptr;
#endif
    return local_track_session_->hasTrack();
}
QString PlayerPresenter::currentTitle() const {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_) {
        const QueueItem* const track = currentTrack();
        return track != nullptr ? track->title : QString{};
    }
#endif
    return fromUtf8(local_track_session_->displayTitle());
}
QString PlayerPresenter::currentArtist() const {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_) {
        const QueueItem* const track = currentTrack();
        return track != nullptr ? track->artist : QString{};
    }
#endif
    return fromUtf8(local_track_session_->artist());
}
QString PlayerPresenter::currentAlbum() const {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_) {
        const QueueItem* const track = currentTrack();
        return track != nullptr ? track->album : QString{};
    }
#endif
    return fromUtf8(local_track_session_->album());
}
QUrl PlayerPresenter::currentArtworkSource() const { return artwork_cache_->currentSource(); }
bool PlayerPresenter::detailsAvailable() const noexcept {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return currentTrack() != nullptr;
#endif
    return local_track_session_ != nullptr && local_track_session_->hasTrack();
}
bool PlayerPresenter::hasDetailsFileName() const { return !detailsFileName().isEmpty(); }
QString PlayerPresenter::detailsFileName() const {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_) {
        const QueueItem* const track = currentTrack();
        return track != nullptr ? track->file_name : QString{};
    }
#endif
    if (!detailsAvailable())
        return {};
    return displayPath(local_track_session_->sourcePath().filename());
}
bool PlayerPresenter::hasDetailsFilePath() const noexcept {
    return !uses_frozen_fixture_ && detailsAvailable() && !local_track_session_->sourcePath().empty();
}
QString PlayerPresenter::detailsFilePath() const {
    return hasDetailsFilePath() ? displayPath(local_track_session_->sourcePath()) : QString{};
}
bool PlayerPresenter::hasDetailsFileSize() const noexcept {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_) {
        const auto* const details = currentFixtureTrackDetails();
        return details != nullptr && details->size_bytes > 0;
    }
#endif
    const auto* const metadata = currentLocalTrackMetadata();
    return metadata != nullptr && metadata->file_size_bytes > 0;
}
QVariant PlayerPresenter::detailsFileSizeBytes() const {
    if (!hasDetailsFileSize())
        return {};
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return unsignedVariant(static_cast<std::uint64_t>(currentFixtureTrackDetails()->size_bytes));
#endif
    return unsignedVariant(static_cast<std::uint64_t>(currentLocalTrackMetadata()->file_size_bytes));
}
QString PlayerPresenter::detailsFileSizeBytesText() const {
    if (!hasDetailsFileSize())
        return {};
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return unsignedDecimal(static_cast<std::uint64_t>(currentFixtureTrackDetails()->size_bytes));
#endif
    return unsignedDecimal(static_cast<std::uint64_t>(currentLocalTrackMetadata()->file_size_bytes));
}
bool PlayerPresenter::hasDetailsModifiedUtc() const noexcept {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_) {
        const auto* const details = currentFixtureTrackDetails();
        return details != nullptr && !details->modified_utc.isEmpty();
    }
#endif
    const auto* const metadata = currentLocalTrackMetadata();
    return metadata != nullptr && metadata->last_modified_time.has_value() &&
           !modifiedUtc(*metadata->last_modified_time).isEmpty();
}
QString PlayerPresenter::detailsModifiedUtc() const {
    if (!hasDetailsModifiedUtc())
        return {};
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return currentFixtureTrackDetails()->modified_utc;
#endif
    return modifiedUtc(*currentLocalTrackMetadata()->last_modified_time);
}
bool PlayerPresenter::hasDetailsTitle() const { return !detailsTitle().isEmpty(); }
QString PlayerPresenter::detailsTitle() const {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_) {
        const QueueItem* const track = currentTrack();
        return track != nullptr ? track->title : QString{};
    }
#endif
    const auto* const metadata = currentLocalTrackMetadata();
    return metadata != nullptr && metadata->tags.title.has_value() ? fromUtf8(*metadata->tags.title) : QString{};
}
bool PlayerPresenter::hasDetailsArtist() const { return !detailsArtist().isEmpty(); }
QString PlayerPresenter::detailsArtist() const {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_) {
        const QueueItem* const track = currentTrack();
        return track != nullptr ? track->artist : QString{};
    }
#endif
    const auto* const metadata = currentLocalTrackMetadata();
    return metadata != nullptr && metadata->tags.artist.has_value() ? fromUtf8(*metadata->tags.artist) : QString{};
}
bool PlayerPresenter::hasDetailsAlbum() const { return !detailsAlbum().isEmpty(); }
QString PlayerPresenter::detailsAlbum() const {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_) {
        const QueueItem* const track = currentTrack();
        return track != nullptr ? track->album : QString{};
    }
#endif
    const auto* const metadata = currentLocalTrackMetadata();
    return metadata != nullptr && metadata->tags.album.has_value() ? fromUtf8(*metadata->tags.album) : QString{};
}
bool PlayerPresenter::hasDetailsTrackNumber() const noexcept {
    const auto* const metadata = currentLocalTrackMetadata();
    return metadata != nullptr && metadata->tags.track_number.has_value() && *metadata->tags.track_number > 0;
}
QVariant PlayerPresenter::detailsTrackNumber() const {
    return hasDetailsTrackNumber() ? unsignedVariant(*currentLocalTrackMetadata()->tags.track_number) : QVariant{};
}
bool PlayerPresenter::hasDetailsYear() const noexcept {
    const auto* const metadata = currentLocalTrackMetadata();
    return metadata != nullptr && metadata->tags.year.has_value() && *metadata->tags.year > 0;
}
QVariant PlayerPresenter::detailsYear() const {
    return hasDetailsYear() ? unsignedVariant(*currentLocalTrackMetadata()->tags.year) : QVariant{};
}
bool PlayerPresenter::hasDetailsDuration() const noexcept {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_) {
        const QueueItem* const track = currentTrack();
        return track != nullptr && track->duration_ms.has_value() && *track->duration_ms > 0;
    }
#endif
    return localDurationMs().has_value();
}
QVariant PlayerPresenter::detailsDurationMs() const {
    if (!hasDetailsDuration())
        return {};
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return QVariant::fromValue<qint64>(*currentTrack()->duration_ms);
#endif
    return QVariant::fromValue<qint64>(*localDurationMs());
}
bool PlayerPresenter::hasDetailsContainer() const { return !detailsContainer().isEmpty(); }
QString PlayerPresenter::detailsContainer() const {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_) {
        const auto* const details = currentFixtureTrackDetails();
        return details != nullptr ? details->container : QString{};
    }
#endif
    const auto* const metadata = currentLocalTrackMetadata();
    return metadata != nullptr ? fromUtf8(metadata->audio.container) : QString{};
}
bool PlayerPresenter::hasDetailsCodec() const { return !detailsCodec().isEmpty(); }
QString PlayerPresenter::detailsCodec() const {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_) {
        const auto* const details = currentFixtureTrackDetails();
        return details != nullptr ? details->codec : QString{};
    }
#endif
    const auto* const metadata = currentLocalTrackMetadata();
    return metadata != nullptr ? fromUtf8(metadata->audio.codec) : QString{};
}
bool PlayerPresenter::hasDetailsBitrate() const noexcept {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_) {
        const auto* const details = currentFixtureTrackDetails();
        return details != nullptr && details->bitrate_bps > 0;
    }
#endif
    const auto* const metadata = currentLocalTrackMetadata();
    return metadata != nullptr && metadata->audio.bitrate_kbps.has_value() && *metadata->audio.bitrate_kbps > 0;
}
QVariant PlayerPresenter::detailsBitrateBps() const {
    if (!hasDetailsBitrate())
        return {};
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return unsignedVariant(static_cast<std::uint64_t>(currentFixtureTrackDetails()->bitrate_bps));
#endif
    return unsignedVariant(static_cast<std::uint64_t>(*currentLocalTrackMetadata()->audio.bitrate_kbps) * 1'000U);
}
bool PlayerPresenter::hasDetailsSampleRate() const noexcept {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_) {
        const auto* const details = currentFixtureTrackDetails();
        return details != nullptr && details->sample_rate_hz > 0;
    }
#endif
    const auto* const metadata = currentLocalTrackMetadata();
    return metadata != nullptr && metadata->audio.sample_rate_hz.has_value() && *metadata->audio.sample_rate_hz > 0;
}
QVariant PlayerPresenter::detailsSampleRateHz() const {
    if (!hasDetailsSampleRate())
        return {};
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return unsignedVariant(static_cast<std::uint64_t>(currentFixtureTrackDetails()->sample_rate_hz));
#endif
    return unsignedVariant(*currentLocalTrackMetadata()->audio.sample_rate_hz);
}
bool PlayerPresenter::hasDetailsChannels() const noexcept {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_) {
        const auto* const details = currentFixtureTrackDetails();
        return details != nullptr && details->channels > 0;
    }
#endif
    const auto* const metadata = currentLocalTrackMetadata();
    return metadata != nullptr && metadata->audio.channels.has_value() && *metadata->audio.channels > 0;
}
QVariant PlayerPresenter::detailsChannels() const {
    if (!hasDetailsChannels())
        return {};
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return unsignedVariant(static_cast<std::uint64_t>(currentFixtureTrackDetails()->channels));
#endif
    return unsignedVariant(*currentLocalTrackMetadata()->audio.channels);
}
bool PlayerPresenter::hasDetailsBitDepth() const noexcept {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_) {
        const auto* const details = currentFixtureTrackDetails();
        return details != nullptr && details->bit_depth.has_value() && *details->bit_depth > 0;
    }
#endif
    const auto* const metadata = currentLocalTrackMetadata();
    return metadata != nullptr && metadata->audio.bit_depth.has_value() && *metadata->audio.bit_depth > 0;
}
QVariant PlayerPresenter::detailsBitDepth() const {
    if (!hasDetailsBitDepth())
        return {};
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return unsignedVariant(static_cast<std::uint64_t>(*currentFixtureTrackDetails()->bit_depth));
#endif
    return unsignedVariant(*currentLocalTrackMetadata()->audio.bit_depth);
}
bool PlayerPresenter::hasDetailsDiagnostics() const { return !detailsDiagnostics().isEmpty(); }
QVariantList PlayerPresenter::detailsDiagnostics() const {
    QVariantList diagnostics;
    if (!uses_frozen_fixture_) {
        if (const auto* const metadata = currentLocalTrackMetadata(); metadata != nullptr) {
            for (const MetadataDiagnostic& diagnostic : metadata->diagnostics) {
                QVariantMap entry{{QStringLiteral("code"), metadataDiagnosticId(diagnostic.code)},
                                  {QStringLiteral("field"), metadataFieldId(diagnostic.field)}};
                if (diagnostic.observed_bytes > 0) {
                    entry.insert(QStringLiteral("observedBytes"),
                                 unsignedVariant(static_cast<std::uint64_t>(diagnostic.observed_bytes)));
                    entry.insert(QStringLiteral("observedBytesText"),
                                 unsignedDecimal(static_cast<std::uint64_t>(diagnostic.observed_bytes)));
                }
                if (diagnostic.limit_bytes > 0) {
                    entry.insert(QStringLiteral("limitBytes"),
                                 unsignedVariant(static_cast<std::uint64_t>(diagnostic.limit_bytes)));
                    entry.insert(QStringLiteral("limitBytesText"),
                                 unsignedDecimal(static_cast<std::uint64_t>(diagnostic.limit_bytes)));
                }
                diagnostics.push_back(entry);
            }
            for (const ArtworkDiagnostic& diagnostic : metadata->artwork_diagnostics) {
                QVariantMap entry{{QStringLiteral("code"), artworkDiagnosticId(diagnostic.code)}};
                if (diagnostic.encoded_bytes > 0) {
                    entry.insert(QStringLiteral("encodedBytes"),
                                 unsignedVariant(static_cast<std::uint64_t>(diagnostic.encoded_bytes)));
                    entry.insert(QStringLiteral("encodedBytesText"),
                                 unsignedDecimal(static_cast<std::uint64_t>(diagnostic.encoded_bytes)));
                }
                if (diagnostic.width_pixels > 0)
                    entry.insert(QStringLiteral("widthPixels"), unsignedVariant(diagnostic.width_pixels));
                if (diagnostic.height_pixels > 0)
                    entry.insert(QStringLiteral("heightPixels"), unsignedVariant(diagnostic.height_pixels));
                diagnostics.push_back(entry);
            }
        }
        if (local_track_session_ != nullptr) {
            switch (local_track_session_->metadataStatus()) {
            case LocalTrackMetadataStatus::Failed: {
                QVariantMap entry{{QStringLiteral("code"), QStringLiteral("metadata_read_failed")}};
                if (const auto error = local_track_session_->metadataErrorCode(); error.has_value())
                    entry.insert(QStringLiteral("errorCode"), metadataReadErrorId(*error));
                diagnostics.push_back(entry);
                break;
            }
            case LocalTrackMetadataStatus::Cancelled:
                diagnostics.push_back(QVariantMap{{QStringLiteral("code"), QStringLiteral("metadata_read_cancelled")}});
                break;
            case LocalTrackMetadataStatus::Expired:
                diagnostics.push_back(QVariantMap{{QStringLiteral("code"), QStringLiteral("metadata_read_expired")}});
                break;
            case LocalTrackMetadataStatus::Idle:
            case LocalTrackMetadataStatus::Pending:
            case LocalTrackMetadataStatus::Ready:
                break;
            }
        }
    }
    if (hasError()) {
        const QString code = errorCode();
        QVariantMap entry{{QStringLiteral("code"), QStringLiteral("playback_error")}};
        if (!code.isEmpty())
            entry.insert(QStringLiteral("errorCode"), code);
        diagnostics.push_back(entry);
    }
    return diagnostics;
}
bool PlayerPresenter::hasContextTrack() const noexcept {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return contextTrack() != nullptr;
#endif
    return hasError() && local_track_session_->hasTrack();
}
QString PlayerPresenter::contextTitle() const {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_) {
        const QueueItem* const track = contextTrack();
        return track != nullptr ? track->title : QString{};
    }
#endif
    return hasContextTrack() ? currentTitle() : QString{};
}
QString PlayerPresenter::contextArtist() const {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_) {
        const QueueItem* const track = contextTrack();
        return track != nullptr ? track->artist : QString{};
    }
#endif
    return hasContextTrack() ? currentArtist() : QString{};
}
QString PlayerPresenter::contextAlbum() const {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_) {
        const QueueItem* const track = contextTrack();
        return track != nullptr ? track->album : QString{};
    }
#endif
    return hasContextTrack() ? currentAlbum() : QString{};
}
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
QueueListModel* PlayerPresenter::queueModel() noexcept { return &queue_model_; }
#endif
QAbstractItemModel* PlayerPresenter::queueModelObject() noexcept {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return static_cast<QAbstractItemModel*>(&queue_model_);
#endif
    return static_cast<QAbstractItemModel*>(&local_queue_model_);
}
PlayerPresenter::SourceImportState PlayerPresenter::sourceImportState() const noexcept { return source_import_state_; }
bool PlayerPresenter::sourceImportActive() const noexcept {
    return source_import_state_ == SourceImportState::Discovering ||
           source_import_state_ == SourceImportState::Cancelling;
}
qulonglong PlayerPresenter::sourceImportAcceptedCount() const noexcept { return source_import_accepted_count_; }
qulonglong PlayerPresenter::sourceImportIssueCount() const noexcept { return source_import_issue_count_; }
qulonglong PlayerPresenter::sourceImportInspectedCount() const noexcept { return source_import_inspected_count_; }
void PlayerPresenter::setSourceExitDrainedProbe(std::function<bool()> probe) {
    source_exit_drained_probe_ = std::move(probe);
}

void PlayerPresenter::setPlaylistExitPreparationProbe(std::function<ExitPreparationState()> probe) {
    playlist_exit_preparation_probe_ = std::move(probe);
}

void PlayerPresenter::cancelPendingPlaylistExit() {
    if (playlist_exit_deadline_.has_value()) {
        playlist_exit_deadline_.reset();
        reconcileTimer();
    }
}

bool PlayerPresenter::setExitPreparationCallbacks(ExitPreparationCallbacks callbacks) {
    if (QThread::currentThread() != thread() || exiting_ || exit_preparation_started_ ||
        exit_preparation_call_in_progress_ || static_cast<bool>(callbacks.begin) != static_cast<bool>(callbacks.poll))
        return false;
    if (callbacks.begin && exit_preparation_callbacks_.begin)
        return false;
    exit_preparation_callbacks_ = std::move(callbacks);
    return true;
}
const Preferences& PlayerPresenter::preferences() const noexcept { return controller_.preferences(); }
double PlayerPresenter::volume() const noexcept { return controller_.preferences().volume; }
bool PlayerPresenter::muted() const noexcept { return controller_.preferences().muted; }
bool PlayerPresenter::audioOutputSelectionSupported() const noexcept { return audio_output_selection_ != nullptr; }
QVariantList PlayerPresenter::audioOutputDevices() const {
    QVariantList devices;
    if (audio_output_selection_ != nullptr) {
        for (const auto& device : audio_output_selection_->audioOutputs()) {
            devices.push_back(QVariantMap{{QStringLiteral("id"), QString::fromStdString(device.id)},
                                          {QStringLiteral("label"), QString::fromStdString(device.description)}});
        }
    }
    return devices;
}
QString PlayerPresenter::selectedAudioOutputId() const {
    return audio_output_selection_ != nullptr ? QString::fromStdString(audio_output_selection_->selectedAudioOutputId())
                                              : QString{};
}
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
bool PlayerPresenter::repeatQueueEnabled() const noexcept { return preferences().repeat_queue_enabled; }
bool PlayerPresenter::shuffleQueueEnabled() const noexcept { return preferences().shuffle_queue_enabled; }
void PlayerPresenter::initializeShuffleRound() {
    shuffle_round_visited_.clear();
    if (!shuffleQueueEnabled() || uses_frozen_fixture_)
        return;
    if (const auto current = local_queue_.currentIndex(); current.has_value()) {
        for (std::size_t index = 0; index <= *current; ++index)
            shuffle_round_visited_.insert(local_queue_.items()[index].occurrence_id.value);
    }
}
bool PlayerPresenter::toggleShuffleQueue() {
    if (download_append_in_progress_)
        return false;
    if (exiting_)
        return false;
    const bool enabled = !shuffleQueueEnabled();
    controller_.setShuffleQueueEnabled(enabled);
    initializeShuffleRound();
    const QPointer<PlayerPresenter> guard{this};
    const auto generation = backend_.snapshot().generation;
    preferenceDidChange();
    if (guard && !exiting_ && enabled && shuffleQueueEnabled() && backend_.snapshot().generation == generation)
        static_cast<void>(shuffleQueue());
    return true;
}
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
void PlayerPresenter::setQueueShuffleSeedForTest(const std::uint32_t seed) noexcept {
    queue_shuffle_random_.seed(seed);
}
#endif
bool PlayerPresenter::shuffleQueue() {
    if (download_append_in_progress_)
        return false;
    if (uses_frozen_fixture_ || exiting_)
        return false;
    const auto current = local_queue_.currentIndex();
    const std::size_t first = current.has_value() ? *current + 1 : 0;
    if (local_queue_.size() - first < 2)
        return false;
    std::vector<LocalQueueOccurrenceId> order;
    for (const auto& item : local_queue_.items().subspan(first))
        order.push_back(item.occurrence_id);
    const auto previous_order = order;
    std::shuffle(order.begin(), order.end(), queue_shuffle_random_);
    // A successful action must have an observable effect even if the random
    // permutation happened to reproduce the original order.
    if (order == previous_order)
        std::rotate(order.begin(), std::next(order.begin()), order.end());

    const QPointer<PlayerPresenter> guard{this};
    const auto generation = backend_.snapshot().generation;
    const bool shuffle_enabled = shuffleQueueEnabled();
    for (std::size_t offset = 0; offset < order.size(); ++offset) {
        const auto source = localOccurrenceIndex(order[offset]);
        const std::size_t destination = first + offset;
        if (!source.has_value())
            throw std::logic_error{"shuffle lost a stable queue occurrence"};
        if (*source == destination)
            continue;
        if (!local_queue_.move(order[offset], destination))
            throw std::logic_error{"a shuffled local occurrence could not be moved"};
        navigation_items_ = navigationItems(local_queue_);
        sessionStructureDidChange();
        const auto expected_queue = local_queue_.snapshot();
        if (!local_queue_model_.move(*source, destination))
            throw std::logic_error{"a shuffled local occurrence could not be projected"};
        // Model observers may synchronously select/remove a track or exit. The
        // remaining permutation no longer has authority over that newer state.
        if (!guard || exiting_ || shuffleQueueEnabled() != shuffle_enabled ||
            backend_.snapshot().generation != generation || local_queue_.snapshot() != expected_queue)
            return true;
    }
    const auto expected_queue = local_queue_.snapshot();
    emit stateChanged();
    if (guard && !exiting_ && backend_.snapshot().generation == generation && local_queue_.snapshot() == expected_queue)
        emit playbackChanged();
    return true;
}
bool PlayerPresenter::toggleRepeatQueue() {
    if (download_append_in_progress_)
        return false;
    if (exiting_)
        return false;
    controller_.setRepeatQueueEnabled(!repeatQueueEnabled());
    const QPointer<PlayerPresenter> guard{this};
    preferenceDidChange();
    if (guard && !exiting_)
        emit playbackChanged();
    return true;
}
bool PlayerPresenter::trayAvailable() const noexcept { return tray_available_; }
bool PlayerPresenter::defaultAppsSettingsSupported() const noexcept { return default_apps_settings_supported_; }
bool PlayerPresenter::defaultAppsSettingsAvailable() const noexcept { return default_apps_settings_available_; }
PlayerPresenter::DefaultAppsSettingsState PlayerPresenter::defaultAppsSettingsState() const noexcept {
    return default_apps_settings_state_;
}
bool PlayerPresenter::exiting() const noexcept { return exiting_; }
bool PlayerPresenter::exitCommitted() const noexcept { return exit_committed_; }
PlayerPresenter::WindowNotice PlayerPresenter::windowNotice() const noexcept { return window_notice_; }
bool PlayerPresenter::trayNoticeEnabled() const noexcept { return tray_notice_enabled_; }
bool PlayerPresenter::usesFrozenFixture() const noexcept { return uses_frozen_fixture_; }
bool PlayerPresenter::openAudio() {
    if (download_append_in_progress_)
        return false;
    if (exiting_ || file_dialog_cancellation_active_ || selectingAudioFiles() || file_location_request_.has_value())
        return false;
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_) {
        return loadFrozenQueue();
    }
#endif

    if (audio_files_picker_ != nullptr)
        return beginAudioFileSelection(false);
    return openSelectedAudio(audio_file_picker_());
}

bool PlayerPresenter::openSelectedAudio(const std::vector<std::filesystem::path>& selected_paths) {
    if (download_append_in_progress_)
        return false;
    if (selected_paths.empty())
        return false;
    auto candidate = local_queue_;
    const std::size_t first_inserted = candidate.size();
    const bool queue_was_empty = candidate.empty();
    if (!candidate.append(selected_paths)) {
        if (!sourceImportActive())
            rejectSourceImport(static_cast<std::uint64_t>(selected_paths.size()));
        return false;
    }
    if (!local_track_session_->open(candidate.items()[first_inserted].source_path, LocalTrackStartIntent::Autoplay)) {
        if (!sourceImportActive())
            rejectSourceImport(static_cast<std::uint64_t>(selected_paths.size()));
        return false;
    }
    if (!candidate.select(first_inserted))
        throw std::logic_error{"an accepted opened local occurrence could not be selected"};

    local_queue_ = std::move(candidate);
    local_recovery_intent_.reset();
    navigation_items_ = navigationItems(local_queue_);
    const QPointer<PlayerPresenter> guard{this};
    const bool model_updated = queue_was_empty
                                   ? local_queue_model_.replace(local_queue_)
                                   : local_queue_model_.append(local_queue_.items().subspan(first_inserted));
    if (!guard)
        return true;
    if (!model_updated)
        throw std::logic_error{"accepted opened local queue could not be projected"};
    artwork_cache_->beginGeneration(local_track_session_->metadataGeneration());
    controller_.applyBackendSnapshot(backend_.snapshot());
    sessionStructureDidChange();
    reconcileTimer();
    emit stateChanged();
    if (guard)
        emit playbackChanged();
    return true;
}
bool PlayerPresenter::addAudio() {
    if (download_append_in_progress_)
        return false;
    if (uses_frozen_fixture_ || exiting_ || file_dialog_cancellation_active_ || selectingAudioFiles() ||
        file_location_request_.has_value())
        return false;

    if (audio_files_picker_ != nullptr)
        return beginAudioFileSelection(true);
    return addSelectedAudio(audio_file_picker_());
}

bool PlayerPresenter::addSelectedAudio(const std::vector<std::filesystem::path>& selected_paths) {
    if (download_append_in_progress_)
        return false;
    if (selected_paths.empty())
        return false;
    auto candidate = local_queue_;
    const std::size_t first_inserted = candidate.size();
    if (!candidate.append(selected_paths)) {
        if (!sourceImportActive())
            rejectSourceImport(static_cast<std::uint64_t>(selected_paths.size()));
        return false;
    }

    const bool needs_current_track = !local_track_session_->hasTrack();
    if (needs_current_track) {
        if (!local_track_session_->open(candidate.items()[first_inserted].source_path,
                                        LocalTrackStartIntent::Autoplay)) {
            if (!sourceImportActive())
                rejectSourceImport(static_cast<std::uint64_t>(selected_paths.size()));
            return false;
        }
        if (!candidate.select(first_inserted))
            throw std::logic_error{"an accepted appended local occurrence could not be selected"};
    }

    local_queue_ = std::move(candidate);
    navigation_items_ = navigationItems(local_queue_);
    const QPointer<PlayerPresenter> guard{this};
    if (!local_queue_model_.append(local_queue_.items().subspan(first_inserted)))
        throw std::logic_error{"accepted local queue append could not be projected"};
    if (!guard)
        return true;
    if (needs_current_track) {
        local_recovery_intent_.reset();
        artwork_cache_->beginGeneration(local_track_session_->metadataGeneration());
        controller_.applyBackendSnapshot(backend_.snapshot());
    }
    sessionStructureDidChange();
    reconcileTimer();
    emit stateChanged();
    if (!guard)
        return true;
    emit playbackChanged();
    if (guard && needs_current_track)
        emit queueFocusRequested(static_cast<int>(first_inserted));
    return true;
}
bool PlayerPresenter::appendPublishedAudio(const std::filesystem::path& path, bool& committed) {
    committed = false;
    if (QThread::currentThread() != thread() || download_import_call_in_progress_)
        return false;
    return appendPublishedAudioImpl(path, committed, nullptr);
}
LocalFileQueueSnapshot PlayerPresenter::playlistQueueSnapshot() const { return local_queue_.snapshot(); }
QString PlayerPresenter::queuedLyricsSource(const QString& occurrence_id) const {
    if (uses_frozen_fixture_ || exiting_)
        return {};
    const auto index = localOccurrenceIndex(occurrence_id);
    if (!index || !local_queue_.items()[*index].available)
        return {};
    return displayPath(local_queue_.items()[*index].source_path);
}
bool PlayerPresenter::appendPlaylistSources(std::vector<std::filesystem::path> paths) {
    if (QThread::currentThread() != thread() || download_import_call_in_progress_)
        return false;
    for (const auto& path : paths) {
#ifdef Q_OS_WIN
        const auto text = QString::fromStdWString(path.native());
#else
        const auto text = QString::fromUtf8(path.native().data(), static_cast<qsizetype>(path.native().size()));
#endif
        if (!isAbsoluteFilesystemSourcePath(text))
            return false;
    }
    bool committed{};
    try {
        return appendLocalSourcesWithoutPlayback(std::move(paths), committed, nullptr);
    } catch (...) {
        // A notification can fail after the queue transaction. Preserve the
        // committed acknowledgement so the caller cannot replay that batch.
        return committed;
    }
}
bool PlayerPresenter::appendPublishedAudioImpl(const std::filesystem::path& path, bool& committed,
                                               const std::string* download_job_id) {
    return appendLocalSourcesWithoutPlayback({path}, committed, download_job_id);
}
bool PlayerPresenter::appendLocalSourcesWithoutPlayback(std::vector<std::filesystem::path> paths, bool& committed,
                                                        const std::string* download_job_id) {
    committed = false;
    if (QThread::currentThread() != thread() || uses_frozen_fixture_ || exiting_ || download_append_in_progress_ ||
        file_dialog_cancellation_active_ || sourceImportActive() || selectingAudioFiles() ||
        file_location_request_.has_value() || paths.empty() ||
        std::any_of(paths.begin(), paths.end(), [](const auto& path) { return path.empty(); }))
        return false;

    auto candidate = local_queue_;
    const auto first_inserted = candidate.size();
    if (!candidate.append(paths))
        return false;
    auto navigation = navigationItems(candidate);
    auto receipts = download_import_receipts_;
    auto revisions = download_import_revisions_;
    if (download_job_id != nullptr) {
        if (!download_import_persistence_ready_ || session_autosave_ == nullptr)
            return false;
        receipts.push_back({.job_id = *download_job_id, .source_path = paths.front()});
        if (!isValidDownloadImportReceipts(receipts))
            return false;
        revisions.push_back(std::numeric_limits<SessionRevision>::max());
    }
    const QPointer<PlayerPresenter> guard{this};
    download_append_in_progress_ = true;
    const auto release = qScopeGuard([guard] {
        if (guard)
            guard->finishPublishedAudioAppend();
    });
    // Projection preparation can fail without committing the domain/receipt.
    // The commit itself only moves prepared state, before any Qt notification.
    const bool projected =
        local_queue_model_.appendWithCommit(candidate.items().subspan(first_inserted), [&]() noexcept {
            local_queue_ = std::move(candidate);
            navigation_items_ = std::move(navigation);
            download_import_receipts_ = std::move(receipts);
            download_import_revisions_ = std::move(revisions);
            committed = true;
        });
    if (!projected)
        return false;
    if (!guard)
        return true;
    sessionStructureDidChange();
    emit stateChanged();
    if (guard)
        emit playbackChanged();
    return true;
}
void PlayerPresenter::finishPublishedAudioAppend() {
    download_append_in_progress_ = false;
    const auto event = std::exchange(deferred_playback_event_, std::nullopt);
    const auto power = std::exchange(deferred_download_power_event_, std::nullopt);
    const bool suspended = std::exchange(deferred_download_suspend_, false);
    reconcileTimer();
    if (!event.has_value() && !power.has_value())
        return;
    const QPointer<PlayerPresenter> guard{this};
    // A nested event loop must not mutate the queue while Qt is inserting rows,
    // nor from the scope guard before the import adapter has returned.
    QMetaObject::invokeMethod(
        this,
        [guard, event, power, suspended] {
            if (guard && suspended)
                static_cast<void>(guard->handleSystemPowerEvent(SystemPowerEvent::Suspending));
            if (guard && power == std::optional{SystemPowerEvent::Resumed})
                static_cast<void>(guard->handleSystemPowerEvent(SystemPowerEvent::Resumed));
            if (guard && event.has_value())
                guard->handlePlaybackEvent(*event);
        },
        Qt::QueuedConnection);
}
bool PlayerPresenter::openSourcePaths(std::vector<std::filesystem::path> source_paths,
                                      const std::size_t insertion_index) {
    return beginSourceImport(std::move(source_paths), insertion_index, 0, true);
}

bool PlayerPresenter::importDroppedUrls(const QList<QUrl>& urls, const int insertion_index) {
    if (download_append_in_progress_)
        return false;
    if (uses_frozen_fixture_ || exiting_ || source_discovery_ == nullptr || sourceImportActive())
        return false;
    if (insertion_index < 0 || static_cast<std::size_t>(insertion_index) > local_queue_.size())
        return beginSourceImport({}, std::nullopt, 0);

    std::vector<std::filesystem::path> roots;
    roots.reserve(static_cast<std::size_t>(urls.size()));
    std::uint64_t boundary_issue_count = 0;
    for (const QUrl& url : urls) {
        auto source_path = localFilesystemPathFromUrl(url);
        if (!source_path.has_value()) {
            ++boundary_issue_count;
            continue;
        }
        roots.push_back(std::move(*source_path));
    }

    return beginSourceImport(std::move(roots), static_cast<std::size_t>(insertion_index), boundary_issue_count);
}

bool PlayerPresenter::beginSourceImport(std::vector<std::filesystem::path> roots,
                                        const std::optional<std::size_t> insertion_index,
                                        const std::uint64_t boundary_issue_count,
                                        const bool allow_during_source_exit_drain) {
    if (download_append_in_progress_)
        return false;
    if (uses_frozen_fixture_ || (exiting_ && !(allow_during_source_exit_drain && source_exit_drain_active_)) ||
        source_discovery_ == nullptr || sourceImportActive())
        return false;
    source_import_occurrences_.clear();
    source_import_issue_occurrences_.clear();
    source_import_autoplay_enabled_ = false;
    source_import_needs_autoplay_ = false;
    if (!insertion_index.has_value() || *insertion_index > local_queue_.size()) {
        rejectSourceImport(1);
        return false;
    }
    if (roots.empty()) {
        rejectSourceImport(std::max<std::uint64_t>(1, boundary_issue_count));
        return false;
    }

    const DiscoveryGeneration generation = nextDiscoveryGeneration(source_import_generation_);
    if (source_discovery_->start({generation, std::move(roots)}) != DiscoveryCommandResult::Accepted) {
        rejectSourceImport(boundary_issue_count + 1);
        return false;
    }

    const std::size_t index = *insertion_index;
    source_import_generation_ = generation;
    source_import_sequence_ = 0;
    source_import_accepted_count_ = 0;
    source_import_issue_count_ = boundary_issue_count;
    source_import_discovery_issue_count_ = 0;
    source_import_inspected_count_ = 0;
    source_import_state_ = SourceImportState::Discovering;
    source_import_before_occurrence_ =
        index < local_queue_.size() ? std::optional{local_queue_.items()[index].occurrence_id} : std::nullopt;
    source_import_predecessor_occurrence_ =
        index > 0 ? std::optional{local_queue_.items()[index - 1].occurrence_id} : std::nullopt;
    source_import_last_inserted_occurrence_.reset();
    source_import_autoplay_enabled_ = !local_track_session_->hasTrack();
    source_import_needs_autoplay_ = source_import_autoplay_enabled_;
    reconcileTimer();
    emit sourceImportChanged();
    return true;
}

bool PlayerPresenter::cancelSourceImport() {
    if (exiting_)
        return false;
    if (source_import_state_ == SourceImportState::Cancelling)
        return true;
    if (source_import_state_ != SourceImportState::Discovering || source_discovery_ == nullptr ||
        source_discovery_->cancel(source_import_generation_) != DiscoveryCommandResult::Accepted) {
        return false;
    }
    source_import_state_ = SourceImportState::Cancelling;
    reconcileTimer();
    emit sourceImportChanged();
    return true;
}

bool PlayerPresenter::dismissSourceImportStatus() {
    if (exiting_ || source_import_state_ == SourceImportState::Idle || sourceImportActive())
        return false;
    source_import_state_ = SourceImportState::Idle;
    source_import_sequence_ = 0;
    source_import_accepted_count_ = 0;
    source_import_issue_count_ = 0;
    source_import_discovery_issue_count_ = 0;
    source_import_inspected_count_ = 0;
    source_import_before_occurrence_.reset();
    source_import_predecessor_occurrence_.reset();
    source_import_last_inserted_occurrence_.reset();
    source_import_occurrences_.clear();
    source_import_autoplay_enabled_ = false;
    source_import_needs_autoplay_ = false;
    emit sourceImportChanged();
    return true;
}
bool PlayerPresenter::playQueueOccurrence(const QString& occurrence_id) {
    if (download_append_in_progress_)
        return false;
    if (uses_frozen_fixture_ || exiting_)
        return false;
    const auto target = localOccurrenceIndex(occurrence_id);
    if (!target.has_value() || !local_queue_.items()[*target].available)
        return false;

    const auto current = local_queue_.currentIndex();
    if (current == target) {
        switch (controller_.effectivePlaybackStatus()) {
        case PlaybackStatus::Loading: {
            if (!local_track_session_->promotePendingStartToAutoplay())
                return false;
            cancelRestoredPositionForExplicitTransport();
            return true;
        }
        case PlaybackStatus::Ready:
        case PlaybackStatus::Paused:
            return togglePlayback();
        case PlaybackStatus::Playing:
            cancelRestoredPositionForExplicitTransport();
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
    sessionStructureDidChange();
    reconcileTimer();
    emit stateChanged();
    if (controller_.confirmedSnapshot() != before)
        emit playbackChanged();
    return true;
}
bool PlayerPresenter::retryQueueOccurrence(const QString& occurrence_id) {
    if (download_append_in_progress_)
        return false;
    if (uses_frozen_fixture_ || exiting_ || !retryingOccurrenceId().isEmpty())
        return false;
    const auto target = localOccurrenceIndex(occurrence_id);
    if (!target.has_value() || local_queue_.items()[*target].available)
        return false;

    const auto id = local_queue_.items()[*target].occurrence_id;
    auto pending_restore = restored_position_;
    if (local_queue_.currentOccurrenceId() != std::optional{id} ||
        (pending_restore && pending_restore->generation != local_track_session_->playbackGeneration()))
        pending_restore.reset();
    const auto intent = pending_restore ? LocalTrackStartIntent::RestorePaused : LocalTrackStartIntent::Autoplay;
    if (!local_track_session_->open(local_queue_.items()[*target].source_path, intent))
        return false;
    if (!local_queue_.selectUnavailableForRetry(*target))
        throw std::logic_error{"an accepted unavailable occurrence retry could not be selected"};
    const auto generation = local_track_session_->playbackGeneration();
    occurrence_retry_ = OccurrenceRetry{id, generation};
    if (pending_restore) {
        pending_restore->generation = generation;
        pending_restore->seek_requested = false;
        restored_position_ = pending_restore;
        restored_eof_generation_.reset();
    }
    if (file_location_error_ == id)
        file_location_error_.reset();
    source_import_needs_autoplay_ = false;
    source_import_autoplay_enabled_ = false;
    local_recovery_intent_.reset();
    artwork_cache_->beginGeneration(local_track_session_->metadataGeneration());
    controller_.applyBackendSnapshot(backend_.snapshot());
    sessionStructureDidChange();
    reconcileTimer();
    emit stateChanged();
    emit playbackChanged();
    if (backend_.snapshot().status != PlaybackStatus::Loading)
        handlePlaybackEvent({PlaybackEventType::SnapshotChanged, backend_.snapshot()});
    return true;
}
bool PlayerPresenter::locateQueueOccurrence(const QString& occurrence_id) {
    if (download_append_in_progress_)
        return false;
    if (!fileLocationAvailable() || exiting_ || file_dialog_cancellation_active_ || selectingAudioFiles() ||
        file_location_request_.has_value() || !retryingOccurrenceId().isEmpty())
        return false;
    const auto target = localOccurrenceIndex(occurrence_id);
    if (!target.has_value() || local_queue_.items()[*target].available)
        return false;
    const auto& item = local_queue_.items()[*target];
    const auto generation =
        file_location_generation_ == std::numeric_limits<std::uint64_t>::max() ? 1 : file_location_generation_ + 1;
    file_location_generation_ = generation;
    file_location_request_ = FileLocationRequest{generation, item.occurrence_id, item.source_path};
    file_location_error_.reset();
    const auto source = item.source_path;
    auto* const locator = audio_file_locator_;
    const QPointer<PlayerPresenter> guard{this};
    emit stateChanged();
    if (!guard || !file_location_request_.has_value() || file_location_request_->generation != generation)
        return false;
    bool accepted = false;
    try {
        accepted = locator->locate(source, [guard, generation](AudioFileLocationResult result) {
            if (!guard)
                return;
            // Even an inline completion must not reenter the request's initiating call.
            QMetaObject::invokeMethod(
                guard,
                [guard, generation, result = std::move(result)]() mutable {
                    if (guard)
                        guard->finishFileLocation(generation, std::move(result));
                },
                Qt::QueuedConnection);
        });
    } catch (...) {
        accepted = false;
    }
    if (guard && !accepted)
        finishFileLocation(generation, {AudioFileLocationStatus::Failed, {}});
    return accepted;
}

void PlayerPresenter::finishFileLocation(const std::uint64_t request_generation, AudioFileLocationResult result) {
    if (!file_location_request_.has_value() || file_location_request_->generation != request_generation)
        return;
    const auto request = std::move(*file_location_request_);
    file_location_request_.reset();
    const QString id = LocalQueueListModel::occurrenceId(request.occurrence_id);
    const QPointer<PlayerPresenter> guard{this};
    const auto finish = [guard, id](const bool changed) {
        if (!guard)
            return;
        emit guard->stateChanged();
        if (guard)
            emit guard->fileLocationFinished(id, changed);
    };
    if (exiting_)
        return;
    const auto target = localOccurrenceIndex(request.occurrence_id);
    if (!target.has_value() || local_queue_.items()[*target].available ||
        local_queue_.items()[*target].source_path != request.original_path || !retryingOccurrenceId().isEmpty()) {
        finish(false);
        return;
    }
    if (result.status == AudioFileLocationStatus::Cancelled) {
        finish(false);
        return;
    }
    if (result.status != AudioFileLocationStatus::Selected || result.source_path.empty() ||
        !result.source_path.is_absolute() || result.source_path.filename().empty() ||
        result.source_path.native().find(std::filesystem::path::value_type{}) !=
            std::filesystem::path::string_type::npos) {
        file_location_error_ = request.occurrence_id;
        finish(false);
        return;
    }
    auto candidate = local_queue_;
    if (!candidate.replaceUnavailableSource(request.occurrence_id, result.source_path)) {
        finish(false);
        return;
    }
    const bool current = local_queue_.currentOccurrenceId() == std::optional{request.occurrence_id};
    if (current) {
        if (!hasError() || local_track_session_->sourcePath() != request.original_path) {
            finish(false);
            return;
        }
        const auto position = currentSessionPosition();
        const bool restore_volume = hasSourceVolumeRecovery();
        if (!local_track_session_->open(result.source_path, LocalTrackStartIntent::RestorePaused)) {
            file_location_error_ = request.occurrence_id;
            finish(false);
            return;
        }
        const auto generation = local_track_session_->playbackGeneration();
        restored_position_ =
            RestoredPosition{.generation = generation, .target_ms = position, .source_volume_recovery = restore_volume};
        restored_eof_generation_.reset();
        occurrence_retry_ = OccurrenceRetry{request.occurrence_id, generation};
        source_import_needs_autoplay_ = false;
        source_import_autoplay_enabled_ = false;
        local_recovery_intent_.reset();
        artwork_cache_->beginGeneration(local_track_session_->metadataGeneration());
        controller_.applyBackendSnapshot(backend_.snapshot());
    }
    // Selecting a replacement invalidates the old diagnostic, but does not prove decoding.
    // A non-current occurrence remains pending verification without touching the playing track.
    local_queue_ = std::move(candidate);
    navigation_items_ = navigationItems(local_queue_);
    const LocalFileQueueItem updated = local_queue_.items()[*target];
    if (!local_queue_model_.replaceUnavailableSource(updated))
        throw std::logic_error{"a relocated occurrence could not be projected"};
    if (!guard)
        return;
    sessionStructureDidChange();
    reconcileTimer();
    if (current) {
        emit playbackChanged();
        if (!guard)
            return;
        if (backend_.snapshot().status != PlaybackStatus::Loading)
            handlePlaybackEvent({PlaybackEventType::SnapshotChanged, backend_.snapshot()});
    }
    finish(true);
}

bool PlayerPresenter::moveQueueOccurrence(const QString& occurrence_id, const int destination_index) {
    if (download_append_in_progress_)
        return false;
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
    sessionStructureDidChange();
    emit stateChanged();
    emit playbackChanged();
    emit queueFocusRequested(destination_index);
    return true;
}
bool PlayerPresenter::removeQueueOccurrence(const QString& occurrence_id) {
    if (download_append_in_progress_)
        return false;
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
            switch (controller_.effectivePlaybackStatus()) {
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
    shuffle_round_visited_.erase(removed_occurrence.value);
    if (!local_queue_model_.remove(*source_index))
        throw std::logic_error{"accepted local queue removal could not be projected"};
    navigation_items_ = navigationItems(local_queue_);
    controller_.applyBackendSnapshot(backend_.snapshot());
    sessionStructureDidChange();
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
    if (download_append_in_progress_)
        return false;
    if (uses_frozen_fixture_ || exiting_ || (sourceImportActive() && !cancelSourceImport()) || local_queue_.empty() ||
        !local_track_session_->unload()) {
        return false;
    }
    if (!local_queue_.clear())
        throw std::logic_error{"a non-empty local queue could not be cleared"};
    shuffle_round_visited_.clear();
    if (!local_queue_model_.clear())
        throw std::logic_error{"an accepted local queue clear could not be projected"};
    navigation_items_.clear();
    local_recovery_intent_.reset();
    artwork_cache_->beginGeneration(MetadataGeneration{});
    controller_.applyBackendSnapshot(backend_.snapshot());
    sessionStructureDidChange();
    reconcileTimer();
    emit stateChanged();
    emit playbackChanged();
    emit queueFocusRequested(-1);
    return true;
}
void PlayerPresenter::handlePlaybackEvent(PlaybackEvent event) {
    if (uses_frozen_fixture_ || exit_committed_)
        return;
    const bool volume_removed = isSourceVolumeRemoval(event);
    // A volume removal already observed by the backend takes precedence over
    // queued decoder errors or completion from the same generation.
    if (!volume_removed &&
        (event.type == PlaybackEventType::Ended || event.snapshot.status == PlaybackStatus::Error ||
         event.snapshot.status == PlaybackStatus::Empty) &&
        backend_.sourceVolumeRemovalObserved(event.snapshot.generation))
        return;
    if (exiting_ || download_append_in_progress_) {
        if (local_track_session_ != nullptr &&
            event.snapshot.generation == local_track_session_->playbackGeneration() &&
            (!deferred_playback_event_ || volume_removed ||
             (!isSourceVolumeRemoval(*deferred_playback_event_) &&
              (deferred_playback_event_->type != PlaybackEventType::Ended ||
               event.type == PlaybackEventType::Ended)))) {
            deferred_playback_event_ = event;
        }
        return;
    }
    const PlaybackSnapshot before = controller_.confirmedSnapshot();
    const bool seek_was_pending = controller_.seekPending();
    const bool restoring_session =
        restored_position_.has_value() && restored_position_->generation == event.snapshot.generation;
    const bool suppress_restored_end =
        event.type == PlaybackEventType::Ended && restored_eof_generation_ == std::optional{event.snapshot.generation};
    const auto result = local_track_session_->applyPlaybackEvent(event);
    if (result == LocalTrackPlaybackEventResult::Ignored)
        return;

    if (volume_removed) {
        // The decoder may already have dropped its time when the volume disappeared.
        if (before.generation == event.snapshot.generation) {
            event.snapshot.position_ms = before.position_ms;
            event.snapshot.duration_ms = before.duration_ms;
        }
        event.snapshot.seekable = false;
        const auto position = restoring_session ? restored_position_->target_ms : event.snapshot.position_ms;
        restored_position_ = RestoredPosition{.generation = event.snapshot.generation,
                                              .target_ms = std::max(position, std::int64_t{0}),
                                              .source_volume_recovery = true};
        restored_eof_generation_.reset();
        source_import_needs_autoplay_ = false;
        source_import_autoplay_enabled_ = false;
    }
    const bool retry_event = occurrence_retry_.has_value() &&
                             occurrence_retry_->generation == event.snapshot.generation &&
                             local_queue_.currentOccurrenceId() == std::optional{occurrence_retry_->occurrence_id};
    controller_.applyBackendSnapshot(event.snapshot);
    reconcileRestoredPosition();
    bool state_projection_changed = volume_removed;
    bool source_import_changed = false;
    std::optional<std::pair<QString, QString>> automatically_skipped;
    if (retry_event && event.snapshot.status != PlaybackStatus::Loading) {
        if (event.snapshot.status == PlaybackStatus::Ready || event.snapshot.status == PlaybackStatus::Paused ||
            event.snapshot.status == PlaybackStatus::Playing) {
            const auto current = local_queue_.currentIndex();
            if (!current.has_value() || !local_queue_.markAvailable(*current) ||
                !local_queue_model_.markAvailable(occurrence_retry_->occurrence_id))
                throw std::logic_error{"a confirmed occurrence retry could not restore availability"};
            navigation_items_[*current].available = true;
            occurrence_retry_.reset();
        }
        if (event.snapshot.status == PlaybackStatus::Empty)
            occurrence_retry_.reset();
        state_projection_changed = true;
    }
    if (result == LocalTrackPlaybackEventResult::OccurrenceUnavailable) {
        const QString failed_title = currentTitle();
        const auto current = local_queue_.currentIndex();
        if (!current.has_value())
            throw std::logic_error{"a current local playback error requires a current queue occurrence"};

        const auto occurrence_id = local_queue_.items()[*current].occurrence_id;
        const bool imported_occurrence = std::find(source_import_occurrences_.begin(), source_import_occurrences_.end(),
                                                   occurrence_id) != source_import_occurrences_.end();
        if (local_queue_.items()[*current].available) {
            const QString error_code = localPlaybackErrorCode(event.snapshot.error);
            if (error_code.isEmpty() || !local_queue_.markUnavailable(*current) ||
                !local_queue_model_.markUnavailable(occurrence_id, error_code)) {
                throw std::logic_error{"a typed local playback error could not be projected"};
            }
            navigation_items_[*current].available = false;
            state_projection_changed = true;
            if (imported_occurrence && recordSourceImportOccurrenceIssue(occurrence_id)) {
                if (source_import_state_ == SourceImportState::Completed)
                    source_import_state_ = SourceImportState::CompletedWithIssues;
                source_import_changed = true;
            }
        } else if (retry_event || volume_removed) {
            static_cast<void>(
                local_queue_model_.markUnavailable(occurrence_id, localPlaybackErrorCode(event.snapshot.error)));
        }

        auto start_intent = local_recovery_intent_.value_or(local_track_session_->startIntent());
        if (!local_recovery_intent_.has_value()) {
            if (before.status == PlaybackStatus::Ready || before.status == PlaybackStatus::Paused)
                start_intent = LocalTrackStartIntent::StayReady;
            else if (before.status == PlaybackStatus::Playing)
                start_intent = LocalTrackStartIntent::Autoplay;
        }
        local_recovery_intent_ = start_intent;
        bool recovered = false;
        if (!volume_removed && !restoring_session && !retry_event) {
            const QPointer<PlayerPresenter> guard{this};
            const auto import_generation = source_import_generation_;
            const auto import_state = source_import_state_;
            const auto advance = advanceLocalOccurrence(start_intent, false);
            if (advance == LocalAdvanceResult::Superseded) {
                if (source_import_changed && guard && !exiting_ && source_import_generation_ == import_generation &&
                    source_import_state_ == import_state)
                    emit sourceImportChanged();
                return;
            }
            if (advance == LocalAdvanceResult::Accepted) {
                state_projection_changed = true;
                recovered = true;
                automatically_skipped.emplace(failed_title, localPlaybackErrorCode(event.snapshot.error));
            }
        }
        if (!volume_removed && imported_occurrence && source_import_state_ == SourceImportState::Discovering &&
            source_import_autoplay_enabled_ && !recovered)
            source_import_needs_autoplay_ = true;
    } else if (result == LocalTrackPlaybackEventResult::Ended && !restoring_session && !suppress_restored_end) {
        const bool has_next = nextAvailableLocalIndex().has_value();
        const auto advance = advanceLocalOccurrence(LocalTrackStartIntent::Autoplay, true);
        if (advance == LocalAdvanceResult::Superseded)
            return;
        if (advance == LocalAdvanceResult::Accepted) {
            state_projection_changed = true;
        } else if (!has_next && preferences().queue_end_behavior == QueueEndBehavior::StopClearSelection &&
                   local_track_session_->unload()) {
            if (!local_queue_.clearSelection())
                throw std::logic_error{"an ended local queue could not clear its current occurrence"};
            local_recovery_intent_.reset();
            artwork_cache_->beginGeneration(MetadataGeneration{});
            state_projection_changed = true;
        }
    }
    if (result == LocalTrackPlaybackEventResult::Ended &&
        restored_eof_generation_ == std::optional{event.snapshot.generation}) {
        restored_eof_generation_.reset();
    }

    if (state_projection_changed && !volume_removed)
        controller_.applyBackendSnapshot(backend_.snapshot());
    if (state_projection_changed)
        sessionStructureDidChange();
    if (!restoring_session) {
        const PlaybackStatus status = controller_.confirmedSnapshot().status;
        const bool checkpoint = (seek_was_pending && !controller_.seekPending()) || status == PlaybackStatus::Paused ||
                                status == PlaybackStatus::Error || status == PlaybackStatus::Empty ||
                                result == LocalTrackPlaybackEventResult::Ended ||
                                result == LocalTrackPlaybackEventResult::OccurrenceUnavailable;
        observeSessionPosition(checkpoint);
    }
    reconcileTimer();
    const bool playback_changed = controller_.confirmedSnapshot() != before;
    const auto notification_generation = controller_.confirmedSnapshot().generation;
    const auto notification_occurrence = local_queue_.currentOccurrenceId();
    const auto notification_import_generation = source_import_generation_;
    const auto notification_import_state = source_import_state_;
    const QPointer<PlayerPresenter> guard{this};
    const auto notificationStillCurrent = [guard, notification_generation, notification_occurrence] {
        return guard && !guard->exiting_ &&
               guard->controller_.confirmedSnapshot().generation == notification_generation &&
               guard->local_queue_.currentOccurrenceId() == notification_occurrence;
    };
    if (automatically_skipped.has_value())
        emit occurrenceAutomaticallySkipped(automatically_skipped->first, automatically_skipped->second);
    if (notificationStillCurrent() && (state_projection_changed || playback_changed))
        emit stateChanged();
    if (notificationStillCurrent() && playback_changed)
        emit playbackChanged();
    // Import issues belong to their discovery batch, not the playback generation.
    // A track selected by an observer must not hide that batch's updated summary.
    if (source_import_changed && guard && !guard->exiting_ &&
        guard->source_import_generation_ == notification_import_generation &&
        guard->source_import_state_ == notification_import_state)
        emit sourceImportChanged();
}
void PlayerPresenter::pollMetadata() {
    if (download_append_in_progress_)
        return;
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
void PlayerPresenter::pollSourceImport() {
    if (download_append_in_progress_)
        return;
    if (!sourceImportActive() || source_discovery_ == nullptr || (exiting_ && !source_exit_drain_active_))
        return;

    auto result = source_discovery_->poll(source_import_generation_);
    switch (result.status) {
    case DiscoveryPollStatus::Pending:
        return;
    case DiscoveryPollStatus::BatchReady:
        if (source_import_state_ == SourceImportState::Cancelling)
            return;
        if (!result.batch.has_value() || result.batch->generation != source_import_generation_ ||
            result.batch->sequence <= source_import_sequence_) {
            static_cast<void>(source_discovery_->cancel(source_import_generation_));
            addSaturated(source_import_issue_count_, 1);
            finishSourceImport(SourceImportState::Rejected);
            return;
        }
        source_import_sequence_ = result.batch->sequence;
        if (!applySourceImportBatch(std::move(*result.batch))) {
            static_cast<void>(source_discovery_->cancel(source_import_generation_));
            addSaturated(source_import_issue_count_, 1);
            finishSourceImport(SourceImportState::Rejected);
            return;
        }
        emit sourceImportChanged();
        return;
    case DiscoveryPollStatus::Completed:
        if (!result.summary.has_value()) {
            addSaturated(source_import_issue_count_, 1);
            finishSourceImport(SourceImportState::Rejected);
            return;
        }
        source_import_inspected_count_ = std::max(source_import_inspected_count_, result.summary->inspected_count);
        if (result.summary->issue_count > source_import_discovery_issue_count_) {
            addSaturated(source_import_issue_count_,
                         result.summary->issue_count - source_import_discovery_issue_count_);
            source_import_discovery_issue_count_ = result.summary->issue_count;
        }
        if (result.summary->accepted_count != source_import_accepted_count_)
            addSaturated(source_import_issue_count_, 1);
        finishSourceImport(source_import_issue_count_ == 0 ? SourceImportState::Completed
                                                           : SourceImportState::CompletedWithIssues);
        return;
    case DiscoveryPollStatus::Cancelled:
        finishSourceImport(SourceImportState::Cancelled);
        return;
    case DiscoveryPollStatus::InvalidArgument:
    case DiscoveryPollStatus::StaleGeneration:
    case DiscoveryPollStatus::Shutdown:
    case DiscoveryPollStatus::Idle:
        addSaturated(source_import_issue_count_, 1);
        finishSourceImport(SourceImportState::Rejected);
        return;
    }
}

bool PlayerPresenter::applySourceImportBatch(LocalSourceDiscoveryBatch batch) {
    addSaturated(source_import_inspected_count_, batch.inspected_count);
    addSaturated(source_import_issue_count_, static_cast<std::uint64_t>(batch.issues.size()));
    addSaturated(source_import_discovery_issue_count_, static_cast<std::uint64_t>(batch.issues.size()));
    if (batch.accepted_paths.empty())
        return true;

    const std::size_t insertion_index = sourceImportInsertionIndex();
    const std::size_t insertion_count = batch.accepted_paths.size();
    if (!local_queue_.insert(std::move(batch.accepted_paths), insertion_index))
        return false;
    if (!local_queue_model_.insert(local_queue_.items().subspan(insertion_index, insertion_count), insertion_index))
        throw std::logic_error{"accepted source discovery batch could not be projected"};

    source_import_occurrences_.reserve(source_import_occurrences_.size() + insertion_count);
    for (const auto& item : local_queue_.items().subspan(insertion_index, insertion_count))
        source_import_occurrences_.push_back(item.occurrence_id);
    source_import_last_inserted_occurrence_ = local_queue_.items()[insertion_index + insertion_count - 1].occurrence_id;
    addSaturated(source_import_accepted_count_, static_cast<std::uint64_t>(insertion_count));
    navigation_items_ = navigationItems(local_queue_);

    if (source_import_needs_autoplay_) {
        const auto current = local_queue_.currentIndex();
        const bool has_available_track =
            local_track_session_->hasTrack() && current.has_value() && local_queue_.items()[*current].available;
        if (has_available_track) {
            source_import_needs_autoplay_ = false;
        } else {
            for (std::size_t index = insertion_index; index < insertion_index + insertion_count; ++index) {
                const PlaybackCommandResult load_result =
                    loadLocalOccurrenceWithResult(index, LocalTrackStartIntent::Autoplay);
                if (load_result == PlaybackCommandResult::Accepted) {
                    source_import_needs_autoplay_ = false;
                    controller_.applyBackendSnapshot(backend_.snapshot());
                    reconcileTimer();
                    break;
                }
                if (load_result != PlaybackCommandResult::InvalidArgument) {
                    if (load_result == PlaybackCommandResult::Shutdown)
                        break;
                    continue;
                }
                const LocalQueueOccurrenceId occurrence_id = local_queue_.items()[index].occurrence_id;
                if (!local_queue_.markUnavailable(index) ||
                    !local_queue_model_.markUnavailable(occurrence_id, QStringLiteral("open_rejected"))) {
                    throw std::logic_error{"rejected discovered occurrence could not be isolated"};
                }
                navigation_items_[index].available = false;
                static_cast<void>(recordSourceImportOccurrenceIssue(occurrence_id));
            }
        }
    }

    sessionStructureDidChange();
    emit stateChanged();
    emit playbackChanged();
    return true;
}

bool PlayerPresenter::recordSourceImportOccurrenceIssue(const LocalQueueOccurrenceId occurrence_id) {
    if (std::find(source_import_issue_occurrences_.begin(), source_import_issue_occurrences_.end(), occurrence_id) !=
        source_import_issue_occurrences_.end())
        return false;
    source_import_issue_occurrences_.push_back(occurrence_id);
    addSaturated(source_import_issue_count_, 1);
    return true;
}

std::size_t PlayerPresenter::sourceImportInsertionIndex() const noexcept {
    if (source_import_before_occurrence_.has_value()) {
        if (const auto index = localOccurrenceIndex(*source_import_before_occurrence_); index.has_value())
            return *index;
    }
    if (source_import_last_inserted_occurrence_.has_value()) {
        if (const auto index = localOccurrenceIndex(*source_import_last_inserted_occurrence_); index.has_value())
            return *index + 1;
    }
    if (source_import_predecessor_occurrence_.has_value()) {
        if (const auto index = localOccurrenceIndex(*source_import_predecessor_occurrence_); index.has_value())
            return *index + 1;
    }
    return local_queue_.size();
}

std::optional<std::size_t>
PlayerPresenter::localOccurrenceIndex(const LocalQueueOccurrenceId occurrence_id) const noexcept {
    const auto items = local_queue_.items();
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (items[index].occurrence_id == occurrence_id)
            return index;
    }
    return std::nullopt;
}

DiscoveryGeneration PlayerPresenter::nextDiscoveryGeneration(const DiscoveryGeneration current) noexcept {
    return current.value == std::numeric_limits<std::uint64_t>::max() ? DiscoveryGeneration{1}
                                                                      : DiscoveryGeneration{current.value + 1};
}

void PlayerPresenter::rejectSourceImport(const std::uint64_t issue_count) {
    source_import_sequence_ = 0;
    source_import_accepted_count_ = 0;
    source_import_issue_count_ = issue_count;
    source_import_discovery_issue_count_ = 0;
    source_import_inspected_count_ = 0;
    source_import_state_ = SourceImportState::Rejected;
    source_import_before_occurrence_.reset();
    source_import_predecessor_occurrence_.reset();
    source_import_last_inserted_occurrence_.reset();
    source_import_occurrences_.clear();
    source_import_autoplay_enabled_ = false;
    source_import_needs_autoplay_ = false;
    reconcileTimer();
    emit sourceImportChanged();
}

void PlayerPresenter::finishSourceImport(const SourceImportState terminal_state) {
    source_import_state_ = terminal_state;
    source_import_before_occurrence_.reset();
    source_import_predecessor_occurrence_.reset();
    source_import_last_inserted_occurrence_.reset();
    source_import_autoplay_enabled_ = false;
    source_import_needs_autoplay_ = false;
    reconcileTimer();
    emit sourceImportChanged();
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
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
DeterministicPlaybackBackend* PlayerPresenter::fixtureBackend() noexcept { return owned_backend_.get(); }
const DeterministicPlaybackBackend* PlayerPresenter::fixtureBackend() const noexcept { return owned_backend_.get(); }
#endif

void PlayerPresenter::reportTrayUnavailable() {
    if (exiting_ || window_notice_ == WindowNotice::TrayUnavailableNotice)
        return;
    window_notice_ = WindowNotice::TrayUnavailableNotice;
    emit lifecycleChanged();
}
bool PlayerPresenter::requestClose() {
    if (download_append_in_progress_)
        return false;
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
    if (download_append_in_progress_)
        return false;
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
    if (download_append_in_progress_)
        return false;
    if (source_exit_drain_active_)
        return true;
    if (exiting_)
        return false;
    controller_.requestRestore();
    return true;
}
bool PlayerPresenter::requestExit() {
    if (QThread::currentThread() != thread())
        return false;
    if (download_append_in_progress_)
        return false;
    if (exiting_ || playlist_exit_deadline_.has_value() || file_dialog_cancellation_active_)
        return false;

    const QPointer<PlayerPresenter> guard{this};
    const auto playlist_preparation = playlistExitPreparation();
    if (!guard)
        return false;
    if (playlist_preparation == ExitPreparationState::Pending) {
        try {
            playlist_exit_deadline_ = preference_now_() + preference_exit_flush_timeout_;
        } catch (...) {
            return false;
        }
        reconcileTimer();
        return true;
    }
    if (playlist_preparation != ExitPreparationState::Ready) {
        showPlaylistExitRecovery();
        return false;
    }

    const bool selection_was_pending = selectingAudioFiles() || file_location_request_.has_value();
    cancelAudioFileSelection();
    if (!guard)
        return false;
    cancelFileLocation();
    if (!guard)
        return false;
    if (selection_was_pending) {
        // Publish after the exit decision so observers cannot reenter with another picker.
        QMetaObject::invokeMethod(this, [this] { emit stateChanged(); }, Qt::QueuedConnection);
    }

    const bool needs_preparation = static_cast<bool>(exit_preparation_callbacks_.begin);
    if (needs_preparation || !sourceExitDrained()) {
        if (!guard)
            return false;
        PlayerPresenterPreferences::TimePoint deadline;
        try {
            deadline = preference_now_() + preference_exit_flush_timeout_;
        } catch (...) {
            return false;
        }
        deferred_playback_event_.reset();
        exiting_ = true;
        exit_committed_ = false;
        source_exit_drain_active_ = true;
        window_notice_ = WindowNotice::NoNotice;
        preference_exit_deadline_ = deadline;
        if (needs_preparation) {
            exit_preparation_started_ = true;
            exit_preparation_call_in_progress_ = true;
        }
        emit lifecycleChanged();
        if (!guard)
            return false;
        if (needs_preparation) {
            try {
                // The callback can delete its owner/presenter; retain its callable
                // independently and never access this again without the guard.
                const auto begin = exit_preparation_callbacks_.begin;
                begin();
            } catch (...) {
                if (guard) {
                    exit_preparation_call_in_progress_ = false;
                    cancelPendingExit();
                }
                return false;
            }
            if (!guard)
                return false;
            exit_preparation_call_in_progress_ = false;
        }
        reconcileTimer();
        pollPersistence();
        return true;
    }

    return guard ? beginPersistenceExit() : false;
}

bool PlayerPresenter::sourceExitDrained() const {
    try {
        const auto probe = source_exit_drained_probe_;
        return probe ? probe() : !sourceImportActive();
    } catch (...) {
        return false;
    }
}

ExitPreparationState PlayerPresenter::playlistExitPreparation() const {
    try {
        const auto probe = playlist_exit_preparation_probe_;
        return probe ? probe() : ExitPreparationState::Ready;
    } catch (...) {
        return ExitPreparationState::Failed;
    }
}

void PlayerPresenter::showPlaylistExitRecovery() {
    const QPointer<PlayerPresenter> guard{this};
    controller_.requestRestore();
    if (guard)
        emit playlistExitRecoveryRequested();
}

ExitPreparationState PlayerPresenter::pollExitPreparation() {
    if (!exit_preparation_callbacks_.poll)
        return ExitPreparationState::Ready;
    if (exit_preparation_call_in_progress_)
        return ExitPreparationState::Pending;
    const QPointer<PlayerPresenter> guard{this};
    exit_preparation_call_in_progress_ = true;
    auto result = ExitPreparationState::Failed;
    try {
        const auto poll = exit_preparation_callbacks_.poll;
        const auto reported = poll();
        if (reported == ExitPreparationState::Pending || reported == ExitPreparationState::Ready ||
            reported == ExitPreparationState::Failed)
            result = reported;
    } catch (...) {
    }
    if (guard)
        exit_preparation_call_in_progress_ = false;
    return guard ? result : ExitPreparationState::Failed;
}

bool PlayerPresenter::beginPersistenceExit() {
    source_exit_drain_active_ = false;

    emit windowPlacementFlushRequested();
    if (session_autosave_ != nullptr) {
        if (!uses_frozen_fixture_)
            static_cast<void>(session_autosave_->scheduleStructural(currentSessionSnapshot()));
        if (!exiting_)
            observeSessionPosition(true);
        session_autosave_->requestSaveNow();
    }
    if (preferences_autosave_ != nullptr)
        preferences_autosave_->requestSaveNow();
    synchronizePreferenceWarning();

    const bool preferences_pending =
        preferences_autosave_ != nullptr && (preferences_autosave_->dirty() || preferences_autosave_->writeActive());
    const bool session_pending =
        session_autosave_ != nullptr && (session_autosave_->dirty() || session_autosave_->writeActive());
    const bool preferences_failed = preferences_autosave_ != nullptr && preferences_autosave_->dirty() &&
                                    !preferences_autosave_->writeActive() &&
                                    preferences_autosave_->notice() != PreferencesAutosaveNotice::None;
    const bool session_failed = session_autosave_ != nullptr && session_autosave_->dirty() &&
                                !session_autosave_->writeActive() &&
                                session_autosave_->notice() != SessionAutosaveNotice::None;
    if (preferences_failed || session_failed) {
        if (exiting_)
            cancelPendingExit();
        else
            reconcileTimer();
        return false;
    }
    if (preferences_pending || session_pending) {
        deferred_playback_event_.reset();
        const bool publish_exit_intent = !exiting_;
        exiting_ = true;
        window_notice_ = WindowNotice::NoNotice;
        if (!preference_exit_deadline_.has_value())
            preference_exit_deadline_ = preference_now_() + preference_exit_flush_timeout_;
        if (publish_exit_intent)
            emit lifecycleChanged();
        reconcileTimer();
        pollPersistence();
        return true;
    }

    completeExit();
    return true;
}

void PlayerPresenter::completeExit() {
    const QPointer<PlayerPresenter> guard{this};
    const auto preparation = pollExitPreparation();
    if (!guard)
        return;
    if (preparation != ExitPreparationState::Ready) {
        if (preparation == ExitPreparationState::Failed)
            cancelPendingExit();
        else {
            source_exit_drain_active_ = true;
            reconcileTimer();
        }
        return;
    }
    preference_exit_deadline_.reset();
    deferred_playback_event_.reset();
    source_exit_drain_active_ = false;
    const bool changed = !exiting_ || !exit_committed_ || window_notice_ != WindowNotice::NoNotice;
    exiting_ = true;
    exit_committed_ = true;
    window_notice_ = WindowNotice::NoNotice;
    timer_.stop();
    if (default_apps_settings_state_ == DefaultAppsSettingsState::Opening) {
        ++default_apps_settings_generation_;
        default_apps_settings_state_ = DefaultAppsSettingsState::Idle;
        emit defaultAppsSettingsChanged();
    }
    if (sourceImportActive() && source_discovery_ != nullptr) {
        static_cast<void>(source_discovery_->cancel(source_import_generation_));
        finishSourceImport(SourceImportState::Cancelled);
    }
    if (changed)
        emit lifecycleChanged();
    controller_.requestExit();
}

void PlayerPresenter::cancelPendingExit() {
    if (!preference_exit_deadline_.has_value())
        return;
    auto deferred_playback_event = std::exchange(deferred_playback_event_, std::nullopt);
    preference_exit_deadline_.reset();
    exiting_ = false;
    exit_committed_ = false;
    source_exit_drain_active_ = false;
    emit lifecycleChanged();
    controller_.requestRestore();
    if (deferred_playback_event.has_value()) {
        handlePlaybackEvent(*deferred_playback_event);
    } else if (!uses_frozen_fixture_ && local_track_session_ != nullptr && local_track_session_->hasTrack() &&
               backend_.snapshot().generation == local_track_session_->playbackGeneration() &&
               backend_.snapshot() != controller_.confirmedSnapshot()) {
        handlePlaybackEvent({PlaybackEventType::SnapshotChanged, backend_.snapshot()});
    }
    reconcileTimer();
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
bool PlayerPresenter::handleSystemPowerEvent(const SystemPowerEvent event) {
    if (download_append_in_progress_) {
        deferred_download_suspend_ = deferred_download_suspend_ || event == SystemPowerEvent::Suspending;
        deferred_download_power_event_ = event;
        return true;
    }
    if (exit_committed_ || last_system_power_event_ == std::optional{event})
        return false;

    last_system_power_event_ = event;
    if (exiting_) {
        reconcileTimer();
        return true;
    }
    if (event == SystemPowerEvent::Suspending) {
        if (session_autosave_ != nullptr && !uses_frozen_fixture_) {
            observeSessionPosition(true);
            session_autosave_->requestSaveNow();
        }
        reconcileTimer();
        return true;
    }

    refreshPlatformAvailability();
    if (!uses_frozen_fixture_ && local_track_session_ != nullptr && local_track_session_->hasTrack()) {
        const PlaybackSnapshot snapshot = backend_.snapshot();
        if (snapshot.generation == local_track_session_->playbackGeneration() &&
            snapshot != controller_.confirmedSnapshot()) {
            handlePlaybackEvent({PlaybackEventType::SnapshotChanged, snapshot});
        }
    }
    reconcileTimer();
    return true;
}
void PlayerPresenter::refreshPlatformAvailability() {
    const bool tray_available = platform_.trayAvailable();
    const bool default_apps_settings_supported = platform_.defaultAppsSettingsSupported();
    const bool default_apps_settings_available =
        default_apps_settings_supported && platform_.defaultAppsSettingsAvailable();
    if (tray_available != tray_available_) {
        tray_available_ = tray_available;
        emit platformChanged();
    }
    if (default_apps_settings_supported != default_apps_settings_supported_ ||
        default_apps_settings_available != default_apps_settings_available_) {
        default_apps_settings_supported_ = default_apps_settings_supported;
        default_apps_settings_available_ = default_apps_settings_available;
        emit defaultAppsSettingsChanged();
    }
}

bool PlayerPresenter::openDefaultAppsSettings() {
    if (exiting_ || !default_apps_settings_supported_ ||
        default_apps_settings_state_ == DefaultAppsSettingsState::Opening) {
        return false;
    }
    if (!default_apps_settings_available_) {
        if (default_apps_settings_state_ != DefaultAppsSettingsState::Unavailable) {
            default_apps_settings_state_ = DefaultAppsSettingsState::Unavailable;
            emit defaultAppsSettingsChanged();
        }
        return false;
    }

    const std::uint64_t generation = ++default_apps_settings_generation_;
    default_apps_settings_state_ = DefaultAppsSettingsState::Opening;
    emit defaultAppsSettingsChanged();
    const QPointer<PlayerPresenter> guarded_this{this};
    try {
        platform_.openDefaultAppsSettings([guarded_this, generation](const DefaultAppsSettingsLaunchResult result) {
            if (guarded_this == nullptr)
                return;
            QMetaObject::invokeMethod(
                guarded_this,
                [guarded_this, generation, result] {
                    if (guarded_this != nullptr)
                        guarded_this->completeDefaultAppsSettingsLaunch(result, generation);
                },
                Qt::QueuedConnection);
        });
    } catch (...) {
        completeDefaultAppsSettingsLaunch(DefaultAppsSettingsLaunchResult::Failed, generation);
    }
    return true;
}

void PlayerPresenter::completeDefaultAppsSettingsLaunch(const DefaultAppsSettingsLaunchResult result,
                                                        const std::uint64_t generation) {
    if (exit_committed_ || generation != default_apps_settings_generation_ ||
        default_apps_settings_state_ != DefaultAppsSettingsState::Opening) {
        return;
    }
    switch (result) {
    case DefaultAppsSettingsLaunchResult::Dispatched:
        default_apps_settings_state_ = DefaultAppsSettingsState::Dispatched;
        break;
    case DefaultAppsSettingsLaunchResult::Unavailable:
        default_apps_settings_available_ = false;
        default_apps_settings_state_ = DefaultAppsSettingsState::Unavailable;
        break;
    case DefaultAppsSettingsLaunchResult::Failed:
        default_apps_settings_state_ = DefaultAppsSettingsState::Failed;
        break;
    }
    emit defaultAppsSettingsChanged();
}
bool PlayerPresenter::play() {
    if (download_append_in_progress_)
        return false;
    if (exiting_)
        return false;
    const PlaybackSnapshot before = controller_.confirmedSnapshot();
    if (uses_frozen_fixture_ && before.duration_ms > 0 && before.position_ms >= before.duration_ms)
        return false;
    if (!controller_.play())
        return false;
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        controller_.applyBackendSnapshot(backend_.snapshot());
#endif
    cancelRestoredPositionForExplicitTransport();
    const bool changed = before != controller_.confirmedSnapshot();
    if (changed) {
        reconcileTimer();
        emit playbackChanged();
    }
    return uses_frozen_fixture_ ? changed : true;
}
bool PlayerPresenter::pause() {
    if (download_append_in_progress_)
        return false;
    if (exiting_)
        return false;
    const PlaybackSnapshot before = controller_.confirmedSnapshot();
    if (!controller_.pause())
        return false;
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        controller_.applyBackendSnapshot(backend_.snapshot());
#endif
    cancelRestoredPositionForExplicitTransport();
    const bool changed = before != controller_.confirmedSnapshot();
    if (changed) {
        reconcileTimer();
        emit playbackChanged();
    }
    return uses_frozen_fixture_ ? changed : true;
}
bool PlayerPresenter::togglePlayback() {
    if (download_append_in_progress_)
        return false;
    if (exiting_)
        return false;
    const PlaybackSnapshot before = controller_.confirmedSnapshot();
    if (uses_frozen_fixture_ && before.status != PlaybackStatus::Playing && before.duration_ms > 0 &&
        before.position_ms >= before.duration_ms)
        return false;
    if (!controller_.togglePlayback())
        return false;
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        controller_.applyBackendSnapshot(backend_.snapshot());
#endif
    cancelRestoredPositionForExplicitTransport();
    const bool changed = before != controller_.confirmedSnapshot();
    if (changed) {
        reconcileTimer();
        emit playbackChanged();
    }
    return uses_frozen_fixture_ ? changed : true;
}
bool PlayerPresenter::skipBackward() { return skipByPreference(false); }
bool PlayerPresenter::skipForward() { return skipByPreference(true); }
bool PlayerPresenter::skipByPreference(const bool forward) {
    if (download_append_in_progress_)
        return false;
    const auto snapshot = controller_.confirmedSnapshot();
    const bool transport_active = snapshot.status == PlaybackStatus::Ready ||
                                  snapshot.status == PlaybackStatus::Paused ||
                                  snapshot.status == PlaybackStatus::Playing;
    if (exiting_ || !transport_active || !snapshot.seekable || controller_.seekPreviewActive() ||
        controller_.seekPending())
        return false;
    const bool accepted = forward ? controller_.skipForward() : controller_.skipBackward();
    return finishSeekCommand(accepted, snapshot.generation);
}
bool PlayerPresenter::previousTrack() { return !exiting_ && navigate(QueueDirection::Previous); }
bool PlayerPresenter::nextTrack() { return !exiting_ && navigate(QueueDirection::Next); }
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
    const std::size_t candidates = repeatQueueEnabled() ? local_queue_.size() : local_queue_.size() - *current - 1;
    for (std::size_t offset = 1; offset <= candidates; ++offset) {
        const std::size_t index = (*current + offset) % local_queue_.size();
        if (local_queue_.items()[index].available)
            return index;
    }
    return std::nullopt;
}
PlayerPresenter::LocalAdvanceResult PlayerPresenter::advanceLocalOccurrence(const LocalTrackStartIntent start_intent,
                                                                            const bool completed) {
    const auto previous_index = local_queue_.currentIndex();
    const auto previous_id = local_queue_.currentOccurrenceId();
    auto next = nextAvailableLocalIndex();
    if (!previous_index.has_value() || !previous_id.has_value() || !next.has_value())
        return LocalAdvanceResult::Rejected;

    bool starts_new_round = false;
    if (shuffleQueueEnabled()) {
        std::vector<std::size_t> candidates;
        const auto collect = [&](const bool allow_visited) {
            const auto first = repeatQueueEnabled() ? 0 : *previous_index + 1;
            for (std::size_t index = first; index < local_queue_.size(); ++index) {
                const auto& item = local_queue_.items()[index];
                if (index != *previous_index && item.available &&
                    (allow_visited || !shuffle_round_visited_.contains(item.occurrence_id.value)))
                    candidates.push_back(index);
            }
        };
        collect(!repeatQueueEnabled());
        if (candidates.empty() && repeatQueueEnabled()) {
            starts_new_round = true;
            collect(true);
            if (candidates.empty() && local_queue_.items()[*previous_index].available)
                candidates.push_back(*previous_index);
        }
        if (candidates.empty())
            return LocalAdvanceResult::Rejected;
        next = candidates[std::uniform_int_distribution<std::size_t>{0, candidates.size() - 1}(queue_shuffle_random_)];
    }
    const auto selected_id = local_queue_.items()[*next].occurrence_id;
    // The engine must accept before we consume eligibility or change the visible queue.
    if (!loadLocalOccurrence(*next, start_intent))
        return LocalAdvanceResult::Rejected;
    if (shuffleQueueEnabled()) {
        if (starts_new_round)
            shuffle_round_visited_.clear();
        shuffle_round_visited_.insert(selected_id.value);
    }
    const QPointer<PlayerPresenter> guard{this};
    const auto generation = backend_.snapshot().generation;
    const auto move = [&](const LocalQueueOccurrenceId id, const std::size_t destination) {
        const auto source = localOccurrenceIndex(id);
        if (!source.has_value())
            throw std::logic_error{"an advancing occurrence disappeared before its projection"};
        if (*source == destination)
            return true;
        if (!local_queue_.move(id, destination))
            throw std::logic_error{"an advancing occurrence could not be moved"};
        navigation_items_ = navigationItems(local_queue_);
        controller_.applyBackendSnapshot(backend_.snapshot());
        sessionStructureDidChange();
        const auto expected_queue = local_queue_.snapshot();
        if (!local_queue_model_.move(*source, destination))
            throw std::logic_error{"an advancing occurrence could not be projected"};
        return guard && !exiting_ && backend_.snapshot().generation == generation &&
               local_queue_.snapshot() == expected_queue;
    };
    if (shuffleQueueEnabled() && selected_id != *previous_id) {
        // Move the chosen occurrence next to the current one, keeping every unchosen
        // occurrence pending. A circular selection from the prefix starts at index zero.
        const auto destination = *next > *previous_index ? *previous_index + 1 : 0;
        if (!move(selected_id, destination))
            return LocalAdvanceResult::Superseded;
    }
    if (completed && repeatQueueEnabled() && !move(*previous_id, local_queue_.size() - 1))
        return LocalAdvanceResult::Superseded;
    return LocalAdvanceResult::Accepted;
}
PlaybackCommandResult PlayerPresenter::loadLocalOccurrenceWithResult(const std::size_t index,
                                                                     const LocalTrackStartIntent start_intent) {
    if (index >= local_queue_.size() || !local_queue_.items()[index].available)
        return PlaybackCommandResult::InvalidArgument;
    const PlaybackCommandResult result =
        local_track_session_->openWithResult(local_queue_.items()[index].source_path, start_intent);
    if (result != PlaybackCommandResult::Accepted)
        return result;
    if (!local_queue_.select(index))
        throw std::logic_error{"an accepted local occurrence could not be selected"};
    local_recovery_intent_.reset();
    artwork_cache_->beginGeneration(local_track_session_->metadataGeneration());
    return PlaybackCommandResult::Accepted;
}
bool PlayerPresenter::loadLocalOccurrence(const std::size_t index, const LocalTrackStartIntent start_intent) {
    return loadLocalOccurrenceWithResult(index, start_intent) == PlaybackCommandResult::Accepted;
}
bool PlayerPresenter::navigate(const QueueDirection direction) {
    if (download_append_in_progress_)
        return false;

    const auto target = navigationTarget(direction);
    if (!target.has_value())
        return false;
    if (*target == static_cast<std::size_t>(currentQueueIndex()) &&
        (uses_frozen_fixture_ || direction == QueueDirection::Previous))
        return beginSeekPreview(0) && commitSeekPreview();

#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_) {

        const QueueItem& track = fixture_bundle_->queue.at(static_cast<qsizetype>(*target));
        auto* const fixture_backend = fixtureBackend();
        if (fixture_backend == nullptr || !track.duration_ms.has_value() ||
            !fixture_backend->selectTrack(*track.duration_ms, controller_.effectivePlaybackStatus()))
            return false;
        controller_.applyBackendSnapshot(backend_.snapshot());
        state_ = stateById(*fixture_bundle_, QStringLiteral("playing"));
        state_.current_occurrence_id = track.occurrence_id;
        state_.position_ms = backend_.snapshot().position_ms;
        state_.duration_ms = backend_.snapshot().duration_ms;
        state_.seekable = backend_.snapshot().seekable;
        reconcileTimer();
        emit stateChanged();
        emit playbackChanged();
        return true;
    }
#endif

    const bool recovering_terminal_source = controller_.confirmedSnapshot().status == PlaybackStatus::Error &&
                                            !navigation_items_[static_cast<std::size_t>(currentQueueIndex())].available;
    const auto start_intent =
        recovering_terminal_source
            ? local_recovery_intent_.value_or(local_track_session_->startIntent())
            : (controller_.effectivePlaybackStatus() == PlaybackStatus::Playing ? LocalTrackStartIntent::Autoplay
                                                                                : LocalTrackStartIntent::StayReady);
    if (direction == QueueDirection::Next) {
        const auto advance = advanceLocalOccurrence(start_intent, false);
        if (advance != LocalAdvanceResult::Accepted)
            return advance == LocalAdvanceResult::Superseded;
    } else if (!loadLocalOccurrence(*target, start_intent)) {
        return false;
    }
    controller_.applyBackendSnapshot(backend_.snapshot());
    sessionStructureDidChange();
    reconcileTimer();
    const QPointer<PlayerPresenter> guard{this};
    const auto generation = backend_.snapshot().generation;
    emit stateChanged();
    if (guard && !exiting_ && backend_.snapshot().generation == generation)
        emit playbackChanged();
    return true;
}
bool PlayerPresenter::beginSeekPreview(const qint64 position_ms) {
    if (download_append_in_progress_)
        return false;
    if (exiting_)
        return false;
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
    if (download_append_in_progress_)
        return false;
    if (exiting_ || !controller_.seekPreviewActive() || controller_.seekPending())
        return false;
    const auto before = controller_.displayedPositionMs();
    controller_.updateSeekPreview(position_ms);
    if (before != controller_.displayedPositionMs())
        emit playbackChanged();
    return true;
}
bool PlayerPresenter::commitSeekPreview() {
    if (download_append_in_progress_)
        return false;
    if (exiting_)
        return false;
    ++seek_commit_attempt_count_;
    if (!controller_.seekPreviewActive() || controller_.seekPending())
        return false;
    const auto generation = controller_.confirmedSnapshot().generation;
    return finishSeekCommand(controller_.commitSeekPreview(), generation);
}
bool PlayerPresenter::finishSeekCommand(const bool accepted, const std::uint64_t generation) {
    if (accepted)
        cancelRestoredPositionForExplicitTransport();
    reconcileTimer();
    const QPointer<PlayerPresenter> guard{this};
    emit playbackChanged();
    // Rejection already restored the controller's confirmed position. Publish
    // that state first; a listener may destroy us or replace the operation.
    if (!accepted && guard && !exiting_ && controller_.confirmedSnapshot().generation == generation &&
        !controller_.seekPreviewActive() && !controller_.seekPending()) {
        emit seekRejected();
    }
    return accepted;
}
bool PlayerPresenter::setVolume(const double volume) {
    if (download_append_in_progress_)
        return false;
    if (exiting_ || volume == controller_.preferences().volume || !controller_.setVolume(volume))
        return false;
    preferenceDidChange();
    return true;
}
bool PlayerPresenter::toggleMute() {
    if (download_append_in_progress_)
        return false;
    if (exiting_ || !controller_.toggleMute())
        return false;
    preferenceDidChange();
    return true;
}
bool PlayerPresenter::setThemePreference(const ThemePreference theme) {
    if (exiting_)
        return false;
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
    preferenceDidChange();
    return true;
}
bool PlayerPresenter::setMotionPreference(const MotionPreference motion) {
    if (exiting_)
        return false;
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
    preferenceDidChange();
    return true;
}
bool PlayerPresenter::setSkipBackwardSeconds(const int seconds) {
    if (exiting_ || seconds == skipBackwardSeconds() || !controller_.setSkipBackwardSeconds(seconds))
        return false;
    preferenceDidChange();
    return true;
}
bool PlayerPresenter::setSkipForwardSeconds(const int seconds) {
    if (exiting_ || seconds == skipForwardSeconds() || !controller_.setSkipForwardSeconds(seconds))
        return false;
    preferenceDidChange();
    return true;
}
bool PlayerPresenter::setClosePreference(const ClosePreference behavior) {
    if (exiting_)
        return false;
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
    preferenceDidChange();
    return true;
}
bool PlayerPresenter::setRestoreLastSession(const bool restore) {
    if (exiting_ || restore == restoreLastSession())
        return false;
    controller_.setRestoreLastSession(restore);
    if (restore && session_autosave_ != nullptr) {
        if (!session_autosave_->scheduleStructural(currentSessionSnapshot()))
            session_autosave_->scheduleRepair();
    }
    preferenceDidChange();
    return true;
}
bool PlayerPresenter::setStartMinimized(const bool minimized) {
    if (exiting_ || minimized == startMinimized() || !controller_.setStartMinimized(minimized))
        return false;
    preferenceDidChange();
    return true;
}
bool PlayerPresenter::setDownloadDirectory(std::string directory) {
    if (exiting_ || !controller_.setDownloadDirectory(std::move(directory)))
        return false;
    preferenceDidChange();
    return true;
}
int PlayerPresenter::lyricsTextSize() const noexcept { return preferences().lyrics_text_size; }
bool PlayerPresenter::setLyricsTextSize(const int size) {
    if (exiting_ || !controller_.setLyricsTextSize(size))
        return false;
    preferenceDidChange();
    return true;
}
bool PlayerPresenter::setQueueEndPreference(const QueueEndPreference behavior) {
    if (exiting_)
        return false;
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
    preferenceDidChange();
    return true;
}

bool PlayerPresenter::recordWindowPlacement(const WindowPlacement placement) {
    if (exiting_ || (preferences().window_placement && *preferences().window_placement == placement) ||
        !controller_.setWindowPlacement(placement)) {
        return false;
    }
    schedulePreferencePersistence();
    return true;
}

PlayerPresenter::PreferenceWarning PlayerPresenter::preferenceWarning() const noexcept { return preference_warning_; }

PlayerPresenter::PersistenceWarningScope PlayerPresenter::persistenceWarningScope() const noexcept {
    return persistence_warning_scope_;
}

QString PlayerPresenter::persistenceWarningRevision() const {
    // Never recycle an authorization identity, including at counter exhaustion.
    if (persistence_warning_revision_ == std::numeric_limits<std::uint64_t>::max())
        return {};
    return QString::number(persistence_warning_revision_);
}

void PlayerPresenter::schedulePreferencePersistence() {
    if (preferences_autosave_ != nullptr)
        static_cast<void>(preferences_autosave_->schedule(preferences()));
    reconcileTimer();
}

void PlayerPresenter::preferenceDidChange() {
    schedulePreferencePersistence();
    emit preferencesChanged();
}

void PlayerPresenter::pollPreferencePersistence() { pollPersistence(); }

void PlayerPresenter::pollPersistence() {
    if (download_append_in_progress_ || download_import_call_in_progress_ || exit_preparation_call_in_progress_)
        return;
    if (preferences_autosave_ != nullptr)
        preferences_autosave_->poll();
    if (session_autosave_ != nullptr)
        session_autosave_->poll();
    commitSavedDownloadImportRetirements();
    synchronizePreferenceWarning();

    if (playlist_exit_deadline_.has_value()) {
        const QPointer<PlayerPresenter> guard{this};
        const auto preparation = playlistExitPreparation();
        if (!guard)
            return;
        const bool expired = preference_now_() >= *playlist_exit_deadline_;
        if (preparation == ExitPreparationState::Pending && !expired)
            return;
        playlist_exit_deadline_.reset();
        if (preparation == ExitPreparationState::Ready && !expired)
            static_cast<void>(requestExit());
        else
            showPlaylistExitRecovery();
        if (guard && !exit_committed_)
            reconcileTimer();
        return;
    }

    if (source_exit_drain_active_) {
        const QPointer<PlayerPresenter> guard{this};
        const auto preparation = pollExitPreparation();
        if (!guard)
            return;
        if (preparation == ExitPreparationState::Failed) {
            cancelPendingExit();
            return;
        }
        const bool sources_drained = sourceExitDrained();
        if (!guard)
            return;
        if (preparation == ExitPreparationState::Ready && sources_drained) {
            static_cast<void>(beginPersistenceExit());
            return;
        }
        if (preference_exit_deadline_.has_value() && preference_now_() >= *preference_exit_deadline_) {
            cancelPendingExit();
            return;
        }
        reconcileTimer();
        return;
    }

    if (preference_exit_deadline_.has_value()) {
        const bool preferences_clean = preferences_autosave_ == nullptr ||
                                       (!preferences_autosave_->dirty() && !preferences_autosave_->writeActive());
        const bool session_clean =
            session_autosave_ == nullptr || (!session_autosave_->dirty() && !session_autosave_->writeActive());
        if (preferences_clean && session_clean) {
            completeExit();
            return;
        }
        const bool preferences_failed = preferences_autosave_ != nullptr && preferences_autosave_->dirty() &&
                                        !preferences_autosave_->writeActive() &&
                                        preferences_autosave_->notice() != PreferencesAutosaveNotice::None;
        const bool session_failed = session_autosave_ != nullptr && session_autosave_->dirty() &&
                                    !session_autosave_->writeActive() &&
                                    session_autosave_->notice() != SessionAutosaveNotice::None;
        if (preferences_failed || session_failed) {
            cancelPendingExit();
            return;
        }
        if (preference_now_() >= *preference_exit_deadline_) {
            if (!preferences_clean && preference_persistence_warning_ != PreferenceWarning::FutureVersion)
                preference_persistence_warning_ = PreferenceWarning::SaveFailed;
            if (!session_clean && session_persistence_warning_ != PreferenceWarning::FutureVersion)
                session_persistence_warning_ = PreferenceWarning::SaveFailed;
            projectPersistenceWarning();
            cancelPendingExit();
            return;
        }
    }
    reconcileTimer();
}

void PlayerPresenter::synchronizePreferenceWarning() {
    const PreferencesAutosaveNotice preferences_notice =
        preferences_autosave_ == nullptr ? PreferencesAutosaveNotice::None : preferences_autosave_->notice();
    const SessionAutosaveNotice session_notice =
        session_autosave_ == nullptr ? SessionAutosaveNotice::None : session_autosave_->notice();
    const bool preferences_future_version =
        preferences_future_version_pending_ || preferences_notice == PreferencesAutosaveNotice::FutureVersionBlocked;
    const bool session_future_version =
        session_future_version_pending_ || session_notice == SessionAutosaveNotice::FutureVersionBlocked;
    const bool preferences_save_failed = preferences_notice == PreferencesAutosaveNotice::SaveFailed;
    const bool session_save_failed =
        session_notice == SessionAutosaveNotice::SaveFailed || session_notice == SessionAutosaveNotice::SaveIncomplete;
    const bool preferences_clean =
        preferences_autosave_ == nullptr || (!preferences_autosave_->dirty() && !preferences_autosave_->writeActive());
    const bool session_clean =
        session_autosave_ == nullptr || (!session_autosave_->dirty() && !session_autosave_->writeActive());
    const auto synchronize_source = [](const PreferenceWarning current, const bool future_version,
                                       const bool save_failed, const bool clean) {
        if (future_version)
            return PreferenceWarning::FutureVersion;
        if (save_failed)
            return PreferenceWarning::SaveFailed;
        if (clean && (current == PreferenceWarning::SaveFailed || current == PreferenceWarning::FutureVersion))
            return PreferenceWarning::NoPreferenceWarning;
        return current;
    };

    preference_persistence_warning_ = synchronize_source(preference_persistence_warning_, preferences_future_version,
                                                         preferences_save_failed, preferences_clean);
    session_persistence_warning_ =
        synchronize_source(session_persistence_warning_, session_future_version, session_save_failed, session_clean);
    projectPersistenceWarning();
}

void PlayerPresenter::advancePersistenceWarningRevision() noexcept {
    if (persistence_warning_revision_ != std::numeric_limits<std::uint64_t>::max())
        ++persistence_warning_revision_;
}

void PlayerPresenter::projectPersistenceWarning() {
    const int preference_priority = persistenceWarningPriority(preference_persistence_warning_);
    const int session_priority = persistenceWarningPriority(session_persistence_warning_);
    const int projected_priority = std::max(preference_priority, session_priority);
    const bool projects_preferences = projected_priority > 0 && preference_priority == projected_priority;
    const bool projects_session = projected_priority > 0 && session_priority == projected_priority;
    const PreferenceWarning next_warning =
        projects_preferences ? preference_persistence_warning_ : session_persistence_warning_;
    const PersistenceWarningScope next_scope = warningScopeForSources(projects_preferences, projects_session);
    if (next_warning == preference_warning_ && next_scope == persistence_warning_scope_)
        return;
    preference_warning_ = next_warning;
    persistence_warning_scope_ = next_scope;
    advancePersistenceWarningRevision();
    emit preferencePersistenceChanged();
}

bool PlayerPresenter::dismissPreferenceWarning() {
    if (exiting_ || (preference_warning_ != PreferenceWarning::RecoveredFromBackup &&
                     preference_warning_ != PreferenceWarning::UsingDefaults)) {
        return false;
    }
    bool dismissed = false;
    if ((persistence_warning_scope_ == PersistenceWarningScope::PreferencePersistence ||
         persistence_warning_scope_ == PersistenceWarningScope::CombinedPersistence) &&
        preference_persistence_warning_ == preference_warning_) {
        preference_persistence_warning_ = PreferenceWarning::NoPreferenceWarning;
        dismissed = true;
    }
    if ((persistence_warning_scope_ == PersistenceWarningScope::SessionPersistence ||
         persistence_warning_scope_ == PersistenceWarningScope::CombinedPersistence) &&
        session_persistence_warning_ == preference_warning_) {
        session_persistence_warning_ = PreferenceWarning::NoPreferenceWarning;
        dismissed = true;
    }
    if (!dismissed)
        return false;
    projectPersistenceWarning();
    return true;
}

bool PlayerPresenter::retryPreferenceSave() {
    if (exiting_ || preference_warning_ != PreferenceWarning::SaveFailed)
        return false;

    bool retried = false;
    if (preferences_autosave_ != nullptr && preferences_autosave_->dirty() &&
        preferences_autosave_->notice() != PreferencesAutosaveNotice::FutureVersionBlocked) {
        retried = preferences_autosave_->retry() || retried;
    }
    if (session_autosave_ != nullptr && session_autosave_->dirty() &&
        session_autosave_->notice() != SessionAutosaveNotice::FutureVersionBlocked) {
        retried = session_autosave_->retry() || retried;
    }
    if (!retried)
        return false;
    reconcileTimer();
    return true;
}

bool PlayerPresenter::confirmPreferenceReplacement(const QString& expected_revision) {
    if (exiting_ || persistence_replacement_in_progress_ || preference_warning_ != PreferenceWarning::FutureVersion ||
        expected_revision.isEmpty() || expected_revision != persistenceWarningRevision())
        return false;

    // Freeze the displayed sources before scheduling can invoke another callback.
    const bool replace_preferences =
        (persistence_warning_scope_ == PersistenceWarningScope::PreferencePersistence ||
         persistence_warning_scope_ == PersistenceWarningScope::CombinedPersistence) &&
        preferences_autosave_ != nullptr &&
        (preferences_future_version_pending_ ||
         preferences_autosave_->notice() == PreferencesAutosaveNotice::FutureVersionBlocked);
    const bool replace_session =
        (persistence_warning_scope_ == PersistenceWarningScope::SessionPersistence ||
         persistence_warning_scope_ == PersistenceWarningScope::CombinedPersistence) &&
        session_autosave_ != nullptr &&
        (session_future_version_pending_ || session_autosave_->notice() == SessionAutosaveNotice::FutureVersionBlocked);
    if (!replace_preferences && !replace_session)
        return false;

    const QPointer<PlayerPresenter> guard{this};
    persistence_replacement_in_progress_ = true;
    const auto confirming = qScopeGuard([guard] {
        if (guard)
            guard->persistence_replacement_in_progress_ = false;
    });
    advancePersistenceWarningRevision();
    const auto consumed_revision = persistence_warning_revision_;
    bool succeeded = true;
    if (replace_preferences) {
        if (!preferences_autosave_->dirty())
            preferences_autosave_->scheduleRepair();
        if (!guard)
            return false;
        const bool authorized = preferences_autosave_->confirmFutureVersionReplacement();
        if (!guard)
            return false;
        if (authorized) {
            preferences_future_version_pending_ = false;
            preference_persistence_warning_ = PreferenceWarning::NoPreferenceWarning;
        } else {
            succeeded = false;
        }
    }
    if (replace_session) {
        if (!session_autosave_->dirty())
            session_autosave_->scheduleRepair();
        if (!guard)
            return false;
        const bool authorized = session_autosave_->confirmFutureVersionReplacement();
        if (!guard)
            return false;
        if (authorized) {
            session_future_version_pending_ = false;
            session_persistence_warning_ = PreferenceWarning::NoPreferenceWarning;
        } else {
            succeeded = false;
        }
    }
    projectPersistenceWarning();
    // A failed attempt also consumes consent, even if the warning is unchanged.
    if (!guard)
        return succeeded;
    if (persistence_warning_revision_ == consumed_revision)
        emit preferencePersistenceChanged();
    if (!guard)
        return succeeded;
    if (!succeeded)
        return false;
    reconcileTimer();
    return true;
}
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
bool PlayerPresenter::enterFrozenUnavailableScenario() {
    if (exiting_)
        return false;
    auto* const fixture_backend = fixtureBackend();
    if (fixture_backend == nullptr || !queueLoaded() || state_.state_id != QStringLiteral("playing") ||
        !fixture_backend->enterUnavailableScenario())
        return false;
    controller_.applyBackendSnapshot(backend_.snapshot());
    state_ = stateById(*fixture_bundle_, QStringLiteral("error"));
    reconcileTimer();
    emit stateChanged();
    emit playbackChanged();
    return true;
}
#endif

bool PlayerPresenter::executeErrorAction() {
    if (download_append_in_progress_)
        return false;
    if (!hasError() || exiting_)
        return false;
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        return errorActionId() == QStringLiteral("skip") && skipUnavailable();
#endif
    if (errorActionId() == QStringLiteral("recover-audio-output"))
        return recoverAudioOutput();
    return errorActionId() == QStringLiteral("open-audio") && openAudio();
}
bool PlayerPresenter::selectAudioOutput(const QString& id) {
    if (QThread::currentThread() != thread() || audio_output_selection_ == nullptr || exiting_ ||
        download_append_in_progress_)
        return false;
    const auto requested = id.toStdString();
    const bool changed = audio_output_selection_->selectedAudioOutputId() != requested;
    if (!audio_output_selection_->selectAudioOutput(requested))
        return false;

    const auto occurrence = local_queue_.currentOccurrenceId();
    const auto snapshot = backend_.snapshot();
    if (changed && occurrence && isAudioOutputError(snapshot.error)) {
        // Process the backend's queued output error before recovering the same track.
        QMetaObject::invokeMethod(
            this,
            [this, id, occurrence, generation = snapshot.generation] {
                if (!exiting_ && local_queue_.currentOccurrenceId() == occurrence &&
                    backend_.snapshot().generation == generation && selectedAudioOutputId() == id &&
                    isAudioOutputError(backend_.snapshot().error) &&
                    isAudioOutputError(controller_.confirmedSnapshot().error))
                    static_cast<void>(recoverAudioOutput());
            },
            Qt::QueuedConnection);
    }
    return true;
}
bool PlayerPresenter::recoverAudioOutput() {
    if (download_append_in_progress_)
        return false;
    const auto current = local_queue_.currentIndex();
    if (!current.has_value())
        return false;
    const auto position = currentSessionPosition();
    const bool restore_volume = hasSourceVolumeRecovery();
    const auto id = local_queue_.items()[*current].occurrence_id;
    if (!local_track_session_->open(local_queue_.items()[*current].source_path, LocalTrackStartIntent::RestorePaused))
        return false;
    const auto generation = local_track_session_->playbackGeneration();
    restored_position_ =
        RestoredPosition{.generation = generation, .target_ms = position, .source_volume_recovery = restore_volume};
    restored_eof_generation_.reset();
    occurrence_retry_.reset();
    if (!local_queue_.items()[*current].available)
        occurrence_retry_ = OccurrenceRetry{id, generation};
    source_import_needs_autoplay_ = false;
    source_import_autoplay_enabled_ = false;
    local_recovery_intent_.reset();
    artwork_cache_->beginGeneration(local_track_session_->metadataGeneration());
    controller_.applyBackendSnapshot(backend_.snapshot());
    sessionStructureDidChange();
    reconcileTimer();
    emit stateChanged();
    emit playbackChanged();
    if (backend_.snapshot().status != PlaybackStatus::Loading)
        handlePlaybackEvent({PlaybackEventType::SnapshotChanged, backend_.snapshot()});
    return true;
}
bool PlayerPresenter::skipUnavailable() {
    if (download_append_in_progress_)
        return false;
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (exiting_)
        return false;
    auto* const fixture_backend = fixtureBackend();
    if (fixture_backend == nullptr || state_.state_id != QStringLiteral("error") ||
        !fixture_backend->restoreInitialScenario())
        return false;
    controller_.applyBackendSnapshot(backend_.snapshot());
    state_ = stateById(*fixture_bundle_, QStringLiteral("playing"));
    state_.position_ms = backend_.snapshot().position_ms;
    state_.duration_ms = backend_.snapshot().duration_ms;
    state_.seekable = backend_.snapshot().seekable;
    reconcileTimer();
    emit stateChanged();
    emit playbackChanged();
    return true;
#else
    return false;
#endif
}
bool PlayerPresenter::advanceOneTick() {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
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
#else
    return false;
#endif
}
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
void PlayerPresenter::rejectNextSeekForTest() noexcept {
    if (auto* const fixture_backend = fixtureBackend(); fixture_backend != nullptr)
        fixture_backend->rejectNextSeek();
}
#endif

std::size_t PlayerPresenter::seekCommitAttemptCountForTest() const noexcept { return seek_commit_attempt_count_; }
bool PlayerPresenter::applicationPollingEnabled() const noexcept {
    return !download_append_in_progress_ && last_system_power_event_ != std::optional{SystemPowerEvent::Suspending};
}
void PlayerPresenter::reconcileTimer() {
    const bool application_needed =
        applicationPollingEnabled() &&
        (playlist_exit_deadline_.has_value() ||
         (uses_frozen_fixture_
              ? source_exit_drain_active_ ||
                    (!exit_committed_ && queueLoaded() &&
                     (controller_.confirmedSnapshot().status == PlaybackStatus::Playing || controller_.seekPending()))
              : source_exit_drain_active_ ||
                    (!exiting_ && (local_track_session_->metadataStatus() == LocalTrackMetadataStatus::Pending ||
                                   sourceImportActive()))));
    const bool preferences_needed = preferences_autosave_ != nullptr && preferences_autosave_->needsPolling();
    const bool session_needed = session_autosave_ != nullptr && session_autosave_->needsPolling();
    const bool needed = application_needed || preferences_needed || session_needed;
    int interval_ms = 25;
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (uses_frozen_fixture_)
        interval_ms = static_cast<int>(fixture_bundle_->fake_backend.snapshots.interval_ms);
#endif
    if (session_needed && !application_needed && !preferences_needed && !session_autosave_->writeActive()) {
        if (const auto wakeup = session_autosave_->nextWakeup(); wakeup.has_value()) {
            const auto now = session_now_();
            auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(*wakeup - now);
            if (now + delay < *wakeup)
                delay += std::chrono::milliseconds{1};
            delay = std::clamp(delay, std::chrono::milliseconds{1},
                               std::chrono::milliseconds{std::numeric_limits<int>::max()});
            interval_ms = static_cast<int>(delay.count());
        }
    }
    if (needed && timer_.interval() != interval_ms)
        timer_.setInterval(interval_ms);
    if (needed && !timer_.isActive())
        timer_.start();
    else if (!needed && timer_.isActive())
        timer_.stop();
}
bool PlayerPresenter::loadFrozenQueue() {
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    if (exiting_ || !uses_frozen_fixture_ || queue_model_.loaded() || fixture_bundle_->queue.isEmpty())
        return false;
    controller_.applyBackendSnapshot(backend_.snapshot());
    state_ = stateById(*fixture_bundle_, QStringLiteral("playing"));
    state_.position_ms = backend_.snapshot().position_ms;
    state_.duration_ms = backend_.snapshot().duration_ms;
    state_.seekable = backend_.snapshot().seekable;
    if (!queue_model_.loadOnce(fixture_bundle_->queue))
        return false;
    reconcileTimer();
    emit stateChanged();
    emit playbackChanged();
    return true;
#else
    return false;
#endif
}

} // namespace music_player::m0
