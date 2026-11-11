import QtQuick
import QtQuick.Controls
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "DownloadLyricsPanel"
    when: windowShown

    property var shell: null
    property var backend: null
    property var currentBackend: null

    Component {
        id: backendComponent
        QtObject {
            id: fake
            property bool hasRequest: true
            property bool busy: false
            property bool canSave: true
            property bool canSavePasted: true
            property bool canRetry: true
            property bool canRetrySave: false
            property bool canDismiss: true
            property string title: "Original 日本語"
            property string artist: "Synthetic artist"
            property string requestToken: "request-a"
            property string statusText: "<b>Confira as versões</b>"
            property var candidates: [
                {
                    index: 42,
                    label: "<b>Original 日本語</b>",
                    text: "[00:01.001]独自のテスト\n[00:05.000]次の行",
                    synced: true
                },
                {
                    index: 7,
                    label: "Romanizada",
                    text: "<i>Dokujino tesuto</i>\nTsugino gyō",
                    synced: false
                }
            ]
            property int retryCalls: 0
            property int saveCalls: 0
            property int pasteCalls: 0
            property int retrySaveCalls: 0
            property int dismissCalls: 0
            property string submittedTitle: ""
            property string submittedArtist: ""
            property int submittedIndex: -1
            property string submittedName: ""
            property string submittedLanguage: ""
            property string submittedScript: ""
            property string submittedKind: ""
            property string submittedToken: ""
            property string submittedContent: ""
            property bool retryAccepted: true
            property bool pasteAccepted: true
            signal changed
            function retry(title, artist) {
                fake.retryCalls++;
                fake.submittedTitle = title;
                fake.submittedArtist = artist;
                if (fake.retryAccepted) {
                    fake.title = title;
                    fake.artist = artist;
                    fake.changed();
                }
                return fake.retryAccepted;
            }
            function saveCandidate(index, name, language, script, kind) {
                fake.saveCalls++;
                fake.submittedIndex = index;
                fake.submittedName = name;
                fake.submittedLanguage = language;
                fake.submittedScript = script;
                fake.submittedKind = kind;
                return true;
            }
            function savePasted(token, content, name, language, script, kind) {
                ++fake.pasteCalls;
                fake.submittedToken = token;
                fake.submittedContent = content;
                fake.submittedName = name;
                fake.submittedLanguage = language;
                fake.submittedScript = script;
                fake.submittedKind = kind;
                return fake.pasteAccepted;
            }
            function retrySave() {
                fake.retrySaveCalls++;
                return true;
            }
            function dismiss() {
                fake.dismissCalls++;
                return true;
            }
        }
    }
    Component {
        id: windowComponent
        ApplicationWindow {
            width: 720
            height: 520
            visible: true
            property alias panel: acquisitionPanel
            Button {
                id: opener
                objectName: "download-lyrics-opener"
                text: "Letras"
                onClicked: acquisitionPanel.open()
            }
            DownloadLyricsPanel {
                id: acquisitionPanel
                contract: testShellUiContract // qmllint disable unqualified
                backend: testCase.currentBackend
            }
        }
    }

    function init() {
        backend = createTemporaryObject(backendComponent, testCase);
        verify(backend);
        currentBackend = backend;
        shell = createTemporaryObject(windowComponent, null);
        verify(shell);
        shell.requestActivate();
        tryVerify(function () {
            return testCase.shell.active;
        });
        shell.panel.open();
        tryCompare(shell.panel, "opened", true);
        verify(waitForPolish(shell));
    }
    function cleanup() {
        if (shell) {
            shell.panel.close();
            shell.destroy();
        }
        shell = null;
        currentBackend = null;
        backend = null;
        wait(0);
    }
    function field(suffix) {
        const item = findChild(shell.panel, "download-lyrics-" + suffix);
        verify(item !== null, "The acquisition dialog exposes " + suffix);
        return item;
    }
    function prepareVersion(kindIndex) {
        field("version-name").text = "Versão revisada";
        field("kind").currentIndex = kindIndex;
    }
    function press(item) {
        verify(item.enabled);
        item.Accessible.pressAction();
    }

    function test_openingPreviewsWithoutLookupSaveOrDismiss() {
        compare(shell.panel.objectName, "download-lyrics-panel");
        verify(shell.panel.modal);
        verify(shell.panel.focus);
        compare(shell.panel.popupType, Popup.Item);
        compare(field("title").text, backend.title);
        compare(field("artist").text, backend.artist);
        compare(backend.retryCalls, 0);
        compare(backend.saveCalls, 0);
        compare(backend.dismissCalls, 0);
    }
    function test_allCandidateFormsAndMarkupRemainPlainText() {
        const candidates = field("candidates");
        const preview = field("preview");
        compare(candidates.count, 2);
        compare(candidates.textAt(0), backend.candidates[0].label);
        compare(candidates.contentItem.textFormat, Text.PlainText);
        compare(preview.textFormat, TextEdit.PlainText);
        verify(preview.readOnly);
        candidates.currentIndex = 0;
        compare(preview.text, backend.candidates[0].text);
        candidates.currentIndex = 1;
        compare(preview.text, backend.candidates[1].text);
        compare(field("status").text, backend.statusText);
        compare(field("status").textFormat, Text.PlainText);
        compare(backend.retryCalls, 0);
        compare(backend.saveCalls, 0);
    }
    function test_searchRequiresClickAndUsesCorrectedMetadata() {
        const title = field("title");
        const artist = field("artist");
        title.text = "Corrected title";
        artist.text = "Corrected artist";
        title.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        compare(backend.retryCalls, 0);
        press(field("search"));
        compare(backend.retryCalls, 1);
        compare(backend.submittedTitle, "Corrected title");
        compare(backend.submittedArtist, "Corrected artist");
        compare(backend.saveCalls, 0);
    }
    function test_saveRequiresNameAndAnExplicitFormChoice() {
        const save = field("save");
        const kind = field("kind");
        compare(kind.count, 2);
        compare(kind.currentIndex, -1);
        compare(kind.textAt(0), "Original");
        compare(kind.textAt(1), "Romanizada");
        verify(!save.enabled);
        field("version-name").text = "Original revisada";
        verify(!save.enabled, "The provider and script must not infer the role");
        kind.currentIndex = 0;
        verify(save.enabled);
        field("version-name").text = " \t ";
        verify(!save.enabled);
        compare(backend.saveCalls, 0);
    }
    function test_savingAnOriginalPreservesOptionalTagsAndCandidateIdentity() {
        field("candidates").currentIndex = 0;
        prepareVersion(0);
        field("language").text = "ja";
        field("script").text = "Jpan";
        press(field("save"));
        compare(backend.saveCalls, 1);
        compare(backend.submittedIndex, 42);
        compare(backend.submittedName, "Versão revisada");
        compare(backend.submittedLanguage, "ja");
        compare(backend.submittedScript, "Jpan");
        compare(backend.submittedKind, "original");
        compare(backend.retryCalls, 0);
    }
    function test_savingRomanizedAllowsEmptyLanguageAndScriptWithoutInference() {
        field("candidates").currentIndex = 1;
        prepareVersion(1);
        compare(field("language").text, "");
        compare(field("script").text, "");
        press(field("save"));
        compare(backend.saveCalls, 1);
        compare(backend.submittedIndex, 7);
        compare(backend.submittedKind, "romanized");
        compare(backend.submittedLanguage, "");
        compare(backend.submittedScript, "");
        compare(backend.retryCalls, 0);
    }
    function test_changedLookupMetadataRevokesSavingUntilAConfirmedNewSearch() {
        prepareVersion(0);
        verify(field("save").enabled);
        field("title").text = "Corrected title";
        verify(!field("save").enabled);
        field("artist").text = "Corrected artist";
        verify(!field("save").enabled);
        compare(backend.retryCalls, 0);
        press(field("search"));
        compare(backend.retryCalls, 1);
        verify(field("save").enabled);
        compare(backend.saveCalls, 0);
    }
    function test_refusedSearchDoesNotAuthorizeSavingCorrectedMetadata() {
        prepareVersion(0);
        backend.retryAccepted = false;
        field("title").text = "Unaccepted title";
        press(field("search"));
        compare(backend.retryCalls, 1);
        verify(!field("save").enabled);
        compare(backend.saveCalls, 0);
    }
    function test_retrySaveIsSeparateFromLookup() {
        backend.canSave = false;
        backend.canRetry = false;
        backend.canRetrySave = true;
        backend.changed();
        const retrySave = field("retry-save");
        verify(retrySave.visible && retrySave.enabled);
        press(retrySave);
        compare(backend.retrySaveCalls, 1);
        compare(backend.retryCalls, 0);
        compare(backend.saveCalls, 0);
    }
    function test_busyAndBackendCapabilitiesGateEveryMutatingAction() {
        prepareVersion(0);
        backend.busy = true;
        backend.canSave = false;
        backend.canRetry = false;
        backend.canDismiss = false;
        backend.canRetrySave = false;
        backend.changed();
        verify(!field("save").enabled);
        verify(!field("search").enabled);
        verify(!field("dismiss").enabled);
        verify(!field("retry-save").enabled);
        verify(field("close").enabled);
        compare(backend.retryCalls, 0);
        compare(backend.saveCalls, 0);
        compare(backend.dismissCalls, 0);
    }
    function test_finishReviewRequiresDismissCapabilityAndExplicitAction() {
        const dismiss = field("dismiss");
        compare(dismiss.text, "Concluir revisão");
        compare(dismiss.Accessible.name, dismiss.text);
        backend.canDismiss = false;
        backend.changed();
        verify(!dismiss.enabled);
        compare(backend.dismissCalls, 0);
        backend.canDismiss = true;
        backend.changed();
        press(dismiss);
        compare(backend.dismissCalls, 1);
        compare(backend.retryCalls, 0);
    }
    function test_escapeClosesPreviewWithoutDismissingOrCancellingWork() {
        backend.busy = true;
        backend.canDismiss = false;
        backend.changed();
        field("title").forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Escape);
        tryCompare(shell.panel, "visible", false);
        compare(backend.dismissCalls, 0);
        compare(backend.retryCalls, 0);
        compare(backend.saveCalls, 0);
        verify(backend.hasRequest);
    }
    function test_closeButtonOnlyClosesThePreview() {
        press(field("close"));
        tryCompare(shell.panel, "visible", false);
        compare(backend.dismissCalls, 0);
        compare(backend.retryCalls, 0);
        verify(backend.hasRequest);
    }
    function test_newRequestTokenClosesAndRevokesOldActions() {
        prepareVersion(0);
        const staleSave = field("save");
        backend.requestToken = "request-b";
        backend.changed();
        tryCompare(shell.panel, "visible", false);
        staleSave.Accessible.pressAction();
        compare(backend.saveCalls, 0);
        compare(backend.dismissCalls, 0);
    }
    function test_replacingBackendClosesAndRevokesOldActions() {
        prepareVersion(0);
        const staleSave = field("save");
        const replacement = createTemporaryObject(backendComponent, testCase);
        verify(replacement);
        currentBackend = replacement;
        tryCompare(shell.panel, "visible", false);
        staleSave.Accessible.pressAction();
        compare(backend.saveCalls, 0);
        compare(replacement.saveCalls, 0);
        compare(backend.dismissCalls, 0);
    }
    function test_losingTheRequestOrBackendClosesWithoutDismissing() {
        backend.hasRequest = false;
        backend.changed();
        tryCompare(shell.panel, "visible", false);
        compare(backend.dismissCalls, 0);
        currentBackend = null;
        shell.panel.open();
        tryCompare(shell.panel, "visible", false);
        compare(backend.retryCalls, 0);
    }
    function test_noCandidatesOrSaveCapabilityCannotSubmitAVersion() {
        prepareVersion(0);
        backend.candidates = [];
        backend.changed();
        compare(field("candidates").count, 0);
        compare(field("preview").text, "");
        verify(!field("save").enabled);
        backend.candidates = [
            {
                index: 0,
                label: "Other",
                text: "Another synthetic verse",
                synced: false
            }
        ];
        backend.canSave = false;
        backend.changed();
        verify(!field("save").enabled);
        compare(backend.saveCalls, 0);
    }
    function test_pastedLrcCanSaveWithoutLookupCandidates() {
        backend.candidates = [];
        backend.canSave = false;
        backend.canRetry = false;
        field("title").text = "An unsubmitted lookup edit";
        backend.busy = true;
        const paste = field("paste");
        const content = field("pasted-content");
        verify(!content.visible);
        paste.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        tryCompare(content, "activeFocus", true);
        verify(content.visible && !content.readOnly);
        verify(content.Accessible.name.length > 0);
        compare(content.textFormat, TextEdit.PlainText);
        const lrc = "[00:01.125]日本語 <b>literal</b>\n[00:05.000]次の行";
        content.text = lrc;
        field("version-name").text = "Japonês original";
        verify(!field("save").enabled, "Pasted text still requires an explicit form choice");
        field("kind").currentIndex = 0;
        field("language").text = "ja";
        field("script").text = "Jpan";
        press(field("save"));
        compare(backend.pasteCalls, 1);
        compare(backend.submittedToken, "request-a");
        compare(backend.submittedContent, lrc);
        compare(backend.submittedName, "Japonês original");
        compare(backend.submittedLanguage, "ja");
        compare(backend.submittedScript, "Jpan");
        compare(backend.submittedKind, "original");
        compare(backend.retryCalls, 0);
        compare(backend.saveCalls, 0);
    }
    function test_pastedDraftSurvivesRefusedSaveAndCannotRetarget() {
        backend.pasteAccepted = false;
        press(field("paste"));
        const content = field("pasted-content");
        const plain = "Dokujino tesuto\nTsugino gyō";
        content.text = plain;
        prepareVersion(1);
        const staleSave = field("save");
        press(staleSave);
        compare(backend.pasteCalls, 1);
        compare(content.text, plain);
        compare(field("version-name").text, "Versão revisada");
        compare(backend.submittedKind, "romanized");
        backend.requestToken = "request-b";
        tryCompare(shell.panel, "visible", false);
        staleSave.Accessible.pressAction();
        compare(backend.pasteCalls, 1);
        shell.panel.open();
        tryCompare(shell.panel, "opened", true);
        press(field("paste"));
        compare(content.text, "");
        verify(!field("save").enabled);
    }
    function test_dialogFitsTheMinimumWindowAndItsFormCanScroll() {
        const panel = shell.panel;
        const position = panel.contentItem.mapToItem(shell.contentItem, 0, 0);
        verify(position.x >= 0 && position.y >= 0);
        verify(panel.width <= shell.width);
        verify(panel.height <= shell.height);
        verify(position.x + panel.contentItem.width <= shell.width + 0.5);
        verify(position.y + panel.contentItem.height <= shell.height + 0.5);
        const scroll = field("scroll");
        verify(scroll.clip);
        verify(scroll.contentHeight > scroll.availableHeight, "All form controls remain reachable by scrolling");
        compare(field("save").text, "Guardar como nova versão");
    }
}
