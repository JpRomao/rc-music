import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "ShellLifecycle"
    when: windowShown

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testPlayerPresenter // qmllint disable unqualified
        }
    }

    Component {
        id: deferredShellComponent
        Main {
            startupReady: false
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testPlayerPresenter // qmllint disable unqualified
        }
    }

    function isInside(item, ancestor) {
        while (item) {
            if (item === ancestor)
                return true;
            item = item.parent;
        }
        return false;
    }

    function test_startupVisibilityWaitsForPlacementResolution() {
        const shell = createTemporaryObject(deferredShellComponent, null);
        verify(shell);
        compare(shell.visible, false);

        shell.startupReady = true;

        tryCompare(shell, "visible", true);
    }

    function test_closeNoticeFallbackAndExitJourney() {
        testScenarioDriver.setSystemAppearance(false, true); // qmllint disable unqualified
        testScenarioDriver.setTrayAvailable(true); // qmllint disable unqualified
        const shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.width = 720;
        shell.height = 520;
        shell.requestActivate();
        tryVerify(function () {
            return shell.active;
        });
        mouseClick(findChild(shell, "open-audio"));
        const playPause = findChild(shell, "play-pause");
        mouseClick(playPause);
        compare(testPlayerPresenter.playbackState, PlayerPresenter.Paused); // qmllint disable unqualified
        const model = testPlayerPresenter.queueModel; // qmllint disable unqualified
        const position = testPlayerPresenter.positionMs; // qmllint disable unqualified
        playPause.forceActiveFocus(Qt.TabFocusReason);
        shell.close();
        compare(shell.visible, true);
        compare(testScenarioDriver.platformHideCalls(), 0); // qmllint disable unqualified
        compare(testScenarioDriver.platformExitCalls(), 0); // qmllint disable unqualified
        const notice = findChild(shell, "window-notice");
        verify(notice);
        tryVerify(function () {
            return notice.opened;
        });
        compare(testPlayerPresenter.windowNotice, PlayerPresenter.TrayCloseNotice); // qmllint disable unqualified
        const message = findChild(shell, "window-notice-message");
        compare(message.text, "O player continuará na bandeja. Use Sair para encerrar.");
        compare(message.Accessible.id, "window-notice-message");
        compare(notice.contentItem.Accessible.role, Accessible.Dialog);
        const confirm = findChild(shell, "window-notice-confirm");
        const cancel = findChild(shell, "window-notice-cancel");
        const enabled = findChild(shell, "window-notice-enabled");
        verify(confirm.activeFocus);
        compare(confirm.text, "Continuar");
        compare(cancel.text, "Cancelar");
        verify(enabled.checked);
        verify(notice.x >= 0 && notice.y >= 0);
        verify(notice.x + notice.width <= shell.width);
        verify(notice.y + notice.height <= shell.height);
        for (let index = 0; index < 8; ++index) {
            keyClick(Qt.Key_Tab);
            verify(isInside(shell.activeFocusItem, notice.contentItem));
        }
        keyClick(Qt.Key_Right, Qt.ControlModifier);
        keyClick(Qt.Key_L, Qt.ControlModifier);
        keyClick(Qt.Key_Comma, Qt.ControlModifier);
        compare(testPlayerPresenter.currentOccurrenceId, "queue-0017"); // qmllint disable unqualified
        verify(!findChild(shell, "queue-panel").visible);
        verify(!findChild(shell, "preferences-panel").visible);
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !notice.visible;
        });
        compare(testPlayerPresenter.windowNotice, PlayerPresenter.NoNotice); // qmllint disable unqualified
        verify(playPause.activeFocus);
        compare(testScenarioDriver.platformHideCalls(), 0); // qmllint disable unqualified

        shell.close();
        tryVerify(function () {
            return notice.opened;
        });
        shell.close();
        compare(testScenarioDriver.platformHideCalls(), 0); // qmllint disable unqualified
        mouseClick(enabled);
        verify(!testPlayerPresenter.trayNoticeEnabled); // qmllint disable unqualified
        testScenarioDriver.setSystemAppearance(false, false); // qmllint disable unqualified
        compare(testShellUiContract.panelTransitionMs, 160); // qmllint disable unqualified
        mouseClick(cancel);
        compare(message.text, "O player continuará na bandeja. Use Sair para encerrar.");
        tryVerify(function () {
            return !notice.visible;
        });
        testScenarioDriver.setSystemAppearance(false, true); // qmllint disable unqualified
        shell.close();
        compare(testScenarioDriver.platformHideCalls(), 1); // qmllint disable unqualified
        verify(!notice.visible);

        verify(testPlayerPresenter.setTrayNoticeEnabled(true)); // qmllint disable unqualified
        testScenarioDriver.setHighContrast(true, false); // qmllint disable unqualified
        shell.close();
        tryVerify(function () {
            return notice.opened;
        });
        compare(notice.background.color.toString(), "#010102");
        compare(message.color.toString(), "#fafafa");
        mouseClick(confirm);
        tryVerify(function () {
            return !notice.visible;
        });
        compare(testScenarioDriver.platformHideCalls(), 2); // qmllint disable unqualified
        shell.close();
        compare(testScenarioDriver.platformHideCalls(), 3); // qmllint disable unqualified
        verify(!notice.visible);
        compare(testPlayerPresenter.queueModel, model); // qmllint disable unqualified
        compare(testPlayerPresenter.positionMs, position); // qmllint disable unqualified
        compare(testPlayerPresenter.playbackState, PlayerPresenter.Paused); // qmllint disable unqualified

        testScenarioDriver.setTrayAvailable(false); // qmllint disable unqualified
        shell.close();
        compare(testScenarioDriver.platformMinimizeCalls(), 1); // qmllint disable unqualified
        compare(testScenarioDriver.platformHideCalls(), 3); // qmllint disable unqualified
        tryVerify(function () {
            return notice.opened;
        });
        compare(testPlayerPresenter.windowNotice, PlayerPresenter.TrayUnavailableNotice); // qmllint disable unqualified
        compare(message.text, "Não há bandeja disponível neste ambiente. Ao fechar, a janela é minimizada normalmente.");
        compare(confirm.text, "Entendi");
        verify(!cancel.visible && !enabled.visible);
        mouseClick(confirm);
        tryVerify(function () {
            return !notice.visible;
        });
        compare(testScenarioDriver.platformMinimizeCalls(), 1); // qmllint disable unqualified

        verify(testPlayerPresenter.setClosePreference(PlayerPresenter.Exit)); // qmllint disable unqualified
        shell.close();
        verify(testPlayerPresenter.exiting); // qmllint disable unqualified
        compare(testScenarioDriver.platformExitCalls(), 1); // qmllint disable unqualified
        verify(!notice.visible);
        verify(!testPlayerPresenter.requestExit()); // qmllint disable unqualified
        compare(testScenarioDriver.platformExitCalls(), 1); // qmllint disable unqualified
        shell.close();
        tryCompare(shell, "visible", false);
    }
}
