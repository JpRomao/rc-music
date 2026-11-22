import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "KeyboardFullJourney"
    when: windowShown

    readonly property PlayerPresenter presenter: testLocalPlayerPresenter // qmllint disable unqualified
    readonly property var driver: testScenarioDriver // qmllint disable unqualified
    property var shell

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testCase.presenter
        }
    }

    function init() {
        driver.resetLocalScenario();
    }

    function cleanup() {
        if (shell) {
            shell.destroy();
            shell = null;
            wait(0);
        }
        driver.resetLocalScenario();
    }

    function createActiveShell() {
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.requestActivate();
        tryVerify(function () {
            return shell.active;
        });
    }

    function openLocalQueue(paths) {
        driver.enqueueLocalSelection(paths);
        const openAudio = findChild(shell, "open-audio");
        verify(openAudio);
        openAudio.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        tryVerify(function () {
            return presenter.queueLoaded;
        });
        driver.publishLocalReady();
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
    }

    function tabUntil(item, backwards) {
        for (let attempts = 0; attempts < 24 && !item.activeFocus; ++attempts)
            keyClick(Qt.Key_Tab, backwards ? Qt.ShiftModifier : Qt.NoModifier);
        verify(item.activeFocus, "Keyboard focus did not reach " + item.objectName);
    }

    function test_transportShortcutsAndDisabledSeekAreExplained() {
        createActiveShell();

        const progress = findChild(shell, "progress");
        const progressToolTipArea = findChild(shell, "progress-tooltip-area");
        verify(progressToolTipArea);
        // Player C: the ±5 s buttons leave the player; ←/→ and the progress bar keep the jump.
        verify(!findChild(shell, "skip-backward").visible);
        verify(!findChild(shell, "skip-forward").visible);
        compare(progress.Accessible.description, "Abra uma faixa para alterar a posição.");
        compare(progressToolTipArea.toolTipText, "Posição da faixa — Abra uma faixa para alterar a posição.");

        openLocalQueue(["C:/Música/Primeira faixa.mp3", "C:/Música/Segunda faixa.mp3"]);

        const expected = {
            "play-pause": "Espaço",
            "next": "Shift+→ (ou Ctrl+→)",
            "mute": "M"
        };
        for (const objectName in expected) {
            const button = findChild(shell, objectName);
            verify(button.enabled, objectName + " must be enabled for the shortcut check");
            compare(button.Accessible.description, expected[objectName]);
            compare(button.toolTipText, button.Accessible.name + " (" + expected[objectName] + ")");
        }
        compare(progress.Accessible.description, "←/→: saltos configurados; ↑/↓: volume; Page Up/Page Down: 10 segundos; Home/End: início/fim.");
        compare(progressToolTipArea.toolTipText, "Posição da faixa (←/→: saltos configurados; ↑/↓: volume; Page Up/Page Down: 10 segundos; Home/End: início/fim.)");

        // ← and → still move the position by the configured jumps.
        driver.publishLocalPosition(20000);
        tryCompare(presenter, "positionMs", 20000);
        findChild(shell, "play-pause").forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Right);
        const forwardTarget = 20000 + presenter.skipForwardSeconds * 1000;
        compare(driver.localSeekTarget(), forwardTarget);
        driver.publishLocalPosition(forwardTarget);
        tryCompare(presenter, "positionMs", forwardTarget);
        tryCompare(presenter, "seekPending", false);
        keyClick(Qt.Key_Left);
        const backwardTarget = forwardTarget - presenter.skipBackwardSeconds * 1000;
        compare(driver.localSeekTarget(), backwardTarget);
        driver.publishLocalPosition(backwardTarget);
        tryCompare(presenter, "positionMs", backwardTarget);
        tryCompare(presenter, "seekPending", false);

        driver.publishLocalUnseekablePlaying();
        tryCompare(presenter, "seekable", false);
        verify(!progress.enabled);
        compare(progress.Accessible.description, "Esta fonte não permite avançar ou voltar.");
        compare(progressToolTipArea.toolTipText, "Posição da faixa — Esta fonte não permite avançar ou voltar.");
        mouseMove(progressToolTipArea, progressToolTipArea.width / 2, progressToolTipArea.height / 2);
        tryVerify(function () {
            return progressToolTipArea.toolTipVisible;
        });
        driver.publishLocalPlaying();
        tryCompare(presenter, "seekable", true);

        const next = findChild(shell, "next");
        next.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        driver.publishLocalReady();
        driver.publishLocalPlaying();
        tryCompare(presenter, "currentQueueIndex", 1);
        const previous = findChild(shell, "previous");
        compare(previous.Accessible.description, "Shift+← (ou Ctrl+←)");
        compare(previous.toolTipText, "Faixa anterior (Shift+← (ou Ctrl+←))");

        driver.publishLocalError();
        tryCompare(presenter, "playbackState", PlayerPresenter.Error);
        compare(progress.Accessible.description, "Não é possível mudar a posição agora.");
        compare(progressToolTipArea.toolTipText, "Posição da faixa — Não é possível mudar a posição agora.");
    }

    function test_openNavigateAndClearQueueWithoutLeavingKeyboardOrDialog() {
        createActiveShell();
        openLocalQueue(["C:/Música/Um.mp3", "C:/Música/Dois.mp3", "C:/Música/Três.mp3"]);

        keyClick(Qt.Key_L, Qt.ControlModifier);
        const queuePanel = findChild(shell, "queue-panel");
        const queueList = findChild(shell, "queue-list");
        tryVerify(function () {
            return queuePanel.visible && queueList.activeFocus && queueList.count === 3;
        });

        keyClick(Qt.Key_Down, Qt.ControlModifier);
        compare(queueList.currentIndex, 1);
        keyClick(Qt.Key_Return);
        tryCompare(presenter, "currentQueueIndex", 1);
        driver.publishLocalReady();
        driver.publishLocalPlaying();
        tryVerify(function () {
            return queueList.activeFocus;
        });

        const queueClear = findChild(shell, "queue-clear");
        tabUntil(queueClear, false);
        keyClick(Qt.Key_Return);

        const confirmation = findChild(shell, "queue-clear-confirmation");
        const cancel = findChild(shell, "queue-clear-cancel");
        const confirm = findChild(shell, "queue-clear-confirm");
        tryVerify(function () {
            return confirmation.opened && cancel.activeFocus;
        });

        keyClick(Qt.Key_Tab);
        tryVerify(function () {
            return confirmation.opened && confirm.activeFocus;
        });
        keyClick(Qt.Key_Tab);
        tryVerify(function () {
            return confirmation.opened && cancel.activeFocus;
        });
        keyClick(Qt.Key_Tab, Qt.ShiftModifier);
        tryVerify(function () {
            return confirmation.opened && confirm.activeFocus;
        });
        keyClick(Qt.Key_Tab, Qt.ShiftModifier);
        tryVerify(function () {
            return confirmation.opened && cancel.activeFocus;
        });

        keyClick(Qt.Key_Tab);
        tryVerify(function () {
            return confirm.activeFocus;
        });
        keyClick(Qt.Key_Return);
        tryVerify(function () {
            return !confirmation.visible && !presenter.queueLoaded;
        });
        tryVerify(function () {
            return findChild(shell, "open-audio").activeFocus;
        });
    }
}
