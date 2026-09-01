import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "AccessibleInterfaceTree"
    when: windowShown

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testLocalPlayerPresenter // qmllint disable unqualified
        }
    }

    property var shell

    function snapshot(item) {
        const result = testScenarioDriver.accessibleSnapshot(item); // qmllint disable unqualified
        verify(result.valid, item ? item.objectName : "null item");
        verify(result.parentValid, item.objectName + " must belong to the native accessibility tree");
        return result;
    }

    function verifyControl(item, role, name, focusable) {
        const result = snapshot(item);
        compare(result.role, role);
        compare(result.name, name);
        compare(result.focusable, focusable);
        verify(!result.name.includes("queue-"));
        verify(!result.name.includes("invalid_media"));
        return result;
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
            shell.destroy();
        shell = null;
        testScenarioDriver.resetLocalScenario(); // qmllint disable unqualified
    }

    function test_publicControlsExistInTheNativeAccessibleTree() {
        verifyControl(findChild(shell, "open-audio"), Accessible.Button, "Abrir áudio", true);
        verifyControl(findChild(shell, "preferences"), Accessible.Button, "Preferências", true);

        testScenarioDriver.enqueueLocalSelection(["C:/Música/Árvore acessível 日本語.mp3"]); // qmllint disable unqualified
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Loading); // qmllint disable unqualified
        testScenarioDriver.publishLocalPlaying(); // qmllint disable unqualified
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Playing); // qmllint disable unqualified

        verifyControl(findChild(shell, "progress"), Accessible.Slider, "Posição da faixa", true);
        verifyControl(findChild(shell, "volume"), Accessible.Slider, "Volume", true);

        keyClick(Qt.Key_L, Qt.ControlModifier);
        const list = findChild(shell, "queue-list");
        tryVerify(function () {
            return list.visible && list.currentItem !== null;
        });
        const listSnapshot = verifyControl(list, Accessible.List, "Fila", true);
        verify(listSnapshot.childCount > 0);

        const row = list.currentItem;
        const rowSnapshot = verifyControl(row, Accessible.ListItem, "Árvore acessível 日本語. Não informado. posição 1 de 1.", true);
        compare(rowSnapshot.description, "");
        compare(rowSnapshot.parentRole, Accessible.List);
    }
}
