import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "ShellKeyboardExit"
    when: windowShown
    readonly property PlayerPresenter presenter: testPlayerPresenter // qmllint disable unqualified
    readonly property var driver: testScenarioDriver // qmllint disable unqualified

    Component {
        id: otherWindowComponent
        Window {
            width: 100
            height: 100
            visible: true
        }
    }
    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testCase.presenter
        }
    }

    function test_explicitExitFromPreferencesButNotLifecycleNotice() {
        driver.setSystemAppearance(false, true);
        driver.setTrayAvailable(true);
        const shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.requestActivate();
        tryVerify(function () {
            return shell.active;
        });
        const otherWindow = createTemporaryObject(otherWindowComponent, null);
        verify(otherWindow);
        otherWindow.requestActivate();
        tryVerify(function () {
            return otherWindow.active && !shell.active;
        });
        keyClick(Qt.Key_Comma, Qt.ControlModifier);
        verify(!findChild(shell, "preferences-panel").visible);
        keyClick(Qt.Key_Q, Qt.ControlModifier);
        verify(!presenter.exiting);
        otherWindow.close();
        shell.requestActivate();
        tryVerify(function () {
            return shell.active;
        });
        const open = findChild(shell, "open-audio");
        open.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        verify(presenter.queueLoaded);
        verify(presenter.togglePlayback());
        compare(presenter.playbackState, PlayerPresenter.Paused);
        compare(presenter.closePreference, PlayerPresenter.MinimizeToTray);
        keyClick(Qt.Key_Comma, Qt.ControlModifier);
        const preferences = findChild(shell, "preferences-panel");
        tryVerify(function () {
            return preferences.opened;
        });
        mouseClick(findChild(shell, "preferences-playback"));
        const field = findChild(shell, "skip-forward-seconds");
        field.forceActiveFocus(Qt.TabFocusReason);
        verify(field.activeFocus);
        shell.close();
        const notice = findChild(shell, "window-notice");
        tryVerify(function () {
            return notice.opened;
        });
        findChild(shell, "window-notice-message").forceActiveFocus(Qt.TabFocusReason);
        const position = presenter.positionMs;
        const occurrence = presenter.currentOccurrenceId;
        for (const key of [Qt.Key_Space, Qt.Key_M, Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down])
            keyClick(key);
        for (const key of [Qt.Key_O, Qt.Key_Q, Qt.Key_L, Qt.Key_Left, Qt.Key_Right, Qt.Key_Comma])
            keyClick(key, Qt.ControlModifier);
        verify(!presenter.exiting);
        compare(driver.platformExitCalls(), 0);
        compare(driver.platformHideCalls(), 0);
        compare(presenter.windowNotice, PlayerPresenter.TrayCloseNotice);
        compare(presenter.playbackState, PlayerPresenter.Paused);
        compare(presenter.positionMs, position);
        compare(presenter.currentOccurrenceId, occurrence);
        compare(presenter.volume, 0.8);
        verify(!presenter.muted);
        verify(!shell.queueOpen);
        driver.setSystemAppearance(false, false);
        keyClick(Qt.Key_Escape);
        verify(notice.visible && !notice.opened);
        keyClick(Qt.Key_Q, Qt.ControlModifier);
        keyClick(Qt.Key_Comma, Qt.ControlModifier);
        verify(!presenter.exiting, "Lifecycle exit fade still suspends window shortcuts");
        tryVerify(function () {
            return !notice.visible;
        });
        verify(preferences.opened);
        verify(field.activeFocus);
        keyClick(Qt.Key_Comma, Qt.ControlModifier);
        verify(findChild(shell, "preferences-general").activeFocus, "Ctrl+, must refocus the open preferences panel");
        verify(preferences.opened);
        field.forceActiveFocus(Qt.TabFocusReason);
        verify(driver.repeatKey(Qt.Key_Comma, Qt.ControlModifier));
        verify(field.activeFocus);
        keyClick(Qt.Key_Q, Qt.ControlModifier);
        verify(presenter.exiting, "Ctrl+Q must request real exit even while editing preferences");
        compare(driver.platformExitCalls(), 1);
        compare(driver.platformHideCalls(), 0);
        compare(driver.platformMinimizeCalls(), 0);
        compare(presenter.closePreference, PlayerPresenter.MinimizeToTray);
        compare(presenter.windowNotice, PlayerPresenter.NoNotice);
        keyClick(Qt.Key_Q, Qt.ControlModifier);
        verify(driver.repeatKey(Qt.Key_Q, Qt.ControlModifier));
        compare(driver.platformExitCalls(), 1);
        shell.close();
        verify(!shell.visible);
    }
}
