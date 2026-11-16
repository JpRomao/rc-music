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
        property bool linksMode: false
        property var linkIssues: []
        property bool canDownloadAll: false
        property var pastedTexts: []
        property int downloadAllCalls: 0
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
        function lookupLinks(text) {
            pastedTexts = pastedTexts.concat([text]);
            busy = true;
            linksMode = true;
            return true;
        }
        function downloadAll() {
            downloadAllCalls += 1;
            canDownloadAll = false;
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
        finder.linksMode = false;
        finder.linkIssues = [];
        finder.canDownloadAll = false;
        finder.pastedTexts = [];
        finder.downloadAllCalls = 0;
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
    function test_severalLinksCanBePastedAtOnce() {
        const panel = openSearch();
        const toggle = findChild(panel, "search-links-toggle");
        verify(toggle && toggle.visible);
        compare(toggle.text, "Colar links");
        const field = findChild(panel, "search-links-field");
        verify(!field.visible, "The links box opens on demand");
        mouseClick(toggle);
        tryVerify(() => field.visible);
        verify(field.activeFocus);
        const submit = findChild(panel, "search-links-submit");
        verify(!submit.enabled, "Nothing pasted yet");
        field.text = "https://youtu.be/aB0-_Cd1EF2\nhttps://www.letras.mus.br/lia/mar/";
        verify(submit.enabled);
        compare(submit.text, "Consultar links");
        mouseClick(submit);
        compare(finder.pastedTexts, ["https://youtu.be/aB0-_Cd1EF2\nhttps://www.letras.mus.br/lia/mar/"]);
        compare(finder.searches, []);

        finder.busy = false;
        finder.linkIssues = ["Linha 3: não é um link do YouTube nem do Letras."];
        finder.results = [
            {
                videoId: "aB0-_Cd1EF2",
                title: "Canção do Mar",
                channel: "Lia Campos",
                durationText: "3:37",
                requested: false,
                hasLyricsPage: true
            }
        ];
        finder.canDownloadAll = true;
        const issues = findChild(panel, "search-link-issues");
        tryVerify(() => issues.visible);
        compare(issues.text, "Linha 3: não é um link do YouTube nem do Letras.");
        const all = findChild(panel, "search-download-all");
        tryVerify(() => all.visible && all.enabled);
        compare(all.text, "Baixar todos");
        mouseClick(all);
        compare(finder.downloadAllCalls, 1);
        verify(!all.enabled);
        // Download all belongs to pasted links, not to a text search.
        finder.linksMode = false;
        tryVerify(() => !all.visible);
    }
    function linkResults(count) {
        const rows = [];
        for (let index = 0; index < count; ++index)
            rows.push({
                videoId: "aB0-_Cd1EF" + index,
                title: "Faixa " + index,
                channel: "Canal",
                durationText: "3:0" + index,
                requested: false,
                hasLyricsPage: false
            });
        return rows;
    }
    function test_statusAndLinkIssuesAreReadByAssistiveTechnology() {
        const panel = openSearch();
        finder.statusText = "3 links prontos para baixar.";
        finder.linksMode = true;
        finder.linkIssues = ["Linha 4: não é um link do YouTube nem do Letras."];
        finder.results = linkResults(1);
        const status = findChild(panel, "search-status");
        compare(status.Accessible.role, Accessible.StaticText);
        compare(status.Accessible.name, "3 links prontos para baixar.");
        const issues = findChild(panel, "search-link-issues");
        tryVerify(() => issues.visible);
        compare(issues.Accessible.role, Accessible.StaticText);
        compare(issues.Accessible.name, "Linha 4: não é um link do YouTube nem do Letras.");
    }
    function test_resultsScrollInsideASmallWindowWithTheLinksBoxOpen() {
        shell.width = 1024;
        shell.height = 760;
        const panel = openSearch();
        mouseClick(findChild(panel, "search-links-toggle"));
        finder.linksMode = true;
        finder.statusText = "6 links prontos para baixar.";
        finder.results = linkResults(6);
        const list = findChild(panel, "search-results");
        tryVerify(() => list.visible && list.count === 6 && list.height > 0);
        verify(waitForPolish(shell));
        // The list ends above the player footer and scrolls instead of being cut.
        const bottom = list.mapToItem(shell.contentItem, 0, list.height).y;
        verify(bottom <= findChild(shell, "player-footer").y + 1, "list bottom " + bottom);
        verify(list.height < list.contentHeight, "the list scrolls");
        verify(list.height >= 60, "at least one row stays visible");
        list.positionViewAtEnd();
        tryVerify(() => findChild(list, "search-row-aB0-_Cd1EF5") !== null);
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
