#pragma once

#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
#include "m0/adapters/DeterministicPlaybackBackend.hpp"
#endif
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
#include "m0/adapters/fixture/FixtureRepository.hpp"
#endif
#include "m0/application/LocalTrackSession.hpp"
#include "m0/application/PlayerController.hpp"
#include "m0/application/PreferencesAutosave.hpp"
#include "m0/application/SessionAutosave.hpp"
#include "m0/domain/ListeningRoom.hpp"
#include "m0/domain/LocalFileQueue.hpp"
#include "m0/domain/QueueNavigation.hpp"
#include "m0/ports/AudioFileLocator.hpp"
#include "m0/ports/AudioFilesPicker.hpp"
#include "m0/ports/LocalSourceDiscovery.hpp"
#include "m0/ports/RoomHostPlayer.hpp"
#include "m0/ports/SystemPowerEvents.hpp"
#include "m0/presentation/LocalQueueListModel.hpp"
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
#include "m0/presentation/QueueListModel.hpp"
#endif

#include <QAbstractItemModel>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <unordered_set>
#include <vector>

namespace music_player::m0 {
class ArtworkImageCache;
class AudioOutputSelection;
class PlayerDownloadImport;
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
namespace experiment {
class PlayerPresenterScenarioControl;
}
#endif

namespace test {
struct PlayerPresenterTestAccess;
}

struct PlayerPresenterSession final {
    SessionSnapshot initial{};
    SessionLoadStatus load_status{SessionLoadStatus::Missing};
    SessionAutosave* autosave{nullptr};
    std::function<SessionAutosave::TimePoint()> now{[] { return std::chrono::steady_clock::now(); }};
};

struct PlayerPresenterPreferences final {
    using TimePoint = std::chrono::steady_clock::time_point;

    Preferences initial{};
    PreferencesLoadStatus load_status{PreferencesLoadStatus::Missing};
    PreferencesAutosave* autosave{nullptr};
    std::chrono::milliseconds exit_flush_timeout{2'000};
    std::function<TimePoint()> now{[] { return std::chrono::steady_clock::now(); }};
    PlayerPresenterSession session{};
};

enum class ExitPreparationState { Pending, Ready, Failed };

// Listening-room seam over session persistence (ADR-0022, S7.3). Every member is optional.
// filter maps the live session before it reaches the autosave (host: withoutGuestItems; guest:
// sessionForAutosave). While hold_position returns true, confirmed positions are not fed to the
// autosave. before_exit runs once per exit decision, before the final session save, so the room is
// left (guest) or closed (host) while the queue can still change.
struct PlayerPresenterRoomSessionGate final {
    std::function<SessionSnapshot(SessionSnapshot)> filter{};
    std::function<bool()> hold_position{};
    std::function<void()> before_exit{};
    // S9.1: true while a guest follows a room on the shared player (until its pre-room session is back).
    // Opening, adding, importing and appending sources are refused meanwhile.
    std::function<bool()> blocks_local_sources{};
};

// One optional application-thread participant. begin is irreversible for the
// participant, while a failed/timed-out player exit remains locally reversible.
// Polling Ready proves both durable preparation and physical worker drain.
struct ExitPreparationCallbacks final {
    std::function<void()> begin{};
    std::function<ExitPreparationState()> poll{};
};

class PlayerPresenter final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(PlayerPresenter)
    QML_UNCREATABLE("PlayerPresenter is provided by the application bootstrap")
    Q_PROPERTY(QString stateId READ stateId NOTIFY stateChanged)
    Q_PROPERTY(PlaybackState playbackState READ playbackState NOTIFY playbackChanged)
    Q_PROPERTY(bool queueLoaded READ queueLoaded NOTIFY stateChanged)
    Q_PROPERTY(bool hasCurrentOccurrence READ hasCurrentOccurrence NOTIFY stateChanged)
    Q_PROPERTY(QString currentOccurrenceId READ currentOccurrenceId NOTIFY stateChanged)
    Q_PROPERTY(QString retryingOccurrenceId READ retryingOccurrenceId NOTIFY stateChanged)
    Q_PROPERTY(bool canRetryCurrentOccurrence READ canRetryCurrentOccurrence NOTIFY stateChanged)
    Q_PROPERTY(bool fileLocationAvailable READ fileLocationAvailable NOTIFY stateChanged)
    Q_PROPERTY(QString locatingOccurrenceId READ locatingOccurrenceId NOTIFY stateChanged)
    Q_PROPERTY(QString fileLocationErrorOccurrenceId READ fileLocationErrorOccurrenceId NOTIFY stateChanged)
    Q_PROPERTY(bool canLocateCurrentOccurrence READ canLocateCurrentOccurrence NOTIFY stateChanged)
    Q_PROPERTY(bool selectingAudioFiles READ selectingAudioFiles NOTIFY stateChanged)
    Q_PROPERTY(bool audioFileSelectionFailed READ audioFileSelectionFailed NOTIFY stateChanged)
    Q_PROPERTY(int currentQueueIndex READ currentQueueIndex NOTIFY stateChanged)
    Q_PROPERTY(bool hasContextOccurrence READ hasContextOccurrence NOTIFY stateChanged)
    Q_PROPERTY(QString contextOccurrenceId READ contextOccurrenceId NOTIFY stateChanged)
    Q_PROPERTY(bool hasPosition READ hasPosition NOTIFY stateChanged)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY playbackChanged)
    Q_PROPERTY(qint64 confirmedPositionMs READ confirmedPositionMs NOTIFY playbackChanged)
    Q_PROPERTY(bool seekPreviewActive READ seekPreviewActive NOTIFY playbackChanged)
    Q_PROPERTY(bool seekPending READ seekPending NOTIFY playbackChanged)
    Q_PROPERTY(bool hasDuration READ hasDuration NOTIFY playbackChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY playbackChanged)
    Q_PROPERTY(bool seekable READ seekable NOTIFY playbackChanged)
    Q_PROPERTY(bool canGoPrevious READ canGoPrevious NOTIFY playbackChanged)
    Q_PROPERTY(bool canGoNext READ canGoNext NOTIFY playbackChanged)
    Q_PROPERTY(QString assetId READ assetId NOTIFY stateChanged)
    Q_PROPERTY(bool hasMessage READ hasMessage NOTIFY stateChanged)
    Q_PROPERTY(QString messageKey READ messageKey NOTIFY stateChanged)
    Q_PROPERTY(QString primaryActionId READ primaryActionId NOTIFY stateChanged)
    Q_PROPERTY(bool hasError READ hasError NOTIFY stateChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessageKey READ errorMessageKey NOTIFY stateChanged)
    Q_PROPERTY(QString errorActionId READ errorActionId NOTIFY stateChanged)
    Q_PROPERTY(bool hasCurrentTrack READ hasCurrentTrack NOTIFY stateChanged)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY stateChanged)
    Q_PROPERTY(QString currentArtist READ currentArtist NOTIFY stateChanged)
    Q_PROPERTY(QString currentAlbum READ currentAlbum NOTIFY stateChanged)
    Q_PROPERTY(QUrl currentArtworkSource READ currentArtworkSource NOTIFY stateChanged)
    Q_PROPERTY(bool detailsAvailable READ detailsAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool hasDetailsFileName READ hasDetailsFileName NOTIFY stateChanged)
    Q_PROPERTY(QString detailsFileName READ detailsFileName NOTIFY stateChanged)
    Q_PROPERTY(bool hasDetailsFilePath READ hasDetailsFilePath NOTIFY stateChanged)
    Q_PROPERTY(QString detailsFilePath READ detailsFilePath NOTIFY stateChanged)
    Q_PROPERTY(bool hasDetailsFileSize READ hasDetailsFileSize NOTIFY stateChanged)
    Q_PROPERTY(QVariant detailsFileSizeBytes READ detailsFileSizeBytes NOTIFY stateChanged)
    Q_PROPERTY(QString detailsFileSizeBytesText READ detailsFileSizeBytesText NOTIFY stateChanged)
    Q_PROPERTY(bool hasDetailsModifiedUtc READ hasDetailsModifiedUtc NOTIFY stateChanged)
    Q_PROPERTY(QString detailsModifiedUtc READ detailsModifiedUtc NOTIFY stateChanged)
    Q_PROPERTY(bool hasDetailsTitle READ hasDetailsTitle NOTIFY stateChanged)
    Q_PROPERTY(QString detailsTitle READ detailsTitle NOTIFY stateChanged)
    Q_PROPERTY(bool hasDetailsArtist READ hasDetailsArtist NOTIFY stateChanged)
    Q_PROPERTY(QString detailsArtist READ detailsArtist NOTIFY stateChanged)
    Q_PROPERTY(bool hasDetailsAlbum READ hasDetailsAlbum NOTIFY stateChanged)
    Q_PROPERTY(QString detailsAlbum READ detailsAlbum NOTIFY stateChanged)
    Q_PROPERTY(bool hasDetailsTrackNumber READ hasDetailsTrackNumber NOTIFY stateChanged)
    Q_PROPERTY(QVariant detailsTrackNumber READ detailsTrackNumber NOTIFY stateChanged)
    Q_PROPERTY(bool hasDetailsYear READ hasDetailsYear NOTIFY stateChanged)
    Q_PROPERTY(QVariant detailsYear READ detailsYear NOTIFY stateChanged)
    Q_PROPERTY(bool hasDetailsDuration READ hasDetailsDuration NOTIFY stateChanged)
    Q_PROPERTY(QVariant detailsDurationMs READ detailsDurationMs NOTIFY stateChanged)
    Q_PROPERTY(bool hasDetailsContainer READ hasDetailsContainer NOTIFY stateChanged)
    Q_PROPERTY(QString detailsContainer READ detailsContainer NOTIFY stateChanged)
    Q_PROPERTY(bool hasDetailsCodec READ hasDetailsCodec NOTIFY stateChanged)
    Q_PROPERTY(QString detailsCodec READ detailsCodec NOTIFY stateChanged)
    Q_PROPERTY(bool hasDetailsBitrate READ hasDetailsBitrate NOTIFY stateChanged)
    Q_PROPERTY(QVariant detailsBitrateBps READ detailsBitrateBps NOTIFY stateChanged)
    Q_PROPERTY(bool hasDetailsSampleRate READ hasDetailsSampleRate NOTIFY stateChanged)
    Q_PROPERTY(QVariant detailsSampleRateHz READ detailsSampleRateHz NOTIFY stateChanged)
    Q_PROPERTY(bool hasDetailsChannels READ hasDetailsChannels NOTIFY stateChanged)
    Q_PROPERTY(QVariant detailsChannels READ detailsChannels NOTIFY stateChanged)
    Q_PROPERTY(bool hasDetailsBitDepth READ hasDetailsBitDepth NOTIFY stateChanged)
    Q_PROPERTY(QVariant detailsBitDepth READ detailsBitDepth NOTIFY stateChanged)
    Q_PROPERTY(bool hasDetailsDiagnostics READ hasDetailsDiagnostics NOTIFY stateChanged)
    Q_PROPERTY(QVariantList detailsDiagnostics READ detailsDiagnostics NOTIFY stateChanged)
    Q_PROPERTY(bool hasContextTrack READ hasContextTrack NOTIFY stateChanged)
    Q_PROPERTY(QString contextTitle READ contextTitle NOTIFY stateChanged)
    Q_PROPERTY(QString contextArtist READ contextArtist NOTIFY stateChanged)
    Q_PROPERTY(QString contextAlbum READ contextAlbum NOTIFY stateChanged)
    Q_PROPERTY(QAbstractItemModel* queueModel READ queueModelObject CONSTANT)
    Q_PROPERTY(SourceImportState sourceImportState READ sourceImportState NOTIFY sourceImportChanged)
    Q_PROPERTY(bool sourceImportActive READ sourceImportActive NOTIFY sourceImportChanged)
    Q_PROPERTY(qulonglong sourceImportAcceptedCount READ sourceImportAcceptedCount NOTIFY sourceImportChanged)
    Q_PROPERTY(qulonglong sourceImportIssueCount READ sourceImportIssueCount NOTIFY sourceImportChanged)
    Q_PROPERTY(qulonglong sourceImportInspectedCount READ sourceImportInspectedCount NOTIFY sourceImportChanged)
    Q_PROPERTY(double volume READ volume NOTIFY preferencesChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY preferencesChanged)
    Q_PROPERTY(bool audioOutputSelectionSupported READ audioOutputSelectionSupported CONSTANT)
    Q_PROPERTY(QVariantList audioOutputDevices READ audioOutputDevices NOTIFY audioOutputsChanged)
    Q_PROPERTY(QString selectedAudioOutputId READ selectedAudioOutputId NOTIFY audioOutputsChanged)
    Q_PROPERTY(int skipBackwardSeconds READ skipBackwardSeconds NOTIFY preferencesChanged)
    Q_PROPERTY(int skipForwardSeconds READ skipForwardSeconds NOTIFY preferencesChanged)
    Q_PROPERTY(ThemePreference themePreference READ themePreference NOTIFY preferencesChanged)
    Q_PROPERTY(MotionPreference motionPreference READ motionPreference NOTIFY preferencesChanged)
    Q_PROPERTY(int lyricsTextSize READ lyricsTextSize NOTIFY preferencesChanged)
    // S9.1: how much earlier (positive) or later this guest plays than the room's time.
    Q_PROPERTY(int roomOutputOffsetMs READ roomOutputOffsetMs NOTIFY preferencesChanged)
    Q_PROPERTY(int roomOutputOffsetMinimumMs READ roomOutputOffsetMinimumMs CONSTANT)
    Q_PROPERTY(int roomOutputOffsetMaximumMs READ roomOutputOffsetMaximumMs CONSTANT)
    Q_PROPERTY(ClosePreference closePreference READ closePreference NOTIFY preferencesChanged)
    Q_PROPERTY(QueueEndPreference queueEndPreference READ queueEndPreference NOTIFY preferencesChanged)
    Q_PROPERTY(bool restoreLastSession READ restoreLastSession NOTIFY preferencesChanged)
    Q_PROPERTY(bool startMinimized READ startMinimized NOTIFY preferencesChanged)
    Q_PROPERTY(bool repeatQueueEnabled READ repeatQueueEnabled NOTIFY preferencesChanged)
    Q_PROPERTY(bool shuffleQueueEnabled READ shuffleQueueEnabled NOTIFY preferencesChanged)
    Q_PROPERTY(PreferenceWarning preferenceWarning READ preferenceWarning NOTIFY preferencePersistenceChanged)
    Q_PROPERTY(PersistenceWarningScope persistenceWarningScope READ persistenceWarningScope NOTIFY
                   preferencePersistenceChanged)
    Q_PROPERTY(QString persistenceWarningRevision READ persistenceWarningRevision NOTIFY preferencePersistenceChanged)
    Q_PROPERTY(bool trayAvailable READ trayAvailable NOTIFY platformChanged)
    Q_PROPERTY(bool defaultAppsSettingsSupported READ defaultAppsSettingsSupported NOTIFY defaultAppsSettingsChanged)
    Q_PROPERTY(bool defaultAppsSettingsAvailable READ defaultAppsSettingsAvailable NOTIFY defaultAppsSettingsChanged)
    Q_PROPERTY(DefaultAppsSettingsState defaultAppsSettingsState READ defaultAppsSettingsState NOTIFY
                   defaultAppsSettingsChanged)
    Q_PROPERTY(bool exiting READ exiting NOTIFY lifecycleChanged)
    Q_PROPERTY(bool exitCommitted READ exitCommitted NOTIFY lifecycleChanged)
    Q_PROPERTY(WindowNotice windowNotice READ windowNotice NOTIFY lifecycleChanged)
    Q_PROPERTY(bool trayNoticeEnabled READ trayNoticeEnabled NOTIFY lifecycleChanged)
    Q_PROPERTY(bool usesFrozenFixture READ usesFrozenFixture CONSTANT)

  public:
    using AudioFilePicker = std::function<std::vector<std::filesystem::path>()>;

    enum class PlaybackState { Unloaded, Loading, Ready, Paused, Playing, Error };
    Q_ENUM(PlaybackState)
    enum class ThemePreference { SystemTheme, LightTheme, DarkTheme };
    Q_ENUM(ThemePreference)
    enum class MotionPreference { SystemMotion, ReducedMotion };
    Q_ENUM(MotionPreference)
    enum class ClosePreference { MinimizeToTray, Exit };
    Q_ENUM(ClosePreference)
    enum class QueueEndPreference { StopKeepSelection, StopClearSelection };
    Q_ENUM(QueueEndPreference)
    enum class WindowNotice { NoNotice, TrayCloseNotice, TrayUnavailableNotice };
    Q_ENUM(WindowNotice)
    enum class DefaultAppsSettingsState { Idle, Opening, Dispatched, Unavailable, Failed };
    Q_ENUM(DefaultAppsSettingsState)
    enum class SourceImportState { Idle, Discovering, Cancelling, Completed, CompletedWithIssues, Cancelled, Rejected };
    Q_ENUM(SourceImportState)
    enum class PreferenceWarning {
        NoPreferenceWarning,
        RecoveredFromBackup,
        UsingDefaults,
        FutureVersion,
        SaveFailed,
    };
    Q_ENUM(PreferenceWarning)
    enum class PersistenceWarningScope {
        NoPersistenceWarningScope,
        PreferencePersistence,
        SessionPersistence,
        CombinedPersistence,
    };
    Q_ENUM(PersistenceWarningScope)

#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform, QObject* parent = nullptr);
    PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform, PlayerPresenterPreferences preferences,
                    QObject* parent = nullptr);
#endif

    PlayerPresenter(PlatformIntegration& platform, PlaybackBackend& playback_backend, MetadataReader& metadata_reader,
                    AudioFilePicker audio_file_picker, QObject* parent = nullptr);
    PlayerPresenter(PlatformIntegration& platform, PlaybackBackend& playback_backend, MetadataReader& metadata_reader,
                    AudioFilePicker audio_file_picker, PlayerPresenterPreferences preferences,
                    QObject* parent = nullptr);
    PlayerPresenter(PlatformIntegration& platform, PlaybackBackend& playback_backend, MetadataReader& metadata_reader,
                    AudioFilePicker audio_file_picker, std::shared_ptr<ArtworkImageCache> artwork_cache,
                    QObject* parent = nullptr);
    PlayerPresenter(PlatformIntegration& platform, PlaybackBackend& playback_backend, MetadataReader& metadata_reader,
                    AudioFilePicker audio_file_picker, std::shared_ptr<ArtworkImageCache> artwork_cache,
                    PlayerPresenterPreferences preferences, QObject* parent = nullptr);
    PlayerPresenter(PlatformIntegration& platform, PlaybackBackend& playback_backend, MetadataReader& metadata_reader,
                    AudioFilePicker audio_file_picker, LocalSourceDiscovery& source_discovery,
                    QObject* parent = nullptr);
    PlayerPresenter(PlatformIntegration& platform, PlaybackBackend& playback_backend, MetadataReader& metadata_reader,
                    AudioFilePicker audio_file_picker, LocalSourceDiscovery& source_discovery,
                    std::shared_ptr<ArtworkImageCache> artwork_cache, QObject* parent = nullptr);
    ~PlayerPresenter() override;

    [[nodiscard]] QString stateId() const;
    [[nodiscard]] PlaybackState playbackState() const;
    [[nodiscard]] bool queueLoaded() const noexcept;
    [[nodiscard]] bool hasCurrentOccurrence() const noexcept;
    [[nodiscard]] QString currentOccurrenceId() const;
    [[nodiscard]] int currentQueueIndex() const noexcept;
    [[nodiscard]] bool hasContextOccurrence() const noexcept;
    [[nodiscard]] QString contextOccurrenceId() const;
    [[nodiscard]] bool hasPosition() const noexcept;
    [[nodiscard]] qint64 positionMs() const noexcept;
    [[nodiscard]] qint64 confirmedPositionMs() const noexcept;
    [[nodiscard]] bool seekPreviewActive() const noexcept;
    [[nodiscard]] bool seekPending() const noexcept;
    [[nodiscard]] bool hasDuration() const noexcept;
    [[nodiscard]] qint64 durationMs() const noexcept;
    [[nodiscard]] bool seekable() const noexcept;
    [[nodiscard]] bool canGoPrevious() const noexcept;
    [[nodiscard]] bool canGoNext() const noexcept;
    [[nodiscard]] QString assetId() const;
    [[nodiscard]] bool hasMessage() const noexcept;
    [[nodiscard]] QString messageKey() const;
    [[nodiscard]] QString primaryActionId() const;
    [[nodiscard]] bool hasError() const noexcept;
    [[nodiscard]] QString errorCode() const;
    [[nodiscard]] QString errorMessageKey() const;
    [[nodiscard]] QString errorActionId() const;
    [[nodiscard]] QString retryingOccurrenceId() const;
    // The supplied locator must outlive the presenter, or be detached before it is destroyed.
    void setAudioFileLocator(AudioFileLocator* locator);
    // The asynchronous picker must outlive the presenter, or be detached before destruction.
    void setAudioFilesPicker(AudioFilesPicker* picker);
    [[nodiscard]] bool selectingAudioFiles() const noexcept;
    [[nodiscard]] bool audioFileSelectionFailed() const noexcept;
    [[nodiscard]] bool fileLocationAvailable() const noexcept;
    [[nodiscard]] QString locatingOccurrenceId() const;
    [[nodiscard]] QString fileLocationErrorOccurrenceId() const;
    [[nodiscard]] bool canLocateCurrentOccurrence() const noexcept;
    [[nodiscard]] bool canRetryCurrentOccurrence() const noexcept;
    [[nodiscard]] bool hasCurrentTrack() const noexcept;
    [[nodiscard]] QString currentTitle() const;
    [[nodiscard]] QString currentArtist() const;
    [[nodiscard]] QString currentAlbum() const;
    [[nodiscard]] QUrl currentArtworkSource() const;
    [[nodiscard]] bool detailsAvailable() const noexcept;
    [[nodiscard]] bool hasDetailsFileName() const;
    [[nodiscard]] QString detailsFileName() const;
    [[nodiscard]] bool hasDetailsFilePath() const noexcept;
    [[nodiscard]] QString detailsFilePath() const;
    [[nodiscard]] bool hasDetailsFileSize() const noexcept;
    [[nodiscard]] QVariant detailsFileSizeBytes() const;
    [[nodiscard]] QString detailsFileSizeBytesText() const;
    [[nodiscard]] bool hasDetailsModifiedUtc() const noexcept;
    [[nodiscard]] QString detailsModifiedUtc() const;
    [[nodiscard]] bool hasDetailsTitle() const;
    [[nodiscard]] QString detailsTitle() const;
    [[nodiscard]] bool hasDetailsArtist() const;
    [[nodiscard]] QString detailsArtist() const;
    [[nodiscard]] bool hasDetailsAlbum() const;
    [[nodiscard]] QString detailsAlbum() const;
    [[nodiscard]] bool hasDetailsTrackNumber() const noexcept;
    [[nodiscard]] QVariant detailsTrackNumber() const;
    [[nodiscard]] bool hasDetailsYear() const noexcept;
    [[nodiscard]] QVariant detailsYear() const;
    [[nodiscard]] bool hasDetailsDuration() const noexcept;
    [[nodiscard]] QVariant detailsDurationMs() const;
    [[nodiscard]] bool hasDetailsContainer() const;
    [[nodiscard]] QString detailsContainer() const;
    [[nodiscard]] bool hasDetailsCodec() const;
    [[nodiscard]] QString detailsCodec() const;
    [[nodiscard]] bool hasDetailsBitrate() const noexcept;
    [[nodiscard]] QVariant detailsBitrateBps() const;
    [[nodiscard]] bool hasDetailsSampleRate() const noexcept;
    [[nodiscard]] QVariant detailsSampleRateHz() const;
    [[nodiscard]] bool hasDetailsChannels() const noexcept;
    [[nodiscard]] QVariant detailsChannels() const;
    [[nodiscard]] bool hasDetailsBitDepth() const noexcept;
    [[nodiscard]] QVariant detailsBitDepth() const;
    [[nodiscard]] bool hasDetailsDiagnostics() const;
    [[nodiscard]] QVariantList detailsDiagnostics() const;
    [[nodiscard]] bool hasContextTrack() const noexcept;
    [[nodiscard]] QString contextTitle() const;
    [[nodiscard]] QString contextArtist() const;
    [[nodiscard]] QString contextAlbum() const;
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    [[nodiscard]] QueueListModel* queueModel() noexcept;
#endif
    [[nodiscard]] QAbstractItemModel* queueModelObject() noexcept;
    [[nodiscard]] SourceImportState sourceImportState() const noexcept;
    [[nodiscard]] bool sourceImportActive() const noexcept;
    [[nodiscard]] qulonglong sourceImportAcceptedCount() const noexcept;
    [[nodiscard]] qulonglong sourceImportIssueCount() const noexcept;
    [[nodiscard]] qulonglong sourceImportInspectedCount() const noexcept;
    void setSourceExitDrainedProbe(std::function<bool()> probe);
    void setPlaylistExitPreparationProbe(std::function<ExitPreparationState()> probe);
    // ADR-0019: receives the paths of every accepted local ingress (open, add,
    // drop/forwarding, playlist) after the queue commits. Exceptions are contained.
    void setLocalAdmissionObserver(std::function<void(const std::vector<std::filesystem::path>&)> observer);
    // ADR-0019: a committed download import, with its job ID when the import is
    // durable. A replayed import never appends again, so it is not reported twice.
    void
    setDownloadAdmissionObserver(std::function<void(const std::filesystem::path&, const std::string* job_id)> observer);
    // Copies of the session's durable download receipts, for the library's
    // startup reconciliation.
    [[nodiscard]] std::vector<DownloadImportReceipt> downloadImportReceipts() const;
    void cancelPendingPlaylistExit();
    // A complete pair or an empty pair is accepted before its first begin only.
    // An active/used gate cannot be detached or replaced to bypass preparation.
    [[nodiscard]] bool setExitPreparationCallbacks(ExitPreparationCallbacks callbacks);
    [[nodiscard]] const Preferences& preferences() const noexcept;
    [[nodiscard]] bool setDownloadDirectory(std::string directory);
    [[nodiscard]] double volume() const noexcept;
    [[nodiscard]] bool muted() const noexcept;
    [[nodiscard]] bool audioOutputSelectionSupported() const noexcept;
    [[nodiscard]] QVariantList audioOutputDevices() const;
    [[nodiscard]] QString selectedAudioOutputId() const;
    [[nodiscard]] int skipBackwardSeconds() const noexcept;
    [[nodiscard]] int skipForwardSeconds() const noexcept;
    [[nodiscard]] ThemePreference themePreference() const;
    [[nodiscard]] MotionPreference motionPreference() const;
    [[nodiscard]] ClosePreference closePreference() const;
    [[nodiscard]] QueueEndPreference queueEndPreference() const;
    [[nodiscard]] bool restoreLastSession() const noexcept;
    [[nodiscard]] bool startMinimized() const noexcept;
    [[nodiscard]] bool repeatQueueEnabled() const noexcept;
    [[nodiscard]] bool shuffleQueueEnabled() const noexcept;
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    void setQueueShuffleSeedForTest(std::uint32_t seed) noexcept;
#endif
    [[nodiscard]] PreferenceWarning preferenceWarning() const noexcept;
    [[nodiscard]] PersistenceWarningScope persistenceWarningScope() const noexcept;
    [[nodiscard]] QString persistenceWarningRevision() const;
    [[nodiscard]] bool trayAvailable() const noexcept;
    [[nodiscard]] bool defaultAppsSettingsSupported() const noexcept;
    [[nodiscard]] bool defaultAppsSettingsAvailable() const noexcept;
    [[nodiscard]] DefaultAppsSettingsState defaultAppsSettingsState() const noexcept;
    [[nodiscard]] bool exiting() const noexcept;
    [[nodiscard]] bool exitCommitted() const noexcept;
    [[nodiscard]] WindowNotice windowNotice() const noexcept;
    [[nodiscard]] bool trayNoticeEnabled() const noexcept;
    [[nodiscard]] bool usesFrozenFixture() const noexcept;
    void refreshPlatformAvailability();
    void reportTrayUnavailable();
    [[nodiscard]] bool recordWindowPlacement(WindowPlacement placement);
    [[nodiscard]] bool handleSystemPowerEvent(SystemPowerEvent event);
    Q_INVOKABLE bool requestClose();
    Q_INVOKABLE bool requestHide();
    Q_INVOKABLE bool requestRestore();
    Q_INVOKABLE bool requestExit();
    Q_INVOKABLE bool openDefaultAppsSettings();
    Q_INVOKABLE bool confirmWindowNotice();
    Q_INVOKABLE bool cancelWindowNotice();
    Q_INVOKABLE bool setTrayNoticeEnabled(bool enabled);
    Q_INVOKABLE bool openAudio();
    Q_INVOKABLE bool addAudio();
    // Trusted C++ handoff only, never a page/QML command. No I/O or autoplay.
    // committed is set after queue append and BEFORE notifications; a receipt
    // can recognize this commit even if a later observer throws. False without
    // commitment permits retry. Composition supplies a validated final MP3.
    [[nodiscard]] bool appendPublishedAudio(const std::filesystem::path& path, bool& committed);
    // Trusted local playlist handoff: detached snapshot and append-only batch,
    // without selection, decoding, autoplay or a download receipt.
    [[nodiscard]] LocalFileQueueSnapshot playlistQueueSnapshot() const;
    [[nodiscard]] bool appendPlaylistSources(std::vector<std::filesystem::path> source_paths);
    [[nodiscard]] QString queuedLyricsSource(const QString& occurrence_id) const;
    // Listening room (C++ composition only, never QML). While the room play order is on, the queue
    // order is the play order, as RoomHostPlayer requires: shuffle never picks the next track at
    // random. Shuffle is a physical permutation of the pending part (after the current item) instead,
    // made once when the room order turns on with shuffle enabled, and again whenever shuffle is
    // turned on during the room; queue observers (the room's guests) see the new order.
    void setRoomPlayOrder(bool enabled);
    void setRoomSessionGate(PlayerPresenterRoomSessionGate gate);
    // Schedules the (filtered) session again: the room changed what the gate filters, e.g. the host
    // learned that an item it just enqueued is a guest's.
    void refreshRoomSession();
    // The live session, before the room gate filters it.
    [[nodiscard]] SessionSnapshot liveSessionSnapshot() const;
    // Puts back a session captured by liveSessionSnapshot (the guest leaving a room): its queue and
    // current item, loaded paused at its position. No-op when the queue already matches.
    [[nodiscard]] bool restoreRoomSession(const SessionSnapshot& snapshot);
    // S13.1: while a guest follows a room (its local queue put away), the player shows the room's current
    // item and the shared player's playback (the follower's), never entering the local queue or session.
    // nullopt goes back to the local state.
    void showRoomPlayback(std::optional<RoomTrackSummary> track);
    // Appends one file without playback, library admission or download receipt; its new occurrence.
    [[nodiscard]] std::optional<LocalQueueOccurrenceId> appendRoomSource(const std::filesystem::path& source);
    // RoomHostPlayer::state(): the queue and where playback is heading. An accepted transport command or
    // seek counts before the backend confirms it (its events arrive queued), a track loaded to autoplay
    // counts as playing, and so does a held room start.
    [[nodiscard]] RoomHostPlayerState roomHostState() const;
    // RoomHostPlayer::holdStart/releaseStart. A held start keeps the current track silent (paused, or a
    // pending autoplay kept at Ready) while playbackState() still shows Playing, so the play button, tray
    // and system media controls do not flicker. pause()/togglePlayback() end it paused, play() keeps it,
    // another track, an error or an unload drop it. While a room is open (setRoomPlayOrder), a resume from
    // the host's own controls is held before the backend hears it and offered to the room through
    // playbackChanged; holdRoomStart from that notification keeps it, otherwise it plays at once.
    [[nodiscard]] bool holdRoomStart();
    [[nodiscard]] bool releaseRoomStart();
    // The one-time IP privacy notice of the listening room (FR-058), kept with the preferences.
    [[nodiscard]] bool roomPrivacyNoticeShown() const noexcept;
    bool markRoomPrivacyNoticeShown();
    [[nodiscard]] bool downloadImportRetirementPending() const noexcept;
    [[nodiscard]] bool openSourcePaths(std::vector<std::filesystem::path> source_paths, std::size_t insertion_index);
    Q_INVOKABLE bool importDroppedUrls(const QList<QUrl>& urls, int insertion_index);
    Q_INVOKABLE bool cancelSourceImport();
    Q_INVOKABLE bool dismissSourceImportStatus();
    Q_INVOKABLE bool playQueueOccurrence(const QString& occurrence_id);
    Q_INVOKABLE bool retryQueueOccurrence(const QString& occurrence_id);
    Q_INVOKABLE bool locateQueueOccurrence(const QString& occurrence_id);
    Q_INVOKABLE bool moveQueueOccurrence(const QString& occurrence_id, int destination_index);
    Q_INVOKABLE bool removeQueueOccurrence(const QString& occurrence_id);
    Q_INVOKABLE bool clearQueue();
    [[nodiscard]] bool shuffleQueue();
    Q_INVOKABLE bool toggleShuffleQueue();
    Q_INVOKABLE bool toggleRepeatQueue();
    Q_INVOKABLE bool loadFrozenQueue();
    Q_INVOKABLE bool play();
    Q_INVOKABLE bool pause();
    Q_INVOKABLE bool togglePlayback();
    Q_INVOKABLE bool skipBackward();
    Q_INVOKABLE bool skipForward();
    Q_INVOKABLE bool previousTrack();
    Q_INVOKABLE bool nextTrack();
    Q_INVOKABLE bool beginSeekPreview(qint64 position_ms);
    Q_INVOKABLE bool updateSeekPreview(qint64 position_ms);
    Q_INVOKABLE bool commitSeekPreview();
    Q_INVOKABLE bool setVolume(double volume);
    Q_INVOKABLE bool selectAudioOutput(const QString& id);
    Q_INVOKABLE bool toggleMute();
    Q_INVOKABLE bool setThemePreference(ThemePreference theme);
    Q_INVOKABLE bool setMotionPreference(MotionPreference motion);
    [[nodiscard]] int lyricsTextSize() const noexcept;
    Q_INVOKABLE bool setLyricsTextSize(int size);
    [[nodiscard]] int roomOutputOffsetMs() const noexcept;
    [[nodiscard]] static int roomOutputOffsetMinimumMs() noexcept;
    [[nodiscard]] static int roomOutputOffsetMaximumMs() noexcept;
    Q_INVOKABLE bool setRoomOutputOffsetMs(int offset_ms);
    Q_INVOKABLE bool setSkipBackwardSeconds(int seconds);
    Q_INVOKABLE bool setSkipForwardSeconds(int seconds);
    Q_INVOKABLE bool setClosePreference(ClosePreference behavior);
    Q_INVOKABLE bool setRestoreLastSession(bool restore);
    Q_INVOKABLE bool setStartMinimized(bool minimized);
    Q_INVOKABLE bool setQueueEndPreference(QueueEndPreference behavior);
    Q_INVOKABLE bool dismissPreferenceWarning();
    Q_INVOKABLE bool retryPreferenceSave();
    Q_INVOKABLE bool confirmPreferenceReplacement(const QString& expected_revision);
    Q_INVOKABLE bool executeErrorAction();
    Q_INVOKABLE bool skipUnavailable();
    [[nodiscard]] bool advanceOneTick();

  signals:
    void stateChanged();
    void preferencesChanged();
    void audioOutputsChanged();
    void preferencePersistenceChanged();
    void playbackChanged();
    void occurrenceAutomaticallySkipped(QString title, QString errorCode);
    void seekRejected();
    void platformChanged();
    void defaultAppsSettingsChanged();
    void lifecycleChanged();
    void playlistExitRecoveryRequested();
    void fileLocationFinished(QString occurrence_id, bool changed);
    void audioFileSelectionFinished(bool changed);
    void queueFocusRequested(int index);
    void sourceImportChanged();
    void windowPlacementFlushRequested();

  private:
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    friend class experiment::PlayerPresenterScenarioControl;
#endif
    friend struct test::PlayerPresenterTestAccess;
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    void rejectNextSeekForTest() noexcept;
#endif
    [[nodiscard]] std::size_t seekCommitAttemptCountForTest() const noexcept;
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    [[nodiscard]] bool enterFrozenUnavailableScenario();
#endif
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    void initializeDefaults();
#endif
    void initializePersistedPreferences(const PlayerPresenterPreferences& preferences);
    void initializePersistedSession(const PlayerPresenterPreferences& preferences);
    [[nodiscard]] SessionSnapshot currentSessionSnapshot() const;
    [[nodiscard]] std::int64_t currentSessionPosition() const;
    void commitSavedDownloadImportRetirements() noexcept;
    [[nodiscard]] bool appendPublishedAudioImpl(const std::filesystem::path& path, bool& committed,
                                                const std::string* download_job_id);
    [[nodiscard]] bool appendLocalSourcesWithoutPlayback(std::vector<std::filesystem::path> source_paths,
                                                         bool& committed, const std::string* download_job_id);
    void discardStaleRestoredSessionState();
    void cancelRestoredPositionForExplicitTransport();
    void sessionStructureDidChange();
    void notifyLocalAdmission(const std::vector<std::filesystem::path>& paths) noexcept;
    void observeSessionPosition(bool checkpoint);
    void reconcileRestoredPosition();
    [[nodiscard]] bool hasSourceVolumeRecovery() const noexcept;
    void schedulePreferencePersistence();
    void preferenceDidChange();
    void pollPreferencePersistence();
    void pollPersistence();
    void synchronizePreferenceWarning();
    void projectPersistenceWarning();
    void advancePersistenceWarningRevision() noexcept;
    [[nodiscard]] bool sourceExitDrained() const;
    [[nodiscard]] ExitPreparationState playlistExitPreparation() const;
    void showPlaylistExitRecovery();
    [[nodiscard]] ExitPreparationState pollExitPreparation();
    [[nodiscard]] bool beginPersistenceExit();
    [[nodiscard]] bool roomHoldsPosition() const;
    // S13.1: the room's item is shown while the local session has no track (the guest's queue is away).
    [[nodiscard]] bool showsRoomPlayback() const noexcept;
    // A room start of the current track that is held, or released and not yet confirmed Playing.
    [[nodiscard]] bool roomStartActive() const;
    [[nodiscard]] bool roomStartHeld() const;
    [[nodiscard]] PlaybackStatus roomHostStatus() const;
    bool endRoomStartHold();
    bool offerRoomStart();
    void completeExit();
    void cancelPendingExit();
    void completeDefaultAppsSettingsLaunch(DefaultAppsSettingsLaunchResult result, std::uint64_t generation);
    void handlePlaybackEvent(PlaybackEvent event);
    void pollMetadata();
    void pollSourceImport();
    void initializeLocalMode(const PlayerPresenterPreferences* preferences = nullptr);
    [[nodiscard]] bool beginSourceImport(std::vector<std::filesystem::path> roots,
                                         std::optional<std::size_t> insertion_index, std::uint64_t boundary_issue_count,
                                         bool allow_during_source_exit_drain = false);
    void rejectSourceImport(std::uint64_t issue_count);
    void finishSourceImport(SourceImportState terminal_state);
    [[nodiscard]] bool applySourceImportBatch(LocalSourceDiscoveryBatch batch);
    [[nodiscard]] std::size_t sourceImportInsertionIndex() const noexcept;
    [[nodiscard]] std::optional<std::size_t> localOccurrenceIndex(LocalQueueOccurrenceId occurrence_id) const noexcept;
    [[nodiscard]] static DiscoveryGeneration nextDiscoveryGeneration(DiscoveryGeneration current) noexcept;
    [[nodiscard]] bool applicationPollingEnabled() const noexcept;
    void reconcileTimer();
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    [[nodiscard]] DeterministicPlaybackBackend* fixtureBackend() noexcept;
    [[nodiscard]] const DeterministicPlaybackBackend* fixtureBackend() const noexcept;
#endif
    [[nodiscard]] std::optional<std::int64_t> localDurationMs() const noexcept;
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    [[nodiscard]] const QueueItem* currentTrack() const noexcept;
    [[nodiscard]] const QueueItem* contextTrack() const noexcept;
    [[nodiscard]] const FixtureTrackDetails* currentFixtureTrackDetails() const noexcept;
#endif
    [[nodiscard]] const TrackMetadata* currentLocalTrackMetadata() const noexcept;
    [[nodiscard]] std::optional<std::size_t> navigationTarget(QueueDirection direction) const noexcept;
    [[nodiscard]] std::optional<std::size_t> localOccurrenceIndex(const QString& occurrence_id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> nextAvailableLocalIndex() const noexcept;
    enum class LocalAdvanceResult { Rejected, Accepted, Superseded };
    [[nodiscard]] LocalAdvanceResult advanceLocalOccurrence(LocalTrackStartIntent start_intent, bool completed);
    void initializeShuffleRound();
    [[nodiscard]] PlaybackCommandResult loadLocalOccurrenceWithResult(std::size_t index,
                                                                      LocalTrackStartIntent start_intent);
    [[nodiscard]] bool loadLocalOccurrence(std::size_t index, LocalTrackStartIntent start_intent);
    [[nodiscard]] bool recordSourceImportOccurrenceIssue(LocalQueueOccurrenceId occurrence_id);
    [[nodiscard]] bool recoverAudioOutput();
    void finishFileLocation(std::uint64_t request_generation, AudioFileLocationResult result);
    void cancelFileLocation() noexcept;
    [[nodiscard]] bool beginAudioFileSelection(bool append_only);
    void finishAudioFileSelection(std::uint64_t generation, AudioFilesSelectionResult result);
    void cancelAudioFileSelection() noexcept;
    [[nodiscard]] bool localSourcesBlocked() const;
    [[nodiscard]] bool openSelectedAudio(const std::vector<std::filesystem::path>& selected_paths);
    [[nodiscard]] bool addSelectedAudio(const std::vector<std::filesystem::path>& selected_paths);
    void finishPublishedAudioAppend();
    [[nodiscard]] bool navigate(QueueDirection direction);
    [[nodiscard]] bool skipByPreference(bool forward);
    [[nodiscard]] bool finishSeekCommand(bool accepted, std::uint64_t generation);
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    std::unique_ptr<FixtureBundle> fixture_bundle_;
#endif
    std::vector<QueueNavigationItem> navigation_items_;
    PlatformIntegration& platform_;
    bool tray_available_;
    bool default_apps_settings_supported_;
    bool default_apps_settings_available_;
    DefaultAppsSettingsState default_apps_settings_state_{DefaultAppsSettingsState::Idle};
    std::uint64_t default_apps_settings_generation_{0};
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    std::unique_ptr<DeterministicPlaybackBackend> owned_backend_;
#endif
    PlaybackBackend& backend_;
    AudioOutputSelection* audio_output_selection_{};
    PlayerController controller_;
    LocalFileQueue local_queue_;
    std::mt19937 queue_shuffle_random_;
    std::unordered_set<std::uint64_t> shuffle_round_visited_;
    bool room_play_order_{false};
    PlayerPresenterRoomSessionGate room_session_gate_{};
    struct OccurrenceRetry final {
        LocalQueueOccurrenceId occurrence_id;
        std::uint64_t generation;
    };
    std::optional<OccurrenceRetry> occurrence_retry_{};
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    QueueListModel queue_model_{this};
#endif
    LocalQueueListModel local_queue_model_;
    QTimer timer_;
#ifdef RC_MUSIC_ENABLE_FIXTURE_SCENARIOS
    FixtureState state_;
#endif
    AudioFilePicker audio_file_picker_{};
    AudioFilesPicker* audio_files_picker_{nullptr};
    struct AudioFileSelectionRequest final {
        std::uint64_t generation{};
        bool append_only{false};
    };
    std::uint64_t audio_file_selection_generation_{0};
    std::optional<AudioFileSelectionRequest> audio_file_selection_{};
    bool audio_file_selection_failed_{false};
    bool file_dialog_cancellation_active_{false};
    AudioFileLocator* audio_file_locator_{nullptr};
    struct FileLocationRequest final {
        std::uint64_t generation{0};
        LocalQueueOccurrenceId occurrence_id{};
        std::filesystem::path original_path{};
    };
    std::uint64_t file_location_generation_{0};
    std::optional<FileLocationRequest> file_location_request_{};
    std::optional<LocalQueueOccurrenceId> file_location_error_{};
    std::unique_ptr<LocalTrackSession> local_track_session_{};
    std::shared_ptr<ArtworkImageCache> artwork_cache_{};
    std::unique_ptr<LocalSourceDiscovery> owned_source_discovery_{};
    LocalSourceDiscovery* source_discovery_{nullptr};
    DiscoveryGeneration source_import_generation_{};
    std::uint64_t source_import_sequence_{0};
    std::uint64_t source_import_accepted_count_{0};
    std::uint64_t source_import_issue_count_{0};
    std::uint64_t source_import_discovery_issue_count_{0};
    std::uint64_t source_import_inspected_count_{0};
    SourceImportState source_import_state_{SourceImportState::Idle};
    std::optional<LocalQueueOccurrenceId> source_import_before_occurrence_{};
    std::optional<LocalQueueOccurrenceId> source_import_predecessor_occurrence_{};
    std::optional<LocalQueueOccurrenceId> source_import_last_inserted_occurrence_{};
    std::vector<LocalQueueOccurrenceId> source_import_occurrences_{};
    std::vector<LocalQueueOccurrenceId> source_import_issue_occurrences_{};
    bool source_import_autoplay_enabled_{false};
    bool source_import_needs_autoplay_{false};
    std::optional<LocalTrackStartIntent> local_recovery_intent_{};
    bool uses_frozen_fixture_{false};
    bool download_append_in_progress_{false};
    bool deferred_download_suspend_{false};
    std::optional<SystemPowerEvent> deferred_download_power_event_{};
    std::size_t seek_commit_attempt_count_{0};
    bool exiting_{false};
    bool exit_committed_{false};
    bool source_exit_drain_active_{false};
    ExitPreparationCallbacks exit_preparation_callbacks_{};
    bool exit_preparation_started_{false};
    bool exit_preparation_call_in_progress_{false};
    std::optional<SystemPowerEvent> last_system_power_event_{};
    std::function<bool()> source_exit_drained_probe_{};
    std::function<ExitPreparationState()> playlist_exit_preparation_probe_{};
    std::function<void(const std::vector<std::filesystem::path>&)> local_admission_observer_{};
    std::function<void(const std::filesystem::path&, const std::string*)> download_admission_observer_{};
    std::optional<PlaybackEvent> deferred_playback_event_{};
    WindowNotice window_notice_{WindowNotice::NoNotice};
    bool tray_notice_enabled_{true};
    bool tray_notice_seen_{false};
    PreferencesAutosave* preferences_autosave_{nullptr};
    SessionAutosave* session_autosave_{nullptr};
    friend class PlayerDownloadImport;
    // Receipts survive all queue CRUD. Zero marks a valid loaded receipt; the
    // maximum revision marks a live append not yet staged into the autosave.
    std::vector<DownloadImportReceipt> download_import_receipts_{};
    std::vector<SessionRevision> download_import_revisions_{};
    struct DownloadImportRetirement final {
        std::string job_id;
        SessionRevision revision{0};
    };
    std::vector<std::string> retired_download_import_job_ids_{};
    // Each new retirement's revision covers every older staged retirement.
    std::vector<DownloadImportRetirement> download_import_retirements_{};
    bool download_import_persistence_ready_{false};
    bool download_import_call_in_progress_{false};
    PreferenceWarning preference_persistence_warning_{PreferenceWarning::NoPreferenceWarning};
    PreferenceWarning session_persistence_warning_{PreferenceWarning::NoPreferenceWarning};
    PreferenceWarning preference_warning_{PreferenceWarning::NoPreferenceWarning};
    PersistenceWarningScope persistence_warning_scope_{PersistenceWarningScope::NoPersistenceWarningScope};
    std::uint64_t persistence_warning_revision_{0};
    bool persistence_replacement_in_progress_{false};
    std::chrono::milliseconds preference_exit_flush_timeout_{2'000};
    std::function<PlayerPresenterPreferences::TimePoint()> preference_now_{
        [] { return std::chrono::steady_clock::now(); }};
    std::optional<PlayerPresenterPreferences::TimePoint> preference_exit_deadline_{};
    std::optional<PlayerPresenterPreferences::TimePoint> playlist_exit_deadline_{};
    std::function<SessionAutosave::TimePoint()> session_now_{};
    struct RestoredPosition final {
        std::uint64_t generation{0};
        std::int64_t target_ms{0};
        bool seek_requested{false};
        bool source_volume_recovery{false};
    };
    std::optional<RestoredPosition> restored_position_{};
    // S13.1: the room's current item while a guest follows a room, and the shared player's playback under it
    // (the follower's generations, which the local track session ignores).
    std::optional<RoomTrackSummary> room_track_{};
    PlaybackSnapshot room_playback_{};
    // A room start held for the room's anchor (holdRoomStart): its track's playback generation, whether
    // it was released and still waits for the backend to confirm Playing, and whether the presenter held
    // it itself (offerRoomStart) and the room has not taken it yet.
    struct RoomStartHold final {
        std::uint64_t generation{};
        bool released{false};
        bool offered{false};
    };
    std::optional<RoomStartHold> room_start_hold_{};
    std::uint64_t committed_seeks_{0};
    std::optional<std::uint64_t> restored_eof_generation_{};
    bool preferences_future_version_pending_{false};
    bool session_future_version_pending_{false};
};

} // namespace music_player::m0
