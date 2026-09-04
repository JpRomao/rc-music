import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "QueueMicrocopy"
    when: windowShown

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testLocalPlayerPresenter // qmllint disable unqualified
        }
    }

    property var shell

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
            shell.destroy();
        shell = null;
        testScenarioDriver.resetLocalScenario(); // qmllint disable unqualified
    }

    function test_unavailableRowsExposeLocalizedReasonWithoutInternalIdentifiers() {
        testScenarioDriver.enqueueLocalSelection(["C:/Música/Inválida 日本語.mp3"]); // qmllint disable unqualified
        findChild(shell, "open-audio").Accessible.pressAction();
        tryVerify(function () {
            return testLocalPlayerPresenter.queueLoaded; // qmllint disable unqualified
        });
        testScenarioDriver.publishLocalError(); // qmllint disable unqualified
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Error); // qmllint disable unqualified

        keyClick(Qt.Key_L, Qt.ControlModifier);
        const queue = findChild(shell, "queue-list");
        tryVerify(function () {
            return queue.visible && queue.count === 1 && queue.currentItem !== null && !queue.currentItem.available;
        });
        const row = queue.currentItem;
        const status = findChild(row, "queue-row-status");
        const internalId = row.occurrenceId;

        compare(row.Accessible.name, "Inválida 日本語. Não informado. posição 1 de 1. Arquivo inválido ou não suportado.");
        compare(row.Accessible.description, "");
        compare(status.text, "Arquivo inválido ou não suportado.");
        verify(!row.Accessible.name.includes(internalId));
        verify(!row.Accessible.name.includes("invalid_media"));
        verify(!row.Accessible.description.includes("invalid_media"));
    }

    function openQueueWithUnavailableOccurrence() {
        testScenarioDriver.enqueueLocalSelection(["C:/Música/Tentar novamente 日本語.mp3"]); // qmllint disable unqualified
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Loading); // qmllint disable unqualified
        testScenarioDriver.publishLocalError(); // qmllint disable unqualified
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Error); // qmllint disable unqualified
        shell.setQueueOpen(true);
        const queue = findChild(shell, "queue-list");
        tryVerify(function () {
            return queue.visible && queue.activeFocus && queue.currentItem !== null && !queue.currentItem.available;
        });
        return queue;
    }

    function test_retryMenu_data() {
        return [
            {
                tag: "keyboard",
                accessible: false
            },
            {
                tag: "accessible",
                accessible: true
            }
        ];
    }

    function test_retryMenu(data) {
        const queue = openQueueWithUnavailableOccurrence();
        const occurrenceId = queue.currentItem.occurrenceId;
        keyClick(Qt.Key_F10, Qt.ShiftModifier);
        const menu = findChild(shell, "queue-row-menu");
        tryVerify(function () {
            return menu.opened;
        });
        const retry = findChild(shell, "queue-menu-retry");
        verify(retry && retry.visible && retry.enabled);
        compare(retry.text, "Tentar reproduzir novamente");
        compare(retry.Accessible.name, "Tentar reproduzir novamente");
        compare(retry.Accessible.role, Accessible.MenuItem);
        verify(retry.Accessible.description.length > 0);
        verify(!findChild(shell, "queue-menu-play-now").enabled);
        if (data.accessible)
            retry.Accessible.pressAction();
        else {
            if (!retry.activeFocus)
                keyClick(Qt.Key_Down);
            tryVerify(function () {
                return retry.activeFocus;
            });
            keyClick(Qt.Key_Return);
        }
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Loading); // qmllint disable unqualified
        compare(testLocalPlayerPresenter.currentOccurrenceId, occurrenceId); // qmllint disable unqualified
        compare(testScenarioDriver.localLoadCount(), 2); // qmllint disable unqualified
        tryVerify(function () {
            return !menu.visible && queue.activeFocus;
        });
        const status = findChild(queue.currentItem, "queue-row-status");
        verify(status.visible);
        compare(status.text, "Tentando reproduzir novamente…");
        verify(queue.currentItem.Accessible.name.includes(status.text));
        verify(!queue.currentItem.Accessible.name.includes(occurrenceId));

        keyClick(Qt.Key_Menu);
        tryVerify(function () {
            return menu.opened;
        });
        verify(retry.visible && !retry.enabled);
        retry.Accessible.pressAction();
        compare(testScenarioDriver.localLoadCount(), 2); // qmllint disable unqualified
        keyClick(Qt.Key_Escape);

        testScenarioDriver.publishLocalReady(); // qmllint disable unqualified
        tryVerify(function () {
            return queue.currentItem.available && !status.visible;
        });
        queue.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Menu);
        tryVerify(function () {
            return menu.opened;
        });
        verify(!retry.visible);
        verify(findChild(shell, "queue-menu-play-now").enabled);
        keyClick(Qt.Key_Escape);
    }

    function test_retryShowsProgressOnlyOnTheSelectedUnavailableDuplicate() {
        testScenarioDriver.enqueueLocalSelection(["C:/Música/Duplicada.mp3", "C:/Música/Duplicada.mp3"]); // qmllint disable unqualified
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Loading); // qmllint disable unqualified
        const firstId = testLocalPlayerPresenter.currentOccurrenceId; // qmllint disable unqualified
        testScenarioDriver.publishLocalError(); // qmllint disable unqualified
        tryVerify(function () {
            return testLocalPlayerPresenter.currentOccurrenceId !== firstId; // qmllint disable unqualified
        });
        testScenarioDriver.publishLocalReady(); // qmllint disable unqualified
        testScenarioDriver.publishLocalPlaying(); // qmllint disable unqualified
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Playing); // qmllint disable unqualified
        const duplicateId = testLocalPlayerPresenter.currentOccurrenceId; // qmllint disable unqualified
        shell.setQueueOpen(true);
        const queue = findChild(shell, "queue-list");
        tryVerify(function () {
            return queue.visible && queue.activeFocus && queue.count === 2;
        });
        queue.currentIndex = 0;
        queue.forceLayout();
        tryVerify(function () {
            return queue.currentItem !== null && queue.currentItem.occurrenceId === firstId;
        });
        keyClick(Qt.Key_Menu);
        const menu = findChild(shell, "queue-row-menu");
        tryVerify(function () {
            return menu.opened;
        });
        const retry = findChild(shell, "queue-menu-retry");
        verify(retry && retry.enabled);
        retry.Accessible.pressAction();
        tryCompare(testLocalPlayerPresenter, "retryingOccurrenceId", firstId); // qmllint disable unqualified
        compare(testScenarioDriver.localLoadCount(), 3); // qmllint disable unqualified
        compare(queue.count, 2);
        const first = queue.itemAtIndex(0);
        const duplicate = queue.itemAtIndex(1);
        verify(first && duplicate);
        compare(first.occurrenceId, firstId);
        compare(duplicate.occurrenceId, duplicateId);
        compare(findChild(first, "queue-row-status").text, "Tentando reproduzir novamente…");
        verify(findChild(first, "queue-row-status").visible);
        verify(!findChild(duplicate, "queue-row-status").visible);
        verify(duplicate.available);
        verify(!duplicate.Accessible.name.includes("Tentando reproduzir novamente"));
        testScenarioDriver.publishLocalReady(); // qmllint disable unqualified
        tryVerify(function () {
            return first.available && duplicate.available;
        });
    }
}
