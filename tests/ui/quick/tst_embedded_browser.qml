import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "EmbeddedBrowserComposition"
    when: windowShown
    property var shell
    readonly property var driver: testScenarioDriver // qmllint disable unqualified
    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testLocalPlayerPresenter // qmllint disable unqualified
        }
    }
    Component {
        id: browserComponent
        QtObject {
            property int opens: 0
            property bool reject: false
            property string errorText: ""
            signal errorChanged
            function open() {
                opens += 1;
                if (reject) {
                    const message = "Não foi possível abrir a navegação. Tente novamente.";
                    if (errorText !== message) {
                        errorText = message;
                        errorChanged();
                    }
                }
            }
        }
    }
    function init() {
        driver.resetLocalScenario();
    }
    function cleanup() {
        if (shell)
            shell.destroy();
        shell = null;
        driver.resetLocalScenario();
    }
    function test_liteHasNoVisibleNavigationAction() {
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        compare(shell.embeddedBrowser, null);
        const action = findChild(shell, "embedded-browser");
        verify(action);
        compare(action.visible, false);
    }
    function test_optionalActionOpensOnlyAfterExplicitClick() {
        const browser = createTemporaryObject(browserComponent, null);
        shell = createTemporaryObject(shellComponent, null, {
            embeddedBrowser: browser
        });
        verify(shell);
        compare(browser.opens, 0);
        const action = findChild(shell, "embedded-browser");
        verify(action && action.visible && action.enabled);
        mouseClick(action);
        compare(browser.opens, 1);
        compare(findChild(shell, "embedded-browser-error").visible, false);
    }
    function test_openFailureHasAnActionableModalWithoutChangingPlayer() {
        const browser = createTemporaryObject(browserComponent, null, {
            reject: true
        });
        shell = createTemporaryObject(shellComponent, null, {
            embeddedBrowser: browser
        });
        verify(shell);
        const before = shell.presenter.playbackState;
        mouseClick(findChild(shell, "embedded-browser"));
        tryCompare(findChild(shell, "embedded-browser-error"), "visible", true);
        compare(shell.presenter.playbackState, before);
        compare(shell.windowShortcutsEnabled, false);
    }
    function test_repeatedSameFailureReopensTheDismissedWarning() {
        const browser = createTemporaryObject(browserComponent, null, {
            reject: true
        });
        shell = createTemporaryObject(shellComponent, null, {
            embeddedBrowser: browser
        });
        verify(shell);
        const action = findChild(shell, "embedded-browser");
        const notice = findChild(shell, "embedded-browser-error");
        mouseClick(action);
        tryCompare(notice, "visible", true);
        const firstError = browser.errorText;
        notice.close();
        tryCompare(notice, "visible", false);
        mouseClick(action);
        compare(browser.opens, 2);
        compare(browser.errorText, firstError);
        tryCompare(notice, "visible", true);
    }
}
