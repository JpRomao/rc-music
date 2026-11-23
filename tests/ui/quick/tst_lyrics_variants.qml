import QtQuick
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
        property string album: "Test album"
        property int durationMs: 123000
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
        property bool canGenerateRomaji: false
        property string romajiSourceRevision: ""
        property string romajiStatusText: ""
        signal romajiDraftReady(string token, string content)
        property bool queueVariantAvailable: true
        property string queuedToken: ""
        property string queuedVariantStatusText: ""
        property bool queuedManagerBusy: false
        property bool queuedManagerCanEdit: true
        property bool queuedManagerSearchAvailable: true
        property var queuedLyricsMetadata: ({})
        property string queuedManagerStatus: "Versões da faixa na fila"
        property var queuedManagerVariants: []
        property var openQueuedManager: null
        property var closeQueuedManager: null
        property var prepareQueuedVariantEdit: null
        property var removeQueuedVariant: null
        property var queuedRomajiSource: null
        property var generateQueuedRomaji: null
        property bool recordingSearchBusy: false
        property string recordingSearchStatus: ""
        property string recordingSearchRevision: "matches-7"
        property var recordingCandidates: []
        property bool canSaveRecording: true
        property string pendingRecordingSaveRevision: ""
        property bool failRecordingSave: false
        property var calls: []
        signal importedVariantLrc(string token, string name, string content)
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
        function searchRecordings(title, artist, album, durationMs) {
            recordingSearchBusy = true;
            return record("searchRecordings", [title, artist, album, durationMs]);
        }
        function searchQueuedRecordings(token, title, artist, album, durationMs) {
            recordingSearchBusy = true;
            return record("searchQueuedRecordings", [token, title, artist, album, durationMs]);
        }
        function previewRecording(index, format, revision) {
            return index === 0 && format === 1 && revision === recordingSearchRevision ? "<i>Text preview</i>" : "";
        }
        function saveManagedRecording(index, format, name, revision) {
            variantsStatusText = "Versão da letra salva.";
            return record("saveManagedRecording", [index, format, name, revision]);
        }
        function saveQueuedRecording(token, index, format, name, revision) {
            variantsHasPendingChange = failRecordingSave;
            variantsCanRetrySave = failRecordingSave;
            pendingRecordingSaveRevision = failRecordingSave ? revision : "";
            queuedManagerStatus = failRecordingSave ? "A gravação falhou." : "Versão da letra salva.";
            return record("saveQueuedRecording", [token, index, format, name, revision]);
        }
        function retryRecordingSave(revision) {
            if (revision !== pendingRecordingSaveRevision)
                return false;
            queuedManagerStatus = "Versão da letra salva.";
            pendingRecordingSaveRevision = "";
            variantsHasPendingChange = false;
            variantsCanRetrySave = false;
            return record("retryRecordingSave", [revision]);
        }
        function cancelRecordingSearch() {
            recordingSearchBusy = false;
            recordingCandidates = [];
            record("cancelSearchRecordings", []);
        }
        function selectVariant(id) {
            selectedVariantId = id;
            currentLine = id === "off" ? "" : id === "9007199254740993" ? "Romanized verse" : (id.length ? "日本語の行" : "<b>Service verse</b>");
            hasSyncedLyrics = id !== "off";
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
        function prepareQueuedVariant(occurrenceId) {
            record("prepareQueued", [occurrenceId]);
            queuedToken = "opaque-variant-token";
            return queuedToken;
        }
        function addQueuedVariant(token, name, language, script, source, kind, content, generated) {
            if (token.length === 0 || token !== queuedToken)
                return false;
            queuedToken = "";
            variantsHasPendingChange = true;
            variantsBusy = true;
            queuedVariantStatusText = "Salvando versão da letra…";
            return record("addQueued", [token, name, language, script, source, kind, content, generated]);
        }
        function cancelQueuedVariant(token) {
            if (token.length > 0 && token === queuedToken) {
                queuedToken = "";
                record("cancelQueued", [token]);
            }
        }
        function saveCurrentLyrics(name, language, script, kind, revision) {
            return record("saveCurrent", [name, language, script, kind, selectedOnlineLyricsCandidate, revision]);
        }
        function generateRomaji() {
            record("generateRomaji", []);
            return "romaji-token";
        }
        function saveRomaji(token, name, content) {
            return record("saveRomaji", [token, name, content]);
        }
        function discardRomaji(token) {
            record("discardRomaji", [token]);
        }
        function removeVariant(id) {
            return record("remove", [id]);
        }
        function prepareVariantEdit(id) {
            record("prepareEdit", [id]);
            const variant = variants.find(row => row.id === id);
            return variant ? Object.assign({}, variant, {
                token: "edit-" + id,
                content: "[00:01.00]Saved verse",
                synchronized: true
            }) : {};
        }
        function saveVariantEdit(token, name, language, script, kind, content, asNew, editTiming) {
            return record("saveEdit", [token, name, language, script, kind, content, asNew, editTiming]);
        }
        function cancelVariantEdit(token) {
            record("cancelEdit", [token]);
        }
        function importVariantLrc(file, track, token) {
            return record("importLrc", [file.toString(), track, token]);
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
        backend.openQueuedManager = null;
        backend.closeQueuedManager = null;
        backend.queuedRomajiSource = null;
        backend.generateQueuedRomaji = null;
        backend.prepareQueuedVariantEdit = null;
        backend.removeQueuedVariant = null;
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
        backend.variantsStatusText = "Versões guardadas";
        backend.queueVariantAvailable = true;
        backend.queuedToken = "";
        backend.queuedVariantStatusText = "";
        backend.queuedManagerCanEdit = true;
        backend.queuedManagerSearchAvailable = true;
        backend.queuedLyricsMetadata = {};
        backend.queuedManagerStatus = "Versões da faixa na fila";
        backend.recordingSearchBusy = false;
        backend.recordingCandidates = [];
        backend.pendingRecordingSaveRevision = "";
        backend.failRecordingSave = false;
        backend.canSaveCurrentLyrics = true;
        backend.canGenerateRomaji = false;
        backend.romajiSourceRevision = "";
        backend.romajiStatusText = "";
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
    function openManagerSearch() {
        const action = findChild(shell, "lyrics-variant-search-lrclib");
        verify(action && action.visible && action.enabled, "Gerenciar letras oferece busca no LRCLIB");
        mouseClick(action);
        const dialog = findChild(shell, "lyrics-query-dialog");
        tryCompare(dialog, "opened", true);
        return dialog;
    }
    function showRecordingForManager() {
        backend.recordingSearchBusy = false;
        backend.recordingCandidates = [
            {
                id: "9",
                title: "Match 1",
                artist: "Result artist",
                durationMs: 123000,
                album: "Result album",
                formats: [
                    {
                        label: "Sincronizada"
                    },
                    {
                        label: "Texto simples"
                    }
                ]
            }
        ];
        const recording = findChild(shell, "lyrics-recording-choice");
        tryCompare(recording, "count", 2);
        recording.activated(1);
        const format = findChild(shell, "lyrics-recording-format");
        tryCompare(format, "visible", true);
        format.activated(2);
        const preview = findChild(shell, "lyrics-recording-preview");
        compare(preview.text, "<i>Text preview</i>");
        compare(preview.textFormat, Text.PlainText);
    }
    function test_currentManagerOffersExplicitLrclibPreviewAndSaveOnly() {
        const manager = openPanel();
        const dialog = openManagerSearch();
        compare(backend.calls.length, 0);
        compare(findChild(shell, "lyrics-query-title").text, "Test song");
        compare(findChild(shell, "lyrics-query-album").text, "Test album");
        compare(findChild(shell, "lyrics-query-duration").text, "123");
        mouseClick(findChild(shell, "lyrics-query-submit"));
        compare(lastCall("searchRecordings").args, ["Test song", "Artist", "Test album", 123000]);
        showRecordingForManager();
        verify(!findChild(shell, "lyrics-recording-use").visible);
        compare(backend.calls.length, 1);
        mouseClick(findChild(shell, "lyrics-recording-save"));
        compare(lastCall("saveManagedRecording").args, [0, 1, "Match 1", "matches-7"]);
        compare(backend.currentLine, "<b>Service verse</b>");
        keyClick(Qt.Key_Escape);
        tryCompare(dialog, "opened", false);
        tryCompare(manager, "opened", true);
        tryCompare(findChild(shell, "lyrics-variant-search-lrclib"), "activeFocus", true);
    }
    function test_queueManagerSearchSurvivesPlaybackAdvanceAndRetryDoesNotQueryAgain() {
        backend.queuedLyricsMetadata = {
            title: "Título oficial",
            artist: "Artista da faixa",
            album: "Álbum original",
            durationMs: 123000
        };
        backend.openQueuedManager = occurrence => {
            backend.record("openManager", [occurrence]);
            return "queue-manager";
        };
        const menu = openQueueVariantMenu();
        menu.action.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        const manager = findChild(shell, "lyrics-variants-panel");
        tryCompare(manager, "visible", true);
        const dialog = openManagerSearch();
        compare(lastCall("openManager").args[0], menu.target);
        verify(menu.title !== "Título oficial");
        compare(findChild(shell, "lyrics-query-title").text, "Título oficial");
        compare(findChild(shell, "lyrics-query-artist").text, "Artista da faixa");
        mouseClick(findChild(shell, "lyrics-query-submit"));
        compare(lastCall("searchQueuedRecordings").args, ["queue-manager", "Título oficial", "Artista da faixa", "Álbum original", 123000]);
        const previous = presenter.currentOccurrenceId;
        driver.publishLocalSourceUnavailable();
        tryVerify(() => presenter.currentOccurrenceId !== previous && presenter.currentOccurrenceId !== menu.target);
        driver.publishLocalReady();
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        tryCompare(dialog, "opened", true);
        showRecordingForManager();
        verify(!findChild(shell, "lyrics-recording-use").visible);
        backend.failRecordingSave = true;
        mouseClick(findChild(shell, "lyrics-recording-save"));
        compare(lastCall("saveQueuedRecording").args, ["queue-manager", 0, 1, "Match 1", "matches-7"]);
        const retry = findChild(shell, "lyrics-recording-retry-save");
        tryCompare(retry, "visible", true);
        verify(!findChild(shell, "lyrics-recording-choice").enabled);
        mouseClick(retry);
        compare(lastCall("retryRecordingSave").args, ["matches-7"]);
        verify(!retry.visible);
        verify(findChild(shell, "lyrics-recording-choice").enabled);
        backend.variantsCanRetrySave = true;
        backend.pendingRecordingSaveRevision = "unrelated-write";
        verify(!retry.visible);
        compare(backend.calls.filter(call => call.action === "searchQueuedRecordings").length, 1);
        compare(backend.currentLine, "<b>Service verse</b>");
        backend.queuedManagerSearchAvailable = false;
        tryCompare(dialog, "opened", false);
        tryCompare(manager, "opened", true);
        verify(!findChild(shell, "lyrics-variant-search-lrclib").enabled);
    }
    function test_queueSearchLeavesMissingTagsBlankUntilEdited() {
        backend.openQueuedManager = () => "untagged-manager";
        const menu = openQueueVariantMenu();
        menu.action.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        const dialog = openManagerSearch();
        const title = findChild(shell, "lyrics-query-title");
        const artist = findChild(shell, "lyrics-query-artist");
        const submit = findChild(shell, "lyrics-query-submit");
        verify(dialog.opened);
        compare(title.text, "");
        compare(artist.text, "");
        verify(!submit.enabled);
        title.text = "Título informado";
        artist.text = "Artista informado";
        mouseClick(submit);
        compare(lastCall("searchQueuedRecordings").args, ["untagged-manager", "Título informado", "Artista informado", "", 0]);
    }
    function openPanel() {
        const lyricsPanel = findChild(shell, "lyrics-panel");
        verify(lyricsPanel.variantsAvailable, "Versions must be available for a variants backend");
        // The real entry point: the player's lyrics button opens the drawer, whose toolbar opens the versions.
        const reader = findChild(shell, "lyrics-reader");
        if (!reader.visible) {
            const player = findChild(shell, "player-lyrics");
            tryVerify(() => player.enabled);
            mouseClick(player);
            tryCompare(reader, "opened", true);
        }
        const versions = findChild(shell, "lyrics-drawer-versions");
        tryVerify(() => versions.visible && versions.enabled);
        // The toolbar lays out on the next frame: the romaji button must have left the first button's place.
        const more = findChild(shell, "lyrics-drawer-more");
        tryVerify(() => !more.visible || more.mapToItem(reader.contentItem, 0, 0).x > versions.mapToItem(reader.contentItem, 0, 0).x);
        mouseClick(versions);
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
    function openQueueVariantMenu() {
        driver.enqueueLocalSelection(["C:/Music/Segunda.flac", "C:/Music/日本語.flac"]);
        verify(presenter.addAudio());
        const list = findChild(shell, "queue-list");
        tryCompare(list, "count", 3);
        tryCompare(presenter, "selectingAudioFiles", false);
        shell.requestActivate();
        tryCompare(shell, "active", true);
        shell.setQueueOpen(true);
        tryCompare(list, "visible", true);
        wait(0);
        list.forceActiveFocus(Qt.TabFocusReason);
        tryCompare(list, "activeFocus", true);
        list.currentIndex = 2;
        list.positionViewAtIndex(2, ListView.Contain);
        list.forceLayout();
        const target = list.currentItem.occurrenceId;
        const title = list.currentItem.title;
        list.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Menu);
        tryCompare(findChild(shell, "queue-row-menu"), "opened", true);
        const action = findChild(shell, "queue-menu-add-lyrics-variant");
        verify(action && action.visible, "Queue row menu must offer a lyric variant");
        return {
            list: list,
            target: target,
            title: title,
            action: action
        };
    }
    function activateQueuedVariantAction(menu) {
        verify(menu.action.enabled);
        menu.action.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        const form = findChild(shell, "lyrics-variant-form");
        tryCompare(form, "visible", true);
        tryCompare(findChild(shell, "lyrics-variant-name-input"), "activeFocus", true);
        compare(lastCall("prepareQueued").args[0], menu.target);
        return form;
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
    function test_verseLineShowsPendingChangeAndReview() {
        backend.variantsHasPendingChange = true;
        backend.variantsBusy = false;
        backend.variantsStatusText = "Salvando versão da letra…";
        const status = findChild(shell, "lyrics-line-status");
        const review = findChild(shell, "lyrics-line-review");
        tryVerify(() => status.visible && review.visible);
        compare(status.text, "Salvando versão da letra…");
        compare(review.text, "Revisar alterações");
        compare(review.Accessible.name, "Revisar alterações não salvas da letra");
        verify(!findChild(shell, "lyrics-line-open").visible, "the notice has precedence over the verse");
        mouseClick(review);
        tryCompare(findChild(shell, "lyrics-variants-panel"), "visible", true);
        backend.variantsHasPendingChange = false;
        backend.variantsStatusText = "Versões guardadas";
        findChild(shell, "lyrics-variants-panel").close();
        tryVerify(() => findChild(shell, "lyrics-line-open").visible);
        tryCompare(findChild(shell, "lyrics-line-open"), "activeFocus", true);
    }
    function test_compactVerseLineKeepsTheNoticeAndItsReviewButton() {
        shell.width = 720;
        shell.height = 520;
        tryCompare(shell.contentItem, "width", 720);
        backend.variantsHasPendingChange = true;
        backend.variantsBusy = false;
        backend.variantsStatusText = "Salvando versão da letra…";
        const line = findChild(shell, "lyrics-line");
        const status = findChild(shell, "lyrics-line-status");
        const review = findChild(shell, "lyrics-line-review");
        tryVerify(() => status.visible && review.visible);
        verify(waitForPolish(shell));
        compare(line.height, 40);
        compare(status.text, "Salvando versão da letra…");
        compare(review.text, "Revisar alterações");
        compare(review.Accessible.name, "Revisar alterações não salvas da letra");
        const open = findChild(shell, "lyrics-line-open");
        verify(!open || !open.visible, "the notice has precedence over the verse");
        verify(!findChild(shell, "lyrics-line-current").visible);
        const corner = review.mapToItem(line, review.width, review.height);
        verify(corner.x <= line.width && corner.y <= line.height, "the review button stays inside the compact line");
        const statusEdge = status.mapToItem(line, status.width, 0);
        verify(statusEdge.x <= review.mapToItem(line, 0, 0).x + 1, "the status does not run under the button");
        mouseClick(review);
        tryCompare(findChild(shell, "lyrics-variants-panel"), "visible", true);
    }
    function test_queuedFeedbackNoticeShowsWithoutAPendingChange() {
        shell.width = 1280;
        shell.height = 800;
        tryCompare(shell.contentItem, "width", 1280);
        const panel = findChild(shell, "lyrics-panel");
        verify(!panel.variantsPending);
        backend.queuedVariantStatusText = "Versão da letra salva.";
        panel.queuedSaveFeedback = true;
        const status = findChild(shell, "lyrics-line-status");
        tryVerify(() => status.visible);
        compare(status.text, "Versão da letra salva.");
        verify(!findChild(shell, "lyrics-line-review").visible, "nothing to review without a pending change");
        const open = findChild(shell, "lyrics-line-open");
        verify(!open || !open.visible, "the notice has precedence over the verse");
        compare(findChild(shell, "lyrics-line").height, 48);
        compare(panel.lineEntryItem, null);
        panel.queuedSaveFeedback = false;
        tryVerify(() => findChild(shell, "lyrics-line-open").visible);
        verify(!status.visible);
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
        compare(findChild(shell, "lyrics-line-current").text, "Romanized verse");
        compare(presenter.currentOccurrenceId, current);
        compare(presenter.playbackState, PlayerPresenter.Playing);
        mouseClick(rowAt(0));
        compare(lastCall("select").args[0], "");
        compare(findChild(shell, "lyrics-line-current").text, "<b>Service verse</b>");
    }
    function test_keyboardCanTurnLyricsOffAndReopenTheSelectedOptionWithoutPausing() {
        backend.selectedVariantId = "1";
        showOnlineCandidates();
        const playing = presenter.currentOccurrenceId;
        const panel = openPanel();
        const list = findChild(shell, "lyrics-variants-list");
        tryCompare(list, "activeFocus", true);
        tryCompare(list, "currentIndex", 1);
        keyClick(Qt.Key_End);
        compare(list.currentItem.Accessible.name, "Sem letra");
        compare(backend.selectedVariantId, "1");
        keyClick(Qt.Key_Space);
        compare(lastCall("select").args, ["off"]);
        verify(list.currentItem.Accessible.selected);
        verify(!findChild(shell, "lyrics-variant-save-current").enabled);
        verify(!findChild(shell, "lyrics-variant-edit").enabled);
        verify(!findChild(shell, "lyrics-variant-remove").enabled);
        verify(!findChild(shell, "lyrics-online-candidates").visible);
        compare(presenter.playbackState, PlayerPresenter.Playing);
        compare(presenter.currentOccurrenceId, playing);
        panel.close();
        tryCompare(panel, "visible", false);
        openPanel();
        tryCompare(list, "activeFocus", true);
        compare(list.currentItem.Accessible.name, "Sem letra");
        keyClick(Qt.Key_Home);
        keyClick(Qt.Key_Return);
        compare(lastCall("select").args, [""]);
        tryCompare(findChild(shell, "lyrics-online-candidates"), "visible", true);
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
    function test_importLrcPopulatesDraftAndWaitsForExplicitSave() {
        openAddForm();
        const input = findChild(shell, "lyrics-variant-content-input");
        const picker = findChild(shell, "lyrics-variant-lrc-picker");
        const choose = findChild(shell, "lyrics-variant-import-lrc");
        verify(picker && choose && choose.enabled);
        choose.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        tryCompare(picker, "visible", true);
        picker.reject();
        tryCompare(picker, "visible", false);
        compare(backend.calls.length, 0);
        tryCompare(choose, "activeFocus", true);
        keyClick(Qt.Key_Return);
        tryCompare(picker, "visible", true);
        const file = Qt.resolvedUrl("data/variant-import.lrc");
        selectLrcFixture(picker);
        tryCompare(picker, "visible", false);
        const request = lastCall("importLrc");
        compare(request.args[0], file.toString());
        compare(request.args[1], findChild(shell, "lyrics-panel").trackIdentity);
        verify(request.args[2].length > 0);
        const save = findChild(shell, "lyrics-variant-submit");
        verify(!save.enabled);
        const lrc = "[00:01.000]日本語";
        backend.importedVariantLrc(request.args[2], "Imported lyrics", lrc);
        compare(input.text, lrc);
        compare(findChild(shell, "lyrics-variant-name-input").text, "Imported lyrics");
        compare(backend.calls.length, 1);
        verify(save.enabled);
        mouseClick(save);
        compare(lastCall("add").args[5], lrc);
    }
    function test_importLrcIgnoresResultForClosedForm() {
        openAddForm();
        const picker = findChild(shell, "lyrics-variant-lrc-picker");
        const choose = findChild(shell, "lyrics-variant-import-lrc");
        choose.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        tryCompare(picker, "visible", true);
        selectLrcFixture(picker);
        tryCompare(picker, "visible", false);
        const request = lastCall("importLrc");
        findChild(shell, "lyrics-variant-form").close();
        tryCompare(findChild(shell, "lyrics-variant-form"), "visible", false);
        mouseClick(findChild(shell, "lyrics-variant-add"));
        tryCompare(findChild(shell, "lyrics-variant-form"), "visible", true);
        backend.importedVariantLrc(request.args[2], "Stale", "[00:01.000]Stale");
        compare(findChild(shell, "lyrics-variant-content-input").text, "");
        compare(backend.calls.length, 1);
    }
    function selectLrcFixture(picker) {
        picker.currentFolder = Qt.resolvedUrl("data");
        const fileList = findChild(shell, "fileDialogListView");
        verify(fileList);
        tryCompare(fileList, "count", 1);
        tryVerify(() => fileList.itemAtIndex(0) && fileList.itemAtIndex(0).width > 0);
        mouseClick(fileList.itemAtIndex(0));
        compare(picker.selectedFile.toString(), Qt.resolvedUrl("data/variant-import.lrc").toString());
        picker.accept();
    }
    function test_translationHasItsOwnLabelAndSubmitsItsKind() {
        const versions = backend.variants.slice();
        versions.push({
            id: "9",
            name: "Português",
            language: "pt",
            script: "Latn",
            source: "Letras",
            kind: "translation",
            generated: false
        });
        backend.variants = versions;
        const panel = openPanel();
        verify(rowAt(3).Accessible.name.includes("Tradução · pt · Latn · Letras"));
        panel.close();
        tryCompare(panel, "visible", false);

        openAddForm();
        findChild(shell, "lyrics-variant-name-input").text = "Versão traduzida";
        findChild(shell, "lyrics-variant-content-input").text = "Uma linha de teste";
        const kindField = findChild(shell, "lyrics-variant-kind-input");
        kindField.currentIndex = 2;
        compare(kindField.currentText, "Tradução");
        mouseClick(findChild(shell, "lyrics-variant-submit"));
        compare(lastCall("add").args[4], "translation");
    }
    function test_queueVariantKeepsItsTargetWhilePlaybackAdvances() {
        const menu = openQueueVariantMenu();
        const form = activateQueuedVariantAction(menu);
        const target = findChild(shell, "lyrics-variant-queued-target");
        verify(target && target.visible);
        compare(target.text, menu.title);
        compare(target.textFormat, Text.PlainText);
        compare(shell.windowShortcutsEnabled, false);
        const name = findChild(shell, "lyrics-variant-name-input");
        const submit = findChild(shell, "lyrics-variant-submit");
        name.text = "Romaji 日本";
        findChild(shell, "lyrics-variant-content-input").text = "[00:01.000]<b>Romaji</b>";
        backend.queueVariantAvailable = false;
        compare(name.enabled, true);
        compare(submit.enabled, false);
        const initial = presenter.currentOccurrenceId;
        driver.publishLocalSourceUnavailable();
        tryVerify(function () {
            return presenter.currentOccurrenceId !== initial && presenter.currentOccurrenceId !== menu.target;
        });
        driver.publishLocalReady();
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        const playing = presenter.currentOccurrenceId;
        const loads = driver.localLoadCount();
        const plays = driver.localPlayCount();
        compare(form.visible, true);
        compare(target.text, menu.title);
        compare(name.text, "Romaji 日本");
        backend.queueVariantAvailable = true;
        findChild(shell, "lyrics-variant-language-input").text = "ja";
        findChild(shell, "lyrics-variant-script-input").text = "Latn";
        findChild(shell, "lyrics-variant-source-input").text = "Origem local";
        findChild(shell, "lyrics-variant-kind-input").currentIndex = 1;
        findChild(shell, "lyrics-variant-generated-input").checked = true;
        mouseClick(submit);
        tryCompare(form, "visible", false);
        compare(lastCall("addQueued").args.join("|"), "opaque-variant-token|Romaji 日本|ja|Latn|Origem local|romanized|[00:01.000]<b>Romaji</b>|true");
        compare(backend.calls.length, 2);
        compare(backend.variants.length, 2);
        compare(presenter.currentOccurrenceId, playing);
        compare(presenter.playbackState, PlayerPresenter.Playing);
        compare(driver.localLoadCount(), loads);
        compare(driver.localPlayCount(), plays);
        compare(menu.list.count, 3);
        const status = findChild(shell, "lyrics-line-status");
        compare(status.text, "Salvando versão da letra…");
        verify(presenter.clearQueue());
        tryCompare(presenter, "hasCurrentTrack", false);
        backend.variantsStatusText = "Carregando versões da letra…";
        compare(status.text, "Salvando versão da letra…");
        backend.variantsBusy = false;
        backend.variantsCanRetrySave = true;
        compare(findChild(shell, "lyrics-line-review").text, "Revisar alterações");
        backend.variantsCanRetrySave = false;
        backend.queuedVariantStatusText = "Versão da letra salva.";
        backend.variantsHasPendingChange = false;
        compare(findChild(shell, "lyrics-panel").visible, true);
        compare(status.text, "Versão da letra salva.");
    }

    function test_queueManagerEditsAndRemovesExactRowWithoutSelectingItInThePlayer() {
        backend.openQueuedManager = occurrence => {
            backend.record("openManager", [occurrence]);
            return "queue-manager";
        };
        backend.closeQueuedManager = token => backend.record("closeManager", [token]);
        backend.prepareQueuedVariantEdit = (token, id) => {
            backend.record("prepareManagerEdit", [token, id]);
            return Object.assign({}, backend.queuedManagerVariants[0], {
                token: "queue-edit",
                content: "Queue lyrics",
                synchronized: false
            });
        };
        backend.removeQueuedVariant = (token, id) => backend.record("removeManager", [token, id]);
        backend.queuedManagerVariants = [
            {
                id: "queued-1",
                name: "Queued variant",
                language: "",
                script: "",
                source: "Letras",
                kind: "original",
                generated: false
            }
        ];
        const menu = openQueueVariantMenu();
        menu.action.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        const panel = findChild(shell, "lyrics-variants-panel");
        tryCompare(panel, "visible", true);
        compare(lastCall("openManager").args[0], menu.target);
        const playing = presenter.currentOccurrenceId;
        compare(findChild(shell, "lyrics-variants-list").count, 1);
        tryVerify(() => rowAt(0) !== null);
        rowAt(0).Accessible.pressAction();
        tryCompare(findChild(shell, "lyrics-variant-edit"), "enabled", true);
        mouseClick(findChild(shell, "lyrics-variant-edit"));
        tryCompare(findChild(shell, "lyrics-variant-form"), "visible", true);
        compare(lastCall("prepareManagerEdit").args.join("|"), "queue-manager|queued-1");
        mouseClick(findChild(shell, "lyrics-variant-submit"));
        tryCompare(findChild(shell, "lyrics-variant-form"), "visible", false);
        compare(lastCall("saveEdit").args[0], "queue-edit");
        mouseClick(findChild(shell, "lyrics-variant-remove"));
        verify(findChild(shell, "lyrics-variant-confirm-text").text.includes(menu.title));
        mouseClick(findChild(shell, "lyrics-variant-confirm-submit"));
        compare(lastCall("removeManager").args.join("|"), "queue-manager|queued-1");
        compare(backend.selectedVariantId, "");
        compare(presenter.currentOccurrenceId, playing);
        panel.close();
        tryCompare(panel, "visible", false);
    }
    function test_queueVariantEscapeCancelsOnlyThePreparedTargetAndRestoresFocus() {
        const menu = openQueueVariantMenu();
        const form = activateQueuedVariantAction(menu);
        const current = presenter.currentOccurrenceId;
        const submit = findChild(shell, "lyrics-variant-submit");
        findChild(shell, "lyrics-variant-name-input").text = "Original";
        findChild(shell, "lyrics-variant-content-input").text = "日本語";
        keyClick(Qt.Key_Escape);
        tryCompare(form, "visible", false);
        tryCompare(menu.list, "activeFocus", true);
        compare(lastCall("cancelQueued").args[0], "opaque-variant-token");
        submit.Accessible.pressAction();
        compare(backend.calls.length, 2);
        compare(presenter.currentOccurrenceId, current);
        compare(presenter.playbackState, PlayerPresenter.Playing);
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
    function test_reviewsGeneratedRomajiBeforeSavingIt() {
        backend.canGenerateRomaji = true;
        backend.romajiSourceRevision = "jp-1";
        const playing = presenter.currentOccurrenceId;
        openPanel();
        const button = findChild(shell, "lyrics-variant-generate-romaji");
        verify(button && button.enabled);
        mouseClick(button);
        compare(lastCall("generateRomaji").args.length, 0);
        const form = findChild(shell, "lyrics-variant-form");
        compare(form.visible, false);
        backend.romajiDraftReady("romaji-token", "[00:01.000]Nihongo");
        tryCompare(form, "visible", true);
        const content = findChild(shell, "lyrics-variant-content-input");
        compare(content.text, "[00:01.000]Nihongo");
        compare(content.readOnly, false);
        compare(findChild(shell, "lyrics-variant-source-input").text, "pykakasi");
        verify(findChild(shell, "lyrics-variant-generated-input").checked);
        content.text = "[00:01.000]Nihon-go";
        mouseClick(findChild(shell, "lyrics-variant-submit"));
        compare(lastCall("saveRomaji").args.join("|"), "romaji-token|Romaji|[00:01.000]Nihon-go");
        compare(presenter.currentOccurrenceId, playing);
        compare(presenter.playbackState, PlayerPresenter.Playing);
    }

    function test_queueManagerReviewsRomajiFromTheSelectedSavedVersion() {
        backend.openQueuedManager = occurrence => "queue-manager";
        backend.queuedRomajiSource = (token, id) => token === "queue-manager" && id === "queued-japanese" ? {
                revision: "queued-revision",
                available: true,
                status: ""
            } : {};
        backend.generateQueuedRomaji = (token, id) => {
            backend.record("generateQueuedRomaji", [token, id]);
            return "queue-romaji";
        };
        backend.queuedManagerVariants = [
            {
                id: "queued-japanese",
                name: "Original japonês",
                language: "ja",
                script: "Jpan",
                source: "Letras",
                kind: "original",
                generated: false
            }
        ];
        const playing = presenter.currentOccurrenceId;
        const menu = openQueueVariantMenu();
        menu.action.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        tryVerify(() => rowAt(0) !== null);
        rowAt(0).Accessible.pressAction();
        const button = findChild(shell, "lyrics-variant-generate-romaji");
        tryCompare(button, "enabled", true);
        mouseClick(button);
        compare(lastCall("generateQueuedRomaji").args.join("|"), "queue-manager|queued-japanese");
        backend.romajiDraftReady("queue-romaji", "[00:01.000]Nihongo");
        const form = findChild(shell, "lyrics-variant-form");
        tryCompare(form, "visible", true);
        mouseClick(findChild(shell, "lyrics-variant-submit"));
        compare(lastCall("saveRomaji").args.join("|"), "queue-romaji|Romaji|[00:01.000]Nihongo");
        tryCompare(form, "visible", false);
        compare(presenter.currentOccurrenceId, playing);
        compare(presenter.playbackState, PlayerPresenter.Playing);
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
                compare(findChild(shell, "lyrics-line-current").text, "Letra sincronizada");
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
    function test_editSavedVersionUsesCapturedTokenAndPreservesTimesByDefault() {
        backend.selectedVariantId = "1";
        openPanel();
        const edit = findChild(shell, "lyrics-variant-edit");
        verify(edit && edit.enabled);
        mouseClick(edit);
        const form = findChild(shell, "lyrics-variant-form");
        tryCompare(form, "visible", true);
        compare(lastCall("prepareEdit").args[0], "1");
        const name = findChild(shell, "lyrics-variant-name-input");
        const source = findChild(shell, "lyrics-variant-source-input");
        const content = findChild(shell, "lyrics-variant-content-input");
        const timing = findChild(shell, "lyrics-variant-timing-input");
        verify(name && source && content && timing);
        compare(name.text, "<b>日本語 Original</b>");
        compare(content.text, "[00:01.00]Saved verse");
        verify(source.readOnly);
        verify(timing.visible && !timing.checked);
        name.text = "Revised";
        content.text = "[00:01.00]Revised verse";
        mouseClick(findChild(shell, "lyrics-variant-submit"));
        compare(lastCall("saveEdit").args, ["edit-1", "Revised", "ja", "Jpan", "original", "[00:01.00]Revised verse", false, false]);
        tryCompare(form, "visible", false);
        compare(presenter.playbackState, PlayerPresenter.Playing);
    }
    function test_editSavedVersionCanCreateCopyAndCancelWhenTargetChanges() {
        backend.selectedVariantId = "1";
        openPanel();
        const edit = findChild(shell, "lyrics-variant-edit");
        mouseClick(edit);
        const form = findChild(shell, "lyrics-variant-form");
        tryCompare(form, "visible", true);
        const timing = findChild(shell, "lyrics-variant-timing-input");
        timing.checked = true;
        const content = findChild(shell, "lyrics-variant-content-input");
        content.text = "[00:02.00]Saved verse";
        compare(timing.checked, true);
        const copyButton = findChild(shell, "lyrics-variant-save-copy");
        verify(waitForPolish(shell));
        mouseClick(copyButton);
        compare(lastCall("saveEdit").args.slice(5), ["[00:02.00]Saved verse", true, true]);
        tryCompare(form, "visible", false);

        mouseClick(edit);
        tryCompare(form, "visible", true);
        backend.selectedVariantId = "9007199254740993";
        tryCompare(form, "visible", false);
        compare(lastCall("cancelEdit").args[0], "edit-1");
        const calls = backend.calls.length;
        findChild(shell, "lyrics-variant-submit").Accessible.pressAction();
        compare(backend.calls.length, calls);
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
        // Without a current track the drawer does not open: the verse line's notice leads to the pending change.
        verify(!findChild(shell, "player-lyrics").enabled);
        const review = findChild(shell, "lyrics-line-review");
        tryVerify(() => review.visible && review.enabled);
        mouseClick(review);
        tryCompare(panel, "visible", true);
        verify(waitForPolish(shell));
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
        compare(findChild(shell, "lyrics-variants-list").count, 3);
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
        // The versions were launched from the drawer's toolbar: the focus goes back there.
        tryCompare(findChild(shell, "lyrics-drawer-versions"), "activeFocus", true);
    }
    function test_keyboardCanChooseASavedVersionWithoutTransportShortcuts() {
        const lastId = "9007199254740993";
        backend.variants = Array.from({
            length: 16
        }, (_, index) => ({
                    id: index === 15 ? lastId : String(index + 1),
                    name: "Version " + index,
                    language: "ja",
                    script: "Latn",
                    source: "",
                    kind: "romanized",
                    generated: false
                }));
        openPanel();
        const list = findChild(shell, "lyrics-variants-list");
        tryCompare(list, "activeFocus", true);
        compare(list.count, 18);
        verify(list.itemAtIndex(16) === null);
        keyClick(Qt.Key_End);
        tryCompare(list, "currentIndex", 17);
        keyClick(Qt.Key_Up);
        tryCompare(list, "currentIndex", 16);
        tryVerify(function () {
            const row = list.itemAtIndex(16);
            return row && row.mapToItem(list, 0, row.height).y <= list.height + 1;
        });
        compare(backend.calls.length, 0);
        const reordered = backend.variants.slice();
        reordered.splice(2, 0, reordered.pop());
        backend.variants = reordered;
        tryCompare(list, "currentIndex", 3);
        keyClick(Qt.Key_Return);
        compare(lastCall("select").args[0], lastId);
        keyClick(Qt.Key_Tab);
        tryCompare(findChild(shell, "lyrics-variant-edit"), "activeFocus", true);
        keyClick(Qt.Key_Tab);
        tryCompare(findChild(shell, "lyrics-variant-remove"), "activeFocus", true);
        keyClick(Qt.Key_Tab, Qt.ShiftModifier);
        tryCompare(findChild(shell, "lyrics-variant-edit"), "activeFocus", true);
        keyClick(Qt.Key_Tab, Qt.ShiftModifier);
        tryCompare(list, "activeFocus", true);
        keyClick(Qt.Key_Home);
        tryCompare(list, "currentIndex", 0);
        compare(backend.selectedVariantId, lastId);
        keyClick(Qt.Key_Space);
        compare(lastCall("select").args[0], "");
        keyClick(Qt.Key_Down);
        tryCompare(list, "currentIndex", 1);
        keyClick(Qt.Key_Up);
        tryCompare(list, "currentIndex", 0);
        compare(presenter.playbackState, PlayerPresenter.Playing);
    }
}
