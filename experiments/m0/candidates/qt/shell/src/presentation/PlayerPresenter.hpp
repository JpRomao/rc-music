#pragma once

#include "m0/adapters/DeterministicPlaybackBackend.hpp"
#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "m0/application/PlayerController.hpp"
#include "m0/presentation/QueueListModel.hpp"

#include <QAbstractItemModel>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QtQmlIntegration/qqmlintegration.h>

#include <cstddef>

namespace music_player::m0 {
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
    Q_PROPERTY(bool hasContextTrack READ hasContextTrack NOTIFY stateChanged)
    Q_PROPERTY(QString contextTitle READ contextTitle NOTIFY stateChanged)
    Q_PROPERTY(QString contextArtist READ contextArtist NOTIFY stateChanged)
    Q_PROPERTY(QString contextAlbum READ contextAlbum NOTIFY stateChanged)
    Q_PROPERTY(QAbstractItemModel* queueModel READ queueModelObject CONSTANT)
    Q_PROPERTY(double volume READ volume NOTIFY preferencesChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY preferencesChanged)
    Q_PROPERTY(int skipBackwardSeconds READ skipBackwardSeconds NOTIFY preferencesChanged)
    Q_PROPERTY(int skipForwardSeconds READ skipForwardSeconds NOTIFY preferencesChanged)

  public:
    enum class PlaybackState { Unloaded, Loading, Ready, Paused, Playing, Error };
    Q_ENUM(PlaybackState)

    PlayerPresenter(FixtureBundle bundle, PlatformIntegration& platform, QObject* parent = nullptr);

    [[nodiscard]] QString stateId() const;
    [[nodiscard]] PlaybackState playbackState() const;
    [[nodiscard]] bool queueLoaded() const noexcept;
    [[nodiscard]] bool hasCurrentOccurrence() const noexcept;
    [[nodiscard]] QString currentOccurrenceId() const;
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
    Q_INVOKABLE bool loadFrozenQueue();
    Q_INVOKABLE bool togglePlayback();
    Q_INVOKABLE bool skipBackward();
    Q_INVOKABLE bool skipForward();
    Q_INVOKABLE bool beginSeekPreview(qint64 position_ms);
    Q_INVOKABLE bool updateSeekPreview(qint64 position_ms);
    Q_INVOKABLE bool commitSeekPreview();
    Q_INVOKABLE bool setVolume(double volume);
    Q_INVOKABLE bool toggleMute();
    Q_INVOKABLE bool skipUnavailable();
    [[nodiscard]] bool advanceOneTick();

  signals:
    void stateChanged();
    void preferencesChanged();
    void playbackChanged();
    void seekRejected();

  private:
    friend class experiment::PlayerPresenterScenarioControl;
    friend struct test::PlayerPresenterTestAccess;
    void rejectNextSeekForTest() noexcept;
    [[nodiscard]] std::size_t seekCommitAttemptCountForTest() const noexcept;
    [[nodiscard]] bool enterFrozenUnavailableScenario();
    void reconcileTimer();
    [[nodiscard]] const QueueItem* currentTrack() const noexcept;
    [[nodiscard]] const QueueItem* contextTrack() const noexcept;
    FixtureBundle bundle_;
    DeterministicPlaybackBackend backend_;
    PlayerController controller_;
    QueueListModel queue_model_;
    QTimer timer_;
    FixtureState state_;
    std::size_t seek_commit_attempt_count_{0};
};

} // namespace music_player::m0
