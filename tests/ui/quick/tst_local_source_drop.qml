import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "LocalSourceDrop"
    when: windowShown

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testLocalPlayerPresenter // qmllint disable unqualified
        }
    }

    property var shell

    function scenePoint(item, x, y) {
        return item.mapToItem(null, x, y);
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

    function beginUrlDragAt(point, sources) {
        return testScenarioDriver.beginUrlDrag(shell, point.x, point.y, sources); // qmllint disable unqualified
    }

    function moveDragTo(point) {
        return testScenarioDriver.moveDrag(shell, point.x, point.y); // qmllint disable unqualified
    }

    function dropAt(point) {
        return testScenarioDriver.dropDrag(shell, point.x, point.y); // qmllint disable unqualified
    }

    function pollSource() {
        testScenarioDriver.pollSourceImport(); // qmllint disable unqualified
    }

    function completeSource(inspectedCount, acceptedCount, issueCount) {
        testScenarioDriver.enqueueSourceCompletion(inspectedCount, acceptedCount, issueCount); // qmllint disable unqualified
        pollSource();
    }

    function seedQueue(paths) {
        const center = Qt.point(shell.width / 2, shell.height / 2);
        compare(beginUrlDragAt(center, ["C:/Entrada"]), Qt.CopyAction);
        compare(dropAt(center), Qt.CopyAction);
        tryCompare(testLocalPlayerPresenter, "sourceImportState", PlayerPresenter.Discovering); // qmllint disable unqualified
        testScenarioDriver.enqueueSourceBatch(paths, 0, paths.length); // qmllint disable unqualified
        pollSource();
        tryCompare(findChild(shell, "queue-list"), "count", paths.length);
        completeSource(paths.length, paths.length, 0);
        tryCompare(testLocalPlayerPresenter, "sourceImportState", PlayerPresenter.Completed); // qmllint disable unqualified
        verify(testLocalPlayerPresenter.dismissSourceImportStatus()); // qmllint disable unqualified
    }

    function init() {
        testScenarioDriver.resetLocalScenario(); // qmllint disable unqualified
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.requestActivate();
        tryVerify(function () {
            return shell.active;
        });
    }

    function cleanup() {
        if (shell)
            testScenarioDriver.cancelDrag(shell); // qmllint disable unqualified
        if (shell)
            shell.destroy();
        shell = null;
    }

    function test_globalOverlayOffersCopyAndEmptyDropStartsImport() {
        const dropArea = findChild(shell, "source-drop-area");
        const overlay = findChild(shell, "source-drop-overlay");
        verify(dropArea);
        verify(overlay);
        verify(!overlay.visible);
        compare(dropArea.width, shell.width);
        compare(dropArea.height, shell.height);

        const firstCorner = Qt.point(2, 2);
        compare(beginUrlDragAt(firstCorner, ["C:/Música/Abertura.mp3", "C:/Música/Álbum"]), Qt.CopyAction);
        tryVerify(function () {
            return overlay.visible;
        });
        compare(dropArea.acceptedDropAction, Qt.CopyAction);

        const oppositeCorner = Qt.point(shell.width - 2, shell.height - 2);
        compare(moveDragTo(oppositeCorner), Qt.CopyAction);
        compare(dropArea.acceptedDropAction, Qt.CopyAction);
        compare(dropAt(oppositeCorner), Qt.CopyAction);

        tryCompare(testLocalPlayerPresenter, "sourceImportState", PlayerPresenter.Discovering); // qmllint disable unqualified
        compare(testScenarioDriver.sourceRequestCount(), 1); // qmllint disable unqualified
        compare(testScenarioDriver.sourceRequestRootCount(0), 2); // qmllint disable unqualified
        verify(testScenarioDriver.sourceRequestRoot(0, 0).endsWith("Abertura.mp3")); // qmllint disable unqualified
        verify(testScenarioDriver.sourceRequestRoot(0, 1).endsWith("Álbum")); // qmllint disable unqualified
        verify(!overlay.visible);
    }

    function test_queueDropCalculatesBeforeAfterAndEndAndPreservesFocusAndCurrent() {
        seedQueue(["C:/Música/Primeira.mp3", "C:/Música/Segunda.flac"]);
        keyClick(Qt.Key_L, Qt.ControlModifier);
        const queuePanel = findChild(shell, "queue-panel");
        const queueList = findChild(shell, "queue-list");
        const indicator = findChild(shell, "queue-drop-indicator");
        const dropArea = findChild(shell, "source-drop-area");
        const overlay = findChild(shell, "source-drop-overlay");
        verify(indicator);
        verify(dropArea);
        verify(overlay);
        compare(indicator.parent, dropArea);
        verify(indicator.z > overlay.z);
        tryVerify(function () {
            return queuePanel.visible && queueList.activeFocus && queueList.count === 2;
        });

        const currentId = testLocalPlayerPresenter.currentOccurrenceId; // qmllint disable unqualified
        const currentRowId = revealRow(queueList, testLocalPlayerPresenter.currentQueueIndex).occurrenceId; // qmllint disable unqualified
        compare(currentRowId, currentId);
        const firstRow = revealRow(queueList, 0);
        const beforeFirst = scenePoint(firstRow, firstRow.width / 2, 1);
        compare(beginUrlDragAt(beforeFirst, ["C:/Música/Inserida.wav"]), Qt.CopyAction);
        tryVerify(function () {
            return indicator.visible;
        });
        compare(dropArea.insertionIndex, 0);
        compare(indicator.insertionIndex, 0);

        const afterFirst = scenePoint(firstRow, firstRow.width / 2, firstRow.height - 1);
        compare(moveDragTo(afterFirst), Qt.CopyAction);
        compare(dropArea.insertionIndex, 1);
        compare(indicator.insertionIndex, 1);

        const endOfList = scenePoint(queueList, queueList.width / 2, queueList.height - 2);
        compare(moveDragTo(endOfList), Qt.CopyAction);
        compare(dropArea.insertionIndex, queueList.count);
        compare(indicator.insertionIndex, queueList.count);

        compare(moveDragTo(afterFirst), Qt.CopyAction);
        compare(dropAt(afterFirst), Qt.CopyAction);
        testScenarioDriver.enqueueSourceBatch(["C:/Música/Inserida.wav"], 0, 1); // qmllint disable unqualified
        pollSource();
        completeSource(1, 1, 0);

        tryCompare(queueList, "count", 3);
        compare(testLocalPlayerPresenter.currentOccurrenceId, currentId); // qmllint disable unqualified
        tryVerify(function () {
            return queueList.activeFocus && queueList.currentItem !== null && queueList.currentItem.occurrenceId === currentId;
        });
        compare(revealRow(queueList, 1).fileName, "Inserida.wav");
        verify(!indicator.visible);
    }

    function test_progressCancelAndDismissAreObservableAndAccessible() {
        shell.width = 720;
        shell.height = 520;
        const stateLayer = findChild(shell, "state-layer");
        tryVerify(function () {
            return stateLayer.width > 0 && stateLayer.height > 0;
        });
        const stateHeightBeforeImport = stateLayer.height;
        const center = Qt.point(shell.width / 2, shell.height / 2);
        compare(beginUrlDragAt(center, ["C:/Música/Discografia"]), Qt.CopyAction);
        compare(dropAt(center), Qt.CopyAction);

        const status = findChild(shell, "source-import-status");
        const progress = findChild(shell, "source-import-progress");
        const cancel = findChild(shell, "source-import-cancel");
        const dismiss = findChild(shell, "source-import-dismiss");
        verify(status);
        verify(progress);
        verify(cancel);
        verify(dismiss);
        tryVerify(function () {
            return status.visible && cancel.visible && cancel.enabled;
        });
        compare(cancel.Accessible.role, Accessible.Button);
        verify(!dismiss.visible);
        tryCompare(stateLayer, "height", stateHeightBeforeImport);

        testScenarioDriver.enqueueSourceBatch(["C:/Música/Encontrada.ogg"], 1, 2); // qmllint disable unqualified
        pollSource();
        tryCompare(testLocalPlayerPresenter, "sourceImportAcceptedCount", 1); // qmllint disable unqualified
        compare(testLocalPlayerPresenter.sourceImportIssueCount, 1); // qmllint disable unqualified
        compare(testLocalPlayerPresenter.sourceImportInspectedCount, 2); // qmllint disable unqualified
        verify(progress.text.includes("1"));
        verify(progress.text.includes("2"));

        cancel.forceActiveFocus(Qt.TabFocusReason);
        tryVerify(function () {
            return cancel.activeFocus;
        });
        cancel.Accessible.pressAction();
        tryCompare(testLocalPlayerPresenter, "sourceImportState", PlayerPresenter.Cancelling); // qmllint disable unqualified
        compare(testScenarioDriver.sourceCancelCount(), 1); // qmllint disable unqualified
        compare(testScenarioDriver.sourceCancelledGeneration(0), testScenarioDriver.sourceRequestGeneration(0)); // qmllint disable unqualified
        testScenarioDriver.enqueueSourceCancelled(); // qmllint disable unqualified
        pollSource();
        tryCompare(testLocalPlayerPresenter, "sourceImportState", PlayerPresenter.Cancelled); // qmllint disable unqualified
        tryVerify(function () {
            return status.visible && !cancel.visible && dismiss.visible && dismiss.enabled && dismiss.activeFocus;
        });
        compare(dismiss.Accessible.role, Accessible.Button);
        dismiss.Accessible.pressAction();
        tryCompare(testLocalPlayerPresenter, "sourceImportState", PlayerPresenter.Idle); // qmllint disable unqualified
        tryVerify(function () {
            return !status.visible;
        });
        tryVerify(function () {
            return findChild(shell, "player-queue").activeFocus;
        });
    }

    function test_terminalDoesNotStealFocusAfterCancelLosesItToPreferences() {
        const center = Qt.point(shell.width / 2, shell.height / 2);
        compare(beginUrlDragAt(center, ["C:/Música/Discografia"]), Qt.CopyAction);
        compare(dropAt(center), Qt.CopyAction);
        tryCompare(testLocalPlayerPresenter, "sourceImportState", PlayerPresenter.Discovering); // qmllint disable unqualified

        const cancel = findChild(shell, "source-import-cancel");
        const dismiss = findChild(shell, "source-import-dismiss");
        const preferencesButton = findChild(shell, "preferences");
        const preferencesPanel = findChild(shell, "preferences-panel");
        cancel.forceActiveFocus(Qt.TabFocusReason);
        tryVerify(function () {
            return cancel.activeFocus;
        });

        mouseClick(preferencesButton);
        tryVerify(function () {
            return preferencesPanel.opened && findChild(shell, "preferences-general").activeFocus;
        });
        completeSource(1, 0, 1);
        tryCompare(testLocalPlayerPresenter, "sourceImportState", PlayerPresenter.CompletedWithIssues); // qmllint disable unqualified
        wait(50);

        verify(preferencesPanel.opened);
        verify(findChild(shell, "preferences-general").activeFocus);
        verify(!dismiss.activeFocus, "A terminal import must not take focus back from an open modal");
    }

    function test_newImportDoesNotReuseFocusOwnershipFromPreviousTerminalState() {
        const center = Qt.point(shell.width / 2, shell.height / 2);
        compare(beginUrlDragAt(center, ["C:/Música/Primeira"]), Qt.CopyAction);
        compare(dropAt(center), Qt.CopyAction);
        const cancel = findChild(shell, "source-import-cancel");
        const dismiss = findChild(shell, "source-import-dismiss");
        const recover = findChild(shell, "source-import-recover");
        cancel.forceActiveFocus(Qt.TabFocusReason);
        tryVerify(function () {
            return cancel.activeFocus;
        });
        completeSource(1, 0, 1);
        tryVerify(function () {
            return recover.visible && recover.activeFocus;
        });

        compare(beginUrlDragAt(center, ["C:/Música/Segunda"]), Qt.CopyAction);
        compare(dropAt(center), Qt.CopyAction);
        tryCompare(testLocalPlayerPresenter, "sourceImportState", PlayerPresenter.Discovering); // qmllint disable unqualified
        const preferencesButton = findChild(shell, "preferences");
        preferencesButton.forceActiveFocus(Qt.TabFocusReason);
        tryVerify(function () {
            return preferencesButton.activeFocus;
        });

        completeSource(1, 0, 1);
        tryCompare(testLocalPlayerPresenter, "sourceImportState", PlayerPresenter.CompletedWithIssues); // qmllint disable unqualified
        wait(50);
        verify(preferencesButton.activeFocus, "A new import must not inherit the prior terminal focus latch");
        verify(!dismiss.activeFocus);
        verify(!recover.activeFocus);
    }

    function test_programmaticDismissDoesNotRestoreFocusAfterUserLeavesAction() {
        const center = Qt.point(shell.width / 2, shell.height / 2);
        compare(beginUrlDragAt(center, ["C:/Música/Discografia"]), Qt.CopyAction);
        compare(dropAt(center), Qt.CopyAction);
        const cancel = findChild(shell, "source-import-cancel");
        const recover = findChild(shell, "source-import-recover");
        cancel.forceActiveFocus(Qt.TabFocusReason);
        tryVerify(function () {
            return cancel.activeFocus;
        });
        completeSource(1, 0, 1);
        tryVerify(function () {
            return recover.visible && recover.activeFocus;
        });

        const preferencesButton = findChild(shell, "preferences");
        preferencesButton.forceActiveFocus(Qt.TabFocusReason);
        tryVerify(function () {
            return preferencesButton.activeFocus;
        });
        verify(testLocalPlayerPresenter.dismissSourceImportStatus()); // qmllint disable unqualified
        tryCompare(testLocalPlayerPresenter, "sourceImportState", PlayerPresenter.Idle); // qmllint disable unqualified
        wait(50);

        verify(preferencesButton.activeFocus, "A non-user dismissal must preserve the focus chosen after the action");
    }

    function test_remoteAndMissingUrlsAreRejectedWithoutDiscovery() {
        const center = Qt.point(shell.width / 2, shell.height / 2);
        compare(beginUrlDragAt(center, ["https://example.invalid/audio.mp3"]), Qt.CopyAction);
        compare(dropAt(center), Qt.IgnoreAction);
        tryCompare(testLocalPlayerPresenter, "sourceImportState", PlayerPresenter.Rejected); // qmllint disable unqualified
        compare(testLocalPlayerPresenter.sourceImportIssueCount, 1); // qmllint disable unqualified
        compare(testScenarioDriver.sourceRequestCount(), 0); // qmllint disable unqualified
        const status = findChild(shell, "source-import-status");
        const progress = findChild(shell, "source-import-progress");
        const advice = findChild(shell, "source-import-advice");
        const recover = findChild(shell, "source-import-recover");
        verify(status.visible);
        compare(progress.text, "Adicionados: 0 · Examinados: 0 · Ignorados: 1");
        verify(advice.visible);
        verify(advice.text.includes("arquivos ou pastas locais"));
        verify(recover.visible && recover.enabled);
        compare(recover.Accessible.name, "Escolher outros arquivos");
        compare(testScenarioDriver.lastAccessibleAnnouncementPoliteness(), Accessible.Assertive); // qmllint disable unqualified

        testScenarioDriver.enqueueLocalSelection(["C:/Música/Recuperada.mp3"]); // qmllint disable unqualified
        recover.Accessible.pressAction();
        tryCompare(testLocalPlayerPresenter, "sourceImportState", PlayerPresenter.Idle); // qmllint disable unqualified
        tryCompare(findChild(shell, "queue-list"), "count", 1);
        compare(testLocalPlayerPresenter.currentTitle, "Recuperada"); // qmllint disable unqualified

        compare(testScenarioDriver.beginTextDrag(shell, center.x, center.y, "sem URLs"), Qt.IgnoreAction); // qmllint disable unqualified
        verify(!findChild(shell, "source-drop-overlay").visible);
        compare(dropAt(center), Qt.IgnoreAction);
        compare(testLocalPlayerPresenter.sourceImportState, PlayerPresenter.Idle); // qmllint disable unqualified
        compare(testScenarioDriver.sourceRequestCount(), 0); // qmllint disable unqualified
    }

    function test_partialFailureKeepsValidFilesAndAnnouncesCompletionOnce() {
        const center = Qt.point(shell.width / 2, shell.height / 2);
        testScenarioDriver.clearAccessibleAnnouncements(); // qmllint disable unqualified
        compare(beginUrlDragAt(center, ["C:/Música/Mista", "https://example.invalid/remota.ogg"]), Qt.CopyAction);
        compare(dropAt(center), Qt.CopyAction);
        compare(testLocalPlayerPresenter.sourceImportIssueCount, 1); // qmllint disable unqualified
        compare(testScenarioDriver.sourceRequestRootCount(0), 1); // qmllint disable unqualified

        testScenarioDriver.enqueueSourceBatch(["C:/Música/Válida 1.mp3", "C:/Música/Válida 2.flac"], 1, 3); // qmllint disable unqualified
        pollSource();
        tryCompare(findChild(shell, "queue-list"), "count", 2);
        compare(testLocalPlayerPresenter.sourceImportAcceptedCount, 2); // qmllint disable unqualified
        compare(testLocalPlayerPresenter.sourceImportIssueCount, 2); // qmllint disable unqualified
        compare(testScenarioDriver.accessibleAnnouncementCount(), 0); // qmllint disable unqualified

        testScenarioDriver.enqueueSourceBatch([], 1, 1); // qmllint disable unqualified
        pollSource();
        compare(testLocalPlayerPresenter.sourceImportIssueCount, 3); // qmllint disable unqualified
        compare(testScenarioDriver.accessibleAnnouncementCount(), 0); // qmllint disable unqualified
        // The discovery summary reports only worker-side issues. The presenter
        // adds the rejected remote URL counted at its QUrl boundary.
        completeSource(4, 2, 2);
        tryCompare(testLocalPlayerPresenter, "sourceImportState", PlayerPresenter.CompletedWithIssues); // qmllint disable unqualified

        const status = findChild(shell, "source-import-status");
        const progress = findChild(shell, "source-import-progress");
        const advice = findChild(shell, "source-import-advice");
        const recover = findChild(shell, "source-import-recover");
        tryVerify(function () {
            return status.visible && status.accessibilityAnnouncement.length > 0;
        });
        compare(progress.text, "Adicionados: 2 · Examinados: 4 · Ignorados: 3");
        verify(advice.visible && advice.text.includes("não puderam ser lidos"));
        verify(recover.visible && recover.enabled);
        compare(testScenarioDriver.accessibleAnnouncementCount(), 1); // qmllint disable unqualified
        compare(testScenarioDriver.lastAccessibleAnnouncement(), status.accessibilityAnnouncement); // qmllint disable unqualified
        compare(testScenarioDriver.lastAccessibleAnnouncementPoliteness(), Accessible.Assertive); // qmllint disable unqualified
        verify(status.accessibilityAnnouncement.includes("2"));
        verify(status.accessibilityAnnouncement.includes("3"));
    }

    function test_highContrastDropFeedbackUsesOpaqueSystemPairs() {
        const dropArea = findChild(shell, "source-drop-area");
        const overlay = findChild(shell, "source-drop-overlay");
        const prompt = findChild(shell, "source-drop-prompt");
        verify(dropArea);
        verify(overlay);
        verify(prompt);

        const verifyPalette = function (light) {
            testScenarioDriver.setHighContrast(true, light); // qmllint disable unqualified
            const center = Qt.point(shell.width / 2, shell.height / 2);
            compare(beginUrlDragAt(center, ["C:/Música/Contraste.mp3"]), Qt.CopyAction);
            tryVerify(function () {
                return overlay.visible;
            });
            compare(overlay.color.a, 0);
            compare(prompt.color.toString(), testShellUiContract.surfaceRaisedColor); // qmllint disable unqualified
            compare(prompt.opacity, 1);
            compare(prompt.border.color.toString(), testShellUiContract.focusColor); // qmllint disable unqualified
            testScenarioDriver.cancelDrag(shell); // qmllint disable unqualified
            tryVerify(function () {
                return !overlay.visible;
            });
        };

        verifyPalette(false);
        verifyPalette(true);
        testScenarioDriver.setHighContrast(false, false); // qmllint disable unqualified
    }
}
