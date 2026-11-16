import QtQuick
import QtTest
import MusicPlayer.M0

// ADR-0019: the online search page (metadata only, download by video ID).
TestCase {
    id: testCase
    name: "SearchPage"
    when: windowShown
    readonly property var driver: testScenarioDriver // qmllint disable unqualified
    readonly property PlayerPresenter presenter: testLocalPlayerPresenter // qmllint disable unqualified
    readonly property ShellUiContract contract: testProductUiContract // qmllint disable unqualified
    property var shell

    QtObject {
        id: finder
        property bool available: true
        property bool busy: false
        property bool requesting: false
        property bool searched: false
        property string query: ""
        property string statusText: ""
        property var results: []
        property var searches: []
        property var downloads: []
        property var previews: []
        property int cancels: 0
        function search(text) {
            searches = searches.concat([text]);
            busy = true;
            statusText = "Buscando “" + text + "” no YouTube…";
            return true;
        }
        function cancel() {
            cancels += 1;
        }
        function download(id) {
            downloads = downloads.concat([id]);
            return true;
        }
        function openPreview(id) {
            previews = previews.concat([id]);
            return true;
        }
    }
    Component {
        id: shellComponent
        Main {
            uiContract: testCase.contract
            presenter: testCase.presenter
            onlineSearch: finder
        }
    }
    function init() {
        driver.resetLocalScenario();
        finder.available = true;
        finder.busy = false;
        finder.requesting = false;
        finder.statusText = "";
        finder.results = [];
        finder.searches = [];
        finder.downloads = [];
        finder.previews = [];
        finder.cancels = 0;
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
    function openSearch() {
        const button = findChild(shell, "search");
        verify(button.visible);
        mouseClick(button);
        const panel = findChild(shell, "search-panel");
        tryCompare(panel, "opened", true);
        compare(shell.currentPage, "search");
        verify(button.checked);
        return panel;
    }

    function test_theSearchOpensAsAPageWithTheFieldFocused() {
        const panel = openSearch();
        compare(panel.modal, false);
        tryVerify(() => findChild(panel, "search-field").activeFocus);
        verify(!findChild(panel, "search-results").visible);
        verify(!findChild(panel, "search-submit").enabled);
    }
    function test_enterRunsTheSearchAndABusySearchCanBeCancelled() {
        const panel = openSearch();
        const field = findChild(panel, "search-field");
        tryVerify(() => field.activeFocus);
        keyClick(Qt.Key_L);
        keyClick(Qt.Key_I);
        keyClick(Qt.Key_A);
        keyClick(Qt.Key_Return);
        compare(finder.searches, ["lia"]);
        tryVerify(() => findChild(panel, "search-cancel").visible);
        verify(!findChild(panel, "search-submit").visible);
        verify(findChild(panel, "search-status").text.indexOf("lia") >= 0);
        mouseClick(findChild(panel, "search-cancel"));
        compare(finder.cancels, 1);
    }
    function test_eachResultOffersOneDownload() {
        const panel = openSearch();
        finder.results = [
            {
                videoId: "aB0-_Cd1EF2",
                title: "Canção do Mar",
                channel: "Lia Campos",
                durationText: "3:37",
                requested: false
            },
            {
                videoId: "Zz9_-Yy8Xx7",
                title: "Já pedida",
                channel: "",
                durationText: "",
                requested: true
            }
        ];
        const list = findChild(panel, "search-results");
        tryVerify(() => list.visible && list.count === 2);
        // Showing the list relayouts the page; wait until it has its real size.
        tryVerify(() => list.width > 0 && list.height > 0);
        tryVerify(() => findChild(list, "search-row-aB0-_Cd1EF2") !== null);
        const first = findChild(list, "search-row-aB0-_Cd1EF2");
        verify(first.Accessible.name.indexOf("Lia Campos") >= 0);
        mouseClick(findChild(first, "search-download"));
        compare(finder.downloads, ["aB0-_Cd1EF2"]);
        tryVerify(() => findChild(list, "search-row-Zz9_-Yy8Xx7") !== null);
        verify(!findChild(findChild(list, "search-row-Zz9_-Yy8Xx7"), "search-download").enabled);
    }
    function test_eachResultCanBePreviewedInTheBrowser() {
        const panel = openSearch();
        finder.results = [
            {
                videoId: "Zz9_-Yy8Xx7",
                title: "Já pedida",
                channel: "Lia Campos",
                durationText: "3:37",
                requested: true
            }
        ];
        const list = findChild(panel, "search-results");
        tryVerify(() => list.visible && list.count === 1 && list.width > 0 && list.height > 0);
        tryVerify(() => findChild(list, "search-row-Zz9_-Yy8Xx7") !== null);
        const preview = findChild(findChild(list, "search-row-Zz9_-Yy8Xx7"), "search-preview");
        verify(preview && preview.visible);
        // A requested result can still be previewed; previewing never downloads.
        verify(preview.enabled);
        compare(preview.text, "Abrir no YouTube");
        compare(preview.accessibleName, "Abrir Já pedida no YouTube");
        mouseClick(preview);
        compare(finder.previews, ["Zz9_-Yy8Xx7"]);
        compare(finder.downloads, []);
    }
    function test_theEntryIsHiddenWhileTheSearchIsUnavailable() {
        finder.available = false;
        tryVerify(() => !findChild(shell, "search").visible);
        finder.available = true;
        tryVerify(() => findChild(shell, "search").visible);
    }
}
