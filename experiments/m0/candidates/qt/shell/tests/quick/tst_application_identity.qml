import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "ApplicationIdentity"
    when: windowShown

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testPlayerPresenter // qmllint disable unqualified
        }
    }

    function test_windowUsesTheProductBrand() {
        const shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        compare(testShellUiContract.appName, "RC Music"); // qmllint disable unqualified
        compare(shell.title, "RC Music");
    }
}
