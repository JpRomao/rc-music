import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "PlaylistPageLifecycle"
    when: windowShown
    readonly property var driver: testScenarioDriver // qmllint disable unqualified
    readonly property PlayerPresenter presenter: testLocalPlayerPresenter // qmllint disable unqualified
    readonly property ShellUiContract contract: testProductUiContract // qmllint disable unqualified
    property var first
    property var replacement
    property var shell
    Component {
        id: shellComponent
        Main {
            uiContract: testCase.contract
            presenter: testCase.presenter
        }
    }
    function init() {
        driver.resetLocalScenario();
        first = driver.createPlaylistsScenario();
        replacement = driver.createPlaylistsScenario();
        verify(first && replacement);
        for (const scenario of [first, replacement]) {
            tryCompare(scenario.binding, "ready", true);
            verify(scenario.binding.selectPlaylist("7"));
            compare(scenario.saves(), 0);
        }
        shell = createTemporaryObject(shellComponent, null, {
            playlists: first.binding,
            width: 1440,
            height: 900
        });
        verify(shell);
        shell.requestActivate();
        tryCompare(shell, "active", true);
    }
    function cleanup() {
        if (shell)
            shell.destroy();
        shell = null;
        if (first)
            verify(driver.releasePlaylistsScenario(first));
        if (replacement)
            verify(driver.releasePlaylistsScenario(replacement));
        first = null;
        replacement = null;
        wait(0);
        driver.resetLocalScenario();
    }
    function commands() {
        return [driver.localLoadCount(), driver.localUnloadCount(), driver.localPlayCount(), driver.localPauseCount(), driver.localSeekCount()];
    }
    function test_leavingOrReplacingThePageOwnerRevokesPendingFileSelection_data() {
        return [
            {
                tag: "picker-navigate",
                replaceFile: false,
                replaceOwner: false
            },
            {
                tag: "locator-navigate",
                replaceFile: true,
                replaceOwner: false
            },
            {
                tag: "picker-owner",
                replaceFile: false,
                replaceOwner: true
            },
            {
                tag: "locator-owner",
                replaceFile: true,
                replaceOwner: true
            }
        ];
    }
    function test_leavingOrReplacingThePageOwnerRevokesPendingFileSelection(data) {
        driver.enqueueLocalSelection(["C:/Fixture/Playing.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        shell.openPlaylists();
        tryCompare(shell, "currentPage", "playlists");
        const entries = first.binding.entries;
        const audio = commands();
        const identity = presenter.currentOccurrenceId;
        verify(data.replaceFile ? first.binding.requestReplaceFile("7", "11") : first.binding.requestAddFiles("7"));
        verify(first.holdReply(data.replaceFile), "The real binding must have started the controlled picker/locator");
        verify(first.binding.busy);
        if (data.replaceOwner) {
            shell.playlists = replacement.binding;
            const button = findChild(shell, "theme-toggle");
            button.forceActiveFocus(Qt.TabFocusReason);
            tryCompare(button, "activeFocus", true);
        } else {
            shell.openPreferences();
            tryCompare(shell, "currentPage", "preferences");
            const destination = findChild(shell, "preferences-panel");
            tryVerify(() => shell.isInside(shell.activeFocusItem, destination.contentItem));
        }
        const focus = shell.activeFocusItem;
        first.completeHeldReply(data.replaceFile);
        tryVerify(() => first.deliveries() === 1);
        tryCompare(first.binding, "busy", false);
        compare(first.saves(), 0, "A response from the previous page/owner must not save the old playlist");
        compare(first.binding.entries, entries);
        verify(!first.binding.hasPendingChange);
        compare(replacement.saves(), 0);
        compare(shell.activeFocusItem, focus);
        compare(presenter.currentOccurrenceId, identity);
        compare(commands(), audio);
    }
}
