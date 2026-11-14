pragma ComponentBehavior: Bound

import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "LyricsPanel"
    when: windowShown
    readonly property var driver: testScenarioDriver // qmllint disable unqualified
    readonly property PlayerPresenter presenter: testLocalPlayerPresenter // qmllint disable unqualified
    property var shell
    readonly property var lyricsBackend: lyrics
    QtObject {
        id: lyrics
        property string title: "Original song"
        property string artist: "Artist"
        property string statusText: "Letra sincronizada"
        property string currentLine: "<b>Original verse</b>"
        property string previousLine: "Earlier verse"
        property string nextLine: "Next verse"
        property string plainText: ""
        property bool hasSyncedLyrics: true
        property bool busy: false
        property bool canSearch: true
        property bool lyricsCopyProtected: false
        property bool lyricFindConfigured: false
        property var variants: []
        property bool variantsReady: true
        property bool variantsCanEdit: true
        property bool variantsBusy: false
        property string variantsStatusText: ""
        property bool variantsCanRetrySave: false
        property bool variantsHasPendingChange: false
        property string selectedVariantId: ""
        property string romajiSourceRevision: ""
        property string romajiStatusText: ""
        property bool canGenerateRomaji: false
        property int romajiRequests: 0
        signal romajiDraftReady(string token, string content)
        property string lyricsAttribution: ""
        property string album: "Disc"
        property int durationMs: 246000
        property bool recordingSearchBusy: false
        property string recordingSearchStatus: ""
        property string recordingSearchRevision: "query-1"
        property var recordingCandidates: []
        property bool canSaveRecording: false
        property int requests: 0
        property int uses: 0
        property int cancels: 0
        property string submittedTitle: ""
        property string submittedArtist: ""
        property string submittedAlbum: ""
        property int submittedDuration: 0
        function generateRomaji() {
            romajiRequests++;
            return "focused-romaji";
        }
        function discardRomaji(token) {
        }
        function searchRecordings(title, artist, album, duration) {
            requests++;
            submittedTitle = title;
            submittedArtist = artist;
            submittedAlbum = album;
            submittedDuration = duration;
            return true;
        }
        function previewRecording(index, format, revision) {
            return revision === recordingSearchRevision && index === 1 && format === 0 ? "<b>Other recording</b>" : "";
        }
        function useRecording(index, format, revision) {
            if (previewRecording(index, format, revision) === "")
                return false;
            uses++;
            currentLine = "New verse";
            return true;
        }
        function cancelRecordingSearch() {
            cancels++;
            recordingCandidates = [];
            recordingSearchBusy = false;
        }
    }
    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testCase.presenter
            lyrics: testCase.lyricsBackend
        }
    }
    function init() {
        driver.resetLocalScenario();
        lyrics.requests = 0;
        lyrics.uses = 0;
        lyrics.cancels = 0;
        lyrics.recordingSearchBusy = false;
        lyrics.recordingSearchStatus = "";
        lyrics.recordingCandidates = [];
        lyrics.currentLine = "<b>Original verse</b>";
        lyrics.hasSyncedLyrics = true;
        lyrics.plainText = "";
        lyrics.lyricsCopyProtected = false;
        lyrics.lyricFindConfigured = false;
        lyrics.lyricsAttribution = "";
        lyrics.romajiSourceRevision = "";
        lyrics.romajiStatusText = "";
        lyrics.canGenerateRomaji = false;
        lyrics.romajiRequests = 0;
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.requestActivate();
        tryVerify(function () {
            return testCase.shell.active;
        });
        driver.enqueueLocalSelection(["C:/Music/lyrics.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
    }
    function cleanup() {
        shell.destroy();
        shell = null;
        wait(0);
        driver.resetLocalScenario();
    }
    function test_originalVerseIsPlainTextImmediatelyAboveTheProgressBar() {
        const line = findChild(shell, "lyrics-current-line");
        verify(line);
        compare(line.text, "<b>Original verse</b>");
        compare(line.textFormat, Text.PlainText);
        const progress = findChild(shell, "progress");
        const linePosition = line.mapToItem(shell.contentItem, 0, line.height);
        const progressPosition = progress.mapToItem(shell.contentItem, 0, 0);
        verify(linePosition.y <= progressPosition.y);
        compare(lyrics.requests, 0);
    }
    function test_japaneseLyricsOfferRomajiAndReturnFocusAfterReview() {
        const action = findChild(shell, "lyrics-generate-romaji");
        verify(action);
        verify(!action.visible);
        lyrics.romajiSourceRevision = "jp-12";
        lyrics.canGenerateRomaji = true;
        tryVerify(() => action.visible && action.enabled);
        action.Accessible.pressAction();
        tryCompare(lyrics, "romajiRequests", 1);
        lyrics.romajiDraftReady("focused-romaji", "[00:01.000]nihongo");
        tryCompare(findChild(shell, "lyrics-variant-content-input"), "text", "[00:01.000]nihongo");
        const manager = findChild(shell, "lyrics-variants-panel");
        verify(manager.visible);
        manager.close();
        tryCompare(action, "activeFocus", true);
        compare(presenter.playbackState, PlayerPresenter.Playing);
    }

    function test_japaneseLyricsActionOpensPreparationStatusWithoutRunningConverter() {
        const action = findChild(shell, "lyrics-generate-romaji");
        verify(action);
        lyrics.romajiSourceRevision = "jp-21";
        lyrics.romajiStatusText = "Prepare o componente local de romaji.";
        tryVerify(() => action.visible && action.enabled);
        action.Accessible.pressAction();
        tryVerify(() => findChild(shell, "lyrics-variants-panel").visible);
        compare(lyrics.romajiRequests, 0);
        verify(!findChild(shell, "lyrics-variant-generate-romaji").enabled);
        lyrics.lyricsCopyProtected = true;
        tryVerify(() => !action.visible);
    }
    function test_onlineSearchRequiresExplicitConfirmationAndMetadataCanBeCorrected() {
        const button = findChild(shell, "lyrics-search");
        verify(button);
        mouseClick(button);
        compare(lyrics.requests, 0);
        const title = findChild(shell, "lyrics-query-title");
        const artist = findChild(shell, "lyrics-query-artist");
        const album = findChild(shell, "lyrics-query-album");
        const duration = findChild(shell, "lyrics-query-duration");
        verify(title && artist && album && duration);
        compare(title.text, "Original song");
        title.text = "Corrected title";
        artist.text = "";
        album.text = "Corrected album";
        duration.text = "123";
        compare(lyrics.requests, 0);
        const submit = findChild(shell, "lyrics-query-submit");
        verify(submit);
        mouseClick(submit);
        compare(lyrics.requests, 1);
        compare(lyrics.submittedTitle, "Corrected title");
        compare(lyrics.submittedArtist, "");
        compare(lyrics.submittedAlbum, "Corrected album");
        compare(lyrics.submittedDuration, 123000);
    }
    function test_previewAndExplicitChoiceKeepSameTitleRecordingsSeparate() {
        mouseClick(findChild(shell, "lyrics-search"));
        mouseClick(findChild(shell, "lyrics-query-submit"));
        lyrics.recordingCandidates = [
            {
                id: "1",
                title: "Same title",
                artist: "First artist",
                durationMs: 246000,
                album: "",
                formats: [
                    {
                        label: "Texto"
                    }
                ]
            },
            {
                id: "2",
                title: "Same title",
                artist: "Other artist",
                durationMs: 120000,
                album: "",
                formats: [
                    {
                        label: "Texto"
                    }
                ]
            }
        ];
        const recordings = findChild(shell, "lyrics-recording-choice");
        const choice = findChild(shell, "lyrics-recording-use");
        verify(recordings && choice);
        compare(recordings.count, 3);
        verify(recordings.textAt(1).includes("First artist"));
        verify(recordings.textAt(2).includes("Other artist"));
        compare(recordings.currentIndex, 0);
        compare(choice.enabled, false);
        recordings.activated(2);
        const preview = findChild(shell, "lyrics-recording-preview");
        compare(preview.textFormat, Text.PlainText);
        compare(preview.text, "<b>Other recording</b>");
        compare(lyrics.currentLine, "<b>Original verse</b>");
        compare(lyrics.uses, 0);
        mouseClick(choice);
        compare(lyrics.uses, 1);
        compare(lyrics.currentLine, "New verse");
        compare(lyrics.requests, 1);
    }
    function test_cancelDuringSearchDiscardsResultsAndReturnsFocus() {
        const button = findChild(shell, "lyrics-search");
        mouseClick(button);
        mouseClick(findChild(shell, "lyrics-query-submit"));
        lyrics.recordingSearchBusy = true;
        keyClick(Qt.Key_Escape);
        tryCompare(findChild(shell, "lyrics-panel"), "queryOpen", false);
        compare(lyrics.cancels, 1);
        compare(lyrics.currentLine, "<b>Original verse</b>");
        tryCompare(button, "activeFocus", true);
    }
    function test_licensedLyricsUsePlainDisplayAndVisibleCredits() {
        lyrics.hasSyncedLyrics = false;
        lyrics.plainText = "<b>Licensed lyric</b>";
        lyrics.lyricsAttribution = "<b>Writer</b> · Publisher";
        lyrics.lyricsCopyProtected = true;
        lyrics.lyricFindConfigured = true;
        const selectable = findChild(shell, "lyrics-plain-text");
        const protectedText = findChild(shell, "lyrics-protected-plain-text");
        const credit = findChild(shell, "lyrics-provider-credit");
        verify(protectedText.visible && credit.visible);
        verify(!selectable.visible);
        compare(selectable.text, "");
        compare(protectedText.text, "<b>Licensed lyric</b>");
        compare(protectedText.textFormat, Text.PlainText);
        compare(credit.text, "<b>Writer</b> · Publisher");
        compare(credit.textFormat, Text.PlainText);
        mouseClick(findChild(shell, "lyrics-search"));
        verify(findChild(shell, "lyrics-query-notice").text.includes("LRCLIB"));
    }
    function test_lyricsKeepPlayingContentInsideTheMinimumWindow() {
        shell.width = 720;
        shell.height = 520;
        wait(0);
        const playing = findChild(shell, "playing-state");
        const layer = findChild(shell, "state-layer");
        verify(playing && layer);
        const contentTop = playing.mapToItem(layer, 0, 0).y;
        const contentBottom = playing.mapToItem(layer, 0, playing.height).y;
        verify(contentTop >= 0, "Playing content starts above its area");
        verify(contentBottom <= layer.height, "Playing content overlaps lyrics");
    }
    function test_searchDialogDisablesPlayerShortcutsAndClosesOnTrackChange() {
        mouseClick(findChild(shell, "lyrics-search"));
        compare(shell.windowShortcutsEnabled, false);
        compare(lyrics.requests, 0);
        const panel = findChild(shell, "lyrics-panel");
        compare(panel.queryOpen, true);
        panel.trackIdentity = "different-occurrence";
        tryCompare(panel, "queryOpen", false);
        compare(lyrics.requests, 0);
    }
}
