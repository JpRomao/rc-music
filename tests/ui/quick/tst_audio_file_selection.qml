import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "AudioFileSelection"
    when: windowShown
    readonly property PlayerPresenter presenter: testLocalPlayerPresenter // qmllint disable unqualified
    readonly property var driver: testScenarioDriver // qmllint disable unqualified
    property var shell

    SignalSpy {
        id: finishedSpy
        target: testCase.presenter
        signalName: "audioFileSelectionFinished"
    }

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testCase.presenter
        }
    }

    Component {
        id: otherWindowComponent
        Window {
            width: 300
            height: 200
            visible: true
        }
    }

    function init() {
        driver.resetLocalScenario();
        driver.deferLocalAudioSelection(true);
        finishedSpy.clear();
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

    function beginOpenFromEmpty() {
        const opener = findChild(shell, "open-audio");
        verify(opener && opener.visible && opener.enabled);
        opener.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        tryCompare(presenter, "selectingAudioFiles", true);
        return opener;
    }

    function selectInitialTrack() {
        beginOpenFromEmpty();
        driver.completeLocalAudioSelection(["C:/Música/Em reprodução 日本語.mp3"]);
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalReady();
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
    }

    function test_pendingSelectionBlocksDuplicateOpeningAndCancellationIsNeutral() {
        const opener = beginOpenFromEmpty();
        const status = findChild(shell, "audio-file-selection-status");
        verify(status && status.visible);
        compare(status.text, "Aguardando seleção de arquivos…");
        compare(status.Accessible.name, status.text);
        verify(!opener.enabled);
        verify(!shell.windowShortcutsEnabled);
        compare(driver.localAudioSelectionCount(), 1);
        opener.Accessible.pressAction();
        keyClick(Qt.Key_O, Qt.ControlModifier);
        compare(driver.localAudioSelectionCount(), 1);
        compare(driver.localLoadCount(), 0);

        driver.cancelLocalAudioSelection();
        tryCompare(presenter, "selectingAudioFiles", false);
        verify(!presenter.audioFileSelectionFailed);
        verify(!presenter.queueLoaded);
        verify(!status.visible);
        tryVerify(function () {
            return opener.enabled && opener.activeFocus;
        });
        compare(driver.localLoadCount(), 0);
    }

    function test_failedSelectionKeepsContextAndTheOriginalActionRetriesIt() {
        shell.width = 720;
        shell.height = 520;
        const opener = beginOpenFromEmpty();
        driver.failLocalAudioSelection();
        tryCompare(presenter, "audioFileSelectionFailed", true);
        const status = findChild(shell, "audio-file-selection-status");
        verify(status && status.visible);
        compare(status.text, "Não foi possível abrir o seletor. Tente abrir ou adicionar arquivos novamente.");
        verify(!presenter.queueLoaded);
        tryVerify(function () {
            return opener.enabled && opener.activeFocus;
        });
        const transport = findChild(shell, "transport-bar");
        tryVerify(function () {
            const actionBottom = opener.mapToItem(shell.contentItem, 0, opener.height).y;
            const statusTop = status.mapToItem(shell.contentItem, 0, 0).y;
            const statusBottom = status.mapToItem(shell.contentItem, 0, status.height).y;
            return actionBottom <= statusTop && statusBottom <= transport.mapToItem(shell.contentItem, 0, 0).y;
        }, 5000, "The selection failure and its action fit above transport at 720 × 520");
        keyClick(Qt.Key_Return);
        tryCompare(presenter, "selectingAudioFiles", true);
        compare(driver.localAudioSelectionCount(), 2);
        driver.completeLocalAudioSelection(["C:/Música/Selecionada após falha ☕.flac"]);
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        verify(!presenter.audioFileSelectionFailed);
        verify(!status.visible);
        compare(driver.localLoadCount(), 1);
        compare(driver.localLoadedPath(0), "C:/Música/Selecionada após falha ☕.flac");
    }

    function test_pendingSelectionKeepsBackendUpdatesAndDoesNotEnableWindowShortcuts() {
        selectInitialTrack();
        const occurrenceId = presenter.currentOccurrenceId;
        const opener = findChild(shell, "open-audio");
        opener.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        tryCompare(presenter, "selectingAudioFiles", true);
        compare(presenter.currentOccurrenceId, occurrenceId);
        compare(driver.localLoadCount(), 1);
        driver.publishLocalPaused();
        tryCompare(presenter, "playbackState", PlayerPresenter.Paused);
        verify(presenter.selectingAudioFiles);
        keyClick(Qt.Key_L, Qt.ControlModifier);
        verify(!shell.queueOpen);
        keyClick(Qt.Key_Q, Qt.ControlModifier);
        verify(!presenter.exiting);
        driver.cancelLocalAudioSelection();
        tryCompare(presenter, "selectingAudioFiles", false);
        compare(presenter.currentOccurrenceId, occurrenceId);
        compare(presenter.playbackState, PlayerPresenter.Paused);
        tryVerify(function () {
            return opener.activeFocus;
        });
    }

    function test_addSelectionRestoresTheQueueOpenerWithoutReplacingPlayback() {
        selectInitialTrack();
        const occurrenceId = presenter.currentOccurrenceId;
        shell.setQueueOpen(true);
        const queue = findChild(shell, "queue-list");
        tryVerify(function () {
            return queue.visible && queue.activeFocus;
        });
        const add = findChild(shell, "queue-add");
        add.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        tryCompare(presenter, "selectingAudioFiles", true);
        verify(!add.enabled);
        driver.completeLocalAudioSelection(["C:/Música/Adicionada.wav"]);
        tryCompare(presenter, "selectingAudioFiles", false);
        tryCompare(queue, "count", 2);
        compare(presenter.currentOccurrenceId, occurrenceId);
        compare(presenter.playbackState, PlayerPresenter.Playing);
        compare(driver.localLoadCount(), 1);
        tryVerify(function () {
            return add.activeFocus && add.enabled;
        });
    }

    function test_cancelDoesNotStealFocusFromAnotherActiveWindow() {
        beginOpenFromEmpty();
        const otherWindow = createTemporaryObject(otherWindowComponent, null);
        verify(otherWindow);
        otherWindow.requestActivate();
        tryVerify(function () {
            return otherWindow.active && !shell.active;
        });
        driver.cancelLocalAudioSelection();
        tryCompare(presenter, "selectingAudioFiles", false);
        verify(otherWindow.active);
        verify(!shell.active);
    }

    function test_cancellationDoesNotStealFocusFromANewerModal() {
        beginOpenFromEmpty();
        shell.openPreferences();
        const preferences = findChild(shell, "preferences-panel");
        const control = findChild(shell, "preferences-general");
        tryVerify(function () {
            return preferences.opened && control.activeFocus;
        });
        driver.cancelLocalAudioSelection();
        tryCompare(presenter, "selectingAudioFiles", false);
        verify(preferences.opened && control.activeFocus);
    }

    function test_importRecoveryCancellationRestoresFocusAfterTheOpenerDisappears() {
        const center = Qt.point(shell.width / 2, shell.height / 2);
        compare(driver.beginUrlDrag(shell, center.x, center.y, ["https://example.invalid/audio.mp3"]), Qt.CopyAction);
        compare(driver.dropDrag(shell, center.x, center.y), Qt.IgnoreAction);
        tryCompare(presenter, "sourceImportState", PlayerPresenter.Rejected);
        const recover = findChild(shell, "source-import-recover");
        verify(recover.visible && recover.enabled);
        recover.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        tryCompare(presenter, "selectingAudioFiles", true);
        compare(presenter.sourceImportState, PlayerPresenter.Idle);
        verify(!recover.visible);

        driver.cancelLocalAudioSelection();
        tryCompare(presenter, "selectingAudioFiles", false);
        verify(!presenter.queueLoaded && !presenter.audioFileSelectionFailed);
        const fallback = findChild(shell, "open-audio");
        tryVerify(function () {
            return fallback.visible && fallback.enabled && fallback.activeFocus;
        }, 5000, "Dismissed import recovery must restore a visible primary action after cancelling the picker");
    }

    function test_rebindWithoutCompletionDoesNotReuseTheOldOpener_data() {
        return [
            {
                tag: "later-event",
                drain: true
            },
            {
                tag: "same-event",
                drain: false
            }
        ];
    }

    function test_rebindWithoutCompletionDoesNotReuseTheOldOpener(data) {
        beginOpenFromEmpty();
        driver.rebindLocalAudioPicker();
        tryCompare(presenter, "selectingAudioFiles", false);
        if (data.drain)
            wait(0);
        compare(finishedSpy.count, 0);
        const preferences = findChild(shell, "preferences");
        preferences.forceActiveFocus(Qt.TabFocusReason);
        verify(preferences.activeFocus);

        verify(presenter.openAudio());
        tryCompare(presenter, "selectingAudioFiles", true);
        driver.cancelLocalAudioSelection();
        tryCompare(presenter, "selectingAudioFiles", false);
        tryCompare(finishedSpy, "count", 1);
        wait(0);
        verify(preferences.activeFocus, "A cancelled old request must not own the focus of a later external request");
    }

    function test_deferredQueueOpeningDoesNotMoveFocusIntoThePendingPickerOwner() {
        selectInitialTrack();
        shell.setQueueOpen(true);
        const opener = findChild(shell, "queue-add");
        verify(opener.visible && opener.enabled);
        opener.forceActiveFocus(Qt.TabFocusReason);
        opener.Accessible.pressAction();
        tryCompare(presenter, "selectingAudioFiles", true);
        wait(0);
        verify(!findChild(shell, "queue-list").activeFocus, "A queued focus transfer must recheck whether a file picker has opened");
        driver.cancelLocalAudioSelection();
        tryCompare(presenter, "selectingAudioFiles", false);
        tryVerify(function () {
            return opener.activeFocus;
        });
    }

    function test_rebindWithoutCompletionReleasesQueuePlaybackSelection() {
        beginOpenFromEmpty();
        driver.completeLocalAudioSelection(["C:/Música/Primeira.mp3", "C:/Música/Segunda.mp3"]);
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        shell.setQueueOpen(true);
        const queue = findChild(shell, "queue-list");
        tryVerify(function () {
            return queue.visible && queue.activeFocus && queue.currentItem !== null;
        });
        finishedSpy.clear();
        const add = findChild(shell, "queue-add");
        add.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        tryCompare(presenter, "selectingAudioFiles", true);
        driver.rebindLocalAudioPicker();
        tryCompare(presenter, "selectingAudioFiles", false);
        wait(0);
        compare(finishedSpy.count, 0);

        verify(presenter.nextTrack());
        tryCompare(presenter, "currentQueueIndex", 1);
        tryVerify(function () {
            return queue.currentItem && queue.currentItem.occurrenceId === presenter.currentOccurrenceId;
        }, 5000, "Cancellation without Finished must not leave queue synchronization locked");
    }

    function test_addKeepsTheSelectedOccurrenceWhenPlaybackAdvancesWhileThePickerIsOpen() {
        beginOpenFromEmpty();
        driver.completeLocalAudioSelection(["C:/Música/Primeira.mp3", "C:/Música/Segunda.mp3"]);
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalReady();
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        shell.setQueueOpen(true);
        const queue = findChild(shell, "queue-list");
        tryVerify(function () {
            return queue.visible && queue.activeFocus && queue.currentItem !== null;
        });
        const selectedId = queue.currentItem.occurrenceId;
        const add = findChild(shell, "queue-add");
        add.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        tryCompare(presenter, "selectingAudioFiles", true);

        verify(presenter.nextTrack());
        tryCompare(presenter, "currentQueueIndex", 1);
        driver.publishLocalReady();
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        verify(presenter.currentOccurrenceId !== selectedId);
        driver.completeLocalAudioSelection(["C:/Música/Terceira.wav"]);
        tryCompare(presenter, "selectingAudioFiles", false);
        tryCompare(queue, "count", 3);
        compare(queue.currentItem.occurrenceId, selectedId);
        compare(presenter.currentQueueIndex, 1);
        compare(driver.localLoadCount(), 2);
        tryVerify(function () {
            return add.activeFocus;
        });
    }
}
