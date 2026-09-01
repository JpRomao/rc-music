#include "presentation/PlayerPresenter.hpp"

#include "m0/adapters/SingleWorkerLocalSourceDiscovery.hpp"
#include "presentation/ArtworkImageProvider.hpp"

#include <QDateTime>
#include <QMetaObject>
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
    return std::isfinite(preferences.volume) && preferences.volume >= 0.0 && preferences.volume <= 1.0 &&
           preferences.skip_backward_seconds >= 1 && preferences.skip_backward_seconds <= 300 &&
           preferences.skip_forward_seconds >= 1 && preferences.skip_forward_seconds <= 300;
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
    }
    return QStringLiteral("unknown");
}

QString metadataDiagnosticId(const MetadataDiagnosticCode code) {
    switch (code) {
    case MetadataDiagnosticCode::FieldTooLarge:
        return QStringLiteral("metadata_field_too_large");
    case MetadataDiagnosticCode::RetainedMetadataTooLarge:
        return QStringLiteral("metadata_total_too_large");
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

QString localPlaybackErrorCode(const PlaybackErrorCode error) {
    switch (error) {
    case PlaybackErrorCode::SourceUnavailable:
        return QStringLiteral("source_unavailable");
    case PlaybackErrorCode::InvalidMedia:
        return QStringLiteral("invalid_media");
    case PlaybackErrorCode::AccessDenied:
        return QStringLiteral("access_denied");
    case PlaybackErrorCode::NetworkFailure:
        return QStringLiteral("network_failure");
    case PlaybackErrorCode::BackendFailure:
        return QStringLiteral("backend_failure");
    case PlaybackErrorCode::None:
        return {};
    }
    return {};
}

QString localPlaybackErrorMessageKey(const PlaybackErrorCode error) {
    switch (error) {
    case PlaybackErrorCode::SourceUnavailable:
        return QStringLiteral("source_unavailable_error");
    case PlaybackErrorCode::InvalidMedia:
        return QStringLiteral("invalid_media_error");
    case PlaybackErrorCode::AccessDenied:
        return QStringLiteral("access_denied_error");
    case PlaybackErrorCode::None:
    case PlaybackErrorCode::NetworkFailure:
    case PlaybackErrorCode::BackendFailure:
        return QStringLiteral("backend_failure_error");
    }
    return QStringLiteral("backend_failure_error");
}

[[nodiscard]] std::optional<std::filesystem::path> localSourcePath(const QUrl& url) {
    if (!url.isValid() || !url.isLocalFile())
        return std::nullopt;
    const QString local_file = url.toLocalFile();
    if (local_file.isEmpty())
        return std::nullopt;
#ifdef Q_OS_WIN
    return std::filesystem::path{local_file.toStdWString()};
#else
    const QByteArray utf8 = local_file.toUtf8();
    return std::filesystem::path{std::string{utf8.constData(), static_cast<std::size_t>(utf8.size())}};
#endif
}

void addSaturated(std::uint64_t& target, const std::uint64_t increment) noexcept {
    target = increment > std::numeric_limits<std::uint64_t>::max() - target ? std::numeric_limits<std::uint64_t>::max()
                                                                            : target + increment;
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
    connect(&timer_, &QTimer::timeout, this, [this] {
        pollPersistence();
        if (!exit_committed_)
            static_cast<void>(advanceOneTick());
    });
}

PlayerPresenter::PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform,
                                 PlayerPresenterPreferences preferences, QObject* parent)
    : QObject(parent), bundle_(std::move(bundle)), navigation_items_(navigationItems(bundle_)), platform_(platform),
      tray_available_(platform.trayAvailable()),
      owned_backend_(std::make_unique<DeterministicPlaybackBackend>(backendConfig(bundle_))), backend_(*owned_backend_),
      controller_(backend_, platform, PlaybackSnapshot{}, preferences.initial), queue_model_(this),
      local_queue_model_(this), timer_(this), state_(stateById(bundle_, bundle_.default_state_id)),
      artwork_cache_(std::make_shared<ArtworkImageCache>()) {
    initializePersistedPreferences(preferences);
    timer_.setInterval(static_cast<int>(bundle_.fake_backend.snapshots.interval_ms));
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, [this] {
        pollPersistence();
        if (!exit_committed_)
            static_cast<void>(advanceOneTick());
    });
    reconcileTimer();
}

PlayerPresenter::PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform, PlaybackBackend& playback_backend,
                                 MetadataReader& metadata_reader, AudioFilePicker audio_file_picker, QObject* parent)
    : PlayerPresenter(std::move(bundle), platform, playback_backend, metadata_reader, std::move(audio_file_picker),
                      std::make_shared<ArtworkImageCache>(), parent) {}

PlayerPresenter::PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform, PlaybackBackend& playback_backend,
                                 MetadataReader& metadata_reader, AudioFilePicker audio_file_picker,
                                 PlayerPresenterPreferences preferences, QObject* parent)
    : PlayerPresenter(std::move(bundle), platform, playback_backend, metadata_reader, std::move(audio_file_picker),
                      std::make_shared<ArtworkImageCache>(), std::move(preferences), parent) {}

PlayerPresenter::PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform, PlaybackBackend& playback_backend,
                                 MetadataReader& metadata_reader, AudioFilePicker audio_file_picker,
                                 std::shared_ptr<ArtworkImageCache> artwork_cache, QObject* parent)
    : QObject(parent), bundle_(std::move(bundle)), navigation_items_(navigationItems(bundle_)), platform_(platform),
      tray_available_(platform.trayAvailable()), owned_backend_(), backend_(playback_backend),
      controller_(backend_, platform, backend_.snapshot()), queue_model_(this), local_queue_model_(this), timer_(this),
      state_(stateById(bundle_, bundle_.default_state_id)), audio_file_picker_(std::move(audio_file_picker)),
      local_track_session_(std::make_unique<LocalTrackSession>(backend_, metadata_reader)),
      artwork_cache_(std::move(artwork_cache)),
      owned_source_discovery_(std::make_unique<SingleWorkerLocalSourceDiscovery>()),
      source_discovery_(owned_source_discovery_.get()), uses_frozen_fixture_(false) {
    initializeLocalMode();
}

PlayerPresenter::PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform, PlaybackBackend& playback_backend,
                                 MetadataReader& metadata_reader, AudioFilePicker audio_file_picker,
                                 std::shared_ptr<ArtworkImageCache> artwork_cache,
                                 PlayerPresenterPreferences preferences, QObject* parent)
    : QObject(parent), bundle_(std::move(bundle)), navigation_items_(navigationItems(bundle_)), platform_(platform),
      tray_available_(platform.trayAvailable()), owned_backend_(), backend_(playback_backend),
      controller_(backend_, platform, backend_.snapshot(), preferences.initial), queue_model_(this),
      local_queue_model_(this), timer_(this), state_(stateById(bundle_, bundle_.default_state_id)),
      audio_file_picker_(std::move(audio_file_picker)),
      local_track_session_(std::make_unique<LocalTrackSession>(backend_, metadata_reader)),
      artwork_cache_(std::move(artwork_cache)),
      owned_source_discovery_(std::make_unique<SingleWorkerLocalSourceDiscovery>()),
      source_discovery_(owned_source_discovery_.get()), uses_frozen_fixture_(false) {
    initializeLocalMode(&preferences);
}

PlayerPresenter::PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform, PlaybackBackend& playback_backend,
                                 MetadataReader& metadata_reader, AudioFilePicker audio_file_picker,
                                 LocalSourceDiscovery& source_discovery, QObject* parent)
    : PlayerPresenter(std::move(bundle), platform, playback_backend, metadata_reader, std::move(audio_file_picker),
                      source_discovery, std::make_shared<ArtworkImageCache>(), parent) {}

PlayerPresenter::PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform, PlaybackBackend& playback_backend,
                                 MetadataReader& metadata_reader, AudioFilePicker audio_file_picker,
                                 LocalSourceDiscovery& source_discovery,
                                 std::shared_ptr<ArtworkImageCache> artwork_cache, QObject* parent)
    : QObject(parent), bundle_(std::move(bundle)), navigation_items_(navigationItems(bundle_)), platform_(platform),
      tray_available_(platform.trayAvailable()), owned_backend_(), backend_(playback_backend),
      controller_(backend_, platform, backend_.snapshot()), queue_model_(this), local_queue_model_(this), timer_(this),
      state_(stateById(bundle_, bundle_.default_state_id)), audio_file_picker_(std::move(audio_file_picker)),
      local_track_session_(std::make_unique<LocalTrackSession>(backend_, metadata_reader)),
      artwork_cache_(std::move(artwork_cache)), source_discovery_(&source_discovery), uses_frozen_fixture_(false) {
    initializeLocalMode();
}

void PlayerPresenter::initializeLocalMode(const PlayerPresenterPreferences* preferences) {
    if (!audio_file_picker_)
        throw std::invalid_argument{"audio file picker is required"};
    if (artwork_cache_ == nullptr)
        throw std::invalid_argument{"artwork image cache is required"};
    if (source_discovery_ == nullptr)
        throw std::invalid_argument{"local source discovery is required"};
    if (preferences == nullptr)
        initializeDefaults();
    else
        initializePersistedPreferences(*preferences);
    backend_.setEventCallback([this](const PlaybackEvent& event) {
        const PlaybackEvent copy = event;
        QMetaObject::invokeMethod(this, [this, copy] { handlePlaybackEvent(copy); }, Qt::QueuedConnection);
    });
    if (preferences != nullptr)
        initializePersistedSession(*preferences);
    timer_.setInterval(25);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, [this] {
        pollPersistence();
        if (!exiting_) {
            pollMetadata();
            pollSourceImport();
        }
    });
    reconcileTimer();
}

PlayerPresenter::~PlayerPresenter() {
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

void PlayerPresenter::initializePersistedPreferences(const PlayerPresenterPreferences& preferences) {
    if (preferences.autosave == nullptr)
        throw std::invalid_argument{"preferences autosave is required"};
    if (!preferences.now)
        throw std::invalid_argument{"preferences exit clock is required"};
    if (preferences.exit_flush_timeout < std::chrono::milliseconds::zero())
        throw std::invalid_argument{"preferences exit flush timeout cannot be negative"};
    if (!validPreferences(preferences.initial))
        invalidMapping("persisted preferences");
    for (const FixtureState& fixture_state : bundle_.states)
        static_cast<void>(mapPlaybackState(fixture_state.playback_state));
    static_cast<void>(stateById(bundle_, QStringLiteral("playing")));

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
        preference_warning_ = PreferenceWarning::NoPreferenceWarning;
        break;
    case PreferencesLoadStatus::RecoveredFromBackup:
        preference_warning_ = PreferenceWarning::RecoveredFromBackup;
        preferences_autosave_->scheduleRepair();
        break;
    case PreferencesLoadStatus::InvalidUsingDefaults:
        preference_warning_ = PreferenceWarning::UsingDefaults;
        break;
    case PreferencesLoadStatus::FutureVersion:
        preferences_future_version_pending_ = true;
        preference_warning_ = PreferenceWarning::FutureVersion;
        break;
    default:
        invalidMapping("preferences load status");
    }
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
    switch (session.load_status) {
    case SessionLoadStatus::Missing:
        return;
    case SessionLoadStatus::InvalidUsingDefaults:
        if (preference_warning_ == PreferenceWarning::NoPreferenceWarning)
            preference_warning_ = PreferenceWarning::UsingDefaults;
        return;
    case SessionLoadStatus::FutureVersion:
        session_future_version_pending_ = true;
        preference_warning_ = PreferenceWarning::FutureVersion;
        return;
    case SessionLoadStatus::Loaded:
        break;
    case SessionLoadStatus::RecoveredFromBackup:
        session_autosave_->scheduleRepair();
        if (preference_warning_ == PreferenceWarning::NoPreferenceWarning)
            preference_warning_ = PreferenceWarning::RecoveredFromBackup;
        break;
    default:
        throw std::invalid_argument{"unknown persisted session load status"};
    }

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
    SessionSnapshot result{.queue = local_queue_.snapshot(), .position_ms = 0};
    if (result.queue.current_occurrence_id.has_value() && local_track_session_ != nullptr &&
        local_track_session_->hasTrack()) {
        result.position_ms = restored_position_.has_value() &&
                                     restored_position_->generation == local_track_session_->playbackGeneration()
                                 ? restored_position_->target_ms
                                 : std::max(controller_.confirmedSnapshot().position_ms, std::int64_t{0});
    }
    return result;
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

void PlayerPresenter::cancelRestoredPositionForExplicitTransport() {
    discardStaleRestoredSessionState();
    if (!restored_position_.has_value())
        return;
    restored_position_.reset();
    restored_eof_generation_.reset();
    observeSessionPosition(true);
}

void PlayerPresenter::sessionStructureDidChange() {
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
    const auto position_ms = currentSessionSnapshot().position_ms;
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
    if (snapshot.status == PlaybackStatus::Error || snapshot.status == PlaybackStatus::Empty) {
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
        static_cast<void>(session_autosave_->checkpointConfirmedPosition(snapshot.position_ms));
        return;
    }
    if (!restored.seek_requested) {
        if (backend_.seekTo(restored.target_ms) == PlaybackCommandResult::Accepted)
            restored.seek_requested = true;
        else {
            restored_position_.reset();
            static_cast<void>(session_autosave_->checkpointConfirmedPosition(snapshot.position_ms));
        }
    }
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
    return hasError() ? localPlaybackErrorCode(controller_.confirmedSnapshot().error) : QString{};
}
QString PlayerPresenter::errorMessageKey() const {
    if (!hasError())
        return {};
    if (uses_frozen_fixture_)
        return messageKey();
    return localPlaybackErrorMessageKey(controller_.confirmedSnapshot().error);
}
QString PlayerPresenter::errorActionId() const {
    if (!hasError())
        return {};
    return uses_frozen_fixture_ ? primaryActionId() : QStringLiteral("open-audio");
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
const FixtureTrackDetails* PlayerPresenter::currentFixtureTrackDetails() const noexcept {
    const QueueItem* const track = currentTrack();
    return track != nullptr && track->occurrence_id == bundle_.track_details.occurrence_id ? &bundle_.track_details
                                                                                           : nullptr;
}
const TrackMetadata* PlayerPresenter::currentLocalTrackMetadata() const noexcept {
    if (uses_frozen_fixture_ || local_track_session_ == nullptr)
        return nullptr;
    const auto& metadata = local_track_session_->trackMetadata();
    return metadata.has_value() ? &*metadata : nullptr;
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
bool PlayerPresenter::detailsAvailable() const noexcept {
    return uses_frozen_fixture_ ? currentTrack() != nullptr
                                : local_track_session_ != nullptr && local_track_session_->hasTrack();
}
bool PlayerPresenter::hasDetailsFileName() const { return !detailsFileName().isEmpty(); }
QString PlayerPresenter::detailsFileName() const {
    if (uses_frozen_fixture_) {
        const QueueItem* const track = currentTrack();
        return track != nullptr ? track->file_name : QString{};
    }
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
    if (uses_frozen_fixture_) {
        const auto* const details = currentFixtureTrackDetails();
        return details != nullptr && details->size_bytes > 0;
    }
    const auto* const metadata = currentLocalTrackMetadata();
    return metadata != nullptr && metadata->file_size_bytes > 0;
}
QVariant PlayerPresenter::detailsFileSizeBytes() const {
    if (!hasDetailsFileSize())
        return {};
    if (uses_frozen_fixture_)
        return unsignedVariant(static_cast<std::uint64_t>(currentFixtureTrackDetails()->size_bytes));
    return unsignedVariant(static_cast<std::uint64_t>(currentLocalTrackMetadata()->file_size_bytes));
}
QString PlayerPresenter::detailsFileSizeBytesText() const {
    if (!hasDetailsFileSize())
        return {};
    if (uses_frozen_fixture_)
        return unsignedDecimal(static_cast<std::uint64_t>(currentFixtureTrackDetails()->size_bytes));
    return unsignedDecimal(static_cast<std::uint64_t>(currentLocalTrackMetadata()->file_size_bytes));
}
bool PlayerPresenter::hasDetailsModifiedUtc() const noexcept {
    if (uses_frozen_fixture_) {
        const auto* const details = currentFixtureTrackDetails();
        return details != nullptr && !details->modified_utc.isEmpty();
    }
    const auto* const metadata = currentLocalTrackMetadata();
    return metadata != nullptr && metadata->last_modified_time.has_value() &&
           !modifiedUtc(*metadata->last_modified_time).isEmpty();
}
QString PlayerPresenter::detailsModifiedUtc() const {
    if (!hasDetailsModifiedUtc())
        return {};
    return uses_frozen_fixture_ ? currentFixtureTrackDetails()->modified_utc
                                : modifiedUtc(*currentLocalTrackMetadata()->last_modified_time);
}
bool PlayerPresenter::hasDetailsTitle() const { return !detailsTitle().isEmpty(); }
QString PlayerPresenter::detailsTitle() const {
    if (uses_frozen_fixture_) {
        const QueueItem* const track = currentTrack();
        return track != nullptr ? track->title : QString{};
    }
    const auto* const metadata = currentLocalTrackMetadata();
    return metadata != nullptr && metadata->tags.title.has_value() ? fromUtf8(*metadata->tags.title) : QString{};
}
bool PlayerPresenter::hasDetailsArtist() const { return !detailsArtist().isEmpty(); }
QString PlayerPresenter::detailsArtist() const {
    if (uses_frozen_fixture_) {
        const QueueItem* const track = currentTrack();
        return track != nullptr ? track->artist : QString{};
    }
    const auto* const metadata = currentLocalTrackMetadata();
    return metadata != nullptr && metadata->tags.artist.has_value() ? fromUtf8(*metadata->tags.artist) : QString{};
}
bool PlayerPresenter::hasDetailsAlbum() const { return !detailsAlbum().isEmpty(); }
QString PlayerPresenter::detailsAlbum() const {
    if (uses_frozen_fixture_) {
        const QueueItem* const track = currentTrack();
        return track != nullptr ? track->album : QString{};
    }
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
    if (uses_frozen_fixture_) {
        const QueueItem* const track = currentTrack();
        return track != nullptr && track->duration_ms.has_value() && *track->duration_ms > 0;
    }
    return localDurationMs().has_value();
}
QVariant PlayerPresenter::detailsDurationMs() const {
    if (!hasDetailsDuration())
        return {};
    if (uses_frozen_fixture_)
        return QVariant::fromValue<qint64>(*currentTrack()->duration_ms);
    return QVariant::fromValue<qint64>(*localDurationMs());
}
bool PlayerPresenter::hasDetailsContainer() const { return !detailsContainer().isEmpty(); }
QString PlayerPresenter::detailsContainer() const {
    if (uses_frozen_fixture_) {
        const auto* const details = currentFixtureTrackDetails();
        return details != nullptr ? details->container : QString{};
    }
    const auto* const metadata = currentLocalTrackMetadata();
    return metadata != nullptr ? fromUtf8(metadata->audio.container) : QString{};
}
bool PlayerPresenter::hasDetailsCodec() const { return !detailsCodec().isEmpty(); }
QString PlayerPresenter::detailsCodec() const {
    if (uses_frozen_fixture_) {
        const auto* const details = currentFixtureTrackDetails();
        return details != nullptr ? details->codec : QString{};
    }
    const auto* const metadata = currentLocalTrackMetadata();
    return metadata != nullptr ? fromUtf8(metadata->audio.codec) : QString{};
}
bool PlayerPresenter::hasDetailsBitrate() const noexcept {
    if (uses_frozen_fixture_) {
        const auto* const details = currentFixtureTrackDetails();
        return details != nullptr && details->bitrate_bps > 0;
    }
    const auto* const metadata = currentLocalTrackMetadata();
    return metadata != nullptr && metadata->audio.bitrate_kbps.has_value() && *metadata->audio.bitrate_kbps > 0;
}
QVariant PlayerPresenter::detailsBitrateBps() const {
    if (!hasDetailsBitrate())
        return {};
    if (uses_frozen_fixture_)
        return unsignedVariant(static_cast<std::uint64_t>(currentFixtureTrackDetails()->bitrate_bps));
    return unsignedVariant(static_cast<std::uint64_t>(*currentLocalTrackMetadata()->audio.bitrate_kbps) * 1'000U);
}
bool PlayerPresenter::hasDetailsSampleRate() const noexcept {
    if (uses_frozen_fixture_) {
        const auto* const details = currentFixtureTrackDetails();
        return details != nullptr && details->sample_rate_hz > 0;
    }
    const auto* const metadata = currentLocalTrackMetadata();
    return metadata != nullptr && metadata->audio.sample_rate_hz.has_value() && *metadata->audio.sample_rate_hz > 0;
}
QVariant PlayerPresenter::detailsSampleRateHz() const {
    if (!hasDetailsSampleRate())
        return {};
    if (uses_frozen_fixture_)
        return unsignedVariant(static_cast<std::uint64_t>(currentFixtureTrackDetails()->sample_rate_hz));
    return unsignedVariant(*currentLocalTrackMetadata()->audio.sample_rate_hz);
}
bool PlayerPresenter::hasDetailsChannels() const noexcept {
    if (uses_frozen_fixture_) {
        const auto* const details = currentFixtureTrackDetails();
        return details != nullptr && details->channels > 0;
    }
    const auto* const metadata = currentLocalTrackMetadata();
    return metadata != nullptr && metadata->audio.channels.has_value() && *metadata->audio.channels > 0;
}
QVariant PlayerPresenter::detailsChannels() const {
    if (!hasDetailsChannels())
        return {};
    if (uses_frozen_fixture_)
        return unsignedVariant(static_cast<std::uint64_t>(currentFixtureTrackDetails()->channels));
    return unsignedVariant(*currentLocalTrackMetadata()->audio.channels);
}
bool PlayerPresenter::hasDetailsBitDepth() const noexcept {
    if (uses_frozen_fixture_) {
        const auto* const details = currentFixtureTrackDetails();
        return details != nullptr && details->bit_depth.has_value() && *details->bit_depth > 0;
    }
    const auto* const metadata = currentLocalTrackMetadata();
    return metadata != nullptr && metadata->audio.bit_depth.has_value() && *metadata->audio.bit_depth > 0;
}
QVariant PlayerPresenter::detailsBitDepth() const {
    if (!hasDetailsBitDepth())
        return {};
    if (uses_frozen_fixture_)
        return unsignedVariant(static_cast<std::uint64_t>(*currentFixtureTrackDetails()->bit_depth));
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
PlayerPresenter::SourceImportState PlayerPresenter::sourceImportState() const noexcept { return source_import_state_; }
bool PlayerPresenter::sourceImportActive() const noexcept {
    return source_import_state_ == SourceImportState::Discovering ||
           source_import_state_ == SourceImportState::Cancelling;
}
qulonglong PlayerPresenter::sourceImportAcceptedCount() const noexcept { return source_import_accepted_count_; }
qulonglong PlayerPresenter::sourceImportIssueCount() const noexcept { return source_import_issue_count_; }
qulonglong PlayerPresenter::sourceImportInspectedCount() const noexcept { return source_import_inspected_count_; }
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
bool PlayerPresenter::exitCommitted() const noexcept { return exit_committed_; }
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
    if (selected_paths.empty())
        return false;
    auto candidate = local_queue_;
    const std::size_t first_inserted = candidate.size();
    const bool queue_was_empty = candidate.empty();
    if (!candidate.append(selected_paths)) {
        rejectSourceImport(static_cast<std::uint64_t>(selected_paths.size()));
        return false;
    }
    if (!local_track_session_->open(candidate.items()[first_inserted].source_path, LocalTrackStartIntent::Autoplay)) {
        rejectSourceImport(static_cast<std::uint64_t>(selected_paths.size()));
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
    sessionStructureDidChange();
    reconcileTimer();
    emit stateChanged();
    emit playbackChanged();
    return true;
}
bool PlayerPresenter::addAudio() {
    if (uses_frozen_fixture_ || exiting_)
        return false;

    const auto selected_paths = audio_file_picker_();
    if (selected_paths.empty())
        return false;
    auto candidate = local_queue_;
    const std::size_t first_inserted = candidate.size();
    if (!candidate.append(selected_paths)) {
        rejectSourceImport(static_cast<std::uint64_t>(selected_paths.size()));
        return false;
    }

    const bool needs_current_track = !local_track_session_->hasTrack();
    if (needs_current_track) {
        if (!local_track_session_->open(candidate.items()[first_inserted].source_path,
                                        LocalTrackStartIntent::Autoplay)) {
            rejectSourceImport(static_cast<std::uint64_t>(selected_paths.size()));
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
    }
    sessionStructureDidChange();
    reconcileTimer();
    emit stateChanged();
    emit playbackChanged();
    if (needs_current_track)
        emit queueFocusRequested(static_cast<int>(first_inserted));
    return true;
}
bool PlayerPresenter::importDroppedUrls(const QList<QUrl>& urls, const int insertion_index) {
    if (uses_frozen_fixture_ || exiting_ || source_discovery_ == nullptr || sourceImportActive())
        return false;
    source_import_occurrences_.clear();
    source_import_autoplay_enabled_ = false;
    source_import_needs_autoplay_ = false;
    if (insertion_index < 0 || static_cast<std::size_t>(insertion_index) > local_queue_.size()) {
        rejectSourceImport(1);
        return false;
    }

    std::vector<std::filesystem::path> roots;
    roots.reserve(static_cast<std::size_t>(urls.size()));
    std::uint64_t boundary_issue_count = 0;
    for (const QUrl& url : urls) {
        auto source_path = localSourcePath(url);
        if (!source_path.has_value()) {
            ++boundary_issue_count;
            continue;
        }
        roots.push_back(std::move(*source_path));
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

    const auto index = static_cast<std::size_t>(insertion_index);
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
    if (uses_frozen_fixture_ || exiting_)
        return false;
    const auto target = localOccurrenceIndex(occurrence_id);
    if (!target.has_value() || !local_queue_.items()[*target].available)
        return false;

    const auto current = local_queue_.currentIndex();
    if (current == target) {
        switch (controller_.confirmedSnapshot().status) {
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
    sessionStructureDidChange();
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
    if (uses_frozen_fixture_ || exiting_ || (sourceImportActive() && !cancelSourceImport()) || local_queue_.empty() ||
        !local_track_session_->unload()) {
        return false;
    }
    if (!local_queue_.clear())
        throw std::logic_error{"a non-empty local queue could not be cleared"};
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
    if (exiting_) {
        if (local_track_session_ != nullptr &&
            event.snapshot.generation == local_track_session_->playbackGeneration() &&
            (!deferred_playback_event_.has_value() || deferred_playback_event_->type != PlaybackEventType::Ended ||
             event.type == PlaybackEventType::Ended)) {
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

    controller_.applyBackendSnapshot(event.snapshot);
    reconcileRestoredPosition();
    bool state_projection_changed = false;
    bool source_import_changed = false;
    if (result == LocalTrackPlaybackEventResult::OccurrenceUnavailable) {
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
            if (imported_occurrence) {
                addSaturated(source_import_issue_count_, 1);
                if (source_import_state_ == SourceImportState::Completed)
                    source_import_state_ = SourceImportState::CompletedWithIssues;
                source_import_changed = true;
            }
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
        if (!restoring_session) {
            if (const auto next = nextAvailableLocalIndex();
                next.has_value() && loadLocalOccurrence(*next, start_intent)) {
                state_projection_changed = true;
                recovered = true;
            }
        }
        if (imported_occurrence && source_import_state_ == SourceImportState::Discovering &&
            source_import_autoplay_enabled_ && !recovered)
            source_import_needs_autoplay_ = true;
    } else if (result == LocalTrackPlaybackEventResult::Ended && !restoring_session && !suppress_restored_end) {
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
    if (result == LocalTrackPlaybackEventResult::Ended &&
        restored_eof_generation_ == std::optional{event.snapshot.generation}) {
        restored_eof_generation_.reset();
    }

    if (state_projection_changed)
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
    if (state_projection_changed || playback_changed)
        emit stateChanged();
    if (playback_changed)
        emit playbackChanged();
    if (source_import_changed)
        emit sourceImportChanged();
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
void PlayerPresenter::pollSourceImport() {
    if (!sourceImportActive() || source_discovery_ == nullptr || exiting_)
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
                addSaturated(source_import_issue_count_, 1);
            }
        }
    }

    sessionStructureDidChange();
    emit stateChanged();
    emit playbackChanged();
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

    if (session_autosave_ != nullptr) {
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
        reconcileTimer();
        return false;
    }
    if (preferences_pending || session_pending) {
        deferred_playback_event_.reset();
        exiting_ = true;
        window_notice_ = WindowNotice::NoNotice;
        preference_exit_deadline_ = preference_now_() + preference_exit_flush_timeout_;
        emit lifecycleChanged();
        reconcileTimer();
        pollPersistence();
        return true;
    }

    completeExit();
    return true;
}

void PlayerPresenter::completeExit() {
    preference_exit_deadline_.reset();
    deferred_playback_event_.reset();
    if (sourceImportActive() && source_discovery_ != nullptr) {
        static_cast<void>(source_discovery_->cancel(source_import_generation_));
        finishSourceImport(SourceImportState::Cancelled);
    }
    const bool changed = !exiting_ || !exit_committed_ || window_notice_ != WindowNotice::NoNotice;
    exiting_ = true;
    exit_committed_ = true;
    window_notice_ = WindowNotice::NoNotice;
    timer_.stop();
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
void PlayerPresenter::refreshPlatformAvailability() {
    const bool available = platform_.trayAvailable();
    if (available == tray_available_)
        return;
    tray_available_ = available;
    emit platformChanged();
}
bool PlayerPresenter::togglePlayback() {
    if (exiting_)
        return false;
    const PlaybackSnapshot before = controller_.confirmedSnapshot();
    if (uses_frozen_fixture_ && before.status != PlaybackStatus::Playing && before.duration_ms > 0 &&
        before.position_ms >= before.duration_ms)
        return false;
    if (!controller_.togglePlayback())
        return false;
    controller_.applyBackendSnapshot(backend_.snapshot());
    cancelRestoredPositionForExplicitTransport();
    const bool changed = before != controller_.confirmedSnapshot();
    if (changed) {
        reconcileTimer();
        emit playbackChanged();
    }
    return uses_frozen_fixture_ ? changed : true;
}
bool PlayerPresenter::skipBackward() {
    if (exiting_ || !controller_.skipBackward())
        return false;
    cancelRestoredPositionForExplicitTransport();
    reconcileTimer();
    emit playbackChanged();
    return true;
}
bool PlayerPresenter::skipForward() {
    if (exiting_ || !controller_.skipForward())
        return false;
    cancelRestoredPositionForExplicitTransport();
    reconcileTimer();
    emit playbackChanged();
    return true;
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
    for (std::size_t index = *current + 1; index < local_queue_.size(); ++index) {
        if (local_queue_.items()[index].available)
            return index;
    }
    return std::nullopt;
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
        sessionStructureDidChange();
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
    if (exiting_ || !controller_.seekPreviewActive() || controller_.seekPending())
        return false;
    const auto before = controller_.displayedPositionMs();
    controller_.updateSeekPreview(position_ms);
    if (before != controller_.displayedPositionMs())
        emit playbackChanged();
    return true;
}
bool PlayerPresenter::commitSeekPreview() {
    if (exiting_)
        return false;
    ++seek_commit_attempt_count_;
    if (!controller_.seekPreviewActive() || controller_.seekPending())
        return false;
    if (!controller_.commitSeekPreview())
        return false;
    cancelRestoredPositionForExplicitTransport();
    reconcileTimer();
    emit playbackChanged();
    return true;
}
bool PlayerPresenter::setVolume(const double volume) {
    if (exiting_ || volume == controller_.preferences().volume || !controller_.setVolume(volume))
        return false;
    preferenceDidChange();
    return true;
}
bool PlayerPresenter::toggleMute() {
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

PlayerPresenter::PreferenceWarning PlayerPresenter::preferenceWarning() const noexcept { return preference_warning_; }

void PlayerPresenter::preferenceDidChange() {
    if (preferences_autosave_ != nullptr)
        static_cast<void>(preferences_autosave_->schedule(preferences()));
    reconcileTimer();
    emit preferencesChanged();
}

void PlayerPresenter::pollPreferencePersistence() { pollPersistence(); }

void PlayerPresenter::pollPersistence() {
    if (preferences_autosave_ != nullptr)
        preferences_autosave_->poll();
    if (session_autosave_ != nullptr)
        session_autosave_->poll();
    synchronizePreferenceWarning();

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
            if ((!preferences_clean || !session_clean) && preference_warning_ != PreferenceWarning::SaveFailed) {
                preference_warning_ = PreferenceWarning::SaveFailed;
                emit preferencePersistenceChanged();
            }
            cancelPendingExit();
            return;
        }
    }
    reconcileTimer();
}

void PlayerPresenter::synchronizePreferenceWarning() {
    PreferenceWarning next = preference_warning_;
    const PreferencesAutosaveNotice preferences_notice =
        preferences_autosave_ == nullptr ? PreferencesAutosaveNotice::None : preferences_autosave_->notice();
    const SessionAutosaveNotice session_notice =
        session_autosave_ == nullptr ? SessionAutosaveNotice::None : session_autosave_->notice();
    const bool future_version = preferences_future_version_pending_ || session_future_version_pending_ ||
                                preferences_notice == PreferencesAutosaveNotice::FutureVersionBlocked ||
                                session_notice == SessionAutosaveNotice::FutureVersionBlocked;
    const bool save_failed = preferences_notice == PreferencesAutosaveNotice::SaveFailed ||
                             session_notice == SessionAutosaveNotice::SaveFailed ||
                             session_notice == SessionAutosaveNotice::SaveIncomplete;
    const bool all_clean =
        (preferences_autosave_ == nullptr ||
         (!preferences_autosave_->dirty() && !preferences_autosave_->writeActive())) &&
        (session_autosave_ == nullptr || (!session_autosave_->dirty() && !session_autosave_->writeActive()));

    if (future_version)
        next = PreferenceWarning::FutureVersion;
    else if (save_failed)
        next = PreferenceWarning::SaveFailed;
    else if (all_clean && (next == PreferenceWarning::SaveFailed || next == PreferenceWarning::FutureVersion))
        next = PreferenceWarning::NoPreferenceWarning;
    if (next == preference_warning_)
        return;
    preference_warning_ = next;
    emit preferencePersistenceChanged();
}

bool PlayerPresenter::dismissPreferenceWarning() {
    if (exiting_ || (preference_warning_ != PreferenceWarning::RecoveredFromBackup &&
                     preference_warning_ != PreferenceWarning::UsingDefaults)) {
        return false;
    }
    preference_warning_ = PreferenceWarning::NoPreferenceWarning;
    emit preferencePersistenceChanged();
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

bool PlayerPresenter::confirmPreferenceReplacement() {
    if (exiting_ || preference_warning_ != PreferenceWarning::FutureVersion)
        return false;

    bool attempted = false;
    bool succeeded = true;
    if (preferences_autosave_ != nullptr &&
        (preferences_future_version_pending_ ||
         preferences_autosave_->notice() == PreferencesAutosaveNotice::FutureVersionBlocked)) {
        attempted = true;
        if (!preferences_autosave_->dirty())
            preferences_autosave_->scheduleRepair();
        if (preferences_autosave_->confirmFutureVersionReplacement())
            preferences_future_version_pending_ = false;
        else
            succeeded = false;
    }
    if (session_autosave_ != nullptr && (session_future_version_pending_ ||
                                         session_autosave_->notice() == SessionAutosaveNotice::FutureVersionBlocked)) {
        attempted = true;
        if (!session_autosave_->dirty())
            session_autosave_->scheduleRepair();
        if (session_autosave_->confirmFutureVersionReplacement())
            session_future_version_pending_ = false;
        else
            succeeded = false;
    }
    if (!attempted || !succeeded)
        return false;
    preference_warning_ = PreferenceWarning::NoPreferenceWarning;
    emit preferencePersistenceChanged();
    reconcileTimer();
    return true;
}
bool PlayerPresenter::enterFrozenUnavailableScenario() {
    if (exiting_)
        return false;
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
bool PlayerPresenter::executeErrorAction() {
    if (!hasError() || exiting_)
        return false;
    if (uses_frozen_fixture_)
        return errorActionId() == QStringLiteral("skip") && skipUnavailable();
    return errorActionId() == QStringLiteral("open-audio") && openAudio();
}
bool PlayerPresenter::skipUnavailable() {
    if (exiting_)
        return false;
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
    const bool application_needed =
        uses_frozen_fixture_
            ? !exit_committed_ && queueLoaded() &&
                  (controller_.confirmedSnapshot().status == PlaybackStatus::Playing || controller_.seekPending())
            : !exiting_ &&
                  (local_track_session_->metadataStatus() == LocalTrackMetadataStatus::Pending || sourceImportActive());
    const bool preferences_needed = preferences_autosave_ != nullptr && preferences_autosave_->needsPolling();
    const bool session_needed = session_autosave_ != nullptr && session_autosave_->needsPolling();
    const bool needed = application_needed || preferences_needed || session_needed;
    int interval_ms = uses_frozen_fixture_ ? static_cast<int>(bundle_.fake_backend.snapshots.interval_ms) : 25;
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
    if (exiting_ || !uses_frozen_fixture_ || queue_model_.loaded() || bundle_.queue.isEmpty())
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
