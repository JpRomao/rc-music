import QtQuick
import QtQuick.Controls
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
        property int requests: 0
        property string submittedTitle: ""
        property string submittedArtist: ""
        function search(title, artist) {
            requests++;
            submittedTitle = title;
            submittedArtist = artist;
            return true;
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
    function test_onlineSearchRequiresExplicitConfirmationAndMetadataCanBeCorrected() {
        const button = findChild(shell, "lyrics-search");
        verify(button);
        mouseClick(button);
        compare(lyrics.requests, 0);
        const title = findChild(shell, "lyrics-query-title");
        const artist = findChild(shell, "lyrics-query-artist");
        verify(title && artist);
        compare(title.text, "Original song");
        title.text = "Corrected title";
        artist.text = "Corrected artist";
        const submit = findChild(shell, "lyrics-query-submit");
        verify(submit);
        mouseClick(submit);
        compare(lyrics.requests, 1);
        compare(lyrics.submittedTitle, "Corrected title");
        compare(lyrics.submittedArtist, "Corrected artist");
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
