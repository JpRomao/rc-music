#pragma once

#include "m0/adapters/DeterministicPlaybackBackend.hpp"
#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "m0/application/LocalTrackSession.hpp"
#include "m0/application/PlayerController.hpp"
#include "m0/domain/QueueNavigation.hpp"
#include "m0/presentation/QueueListModel.hpp"

#include <QAbstractItemModel>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

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
    Q_PROPERTY(bool hasContextTrack READ hasContextTrack NOTIFY stateChanged)
    Q_PROPERTY(QString contextTitle READ contextTitle NOTIFY stateChanged)
    Q_PROPERTY(QString contextArtist READ contextArtist NOTIFY stateChanged)
    Q_PROPERTY(QString contextAlbum READ contextAlbum NOTIFY stateChanged)
    Q_PROPERTY(QAbstractItemModel* queueModel READ queueModelObject CONSTANT)
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
    Q_PROPERTY(bool trayAvailable READ trayAvailable NOTIFY platformChanged)
    Q_PROPERTY(bool exiting READ exiting NOTIFY lifecycleChanged)
    Q_PROPERTY(WindowNotice windowNotice READ windowNotice NOTIFY lifecycleChanged)
    Q_PROPERTY(bool trayNoticeEnabled READ trayNoticeEnabled NOTIFY lifecycleChanged)
    Q_PROPERTY(bool usesFrozenFixture READ usesFrozenFixture CONSTANT)

  public:
    using AudioFilePicker = std::function<std::optional<std::filesystem::path>()>;

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

    PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform, QObject* parent = nullptr);
    PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform, PlaybackBackend& playback_backend,
                    MetadataReader& metadata_reader, AudioFilePicker audio_file_picker, QObject* parent = nullptr);
    PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform, PlaybackBackend& playback_backend,
                    MetadataReader& metadata_reader, AudioFilePicker audio_file_picker,
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
    [[nodiscard]] bool hasCurrentTrack() const noexcept;
    [[nodiscard]] QString currentTitle() const;
    [[nodiscard]] QString currentArtist() const;
    [[nodiscard]] QString currentAlbum() const;
    [[nodiscard]] QUrl currentArtworkSource() const;
    [[nodiscard]] bool hasContextTrack() const noexcept;
    [[nodiscard]] QString contextTitle() const;
    [[nodiscard]] QString contextArtist() const;
    [[nodiscard]] QString contextAlbum() const;
    [[nodiscard]] QueueListModel* queueModel() noexcept;
    [[nodiscard]] QAbstractItemModel* queueModelObject() noexcept;
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
    [[nodiscard]] bool trayAvailable() const noexcept;
    [[nodiscard]] bool exiting() const noexcept;
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
    Q_INVOKABLE bool skipUnavailable();
    [[nodiscard]] bool advanceOneTick();

  signals:
    void stateChanged();
    void preferencesChanged();
    void playbackChanged();
    void seekRejected();
    void platformChanged();
    void lifecycleChanged();

  private:
    friend class experiment::PlayerPresenterScenarioControl;
    friend struct test::PlayerPresenterTestAccess;
    void rejectNextSeekForTest() noexcept;
    [[nodiscard]] std::size_t seekCommitAttemptCountForTest() const noexcept;
    [[nodiscard]] bool enterFrozenUnavailableScenario();
    void initializeDefaults();
    void handlePlaybackEvent(PlaybackEvent event);
    void pollMetadata();
    void reconcileTimer();
    [[nodiscard]] DeterministicPlaybackBackend* fixtureBackend() noexcept;
    [[nodiscard]] const DeterministicPlaybackBackend* fixtureBackend() const noexcept;
    [[nodiscard]] std::optional<std::int64_t> localDurationMs() const noexcept;
    [[nodiscard]] const QueueItem* currentTrack() const noexcept;
    [[nodiscard]] const QueueItem* contextTrack() const noexcept;
    [[nodiscard]] std::optional<std::size_t> navigationTarget(QueueDirection direction) const noexcept;
    [[nodiscard]] bool navigate(QueueDirection direction);
    FixtureBundle bundle_;
    std::vector<QueueNavigationItem> navigation_items_;
    PlatformIntegration& platform_;
    bool tray_available_;
    std::unique_ptr<DeterministicPlaybackBackend> owned_backend_;
    PlaybackBackend& backend_;
    PlayerController controller_;
    QueueListModel queue_model_;
    QTimer timer_;
    FixtureState state_;
    AudioFilePicker audio_file_picker_{};
    std::unique_ptr<LocalTrackSession> local_track_session_{};
    std::shared_ptr<ArtworkImageCache> artwork_cache_{};
    bool uses_frozen_fixture_{true};
    std::size_t seek_commit_attempt_count_{0};
    bool exiting_{false};
    WindowNotice window_notice_{WindowNotice::NoNotice};
    bool tray_notice_enabled_{true};
    bool tray_notice_seen_{false};
};

} // namespace music_player::m0
