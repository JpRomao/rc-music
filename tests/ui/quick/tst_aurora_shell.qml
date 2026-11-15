import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    name: "AuroraShell"
    when: windowShown

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testPlayerPresenter // qmllint disable unqualified
        }
    }

    function test_removedIntegrationIsNotOfferedByTheShell() {
        const shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        verify(!("lidarr" in shell));
        compare(findChild(shell, "lidarr"), null);
        compare(findChild(shell, "lidarr-panel"), null);
        verify(findChild(shell, "queue"));
        verify(findChild(shell, "preferences"));
        verify(findChild(shell, "transport-bar"));
    }
}
