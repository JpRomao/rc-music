#include "adapters/QtShellPlatform.hpp"
#include "m0/domain/WindowPlacement.hpp"

#include <QAction>
#include <QApplication>
#include <QFontMetricsF>
#include <QMenu>
#include <QPixmap>
#include <QPointer>
#include <QSignalSpy>
#include <QSystemTrayIcon>
#include <QTest>
#include <QTimer>
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

[[nodiscard]] QAction* action(const QtShellPlatform& platform, const char* name) {
    return platform.menu()->findChild<QAction*>(QString::fromLatin1(name));
}

} // namespace

class QtShellPlatformTests final : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase() {
        QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
        QApplication::setQuitOnLastWindowClosed(false);
    }

    void createsOwnedTrayAndMenuWithStableActions() {
        QtShellPlatform platform(testIcon(), QStringLiteral("Player M0"), labels(), [] { return true; }, [] {});

        QVERIFY(platform.menu() != nullptr);
        QVERIFY(platform.trayIcon() != nullptr);
        QCOMPARE(platform.trayIcon()->contextMenu(), platform.menu());
        QCOMPARE(platform.trayIcon()->parent(), &platform);
        QVERIFY(platform.trayIcon()->isVisible());
        QVERIFY(platform.trayAvailable());
        QVERIFY(!platform.trayIcon()->icon().isNull());
        QCOMPARE(platform.trayIcon()->toolTip(), QStringLiteral("Player M0"));
        const QStringList expected{QStringLiteral("tray-current-track"), QStringLiteral("tray-play-pause"),
                                   QStringLiteral("tray-previous"),      QStringLiteral("tray-next"),
                                   QStringLiteral("tray-show-hide"),     QStringLiteral("tray-exit")};
        QStringList actual;
        for (const auto* menu_action : platform.menu()->actions()) {
            if (!menu_action->isSeparator())
                actual.append(menu_action->objectName());
        }
        QCOMPARE(actual, expected);
        QCOMPARE(action(platform, "tray-current-track")->text(), labels().no_track);
        QCOMPARE(action(platform, "tray-play-pause")->text(), labels().play);
        QVERIFY(!action(platform, "tray-play-pause")->isEnabled());
        QVERIFY(!action(platform, "tray-previous")->isEnabled());
        QVERIFY(!action(platform, "tray-next")->isEnabled());
    }

    void destructionReleasesTrayMenuAndActions() {
        QPointer<QMenu> menu;
        QPointer<QSystemTrayIcon> tray;
        QPointer<QAction> menu_action;
        {
            QtShellPlatform platform(testIcon(), QStringLiteral("Player"), labels(), [] { return false; }, [] {});
            menu = platform.menu();
            tray = platform.trayIcon();
            menu_action = action(platform, "tray-exit");
            QVERIFY(menu && tray && menu_action);
        }
        QVERIFY(menu.isNull());
        QVERIFY(tray.isNull());
        QVERIFY(menu_action.isNull());
    }

    void menuTracksTransportStateAndOnlyEmitsIntents() {
        bool available{true};
        int quits{0};
        QtShellPlatform platform(
            testIcon(), QStringLiteral("Player"), labels(), [&] { return available; }, [&] { ++quits; });
        QWindow window;
        window.show();
        platform.bindWindow(window);
        auto* const play = action(platform, "tray-play-pause");
        auto* const previous = action(platform, "tray-previous");
        auto* const next = action(platform, "tray-next");
        auto* const exit = action(platform, "tray-exit");
        QVERIFY(play && previous && next && exit);
        QSignalSpy play_requested(&platform, &QtShellPlatform::playPauseRequested);
        QSignalSpy previous_requested(&platform, &QtShellPlatform::previousRequested);
        QSignalSpy next_requested(&platform, &QtShellPlatform::nextRequested);
        QSignalSpy exit_requested(&platform, &QtShellPlatform::exitRequested);

        platform.updateTrayState({QStringLiteral("Faixa"), true, true, true, false});
        QCOMPARE(play->text(), labels().pause);
        QVERIFY(play->isEnabled());
        QVERIFY(previous->isEnabled());
        QVERIFY(!next->isEnabled());
        play->trigger();
        previous->trigger();
        next->trigger();
        exit->trigger();
        QCOMPARE(play_requested.size(), 1);
        QCOMPARE(previous_requested.size(), 1);
        QCOMPARE(next_requested.size(), 0);
        QCOMPARE(exit_requested.size(), 1);
        QCOMPARE(quits, 0);
        QVERIFY(window.isVisible());

        platform.updateTrayState({QStringLiteral("Outra"), true, false, false, true});
        QCOMPARE(play->text(), labels().play);
        QVERIFY(!previous->isEnabled());
        QVERIFY(next->isEnabled());
        next->trigger();
        QCOMPARE(next_requested.size(), 1);
        platform.updateTrayState({QStringLiteral("Erro"), false, false, true, true});
        QVERIFY(!play->isEnabled());
        QVERIFY(!previous->isEnabled());
        QVERIFY(!next->isEnabled());
        platform.updateTrayState({QStringLiteral("Faixa"), true, true, true, true});
        available = false;
        platform.refreshAvailability();
        QVERIFY(!play->isEnabled());
        QVERIFY(!previous->isEnabled());
        QVERIFY(!next->isEnabled());
        QVERIFY(exit->isEnabled());
    }

    void pendingExitDisablesAndCancelledExitRestoresEveryMenuAction() {
        QtShellPlatform platform(testIcon(), QStringLiteral("Player"), labels(), [] { return true; }, [] {});
        QWindow window;
        window.show();
        platform.bindWindow(window);
        auto* const title = action(platform, "tray-current-track");
        auto* const play = action(platform, "tray-play-pause");
        auto* const previous = action(platform, "tray-previous");
        auto* const next = action(platform, "tray-next");
        auto* const show_hide = action(platform, "tray-show-hide");
        auto* const exit = action(platform, "tray-exit");
        QVERIFY(title && play && previous && next && show_hide && exit);

        platform.updateTrayState({QStringLiteral("Faixa"), true, false, true, true, false});
        for (const auto* menu_action : {title, play, previous, next, show_hide, exit})
            QVERIFY(!menu_action->isEnabled());

        platform.updateTrayState({QStringLiteral("Faixa"), true, false, true, true, true});
        for (const auto* menu_action : {title, play, previous, next, show_hide, exit})
            QVERIFY(menu_action->isEnabled());
    }

    void showHideAndTitleRestoreFollowActualWindowVisibility() {
        QtShellPlatform platform(testIcon(), QStringLiteral("Player"), labels(), [] { return true; }, [] {});
        QWindow window;
        window.show();
        platform.bindWindow(window);
        auto* const show_hide = action(platform, "tray-show-hide");
        auto* const title = action(platform, "tray-current-track");
        QVERIFY(show_hide && title);
        QSignalSpy hide_requested(&platform, &QtShellPlatform::hideRequested);
        QSignalSpy restore_requested(&platform, &QtShellPlatform::restoreRequested);

        QCOMPARE(show_hide->text(), labels().hide);
        show_hide->trigger();
        QCOMPARE(hide_requested.size(), 1);
        QVERIFY(window.isVisible());
        window.hide();
        QCOMPARE(show_hide->text(), labels().show);
        show_hide->trigger();
        QCOMPARE(restore_requested.size(), 1);
        QVERIFY(!window.isVisible());
        window.showMinimized();
        QCOMPARE(show_hide->text(), labels().show);
        show_hide->trigger();
        title->trigger();
        QCOMPARE(restore_requested.size(), 3);
        QCOMPARE(hide_requested.size(), 1);
    }

    void trayActivationRestoresWithoutToggling_data() {
        QTest::addColumn<int>("reason");
        QTest::addColumn<int>("expected");
        QTest::newRow("trigger") << static_cast<int>(QSystemTrayIcon::Trigger) << 1;
        QTest::newRow("double click") << static_cast<int>(QSystemTrayIcon::DoubleClick) << 1;
        QTest::newRow("context") << static_cast<int>(QSystemTrayIcon::Context) << 0;
        QTest::newRow("middle click") << static_cast<int>(QSystemTrayIcon::MiddleClick) << 0;
        QTest::newRow("unknown") << static_cast<int>(QSystemTrayIcon::Unknown) << 0;
    }

    void trayActivationRestoresWithoutToggling() {
        QFETCH(int, reason);
        QFETCH(int, expected);
        QtShellPlatform platform(testIcon(), QStringLiteral("Player"), labels(), [] { return true; }, [] {});
        QWindow window;
        window.show();
        platform.bindWindow(window);
        QSignalSpy restore_requested(&platform, &QtShellPlatform::restoreRequested);
        QSignalSpy hide_requested(&platform, &QtShellPlatform::hideRequested);

        platform.trayIcon()->activated(static_cast<QSystemTrayIcon::ActivationReason>(reason));

        QCOMPARE(restore_requested.size(), expected);
        QCOMPARE(hide_requested.size(), 0);
        QVERIFY(window.isVisible());
    }

    void metadataStaysPlainSingleLineLiteralAndElidedWithoutMutation() {
        QtShellPlatform platform(testIcon(), QStringLiteral("Player"), labels(), [] { return true; }, [] {});
        auto* const title_action = action(platform, "tray-current-track");
        QVERIFY(title_action);
        const TrayMenuState short_state{QStringLiteral("A&B\r\n<track>\tName\u2028End\u2029Done"), true, true};
        const QString original = short_state.title;
        platform.updateTrayState(short_state);

        QCOMPARE(short_state.title, original);
        QCOMPARE(title_action->text(), QStringLiteral("A&&B <track> Name End Done"));
        QVERIFY(!title_action->text().contains(u'\n'));
        QVERIFY(!title_action->text().contains(u'\r'));
        QVERIFY(!title_action->text().contains(u'\t'));
        QCOMPARE(title_action->shortcut(), QKeySequence{});
        const TrayMenuState long_state{QStringLiteral("A&B ").repeated(200), true, true};
        platform.updateTrayState(long_state);
        QString displayed = title_action->text();
        displayed.replace(QStringLiteral("&&"), QStringLiteral("&"));
        QVERIFY(displayed.size() < long_state.title.size());
        QVERIFY2(QFontMetricsF(title_action->font()).horizontalAdvance(displayed) <= 280.5, qPrintable(displayed));
        QCOMPARE(long_state.title, QStringLiteral("A&B ").repeated(200));
        platform.updateTrayState({});
        QCOMPARE(title_action->text(), labels().no_track);
    }

    void hideAndRestorePreserveWindowState_data() {
        QTest::addColumn<int>("state");
        QTest::newRow("normal") << static_cast<int>(Qt::WindowNoState);
        QTest::newRow("maximized") << static_cast<int>(Qt::WindowMaximized);
        QTest::newRow("fullscreen") << static_cast<int>(Qt::WindowFullScreen);
    }

    void hideAndRestorePreserveWindowState() {
        QFETCH(int, state);
        QtShellPlatform platform(testIcon(), QStringLiteral("Player"), labels(), [] { return true; }, [] {});
        QWindow window;
        window.setGeometry(80, 90, 720, 520);
        window.setWindowStates(Qt::WindowStates::fromInt(state));
        window.setVisible(true);
        platform.bindWindow(window);
        const QRect original_geometry = window.geometry();

        platform.hideWindow();
        QVERIFY(!window.isVisible());
        platform.showAndActivateWindow();
        QVERIFY(window.isVisible());
        QCOMPARE(window.windowStates(), Qt::WindowStates::fromInt(state));
        QCOMPARE(window.geometry(), original_geometry);
        QSignalSpy visible_changed(&window, &QWindow::visibleChanged);
        platform.showAndActivateWindow();
        QCOMPARE(window.windowStates(), Qt::WindowStates::fromInt(state));
        QCOMPARE(visible_changed.size(), 0);
    }

    void minimizeAndRestorePreserveTheLastNonMinimizedState() {
        QtShellPlatform platform(testIcon(), QStringLiteral("Player"), labels(), [] { return true; }, [] {});
        QWindow window;
        window.showMaximized();
        platform.bindWindow(window);

        platform.minimizeWindow();
        QVERIFY(window.isVisible());
        QVERIFY(window.windowStates().testFlag(Qt::WindowMinimized));
        platform.minimizeWindow();
        platform.showAndActivateWindow();
        QCOMPARE(window.windowStates(), Qt::WindowStates{Qt::WindowMaximized});
        window.showFullScreen();
        window.showMinimized();
        platform.hideWindow();
        platform.showAndActivateWindow();
        QCOMPARE(window.windowStates(), Qt::WindowStates{Qt::WindowFullScreen});
        window.showNormal();
        platform.hideWindow();
        platform.showAndActivateWindow();
        QCOMPARE(window.windowStates(), Qt::WindowStates{Qt::WindowNoState});
    }

    void hideFallsBackToMinimizeWhenTheReaderOrIconIsUnavailable() {
        bool available{false};
        QtShellPlatform platform(testIcon(), QStringLiteral("Player"), labels(), [&] { return available; }, [] {});
        QWindow window;
        window.showMaximized();
        platform.bindWindow(window);

        platform.hideWindow();

        QVERIFY(window.isVisible());
        QVERIFY(window.windowStates().testFlag(Qt::WindowMinimized));
        platform.showAndActivateWindow();
        QCOMPARE(window.windowStates(), Qt::WindowStates{Qt::WindowMaximized});
        available = true;
        platform.refreshAvailability();
        QVERIFY(platform.trayAvailable());
        platform.trayIcon()->hide();
        QVERIFY(!platform.trayAvailable());
        platform.hideWindow();
        QVERIFY(window.isVisible());
        QVERIFY(window.windowStates().testFlag(Qt::WindowMinimized));
    }

    void availabilityChangesNotifyOnceAfterRecoveringAHiddenWindow() {
        bool available{true};
        QtShellPlatform platform(testIcon(), QStringLiteral("Player"), labels(), [&] { return available; }, [] {});
        QWindow window;
        window.show();
        platform.bindWindow(window);
        platform.hideWindow();
        QVERIFY(!window.isVisible());
        QSignalSpy changed(&platform, &QtShellPlatform::availabilityChanged);
        bool coherent{false};
        connect(&platform, &QtShellPlatform::availabilityChanged, this, [&] {
            coherent =
                !platform.trayAvailable() && window.isVisible() && window.windowStates().testFlag(Qt::WindowMinimized);
        });
        available = false;
        platform.refreshAvailability();

        QCOMPARE(changed.size(), 1);
        QVERIFY(coherent);
        platform.refreshAvailability();
        QCOMPARE(changed.size(), 1);
        available = true;
        platform.refreshAvailability();
        QCOMPARE(changed.size(), 2);
        QVERIFY(window.isVisible());
        QVERIFY(window.windowStates().testFlag(Qt::WindowMinimized));
    }

    void pollingOnlyRunsForTheHiddenBoundWindow() {
        bool available{true};
        int reads{0};
        QtShellPlatform platform(
            testIcon(), QStringLiteral("Player"), labels(),
            [&] {
                ++reads;
                return available;
            },
            [] {});
        auto* const poll = platform.findChild<QTimer*>();
        QVERIFY(poll);
        QCOMPARE(poll->interval(), 1000);
        QCOMPARE(poll->timerType(), Qt::CoarseTimer);
        QVERIFY(!poll->isActive());
        QWindow window;
        window.show();
        platform.bindWindow(window);
        QVERIFY(!poll->isActive());
        platform.hideWindow();
        QVERIFY(poll->isActive());
        available = false;
        const int reads_before = reads;

        QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 2000);

        QVERIFY(reads > reads_before);
        QVERIFY(window.windowStates().testFlag(Qt::WindowMinimized));
        QVERIFY(!poll->isActive());
        available = true;
        platform.refreshAvailability();
        platform.hideWindow();
        QVERIFY(poll->isActive());
        window.setVisible(true);
        QVERIFY(!poll->isActive());
    }

    void destroyedAndReboundWindowsAreSafe() {
        QtShellPlatform platform(testIcon(), QStringLiteral("Player"), labels(), [] { return true; }, [] {});
        auto first = std::make_unique<QWindow>();
        first->showMaximized();
        platform.bindWindow(*first);
        platform.hideWindow();
        auto* const poll = platform.findChild<QTimer*>();
        QVERIFY(poll && poll->isActive());
        first.reset();
        QVERIFY(!poll->isActive());
        platform.hideWindow();
        platform.minimizeWindow();
        platform.showAndActivateWindow();
        platform.refreshAvailability();
        QVERIFY(!action(platform, "tray-show-hide")->isEnabled());
        QVERIFY(!action(platform, "tray-current-track")->isEnabled());

        QWindow second;
        second.show();
        platform.bindWindow(second);
        QWindow third;
        third.showFullScreen();
        platform.bindWindow(third);
        second.hide();
        platform.hideWindow();
        QVERIFY(!third.isVisible());
        platform.showAndActivateWindow();
        QCOMPARE(third.windowStates(), Qt::WindowStates{Qt::WindowFullScreen});
        QVERIFY(!second.isVisible());
    }

    void normalGeometryChangesAreCoalescedIntoOnePlacement() {
        QtShellPlatform platform(testIcon(), QStringLiteral("Player"), labels(), [] { return true; }, [] {});
        QWindow window;
        window.setGeometry(5, 7, 720, 520);
        window.show();
        platform.bindWindow(window);
        QList<WindowPlacement> observed;
        connect(&platform, &QtShellPlatform::windowPlacementChanged, &platform,
                [&observed](const WindowPlacement placement) { observed.append(placement); });

        window.setX(11);
        window.setY(13);
        window.resize(740, 540);

        QTRY_COMPARE(observed.size(), 1);
        const WindowPlacement expected{.x = 11, .y = 13, .width = 740, .height = 540};
        QCOMPARE(observed.constFirst(), expected);
    }

    void pendingNormalPlacementIsFlushedBeforeLeavingTheNormalVisibleState_data() {
        QTest::addColumn<bool>("hide");

        QTest::newRow("hide") << true;
        QTest::newRow("minimize") << false;
    }

    void pendingNormalPlacementIsFlushedBeforeLeavingTheNormalVisibleState() {
        QFETCH(bool, hide);
        QtShellPlatform platform(testIcon(), QStringLiteral("Player"), labels(), [] { return true; }, [] {});
        QWindow window;
        window.setGeometry(5, 7, 720, 520);
        window.show();
        platform.bindWindow(window);
        QList<WindowPlacement> observed;
        connect(&platform, &QtShellPlatform::windowPlacementChanged, &platform,
                [&observed](const WindowPlacement placement) { observed.append(placement); });

        window.setGeometry(23, 29, 740, 540);
        if (hide)
            platform.hideWindow();
        else
            platform.minimizeWindow();

        QCOMPARE(observed.size(), 1);
        const WindowPlacement expected{.x = 23, .y = 29, .width = 740, .height = 540};
        QCOMPARE(observed.constFirst(), expected);
    }

    void pendingNormalPlacementCanBeFlushedSynchronously() {
        QtShellPlatform platform(testIcon(), QStringLiteral("Player"), labels(), [] { return true; }, [] {});
        QWindow window;
        window.setGeometry(5, 7, 720, 520);
        window.show();
        platform.bindWindow(window);
        QList<WindowPlacement> observed;
        connect(&platform, &QtShellPlatform::windowPlacementChanged, &platform,
                [&observed](const WindowPlacement placement) { observed.append(placement); });

        window.setGeometry(31, 37, 760, 560);
        platform.flushPendingWindowPlacement();

        QCOMPARE(observed.size(), 1);
        const WindowPlacement expected{.x = 31, .y = 37, .width = 760, .height = 560};
        QCOMPARE(observed.constFirst(), expected);
    }

    void ambiguousPlacementIsDiscardedWhenANativeWindowStateTransitionWinsTheDebounce_data() {
        QTest::addColumn<int>("window_state");

        QTest::newRow("minimized") << static_cast<int>(Qt::WindowMinimized);
        QTest::newRow("maximized") << static_cast<int>(Qt::WindowMaximized);
        QTest::newRow("full screen") << static_cast<int>(Qt::WindowFullScreen);
    }

    void ambiguousPlacementIsDiscardedWhenANativeWindowStateTransitionWinsTheDebounce() {
        QFETCH(int, window_state);
        QtShellPlatform platform(testIcon(), QStringLiteral("Player"), labels(), [] { return true; }, [] {});
        QWindow window;
        window.setGeometry(5, 7, 720, 520);
        window.show();
        platform.bindWindow(window);
        QList<WindowPlacement> observed;
        connect(&platform, &QtShellPlatform::windowPlacementChanged, &platform,
                [&observed](const WindowPlacement placement) { observed.append(placement); });

        // Establish a previously committed normal placement. It is the conservative fallback if the native
        // backend reports expanded geometry before it reports a non-normal state.
        window.xChanged(window.x());
        QTRY_COMPARE(observed.size(), 1);
        const WindowPlacement stable{.x = 5, .y = 7, .width = 720, .height = 520};
        QCOMPARE(observed.constFirst(), stable);

        window.setX(0);
        window.setY(0);
        window.resize(1'600, 900);
        // The offscreen QPA defers its geometry notification. Model the direct native notification so the
        // coalescing timer is pending when the state transition arrives in the same event-loop turn.
        window.xChanged(window.x());
        window.setWindowStates(Qt::WindowStates::fromInt(window_state));
        QCoreApplication::processEvents();

        QCOMPARE(observed.size(), 1);
        QCOMPARE(observed.constFirst(), stable);

        window.setWindowStates(Qt::WindowNoState);
        window.setGeometry(41, 43, 750, 550);
        window.xChanged(window.x());

        QTRY_COMPARE(observed.size(), 2);
        const WindowPlacement restored{.x = 41, .y = 43, .width = 750, .height = 550};
        QCOMPARE(observed.constLast(), restored);
    }

    void nonNormalOrHiddenGeometryNeverReplacesTheLastNormalPlacement_data() {
        QTest::addColumn<int>("window_state");
        QTest::addColumn<bool>("visible");

        QTest::newRow("minimized") << static_cast<int>(Qt::WindowMinimized) << true;
        QTest::newRow("maximized") << static_cast<int>(Qt::WindowMaximized) << true;
        QTest::newRow("full screen") << static_cast<int>(Qt::WindowFullScreen) << true;
        QTest::newRow("hidden") << static_cast<int>(Qt::WindowNoState) << false;
    }

    void nonNormalOrHiddenGeometryNeverReplacesTheLastNormalPlacement() {
        QFETCH(int, window_state);
        QFETCH(bool, visible);
        QtShellPlatform platform(testIcon(), QStringLiteral("Player"), labels(), [] { return true; }, [] {});
        QWindow window;
        window.setGeometry(5, 7, 720, 520);
        window.show();
        platform.bindWindow(window);
        QList<WindowPlacement> observed;
        connect(&platform, &QtShellPlatform::windowPlacementChanged, &platform,
                [&observed](const WindowPlacement placement) { observed.append(placement); });

        window.setWindowStates(static_cast<Qt::WindowState>(window_state));
        window.setVisible(visible);
        window.setGeometry(31, 37, 760, 560);
        QCoreApplication::processEvents();

        QVERIFY(observed.isEmpty());
    }

    void rebindingInvalidatesAQueuedPlacementFromThePreviousWindow() {
        QtShellPlatform platform(testIcon(), QStringLiteral("Player"), labels(), [] { return true; }, [] {});
        QWindow first;
        first.setGeometry(5, 7, 720, 520);
        first.show();
        QWindow second;
        second.setGeometry(17, 19, 720, 520);
        second.show();
        platform.bindWindow(first);
        QList<WindowPlacement> observed;
        connect(&platform, &QtShellPlatform::windowPlacementChanged, &platform,
                [&observed](const WindowPlacement placement) { observed.append(placement); });

        first.setGeometry(23, 29, 740, 540);
        platform.bindWindow(second);
        QCoreApplication::processEvents();
        QVERIFY(observed.isEmpty());

        second.setGeometry(41, 43, 750, 550);
        QTRY_COMPARE(observed.size(), 1);
        const WindowPlacement expected{.x = 41, .y = 43, .width = 750, .height = 550};
        QCOMPARE(observed.constFirst(), expected);
    }

    void reentrantExitDuringPlacementFlushQuitsExactlyOnce() {
        int quits{0};
        QtShellPlatform platform(testIcon(), QStringLiteral("Player"), labels(), [] { return true; }, [&] { ++quits; });
        QWindow window;
        window.setGeometry(5, 7, 720, 520);
        window.show();
        platform.bindWindow(window);
        QMetaObject::Connection reentrant_exit;
        reentrant_exit = connect(&platform, &QtShellPlatform::windowPlacementChanged, &platform, [&] {
            disconnect(reentrant_exit);
            platform.requestExit();
        });

        window.setGeometry(31, 37, 760, 560);
        platform.requestExit();

        QCOMPARE(quits, 1);
    }

    void rebindingDuringPlacementFlushDoesNotMinimizeTheReplacementWindow() {
        QtShellPlatform platform(testIcon(), QStringLiteral("Player"), labels(), [] { return true; }, [] {});
        QWindow first;
        first.setGeometry(5, 7, 720, 520);
        first.show();
        QWindow replacement;
        replacement.setGeometry(17, 19, 720, 520);
        replacement.show();
        platform.bindWindow(first);
        connect(&platform, &QtShellPlatform::windowPlacementChanged, &platform,
                [&] { platform.bindWindow(replacement); });

        first.setGeometry(31, 37, 760, 560);
        platform.minimizeWindow();

        QVERIFY(!first.windowStates().testFlag(Qt::WindowMinimized));
        QVERIFY(!replacement.windowStates().testFlag(Qt::WindowMinimized));
        QVERIFY(replacement.isVisible());
    }

    void explicitExitIsIdempotentAndLatchedBeforeTheCallback() {
        int quits{0};
        QtShellPlatform* active_platform{nullptr};
        bool unavailable_during_quit{false};
        bool poll_stopped_during_quit{false};
        QtShellPlatform platform(
            testIcon(), QStringLiteral("Player"), labels(), [] { return true; },
            [&] {
                ++quits;
                unavailable_during_quit = !active_platform->trayAvailable();
                const auto* poll = active_platform->findChild<QTimer*>();
                poll_stopped_during_quit = poll && !poll->isActive();
                active_platform->requestExit();
            });
        active_platform = &platform;
        QWindow window;
        window.show();
        platform.bindWindow(window);
        platform.hideWindow();
        QSignalSpy exit_intent(&platform, &QtShellPlatform::exitRequested);
        QSignalSpy restore_intent(&platform, &QtShellPlatform::restoreRequested);

        platform.requestExit();
        platform.requestExit();
        platform.trayIcon()->activated(QSystemTrayIcon::DoubleClick);
        platform.showAndActivateWindow();
        platform.refreshAvailability();

        QCOMPARE(quits, 1);
        QVERIFY(unavailable_during_quit);
        QVERIFY(poll_stopped_during_quit);
        QCOMPARE(exit_intent.size(), 0);
        QCOMPARE(restore_intent.size(), 0);
        QVERIFY(!window.isVisible());
        for (const auto* menu_action : platform.menu()->actions())
            QVERIFY(!menu_action->isEnabled());
    }

    void repeatedTraySnapshotsDoNotRepublishActionsOrQueryAvailability() {
        int reads{0};
        QtShellPlatform platform(
            testIcon(), QStringLiteral("Player"), labels(),
            [&] {
                ++reads;
                return true;
            },
            [] {});
        QWindow window;
        window.show();
        platform.bindWindow(window);
        const TrayMenuState state{QStringLiteral("Faixa & artista"), true, true, true, false};
        platform.updateTrayState(state);
        int changes{0};
        for (auto* menu_action : platform.menu()->actions())
            connect(menu_action, &QAction::changed, this, [&] { ++changes; });
        const int reads_before = reads;

        for (int index = 0; index < 50; ++index)
            platform.updateTrayState(state);

        QCOMPARE(changes, 0);
        QCOMPARE(reads, reads_before);
    }

    void openingTheMenuRefreshesAvailabilityWithoutShowingNativeUi() {
        bool available{false};
        QtShellPlatform platform(testIcon(), QStringLiteral("Player"), labels(), [&] { return available; }, [] {});
        QWindow window;
        window.show();
        platform.bindWindow(window);
        platform.updateTrayState({QStringLiteral("Faixa"), true, true, true, true});
        auto* const play = action(platform, "tray-play-pause");
        QVERIFY(play && !play->isEnabled());
        QSignalSpy changed(&platform, &QtShellPlatform::availabilityChanged);
        available = true;

        // Exercise the public notification without opening a native menu or touching the desktop.
        platform.menu()->aboutToShow();

        QCOMPARE(changed.size(), 1);
        QVERIFY(play->isEnabled());
        QVERIFY(window.isVisible());
        available = false;
        platform.menu()->aboutToShow();
        QCOMPARE(changed.size(), 2);
        QVERIFY(!play->isEnabled());
        QVERIFY(window.isVisible());
    }

    void destroyingAHiddenAdapterReleasesThePollAndDisconnectsTheWindow() {
        int reads{0};
        QWindow window;
        window.show();
        auto platform = std::make_unique<QtShellPlatform>(
            testIcon(), QStringLiteral("Player"), labels(),
            [&] {
                ++reads;
                return true;
            },
            [] {});
        platform->bindWindow(window);
        platform->hideWindow();
        QPointer<QTimer> poll = platform->findChild<QTimer*>();
        QVERIFY(poll && poll->isActive());
        const int reads_before = reads;

        platform.reset();
        QVERIFY(poll.isNull());
        window.setVisible(true);
        window.hide();
        QCoreApplication::sendPostedEvents();

        QCOMPARE(reads, reads_before);
    }
};

} // namespace music_player::m0::test

QTEST_MAIN(music_player::m0::test::QtShellPlatformTests)

#include "QtShellPlatformTests.moc"
