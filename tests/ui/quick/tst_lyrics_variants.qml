import QtQuick
import QtQuick.Controls
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "LyricsVariants"
    when: windowShown
    readonly property var driver: testScenarioDriver // qmllint disable unqualified
    readonly property PlayerPresenter presenter: testLocalPlayerPresenter // qmllint disable unqualified
    readonly property var backend: lyricsBackend
    property var shell
    QtObject {
        id: lyricsBackend
        property string title: "Test song"
        property string artist: "Artist"
        property string statusText: "Letra atual"
        property string currentLine: "<b>Service verse</b>"
        property string previousLine: ""
        property string nextLine: ""
        property string plainText: ""
        property bool hasSyncedLyrics: true
        property bool busy: false
        property bool canSearch: true
        property var onlineLyricsCandidates: []
        property int onlineCandidateIndex: -1
        readonly property int selectedOnlineLyricsCandidate: selectedVariantId.length === 0 ? onlineCandidateIndex : -1
        property string currentLyricsRevision: "local-1"
        property var variants: []
        property string selectedVariantId: ""
        property bool variantsBusy: false
        property bool variantsReady: true
        property string variantsStatusText: "Versões guardadas"
        property bool variantsCanEdit: true
        property bool variantsCanRetrySave: false
        property bool variantsHasPendingChange: false
        property bool canSaveCurrentLyrics: true
        property var calls: []
        function record(action, args) {
            calls = calls.concat([
                {
                    action: action,
                    args: args
                }
            ]);
            return true;
        }
        function search(title, artist) {
            return record("search", [title, artist]);
        }
        function selectVariant(id) {
            selectedVariantId = id;
            currentLine = id === "9007199254740993" ? "Romanized verse" : (id.length ? "日本語の行" : "<b>Service verse</b>");
            return record("select", [id]);
        }
        function selectOnlineLyricsCandidate(index) {
            onlineCandidateIndex = index;
            currentLyricsRevision = "online-" + index;
            hasSyncedLyrics = index === 0;
            currentLine = hasSyncedLyrics ? "Letra sincronizada" : "";
            plainText = hasSyncedLyrics ? "Letra sincronizada" : "<b>Outra letra</b>";
            return record("selectOnline", [index]);
        }
        function addVariant(name, language, script, source, kind, content, generated) {
            return record("add", [name, language, script, source, kind, content, generated]);
        }
        function saveCurrentLyrics(name, language, script, kind, revision) {
            return record("saveCurrent", [name, language, script, kind, selectedOnlineLyricsCandidate, revision]);
        }
        function removeVariant(id) {
            return record("remove", [id]);
        }
        function retryVariantsSave() {
            return record("retry", []);
        }
        function reloadVariants() {
            return record("reload", []);
        }
    }
    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testCase.presenter
            lyrics: testCase.backend
        }
    }
    function init() {
        driver.resetLocalScenario();
        backend.currentLine = "<b>Service verse</b>";
        backend.hasSyncedLyrics = true;
        backend.plainText = "";
        backend.onlineLyricsCandidates = [];
        backend.onlineCandidateIndex = -1;
        backend.currentLyricsRevision = "local-1";
        backend.variantsBusy = false;
        backend.variantsReady = true;
        backend.variantsCanEdit = true;
        backend.variantsCanRetrySave = false;
        backend.variantsHasPendingChange = false;
        backend.canSaveCurrentLyrics = true;
        backend.selectedVariantId = "";
        backend.variants = [
            {
                id: "1",
                name: "<b>日本語 Original</b>",
                language: "ja",
                script: "Jpan",
                source: "<i>Local source</i>",
                kind: "original",
                generated: false
            },
            {
                id: "9007199254740993",
                name: "Romaji",
                language: "ja",
                script: "Latn",
                source: "User",
                kind: "romanized",
                generated: true
            }
        ];
        backend.calls = [];
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.width = 720;
        shell.height = 520;
        shell.requestActivate();
        tryVerify(function () {
            return testCase.shell.active;
        });
        driver.enqueueLocalSelection(["C:/Music/lyrics-variants.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
    }
    function cleanup() {
        if (shell)
            shell.destroy();
        shell = null;
        wait(0);
        driver.resetLocalScenario();
    }
    function openPanel() {
        const opener = findChild(shell, "lyrics-versions");
        verify(opener && opener.visible, "Versions action must be available for a variants backend");
        mouseClick(opener);
        const panel = findChild(shell, "lyrics-variants-panel");
        verify(panel);
        tryCompare(panel, "visible", true);
        verify(waitForPolish(shell));
        return panel;
    }
    function rowAt(index) {
        const list = findChild(shell, "lyrics-variants-list");
        verify(list);
        list.positionViewAtIndex(index, ListView.Contain);
        verify(waitForPolish(shell));
        tryVerify(function () {
            return list.itemAtIndex(index) !== null;
        });
        return list.itemAtIndex(index);
    }
    function lastCall(action) {
        verify(backend.calls.length > 0);
        const call = backend.calls[backend.calls.length - 1];
        compare(call.action, action);
        return call;
    }
    function openAddForm() {
        openPanel();
        mouseClick(findChild(shell, "lyrics-variant-add"));
        tryCompare(findChild(shell, "lyrics-variant-form"), "visible", true);
        verify(waitForPolish(shell));
    }
    function showOnlineCandidates() {
        backend.onlineLyricsCandidates = [
            {
                index: 0,
                label: "Sincronizada"
            },
            {
                index: 1,
                label: "Texto simples"
            }
        ];
        backend.onlineCandidateIndex = 0;
        backend.currentLyricsRevision = "online-0";
        backend.plainText = "Letra sincronizada";
    }
    function chooseOnlineCandidate(choice, index) {
        choice.forceActiveFocus(Qt.TabFocusReason);
        tryCompare(choice, "activeFocus", true);
        keyClick(Qt.Key_Space);
        tryCompare(choice.popup, "opened", true);
        keyClick(Qt.Key_Home);
        if (index === 1)
            keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Return);
        tryCompare(choice.popup, "visible", false);
        compare(lastCall("selectOnline").args[0], index);
    }
    function test_openingDoesNotSaveSearchOrChangePlaybackAndUsesPlainText() {
        const state = presenter.playbackState;
        const current = presenter.currentOccurrenceId;
        const panel = openPanel();
        compare(backend.calls.length, 0);
        compare(presenter.playbackState, state);
        compare(presenter.currentOccurrenceId, current);
        compare(shell.windowShortcutsEnabled, false);
        compare(panel.contentItem.Accessible.role, Accessible.Dialog);
        compare(panel.contentItem.Accessible.name, "Versões da letra");
        const label = findChild(rowAt(1), "lyrics-variant-name");
        compare(label.text, "<b>日本語 Original</b>");
        compare(label.textFormat, Text.PlainText);
    }
    function test_selectingRomanizedAndCurrentVersionsPreservesExactIdsAndAudio() {
        const current = presenter.currentOccurrenceId;
        openPanel();
        mouseClick(rowAt(2));
        compare(lastCall("select").args[0], "9007199254740993");
        compare(findChild(shell, "lyrics-current-line").text, "Romanized verse");
        compare(presenter.currentOccurrenceId, current);
        compare(presenter.playbackState, PlayerPresenter.Playing);
        mouseClick(rowAt(0));
        compare(lastCall("select").args[0], "");
        compare(findChild(shell, "lyrics-current-line").text, "<b>Service verse</b>");
    }
    function test_addingLaterKeepsLanguageScriptSourceAndPlainTextSeparate() {
        openAddForm();
        const form = findChild(shell, "lyrics-variant-form");
        compare(form.contentItem.Accessible.role, Accessible.Dialog);
        findChild(shell, "lyrics-variant-name-input").text = "Romanizada 日本";
        findChild(shell, "lyrics-variant-language-input").text = "ja";
        findChild(shell, "lyrics-variant-script-input").text = "Latn";
        findChild(shell, "lyrics-variant-source-input").text = "Local source";
        findChild(shell, "lyrics-variant-kind-input").currentIndex = 1;
        findChild(shell, "lyrics-variant-content-input").text = "[00:01.000]<b>Romaji</b>";
        findChild(shell, "lyrics-variant-generated-input").checked = true;
        mouseClick(findChild(shell, "lyrics-variant-submit"));
        const call = lastCall("add");
        compare(call.args.join("|"), "Romanizada 日本|ja|Latn|Local source|romanized|[00:01.000]<b>Romaji</b>|true");
        compare(backend.variants.length, 2); // UI cannot publish an unacknowledged save.
    }
    function test_savingCurrentLyricsDoesNotAskForReplacementText() {
        openPanel();
        mouseClick(findChild(shell, "lyrics-variant-save-current"));
        tryCompare(findChild(shell, "lyrics-variant-form"), "visible", true);
        findChild(shell, "lyrics-variant-name-input").text = "Original guardada";
        compare(findChild(shell, "lyrics-variant-content-input").visible, false);
        mouseClick(findChild(shell, "lyrics-variant-submit"));
        const saved = lastCall("saveCurrent");
        compare(saved.args[0], "Original guardada");
        compare(saved.args[5], "local-1");
    }

    function test_onlineCandidatesCanBeChosenAndSavedWithoutChangingPlayback() {
        const occurrence = presenter.currentOccurrenceId;
        showOnlineCandidates();
        openPanel();
        const choice = findChild(shell, "lyrics-online-candidates");
        verify(choice);
        tryCompare(choice, "visible", true);
        compare(choice.count, 2);
        compare(choice.currentIndex, 0);
        compare(choice.Accessible.name, "Resultado da busca");
        compare(choice.contentItem.textFormat, Text.PlainText);
        const preview = findChild(shell, "lyrics-online-preview");
        verify(preview && preview.visible);
        compare(preview.textFormat, Text.PlainText);
        compare(backend.calls.length, 0);

        for (const index of [1, 0]) {
            chooseOnlineCandidate(choice, index);
            compare(choice.currentIndex, index);
            compare(preview.text, index === 1 ? "<b>Outra letra</b>" : "Letra sincronizada");
            if (index === 1) {
                const plainLyrics = findChild(shell, "lyrics-plain-text");
                compare(plainLyrics.text, "<b>Outra letra</b>");
                compare(plainLyrics.textFormat, Text.PlainText);
            } else {
                compare(findChild(shell, "lyrics-current-line").text, "Letra sincronizada");
            }
            mouseClick(findChild(shell, "lyrics-variant-save-current"));
            const form = findChild(shell, "lyrics-variant-form");
            tryCompare(form, "visible", true);
            const name = "Resultado " + index;
            findChild(shell, "lyrics-variant-name-input").text = name;
            mouseClick(findChild(shell, "lyrics-variant-submit"));
            const saved = lastCall("saveCurrent");
            compare(saved.args[0], name);
            compare(saved.args[4], index);
            compare(saved.args[5], "online-" + index);
            tryCompare(form, "visible", false);
        }
        compare(backend.calls.length, 4);
        compare(backend.variants.length, 2);
        compare(presenter.currentOccurrenceId, occurrence);
        compare(presenter.playbackState, PlayerPresenter.Playing);
    }

    function test_onlineCandidateChoiceIsHiddenForSingleResultsAndSavedVersions() {
        showOnlineCandidates();
        backend.onlineLyricsCandidates = [backend.onlineLyricsCandidates[0]];
        openPanel();
        const choice = findChild(shell, "lyrics-online-candidates");
        verify(choice);
        const preview = findChild(shell, "lyrics-online-preview");
        verify(preview);
        compare(choice.visible, false);
        compare(preview.visible, false);
        compare(findChild(shell, "lyrics-variant-save-current").enabled, true);
        showOnlineCandidates();
        tryCompare(choice, "visible", true);
        mouseClick(rowAt(1));
        compare(lastCall("select").args[0], "1");
        tryCompare(choice, "visible", false);
        compare(preview.visible, false);
        mouseClick(rowAt(0));
        compare(lastCall("select").args[0], "");
        tryCompare(choice, "visible", true);
        compare(backend.calls.length, 2);
    }

    function test_newOnlineSearchCannotRetargetAnOpenSaveForm() {
        showOnlineCandidates();
        openPanel();
        mouseClick(findChild(shell, "lyrics-variant-save-current"));
        const form = findChild(shell, "lyrics-variant-form");
        tryCompare(form, "visible", true);
        findChild(shell, "lyrics-variant-name-input").text = "Resposta antiga";
        const submit = findChild(shell, "lyrics-variant-submit");

        backend.currentLyricsRevision = "same-index-new-response";
        compare(backend.selectedOnlineLyricsCandidate, 0);
        tryCompare(form, "visible", false);
        submit.Accessible.pressAction();
        compare(backend.calls.length, 0);
    }
    function test_removalRequiresConfirmationOfTheSameVariant() {
        backend.selectedVariantId = "1";
        openPanel();
        mouseClick(findChild(shell, "lyrics-variant-remove"));
        compare(backend.calls.length, 0);
        const confirm = findChild(shell, "lyrics-variant-confirm-submit");
        verify(confirm.visible && confirm.enabled);
        compare(findChild(shell, "lyrics-variant-confirm").contentItem.Accessible.role, Accessible.Dialog);
        mouseClick(confirm);
        compare(lastCall("remove").args[0], "1");
    }
    function test_changedSelectionCannotRedirectAConfirmedRemoval() {
        backend.selectedVariantId = "1";
        openPanel();
        mouseClick(findChild(shell, "lyrics-variant-remove"));
        backend.selectedVariantId = "9007199254740993";
        wait(0);
        const confirm = findChild(shell, "lyrics-variant-confirm-submit");
        verify(!confirm.enabled);
        confirm.Accessible.pressAction();
        compare(backend.calls.length, 0);
    }
    function test_trackChangeRevokesFormsAndLateClicks() {
        openAddForm();
        findChild(shell, "lyrics-variant-name-input").text = "Old target";
        findChild(shell, "lyrics-variant-content-input").text = "Text";
        const submit = findChild(shell, "lyrics-variant-submit");
        const lyricsPanel = findChild(shell, "lyrics-panel");
        lyricsPanel.trackIdentity = "new-occurrence";
        tryCompare(findChild(shell, "lyrics-variants-panel"), "visible", false);
        tryCompare(findChild(shell, "lyrics-variant-form"), "visible", false);
        submit.Accessible.pressAction();
        compare(backend.calls.length, 0);
    }
    function test_backendReplacementRevokesAnOldFormOwner() {
        openAddForm();
        findChild(shell, "lyrics-variant-name-input").text = "Old backend";
        findChild(shell, "lyrics-variant-content-input").text = "Text";
        const submit = findChild(shell, "lyrics-variant-submit");
        shell.lyrics = null;
        tryCompare(findChild(shell, "lyrics-variant-form"), "visible", false);
        submit.Accessible.pressAction();
        compare(backend.calls.length, 0);
    }
    function test_busyAndPendingChangesBlockMutationsButAllowExplicitRetry() {
        backend.variantsBusy = true;
        backend.variantsCanEdit = false;
        openPanel();
        compare(findChild(shell, "lyrics-variant-add").enabled, false);
        compare(findChild(shell, "lyrics-variant-save-current").enabled, false);
        backend.variantsBusy = false;
        backend.variantsHasPendingChange = true;
        backend.variantsCanRetrySave = true;
        const retry = findChild(shell, "lyrics-variant-retry");
        const reload = findChild(shell, "lyrics-variant-reload");
        tryCompare(retry, "visible", true);
        verify(waitForPolish(shell));
        compare(retry.enabled, true);
        verify(retry.width > 0 && retry.height > 0);
        const retryPosition = retry.mapToItem(shell.contentItem, 0, 0);
        const reloadPosition = reload.mapToItem(shell.contentItem, 0, 0);
        verify(retryPosition.x + retry.width <= reloadPosition.x, "Retry and reload actions must occupy separate targets");
        mouseClick(retry);
        lastCall("retry");
    }
    function test_reloadingPendingChangeRequiresExplicitConfirmation() {
        backend.variantsCanEdit = false;
        backend.variantsHasPendingChange = true;
        backend.variantsCanRetrySave = true;
        openPanel();
        mouseClick(findChild(shell, "lyrics-variant-reload"));
        compare(backend.calls.length, 0);
        mouseClick(findChild(shell, "lyrics-variant-confirm-submit"));
        lastCall("reload");
    }
    function test_clearingTheQueueKeepsPendingRecoveryAccessibleWithoutClaimingADiskReload() {
        backend.variantsCanEdit = false;
        backend.variantsHasPendingChange = true;
        backend.variantsCanRetrySave = true;
        const panel = openPanel();
        verify(presenter.clearQueue());
        tryCompare(presenter, "hasCurrentTrack", false);
        tryCompare(findChild(shell, "lyrics-panel"), "visible", true);
        tryCompare(panel, "visible", false); // Changing the target revokes the old modal.
        openPanel();
        mouseClick(findChild(shell, "lyrics-variant-retry"));
        lastCall("retry");
        const abandon = findChild(shell, "lyrics-variant-reload");
        compare(abandon.text, "Abandonar alteração");
        compare(abandon.enabled, true);
        mouseClick(abandon);
        compare(backend.calls.length, 1); // Abandonment still needs confirmation.
        const confirmation = findChild(shell, "lyrics-variant-confirm");
        tryCompare(confirmation, "visible", true);
        compare(confirmation.contentItem.Accessible.name, "Abandonar alteração pendente");
        const prompt = findChild(shell, "lyrics-variant-confirm-text");
        verify(prompt.text.indexOf("Não há faixa selecionada para recarregar") >= 0);
        verify(prompt.text.indexOf("gravação sem confirmação pode já ter sido efetivada") >= 0);
        const submit = findChild(shell, "lyrics-variant-confirm-submit");
        compare(submit.text, "Abandonar");
        mouseClick(submit);
        lastCall("reload");
    }
    function test_singleAvailableVersionNeedsNoRomanizedCompanion() {
        backend.variants = [backend.variants[0]];
        openPanel();
        compare(findChild(shell, "lyrics-variants-list").count, 2);
        mouseClick(rowAt(1));
        compare(lastCall("select").args[0], "1");
    }
    function test_minimumWindowContainsThePanelAndScrollableEditor() {
        const panel = openPanel();
        const panelTop = panel.contentItem.mapToItem(shell.contentItem, -panel.leftPadding, -panel.topPadding);
        verify(panelTop.x >= 0 && panelTop.y >= 0);
        verify(panelTop.x + panel.width <= shell.width);
        verify(panelTop.y + panel.height <= shell.height);
        mouseClick(findChild(shell, "lyrics-variant-add"));
        const form = findChild(shell, "lyrics-variant-form");
        verify(form.height <= shell.height);
        const scroll = findChild(shell, "lyrics-variant-form-scroll");
        verify(scroll && scroll.visible);
        const submit = findChild(shell, "lyrics-variant-submit");
        const submitTop = submit.mapToItem(shell.contentItem, 0, 0);
        verify(submitTop.y >= 0 && submitTop.y + submit.height <= shell.height);
    }
    function test_keyboardEscapeReturnsFocusAndCannotTogglePlayback() {
        openAddForm();
        const name = findChild(shell, "lyrics-variant-name-input");
        tryCompare(name, "activeFocus", true);
        keyClick(Qt.Key_Escape);
        tryCompare(findChild(shell, "lyrics-variant-form"), "visible", false);
        compare(presenter.playbackState, PlayerPresenter.Playing);
        keyClick(Qt.Key_Escape);
        tryCompare(findChild(shell, "lyrics-variants-panel"), "visible", false);
        tryCompare(findChild(shell, "lyrics-versions"), "activeFocus", true);
    }
    function test_keyboardCanChooseASavedVersionWithoutTransportShortcuts() {
        openPanel();
        const row = rowAt(2);
        row.forceActiveFocus(Qt.TabFocusReason);
        tryCompare(row, "activeFocus", true);
        keyClick(Qt.Key_Return);
        compare(lastCall("select").args[0], "9007199254740993");
        compare(presenter.playbackState, PlayerPresenter.Playing);
    }
}
