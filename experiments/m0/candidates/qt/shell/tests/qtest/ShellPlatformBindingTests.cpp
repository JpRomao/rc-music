#include "adapters/QtShellPlatform.hpp"
#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "presentation/ShellPlatformBinding.hpp"

#include <QApplication>
#include <QPersistentModelIndex>
#include <QPixmap>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QVariantMap>
#include <QWindow>

#include <memory>

namespace music_player::m0::test {
namespace {

[[nodiscard]] QIcon testIcon() {
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::blue);
    return QIcon{pixmap};
}

[[nodiscard]] TrayMenuLabels labels() {
    return {QStringLiteral("Reproduzir"), QStringLiteral("Pausar"),       QStringLiteral("Anterior"),
            QStringLiteral("Próxima"),    QStringLiteral("Mostrar"),      QStringLiteral("Ocultar"),
            QStringLiteral("Sair"),       QStringLiteral("Nenhuma faixa")};
}

[[nodiscard]] FixtureBundle frozenBundle() {
    return FixtureRepository{}.load(QString::fromUtf8(M0_TEST_FIXTURE_FILE), QString::fromUtf8(M0_TEST_QUEUE_FILE));
}

[[nodiscard]] QVariantMap playbackAndPreferences(const PlayerPresenter& presenter) {
    QVariantMap result;
    for (const char* name :
         {"stateId",         "playbackState",       "currentOccurrenceId", "currentQueueIndex", "contextOccurrenceId",
          "positionMs",      "confirmedPositionMs", "seekPreviewActive",   "seekPending",       "durationMs",
          "seekable",        "currentTitle",        "currentArtist",       "currentAlbum",      "volume",
          "muted",           "skipBackwardSeconds", "skipForwardSeconds",  "themePreference",   "motionPreference",
          "closePreference", "queueEndPreference",  "restoreLastSession",  "startMinimized"}) {
        result.insert(QString::fromLatin1(name), presenter.property(name));
    }
    return result;
}

[[nodiscard]] QStringList occurrenceIds(const QueueListModel& model) {
    QStringList ids;
    for (int row = 0; row < model.rowCount(); ++row)
        ids.append(model.data(model.index(row, 0), QueueListModel::OccurrenceIdRole).toString());
    return ids;
}

} // namespace

class ShellPlatformBindingTests final : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase() {
        QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
        QApplication::setQuitOnLastWindowClosed(false);
    }

    void aLateHideFallbackAlwaysPublishesTheExplanation_data() {
        QTest::addColumn<bool>("cached_available");
        QTest::addColumn<bool>("confirm_first_notice");
        QTest::newRow("cached-available") << true << false;
        QTest::newRow("cached-unavailable") << false << false;
        QTest::newRow("confirm-first-notice") << true << true;
    }

    void aLateHideFallbackAlwaysPublishesTheExplanation() {
        QFETCH(bool, cached_available);
        QFETCH(bool, confirm_first_notice);
        bool armed = false;
        int action_reads = 0;
        int quit_requests = 0;
        QtShellPlatform platform(
            testIcon(), QStringLiteral("Player M0"), labels(),
            [&] { return armed ? ++action_reads <= 2 : cached_available; }, [&] { ++quit_requests; });
        PlayerPresenter presenter(frozenBundle(), platform);
        QWindow window;
        window.resize(960, 640);
        window.showMaximized();
        bindShellPlatform(presenter, platform, window);
        QVERIFY(presenter.loadFrozenQueue());
        QVERIFY(presenter.setVolume(0.35));
        QVERIFY(presenter.toggleMute());
        QVERIFY(presenter.setSkipBackwardSeconds(7));
        QVERIFY(presenter.setSkipForwardSeconds(20));
        QVERIFY(presenter.setThemePreference(PlayerPresenter::ThemePreference::DarkTheme));
        QVERIFY(presenter.setMotionPreference(PlayerPresenter::MotionPreference::ReducedMotion));
        QVERIFY(presenter.setRestoreLastSession(false));
        QVERIFY(presenter.setQueueEndPreference(PlayerPresenter::QueueEndPreference::StopClearSelection));
        if (confirm_first_notice) {
            QVERIFY(presenter.requestClose());
            QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayCloseNotice);
        } else {
            QVERIFY(presenter.setClosePreference(PlayerPresenter::ClosePreference::Exit));
        }
        QVERIFY(presenter.beginSeekPreview(124'000));
        QVERIFY(presenter.commitSeekPreview());
        QTimer* const timer = presenter.findChild<QTimer*>(QString{}, Qt::FindDirectChildrenOnly);
        QVERIFY(timer != nullptr && timer->isActive());
        QueueListModel* const model = presenter.queueModel();
        const QPersistentModelIndex current(model->index(16, 0));
        const QStringList ids = occurrenceIds(*model);
        QCOMPARE(ids.size(), 1000);
        const QVariantMap before = playbackAndPreferences(presenter);
        for (const auto& value : before)
            QVERIFY(value.isValid());
        QSignalSpy resets(model, &QAbstractItemModel::modelReset);
        QSignalSpy changes(model, &QAbstractItemModel::dataChanged);
        QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);
        QSignalSpy preferences(&presenter, &PlayerPresenter::preferencesChanged);
        QSignalSpy availability(&platform, &QtShellPlatform::availabilityChanged);
        QSignalSpy fallbacks(&platform, &QtShellPlatform::hideFellBackToMinimize);
        QList<PlayerPresenter::WindowNotice> notices;
        connect(&presenter, &PlayerPresenter::lifecycleChanged, &presenter,
                [&] { notices.append(presenter.windowNotice()); });
        connect(&platform, &QtShellPlatform::hideFellBackToMinimize, &presenter, [&] {
            QVERIFY(window.isVisible());
            QVERIFY(window.windowStates().testFlag(Qt::WindowMinimized));
            QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayUnavailableNotice);
            QVERIFY(!presenter.trayAvailable());
        });

        // Only this action sees the race: presenter/core read true, then the adapter reads false.
        // All following reads remain false, including the binding's availability reconciliation.
        armed = true;
        if (confirm_first_notice)
            QVERIFY(presenter.confirmWindowNotice());
        else
            QVERIFY(presenter.requestHide());

        QVERIFY(action_reads >= 3);
        QVERIFY(window.isVisible());
        QVERIFY(window.windowStates().testFlag(Qt::WindowMinimized));
        QCOMPARE(playbackAndPreferences(presenter), before);
        QCOMPARE(presenter.queueModel(), model);
        QCOMPARE(occurrenceIds(*model), ids);
        QVERIFY(current.isValid());
        QCOMPARE(current.data(QueueListModel::OccurrenceIdRole).toString(), QStringLiteral("queue-0017"));
        QCOMPARE(resets.size(), 0);
        QCOMPARE(changes.size(), 0);
        QCOMPARE(playback.size(), 0);
        QCOMPARE(preferences.size(), 0);
        QCOMPARE(quit_requests, 0);
        QVERIFY(timer->isActive());
        QCOMPARE(availability.size(), cached_available ? 1 : 0);
        QCOMPARE(fallbacks.size(), 1);
        QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayUnavailableNotice);
        QVERIFY(!presenter.trayAvailable());
        QCOMPARE(notices, QList<PlayerPresenter::WindowNotice>{PlayerPresenter::WindowNotice::TrayUnavailableNotice});

        QVERIFY(presenter.requestRestore());
        QVERIFY(window.isVisible());
        QVERIFY(window.windowStates().testFlag(Qt::WindowMaximized));
        QVERIFY(!window.windowStates().testFlag(Qt::WindowMinimized));
        QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayUnavailableNotice);
        QCOMPARE(playbackAndPreferences(presenter), before);
        QVERIFY(presenter.confirmWindowNotice());
        QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
        QVERIFY(presenter.advanceOneTick());
        QVERIFY(!presenter.seekPending());
        QVERIFY(!presenter.seekPreviewActive());
        QCOMPARE(presenter.confirmedPositionMs(), 124'000);
        QCOMPARE(presenter.queueModel(), model);
        QCOMPARE(quit_requests, 0);
    }

    void exitingDuringCapabilityRefreshPreventsTheFallback() {
        bool available = true;
        int quit_requests = 0;
        QtShellPlatform platform(
            testIcon(), QStringLiteral("Player M0"), labels(), [&] { return available; }, [&] { ++quit_requests; });
        PlayerPresenter presenter(frozenBundle(), platform);
        QWindow window;
        window.show();
        bindShellPlatform(presenter, platform, window);
        QVERIFY(presenter.loadFrozenQueue());
        const QVariantMap before = playbackAndPreferences(presenter);
        QTimer* const timer = presenter.findChild<QTimer*>(QString{}, Qt::FindDirectChildrenOnly);
        QVERIFY(timer != nullptr && timer->isActive());
        QSignalSpy fallbacks(&platform, &QtShellPlatform::hideFellBackToMinimize);
        connect(&platform, &QtShellPlatform::availabilityChanged, &presenter,
                [&] { QVERIFY(presenter.requestExit()); });

        available = false;
        platform.hideWindow();

        QCOMPARE(quit_requests, 1);
        QVERIFY(presenter.exiting());
        QVERIFY(!timer->isActive());
        QVERIFY(window.isVisible());
        QVERIFY(!window.windowStates().testFlag(Qt::WindowMinimized));
        QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
        QCOMPARE(playbackAndPreferences(presenter), before);
        QCOMPARE(fallbacks.size(), 0);
        platform.hideWindow();
        platform.refreshAvailability();
        QVERIFY(!presenter.requestHide());
        QVERIFY(!presenter.requestExit());
        QCOMPARE(quit_requests, 1);
        QCOMPARE(fallbacks.size(), 0);
    }

    void destroyingThePresenterDisconnectsFallbackNotifications() {
        bool available = true;
        QtShellPlatform platform(testIcon(), QStringLiteral("Player M0"), labels(), [&] { return available; }, [] {});
        QWindow window;
        window.show();
        auto presenter = std::make_unique<PlayerPresenter>(frozenBundle(), platform);
        QPointer<PlayerPresenter> guarded_presenter(presenter.get());
        bindShellPlatform(*presenter, platform, window);
        QVERIFY(presenter->loadFrozenQueue());
        presenter.reset();
        QVERIFY(guarded_presenter.isNull());
        QSignalSpy fallbacks(&platform, &QtShellPlatform::hideFellBackToMinimize);

        available = false;
        platform.hideWindow();

        QCOMPARE(fallbacks.size(), 1);
        QVERIFY(window.isVisible());
        QVERIFY(window.windowStates().testFlag(Qt::WindowMinimized));
    }

    void destroyingTheWindowDisarmsTheFallback() {
        bool available = true;
        QtShellPlatform platform(testIcon(), QStringLiteral("Player M0"), labels(), [&] { return available; }, [] {});
        PlayerPresenter presenter(frozenBundle(), platform);
        auto window = std::make_unique<QWindow>();
        window->show();
        bindShellPlatform(presenter, platform, *window);
        window.reset();
        QSignalSpy fallbacks(&platform, &QtShellPlatform::hideFellBackToMinimize);
        QSignalSpy lifecycle(&presenter, &PlayerPresenter::lifecycleChanged);

        available = false;
        platform.refreshAvailability();
        platform.hideWindow();

        QCOMPARE(fallbacks.size(), 0);
        QCOMPARE(lifecycle.size(), 0);
        QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
        QVERIFY(!presenter.trayAvailable());
    }
};

} // namespace music_player::m0::test

QTEST_MAIN(music_player::m0::test::ShellPlatformBindingTests)
#include "ShellPlatformBindingTests.moc"
