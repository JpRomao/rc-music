#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "presentation/PlayerPresenterScenarioControl.hpp"
#include "presentation/PlayerPresenterTestAccess.hpp"

#include <QAbstractItemModelTester>
#include <QMetaProperty>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QTimer>

#include <limits>
#include <stdexcept>

namespace music_player::m0::test {
namespace {

class AvailableTrayPlatform final : public PlatformIntegration {
  public:
    [[nodiscard]] bool trayAvailable() const noexcept override { return true; }
    void hideWindow() override {}
    void minimizeWindow() override {}
    void showAndActivateWindow() override {}
    void requestExit() override {}
};

[[nodiscard]] FixtureBundle frozenBundle() {
    return FixtureRepository{}.load(QString::fromUtf8(M0_TEST_FIXTURE_FILE), QString::fromUtf8(M0_TEST_QUEUE_FILE));
}

} // namespace

class PlayerPresenterTests final : public QObject {
    Q_OBJECT

  private slots:
    void startsFromNullableEmptyStateAndAppliesEveryDefault();
    void appliesDefaultsThatDifferFromTheCoreDefaults();
    void loadsFrozenQueueOnceWithoutReplacingTheModel();
    void derivesBackendSnapshotFromTheTypedPlayingState();
    void rejectsUnknownMappingsAndExposesNoInfrastructureApi();
    void drivesTransportPreferencesAndManualTicksDeterministically();
    void keepsUnavailableAndTerminalTransportInert();
    void schedulesTicksOnlyWhilePlaybackOrSeekNeedsThem();
    void projectsTypedMetadataAndRecoversFrozenUnavailableScenario();
};

void PlayerPresenterTests::startsFromNullableEmptyStateAndAppliesEveryDefault() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);

    QCOMPARE(presenter.stateId(), QStringLiteral("empty"));
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Unloaded);
    QCOMPARE(presenter.queueLoaded(), false);
    QCOMPARE(presenter.hasCurrentOccurrence(), false);
    QCOMPARE(presenter.currentOccurrenceId(), QString{});
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
    QVERIFY(meta->indexOfMethod("loadFrozenQueue()") >= 0);
    for (const char* method : {"togglePlayback()", "skipBackward()", "skipForward()", "beginSeekPreview(qlonglong)",
                               "updateSeekPreview(qlonglong)", "commitSeekPreview()", "setVolume(double)",
                               "toggleMute()", "skipUnavailable()"})
        QVERIFY(meta->indexOfMethod(method) >= 0);
    const int queue_model_property = meta->indexOfProperty("queueModel");
    QVERIFY(queue_model_property >= 0);
    QCOMPARE(meta->property(queue_model_property).metaType().id(), qMetaTypeId<QAbstractItemModel*>());
    for (const char* property_name : {"volume", "muted"}) {
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
    QCOMPARE(presenter.hasContextTrack(), false);
    QCOMPARE(state_changed.size(), 2);
    QCOMPARE(playback_changed.size(), 2);
    QVERIFY(timer->isActive());
    QVERIFY(!presenter.skipUnavailable());
    QCOMPARE(state_changed.size(), 2);
    QCOMPARE(playback_changed.size(), 2);
}

} // namespace music_player::m0::test

QTEST_GUILESS_MAIN(music_player::m0::test::PlayerPresenterTests)

#include "PlayerPresenterTests.moc"
