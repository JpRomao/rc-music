import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "AuroraPages"
    when: windowShown
    readonly property var driver: testScenarioDriver // qmllint disable unqualified
    readonly property PlayerPresenter presenter: testLocalPlayerPresenter // qmllint disable unqualified
    readonly property ShellUiContract contract: testProductUiContract // qmllint disable unqualified
    property var shell

    QtObject {
        id: lists
        property bool ready: true
        property bool busy: false
        property bool interactionAllowed: true
        property bool hasPendingChange: false
        property bool canRetrySave: false
        property bool canAddFiles: true
        property bool canReplaceFile: true
        property string statusText: "Playlists salvas"
        property string selectedPlaylistId: "1"
        property var playlists: [
            {
                id: "1",
                name: "Favoritas 日本",
                count: 1
            }
        ]
        property var entries: [
            {
                id: "11",
                title: "Faixa.mp3",
                path: "C:/Fixture/Faixa.mp3"
            }
        ]
        property int writes: 0
        function selectPlaylist(id) {
            selectedPlaylistId = id;
            return true;
        }
        function createPlaylist(name) {
            ++writes;
            return true;
        }
        function renamePlaylist(id, name) {
            ++writes;
            return true;
        }
        function deletePlaylist(id) {
            ++writes;
            return true;
        }
        function saveQueueAsPlaylist(name) {
            ++writes;
            return true;
        }
        function addQueueToPlaylist(id) {
            ++writes;
            return true;
        }
        function requestAddFiles(id) {
            return true;
        }
        function requestReplaceFile(id, entry) {
            return true;
        }
        function removeEntry(id, entry) {
            ++writes;
            return true;
        }
        function moveEntry(id, entry, destination) {
            ++writes;
            return true;
        }
        function enqueuePlaylist(id) {
            return true;
        }
        function retrySave() {
            return true;
        }
        function discardPending() {
            return true;
        }
        function discardUnsavedChange() {
            return true;
        }
        function retryLoad() {
            return true;
        }
    }
    QtObject {
        id: jobs
        property bool canCloseAfterFinalSaveFailure: false
        property bool canRetryFinalSave: false
        property bool canRetryAdmission: false
        property bool canRetryStartup: false
        property bool canRenewNamespace: false
        property bool canArchiveSettled: false
        property bool canRetryArchive: false
        property string warningText: ""
        property string statusText: "Sem downloads"
        property string importConsentJobId: ""
        property string importConsentToken: ""
        property string discardConsentJobId: ""
        property string discardConsentToken: ""
        property var jobsModel: null
    }
    Component {
        id: shellComponent
        Main {
            uiContract: testCase.contract
            presenter: testCase.presenter
            playlists: lists
            downloads: jobs
        }
    }
    function init() {
        driver.resetLocalScenario();
        lists.writes = 0;
        lists.hasPendingChange = false;
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.width = 1440;
        shell.height = 900;
        shell.requestActivate();
        tryCompare(shell, "active", true);
    }
    function cleanup() {
        if (shell)
            shell.destroy();
        shell = null;
        driver.resetLocalScenario();
    }
    function commands() {
        return [driver.localLoadCount(), driver.localUnloadCount(), driver.localPlayCount(), driver.localPauseCount(), driver.localSeekCount()];
    }
    function loadQueue() {
        driver.enqueueLocalSelection(["C:/Fixture/Primeira.mp3", "C:/Fixture/Segunda.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalPlaying();
        driver.publishLocalPosition(41000, 180000);
        presenter.setVolume(0.52);
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
    }
    function inside(item, ancestor) {
        while (item) {
            if (item === ancestor)
                return true;
            item = item.parent;
        }
        return false;
    }
    function assertPage(panel, page) {
        tryCompare(panel, "opened", true);
        compare(panel.modal, false, "A destination is a page, not a modal popup");
        compare(shell.currentPage, page);
        verify(panel.embedded);
        const point = panel.mapToItem(shell.contentItem, 0, 0);
        const nav = findChild(shell, "side-navigation");
        const footer = findChild(shell, "player-footer");
        verify(point.x >= nav.width);
        verify(point.y >= 0 && point.y + panel.height <= footer.y + 1);
        verify(panel.width >= shell.width - nav.width - 90);
        compare(findChild(panel, "legacy-page-dialog"), null, "Inline pages must not instantiate a hidden popup host");
    }
    function test_pagesShareTheWorkspaceWithoutOwningThePlayer() {
        loadQueue();
        const transport = findChild(shell, "transport-bar");
        const occurrence = presenter.currentOccurrenceId;
        const audio = commands();
        shell.openPlaylists();
        assertPage(findChild(shell, "playlists-panel"), "playlists");
        shell.openPreferences();
        assertPage(findChild(shell, "preferences-panel"), "preferences");
        verify(!findChild(shell, "playlists-panel").visible);
        shell.openDownloads();
        assertPage(findChild(shell, "downloads-panel"), "downloads");
        verify(!findChild(shell, "preferences-panel").visible);
        shell.setQueueOpen(true);
        tryCompare(shell, "currentPage", "queue");
        verify(!findChild(shell, "downloads-panel").visible);
        findChild(shell, "now").Accessible.pressAction();
        tryCompare(shell, "currentPage", "home");
        compare(findChild(shell, "transport-bar"), transport);
        compare(presenter.currentOccurrenceId, occurrence);
        compare(presenter.positionMs, 41000);
        compare(presenter.volume, 0.52);
        compare(commands(), audio);
    }
    function test_pageNavigationPreservesPlaybackAndRevokesTransientOwners() {
        loadQueue();
        const audio = commands();
        shell.openPlaylists();
        const page = findChild(shell, "playlists-panel");
        tryCompare(page, "opened", true);
        findChild(shell, "playlist-create").Accessible.pressAction();
        const consent = findChild(shell, "playlist-name-dialog");
        tryCompare(consent, "opened", true);
        verify(consent.modal);
        shell.openDownloads();
        tryCompare(consent, "visible", false);
        tryCompare(shell, "currentPage", "downloads");
        compare(page.nameOwner, null);
        compare(lists.writes, 0);
        verify(waitForPolish(shell));
        verify(inside(shell.activeFocusItem, findChild(shell, "downloads-panel").contentItem));
        compare(commands(), audio);
    }
    function test_downloadsEmptyPageDoesNotCreateAKeyboardIsland() {
        loadQueue();
        shell.openDownloads();
        const page = findChild(shell, "downloads-panel");
        tryCompare(page, "opened", true);
        findChild(shell, "downloads-close").forceActiveFocus(Qt.TabFocusReason);
        let escaped = false;
        for (let step = 0; step < 20; ++step) {
            keyClick(Qt.Key_Tab);
            if (!inside(shell.activeFocusItem, page.contentItem)) {
                escaped = true;
                break;
            }
        }
        verify(escaped, "An empty activity page must not cycle forever between its close button and list");
        verify(page.visible);
    }
    function test_inlinePageDoesNotTrapTabAndEditorsOwnTheirKeys() {
        loadQueue();
        shell.openPreferences();
        const page = findChild(shell, "preferences-panel");
        tryCompare(page, "opened", true);
        findChild(shell, "preferences-playback").Accessible.pressAction();
        const editor = findChild(shell, "skip-backward-seconds");
        verify(editor);
        verify(editor instanceof TextInput);
        editor.forceActiveFocus(Qt.TabFocusReason);
        const audio = commands();
        keyClick(Qt.Key_Space);
        compare(commands(), audio, "Space in a preferences editor must not pause audio");
        findChild(shell, "preferences-general").forceActiveFocus(Qt.TabFocusReason);
        let escaped = false;
        for (let step = 0; step < 40; ++step) {
            keyClick(Qt.Key_Tab);
            if (!inside(shell.activeFocusItem, page.contentItem)) {
                escaped = true;
                break;
            }
        }
        verify(escaped, "Tab must be able to leave an inline page for navigation/player");
        verify(page.visible, "Leaving the page's focus chain must not close it");
    }
}
