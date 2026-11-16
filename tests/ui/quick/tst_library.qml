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
        function enqueue(id) {
            enqueued = enqueued.concat([id]);
            return true;
        }
        function remove(id) {
            removed = removed.concat([id]);
            return true;
        }
        function retryLoad() {
            return true;
        }
        function retrySave() {
            return true;
        }
    }
    Component {
        id: shellComponent
        Main {
            uiContract: testCase.contract
            presenter: testCase.presenter
            library: catalog
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
