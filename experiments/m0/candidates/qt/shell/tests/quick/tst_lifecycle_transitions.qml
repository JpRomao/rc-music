import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "ShellLifecycleTransitions"
    when: windowShown
    property var observedShell: null
    property var observedNotice: null
    property var closingPreferences: null
    property bool preferencesStoleFocus: false
    property string preferencesClosingFocus: ""
    property int preferencesOverlapSamples: 0
    property bool preferencesClosedDuringNoticeEntry: false
    property bool noticeReopening: false
    property bool reopenedNoticeLostFocus: false

    Component {
        id: shellComponent
        Main {
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
    Connections {
        target: testCase.closingPreferences
        function onClosed() {
            if (testCase.observedNotice && testCase.observedNotice.visible) {
                ++testCase.preferencesOverlapSamples;
                testCase.preferencesClosedDuringNoticeEntry = !testCase.observedNotice.opened;
                const focused = testCase.observedShell.activeFocusItem;
                testCase.preferencesClosingFocus = String(focused) + ":" + (focused ? focused.objectName : "none");
                testCase.preferencesStoleFocus = !testCase.isInside(focused, testCase.observedNotice.contentItem);
            }
        }
    }
    Connections {
        target: testCase.observedShell

        function onActiveFocusItemChanged() {
            if (testCase.noticeReopening && testCase.observedNotice && testCase.observedNotice.visible && !testCase.isInside(testCase.observedShell.activeFocusItem, testCase.observedNotice.contentItem))
                testCase.reopenedNoticeLostFocus = true;
        }
    }
    function test_newCloseReversesFadeAndPendingPreferencesCannotStealFocus() {
        testScenarioDriver.setSystemAppearance(false, false); // qmllint disable unqualified
        testScenarioDriver.setTrayAvailable(true); // qmllint disable unqualified
        compare(testShellUiContract.panelTransitionMs, 160); // qmllint disable unqualified
        const shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        observedShell = shell;
        shell.requestActivate();
        tryVerify(function () {
            return shell.active;
        });
        const preferencesButton = findChild(shell, "preferences");
        preferencesButton.forceActiveFocus(Qt.TabFocusReason);
        const notice = findChild(shell, "window-notice");
        observedNotice = notice;
        const cancel = findChild(shell, "window-notice-cancel");
        shell.close();
        tryVerify(function () {
            return notice.opened;
        });
        // Keep cancellation and reopening in the same event-loop turn so the
        // stale deferred focus restoration is exercised deterministically.
        verify(testPlayerPresenter.cancelWindowNotice()); // qmllint disable unqualified
        compare(testPlayerPresenter.windowNotice, PlayerPresenter.NoNotice); // qmllint disable unqualified
        verify(notice.visible);
        noticeReopening = true;
        shell.close();
        compare(testPlayerPresenter.windowNotice, PlayerPresenter.TrayCloseNotice); // qmllint disable unqualified
        tryVerify(function () {
            return notice.opened;
        });
        wait(0);
        noticeReopening = false;
        verify(!reopenedNoticeLostFocus, "A stale close callback moved focus outside the reopened lifecycle notice");
        verify(isInside(shell.activeFocusItem, notice.contentItem), "A stale close callback moved focus outside the reopened lifecycle notice");
        compare(testPlayerPresenter.windowNotice, PlayerPresenter.TrayCloseNotice); // qmllint disable unqualified
        compare(testScenarioDriver.platformHideCalls(), 0); // qmllint disable unqualified
        mouseClick(cancel);
        tryVerify(function () {
            return !notice.visible;
        });
        verify(preferencesButton.activeFocus, "Focus after closing the lifecycle notice: " + String(shell.activeFocusItem) + ":" + (shell.activeFocusItem ? shell.activeFocusItem.objectName : "none"));

        mouseClick(preferencesButton);
        const preferences = findChild(shell, "preferences-panel");
        closingPreferences = preferences;
        tryVerify(function () {
            return preferences.opened;
        });
        mouseClick(findChild(shell, "preferences-close"));
        verify(preferences.visible);
        // The older panel's exit ends while the newer notice is still entering.
        wait(80);
        verify(preferences.visible && !preferences.opened);
        shell.close();
        verify(notice.visible);
        tryVerify(function () {
            return !preferences.visible;
        });
        compare(preferencesOverlapSamples, 1);
        verify(preferencesClosedDuringNoticeEntry);
        verify(!preferencesStoleFocus, preferencesClosingFocus);
        tryVerify(function () {
            return notice.opened;
        });
        verify(findChild(shell, "window-notice-confirm").activeFocus);
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !notice.visible;
        });
        verify(preferencesButton.activeFocus, "Focus after closing the lifecycle notice: " + String(shell.activeFocusItem) + ":" + (shell.activeFocusItem ? shell.activeFocusItem.objectName : "none"));
        compare(testScenarioDriver.platformHideCalls(), 0); // qmllint disable unqualified
        compare(testScenarioDriver.platformExitCalls(), 0); // qmllint disable unqualified

        mouseClick(findChild(shell, "open-audio"));
        shell.setQueueOpen(true);
        shell.close();
        verify(notice.visible);
        wait(0);
        verify(!isInside(shell.activeFocusItem, findChild(shell, "queue-panel")), "Deferred queue focus escaped the active notice");
        tryVerify(function () {
            return notice.opened;
        });
        mouseClick(cancel);
        tryVerify(function () {
            return !notice.visible;
        });
    }
}
