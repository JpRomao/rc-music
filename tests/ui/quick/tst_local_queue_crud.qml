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

    Component {
        id: fixtureConfirmationComponent
        QueueClearConfirmation {
            contract: testShellUiContract // qmllint disable unqualified
            queueCount: 3
            fallbackFocusItem: testCase.shell.contentItem
        }
    }

    property var shell

    function cleanup() {
        if (testLocalPlayerPresenter.queueLoaded) // qmllint disable unqualified
            testLocalPlayerPresenter.clearQueue(); // qmllint disable unqualified
    }

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

        enqueue(["C:/Música/Adicionada pelo Enter.ogg"]);
        queueAdd.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Enter);
        tryCompare(queueList, "count", 4);
        compare(testScenarioDriver.localLoadCount(), 1); // qmllint disable unqualified
        tryVerify(function () {
            return queueAdd.activeFocus;
        });

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
        keyClick(Qt.Key_Return);
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
        keyClick(Qt.Key_Return);
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
        compare(findChild(confirmation, "dialog-title").text, "Limpar a fila?");
        // The Aurora dialog is a Dialog in the native accessibility tree; as in every Aurora dialog, its question is
        // the heading that names it.
        const dialogAccessible = testScenarioDriver.accessibleSnapshot(confirmation.contentItem.parent); // qmllint disable unqualified
        verify(dialogAccessible.valid);
        compare(dialogAccessible.role, Accessible.Dialog);
        const dialogTitle = findChild(confirmation, "dialog-title");
        compare(dialogTitle.Accessible.role, Accessible.Heading);
        compare(dialogTitle.Accessible.name, "Limpar a fila?");
        tryCompare(findChild(confirmation, "confirm-consequence"), "text", "As " + countBeforeClear + " faixas saem da fila. Os arquivos, a biblioteca e as playlists não mudam. Isso não pode ser desfeito.");
        const cancelClear = findChild(shell, "queue-clear-confirmation-cancel");
        const confirmClear = findChild(shell, "queue-clear-confirmation-confirm");
        compare(confirmClear.text, "Limpar fila");
        compare(confirmClear.kind, AuroraButton.DangerFilled);
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
        // The drawer stays open on its empty state and the focus moves to its action. Closing it hands the focus to
        // the empty home's open action: the queue's opener, the queue button and play are gone or disabled.
        tryVerify(function () {
            return queuePanel.visible && findChild(queuePanel, "queue-empty-primary").activeFocus;
        });
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !queuePanel.visible && openAudio.activeFocus;
        });
    }

    // Opens the drawer on a local queue of the given files and returns its controls.
    function openDrawerWith(paths) {
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.requestActivate();
        tryVerify(function () {
            return shell.active;
        });
        enqueue(paths);
        findChild(shell, "open-audio").forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        tryVerify(function () {
            return testLocalPlayerPresenter.queueLoaded; // qmllint disable unqualified
        });
        testScenarioDriver.publishLocalReady(); // qmllint disable unqualified
        testScenarioDriver.publishLocalPlaying(); // qmllint disable unqualified
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Playing); // qmllint disable unqualified
        keyClick(Qt.Key_L, Qt.ControlModifier);
        const panel = findChild(shell, "queue-panel");
        tryVerify(function () {
            return panel.visible && findChild(shell, "queue-list").count === paths.length;
        });
        return findChild(shell, "queue-clear");
    }

    function test_realQueueClearAsksWithTheDangerDialog() {
        const queueClear = openDrawerWith(["C:/Música/Primeira.mp3", "C:/Música/Segunda.mp3"]);
        queueClear.forceActiveFocus(Qt.TabFocusReason);
        queueClear.Accessible.pressAction();
        const confirmation = findChild(shell, "queue-clear-confirmation");
        tryVerify(function () {
            return confirmation.opened;
        });
        verify(confirmation instanceof AuroraConfirmDialog);
        compare(findChild(confirmation, "dialog-title").text, "Limpar a fila?");
        compare(findChild(confirmation, "confirm-consequence").text, "As 2 faixas saem da fila. Os arquivos, a biblioteca e as playlists não mudam. Isso não pode ser desfeito.");
        const cancel = findChild(shell, "queue-clear-confirmation-cancel");
        const confirm = findChild(shell, "queue-clear-confirmation-confirm");
        tryVerify(function () {
            return cancel.activeFocus;
        });
        compare(confirm.text, "Limpar fila");
        compare(confirm.kind, AuroraButton.DangerFilled);

        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !confirmation.visible && queueClear.activeFocus;
        });
        queueClear.Accessible.pressAction();
        tryVerify(function () {
            return confirmation.opened && cancel.activeFocus;
        });
        cancel.Accessible.pressAction();
        tryVerify(function () {
            return !confirmation.visible && queueClear.activeFocus;
        });
        compare(findChild(shell, "queue-list").count, 2);

        queueClear.Accessible.pressAction();
        tryVerify(function () {
            return confirmation.opened;
        });
        confirm.Accessible.pressAction();
        tryVerify(function () {
            return !testLocalPlayerPresenter.queueLoaded; // qmllint disable unqualified
        });
        tryVerify(function () {
            return !confirmation.visible && findChild(shell, "queue-empty-primary").activeFocus;
        });
    }

    function test_realQueueClearOfOneTrackUsesTheSingularMessage() {
        const queueClear = openDrawerWith(["C:/Música/Unica.mp3"]);
        queueClear.Accessible.pressAction();
        const confirmation = findChild(shell, "queue-clear-confirmation");
        tryVerify(function () {
            return confirmation.opened;
        });
        compare(findChild(confirmation, "confirm-consequence").text, "A faixa sai da fila. Os arquivos, a biblioteca e as playlists não mudam. Isso não pode ser desfeito.");
    }

    // The frozen fixture keeps its own confirmation texts; the Aurora dialog has its own keys.
    function test_realQueueFixtureConfirmationKeepsItsTexts() {
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.requestActivate();
        tryVerify(function () {
            return shell.active;
        });
        const confirmation = createTemporaryObject(fixtureConfirmationComponent, shell.contentItem, {
            "parent": shell.contentItem
        });
        verify(confirmation);
        confirmation.open();
        tryVerify(function () {
            return confirmation.opened;
        });
        compare(confirmation.contentItem.Accessible.name, "Limpar fila?");
        compare(findChild(confirmation, "queue-clear-confirmation-message").text, "Os 3 itens da fila serão removidos. Esta ação não pode ser desfeita.");
        confirmation.close();
    }
}
