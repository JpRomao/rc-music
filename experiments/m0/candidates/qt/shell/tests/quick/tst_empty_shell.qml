import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "TypedEmptyShell"
    when: windowShown

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
        }
    }
    property var shell
    SignalSpy {
        id: openSpy
        target: testCase.shell
        signalName: "openAudioRequested"
    }

    function initTestCase() {
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        compare(shell.width, 960);
        compare(shell.height, 640);
        compare(shell.minimumWidth, 720);
        compare(shell.minimumHeight, 520);
    }

    function test_emptyContentAndKeyboardAction() {
        const title = findChild(shell, "empty-title");
        const hint = findChild(shell, "empty-hint");
        const button = findChild(shell, "open-audio");
        const image = findChild(shell, "cover-placeholder");
        compare(title.text, testShellUiContract.emptyTitle); // qmllint disable unqualified
        compare(hint.text, testShellUiContract.emptyHint); // qmllint disable unqualified
        compare(button.text, testShellUiContract.openAudioText); // qmllint disable unqualified
        compare(button.Accessible.role, Accessible.Button);
        compare(button.Accessible.name, testShellUiContract.openAudioText); // qmllint disable unqualified
        verify(button.width >= testShellUiContract.controlTargetPreferred); // qmllint disable unqualified
        verify(button.height >= testShellUiContract.controlTargetPreferred); // qmllint disable unqualified
        compare(image.source.toString(), testShellUiContract.placeholderSource.toString()); // qmllint disable unqualified
        tryCompare(image, "status", Image.Ready, 500);
        shell.requestActivate();
        tryVerify(function () {
            return shell.active;
        }, 500);
        button.forceActiveFocus();
        tryVerify(function () {
            return button.activeFocus;
        }, 500);
        keyClick(Qt.Key_Space);
        compare(openSpy.count, 1);
        keyClick(Qt.Key_Return);
        compare(openSpy.count, 2);
        verify(button.background.border.width > 0);
        mousePress(button, button.width / 2, button.height / 2, Qt.LeftButton);
        verify(button.down);
        compare(button.contentItem.topPadding, 2);
        mouseRelease(button, button.width / 2, button.height / 2, Qt.LeftButton);
        compare(openSpy.count, 3);
        button.Accessible.pressAction();
        compare(openSpy.count, 4);
    }
}
