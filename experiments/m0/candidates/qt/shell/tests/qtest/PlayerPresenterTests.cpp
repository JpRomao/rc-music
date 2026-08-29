#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "m0/ports/MetadataReader.hpp"
#include "m0/ports/PlaybackBackend.hpp"
#include "presentation/PlayerPresenterScenarioControl.hpp"
#include "presentation/PlayerPresenterTestAccess.hpp"

#include <QAbstractItemModelTester>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QPersistentModelIndex>
#include <QSet>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QTimer>
#include <QVariant>

#include <deque>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace music_player::m0::test {
namespace {

class AvailableTrayPlatform final : public PlatformIntegration {
  public:
    bool available{true};
    [[nodiscard]] bool trayAvailable() const noexcept override { return available; }
    void hideWindow() override { ++hide_calls; }
    void minimizeWindow() override { ++minimize_calls; }
    void showAndActivateWindow() override { ++restore_calls; }
    void requestExit() override {
        ++exit_calls;
        if (on_exit)
            on_exit();
    }
    int hide_calls{0};
    int minimize_calls{0};
    int restore_calls{0};
    int exit_calls{0};
    std::function<void()> on_exit;
};

class PresenterPlaybackBackend final : public PlaybackBackend {
  public:
    [[nodiscard]] const PlaybackSnapshot& snapshot() const noexcept override { return snapshot_; }
    void setEventCallback(PlaybackEventCallback callback) override {
        event_callback_ = std::move(callback);
        ++callback_assignments;
    }
    PlaybackCommandResult load(const std::filesystem::path& source_path) override {
        loaded_paths.push_back(source_path);
        if (load_result == PlaybackCommandResult::Accepted) {
            snapshot_ = {.status = PlaybackStatus::Loading, .generation = ++last_generation_};
        }
        return load_result;
    }
    PlaybackCommandResult play() override {
        ++play_calls;
        return play_result;
    }
    PlaybackCommandResult pause() override {
        ++pause_calls;
        return pause_result;
    }
    PlaybackCommandResult seekTo(const std::int64_t position_ms) override {
        seek_positions.push_back(position_ms);
        return seek_result;
    }
    PlaybackCommandResult setVolume(const double volume) override {
        volumes.push_back(volume);
        return volume_result;
    }
    PlaybackCommandResult setMuted(const bool muted) override {
        muted_values.push_back(muted);
        return muted_result;
    }
    PlaybackCommandResult unload() override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult shutdown() override { return PlaybackCommandResult::Accepted; }

    void publish(PlaybackSnapshot snapshot) {
        snapshot_ = snapshot;
        if (event_callback_)
            event_callback_({PlaybackEventType::SnapshotChanged, snapshot_});
    }

    [[nodiscard]] bool callbackInstalled() const noexcept { return static_cast<bool>(event_callback_); }

    PlaybackSnapshot snapshot_{};
    PlaybackEventCallback event_callback_{};
    PlaybackCommandResult load_result{PlaybackCommandResult::Accepted};
    PlaybackCommandResult play_result{PlaybackCommandResult::Accepted};
    PlaybackCommandResult pause_result{PlaybackCommandResult::Accepted};
    PlaybackCommandResult seek_result{PlaybackCommandResult::Accepted};
    PlaybackCommandResult volume_result{PlaybackCommandResult::Accepted};
    PlaybackCommandResult muted_result{PlaybackCommandResult::Accepted};
    std::vector<std::filesystem::path> loaded_paths;
    std::vector<std::int64_t> seek_positions;
    std::vector<double> volumes;
    std::vector<bool> muted_values;
    int callback_assignments{0};
    int play_calls{0};
    int pause_calls{0};

  private:
    std::uint64_t last_generation_{0};
};

class PresenterMetadataReader final : public MetadataReader {
  public:
    MetadataCommandResult startRead(MetadataReadRequest request) override {
        requests.push_back(std::move(request));
        return start_result;
    }
    MetadataCommandResult cancelRead(const MetadataGeneration generation) override {
        cancelled_generations.push_back(generation);
        return cancel_result;
    }
    [[nodiscard]] MetadataPollResult poll(const MetadataGeneration generation) override {
        polled_generations.push_back(generation);
        if (poll_results.empty())
            return {MetadataPollStatus::Pending, std::nullopt};
        auto result = std::move(poll_results.front());
        poll_results.pop_front();
        return result;
    }
    [[nodiscard]] std::optional<MetadataReadCompletion> tryTakeCompletion() override { return std::nullopt; }
    MetadataCommandResult shutdown() override { return MetadataCommandResult::Accepted; }

    MetadataCommandResult start_result{MetadataCommandResult::Accepted};
    MetadataCommandResult cancel_result{MetadataCommandResult::Accepted};
    std::vector<MetadataReadRequest> requests;
    std::vector<MetadataGeneration> cancelled_generations;
    std::vector<MetadataGeneration> polled_generations;
    std::deque<MetadataPollResult> poll_results;
};

[[nodiscard]] FixtureBundle frozenBundle() {
    return FixtureRepository{}.load(QString::fromUtf8(M0_TEST_FIXTURE_FILE), QString::fromUtf8(M0_TEST_QUEUE_FILE));
}

} // namespace

class PlayerPresenterTests final : public QObject {
    Q_OBJECT

  private slots:
    void fixtureOpenAudioPreservesTheFrozenModeContract();
    void localOpenCancellationIsInert();
    void localOpenPublishesUnicodeFallbackAndStartsMetadata();
    void rejectedLocalOpenDoesNotStartMetadataOrPublishTrackState();
    void currentReadyEventAutoplaysAndStaleEventsAreIgnored();
    void localTransportAcceptsAsynchronousPlayAndSeekWithoutAQueue();
    void currentMetadataCompletionReplacesFallbackAndStopsPolling();
    void terminalMetadataStatusStopsPollingWithoutReplacingFallback();
    void destructionClearsPlaybackCallbackAndCancelsPendingMetadata();
    void startsFromNullableEmptyStateAndAppliesEveryDefault();
    void appliesDefaultsThatDifferFromTheCoreDefaults();
    void loadsFrozenQueueOnceWithoutReplacingTheModel();
    void derivesBackendSnapshotFromTheTypedPlayingState();
    void rejectsUnknownMappingsAndExposesNoInfrastructureApi();
    void drivesTransportPreferencesAndManualTicksDeterministically();
    void keepsUnavailableAndTerminalTransportInert();
    void schedulesTicksOnlyWhilePlaybackOrSeekNeedsThem();
    void projectsTypedMetadataAndRecoversFrozenUnavailableScenario();
    void navigatesDistinctOccurrencesWithoutResettingTheModel();
    void previousUsesConfirmedThresholdAndPreservesPause();
    void navigationSkipsUnavailableItemsAndStopsAtBounds();
    void navigationIsInertDuringSeekAndInactiveStates();
    void exposesTypedPreferenceDefaults();
    void editsPreferencesWithoutChangingPlaybackOrTheQueue();
    void rejectsInvalidAndUnchangedPreferencesWithoutNotifications();
    void refreshesTrayAvailabilityAndGuardsStartMinimized();
    void exposesReadOnlyLifecycleDefaultsAndOnlyIntentMethods();
    void firstCloseWaitsForAcknowledgementAndDoesNotRepeatTheNotice();
    void unavailableTrayMinimizesAndKeepsTheExplanationUntilRestored();
    void cancellingTheFirstNoticeDoesNotConsumeIt();
    void noticeOptOutIsInMemoryAndDoesNotActOnThePendingWindow();
    void closeRefreshesAvailabilityBeforeDecidingTheWindowAction();
    void confirmationRechecksAvailabilityWithoutPublishingATransientNotice();
    void explicitHideIgnoresClosePreferenceAndBypassesTheNotice_data();
    void explicitHideIgnoresClosePreferenceAndBypassesTheNotice();
    void explicitHideClearsAPendingFirstNoticeWithoutAcknowledgingIt();
    void closeExitPreferenceOverridesNoticesAndTrayAvailability_data();
    void closeExitPreferenceOverridesNoticesAndTrayAvailability();
    void reportingTrayLossOnlyProjectsAnIdempotentExplanation();
    void lifecyclePreservesPlaybackQueueModelAndPendingBackendWork_data();
    void lifecyclePreservesPlaybackQueueModelAndPendingBackendWork();
    void exitPublishesItsGuardAndStopsTicksBeforeRequestingThePlatform_data();
    void exitPublishesItsGuardAndStopsTicksBeforeRequestingThePlatform();
};

void PlayerPresenterTests::fixtureOpenAudioPreservesTheFrozenModeContract() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);

    QVERIFY(presenter.usesFrozenFixture());
    QVERIFY(presenter.openAudio());

    QVERIFY(presenter.queueLoaded());
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QCOMPARE(presenter.currentTitle(), QStringLiteral("Canção nº 17 — São João, ação e café ☕"));
}

void PlayerPresenterTests::localOpenCancellationIsInert() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    int picker_calls = 0;
    PlayerPresenter presenter(frozenBundle(), platform, backend, metadata,
                              [&]() -> std::optional<std::filesystem::path> {
                                  ++picker_calls;
                                  return std::nullopt;
                              });
    QSignalSpy states(&presenter, &PlayerPresenter::stateChanged);
    QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);

    QVERIFY(!presenter.usesFrozenFixture());
    QVERIFY(!presenter.openAudio());

    QCOMPARE(picker_calls, 1);
    QVERIFY(backend.loaded_paths.empty());
    QVERIFY(metadata.requests.empty());
    QVERIFY(!presenter.hasCurrentTrack());
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Unloaded);
    QCOMPARE(states.size(), 0);
    QCOMPARE(playback.size(), 0);
}

void PlayerPresenterTests::localOpenPublishesUnicodeFallbackAndStartsMetadata() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::filesystem::path source{LR"(C:\Música\Abertura 日本語.wav)"};
    PlayerPresenter presenter(frozenBundle(), platform, backend, metadata,
                              [source] { return std::optional<std::filesystem::path>{source}; });
    QSignalSpy states(&presenter, &PlayerPresenter::stateChanged);
    QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);

    QVERIFY(presenter.openAudio());

    QCOMPARE(backend.loaded_paths, std::vector<std::filesystem::path>{source});
    QCOMPARE(metadata.requests.size(), std::size_t{1});
    QCOMPARE(metadata.requests.front().source_path, source);
    QCOMPARE(metadata.requests.front().generation, MetadataGeneration{1});
    QVERIFY(presenter.hasCurrentTrack());
    QCOMPARE(presenter.currentTitle(), QStringLiteral("Abertura 日本語"));
    QCOMPARE(presenter.currentArtist(), QString{});
    QCOMPARE(presenter.currentAlbum(), QString{});
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
    QCOMPARE(presenter.queueLoaded(), false);
    QCOMPARE(presenter.canGoPrevious(), false);
    QCOMPARE(presenter.canGoNext(), false);
    QCOMPARE(states.size(), 1);
    QCOMPARE(playback.size(), 1);
    const QList<QTimer*> timers = presenter.findChildren<QTimer*>(QString{}, Qt::FindDirectChildrenOnly);
    QCOMPARE(timers.size(), 1);
    QVERIFY(timers.front()->interval() < 200);
    QVERIFY(timers.front()->isActive());
}

void PlayerPresenterTests::rejectedLocalOpenDoesNotStartMetadataOrPublishTrackState() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    backend.load_result = PlaybackCommandResult::NotReady;
    PresenterMetadataReader metadata;
    const std::filesystem::path source{LR"(C:\audio\rejected.wav)"};
    PlayerPresenter presenter(frozenBundle(), platform, backend, metadata,
                              [source] { return std::optional<std::filesystem::path>{source}; });
    QSignalSpy states(&presenter, &PlayerPresenter::stateChanged);
    QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);

    QVERIFY(!presenter.openAudio());

    QCOMPARE(backend.loaded_paths, std::vector<std::filesystem::path>{source});
    QVERIFY(metadata.requests.empty());
    QVERIFY(!presenter.hasCurrentTrack());
    QCOMPARE(presenter.currentTitle(), QString{});
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Unloaded);
    QCOMPARE(states.size(), 0);
    QCOMPARE(playback.size(), 0);
}

void PlayerPresenterTests::currentReadyEventAutoplaysAndStaleEventsAreIgnored() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::filesystem::path source{LR"(C:\audio\ready.wav)"};
    PlayerPresenter presenter(frozenBundle(), platform, backend, metadata,
                              [source] { return std::optional<std::filesystem::path>{source}; });
    QVERIFY(presenter.openAudio());
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);

    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 9'000,
                     .duration_ms = 60'000,
                     .seekable = true,
                     .generation = 0});
    QCoreApplication::processEvents();
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
    QCOMPARE(backend.play_calls, 0);

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 60'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Ready);
    QCOMPARE(presenter.durationMs(), 60'000);
    QVERIFY(presenter.seekable());

    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 500,
                     .duration_ms = 60'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QCOMPARE(presenter.positionMs(), 500);
    QCOMPARE(backend.play_calls, 1);
}

void PlayerPresenterTests::localTransportAcceptsAsynchronousPlayAndSeekWithoutAQueue() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::filesystem::path source{LR"(C:\audio\transport.wav)"};
    PlayerPresenter presenter(frozenBundle(), platform, backend, metadata,
                              [source] { return std::optional<std::filesystem::path>{source}; });
    QVERIFY(presenter.openAudio());
    backend.publish({.status = PlaybackStatus::Ready,
                     .position_ms = 5'000,
                     .duration_ms = 60'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Ready);
    QCOMPARE(backend.play_calls, 1);

    QVERIFY(presenter.togglePlayback());
    QCOMPARE(backend.play_calls, 2);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Ready);
    QVERIFY(presenter.beginSeekPreview(10'000));
    QVERIFY(presenter.updateSeekPreview(20'000));
    QVERIFY(presenter.commitSeekPreview());
    QCOMPARE(backend.seek_positions, std::vector<std::int64_t>{20'000});
    QVERIFY(presenter.seekPending());

    backend.publish({.status = PlaybackStatus::Ready,
                     .position_ms = 20'000,
                     .duration_ms = 60'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_VERIFY(!presenter.seekPending());
    QCOMPARE(presenter.positionMs(), 20'000);

    QVERIFY(presenter.beginSeekPreview(20'000));
    QVERIFY(presenter.commitSeekPreview());
    QVERIFY(!presenter.seekPending());
    QVERIFY(!presenter.seekPreviewActive());
    QCOMPARE(backend.seek_positions, std::vector<std::int64_t>{20'000});

    backend.seek_result = PlaybackCommandResult::NotSeekable;
    QVERIFY(presenter.beginSeekPreview(30'000));
    QVERIFY(!presenter.commitSeekPreview());
    QVERIFY(!presenter.seekPending());
    QVERIFY(!presenter.seekPreviewActive());
    QCOMPARE(presenter.positionMs(), 20'000);

    backend.publish({.status = PlaybackStatus::Paused,
                     .position_ms = 60'000,
                     .duration_ms = 60'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QVERIFY(presenter.togglePlayback());
    QCOMPARE(backend.play_calls, 3);
}

void PlayerPresenterTests::currentMetadataCompletionReplacesFallbackAndStopsPolling() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::filesystem::path source{LR"(C:\audio\metadata.wav)"};
    TrackMetadata track{
        .source_path = source,
        .file_size_bytes = 123,
        .tags = {.title = std::string{"Decoded title"},
                 .artist = std::string{"Decoded artist"},
                 .album = std::string{"Decoded album"}},
        .display_title = "Decoded title",
        .audio = {.duration_ms = 91'000, .container = "WAV", .codec = "PCM"},
    };
    metadata.poll_results.push_back(
        {MetadataPollStatus::Completed, MetadataReadCompletion{MetadataGeneration{1}, source, std::move(track)}});
    PlayerPresenter presenter(frozenBundle(), platform, backend, metadata,
                              [source] { return std::optional<std::filesystem::path>{source}; });
    QSignalSpy states(&presenter, &PlayerPresenter::stateChanged);
    QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);

    QVERIFY(presenter.openAudio());
    QTRY_COMPARE(presenter.currentTitle(), QStringLiteral("Decoded title"));

    QCOMPARE(presenter.currentArtist(), QStringLiteral("Decoded artist"));
    QCOMPARE(presenter.currentAlbum(), QStringLiteral("Decoded album"));
    QVERIFY(presenter.hasDuration());
    QCOMPARE(presenter.durationMs(), 91'000);
    QCOMPARE(metadata.polled_generations, std::vector<MetadataGeneration>{MetadataGeneration{1}});
    const QList<QTimer*> timers = presenter.findChildren<QTimer*>(QString{}, Qt::FindDirectChildrenOnly);
    QCOMPARE(timers.size(), 1);
    QVERIFY(!timers.front()->isActive());
    QCOMPARE(states.size(), 2);
    QCOMPARE(playback.size(), 2);
}

void PlayerPresenterTests::terminalMetadataStatusStopsPollingWithoutReplacingFallback() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    metadata.poll_results.push_back({MetadataPollStatus::StaleGeneration, std::nullopt});
    const std::filesystem::path source{LR"(C:\audio\fallback.wav)"};
    PlayerPresenter presenter(frozenBundle(), platform, backend, metadata,
                              [source] { return std::optional<std::filesystem::path>{source}; });
    QSignalSpy states(&presenter, &PlayerPresenter::stateChanged);

    QVERIFY(presenter.openAudio());
    QTRY_COMPARE(metadata.polled_generations.size(), std::size_t{1});

    QCOMPARE(presenter.currentTitle(), QStringLiteral("fallback"));
    const QList<QTimer*> timers = presenter.findChildren<QTimer*>(QString{}, Qt::FindDirectChildrenOnly);
    QCOMPARE(timers.size(), 1);
    QVERIFY(!timers.front()->isActive());
    QCOMPARE(states.size(), 2);
    QTest::qWait(timers.front()->interval() * 2);
    QCOMPARE(metadata.polled_generations.size(), std::size_t{1});
}

void PlayerPresenterTests::destructionClearsPlaybackCallbackAndCancelsPendingMetadata() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::filesystem::path source{LR"(C:\audio\pending.wav)"};
    auto presenter = std::make_unique<PlayerPresenter>(
        frozenBundle(), platform, backend, metadata, [source] { return std::optional<std::filesystem::path>{source}; });
    QVERIFY(backend.callbackInstalled());
    QVERIFY(presenter->openAudio());
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 1'000, .generation = 1});

    presenter.reset();

    QVERIFY(!backend.callbackInstalled());
    QCOMPARE(backend.callback_assignments, 2);
    QCOMPARE(metadata.cancelled_generations, std::vector<MetadataGeneration>{MetadataGeneration{1}});
    QCoreApplication::processEvents();
}

void PlayerPresenterTests::startsFromNullableEmptyStateAndAppliesEveryDefault() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);

    QCOMPARE(presenter.stateId(), QStringLiteral("empty"));
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Unloaded);
    QCOMPARE(presenter.queueLoaded(), false);
    QCOMPARE(presenter.hasCurrentOccurrence(), false);
    QCOMPARE(presenter.currentOccurrenceId(), QString{});
    QCOMPARE(presenter.currentQueueIndex(), -1);
    QCOMPARE(presenter.hasContextOccurrence(), false);
    QCOMPARE(presenter.contextOccurrenceId(), QString{});
    QCOMPARE(presenter.hasPosition(), false);
    QCOMPARE(presenter.positionMs(), 0);
    QCOMPARE(presenter.hasDuration(), false);
    QCOMPARE(presenter.durationMs(), 0);
    QCOMPARE(presenter.seekable(), false);
    QCOMPARE(presenter.assetId(), QStringLiteral("cover-placeholder"));
    QCOMPARE(presenter.hasMessage(), true);
    QCOMPARE(presenter.messageKey(), QStringLiteral("empty_title"));
    QCOMPARE(presenter.primaryActionId(), QStringLiteral("open-audio"));
    QCOMPARE(presenter.hasError(), false);
    QCOMPARE(presenter.errorCode(), QString{});
    QCOMPARE(presenter.hasCurrentTrack(), false);
    QCOMPARE(presenter.currentTitle(), QString{});
    QCOMPARE(presenter.currentArtist(), QString{});
    QCOMPARE(presenter.currentAlbum(), QString{});
    QCOMPARE(presenter.hasContextTrack(), false);
    QCOMPARE(presenter.contextTitle(), QString{});
    QCOMPARE(presenter.contextArtist(), QString{});
    QCOMPARE(presenter.contextAlbum(), QString{});

    const Preferences& preferences = presenter.preferences();
    QCOMPARE(preferences.theme, ThemeMode::System);
    QCOMPARE(preferences.motion, MotionMode::System);
    QCOMPARE(preferences.volume, 0.8);
    QCOMPARE(preferences.muted, false);
    QCOMPARE(preferences.skip_backward_seconds, 5);
    QCOMPARE(preferences.skip_forward_seconds, 5);
    QCOMPARE(preferences.close_behavior, CloseBehavior::MinimizeToTray);
    QCOMPARE(preferences.restore_last_session, true);
    QCOMPARE(preferences.start_minimized, false);
    QCOMPARE(preferences.queue_end_behavior, QueueEndBehavior::StopKeepSelection);
    QCOMPARE(presenter.volume(), 0.8);
    QCOMPARE(presenter.muted(), false);
}

void PlayerPresenterTests::appliesDefaultsThatDifferFromTheCoreDefaults() {
    AvailableTrayPlatform platform;
    FixtureBundle bundle = frozenBundle();
    FixtureDefaults& defaults = bundle.preferences.defaults;
    defaults.theme = QStringLiteral("light");
    defaults.motion = QStringLiteral("reduced");
    defaults.volume_percent = 35;
    defaults.muted = true;
    defaults.skip_backward_seconds = 7;
    defaults.skip_forward_seconds = 20;
    defaults.close_behavior = QStringLiteral("exit");
    defaults.restore_last_session = false;
    defaults.start_minimized = true;
    defaults.queue_end_behavior = QStringLiteral("stop_clear_selection");

    PlayerPresenter presenter(std::move(bundle), platform);
    const Preferences& preferences = presenter.preferences();
    QCOMPARE(preferences.theme, ThemeMode::Light);
    QCOMPARE(preferences.motion, MotionMode::Reduced);
    QCOMPARE(preferences.volume, 0.35);
    QCOMPARE(preferences.muted, true);
    QCOMPARE(preferences.skip_backward_seconds, 7);
    QCOMPARE(preferences.skip_forward_seconds, 20);
    QCOMPARE(preferences.close_behavior, CloseBehavior::Exit);
    QCOMPARE(preferences.restore_last_session, false);
    QCOMPARE(preferences.start_minimized, true);
    QCOMPARE(preferences.queue_end_behavior, QueueEndBehavior::StopClearSelection);
    QCOMPARE(presenter.volume(), 0.35);
    QCOMPARE(presenter.muted(), true);
}

void PlayerPresenterTests::loadsFrozenQueueOnceWithoutReplacingTheModel() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QueueListModel* const identity = presenter.queueModel();
    QAbstractItemModelTester tester(identity, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy state_changed(&presenter, &PlayerPresenter::stateChanged);
    QSignalSpy playback_changed(&presenter, &PlayerPresenter::playbackChanged);
    bool reset_was_atomic = false;
    connect(identity, &QAbstractItemModel::modelReset, &presenter, [&] {
        reset_was_atomic = identity->rowCount() == 1000 && presenter.queueLoaded() &&
                           presenter.stateId() == QStringLiteral("playing") &&
                           presenter.currentOccurrenceId() == QStringLiteral("queue-0017") &&
                           presenter.durationMs() == 248'000;
    });

    QVERIFY(presenter.loadFrozenQueue());
    QCOMPARE(presenter.queueModel(), identity);
    QCOMPARE(identity->rowCount(), 1000);
    QCOMPARE(presenter.stateId(), QStringLiteral("playing"));
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("queue-0017"));
    QCOMPARE(presenter.currentQueueIndex(), 16);
    QCOMPARE(presenter.hasCurrentOccurrence(), true);
    QCOMPARE(presenter.hasContextOccurrence(), false);
    QCOMPARE(presenter.contextOccurrenceId(), QString{});
    QCOMPARE(presenter.hasPosition(), true);
    QCOMPARE(presenter.positionMs(), 84'000);
    QCOMPARE(presenter.durationMs(), 248'000);
    QCOMPARE(presenter.hasDuration(), true);
    QCOMPARE(presenter.seekable(), true);
    QCOMPARE(presenter.assetId(), QStringLiteral("cover-track"));
    QCOMPARE(presenter.hasMessage(), false);
    QCOMPARE(presenter.messageKey(), QString{});
    QCOMPARE(presenter.primaryActionId(), QStringLiteral("play-pause"));
    QCOMPARE(presenter.hasError(), false);
    QCOMPARE(presenter.errorCode(), QString{});
    QCOMPARE(state_changed.size(), 1);
    QCOMPARE(playback_changed.size(), 1);
    QCOMPARE(reset_was_atomic, true);

    QVERIFY(!presenter.loadFrozenQueue());
    QCOMPARE(presenter.queueModel(), identity);
    QCOMPARE(identity->rowCount(), 1000);
    QCOMPARE(state_changed.size(), 1);
    QCOMPARE(playback_changed.size(), 1);
}

void PlayerPresenterTests::derivesBackendSnapshotFromTheTypedPlayingState() {
    AvailableTrayPlatform platform;
    FixtureBundle bundle = frozenBundle();
    FixtureState& playing = bundle.states[1];
    playing.playback_state = QStringLiteral("paused");
    playing.position_ms = 12'000;
    playing.duration_ms = 30'000;
    playing.seekable = false;

    PlayerPresenter presenter(std::move(bundle), platform);
    QVERIFY(presenter.loadFrozenQueue());
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QCOMPARE(presenter.positionMs(), 12'000);
    QCOMPARE(presenter.durationMs(), 30'000);
    QCOMPARE(presenter.seekable(), false);
}

void PlayerPresenterTests::rejectsUnknownMappingsAndExposesNoInfrastructureApi() {
    AvailableTrayPlatform platform;
    for (const QString field :
         {QStringLiteral("theme"), QStringLiteral("motion"), QStringLiteral("close"), QStringLiteral("queue-end"),
          QStringLiteral("playback"), QStringLiteral("default-state"), QStringLiteral("playing-missing")}) {
        FixtureBundle bundle = frozenBundle();
        if (field == QStringLiteral("theme"))
            bundle.preferences.defaults.theme = QStringLiteral("unknown");
        if (field == QStringLiteral("motion"))
            bundle.preferences.defaults.motion = QStringLiteral("unknown");
        if (field == QStringLiteral("close"))
            bundle.preferences.defaults.close_behavior = QStringLiteral("unknown");
        if (field == QStringLiteral("queue-end"))
            bundle.preferences.defaults.queue_end_behavior = QStringLiteral("unknown");
        if (field == QStringLiteral("playback"))
            bundle.states[1].playback_state = QStringLiteral("unknown");
        if (field == QStringLiteral("default-state"))
            bundle.default_state_id = QStringLiteral("missing");
        if (field == QStringLiteral("playing-missing"))
            bundle.states.removeAt(1);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                 static_cast<void>(PlayerPresenter(std::move(bundle), platform)));
    }

    PlayerPresenter presenter(frozenBundle(), platform);
    const QMetaObject* meta = presenter.metaObject();
    QVERIFY(meta->indexOfMethod("openAudio()") >= 0);
    QVERIFY(meta->indexOfMethod("loadFrozenQueue()") >= 0);
    for (const char* method : {"togglePlayback()", "skipBackward()", "skipForward()", "beginSeekPreview(qlonglong)",
                               "updateSeekPreview(qlonglong)", "commitSeekPreview()", "setVolume(double)",
                               "toggleMute()", "skipUnavailable()", "previousTrack()", "nextTrack()"})
        QVERIFY(meta->indexOfMethod(method) >= 0);
    const int queue_model_property = meta->indexOfProperty("queueModel");
    QVERIFY(queue_model_property >= 0);
    QCOMPARE(meta->property(queue_model_property).metaType().id(), qMetaTypeId<QAbstractItemModel*>());
    for (const char* property_name : {"volume", "muted", "canGoPrevious", "canGoNext"}) {
        const int property_index = meta->indexOfProperty(property_name);
        QVERIFY(property_index >= 0);
        QVERIFY(meta->property(property_index).hasNotifySignal());
    }
    QCOMPARE(meta->indexOfProperty("fixturePath"), -1);
    QCOMPARE(meta->indexOfProperty("queuePath"), -1);
    QCOMPARE(meta->indexOfProperty("backend"), -1);
    QCOMPARE(meta->indexOfProperty("timer"), -1);
    for (const char* method : {"advanceOneTick()", "rejectNextSeekForTest()", "enterFrozenUnavailableScenario()",
                               "loadFrozenUnavailableScenario()", "applyBackendSnapshot()"})
        QCOMPARE(meta->indexOfMethod(method), -1);
    QCOMPARE(presenter.findChildren<QTimer*>().size(), 1);
}

void PlayerPresenterTests::drivesTransportPreferencesAndManualTicksDeterministically() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);
    QSignalSpy preferences(&presenter, &PlayerPresenter::preferencesChanged);
    QSignalSpy rejected(&presenter, &PlayerPresenter::seekRejected);

    QVERIFY(!presenter.advanceOneTick());
    QCOMPARE(playback.size(), 0);
    QVERIFY(presenter.loadFrozenQueue());
    playback.clear();
    QCOMPARE(presenter.confirmedPositionMs(), 84'000);
    QCOMPARE(presenter.skipBackwardSeconds(), 5);
    QCOMPARE(presenter.skipForwardSeconds(), 5);

    QVERIFY(presenter.togglePlayback());
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QCOMPARE(playback.size(), 1);
    QVERIFY(!presenter.advanceOneTick());
    QCOMPARE(playback.size(), 1);
    QVERIFY(presenter.togglePlayback());
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);

    QVERIFY(presenter.skipBackward());
    QCOMPARE(presenter.positionMs(), 79'000);
    QCOMPARE(presenter.confirmedPositionMs(), 84'000);
    QVERIFY(presenter.advanceOneTick());
    QCOMPARE(presenter.confirmedPositionMs(), 79'000);
    QVERIFY(presenter.skipForward());
    QCOMPARE(presenter.positionMs(), 84'000);
    QCOMPARE(presenter.confirmedPositionMs(), 79'000);
    QVERIFY(presenter.seekPreviewActive());
    QVERIFY(presenter.seekPending());
    QVERIFY(!presenter.skipBackward());
    QVERIFY(presenter.advanceOneTick());
    QCOMPARE(presenter.positionMs(), 84'000);
    QCOMPARE(presenter.confirmedPositionMs(), 84'000);
    QVERIFY(!presenter.seekPreviewActive());
    QVERIFY(!presenter.seekPending());

    QVERIFY(presenter.beginSeekPreview(std::numeric_limits<qint64>::max()));
    QVERIFY(!presenter.beginSeekPreview(1));
    QCOMPARE(presenter.positionMs(), presenter.durationMs());
    QVERIFY(presenter.advanceOneTick());
    QCOMPARE(presenter.positionMs(), presenter.durationMs());
    QCOMPARE(presenter.confirmedPositionMs(), 84'200);
    QVERIFY(presenter.updateSeekPreview(std::numeric_limits<qint64>::min()));
    QCOMPARE(presenter.positionMs(), 0);
    const int same_update_signals = playback.size();
    QVERIFY(presenter.updateSeekPreview(0));
    QCOMPARE(playback.size(), same_update_signals);
    QVERIFY(presenter.commitSeekPreview());
    QVERIFY(!presenter.commitSeekPreview());
    QStringList rejection_signal_order;
    connect(&presenter, &PlayerPresenter::playbackChanged, &presenter,
            [&rejection_signal_order] { rejection_signal_order.append(QStringLiteral("playback")); });
    connect(&presenter, &PlayerPresenter::seekRejected, &presenter,
            [&rejection_signal_order] { rejection_signal_order.append(QStringLiteral("rejected")); });
    const int playback_before_rejection = playback.size();
    PlayerPresenterTestAccess::rejectNextSeek(presenter);
    QVERIFY(presenter.advanceOneTick());
    QCOMPARE(rejected.size(), 1);
    QCOMPARE(playback.size(), playback_before_rejection + 1);
    QCOMPARE(rejection_signal_order, QStringList({QStringLiteral("playback"), QStringLiteral("rejected")}));
    QCOMPARE(presenter.positionMs(), 84'200);
    QVERIFY(presenter.advanceOneTick());
    QCOMPARE(rejected.size(), 1);
    QCOMPARE(presenter.positionMs(), 84'400);

    QVERIFY(presenter.setVolume(0.25));
    QCOMPARE(presenter.volume(), 0.25);
    QCOMPARE(preferences.size(), 1);
    QVERIFY(!presenter.setVolume(std::numeric_limits<double>::quiet_NaN()));
    QVERIFY(!presenter.setVolume(std::numeric_limits<double>::infinity()));
    QVERIFY(!presenter.setVolume(-0.1));
    QVERIFY(!presenter.setVolume(2.0));
    QVERIFY(!presenter.setVolume(0.25));
    QCOMPARE(preferences.size(), 1);
    QVERIFY(presenter.setVolume(0.0));
    QVERIFY(presenter.toggleMute());
    QVERIFY(presenter.muted());
    QVERIFY(presenter.toggleMute());
    QVERIFY(!presenter.muted());
    QCOMPARE(presenter.volume(), 0.25);
}

void PlayerPresenterTests::keepsUnavailableAndTerminalTransportInert() {
    AvailableTrayPlatform platform;
    PlayerPresenter empty(frozenBundle(), platform);
    QSignalSpy empty_playback(&empty, &PlayerPresenter::playbackChanged);
    QVERIFY(!empty.togglePlayback());
    QVERIFY(!empty.skipBackward());
    QVERIFY(!empty.beginSeekPreview(1));
    QCOMPARE(empty_playback.size(), 0);

    FixtureBundle error_bundle = frozenBundle();
    error_bundle.states[1].playback_state = QStringLiteral("error");
    PlayerPresenter error(std::move(error_bundle), platform);
    QVERIFY(error.loadFrozenQueue());
    QSignalSpy error_playback(&error, &PlayerPresenter::playbackChanged);
    QVERIFY(!error.togglePlayback());
    QVERIFY(!error.skipForward());
    QVERIFY(!error.beginSeekPreview(10));
    QVERIFY(!error.commitSeekPreview());
    QVERIFY(!error.advanceOneTick());
    QCOMPARE(error_playback.size(), 0);

    FixtureBundle terminal_bundle = frozenBundle();
    terminal_bundle.states[1].position_ms = 247'900;
    PlayerPresenter terminal(std::move(terminal_bundle), platform);
    QVERIFY(terminal.loadFrozenQueue());
    QVERIFY(terminal.advanceOneTick());
    QCOMPARE(terminal.positionMs(), 248'000);
    QCOMPARE(terminal.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QSignalSpy terminal_playback(&terminal, &PlayerPresenter::playbackChanged);
    QVERIFY(!terminal.togglePlayback());
    QCOMPARE(terminal_playback.size(), 0);

    FixtureBundle paused_bundle = frozenBundle();
    paused_bundle.states[1].playback_state = QStringLiteral("paused");
    PlayerPresenter paused(std::move(paused_bundle), platform);
    QVERIFY(paused.loadFrozenQueue());
    QVERIFY(paused.beginSeekPreview(90'000));
    QVERIFY(paused.commitSeekPreview());
    QVERIFY(paused.advanceOneTick());
    QCOMPARE(paused.positionMs(), 90'000);
    QCOMPARE(paused.playbackState(), PlayerPresenter::PlaybackState::Paused);
}

void PlayerPresenterTests::schedulesTicksOnlyWhilePlaybackOrSeekNeedsThem() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    const QList<QTimer*> timers = presenter.findChildren<QTimer*>(QString{}, Qt::FindDirectChildrenOnly);
    QCOMPARE(timers.size(), 1);
    QTimer* const timer = timers.front();
    QCOMPARE(timer->interval(), 200);
    QCOMPARE(timer->timerType(), Qt::PreciseTimer);
    QVERIFY(!timer->isActive());
    QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);
    QTest::qWait(250);
    QCOMPARE(playback.size(), 0);

    QVERIFY(presenter.loadFrozenQueue());
    QVERIFY(timer->isActive());
    QTRY_COMPARE_WITH_TIMEOUT(presenter.confirmedPositionMs(), 84'200, 500);
    QVERIFY(presenter.togglePlayback());
    QVERIFY(!timer->isActive());
    const qint64 paused_position = presenter.confirmedPositionMs();
    QTest::qWait(250);
    QCOMPARE(presenter.confirmedPositionMs(), paused_position);

    QVERIFY(presenter.beginSeekPreview(90'000));
    QVERIFY(presenter.commitSeekPreview());
    QVERIFY(timer->isActive());
    QTRY_COMPARE_WITH_TIMEOUT(presenter.confirmedPositionMs(), 90'000, 500);
    QVERIFY(!presenter.seekPending());
    QVERIFY(!timer->isActive());

    QSignalSpy rejected(&presenter, &PlayerPresenter::seekRejected);
    QVERIFY(presenter.beginSeekPreview(91'000));
    QVERIFY(presenter.commitSeekPreview());
    PlayerPresenterTestAccess::rejectNextSeek(presenter);
    QVERIFY(timer->isActive());
    QTRY_COMPARE_WITH_TIMEOUT(rejected.size(), 1, 500);
    QVERIFY(!presenter.seekPending());
    QVERIFY(!timer->isActive());

    QVERIFY(presenter.togglePlayback());
    QVERIFY(timer->isActive());
    QVERIFY(presenter.togglePlayback());
    QVERIFY(!timer->isActive());

    FixtureBundle terminal_bundle = frozenBundle();
    terminal_bundle.states[1].position_ms = 247'900;
    PlayerPresenter terminal(std::move(terminal_bundle), platform);
    QTimer* const terminal_timer = terminal.findChild<QTimer*>(QString{}, Qt::FindDirectChildrenOnly);
    QVERIFY(terminal_timer != nullptr);
    QVERIFY(terminal.loadFrozenQueue());
    QVERIFY(terminal_timer->isActive());
    QTRY_COMPARE_WITH_TIMEOUT(terminal.playbackState(), PlayerPresenter::PlaybackState::Paused, 500);
    QCOMPARE(terminal.positionMs(), 248'000);
    QVERIFY(!terminal_timer->isActive());
}

void PlayerPresenterTests::projectsTypedMetadataAndRecoversFrozenUnavailableScenario() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QueueListModel* const model = presenter.queueModel();
    QVERIFY(presenter.loadFrozenQueue());
    QTimer* const timer = presenter.findChild<QTimer*>(QString{}, Qt::FindDirectChildrenOnly);
    QVERIFY(timer != nullptr);
    QVERIFY(timer->isActive());
    QCOMPARE(presenter.hasCurrentTrack(), true);
    QCOMPARE(presenter.currentTitle(), QStringLiteral("Canção nº 17 — São João, ação e café ☕"));
    QCOMPARE(presenter.currentArtist(), QStringLiteral("Artista Sintética Ç"));
    QCOMPARE(presenter.currentAlbum(), QStringLiteral("Coleção Ação e Emoção"));
    QCOMPARE(presenter.hasContextTrack(), false);
    QCOMPARE(presenter.contextTitle(), QString{});
    QCOMPARE(presenter.contextArtist(), QString{});
    QCOMPARE(presenter.contextAlbum(), QString{});

    QVERIFY(presenter.beginSeekPreview(124'000));
    QVERIFY(presenter.commitSeekPreview());
    QVERIFY(presenter.seekPreviewActive());
    QVERIFY(presenter.seekPending());

    QSignalSpy state_changed(&presenter, &PlayerPresenter::stateChanged);
    QSignalSpy playback_changed(&presenter, &PlayerPresenter::playbackChanged);
    QVERIFY(experiment::PlayerPresenterScenarioControl::enterUnavailable(presenter));
    QCOMPARE(presenter.queueModel(), model);
    QCOMPARE(model->rowCount(), 1000);
    QCOMPARE(presenter.stateId(), QStringLiteral("error"));
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Error);
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("queue-0999"));
    QCOMPARE(presenter.currentQueueIndex(), 998);
    QCOMPARE(presenter.contextOccurrenceId(), QStringLiteral("queue-0017"));
    QCOMPARE(presenter.hasPosition(), false);
    QCOMPARE(presenter.positionMs(), 0);
    QCOMPARE(presenter.hasDuration(), false);
    QCOMPARE(presenter.durationMs(), 0);
    QCOMPARE(presenter.seekable(), false);
    QCOMPARE(presenter.seekPreviewActive(), false);
    QCOMPARE(presenter.seekPending(), false);
    QCOMPARE(presenter.hasError(), true);
    QCOMPARE(presenter.errorCode(), QStringLiteral("fixture_source_unavailable"));
    QCOMPARE(presenter.assetId(), QStringLiteral("cover-track"));
    QCOMPARE(presenter.messageKey(), QStringLiteral("error_body"));
    QCOMPARE(presenter.primaryActionId(), QStringLiteral("skip"));
    QCOMPARE(presenter.hasCurrentTrack(), true);
    QCOMPARE(presenter.currentTitle(), QStringLiteral("Faixa indisponível de teste"));
    QCOMPARE(presenter.currentArtist(), QStringLiteral("Artista sintético indisponível"));
    QCOMPARE(presenter.currentAlbum(), QStringLiteral("Falhas recuperáveis"));
    QCOMPARE(presenter.hasContextTrack(), true);
    QCOMPARE(presenter.contextTitle(), QStringLiteral("Canção nº 17 — São João, ação e café ☕"));
    QCOMPARE(presenter.contextArtist(), QStringLiteral("Artista Sintética Ç"));
    QCOMPARE(presenter.contextAlbum(), QStringLiteral("Coleção Ação e Emoção"));
    QCOMPARE(state_changed.size(), 1);
    QCOMPARE(playback_changed.size(), 1);
    QVERIFY(!timer->isActive());
    QVERIFY(!presenter.advanceOneTick());
    QCOMPARE(state_changed.size(), 1);
    QCOMPARE(playback_changed.size(), 1);

    QVERIFY(presenter.skipUnavailable());
    QCOMPARE(presenter.queueModel(), model);
    QCOMPARE(model->rowCount(), 1000);
    QCOMPARE(presenter.stateId(), QStringLiteral("playing"));
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("queue-0017"));
    QCOMPARE(presenter.currentQueueIndex(), 16);
    QCOMPARE(presenter.hasContextTrack(), false);
    QCOMPARE(state_changed.size(), 2);
    QCOMPARE(playback_changed.size(), 2);
    QVERIFY(timer->isActive());
    QVERIFY(!presenter.skipUnavailable());
    QCOMPARE(state_changed.size(), 2);
    QCOMPARE(playback_changed.size(), 2);
}

void PlayerPresenterTests::navigatesDistinctOccurrencesWithoutResettingTheModel() {
    AvailableTrayPlatform platform;
    const FixtureBundle bundle = frozenBundle();
    PlayerPresenter presenter(bundle, platform);
    QVERIFY(presenter.loadFrozenQueue());
    QVERIFY(presenter.setVolume(0.35));
    QVERIFY(presenter.toggleMute());
    QueueListModel* const model = presenter.queueModel();
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    QSignalSpy states(&presenter, &PlayerPresenter::stateChanged);
    QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);

    QVERIFY(presenter.canGoPrevious());
    QVERIFY(presenter.canGoNext());
    QVERIFY(presenter.nextTrack());
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("queue-0018"));
    QCOMPARE(presenter.currentQueueIndex(), 17);
    QCOMPARE(presenter.currentTitle(), bundle.queue.at(17).title);
    QCOMPARE(presenter.currentArtist(), bundle.queue.at(17).artist);
    QCOMPARE(presenter.currentAlbum(), bundle.queue.at(17).album);
    QCOMPARE(presenter.durationMs(), *bundle.queue.at(17).duration_ms);
    QCOMPARE(presenter.positionMs(), 0);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QCOMPARE(presenter.volume(), 0.35);
    QVERIFY(presenter.muted());
    QCOMPARE(states.size(), 1);
    QCOMPARE(playback.size(), 1);
    QCOMPARE(presenter.queueModel(), model);
    QCOMPARE(model->rowCount(), 1000);
    QCOMPARE(resets.size(), 0);

    QVERIFY(presenter.previousTrack());
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("queue-0017"));
    QCOMPARE(presenter.positionMs(), 0);
    QVERIFY(presenter.advanceOneTick());
    QCOMPARE(presenter.positionMs(), 200);
    QVERIFY(presenter.nextTrack());
    QVERIFY(presenter.nextTrack());
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("queue-0019"));
    QCOMPARE(resets.size(), 0);
}

void PlayerPresenterTests::previousUsesConfirmedThresholdAndPreservesPause() {
    AvailableTrayPlatform platform;
    for (const qint64 position : {2'999, 3'000, 3'001}) {
        FixtureBundle bundle = frozenBundle();
        bundle.states[1].playback_state = QStringLiteral("paused");
        bundle.states[1].position_ms = position;
        PlayerPresenter presenter(std::move(bundle), platform);
        QVERIFY(presenter.loadFrozenQueue());
        QSignalSpy states(&presenter, &PlayerPresenter::stateChanged);
        QVERIFY(presenter.previousTrack());
        QCOMPARE(presenter.positionMs(), 0);
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
        if (position > 3'000) {
            QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("queue-0017"));
            QCOMPARE(presenter.confirmedPositionMs(), position);
            QVERIFY(presenter.seekPending());
            QVERIFY(!presenter.previousTrack());
            QVERIFY(presenter.advanceOneTick());
            QVERIFY(!presenter.seekPending());
            QCOMPARE(presenter.confirmedPositionMs(), 0);
            QCOMPARE(states.size(), 0);
        } else {
            QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("queue-0016"));
            QVERIFY(!presenter.seekPending());
            QCOMPARE(states.size(), 1);
        }
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    }
}

void PlayerPresenterTests::navigationSkipsUnavailableItemsAndStopsAtBounds() {
    AvailableTrayPlatform platform;
    FixtureBundle bundle = frozenBundle();
    bundle.states[1].current_occurrence_id = QStringLiteral("queue-0998");
    bundle.states[1].position_ms = 0;
    bundle.states[1].duration_ms = bundle.queue.at(997).duration_ms;
    bundle.states[1].playback_state = QStringLiteral("paused");
    PlayerPresenter presenter(bundle, platform);
    QVERIFY(presenter.loadFrozenQueue());
    QueueListModel* const model = presenter.queueModel();
    QVERIFY(presenter.nextTrack());
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("queue-1000"));
    QCOMPARE(presenter.currentQueueIndex(), 999);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QVERIFY(!presenter.canGoNext());
    QVERIFY(!presenter.nextTrack());
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("queue-1000"));
    QVERIFY(!model->data(model->index(998, 0), QueueListModel::AvailableRole).toBool());
    QCOMPARE(model->data(model->index(998, 0), QueueListModel::ErrorCodeRole).toString(),
             QStringLiteral("fixture_source_unavailable"));
    QVERIFY(presenter.previousTrack());
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("queue-0998"));
    QCOMPARE(presenter.queueModel(), model);

    bundle.states[1].current_occurrence_id = QStringLiteral("queue-0001");
    bundle.states[1].duration_ms = bundle.queue.at(0).duration_ms;
    PlayerPresenter first(bundle, platform);
    QVERIFY(first.loadFrozenQueue());
    QVERIFY(!first.canGoPrevious());
    QVERIFY(!first.previousTrack());
    QCOMPARE(first.currentOccurrenceId(), QStringLiteral("queue-0001"));
    QCOMPARE(first.positionMs(), 0);
}

void PlayerPresenterTests::navigationIsInertDuringSeekAndInactiveStates() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QVERIFY(!presenter.canGoPrevious());
    QVERIFY(!presenter.canGoNext());
    QVERIFY(!presenter.previousTrack());
    QVERIFY(!presenter.nextTrack());
    QVERIFY(presenter.loadFrozenQueue());
    QVERIFY(presenter.beginSeekPreview(120'000));
    QVERIFY(!presenter.canGoPrevious());
    QVERIFY(!presenter.canGoNext());
    QVERIFY(!presenter.previousTrack());
    QVERIFY(!presenter.nextTrack());
    QVERIFY(presenter.commitSeekPreview());
    QVERIFY(!presenter.canGoPrevious());
    QVERIFY(!presenter.canGoNext());
    QVERIFY(!presenter.previousTrack());
    QVERIFY(!presenter.nextTrack());
    QVERIFY(presenter.advanceOneTick());
    QVERIFY(presenter.canGoPrevious());
    QVERIFY(presenter.canGoNext());
    QVERIFY(presenter.nextTrack());
    QVERIFY(experiment::PlayerPresenterScenarioControl::enterUnavailable(presenter));
    QVERIFY(!presenter.canGoPrevious());
    QVERIFY(!presenter.canGoNext());
    QVERIFY(!presenter.previousTrack());
    QVERIFY(!presenter.nextTrack());
    QVERIFY(presenter.skipUnavailable());
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("queue-0017"));
    QCOMPARE(presenter.positionMs(), 84'000);
}

void PlayerPresenterTests::exposesTypedPreferenceDefaults() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QCOMPARE(presenter.themePreference(), PlayerPresenter::ThemePreference::SystemTheme);
    QCOMPARE(presenter.motionPreference(), PlayerPresenter::MotionPreference::SystemMotion);
    QCOMPARE(presenter.closePreference(), PlayerPresenter::ClosePreference::MinimizeToTray);
    QCOMPARE(presenter.queueEndPreference(), PlayerPresenter::QueueEndPreference::StopKeepSelection);
    QVERIFY(presenter.restoreLastSession());
    QVERIFY(!presenter.startMinimized());
    QVERIFY(presenter.trayAvailable());
    const QMetaObject* meta = presenter.metaObject();
    for (const char* name : {"themePreference", "motionPreference", "closePreference", "queueEndPreference",
                             "restoreLastSession", "startMinimized", "trayAvailable"}) {
        const int index = meta->indexOfProperty(name);
        QVERIFY(index >= 0);
        const QMetaProperty property = meta->property(index);
        QVERIFY(property.isReadable());
        QVERIFY(!property.isWritable());
        QVERIFY(property.hasNotifySignal());
    }
    for (const char* name : {"themePreference", "motionPreference", "closePreference", "queueEndPreference"})
        QVERIFY(meta->property(meta->indexOfProperty(name)).isEnumType());
    QCOMPARE(meta->indexOfMethod("refreshPlatformAvailability()"), -1);
}

void PlayerPresenterTests::editsPreferencesWithoutChangingPlaybackOrTheQueue() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QVERIFY(presenter.loadFrozenQueue());
    QVERIFY(presenter.togglePlayback());
    QueueListModel* const model = presenter.queueModel();
    QSignalSpy preferences(&presenter, &PlayerPresenter::preferencesChanged);
    QSignalSpy states(&presenter, &PlayerPresenter::stateChanged);
    QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    QVERIFY(presenter.setThemePreference(PlayerPresenter::ThemePreference::DarkTheme));
    QCOMPARE(presenter.preferences().theme, ThemeMode::Dark);
    QVERIFY(presenter.setThemePreference(PlayerPresenter::ThemePreference::LightTheme));
    QCOMPARE(presenter.preferences().theme, ThemeMode::Light);
    QVERIFY(presenter.setThemePreference(PlayerPresenter::ThemePreference::SystemTheme));
    QCOMPARE(presenter.preferences().theme, ThemeMode::System);
    QVERIFY(presenter.setMotionPreference(PlayerPresenter::MotionPreference::ReducedMotion));
    QCOMPARE(presenter.preferences().motion, MotionMode::Reduced);
    QVERIFY(presenter.setMotionPreference(PlayerPresenter::MotionPreference::SystemMotion));
    QCOMPARE(presenter.preferences().motion, MotionMode::System);
    QVERIFY(presenter.setSkipBackwardSeconds(7));
    QVERIFY(presenter.setSkipForwardSeconds(12));
    QVERIFY(presenter.setClosePreference(PlayerPresenter::ClosePreference::Exit));
    QVERIFY(presenter.setRestoreLastSession(false));
    QVERIFY(presenter.setStartMinimized(true));
    QVERIFY(presenter.setQueueEndPreference(PlayerPresenter::QueueEndPreference::StopClearSelection));
    QCOMPARE(preferences.size(), 11);
    QCOMPARE(presenter.skipBackwardSeconds(), 7);
    QCOMPARE(presenter.skipForwardSeconds(), 12);
    QCOMPARE(presenter.preferences().close_behavior, CloseBehavior::Exit);
    QVERIFY(!presenter.restoreLastSession());
    QVERIFY(presenter.startMinimized());
    QCOMPARE(presenter.preferences().queue_end_behavior, QueueEndBehavior::StopClearSelection);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("queue-0017"));
    QCOMPARE(presenter.positionMs(), 84'000);
    QCOMPARE(presenter.volume(), 0.8);
    QVERIFY(!presenter.muted());
    QCOMPARE(presenter.queueModel(), model);
    QCOMPARE(resets.size(), 0);
    QCOMPARE(states.size(), 0);
    QCOMPARE(playback.size(), 0);

    QVERIFY(presenter.skipBackward());
    QCOMPARE(presenter.positionMs(), 77'000);
    QVERIFY(presenter.advanceOneTick());
    QVERIFY(presenter.skipForward());
    QCOMPARE(presenter.positionMs(), 89'000);
    QVERIFY(presenter.advanceOneTick());
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
}

void PlayerPresenterTests::rejectsInvalidAndUnchangedPreferencesWithoutNotifications() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QSignalSpy changed(&presenter, &PlayerPresenter::preferencesChanged);
    for (const int value : {-1, 99}) {
        QVERIFY(!presenter.setThemePreference(static_cast<PlayerPresenter::ThemePreference>(value)));
        QVERIFY(!presenter.setMotionPreference(static_cast<PlayerPresenter::MotionPreference>(value)));
        QVERIFY(!presenter.setClosePreference(static_cast<PlayerPresenter::ClosePreference>(value)));
        QVERIFY(!presenter.setQueueEndPreference(static_cast<PlayerPresenter::QueueEndPreference>(value)));
    }
    for (const int value : {std::numeric_limits<int>::min(), 0, 301, std::numeric_limits<int>::max()}) {
        QVERIFY(!presenter.setSkipBackwardSeconds(value));
        QVERIFY(!presenter.setSkipForwardSeconds(value));
    }
    QVERIFY(!presenter.setThemePreference(PlayerPresenter::ThemePreference::SystemTheme));
    QVERIFY(!presenter.setMotionPreference(PlayerPresenter::MotionPreference::SystemMotion));
    QVERIFY(!presenter.setClosePreference(PlayerPresenter::ClosePreference::MinimizeToTray));
    QVERIFY(!presenter.setQueueEndPreference(PlayerPresenter::QueueEndPreference::StopKeepSelection));
    QVERIFY(!presenter.setRestoreLastSession(true));
    QVERIFY(!presenter.setStartMinimized(false));
    QVERIFY(!presenter.setSkipBackwardSeconds(5));
    QVERIFY(!presenter.setSkipForwardSeconds(5));
    QCOMPARE(changed.size(), 0);
    QCOMPARE(presenter.themePreference(), PlayerPresenter::ThemePreference::SystemTheme);
    QCOMPARE(presenter.motionPreference(), PlayerPresenter::MotionPreference::SystemMotion);
    QCOMPARE(presenter.closePreference(), PlayerPresenter::ClosePreference::MinimizeToTray);
    QCOMPARE(presenter.queueEndPreference(), PlayerPresenter::QueueEndPreference::StopKeepSelection);
    QCOMPARE(presenter.skipBackwardSeconds(), 5);
    QCOMPARE(presenter.skipForwardSeconds(), 5);
    for (const int value : {1, 300}) {
        QVERIFY(presenter.setSkipBackwardSeconds(value));
        QVERIFY(presenter.setSkipForwardSeconds(value));
    }
    QCOMPARE(changed.size(), 4);
}

void PlayerPresenterTests::refreshesTrayAvailabilityAndGuardsStartMinimized() {
    AvailableTrayPlatform platform;
    platform.available = false;
    PlayerPresenter presenter(frozenBundle(), platform);
    QSignalSpy preferences(&presenter, &PlayerPresenter::preferencesChanged);
    QSignalSpy availability(&presenter, &PlayerPresenter::platformChanged);
    QVERIFY(!presenter.trayAvailable());
    QVERIFY(!presenter.setStartMinimized(true));
    QVERIFY(!presenter.startMinimized());
    QCOMPARE(preferences.size(), 0);
    presenter.refreshPlatformAvailability();
    QCOMPARE(availability.size(), 0);
    platform.available = true;
    presenter.refreshPlatformAvailability();
    QCOMPARE(availability.size(), 1);
    QVERIFY(presenter.trayAvailable());
    QVERIFY(presenter.setStartMinimized(true));
    QVERIFY(presenter.startMinimized());
    QCOMPARE(preferences.size(), 1);
    platform.available = false;
    presenter.refreshPlatformAvailability();
    QCOMPARE(availability.size(), 2);
    QVERIFY(!presenter.trayAvailable());
    QVERIFY(!presenter.startMinimized());
    QCOMPARE(preferences.size(), 2);
    presenter.refreshPlatformAvailability();
    QCOMPARE(availability.size(), 2);
    QCOMPARE(preferences.size(), 2);
}

void PlayerPresenterTests::exposesReadOnlyLifecycleDefaultsAndOnlyIntentMethods() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QVERIFY(!presenter.exiting());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
    QVERIFY(presenter.trayNoticeEnabled());
    const QMetaObject* const meta = presenter.metaObject();
    for (const char* name : {"exiting", "windowNotice", "trayNoticeEnabled"}) {
        const int index = meta->indexOfProperty(name);
        QVERIFY(index >= 0);
        const QMetaProperty property = meta->property(index);
        QVERIFY(property.isReadable());
        QVERIFY(!property.isWritable());
        QCOMPARE(property.notifySignal().methodSignature(), QByteArray("lifecycleChanged()"));
    }
    const QMetaProperty notice = meta->property(meta->indexOfProperty("windowNotice"));
    QVERIFY(notice.isEnumType());
    QCOMPARE(notice.enumerator().keyCount(), 3);
    QCOMPARE(notice.enumerator().keyToValue("NoNotice"), 0);
    QCOMPARE(notice.enumerator().keyToValue("TrayCloseNotice"), 1);
    QCOMPARE(notice.enumerator().keyToValue("TrayUnavailableNotice"), 2);
    const int fixture_mode_index = meta->indexOfProperty("usesFrozenFixture");
    QVERIFY(fixture_mode_index >= 0);
    const QMetaProperty fixture_mode = meta->property(fixture_mode_index);
    QVERIFY(fixture_mode.isReadable());
    QVERIFY(!fixture_mode.isWritable());
    QVERIFY(fixture_mode.isConstant());
    QVERIFY(!fixture_mode.hasNotifySignal());
    const QSet<QByteArray> expected{"openAudio",
                                    "loadFrozenQueue",
                                    "togglePlayback",
                                    "skipBackward",
                                    "skipForward",
                                    "previousTrack",
                                    "nextTrack",
                                    "beginSeekPreview",
                                    "updateSeekPreview",
                                    "commitSeekPreview",
                                    "setVolume",
                                    "toggleMute",
                                    "setThemePreference",
                                    "setMotionPreference",
                                    "setSkipBackwardSeconds",
                                    "setSkipForwardSeconds",
                                    "setClosePreference",
                                    "setRestoreLastSession",
                                    "setStartMinimized",
                                    "setQueueEndPreference",
                                    "skipUnavailable",
                                    "requestClose",
                                    "requestHide",
                                    "requestRestore",
                                    "requestExit",
                                    "confirmWindowNotice",
                                    "cancelWindowNotice",
                                    "setTrayNoticeEnabled"};
    QSet<QByteArray> actual;
    for (int index = meta->methodOffset(); index < meta->methodCount(); ++index) {
        const QMetaMethod method = meta->method(index);
        if (method.methodType() == QMetaMethod::Signal)
            continue;
        actual.insert(method.name());
        QCOMPARE(method.returnMetaType().id(), QMetaType::Bool);
    }
    QCOMPARE(actual, expected);
    for (const char* method : {"requestClose()", "requestHide()", "requestRestore()", "requestExit()",
                               "confirmWindowNotice()", "cancelWindowNotice()", "setTrayNoticeEnabled(bool)"})
        QVERIFY(meta->indexOfMethod(method) >= 0);
    QCOMPARE(meta->indexOfMethod("reportTrayUnavailable()"), -1);
    QCOMPARE(meta->indexOfMethod("refreshPlatformAvailability()"), -1);
    QCOMPARE(meta->indexOfProperty("noticeSeen"), -1);
    QCOMPARE(meta->indexOfProperty("backend"), -1);
    QCOMPARE(meta->indexOfProperty("timer"), -1);
}

void PlayerPresenterTests::firstCloseWaitsForAcknowledgementAndDoesNotRepeatTheNotice() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QSignalSpy lifecycle(&presenter, &PlayerPresenter::lifecycleChanged);
    QVERIFY(presenter.requestClose());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayCloseNotice);
    QCOMPARE(lifecycle.size(), 1);
    QCOMPARE(platform.hide_calls, 0);
    QCOMPARE(platform.exit_calls, 0);
    QVERIFY(!presenter.requestClose());
    QCOMPARE(lifecycle.size(), 1);
    QCOMPARE(platform.hide_calls, 0);
    QVERIFY(presenter.confirmWindowNotice());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
    QCOMPARE(platform.hide_calls, 1);
    QCOMPARE(lifecycle.size(), 2);
    QVERIFY(presenter.requestRestore());
    QCOMPARE(platform.restore_calls, 1);
    QVERIFY(presenter.requestClose());
    QCOMPARE(platform.hide_calls, 2);
    QCOMPARE(platform.minimize_calls, 0);
    QCOMPARE(platform.exit_calls, 0);
    QCOMPARE(lifecycle.size(), 2);
    QVERIFY(!presenter.confirmWindowNotice());
    QVERIFY(!presenter.cancelWindowNotice());
}

void PlayerPresenterTests::unavailableTrayMinimizesAndKeepsTheExplanationUntilRestored() {
    AvailableTrayPlatform platform;
    platform.available = false;
    PlayerPresenter presenter(frozenBundle(), platform);
    QSignalSpy lifecycle(&presenter, &PlayerPresenter::lifecycleChanged);
    QVERIFY(presenter.requestClose());
    QCOMPARE(platform.minimize_calls, 1);
    QCOMPARE(platform.hide_calls, 0);
    QCOMPARE(platform.exit_calls, 0);
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayUnavailableNotice);
    QCOMPARE(lifecycle.size(), 1);
    QVERIFY(presenter.requestRestore());
    QCOMPARE(platform.restore_calls, 1);
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayUnavailableNotice);
    QCOMPARE(lifecycle.size(), 1);
    QVERIFY(presenter.confirmWindowNotice());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
    QCOMPARE(platform.minimize_calls, 1);
    QCOMPARE(lifecycle.size(), 2);
}

void PlayerPresenterTests::cancellingTheFirstNoticeDoesNotConsumeIt() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QSignalSpy lifecycle(&presenter, &PlayerPresenter::lifecycleChanged);
    QVERIFY(presenter.requestClose());
    QVERIFY(presenter.cancelWindowNotice());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
    QCOMPARE(lifecycle.size(), 2);
    QVERIFY(!presenter.cancelWindowNotice());
    QVERIFY(presenter.requestClose());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayCloseNotice);
    QCOMPARE(lifecycle.size(), 3);
    QCOMPARE(platform.hide_calls, 0);
    QCOMPARE(platform.minimize_calls, 0);
    QCOMPARE(platform.exit_calls, 0);
}

void PlayerPresenterTests::noticeOptOutIsInMemoryAndDoesNotActOnThePendingWindow() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QSignalSpy lifecycle(&presenter, &PlayerPresenter::lifecycleChanged);
    QSignalSpy preferences(&presenter, &PlayerPresenter::preferencesChanged);
    QVERIFY(presenter.requestClose());
    QVERIFY(presenter.setTrayNoticeEnabled(false));
    QVERIFY(!presenter.setTrayNoticeEnabled(false));
    QVERIFY(!presenter.trayNoticeEnabled());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayCloseNotice);
    QCOMPARE(lifecycle.size(), 2);
    QCOMPARE(platform.hide_calls, 0);
    QVERIFY(presenter.cancelWindowNotice());
    QVERIFY(presenter.requestClose());
    QCOMPARE(platform.hide_calls, 1);
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
    QCOMPARE(lifecycle.size(), 3);

    QVERIFY(presenter.requestRestore());
    QVERIFY(presenter.setTrayNoticeEnabled(true));
    QVERIFY(presenter.requestClose());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayCloseNotice);
    QVERIFY(presenter.confirmWindowNotice());
    QCOMPARE(platform.hide_calls, 2);
    QVERIFY(presenter.setTrayNoticeEnabled(false));
    QVERIFY(presenter.setTrayNoticeEnabled(true));
    QVERIFY(presenter.requestRestore());
    QVERIFY(presenter.requestClose());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
    QCOMPARE(platform.hide_calls, 3);
    QCOMPARE(lifecycle.size(), 8);
    QCOMPARE(preferences.size(), 0);
    QCOMPARE(platform.exit_calls, 0);

    AvailableTrayPlatform another_platform;
    PlayerPresenter another(frozenBundle(), another_platform);
    QVERIFY(another.trayNoticeEnabled());
    QVERIFY(another.requestClose());
    QCOMPARE(another.windowNotice(), PlayerPresenter::WindowNotice::TrayCloseNotice);
    QVERIFY(another.setTrayNoticeEnabled(false));
    QVERIFY(another.confirmWindowNotice());
    QCOMPARE(another_platform.hide_calls, 1);
    QVERIFY(another.setTrayNoticeEnabled(true));
    QVERIFY(another.requestRestore());
    QVERIFY(another.requestClose());
    QCOMPARE(another.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
    QCOMPARE(another_platform.hide_calls, 2);
}

void PlayerPresenterTests::closeRefreshesAvailabilityBeforeDecidingTheWindowAction() {
    AvailableTrayPlatform platform;
    platform.available = false;
    PlayerPresenter presenter(frozenBundle(), platform);
    QSignalSpy availability(&presenter, &PlayerPresenter::platformChanged);
    platform.available = true;
    QVERIFY(presenter.requestClose());
    QVERIFY(presenter.trayAvailable());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayCloseNotice);
    QCOMPARE(availability.size(), 1);
    QVERIFY(presenter.cancelWindowNotice());
    platform.available = false;
    QVERIFY(presenter.requestClose());
    QVERIFY(!presenter.trayAvailable());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayUnavailableNotice);
    QCOMPARE(availability.size(), 2);
    QCOMPARE(platform.minimize_calls, 1);
    QCOMPARE(platform.hide_calls, 0);
    QCOMPARE(platform.exit_calls, 0);
}

void PlayerPresenterTests::confirmationRechecksAvailabilityWithoutPublishingATransientNotice() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QSignalSpy lifecycle(&presenter, &PlayerPresenter::lifecycleChanged);
    QList<PlayerPresenter::WindowNotice> notices;
    connect(&presenter, &PlayerPresenter::lifecycleChanged, &presenter,
            [&] { notices.append(presenter.windowNotice()); });
    QVERIFY(presenter.requestClose());
    platform.available = false;
    QVERIFY(!presenter.requestClose());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayCloseNotice);
    QCOMPARE(platform.minimize_calls, 0);
    QCOMPARE(lifecycle.size(), 1);
    QVERIFY(presenter.confirmWindowNotice());
    QVERIFY(!presenter.trayAvailable());
    QCOMPARE(platform.minimize_calls, 1);
    QCOMPARE(platform.hide_calls, 0);
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayUnavailableNotice);
    QCOMPARE(notices, QList<PlayerPresenter::WindowNotice>({PlayerPresenter::WindowNotice::TrayCloseNotice,
                                                            PlayerPresenter::WindowNotice::TrayUnavailableNotice}));
    QVERIFY(presenter.confirmWindowNotice());
    platform.available = true;
    QVERIFY(presenter.requestClose());
    QCOMPARE(platform.hide_calls, 1);
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
    QCOMPARE(platform.exit_calls, 0);
}

void PlayerPresenterTests::explicitHideIgnoresClosePreferenceAndBypassesTheNotice_data() {
    QTest::addColumn<bool>("tray_available");
    QTest::addColumn<bool>("close_exits");
    QTest::newRow("tray-minimize") << true << false;
    QTest::newRow("tray-exit") << true << true;
    QTest::newRow("no-tray-minimize") << false << false;
    QTest::newRow("no-tray-exit") << false << true;
}

void PlayerPresenterTests::explicitHideIgnoresClosePreferenceAndBypassesTheNotice() {
    QFETCH(bool, tray_available);
    QFETCH(bool, close_exits);
    AvailableTrayPlatform platform;
    platform.available = tray_available;
    PlayerPresenter presenter(frozenBundle(), platform);
    if (close_exits)
        QVERIFY(presenter.setClosePreference(PlayerPresenter::ClosePreference::Exit));
    QSignalSpy lifecycle(&presenter, &PlayerPresenter::lifecycleChanged);
    QVERIFY(presenter.requestHide());
    QCOMPARE(platform.hide_calls, tray_available ? 1 : 0);
    QCOMPARE(platform.minimize_calls, tray_available ? 0 : 1);
    QCOMPARE(platform.exit_calls, 0);
    QCOMPARE(presenter.closePreference(),
             close_exits ? PlayerPresenter::ClosePreference::Exit : PlayerPresenter::ClosePreference::MinimizeToTray);
    QCOMPARE(presenter.windowNotice(), tray_available ? PlayerPresenter::WindowNotice::NoNotice
                                                      : PlayerPresenter::WindowNotice::TrayUnavailableNotice);
    QCOMPARE(lifecycle.size(), tray_available ? 0 : 1);
    QVERIFY(!presenter.exiting());
}

void PlayerPresenterTests::explicitHideClearsAPendingFirstNoticeWithoutAcknowledgingIt() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QSignalSpy lifecycle(&presenter, &PlayerPresenter::lifecycleChanged);
    QVERIFY(presenter.requestClose());
    QVERIFY(presenter.setClosePreference(PlayerPresenter::ClosePreference::Exit));
    QVERIFY(presenter.requestHide());
    QCOMPARE(platform.hide_calls, 1);
    QCOMPARE(platform.exit_calls, 0);
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
    QCOMPARE(lifecycle.size(), 2);
    QVERIFY(presenter.requestRestore());
    QVERIFY(presenter.setClosePreference(PlayerPresenter::ClosePreference::MinimizeToTray));
    QVERIFY(presenter.requestClose());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayCloseNotice);
    QCOMPARE(lifecycle.size(), 3);
}

void PlayerPresenterTests::closeExitPreferenceOverridesNoticesAndTrayAvailability_data() {
    QTest::addColumn<bool>("tray_available");
    QTest::newRow("tray") << true;
    QTest::newRow("no-tray") << false;
}

void PlayerPresenterTests::closeExitPreferenceOverridesNoticesAndTrayAvailability() {
    QFETCH(bool, tray_available);
    AvailableTrayPlatform platform;
    platform.available = tray_available;
    PlayerPresenter presenter(frozenBundle(), platform);
    if (tray_available)
        QVERIFY(presenter.requestClose());
    else
        presenter.reportTrayUnavailable();
    QVERIFY(presenter.setClosePreference(PlayerPresenter::ClosePreference::Exit));
    QVERIFY(presenter.requestClose());
    QVERIFY(presenter.exiting());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
    QCOMPARE(presenter.closePreference(), PlayerPresenter::ClosePreference::Exit);
    QCOMPARE(platform.exit_calls, 1);
    QCOMPARE(platform.hide_calls, 0);
    QCOMPARE(platform.minimize_calls, 0);
    QVERIFY(!presenter.requestClose());
    QCOMPARE(platform.exit_calls, 1);
}

void PlayerPresenterTests::reportingTrayLossOnlyProjectsAnIdempotentExplanation() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QVERIFY(presenter.loadFrozenQueue());
    QTimer* const timer = presenter.findChild<QTimer*>(QString{}, Qt::FindDirectChildrenOnly);
    QVERIFY(timer != nullptr);
    QSignalSpy lifecycle(&presenter, &PlayerPresenter::lifecycleChanged);
    QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);
    QSignalSpy preferences(&presenter, &PlayerPresenter::preferencesChanged);
    presenter.reportTrayUnavailable();
    presenter.reportTrayUnavailable();
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayUnavailableNotice);
    QCOMPARE(lifecycle.size(), 1);
    QVERIFY(presenter.requestRestore());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayUnavailableNotice);
    QCOMPARE(lifecycle.size(), 1);
    QVERIFY(presenter.cancelWindowNotice());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
    QCOMPARE(lifecycle.size(), 2);
    QCOMPARE(platform.hide_calls, 0);
    QCOMPARE(platform.minimize_calls, 0);
    QCOMPARE(platform.exit_calls, 0);
    QCOMPARE(platform.restore_calls, 1);
    QCOMPARE(playback.size(), 0);
    QCOMPARE(preferences.size(), 0);
    QCOMPARE(presenter.positionMs(), 84'000);
    QVERIFY(timer->isActive());
}

void PlayerPresenterTests::lifecyclePreservesPlaybackQueueModelAndPendingBackendWork_data() {
    QTest::addColumn<bool>("tray_available");
    QTest::addColumn<QString>("playback_state");
    QTest::newRow("tray-ready") << true << QStringLiteral("ready");
    QTest::newRow("tray-paused") << true << QStringLiteral("paused");
    QTest::newRow("tray-playing") << true << QStringLiteral("playing");
    QTest::newRow("no-tray-ready") << false << QStringLiteral("ready");
    QTest::newRow("no-tray-paused") << false << QStringLiteral("paused");
    QTest::newRow("no-tray-playing") << false << QStringLiteral("playing");
}

void PlayerPresenterTests::lifecyclePreservesPlaybackQueueModelAndPendingBackendWork() {
    QFETCH(bool, tray_available);
    QFETCH(QString, playback_state);
    AvailableTrayPlatform platform;
    platform.available = tray_available;
    FixtureBundle bundle = frozenBundle();
    bundle.states[1].playback_state = playback_state;
    PlayerPresenter presenter(std::move(bundle), platform);
    QVERIFY(presenter.loadFrozenQueue());
    QVERIFY(presenter.setVolume(0.35));
    QVERIFY(presenter.toggleMute());
    QVERIFY(presenter.setSkipBackwardSeconds(7));
    QVERIFY(presenter.setSkipForwardSeconds(12));
    QVERIFY(presenter.setThemePreference(PlayerPresenter::ThemePreference::DarkTheme));
    QVERIFY(presenter.setMotionPreference(PlayerPresenter::MotionPreference::ReducedMotion));
    QVERIFY(presenter.setRestoreLastSession(false));
    QVERIFY(presenter.setQueueEndPreference(PlayerPresenter::QueueEndPreference::StopClearSelection));
    QVERIFY(presenter.beginSeekPreview(124'000));
    QVERIFY(presenter.commitSeekPreview());
    PlayerPresenterTestAccess::rejectNextSeek(presenter);
    QueueListModel* const model = presenter.queueModel();
    const QPersistentModelIndex current = model->index(16, 0);
    const auto occurrence_ids = [&] {
        QStringList ids;
        for (int row = 0; row < model->rowCount(); ++row)
            ids.append(model->data(model->index(row, 0), QueueListModel::OccurrenceIdRole).toString());
        return ids;
    };
    const QStringList ids = occurrence_ids();
    QCOMPARE(ids.size(), 1000);
    const auto presentation = [&] {
        QVariantMap result;
        for (const char* name : {"stateId",
                                 "playbackState",
                                 "queueLoaded",
                                 "currentOccurrenceId",
                                 "currentQueueIndex",
                                 "contextOccurrenceId",
                                 "positionMs",
                                 "confirmedPositionMs",
                                 "seekPreviewActive",
                                 "seekPending",
                                 "durationMs",
                                 "seekable",
                                 "currentTitle",
                                 "currentArtist",
                                 "currentAlbum",
                                 "volume",
                                 "muted",
                                 "skipBackwardSeconds",
                                 "skipForwardSeconds",
                                 "themePreference",
                                 "motionPreference",
                                 "closePreference",
                                 "queueEndPreference",
                                 "restoreLastSession",
                                 "startMinimized"})
            result.insert(QString::fromLatin1(name), presenter.property(name));
        return result;
    };
    const QVariantMap before = presentation();
    QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);
    QSignalSpy states(&presenter, &PlayerPresenter::stateChanged);
    QSignalSpy preferences(&presenter, &PlayerPresenter::preferencesChanged);
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    QSignalSpy changes(model, &QAbstractItemModel::dataChanged);
    const auto verify_preserved = [&] {
        QCOMPARE(presentation(), before);
        QCOMPARE(presenter.queueModel(), model);
        QCOMPARE(occurrence_ids(), ids);
        QVERIFY(current.isValid());
        QCOMPARE(current.data(QueueListModel::OccurrenceIdRole).toString(), QStringLiteral("queue-0017"));
        QCOMPARE(playback.size(), 0);
        QCOMPARE(states.size(), 0);
        QCOMPARE(preferences.size(), 0);
        QCOMPARE(resets.size(), 0);
        QCOMPARE(changes.size(), 0);
    };
    QVERIFY(presenter.requestClose());
    verify_preserved();
    QVERIFY(presenter.cancelWindowNotice());
    verify_preserved();
    QVERIFY(presenter.requestClose());
    QVERIFY(presenter.confirmWindowNotice());
    verify_preserved();
    QVERIFY(presenter.requestRestore());
    verify_preserved();
    QVERIFY(presenter.requestHide());
    verify_preserved();
    presenter.reportTrayUnavailable();
    QVERIFY(presenter.confirmWindowNotice());
    verify_preserved();
    QVERIFY(presenter.setTrayNoticeEnabled(false));
    QVERIFY(presenter.requestClose());
    verify_preserved();
    QVERIFY(presenter.requestRestore());
    verify_preserved();
    QCOMPARE(platform.exit_calls, 0);

    QSignalSpy rejected(&presenter, &PlayerPresenter::seekRejected);
    QVERIFY(presenter.advanceOneTick());
    QCOMPARE(rejected.size(), 1);
    QCOMPARE(presenter.positionMs(), 84'000);
    QCOMPARE(presenter.confirmedPositionMs(), 84'000);
    QVERIFY(!presenter.seekPending());
    QCOMPARE(presenter.volume(), 0.35);
    QVERIFY(presenter.muted());
}

void PlayerPresenterTests::exitPublishesItsGuardAndStopsTicksBeforeRequestingThePlatform_data() {
    explicitHideIgnoresClosePreferenceAndBypassesTheNotice_data();
}

void PlayerPresenterTests::exitPublishesItsGuardAndStopsTicksBeforeRequestingThePlatform() {
    QFETCH(bool, tray_available);
    QFETCH(bool, close_exits);
    AvailableTrayPlatform platform;
    platform.available = tray_available;
    PlayerPresenter presenter(frozenBundle(), platform);
    if (close_exits)
        QVERIFY(presenter.setClosePreference(PlayerPresenter::ClosePreference::Exit));
    QVERIFY(presenter.loadFrozenQueue());
    QVERIFY(presenter.beginSeekPreview(124'000));
    QVERIFY(presenter.commitSeekPreview());
    QTimer* const timer = presenter.findChild<QTimer*>(QString{}, Qt::FindDirectChildrenOnly);
    QVERIFY(timer != nullptr);
    QVERIFY(timer->isActive());
    presenter.reportTrayUnavailable();
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayUnavailableNotice);
    QSignalSpy lifecycle(&presenter, &PlayerPresenter::lifecycleChanged);
    QStringList order;
    connect(&presenter, &PlayerPresenter::lifecycleChanged, &presenter, [&] {
        order.append(QStringLiteral("lifecycle"));
        QVERIFY(presenter.exiting());
        QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
        QVERIFY(!timer->isActive());
        QCOMPARE(platform.exit_calls, 0);
        QVERIFY(!presenter.requestExit());
    });
    platform.on_exit = [&] {
        order.append(QStringLiteral("platform-exit"));
        QVERIFY(presenter.exiting());
        QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
        QVERIFY(!timer->isActive());
        QVERIFY(!presenter.requestClose());
    };

    QVERIFY(presenter.requestExit());

    QCOMPARE(order, QStringList({QStringLiteral("lifecycle"), QStringLiteral("platform-exit")}));
    QCOMPARE(lifecycle.size(), 1);
    QCOMPARE(platform.exit_calls, 1);
    QVERIFY(!presenter.requestExit());
    QVERIFY(!presenter.requestClose());
    QVERIFY(!presenter.requestHide());
    QVERIFY(!presenter.requestRestore());
    QVERIFY(!presenter.confirmWindowNotice());
    QVERIFY(!presenter.cancelWindowNotice());
    QVERIFY(!presenter.setTrayNoticeEnabled(false));
    presenter.reportTrayUnavailable();
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
    QVERIFY(presenter.trayNoticeEnabled());
    QCOMPARE(lifecycle.size(), 1);
    QCOMPARE(platform.exit_calls, 1);
    QCOMPARE(platform.hide_calls, 0);
    QCOMPARE(platform.minimize_calls, 0);
    QCOMPARE(platform.restore_calls, 0);
    QTest::qWait(250);
    QCOMPARE(presenter.positionMs(), 124'000);
    QCOMPARE(presenter.confirmedPositionMs(), 84'000);
    QVERIFY(presenter.seekPending());
    QVERIFY(presenter.seekPreviewActive());
    // Even another existing transport operation must not restart the lifetime-owned timer.
    QVERIFY(presenter.togglePlayback());
    QVERIFY(presenter.togglePlayback());
    QVERIFY(!timer->isActive());
}

} // namespace music_player::m0::test

QTEST_MAIN(music_player::m0::test::PlayerPresenterTests)

#include "PlayerPresenterTests.moc"
