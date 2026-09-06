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
        const skipBackward = findChild(shell, "skip-backward");
        const skipForward = findChild(shell, "skip-forward");
        compare(progress.Accessible.description, "Abra uma faixa para alterar a posição.");
        compare(skipBackward.Accessible.description, "Abra uma faixa para alterar a posição.");
        compare(skipForward.Accessible.description, "Abra uma faixa para alterar a posição.");
        compare(skipBackward.toolTipText, "Voltar 5 segundos — Abra uma faixa para alterar a posição.");
        compare(skipForward.toolTipText, "Avançar 5 segundos — Abra uma faixa para alterar a posição.");

        openLocalQueue(["C:/Música/Primeira faixa.mp3", "C:/Música/Segunda faixa.mp3"]);

        const expected = {
            "skip-backward": "←",
            "play-pause": "Espaço",
            "skip-forward": "→",
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

        driver.publishLocalUnseekablePlaying();
        tryCompare(presenter, "seekable", false);
        const progressToolTipArea = findChild(shell, "progress-tooltip-area");
        verify(progressToolTipArea);
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
        compare(progress.Accessible.description, "A posição não pode ser alterada no estado atual.");
        compare(skipBackward.Accessible.description, "A posição não pode ser alterada no estado atual.");
        compare(skipForward.Accessible.description, "A posição não pode ser alterada no estado atual.");
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
