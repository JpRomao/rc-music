import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "LocalQueueMenuContext"
    when: windowShown
    readonly property PlayerPresenter presenter: testLocalPlayerPresenter // qmllint disable unqualified
    readonly property var driver: testScenarioDriver // qmllint disable unqualified
    property var shell: null

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testCase.presenter
        }
    }

    function init() {
        driver.resetLocalScenario();
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.requestActivate();
        tryVerify(function () {
            return shell.active;
        });
    }

    function cleanup() {
        if (shell)
            shell.destroy();
        shell = null;
        driver.resetLocalScenario();
    }

    function confirmPlaying() {
        driver.publishLocalReady();
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
    }

    function occurrenceAt(list, index) {
        list.positionViewAtIndex(index, ListView.Contain);
        list.forceLayout();
        const row = list.itemAtIndex(index);
        verify(row !== null);
        return row.occurrenceId;
    }

    function queueOrder(list) {
        const ids = [];
        for (let index = 0; index < list.count; ++index)
            ids.push(occurrenceAt(list, index));
        return ids.join(",");
    }

    function seedQueue(unavailableTarget, paths = ["C:/Music/A.mp3", "C:/Music/B.mp3", "C:/Music/C.mp3", "C:/Music/D.mp3"]) {
        driver.enqueueLocalSelection(paths);
        verify(presenter.openAudio());
        const list = findChild(shell, "queue-list");
        tryCompare(list, "count", paths.length);
        confirmPlaying();
        shell.setQueueOpen(true);
        tryVerify(function () {
            return list.visible && list.activeFocus;
        });
        const ids = [];
        for (let index = 0; index < list.count; ++index)
            ids.push(occurrenceAt(list, index));
        if (unavailableTarget) {
            verify(presenter.playQueueOccurrence(ids[2]));
            driver.publishLocalSourceUnavailable();
            tryCompare(presenter, "currentOccurrenceId", ids[3]);
            confirmPlaying();
            verify(presenter.playQueueOccurrence(ids[0]));
            confirmPlaying();
        }
        wait(0);
        return {
            list: list,
            ids: ids
        };
    }

    function openTargetMenu(queue, index = 2) {
        queue.list.currentIndex = index;
        queue.list.positionViewAtIndex(index, ListView.Contain);
        queue.list.forceLayout();
        compare(queue.list.currentItem.occurrenceId, queue.ids[index]);
        queue.list.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Menu);
        const menu = findChild(shell, "queue-row-menu");
        tryVerify(function () {
            return menu.opened;
        });
        return menu;
    }

    function advanceWhileMenuIsOpen(queue, menu) {
        compare(presenter.currentOccurrenceId, queue.ids[0]);
        driver.publishLocalSourceUnavailable();
        tryCompare(presenter, "currentOccurrenceId", queue.ids[1]);
        confirmPlaying();
        // Deliver the deferred current-track synchronization while the user is
        // still choosing an action for C, not for the newly playing B.
        wait(0);
        verify(menu.opened);
    }

    function activateMenuItem(objectName) {
        const action = findChild(shell, objectName);
        verify(action && action.visible && action.enabled);
        action.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
    }

    function test_actionsKeepTheOccurrenceWhoseMenuWasOpened_data() {
        return [
            {
                tag: "play-now",
                action: "queue-menu-play-now"
            },
            {
                tag: "remove",
                action: "queue-menu-remove"
            },
            {
                tag: "move-up",
                action: "queue-menu-move-up"
            },
            {
                tag: "move-down",
                action: "queue-menu-move-down"
            }
        ];
    }

    function test_actionsKeepTheOccurrenceWhoseMenuWasOpened(data) {
        const queue = seedQueue(false);
        const menu = openTargetMenu(queue);
        advanceWhileMenuIsOpen(queue, menu);
        const loadsBeforeAction = driver.localLoadCount();
        const playsBeforeAction = driver.localPlayCount();
        activateMenuItem(data.action);
        if (data.tag === "play-now") {
            compare(presenter.currentOccurrenceId, queue.ids[2]);
            compare(driver.localLoadCount(), loadsBeforeAction + 1);
            compare(driver.localLoadedPath(loadsBeforeAction), "C:/Music/C.mp3");
            compare(queueOrder(queue.list), queue.ids.join(","));
        } else {
            compare(presenter.currentOccurrenceId, queue.ids[1]);
            compare(presenter.playbackState, PlayerPresenter.Playing);
            compare(driver.localLoadCount(), loadsBeforeAction);
            compare(driver.localPlayCount(), playsBeforeAction);
            if (data.tag === "remove")
                compare(queueOrder(queue.list), [queue.ids[0], queue.ids[1], queue.ids[3]].join(","));
            else if (data.tag === "move-up")
                compare(queueOrder(queue.list), [queue.ids[0], queue.ids[2], queue.ids[1], queue.ids[3]].join(","));
            else
                compare(queueOrder(queue.list), [queue.ids[0], queue.ids[1], queue.ids[3], queue.ids[2]].join(","));
        }
    }

    function test_recoveryActionsKeepTheUnavailableMenuOccurrence_data() {
        return [
            {
                tag: "retry",
                action: "queue-menu-retry"
            },
            {
                tag: "locate",
                action: "queue-menu-locate"
            }
        ];
    }

    function test_recoveryActionsKeepTheUnavailableMenuOccurrence(data) {
        const queue = seedQueue(true);
        const menu = openTargetMenu(queue);
        const action = findChild(shell, data.action);
        verify(action.visible && action.enabled);
        advanceWhileMenuIsOpen(queue, menu);
        const loadsBeforeAction = driver.localLoadCount();
        const playsBeforeAction = driver.localPlayCount();
        activateMenuItem(data.action);
        if (data.tag === "retry") {
            compare(presenter.retryingOccurrenceId, queue.ids[2]);
            compare(presenter.currentOccurrenceId, queue.ids[2]);
            compare(driver.localLoadCount(), loadsBeforeAction + 1);
            compare(driver.localLoadedPath(loadsBeforeAction), "C:/Music/C.mp3");
        } else {
            compare(presenter.locatingOccurrenceId, queue.ids[2]);
            compare(driver.localLocateCount(), 1);
            compare(driver.localLocateSource(0), "C:/Music/C.mp3");
            driver.completeLocalLocation("C:/Music/C-relocated.mp3");
            tryCompare(presenter, "locatingOccurrenceId", "");
            compare(presenter.currentOccurrenceId, queue.ids[1]);
            compare(presenter.playbackState, PlayerPresenter.Playing);
            compare(driver.localLoadCount(), loadsBeforeAction);
            compare(driver.localPlayCount(), playsBeforeAction);
        }
        compare(queueOrder(queue.list), queue.ids.join(","));
    }

    function test_removedMenuTargetClosesAndMakesStaleActionsInert() {
        const queue = seedQueue(false);
        const menu = openTargetMenu(queue);
        verify(presenter.removeQueueOccurrence(queue.ids[2]));
        tryVerify(function () {
            return !menu.visible;
        });
        const expectedOrder = [queue.ids[0], queue.ids[1], queue.ids[3]].join(",");
        compare(queueOrder(queue.list), expectedOrder);
        const loadsBeforeStaleAction = driver.localLoadCount();
        // A queued activation belonging to the dismissed menu must not be
        // retargeted to the row that replaced C at its previous index.
        for (const actionName of ["queue-menu-play-now", "queue-menu-remove", "queue-menu-move-up", "queue-menu-move-down"]) {
            const action = findChild(shell, actionName);
            verify(!action.enabled);
            action.triggered();
        }
        compare(queueOrder(queue.list), expectedOrder);
        compare(presenter.currentOccurrenceId, queue.ids[0]);
        compare(presenter.playbackState, PlayerPresenter.Playing);
        compare(driver.localLoadCount(), loadsBeforeStaleAction);
    }

    function test_insertAndReorderBeforeMenuTargetUseItsLivePosition() {
        const queue = seedQueue(false);
        const menu = openTargetMenu(queue);
        const loadsBeforeChanges = driver.localLoadCount();
        verify(presenter.moveQueueOccurrence(queue.ids[3], 0));
        queue.list.forceLayout();
        tryCompare(queue.list, "currentIndex", 3);
        verify(menu.opened);
        compare(queue.list.currentItem.occurrenceId, queue.ids[2]);

        verify(presenter.importDroppedUrls(["file:///C:/Incoming"], 0));
        driver.enqueueSourceBatch(["C:/Music/X.mp3"], 0, 1);
        driver.pollSourceImport();
        tryCompare(queue.list, "count", 5);
        queue.list.forceLayout();
        tryCompare(queue.list, "currentIndex", 4);
        verify(menu.opened);
        compare(queue.list.currentItem.occurrenceId, queue.ids[2]);
        const insertedId = occurrenceAt(queue.list, 0);
        activateMenuItem("queue-menu-move-up");
        compare(queueOrder(queue.list), [insertedId, queue.ids[3], queue.ids[0], queue.ids[2], queue.ids[1]].join(","));
        compare(presenter.currentOccurrenceId, queue.ids[0]);
        compare(presenter.playbackState, PlayerPresenter.Playing);
        compare(driver.localLoadCount(), loadsBeforeChanges);
    }

    function test_reopenedMenuUsesTheNewOccurrenceInsteadOfThePreviousTarget() {
        const queue = seedQueue(false);
        const menu = openTargetMenu(queue);
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !menu.visible;
        });
        openTargetMenu(queue, 3);
        const loadsBeforeAction = driver.localLoadCount();
        activateMenuItem("queue-menu-remove");
        compare(queueOrder(queue.list), [queue.ids[0], queue.ids[1], queue.ids[2]].join(","));
        compare(presenter.currentOccurrenceId, queue.ids[0]);
        compare(presenter.playbackState, PlayerPresenter.Playing);
        compare(driver.localLoadCount(), loadsBeforeAction);
    }

    function test_refocusingTheQueueDoesNotTakeFocusFromItsOpenMenu() {
        const queue = seedQueue(false);
        const menu = openTargetMenu(queue);
        const action = findChild(shell, "queue-menu-remove");
        action.forceActiveFocus(Qt.TabFocusReason);
        verify(action.activeFocus);
        // Main's deferred focus restoration can arrive after the menu opened.
        findChild(shell, "queue-panel").focusCurrent();
        verify(menu.opened);
        verify(action.activeFocus);
        verify(!queue.list.activeFocus);
        compare(queue.list.currentItem.occurrenceId, queue.ids[2]);
    }

    function test_keyboardRemovalKeepsTheSelectedOccurrenceDuringPlaybackAdvance() {
        const queue = seedQueue(false);
        keyClick(Qt.Key_Down, Qt.ControlModifier);
        keyClick(Qt.Key_Down, Qt.ControlModifier);
        compare(queue.list.currentItem.occurrenceId, queue.ids[2]);

        driver.publishLocalSourceUnavailable();
        tryCompare(presenter, "currentOccurrenceId", queue.ids[1]);
        confirmPlaying();
        wait(0);

        verify(queue.list.activeFocus);
        keyClick(Qt.Key_Delete);
        compare(queueOrder(queue.list), [queue.ids[0], queue.ids[1], queue.ids[3]].join(","));
        compare(presenter.currentOccurrenceId, queue.ids[1]);
        compare(presenter.playbackState, PlayerPresenter.Playing);
    }

    function test_homeAndEndRevealQueueBoundariesWithoutChangingPlayback() {
        const paths = Array.from({
            length: 20
        }, (_, index) => "C:/Music/Track-" + index + ".mp3");
        const queue = seedQueue(false, paths);
        const firstId = queue.ids[0];
        const lastId = queue.ids[queue.ids.length - 1];
        queue.list.positionViewAtBeginning();
        verify(queue.list.contentHeight > queue.list.height);

        keyClick(Qt.Key_End);
        compare(queue.list.currentItem.occurrenceId, lastId);
        verify(queue.list.atYEnd);
        compare(presenter.currentOccurrenceId, firstId);
        compare(presenter.playbackState, PlayerPresenter.Playing);

        keyClick(Qt.Key_Home);
        compare(queue.list.currentItem.occurrenceId, firstId);
        verify(queue.list.atYBeginning);
        compare(presenter.currentOccurrenceId, firstId);
        compare(presenter.playbackState, PlayerPresenter.Playing);

        keyClick(Qt.Key_End);
        keyClick(Qt.Key_Return);
        compare(presenter.currentOccurrenceId, lastId);
    }
}
