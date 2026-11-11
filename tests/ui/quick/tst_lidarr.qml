import QtQuick
import QtQuick.Controls
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "LidarrPanel"
    when: windowShown
    readonly property var driver: testScenarioDriver // qmllint disable unqualified
    readonly property PlayerPresenter presenter: testLocalPlayerPresenter // qmllint disable unqualified
    property var shell
    readonly property var backend: lidarrBackend
    QtObject {
        id: optionalDownloads
        property bool canCloseAfterFinalSaveFailure: false
        property bool canRetryAdmission: false
        property bool canRetryFinalSave: false
        property bool canRetryStartup: false
        property bool canRenewNamespace: false
        property string warningText: ""
        property string statusText: "Downloads"
        property string importConsentJobId: ""
        property string importConsentToken: ""
        property string discardConsentJobId: ""
        property string discardConsentToken: ""
        property var jobsModel: null
    }
    QtObject {
        id: optionalBrowser
        property string errorText: ""
        signal errorChanged
        function open() {
        }
    }
    QtObject {
        id: lidarrBackend
        property bool connected: false
        property bool busy: false
        property bool interactionAllowed: true
        property string serverAddress: "http://localhost:8686"
        property string statusText: "Lidarr desconectado"
        property var results: []
        property var rootFolders: ["D:/Lidarr Music"]
        property var qualityProfiles: [
            {
                id: 7,
                name: "Lossless"
            }
        ]
        property var metadataProfiles: [
            {
                id: 9,
                name: "Albums"
            }
        ]
        property int connects: 0
        property int searches: 0
        property int downloads: 0
        property int cancels: 0
        property string submittedAddress: ""
        property string submittedKey: ""
        property string submittedTerm: ""
        property string submittedKind: ""
        property var submittedDownload: null
        function connectServer(address, key) {
            connects++;
            submittedAddress = address;
            submittedKey = key;
            return true;
        }
        function disconnectServer() {
            connected = false;
            results = [];
        }
        function search(term, kind) {
            searches++;
            submittedTerm = term;
            submittedKind = kind;
            return true;
        }
        function requestDownload(key, root, quality, metadata) {
            downloads++;
            submittedDownload = {
                key: key,
                root: root,
                quality: quality,
                metadata: metadata
            };
            return true;
        }
        function cancel() {
            cancels++;
            busy = false;
        }
    }
    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testCase.presenter
            lidarr: testCase.backend
        }
    }
    function init() {
        driver.resetLocalScenario();
        backend.connected = false;
        backend.busy = false;
        backend.interactionAllowed = true;
        backend.connects = 0;
        backend.searches = 0;
        backend.downloads = 0;
        backend.cancels = 0;
        backend.results = [];
        backend.rootFolders = ["D:/Lidarr Music"];
        backend.qualityProfiles = [
            {
                id: 7,
                name: "Lossless"
            }
        ];
        backend.metadataProfiles = [
            {
                id: 9,
                name: "Albums"
            }
        ];
        backend.submittedDownload = null;
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.width = 720;
        shell.height = 520;
        shell.requestActivate();
        tryVerify(function () {
            return testCase.shell.active;
        });
    }
    function cleanup() {
        if (shell)
            shell.destroy();
        shell = null;
        wait(0);
        driver.resetLocalScenario();
    }
    function openPanel() {
        const button = findChild(shell, "lidarr");
        verify(button && button.visible);
        mouseClick(button);
        tryCompare(findChild(shell, "lidarr-panel"), "visible", true);
    }
    function showResults(rows) {
        openPanel();
        backend.connected = true;
        backend.results = rows;
        const list = findChild(shell, "lidarr-results");
        tryCompare(list, "count", rows.length);
        verify(waitForPolish(shell));
        if (rows.length > 0)
            resultAt(0);
    }
    function resultAt(index) {
        const list = findChild(shell, "lidarr-results");
        list.positionViewAtIndex(index, ListView.Contain);
        verify(waitForPolish(shell));
        tryVerify(function () {
            return list.itemAtIndex(index) !== null;
        });
        return list.itemAtIndex(index);
    }
    function album(key, existingId) {
        return {
            key: key,
            kind: "album",
            title: "<b>Album original</b>",
            artist: "Artist 日本",
            year: "2026",
            existingId: existingId
        };
    }
    function test_openingDoesNotConnectAndConnectionUsesExplicitMaskedKey() {
        openPanel();
        compare(backend.connects, 0);
        compare(backend.searches, 0);
        const address = findChild(shell, "lidarr-address");
        const key = findChild(shell, "lidarr-api-key");
        verify(address && key);
        compare(address.text, "http://localhost:8686");
        compare(key.echoMode, TextInput.Password);
        key.text = "private-example-key";
        mouseClick(findChild(shell, "lidarr-connect"));
        compare(backend.connects, 1);
        compare(backend.submittedAddress, "http://localhost:8686");
        compare(backend.submittedKey, "private-example-key");
        compare(key.text, "");
    }
    function test_searchSendsOnlyExplicitTermAndKind() {
        showResults([]);
        const query = findChild(shell, "lidarr-query");
        query.text = "Album & artist 日本";
        compare(backend.searches, 0);
        mouseClick(findChild(shell, "lidarr-search"));
        compare(backend.searches, 1);
        compare(backend.submittedTerm, "Album & artist 日本");
        compare(backend.submittedKind, "album");
        findChild(shell, "lidarr-kind").currentIndex = 1;
        mouseClick(findChild(shell, "lidarr-search"));
        compare(backend.searches, 2);
        compare(backend.submittedKind, "artist");
        compare(backend.downloads, 0);
    }
    function test_albumDownloadNeedsConfirmationAndServerChoices() {
        showResults([album("lookup-1", 0)]);
        const row = resultAt(0);
        const title = findChild(row, "lidarr-result-title-0");
        verify(title);
        compare(title.text, "<b>Album original</b>");
        compare(title.textFormat, Text.PlainText);
        verify(row.Accessible.name.includes("No catálogo do Lidarr"));
        const detail = findChild(row, "lidarr-result-detail-0");
        verify(detail.text.includes("No catálogo do Lidarr"));
        compare(detail.textFormat, Text.PlainText);
        mouseClick(row);
        compare(backend.downloads, 0);
        mouseClick(findChild(shell, "lidarr-request"));
        compare(backend.downloads, 0);
        const confirmation = findChild(shell, "lidarr-confirmation");
        tryCompare(confirmation, "visible", true);
        compare(findChild(shell, "lidarr-confirm-title").text, "<b>Album original</b>");
        compare(findChild(shell, "lidarr-confirm-title").textFormat, Text.PlainText);
        verify(findChild(shell, "lidarr-confirm-advice").text.includes("cadastrará este álbum"));
        mouseClick(findChild(shell, "lidarr-confirm-submit"));
        compare(backend.downloads, 1);
        compare(backend.submittedDownload.key, "lookup-1");
        compare(backend.submittedDownload.root, "D:/Lidarr Music");
        compare(backend.submittedDownload.quality, 7);
        compare(backend.submittedDownload.metadata, 9);
        compare(confirmation.visible, false);
    }
    function test_staleConfirmationNeverTargetsReplacementResult() {
        showResults([album("old-lookup", 0)]);
        mouseClick(resultAt(0));
        mouseClick(findChild(shell, "lidarr-request"));
        const confirmation = findChild(shell, "lidarr-confirmation");
        tryCompare(confirmation, "visible", true);
        backend.results = [album("new-lookup", 0)];
        wait(0);
        const submit = findChild(shell, "lidarr-confirm-submit");
        verify(!confirmation.visible || !submit.enabled);
        compare(backend.downloads, 0);
    }
    function test_confirmationCannotBeReusedWithAnotherLidarrConnection() {
        const rows = [album("same-key", 42)];
        showResults(rows);
        mouseClick(resultAt(0));
        mouseClick(findChild(shell, "lidarr-request"));
        const confirmation = findChild(shell, "lidarr-confirmation");
        tryCompare(confirmation, "visible", true);
        const submit = findChild(shell, "lidarr-confirm-submit");
        verify(submit.enabled);
        let replacementRequests = 0;
        shell.lidarr = {
            connected: true,
            busy: false,
            interactionAllowed: true,
            serverAddress: "http://localhost:8686",
            results: rows,
            rootFolders: [],
            qualityProfiles: [],
            metadataProfiles: [],
            statusText: "Conectado",
            requestDownload: function () {
                replacementRequests++;
            }
        };
        wait(0);
        if (confirmation.visible && submit.enabled)
            mouseClick(submit);
        compare(replacementRequests, 0);
        compare(backend.downloads, 0);
    }
    function test_existingAlbumDoesNotRequireNewArtistOptions() {
        showResults([album("managed-42", 42)]);
        backend.rootFolders = [];
        backend.qualityProfiles = [];
        backend.metadataProfiles = [];
        const row = resultAt(0);
        verify(row.Accessible.name.includes("Já cadastrado no Lidarr"));
        verify(findChild(row, "lidarr-result-detail-0").text.includes("Já cadastrado no Lidarr"));
        mouseClick(row);
        mouseClick(findChild(shell, "lidarr-request"));
        const advice = findChild(shell, "lidarr-confirm-advice");
        verify(advice.text.includes("álbum completo já cadastrado"));
        compare(advice.textFormat, Text.PlainText);
        const submit = findChild(shell, "lidarr-confirm-submit");
        verify(submit.enabled);
        mouseClick(submit);
        compare(backend.downloads, 1);
        compare(backend.submittedDownload.key, "managed-42");
    }
    function test_serverOptionLabelsRemainPlainText() {
        showResults([album("plain-options", 0)]);
        backend.rootFolders = ["D:/<b>Music</b>"];
        backend.qualityProfiles = [
            {
                id: 7,
                name: "<b>Lossless</b>"
            }
        ];
        backend.metadataProfiles = [
            {
                id: 9,
                name: "<i>Albums</i>"
            }
        ];
        mouseClick(resultAt(0));
        mouseClick(findChild(shell, "lidarr-request"));
        const rootChoice = findChild(shell, "lidarr-root-folder");
        const qualityChoice = findChild(shell, "lidarr-quality-profile");
        const metadataChoice = findChild(shell, "lidarr-metadata-profile");
        for (const choice of [rootChoice, qualityChoice, metadataChoice])
            compare(choice.contentItem.textFormat, Text.PlainText);
        compare(qualityChoice.contentItem.text, "<b>Lossless</b>");
        qualityChoice.popup.open();
        const list = qualityChoice.popup.contentItem;
        verify(waitForPolish(shell));
        tryVerify(function () {
            return list.itemAtIndex(0) !== null;
        });
        const option = findChild(list.itemAtIndex(0), "lidarr-quality-profile-option-0");
        verify(option);
        compare(option.textFormat, Text.PlainText);
        compare(option.text, "<b>Lossless</b>");
        qualityChoice.popup.close();
    }
    function test_allOptionalHeaderActionsFitTheMinimumWindow() {
        shell.downloads = optionalDownloads;
        shell.embeddedBrowser = optionalBrowser;
        wait(0);
        const buttons = ["lidarr", "embedded-browser", "downloads", "queue", "preferences"].map(function (name) {
            return findChild(testCase.shell, name);
        });
        let previousRight = -1;
        for (const button of buttons) {
            verify(button && button.visible);
            const point = button.mapToItem(shell.contentItem, 0, 0);
            verify(point.x >= previousRight, "Header actions overlap or start outside the window");
            verify(point.x + button.width <= shell.width);
            previousRight = point.x + button.width;
        }
    }
    function test_artistResultDoesNotOfferAlbumDownload() {
        showResults([
            {
                key: "artist-1",
                kind: "artist",
                title: "Artist",
                artist: "Artist",
                year: "",
                existingId: 0
            }
        ]);
        mouseClick(resultAt(0));
        verify(!findChild(shell, "lidarr-request").enabled);
        compare(backend.downloads, 0);
    }
    function test_keyboardSelectsResultsBeyondTheVisibleRowsWithoutRequestingDownload() {
        const count = 40;
        showResults(Array.from({
            length: count
        }, (_, index) => album("album-" + index, index + 1)));
        const panel = findChild(shell, "lidarr-panel");
        const query = findChild(shell, "lidarr-query");
        const search = findChild(shell, "lidarr-search");
        const list = findChild(shell, "lidarr-results");
        const request = findChild(shell, "lidarr-request");
        query.text = "Album";
        search.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Tab);
        tryCompare(list, "activeFocus", true);
        verify(list.itemAtIndex(count - 1) === null);
        keyClick(Qt.Key_Down);
        tryCompare(list, "currentIndex", 1);
        keyClick(Qt.Key_End);
        tryCompare(list, "currentIndex", count - 1);
        tryVerify(function () {
            const row = list.itemAtIndex(count - 1);
            return row && row.mapToItem(list, 0, row.height).y <= list.height + 1;
        });
        compare(panel.selectedKey, "");
        verify(!request.enabled);
        const reordered = backend.results.slice();
        reordered.splice(2, 0, reordered.pop());
        backend.results = reordered;
        tryCompare(list, "currentIndex", 2);
        keyClick(Qt.Key_Return);
        tryCompare(panel, "selectedKey", "album-39");
        verify(request.enabled);
        compare(backend.downloads, 0);
        keyClick(Qt.Key_Tab);
        tryCompare(request, "activeFocus", true);
        keyClick(Qt.Key_Tab, Qt.ShiftModifier);
        tryCompare(list, "activeFocus", true);
        keyClick(Qt.Key_Home);
        tryCompare(list, "currentIndex", 0);
        compare(panel.selectedKey, "album-39");
        keyClick(Qt.Key_Space);
        tryCompare(panel, "selectedKey", "album-0");
        compare(backend.downloads, 0);
    }
    function test_modalBlocksPlayerShortcutsAndRestoresFocus() {
        openPanel();
        compare(shell.windowShortcutsEnabled, false);
        const muted = presenter.muted;
        const key = findChild(shell, "lidarr-api-key");
        key.forceActiveFocus();
        keyClick(Qt.Key_M);
        compare(presenter.muted, muted);
        keyClick(Qt.Key_Escape);
        tryCompare(findChild(shell, "lidarr-panel"), "visible", false);
        tryCompare(findChild(shell, "lidarr"), "activeFocus", true);
        compare(shell.windowShortcutsEnabled, true);
    }
    function test_minimumWindowKeepsActionsInsidePanelAndBusyHasCancel() {
        showResults([album("lookup-minimum", 0)]);
        const panel = findChild(shell, "lidarr-panel");
        const search = findChild(shell, "lidarr-search");
        const request = findChild(shell, "lidarr-request");
        verify(panel.width <= shell.width);
        verify(panel.height <= shell.height);
        for (const item of [search, request]) {
            const point = item.mapToItem(panel.contentItem, 0, 0);
            verify(point.x >= 0 && point.y >= 0);
            verify(point.x + item.width <= panel.contentItem.width + 1);
            verify(point.y + item.height <= panel.contentItem.height + 1);
        }
        backend.busy = true;
        const cancel = findChild(shell, "lidarr-cancel");
        verify(cancel.visible && cancel.enabled);
        mouseClick(cancel);
        compare(backend.cancels, 1);
    }
}
