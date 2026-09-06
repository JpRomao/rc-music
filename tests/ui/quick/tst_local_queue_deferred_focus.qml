import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "LocalQueueDeferredFocus"
    when: windowShown
    readonly property PlayerPresenter presenter: testLocalPlayerPresenter // qmllint disable unqualified
    readonly property var driver: testScenarioDriver // qmllint disable unqualified
    property var shell: null
    property var guardedModal: null
    property bool modalFocusEscaped: false

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testCase.presenter
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
        target: testCase.shell
        function onActiveFocusItemChanged() {
            if (testCase.guardedModal && testCase.guardedModal.visible && !testCase.isInside(testCase.shell.activeFocusItem, testCase.guardedModal.contentItem))
                testCase.modalFocusEscaped = true;
        }
    }

    function init() {
        driver.resetLocalScenario();
        guardedModal = null;
        modalFocusEscaped = false;
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.requestActivate();
        tryVerify(function () {
            return shell.active;
        });
    }

    function cleanup() {
        guardedModal = null;
        if (shell)
            shell.destroy();
        shell = null;
        driver.resetLocalScenario();
    }

    function reveal(list, index) {
        list.currentIndex = index;
        list.positionViewAtIndex(index, ListView.Contain);
        list.forceLayout();
        verify(list.currentItem !== null && list.currentItem.index === index);
        return list.currentItem.occurrenceId;
    }

    function seedQueue() {
        driver.enqueueLocalSelection(["C:/Music/A.mp3", "C:/Music/B.mp3", "C:/Music/C.mp3"]);
        verify(presenter.openAudio());
        const panel = findChild(shell, "queue-panel");
        const list = findChild(shell, "queue-list");
        tryCompare(list, "count", 3);
        driver.publishLocalReady();
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        shell.setQueueOpen(true);
        tryVerify(function () {
            return panel.visible && list.activeFocus;
        });
        const first = reveal(list, 0);
        const third = reveal(list, 2);
        const second = reveal(list, 1);
        list.forceActiveFocus(Qt.TabFocusReason);
        return {
            panel: panel,
            list: list,
            first: first,
            second: second,
            third: third
        };
    }

    function test_insertBeforeDeferredMoveKeepsOccurrenceAndDeleteTarget() {
        const queue = seedQueue();
        verify(presenter.importDroppedUrls(["file:///C:/Incoming"], 0));
        // Keep the move and the next worker batch before the deferred focus turn.
        queue.panel.moveSelected(1);
        driver.enqueueSourceBatch(["C:/Music/X.mp3"], 0, 1);
        driver.pollSourceImport();
        compare(queue.list.count, 4);
        wait(0);
        compare(queue.panel.selectedOccurrenceId, queue.second);
        compare(queue.list.currentIndex, 3);
        verify(queue.list.activeFocus);

        keyClick(Qt.Key_Delete);
        tryCompare(queue.list, "count", 3);
        const remaining = [reveal(queue.list, 0), reveal(queue.list, 1), reveal(queue.list, 2)];
        verify(!remaining.includes(queue.second));
        verify(remaining.includes(queue.first) && remaining.includes(queue.third));
    }

    function test_deferredMoveDoesNotEscapeClearConfirmation() {
        const queue = seedQueue();
        queue.panel.moveSelected(1);
        queue.panel.requestClear();
        const confirmation = findChild(shell, "queue-clear-confirmation");
        const cancel = findChild(shell, "queue-clear-cancel");
        verify(confirmation.visible);
        cancel.forceActiveFocus(Qt.TabFocusReason);
        guardedModal = confirmation;
        wait(0);
        verify(!modalFocusEscaped);
        verify(cancel.activeFocus);
        compare(queue.list.count, 3);
    }

    function test_removingPlayingTailKeepsThePrecedingKeyboardSelection() {
        const queue = seedQueue();
        verify(presenter.playQueueOccurrence(queue.third));
        driver.publishLocalReady();
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        tryCompare(presenter, "currentOccurrenceId", queue.third);
        wait(0);
        reveal(queue.list, 2);
        queue.list.forceActiveFocus(Qt.TabFocusReason);

        keyClick(Qt.Key_Delete);
        tryCompare(queue.list, "count", 2);
        compare(presenter.currentOccurrenceId, "");
        wait(0);
        compare(queue.list.currentIndex, 1);
        compare(queue.panel.selectedOccurrenceId, queue.second);
        verify(queue.list.activeFocus);
    }

    function test_disallowedFocusRestoreDiscardsPendingMove() {
        const queue = seedQueue();
        queue.panel.moveSelected(1);
        // Simulate the caller withdrawing focus authority while inactive/modal.
        queue.panel.locationFocusRestoreAllowed = false;
        const preferences = findChild(shell, "preferences");
        preferences.forceActiveFocus(Qt.TabFocusReason);
        wait(0);
        verify(preferences.activeFocus);
        queue.panel.locationFocusRestoreAllowed = true;
        wait(0);
        verify(preferences.activeFocus);
    }

    function test_hiddenAndReopenedPanelDoesNotReplayOldMoveFocus() {
        const queue = seedQueue();
        queue.panel.moveSelected(1);
        shell.queueOpen = false;
        shell.queueOpen = true;
        reveal(queue.list, 0);
        const add = findChild(shell, "queue-add");
        add.forceActiveFocus(Qt.TabFocusReason);
        wait(0);
        compare(queue.panel.selectedOccurrenceId, queue.first);
        compare(queue.list.currentIndex, 0);
        verify(add.activeFocus);
    }
}
