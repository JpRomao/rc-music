#pragma once

#include "m0/adapters/DeterministicPlaybackBackend.hpp"
#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "m0/application/LocalTrackSession.hpp"
#include "m0/application/PlayerController.hpp"
#include "m0/application/PreferencesAutosave.hpp"
#include "m0/domain/LocalFileQueue.hpp"
#include "m0/domain/QueueNavigation.hpp"
#include "m0/ports/LocalSourceDiscovery.hpp"
#include "m0/presentation/LocalQueueListModel.hpp"
#include "m0/presentation/QueueListModel.hpp"

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
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace music_player::m0 {
class ArtworkImageCache;
namespace experiment {
class PlayerPresenterScenarioControl;
}
namespace test {
struct PlayerPresenterTestAccess;
}

struct PlayerPresenterPreferences final {
    using TimePoint = std::chrono::steady_clock::time_point;

    Preferences initial{};
    PreferencesLoadStatus load_status{PreferencesLoadStatus::Missing};
    PreferencesAutosave* autosave{nullptr};
    std::chrono::milliseconds exit_flush_timeout{2'000};
    std::function<TimePoint()> now{[] { return std::chrono::steady_clock::now(); }};
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
    Q_PROPERTY(int skipBackwardSeconds READ skipBackwardSeconds NOTIFY preferencesChanged)
    Q_PROPERTY(int skipForwardSeconds READ skipForwardSeconds NOTIFY preferencesChanged)
    Q_PROPERTY(ThemePreference themePreference READ themePreference NOTIFY preferencesChanged)
    Q_PROPERTY(MotionPreference motionPreference READ motionPreference NOTIFY preferencesChanged)
    Q_PROPERTY(ClosePreference closePreference READ closePreference NOTIFY preferencesChanged)
    Q_PROPERTY(QueueEndPreference queueEndPreference READ queueEndPreference NOTIFY preferencesChanged)
    Q_PROPERTY(bool restoreLastSession READ restoreLastSession NOTIFY preferencesChanged)
    Q_PROPERTY(bool startMinimized READ startMinimized NOTIFY preferencesChanged)
    Q_PROPERTY(PreferenceWarning preferenceWarning READ preferenceWarning NOTIFY preferencePersistenceChanged)
    Q_PROPERTY(bool trayAvailable READ trayAvailable NOTIFY platformChanged)
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

    PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform, QObject* parent = nullptr);
    PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform, PlayerPresenterPreferences preferences,
                    QObject* parent = nullptr);
    PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform, PlaybackBackend& playback_backend,
                    MetadataReader& metadata_reader, AudioFilePicker audio_file_picker, QObject* parent = nullptr);
    PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform, PlaybackBackend& playback_backend,
                    MetadataReader& metadata_reader, AudioFilePicker audio_file_picker,
                    PlayerPresenterPreferences preferences, QObject* parent = nullptr);
    PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform, PlaybackBackend& playback_backend,
                    MetadataReader& metadata_reader, AudioFilePicker audio_file_picker,
                    std::shared_ptr<ArtworkImageCache> artwork_cache, QObject* parent = nullptr);
    PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform, PlaybackBackend& playback_backend,
                    MetadataReader& metadata_reader, AudioFilePicker audio_file_picker,
                    std::shared_ptr<ArtworkImageCache> artwork_cache, PlayerPresenterPreferences preferences,
                    QObject* parent = nullptr);
    PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform, PlaybackBackend& playback_backend,
                    MetadataReader& metadata_reader, AudioFilePicker audio_file_picker,
                    LocalSourceDiscovery& source_discovery, QObject* parent = nullptr);
    PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform, PlaybackBackend& playback_backend,
                    MetadataReader& metadata_reader, AudioFilePicker audio_file_picker,
                    LocalSourceDiscovery& source_discovery, std::shared_ptr<ArtworkImageCache> artwork_cache,
                    QObject* parent = nullptr);
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
    [[nodiscard]] QueueListModel* queueModel() noexcept;
    [[nodiscard]] QAbstractItemModel* queueModelObject() noexcept;
    [[nodiscard]] SourceImportState sourceImportState() const noexcept;
    [[nodiscard]] bool sourceImportActive() const noexcept;
    [[nodiscard]] qulonglong sourceImportAcceptedCount() const noexcept;
    [[nodiscard]] qulonglong sourceImportIssueCount() const noexcept;
    [[nodiscard]] qulonglong sourceImportInspectedCount() const noexcept;
    [[nodiscard]] const Preferences& preferences() const noexcept;
    [[nodiscard]] double volume() const noexcept;
    [[nodiscard]] bool muted() const noexcept;
    [[nodiscard]] int skipBackwardSeconds() const noexcept;
    [[nodiscard]] int skipForwardSeconds() const noexcept;
    [[nodiscard]] ThemePreference themePreference() const;
    [[nodiscard]] MotionPreference motionPreference() const;
    [[nodiscard]] ClosePreference closePreference() const;
    [[nodiscard]] QueueEndPreference queueEndPreference() const;
    [[nodiscard]] bool restoreLastSession() const noexcept;
    [[nodiscard]] bool startMinimized() const noexcept;
    [[nodiscard]] PreferenceWarning preferenceWarning() const noexcept;
    [[nodiscard]] bool trayAvailable() const noexcept;
    [[nodiscard]] bool exiting() const noexcept;
    [[nodiscard]] bool exitCommitted() const noexcept;
    [[nodiscard]] WindowNotice windowNotice() const noexcept;
    [[nodiscard]] bool trayNoticeEnabled() const noexcept;
    [[nodiscard]] bool usesFrozenFixture() const noexcept;
    void refreshPlatformAvailability();
    void reportTrayUnavailable();
    Q_INVOKABLE bool requestClose();
    Q_INVOKABLE bool requestHide();
    Q_INVOKABLE bool requestRestore();
    Q_INVOKABLE bool requestExit();
    Q_INVOKABLE bool confirmWindowNotice();
    Q_INVOKABLE bool cancelWindowNotice();
    Q_INVOKABLE bool setTrayNoticeEnabled(bool enabled);
    Q_INVOKABLE bool openAudio();
    Q_INVOKABLE bool addAudio();
    Q_INVOKABLE bool importDroppedUrls(const QList<QUrl>& urls, int insertion_index);
    Q_INVOKABLE bool cancelSourceImport();
    Q_INVOKABLE bool dismissSourceImportStatus();
    Q_INVOKABLE bool playQueueOccurrence(const QString& occurrence_id);
    Q_INVOKABLE bool moveQueueOccurrence(const QString& occurrence_id, int destination_index);
    Q_INVOKABLE bool removeQueueOccurrence(const QString& occurrence_id);
    Q_INVOKABLE bool clearQueue();
    Q_INVOKABLE bool loadFrozenQueue();
    Q_INVOKABLE bool togglePlayback();
    Q_INVOKABLE bool skipBackward();
    Q_INVOKABLE bool skipForward();
    Q_INVOKABLE bool previousTrack();
    Q_INVOKABLE bool nextTrack();
    Q_INVOKABLE bool beginSeekPreview(qint64 position_ms);
    Q_INVOKABLE bool updateSeekPreview(qint64 position_ms);
    Q_INVOKABLE bool commitSeekPreview();
    Q_INVOKABLE bool setVolume(double volume);
    Q_INVOKABLE bool toggleMute();
    Q_INVOKABLE bool setThemePreference(ThemePreference theme);
    Q_INVOKABLE bool setMotionPreference(MotionPreference motion);
    Q_INVOKABLE bool setSkipBackwardSeconds(int seconds);
    Q_INVOKABLE bool setSkipForwardSeconds(int seconds);
    Q_INVOKABLE bool setClosePreference(ClosePreference behavior);
    Q_INVOKABLE bool setRestoreLastSession(bool restore);
    Q_INVOKABLE bool setStartMinimized(bool minimized);
    Q_INVOKABLE bool setQueueEndPreference(QueueEndPreference behavior);
    Q_INVOKABLE bool dismissPreferenceWarning();
    Q_INVOKABLE bool retryPreferenceSave();
    Q_INVOKABLE bool confirmPreferenceReplacement();
    Q_INVOKABLE bool skipUnavailable();
    [[nodiscard]] bool advanceOneTick();

  signals:
    void stateChanged();
    void preferencesChanged();
    void preferencePersistenceChanged();
    void playbackChanged();
    void seekRejected();
    void platformChanged();
    void lifecycleChanged();
    void queueFocusRequested(int index);
    void sourceImportChanged();

  private:
    friend class experiment::PlayerPresenterScenarioControl;
    friend struct test::PlayerPresenterTestAccess;
    void rejectNextSeekForTest() noexcept;
    [[nodiscard]] std::size_t seekCommitAttemptCountForTest() const noexcept;
    [[nodiscard]] bool enterFrozenUnavailableScenario();
    void initializeDefaults();
    void initializePersistedPreferences(const PlayerPresenterPreferences& preferences);
    void preferenceDidChange();
    void pollPreferencePersistence();
    void synchronizePreferenceWarning();
    void completeExit();
    void cancelPendingExit();
    void handlePlaybackEvent(PlaybackEvent event);
    void pollMetadata();
    void pollSourceImport();
    void initializeLocalMode(const PlayerPresenterPreferences* preferences = nullptr);
    void rejectSourceImport(std::uint64_t issue_count);
    void finishSourceImport(SourceImportState terminal_state);
    [[nodiscard]] bool applySourceImportBatch(LocalSourceDiscoveryBatch batch);
    [[nodiscard]] std::size_t sourceImportInsertionIndex() const noexcept;
    [[nodiscard]] std::optional<std::size_t> localOccurrenceIndex(LocalQueueOccurrenceId occurrence_id) const noexcept;
    [[nodiscard]] static DiscoveryGeneration nextDiscoveryGeneration(DiscoveryGeneration current) noexcept;
    void reconcileTimer();
    [[nodiscard]] DeterministicPlaybackBackend* fixtureBackend() noexcept;
    [[nodiscard]] const DeterministicPlaybackBackend* fixtureBackend() const noexcept;
    [[nodiscard]] std::optional<std::int64_t> localDurationMs() const noexcept;
    [[nodiscard]] const QueueItem* currentTrack() const noexcept;
    [[nodiscard]] const QueueItem* contextTrack() const noexcept;
    [[nodiscard]] const FixtureTrackDetails* currentFixtureTrackDetails() const noexcept;
    [[nodiscard]] const TrackMetadata* currentLocalTrackMetadata() const noexcept;
    [[nodiscard]] std::optional<std::size_t> navigationTarget(QueueDirection direction) const noexcept;
    [[nodiscard]] std::optional<std::size_t> localOccurrenceIndex(const QString& occurrence_id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> nextAvailableLocalIndex() const noexcept;
    [[nodiscard]] PlaybackCommandResult loadLocalOccurrenceWithResult(std::size_t index,
                                                                      LocalTrackStartIntent start_intent);
    [[nodiscard]] bool loadLocalOccurrence(std::size_t index, LocalTrackStartIntent start_intent);
    [[nodiscard]] bool navigate(QueueDirection direction);
    FixtureBundle bundle_;
    std::vector<QueueNavigationItem> navigation_items_;
    PlatformIntegration& platform_;
    bool tray_available_;
    std::unique_ptr<DeterministicPlaybackBackend> owned_backend_;
    PlaybackBackend& backend_;
    PlayerController controller_;
    LocalFileQueue local_queue_;
    QueueListModel queue_model_;
    LocalQueueListModel local_queue_model_;
    QTimer timer_;
    FixtureState state_;
    AudioFilePicker audio_file_picker_{};
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
    bool source_import_autoplay_enabled_{false};
    bool source_import_needs_autoplay_{false};
    std::optional<LocalTrackStartIntent> local_recovery_intent_{};
    bool uses_frozen_fixture_{true};
    std::size_t seek_commit_attempt_count_{0};
    bool exiting_{false};
    bool exit_committed_{false};
    WindowNotice window_notice_{WindowNotice::NoNotice};
    bool tray_notice_enabled_{true};
    bool tray_notice_seen_{false};
    PreferencesAutosave* preferences_autosave_{nullptr};
    PreferenceWarning preference_warning_{PreferenceWarning::NoPreferenceWarning};
    std::chrono::milliseconds preference_exit_flush_timeout_{2'000};
    std::function<PlayerPresenterPreferences::TimePoint()> preference_now_{};
    std::optional<PlayerPresenterPreferences::TimePoint> preference_exit_deadline_{};
};

} // namespace music_player::m0
