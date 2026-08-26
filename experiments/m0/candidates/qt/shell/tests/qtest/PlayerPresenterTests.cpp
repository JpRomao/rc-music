#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "m0/presentation/PlayerPresenter.hpp"

#include <QAbstractItemModelTester>
#include <QMetaProperty>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

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
    QCOMPARE(reset_was_atomic, true);

    QVERIFY(!presenter.loadFrozenQueue());
    QCOMPARE(presenter.queueModel(), identity);
    QCOMPARE(identity->rowCount(), 1000);
    QCOMPARE(state_changed.size(), 1);
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
    QCOMPARE(presenter.findChildren<QTimer*>().size(), 0);
}

} // namespace music_player::m0::test

QTEST_GUILESS_MAIN(music_player::m0::test::PlayerPresenterTests)

#include "PlayerPresenterTests.moc"
