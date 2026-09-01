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
}
