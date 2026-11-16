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
    QtObject {
        id: compactLyrics
        property string title: "Local"
        property string artist: ""
        property string album: ""
        property int durationMs: 180000
        property string statusText: "Letra local"
        property string currentLine: "Linha atual"
        property string previousLine: ""
        property string nextLine: ""
        property string plainText: ""
        property bool hasSyncedLyrics: true
        property bool busy: false
        property bool canSearch: false
        property bool lyricsCopyProtected: false
        property bool lyricFindConfigured: false
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
        lists.canRetrySave = false;
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
    function test_pagesUseTheEditorialHeaderWithoutACard_data() {
        return [
            {
                tag: "playlists",
                open: "openPlaylists",
                panel: "playlists-panel",
                title: "Playlists",
                nav: "playlists",
                close: "playlists-close",
                labelled: true
            },
            {
                tag: "activity",
                open: "openDownloads",
                panel: "downloads-panel",
                title: "Atividade",
                nav: "downloads",
                close: "downloads-close"
            },
            {
                tag: "settings",
                open: "openPreferences",
                panel: "preferences-panel",
                title: "Configurações",
                nav: "preferences",
                close: "preferences-close"
            }
        ];
    }
    function test_pagesUseTheEditorialHeaderWithoutACard(data) {
        loadQueue();
        shell[data.open]();
        const panel = findChild(shell, data.panel);
        tryCompare(panel, "opened", true);
        const header = findChild(panel, "page-header");
        verify(header && header.visible, data.tag + " has the editorial page header");
        const title = findChild(header, "page-title");
        compare(title.text, data.title);
        compare(title.font.family, contract.headingFontFamily);
        compare(title.font.pixelSize, contract.typeHeading);
        verify(findChild(header, "page-eyebrow").text.length > 0);
        verify(findChild(header, "page-description").text.length > 0);
        // Pages sit on the canvas like the home page, not inside a bordered card.
        compare(panel.background.border.width, 0);
        verify(Qt.colorEqual(panel.background.color, contract.canvasColor), String(panel.background.color));
        const close = findChild(panel, data.close);
        verify(close && close.visible);
        verify(close.flat, "closing is a quiet action");
        compare(close.iconName, "close");
        compare(close.contentItem.text.length > 0, data.labelled === true, "close label");
        verify(close.accessibleName.length > 0);
        compare(findChild(shell, "workspace-page-title").text, data.title);
        const root = findChild(shell, "workspace-breadcrumb-root");
        verify(root && root.visible);
        compare(root.text, "Seu espaço");
        compare(findChild(shell, data.nav).text, data.title);
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
    function test_pageResizeKeepsTheSameFocusedActionVisible_data() {
        return [
            {
                tag: "playlists",
                preferences: false,
                recovery: false,
                lyrics: false
            },
            {
                tag: "preferences",
                preferences: true,
                recovery: false,
                lyrics: false
            },
            {
                tag: "recovery-with-lyrics",
                preferences: false,
                recovery: true,
                lyrics: true
            }
        ];
    }
    function test_pageResizeKeepsTheSameFocusedActionVisible(data) {
        loadQueue();
        if (data.lyrics)
            shell.lyrics = compactLyrics;
        lists.hasPendingChange = data.recovery;
        lists.canRetrySave = data.recovery;
        if (data.preferences)
            shell.openPreferences();
        else
            shell.openPlaylists();
        const page = findChild(shell, data.preferences ? "preferences-panel" : "playlists-panel");
        tryCompare(page, "opened", true);
        if (data.preferences)
            findChild(page, "preferences-playback").Accessible.pressAction();
        verify(waitForPolish(shell));
        const action = findChild(page, data.preferences ? "skip-forward-seconds" : (data.recovery ? "playlists-retry-save" : "playlists-close"));
        verify(action && action.visible && action.enabled);
        action.forceActiveFocus(Qt.TabFocusReason);
        tryCompare(action, "activeFocus", true);
        const audio = commands();
        const identity = presenter.currentOccurrenceId;
        shell.width = 720;
        shell.height = 520;
        tryCompare(shell.contentItem, "height", 520);
        verify(waitForPolish(shell));
        const viewport = findChild(page, "page-viewport");
        tryVerify(() => {
            const p = action.mapToItem(viewport, 0, 0);
            return p.x >= 0 && p.y >= 0 && p.x + action.width <= viewport.width + 1 && p.y + action.height <= viewport.height + 1;
        }, 5000, "Resize must reveal the existing page focus, not require another focus event");
        compare(shell.activeFocusItem, action);
        compare(presenter.currentOccurrenceId, identity);
        compare(presenter.positionMs, 41000);
        compare(commands(), audio);
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
