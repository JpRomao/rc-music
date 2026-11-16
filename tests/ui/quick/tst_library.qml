import QtQuick
import QtTest
import MusicPlayer.M0

// ADR-0019: the permanent library page in List view.
TestCase {
    id: testCase
    name: "LibraryPage"
    when: windowShown
    readonly property var driver: testScenarioDriver // qmllint disable unqualified
    readonly property PlayerPresenter presenter: testLocalPlayerPresenter // qmllint disable unqualified
    readonly property ShellUiContract contract: testProductUiContract // qmllint disable unqualified
    property var shell

    ListModel {
        id: records
        ListElement {
            recordId: "3"
            title: "Canção do Mar"
            artist: "Lia Campos"
            album: "Depois da Chuva"
            durationText: "3:37"
            available: true
            fileName: "Canção do Mar.mp3"
        }
        ListElement {
            recordId: "4"
            title: "faixa sem nome"
            artist: ""
            album: ""
            durationText: ""
            available: false
            fileName: "faixa sem nome.flac"
        }
    }
    QtObject {
        id: catalog
        property bool ready: true
        property bool busy: false
        property int count: 2
        property int visibleCount: 2
        property bool hasPendingChange: false
        property bool canRetrySave: false
        property string statusText: "Sua biblioteca está pronta."
        property string filterText: ""
        property var model: records
        property var enqueued: []
        property var removed: []
        property var located: []
        property var playlistAdds: []
        property bool canLocate: true
        function enqueue(id) {
            enqueued = enqueued.concat([id]);
            return true;
        }
        function remove(id) {
            removed = removed.concat([id]);
            return true;
        }
        function locate(id) {
            located = located.concat([id]);
            statusText = "Arquivo localizado.";
            return true;
        }
        function cancelLocate() {
        }
        function addToPlaylist(id, playlist) {
            playlistAdds = playlistAdds.concat([id + ":" + playlist]);
            return true;
        }
        function retryLoad() {
            return true;
        }
        function retrySave() {
            return true;
        }
    }
    QtObject {
        id: lists
        property bool ready: true
        property bool busy: false
        property bool interactionAllowed: true
        property bool hasPendingChange: false
        property bool canRetrySave: false
        property bool canAddFiles: true
        property bool canReplaceFile: true
        property string statusText: ""
        property string selectedPlaylistId: ""
        property var playlists: [
            {
                id: "7",
                name: "Favoritas 日本",
                count: 0
            }
        ]
        property var entries: []
        function selectPlaylist(id) {
            return true;
        }
        function retryLoad() {
            return true;
        }
    }
    Component {
        id: shellComponent
        Main {
            uiContract: testCase.contract
            presenter: testCase.presenter
            library: catalog
            playlists: lists
        }
    }
    function init() {
        driver.resetLocalScenario();
        catalog.ready = true;
        catalog.count = 2;
        catalog.visibleCount = 2;
        catalog.filterText = "";
        catalog.enqueued = [];
        catalog.removed = [];
        catalog.located = [];
        catalog.playlistAdds = [];
        catalog.statusText = "Sua biblioteca está pronta.";
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
    function openLibrary() {
        const button = findChild(shell, "library");
        verify(button.visible);
        mouseClick(button);
        const panel = findChild(shell, "library-panel");
        tryCompare(panel, "opened", true);
        compare(shell.currentPage, "library");
        verify(button.checked);
        return panel;
    }
    function row(panel, id) {
        const list = findChild(panel, "library-list");
        tryVerify(() => findChild(list, "library-row-" + id) !== null);
        return findChild(list, "library-row-" + id);
    }

    function test_theLibraryOpensAsAPageInListView() {
        const panel = openLibrary();
        compare(panel.modal, false);
        const list = findChild(panel, "library-list");
        verify(list.visible);
        compare(list.count, 2);
        compare(findChild(panel, "library-count").text, "2 de 2 faixas");
        verify(!findChild(panel, "library-empty").visible);
        verify(!findChild(panel, "library-no-matches").visible);
        const unavailable = row(panel, "4");
        verify(unavailable.Accessible.name.indexOf("Indisponível") >= 0);
        verify(!findChild(unavailable, "library-enqueue").enabled);
    }
    function test_enqueueingSendsTheRecordToTheQueue() {
        const panel = openLibrary();
        const first = row(panel, "3");
        mouseClick(findChild(first, "library-enqueue"));
        compare(catalog.enqueued, ["3"]);
        const notice = findChild(panel, "library-notice");
        verify(notice.visible);
        verify(notice.text.indexOf("Canção do Mar") >= 0);
        compare(catalog.removed, []);
    }
    function test_removingAsksFirstAndTouchesOnlyTheRecord() {
        const panel = openLibrary();
        mouseClick(findChild(row(panel, "3"), "library-remove"));
        const confirmation = findChild(shell, "library-remove-confirmation");
        tryCompare(confirmation, "visible", true);
        mouseClick(findChild(confirmation, "library-remove-cancel"));
        tryCompare(confirmation, "visible", false);
        compare(catalog.removed, []);
        mouseClick(findChild(row(panel, "3"), "library-remove"));
        tryCompare(confirmation, "visible", true);
        mouseClick(findChild(confirmation, "library-remove-confirm"));
        tryCompare(confirmation, "visible", false);
        compare(catalog.removed, ["3"]);
        compare(catalog.enqueued, []);
    }
    function test_onlyUnavailableRecordsOfferLocating() {
        const panel = openLibrary();
        verify(!findChild(row(panel, "3"), "library-locate").visible);
        const locate = findChild(row(panel, "4"), "library-locate");
        verify(locate.visible);
        mouseClick(locate);
        compare(catalog.located, ["4"]);
        tryVerify(() => findChild(panel, "library-status").visible);
        compare(findChild(panel, "library-status").text, "Arquivo localizado.");
    }
    function test_aTrackCanBeAddedToASavedPlaylist() {
        const panel = openLibrary();
        mouseClick(findChild(row(panel, "3"), "library-add-to-playlist"));
        const menu = findChild(panel, "library-playlist-menu");
        tryCompare(menu, "opened", true);
        const choice = findChild(menu.contentItem, "library-playlist-choice-7");
        verify(choice);
        mouseClick(choice);
        compare(catalog.playlistAdds, ["3:7"]);
        verify(findChild(panel, "library-notice").text.indexOf("Favoritas") >= 0);
    }
    function test_listIsTheDefaultAndCoversIsTheAlternative() {
        const panel = openLibrary();
        verify(findChild(panel, "library-view-list").checked);
        verify(!findChild(panel, "library-grid").visible);
        mouseClick(findChild(panel, "library-view-covers"));
        tryVerify(() => findChild(panel, "library-grid").visible);
        verify(!findChild(panel, "library-list").visible);
        verify(!findChild(panel, "library-columns").visible);
        const grid = findChild(panel, "library-grid");
        tryCompare(grid, "count", 2);
        // Switching views relayouts the page; wait until the grid has its real size.
        tryVerify(() => grid.width > 0 && grid.height > 0);
        tryVerify(() => findChild(grid, "library-card-3") !== null);
        mouseClick(findChild(findChild(grid, "library-card-3"), "library-card-enqueue"));
        compare(catalog.enqueued, ["3"]);
        verify(!findChild(findChild(grid, "library-card-4"), "library-card-enqueue").enabled);
        mouseClick(findChild(panel, "library-view-list"));
        tryVerify(() => findChild(panel, "library-list").visible);
    }
    function test_typingFiltersThroughTheBinding() {
        const panel = openLibrary();
        const field = findChild(panel, "library-filter");
        field.forceActiveFocus();
        keyClick(Qt.Key_M);
        keyClick(Qt.Key_A);
        keyClick(Qt.Key_R);
        compare(catalog.filterText, "mar");
    }
    function test_emptyAndNoMatchStatesAreDistinct() {
        catalog.count = 0;
        catalog.visibleCount = 0;
        const panel = openLibrary();
        verify(findChild(panel, "library-empty").visible);
        verify(!findChild(panel, "library-filter").visible);
        verify(!findChild(panel, "library-no-matches").visible);
        catalog.count = 2;
        catalog.filterText = "rock";
        tryVerify(() => findChild(panel, "library-no-matches").visible);
        verify(!findChild(panel, "library-empty").visible);
        verify(findChild(panel, "library-no-matches").text.indexOf("rock") >= 0);
    }
}
