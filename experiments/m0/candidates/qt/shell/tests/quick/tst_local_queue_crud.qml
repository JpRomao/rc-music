import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "LocalQueueCrud"
    when: windowShown

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testLocalPlayerPresenter // qmllint disable unqualified
        }
    }

    property var shell

    function revealRow(listView, index) {
        listView.currentIndex = index;
        listView.positionViewAtIndex(index, ListView.Contain);
        listView.forceLayout();
        tryVerify(function () {
            return listView.currentItem !== null && listView.currentItem.index === index;
        });
        return listView.currentItem;
    }

    function enqueue(paths) {
        testScenarioDriver.enqueueLocalSelection(paths); // qmllint disable unqualified
    }

    function test_realLocalQueueCrudAndFocus() {
        verify(!testLocalPlayerPresenter.queueLoaded); // qmllint disable unqualified
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.requestActivate();
        tryVerify(function () {
            return shell.active;
        });

        enqueue(["C:/Música/Duplicada 日本語.mp3", "C:/Música/Duplicada 日本語.mp3", "C:/Música/{position} {total}.flac"]);
        const openAudio = findChild(shell, "open-audio");
        openAudio.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        tryVerify(function () {
            return testLocalPlayerPresenter.queueLoaded; // qmllint disable unqualified
        });
        compare(testScenarioDriver.localLoadCount(), 1); // qmllint disable unqualified
        compare(testLocalPlayerPresenter.currentQueueIndex, 0); // qmllint disable unqualified
        compare(testScenarioDriver.localPlayCount(), 0); // qmllint disable unqualified
        testScenarioDriver.publishLocalReady(); // qmllint disable unqualified
        tryVerify(function () {
            return testScenarioDriver.localPlayCount() === 1; // qmllint disable unqualified
        });
        testScenarioDriver.publishLocalPlaying(); // qmllint disable unqualified
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Playing); // qmllint disable unqualified

        keyClick(Qt.Key_L, Qt.ControlModifier);
        const queuePanel = findChild(shell, "queue-panel");
        const queueList = findChild(shell, "queue-list");
        tryVerify(function () {
            return queuePanel.visible && queueList.activeFocus && queueList.count === 3;
        });
        const firstId = revealRow(queueList, 0).occurrenceId;
        const duplicateId = revealRow(queueList, 1).occurrenceId;
        verify(firstId !== duplicateId);

        const queueAdd = findChild(shell, "queue-add");
        const queueMoveUp = findChild(shell, "queue-move-up");
        const queueMoveDown = findChild(shell, "queue-move-down");
        const queueRemove = findChild(shell, "queue-remove");
        const queueClear = findChild(shell, "queue-clear");
        for (const action of [queueAdd, queueMoveUp, queueMoveDown, queueRemove, queueClear]) {
            verify(action);
            compare(action.Accessible.role, Accessible.Button);
            verify(action.enabled);
        }

        enqueue(["C:/Música/Adicionada pelo espaço.ogg"]);
        queueAdd.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        tryCompare(queueList, "count", 4);
        compare(testScenarioDriver.localLoadCount(), 1); // qmllint disable unqualified
        verify(queueList.activeFocus);

        enqueue(["C:/Música/Adicionada pelo retorno.wav"]);
        queueAdd.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        tryCompare(queueList, "count", 5);
        compare(testScenarioDriver.localLoadCount(), 1); // qmllint disable unqualified

        enqueue(["C:/Música/Adicionada pela acessibilidade.m4a"]);
        queueAdd.Accessible.pressAction();
        tryCompare(queueList, "count", 6);
        compare(testScenarioDriver.localLoadCount(), 1); // qmllint disable unqualified

        const playedNowId = revealRow(queueList, 1).occurrenceId;
        queueList.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        tryCompare(testLocalPlayerPresenter, "currentOccurrenceId", playedNowId); // qmllint disable unqualified
        compare(testScenarioDriver.localLoadCount(), 2); // qmllint disable unqualified
        testScenarioDriver.publishLocalReady(); // qmllint disable unqualified
        tryVerify(function () {
            return testScenarioDriver.localPlayCount() === 2; // qmllint disable unqualified
        });
        testScenarioDriver.publishLocalPlaying(); // qmllint disable unqualified
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Playing); // qmllint disable unqualified
        verify(queueList.activeFocus);

        enqueue(["C:/Música/Aberta agora.mp3", "C:/Música/Depois da aberta.mp3"]);
        keyClick(Qt.Key_O, Qt.ControlModifier);
        tryCompare(queueList, "count", 8);
        compare(revealRow(queueList, 0).occurrenceId, firstId);
        compare(revealRow(queueList, 1).occurrenceId, duplicateId);
        const openedNowId = revealRow(queueList, 6).occurrenceId;
        compare(testLocalPlayerPresenter.currentOccurrenceId, openedNowId); // qmllint disable unqualified
        compare(testLocalPlayerPresenter.currentQueueIndex, 6); // qmllint disable unqualified
        compare(testScenarioDriver.localLoadCount(), 3); // qmllint disable unqualified
        compare(testScenarioDriver.localLoadedPath(2), "C:/Música/Aberta agora.mp3"); // qmllint disable unqualified
        testScenarioDriver.publishLocalReady(); // qmllint disable unqualified
        tryVerify(function () {
            return testScenarioDriver.localPlayCount() === 3; // qmllint disable unqualified
        });
        testScenarioDriver.publishLocalPlaying(); // qmllint disable unqualified
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Playing); // qmllint disable unqualified

        const successorId = revealRow(queueList, 7).occurrenceId;
        revealRow(queueList, 6);
        const unloadsBeforeCurrentRemoval = testScenarioDriver.localUnloadCount(); // qmllint disable unqualified
        queueRemove.Accessible.pressAction();
        tryVerify(function () {
            return queueList.count === 7 && queueList.currentIndex === 6 && queueList.currentItem !== null && queueList.currentItem.occurrenceId === successorId && queueList.activeFocus;
        });
        compare(testLocalPlayerPresenter.currentOccurrenceId, successorId); // qmllint disable unqualified
        compare(testScenarioDriver.localLoadCount(), 4); // qmllint disable unqualified
        compare(testScenarioDriver.localUnloadCount(), unloadsBeforeCurrentRemoval); // qmllint disable unqualified
        testScenarioDriver.publishLocalReady(); // qmllint disable unqualified
        tryVerify(function () {
            return testScenarioDriver.localPlayCount() === 4; // qmllint disable unqualified
        });
        testScenarioDriver.publishLocalPlaying(); // qmllint disable unqualified
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Playing); // qmllint disable unqualified

        queueList.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Delete);
        tryVerify(function () {
            return queueList.count === 6 && queueList.currentIndex === 5 && queueList.currentItem !== null && queueList.activeFocus;
        });
        compare(testLocalPlayerPresenter.currentOccurrenceId, ""); // qmllint disable unqualified
        compare(testScenarioDriver.localLoadCount(), 4); // qmllint disable unqualified
        compare(testScenarioDriver.localUnloadCount(), unloadsBeforeCurrentRemoval + 1); // qmllint disable unqualified

        queueList.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        tryCompare(testLocalPlayerPresenter, "currentOccurrenceId", queueList.currentItem.occurrenceId); // qmllint disable unqualified
        compare(testScenarioDriver.localLoadCount(), 5); // qmllint disable unqualified
        testScenarioDriver.publishLocalReady(); // qmllint disable unqualified
        tryVerify(function () {
            return testScenarioDriver.localPlayCount() === 5; // qmllint disable unqualified
        });
        testScenarioDriver.publishLocalPlaying(); // qmllint disable unqualified
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Playing); // qmllint disable unqualified

        const selectedRow = revealRow(queueList, 2);
        const movedId = selectedRow.occurrenceId;
        const movedTitle = selectedRow.title;
        queueList.forceActiveFocus(Qt.TabFocusReason);
        testScenarioDriver.clearAccessibleAnnouncements(); // qmllint disable unqualified
        keyClick(Qt.Key_Down, Qt.AltModifier);
        tryVerify(function () {
            return queueList.currentIndex === 3 && queueList.currentItem !== null && queueList.currentItem.occurrenceId === movedId && queueList.activeFocus;
        });
        compare(testScenarioDriver.accessibleAnnouncementCount(), 1); // qmllint disable unqualified
        compare(testScenarioDriver.lastAccessibleAnnouncement(), movedTitle + " movida para a posição 4 de 6."); // qmllint disable unqualified
        compare(testScenarioDriver.lastAccessibleAnnouncementPoliteness(), Accessible.Polite); // qmllint disable unqualified
        compare(queuePanel.accessibilityAnnouncement, movedTitle + " movida para a posição 4 de 6.");

        queueMoveUp.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        tryVerify(function () {
            return queueList.currentIndex === 2 && queueList.currentItem !== null && queueList.currentItem.occurrenceId === movedId && queueList.activeFocus;
        });
        compare(testScenarioDriver.accessibleAnnouncementCount(), 2); // qmllint disable unqualified
        compare(testScenarioDriver.lastAccessibleAnnouncement(), movedTitle + " movida para a posição 3 de 6."); // qmllint disable unqualified
        compare(queuePanel.accessibilityAnnouncement, movedTitle + " movida para a posição 3 de 6.");

        revealRow(queueList, 0);
        verify(!queueMoveUp.enabled);
        revealRow(queueList, queueList.count - 1);
        verify(!queueMoveDown.enabled);

        revealRow(queueList, 1);
        const removedByButtonId = queueList.currentItem.occurrenceId;
        queueRemove.Accessible.pressAction();
        tryCompare(queueList, "count", 5);
        tryVerify(function () {
            return queueList.activeFocus && queueList.currentItem !== null && queueList.currentItem.occurrenceId !== removedByButtonId;
        });

        const removedByDeleteId = revealRow(queueList, 0).occurrenceId;
        queueList.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Delete);
        tryCompare(queueList, "count", 4);
        tryVerify(function () {
            return queueList.activeFocus && queueList.currentItem !== null && queueList.currentItem.occurrenceId !== removedByDeleteId;
        });

        revealRow(queueList, 0);
        queueList.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Menu);
        const rowMenu = findChild(shell, "queue-row-menu");
        tryVerify(function () {
            return rowMenu.visible;
        });
        const menuRemove = findChild(shell, "queue-menu-remove");
        verify(menuRemove);
        mouseClick(menuRemove);
        tryCompare(queueList, "count", 3);
        verify(queueList.activeFocus);

        const countBeforeClear = queueList.count;
        const currentBeforeClear = testLocalPlayerPresenter.currentOccurrenceId; // qmllint disable unqualified
        const unloadsBeforeClear = testScenarioDriver.localUnloadCount(); // qmllint disable unqualified
        queueClear.forceActiveFocus(Qt.TabFocusReason);
        queueClear.Accessible.pressAction();
        const confirmation = findChild(shell, "queue-clear-confirmation");
        tryVerify(function () {
            return confirmation.opened;
        });
        compare(queueList.count, countBeforeClear);
        compare(testLocalPlayerPresenter.currentOccurrenceId, currentBeforeClear); // qmllint disable unqualified
        compare(testScenarioDriver.localUnloadCount(), unloadsBeforeClear); // qmllint disable unqualified
        compare(confirmation.contentItem.Accessible.role, Accessible.Dialog);
        compare(confirmation.contentItem.Accessible.name, "Limpar fila?");
        tryCompare(findChild(shell, "queue-clear-confirmation-message"), "text", "Os " + countBeforeClear + " itens da fila serão removidos. Esta ação não pode ser desfeita.");
        const cancelClear = findChild(shell, "queue-clear-cancel");
        const confirmClear = findChild(shell, "queue-clear-confirm");
        const preferences = findChild(shell, "preferences-panel");
        tryVerify(function () {
            return cancelClear.activeFocus;
        });
        compare(cancelClear.Accessible.role, Accessible.Button);
        compare(confirmClear.Accessible.role, Accessible.Button);
        const exitCallsBeforeModalShortcuts = testScenarioDriver.platformExitCalls(); // qmllint disable unqualified
        keyClick(Qt.Key_Comma, Qt.ControlModifier);
        verify(!preferences.visible);
        verify(confirmation.opened);
        verify(queuePanel.visible);
        keyClick(Qt.Key_Q, Qt.ControlModifier);
        compare(testScenarioDriver.platformExitCalls(), exitCallsBeforeModalShortcuts); // qmllint disable unqualified
        verify(!testLocalPlayerPresenter.exiting); // qmllint disable unqualified
        verify(confirmation.opened);
        cancelClear.Accessible.pressAction();
        tryVerify(function () {
            return !confirmation.visible && queueClear.activeFocus;
        });
        compare(queueList.count, countBeforeClear);
        compare(testLocalPlayerPresenter.currentOccurrenceId, currentBeforeClear); // qmllint disable unqualified
        compare(testScenarioDriver.localUnloadCount(), unloadsBeforeClear); // qmllint disable unqualified

        queueClear.Accessible.pressAction();
        tryVerify(function () {
            return confirmation.opened && cancelClear.activeFocus;
        });
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !confirmation.visible && queueClear.activeFocus;
        });
        compare(queueList.count, countBeforeClear);
        compare(testLocalPlayerPresenter.currentOccurrenceId, currentBeforeClear); // qmllint disable unqualified
        compare(testScenarioDriver.localUnloadCount(), unloadsBeforeClear); // qmllint disable unqualified

        queueClear.Accessible.pressAction();
        tryVerify(function () {
            return confirmation.opened && cancelClear.activeFocus;
        });
        confirmClear.Accessible.pressAction();
        confirmClear.Accessible.pressAction();
        tryVerify(function () {
            return !testLocalPlayerPresenter.queueLoaded; // qmllint disable unqualified
        });
        compare(testScenarioDriver.localUnloadCount(), unloadsBeforeClear + 1); // qmllint disable unqualified
        wait(50);
        compare(testScenarioDriver.localUnloadCount(), unloadsBeforeClear + 1); // qmllint disable unqualified
        tryVerify(function () {
            return !queuePanel.visible && openAudio.activeFocus;
        });
    }
}
