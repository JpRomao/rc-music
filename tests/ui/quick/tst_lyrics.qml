pragma ComponentBehavior: Bound

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
        property var readerVerses: []
        property int activeReaderVerse: -1
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
    // A small library, so a page with a text field sits beside the drawer.
    ListModel {
        id: records
        ListElement {
            recordId: "3"
            title: "Song"
            artist: "Artist"
            album: "Disc"
            durationText: "3:37"
            available: true
            fileName: "Song.mp3"
        }
    }
    QtObject {
        id: catalog
        property bool ready: true
        property bool busy: false
        property int count: 1
        property int visibleCount: 1
        property bool hasPendingChange: false
        property bool canRetrySave: false
        property string statusText: ""
        property string filterText: ""
        property var model: records
        property bool canLocate: false
        property string artworkBase: ""
        function enqueue(id) {
            return false;
        }
        function remove(id) {
            return false;
        }
        function locate(id) {
            return false;
        }
        function cancelLocate() {
        }
        function addToPlaylist(id, playlist) {
            return false;
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
            uiContract: testProductUiContract // qmllint disable unqualified
            presenter: testCase.presenter
            lyrics: testCase.lyricsBackend
        }
    }
    // The M0 reader on its own, as the fixture mounts it: no editorial drawer around it.
    Component {
        id: frozenPanelHost
        ApplicationWindow {
            width: 720
            height: 520
            visible: true
            LyricsPanel {
                width: parent.width
                contract: testShellUiContract // qmllint disable unqualified
                lyrics: testCase.lyricsBackend
            }
        }
    }
    Component {
        id: bareShellComponent
        Main {
            uiContract: testProductUiContract // qmllint disable unqualified
            presenter: testCase.presenter
        }
    }
    function init() {
        driver.resetLocalScenario();
        lyrics.title = "Original song";
        lyrics.artist = "Artist";
        lyrics.album = "Disc";
        lyrics.durationMs = 246000;
        lyrics.requests = 0;
        lyrics.uses = 0;
        lyrics.cancels = 0;
        lyrics.recordingSearchBusy = false;
        lyrics.recordingSearchStatus = "";
        lyrics.recordingCandidates = [];
        lyrics.currentLine = "<b>Original verse</b>";
        lyrics.statusText = "Letra sincronizada";
        lyrics.variants = [];
        lyrics.selectedVariantId = "";
        lyrics.variantsCanEdit = true;
        lyrics.variantsHasPendingChange = false;
        lyrics.canSearch = true;
        lyrics.readerVerses = [];
        lyrics.activeReaderVerse = -1;
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
        driver.setHighContrast(false, false);
        shell.destroy();
        shell = null;
        wait(0);
        driver.resetLocalScenario();
    }
    // The real entry points: the player's lyrics button opens the drawer, its toolbar and empty state launch the dialogs.
    function openDrawer() {
        const reader = findChild(shell, "lyrics-reader");
        if (!reader.visible) {
            const button = findChild(shell, "player-lyrics");
            tryVerify(() => button.enabled);
            mouseClick(button);
        }
        tryCompare(reader, "opened", true);
        // The toolbar lays out on the next frame: wait until the buttons have left each other's place.
        const more = findChild(shell, "lyrics-drawer-more");
        const versions = findChild(shell, "lyrics-drawer-versions");
        tryVerify(() => !more || !more.visible || more.mapToItem(reader.contentItem, 0, 0).x > versions.mapToItem(reader.contentItem, 0, 0).x);
        return reader;
    }
    function clickDrawer(name) {
        const control = findChild(shell, name);
        verify(control, name);
        tryVerify(() => control.visible && control.enabled, 5000, name + " is available");
        mouseClick(control);
        return control;
    }
    function enableRomaji(source, canGenerate = true) {
        lyrics.romajiSourceRevision = source;
        lyrics.canGenerateRomaji = canGenerate;
        tryVerify(() => findChild(shell, "lyrics-panel").romajiAvailable);
    }
    function openRomajiFromMenu() {
        openDrawer();
        clickDrawer("lyrics-drawer-more");
        const menu = findChild(shell, "lyrics-drawer-more-menu");
        tryCompare(menu, "opened", true);
        const item = findChild(shell, "lyrics-drawer-romaji");
        tryVerify(() => item.visible && item.enabled);
        mouseClick(item);
    }
    function isInside(item, ancestor) {
        for (let current = item; current; current = current.parent) {
            if (current === ancestor)
                return true;
        }
        return false;
    }
    function test_playerLyricsButtonOpensTheReader() {
        const button = findChild(shell, "player-lyrics");
        verify(button && button.visible);
        tryVerify(() => button.enabled);
        compare(button.iconName, "lyrics");
        mouseClick(button);
        const reader = findChild(shell, "lyrics-reader");
        tryVerify(() => reader.visible);
        // Aurora drawer: flush to the right edge, from the top to the player.
        tryVerify(() => reader.opened);
        const point = reader.contentItem.parent.mapToItem(shell.contentItem, 0, 0);
        const footer = findChild(shell, "player-footer");
        compare(Math.round(reader.x + reader.width), shell.width);
        compare(reader.y, 0);
        compare(Math.round(reader.y + reader.height), Math.round(footer.y));
        compare(reader.width, 400);
        compare(reader.background.radius, 0);
        compare(reader.background.border.width, 0);
        verify(point.x >= 0);
    }
    function test_playerLyricsButtonOpensTheDrawerEvenWithoutLyrics() {
        const button = findChild(shell, "player-lyrics");
        tryVerify(() => button.enabled);
        lyrics.hasSyncedLyrics = false;
        lyrics.plainText = "";
        // The drawer opens on its empty state, so the button stays enabled while the track is current.
        verify(button.enabled);
        compare(button.accessibleName, "Letra");
        lyrics.plainText = "Verse";
        verify(button.enabled);
        // Without a lyrics backend there is no drawer to open: the reason is spelled out.
        shell.lyrics = null;
        tryVerify(() => !button.enabled);
        compare(button.disabledReason, "Esta faixa ainda não tem letra.");
    }
    function test_playerLyricsButtonNeedsACurrentTrack_data() {
        return [
            {
                tag: "queued save notice"
            },
            {
                tag: "pending version change"
            }
        ];
    }
    // A notice keeps the panel shown without a current track, but there is no track for the drawer to show.
    function test_playerLyricsButtonNeedsACurrentTrack(data) {
        const button = findChild(shell, "player-lyrics");
        tryVerify(() => button.enabled);
        verify(presenter.clearQueue());
        tryVerify(() => !presenter.hasCurrentTrack);
        const panel = findChild(shell, "lyrics-panel");
        if (data.tag === "queued save notice")
            panel.queuedSaveFeedback = true;
        else
            lyrics.variantsHasPendingChange = true;
        tryVerify(() => panel.visible, 5000, "the notice keeps the panel shown");
        verify(waitForRendering(shell.contentItem));
        verify(!button.enabled, "no current track, no drawer");
        verify(!panel.readerAvailable);
        panel.showReader(button);
        verify(!findChild(shell, "lyrics-reader").visible);
    }
    function test_verseLineShowsCurrentAndNextVerse() {
        const line = findChild(shell, "lyrics-line");
        verify(line && line.visible);
        compare(line.height, 48);
        const current = findChild(shell, "lyrics-line-current");
        compare(current.text, lyrics.currentLine);
        compare(current.font.family, shell.uiContract.headingFontFamily);
        compare(findChild(shell, "lyrics-line-next").text, "Next verse");
        const open = findChild(shell, "lyrics-line-open");
        compare(open.text, "Abrir letra");
        compare(open.Accessible.name, "Abrir a letra completa");
        mouseClick(open);
        tryVerify(() => findChild(shell, "lyrics-reader").visible);
    }
    function test_verseLineHidesWithoutSynchronizedLyrics_data() {
        return [
            {
                tag: "plain",
                plain: "Verse",
                protectedLyrics: false
            },
            {
                tag: "protected",
                plain: "Licensed",
                protectedLyrics: true
            },
            {
                tag: "none",
                plain: "",
                protectedLyrics: false
            }
        ];
    }
    function test_verseLineHidesWithoutSynchronizedLyrics(data) {
        lyrics.hasSyncedLyrics = false;
        lyrics.plainText = data.plain;
        lyrics.lyricsCopyProtected = data.protectedLyrics;
        const line = findChild(shell, "lyrics-line");
        tryVerify(() => !line.visible);
        compare(findChild(shell, "lyrics-panel").implicitHeight, 0);
        for (const name of ["lyrics-versions", "lyrics-search", "lyrics-generate-romaji", "lyrics-open-reader"])
            verify(!findChild(shell, name).visible, name + " is not part of the Aurora line");
    }
    function test_compactLineElidesLongVerse() {
        shell.width = 720;
        shell.height = 520;
        tryCompare(shell.contentItem, "width", 720);
        lyrics.currentLine = "x".repeat(300);
        verify(waitForPolish(shell));
        const line = findChild(shell, "lyrics-line");
        compare(line.height, 40);
        const current = findChild(shell, "lyrics-line-current");
        compare(current.elide, Text.ElideRight);
        compare(current.Accessible.name, lyrics.currentLine);
        verify(!findChild(shell, "lyrics-line-next").visible);
        const open = findChild(shell, "lyrics-line-open");
        compare(open.width, 40);
        compare(open.height, 40);
        const corner = open.mapToItem(shell.contentItem, open.width, open.height);
        verify(corner.x <= shell.width && corner.y <= shell.height);
        compare(open.Accessible.name, "Abrir a letra completa");
    }
    function test_verseLineHighContrastOutline() {
        driver.setHighContrast(true, false);
        const line = findChild(shell, "lyrics-line");
        tryCompare(line.border, "width", 2);
        verify(Qt.colorEqual(line.border.color, shell.uiContract.textPrimaryColor));
    }
    function test_originalVerseIsPlainTextImmediatelyAboveTheProgressBar() {
        const line = findChild(shell, "lyrics-line-current");
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
        const panel = findChild(shell, "lyrics-panel");
        verify(!panel.romajiAvailable);
        enableRomaji("jp-12");
        openRomajiFromMenu();
        tryCompare(lyrics, "romajiRequests", 1);
        lyrics.romajiDraftReady("focused-romaji", "[00:01.000]nihongo");
        tryCompare(findChild(shell, "lyrics-variant-content-input"), "text", "[00:01.000]nihongo");
        const manager = findChild(shell, "lyrics-variants-panel");
        verify(manager.visible);
        manager.close();
        // The romaji flow was launched from the drawer's "more" button: the focus goes back to it.
        tryCompare(findChild(shell, "lyrics-drawer-more"), "activeFocus", true);
        compare(presenter.playbackState, PlayerPresenter.Playing);
    }

    function test_japaneseLyricsActionOpensPreparationStatusWithoutRunningConverter() {
        const panel = findChild(shell, "lyrics-panel");
        lyrics.romajiStatusText = "Prepare o componente local de romaji.";
        enableRomaji("jp-21", false);
        openRomajiFromMenu();
        tryVerify(() => findChild(shell, "lyrics-variants-panel").visible);
        compare(lyrics.romajiRequests, 0);
        verify(!findChild(shell, "lyrics-variant-generate-romaji").enabled);
        lyrics.lyricsCopyProtected = true;
        tryVerify(() => !panel.romajiAvailable);
    }
    function test_onlineSearchRequiresExplicitConfirmationAndMetadataCanBeCorrected() {
        openDrawer();
        clickDrawer("lyrics-drawer-search");
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
    function test_lateTrackTagsRefreshOnlyUntouchedFieldsUntilSearch() {
        lyrics.title = "";
        lyrics.artist = "";
        lyrics.album = "";
        lyrics.durationMs = 0;
        const playing = presenter.currentOccurrenceId;
        openDrawer();
        clickDrawer("lyrics-drawer-search");
        const dialog = findChild(shell, "lyrics-query-dialog");
        tryCompare(dialog, "opened", true);
        const title = findChild(shell, "lyrics-query-title");
        const artist = findChild(shell, "lyrics-query-artist");
        const album = findChild(shell, "lyrics-query-album");
        const duration = findChild(shell, "lyrics-query-duration");
        const submit = findChild(shell, "lyrics-query-submit");
        verify(!submit.enabled);
        album.forceActiveFocus(Qt.TabFocusReason);
        tryCompare(album, "activeFocus", true);
        keyClick(Qt.Key_X);
        const customAlbum = album.text;
        verify(customAlbum.length > 0);
        lyrics.title = "Tagged song";
        lyrics.artist = "Tagged artist";
        lyrics.album = "Tagged album";
        lyrics.durationMs = 123000;
        tryCompare(title, "text", "Tagged song");
        compare(artist.text, "Tagged artist");
        compare(album.text, customAlbum);
        compare(duration.text, "123");
        compare(lyrics.requests, 0);
        mouseClick(submit);
        compare(lyrics.requests, 1);
        compare(lyrics.submittedTitle, "Tagged song");
        compare(lyrics.submittedArtist, "Tagged artist");
        compare(lyrics.submittedAlbum, customAlbum);
        compare(lyrics.submittedDuration, 123000);
        lyrics.title = "Updated tag";
        lyrics.durationMs = 456000;
        compare(title.text, "Tagged song");
        compare(duration.text, "123");
        compare(dialog.queryDuration, 123);
        compare(lyrics.requests, 1);
        compare(presenter.currentOccurrenceId, playing);
        compare(presenter.playbackState, PlayerPresenter.Playing);
    }
    function test_previewAndExplicitChoiceKeepSameTitleRecordingsSeparate() {
        openDrawer();
        clickDrawer("lyrics-drawer-search");
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
        openDrawer();
        clickDrawer("lyrics-drawer-search");
        mouseClick(findChild(shell, "lyrics-query-submit"));
        lyrics.recordingSearchBusy = true;
        keyClick(Qt.Key_Escape);
        tryCompare(findChild(shell, "lyrics-panel"), "dialogOpen", false);
        compare(lyrics.cancels, 1);
        compare(lyrics.currentLine, "<b>Original verse</b>");
        // The drawer launched the search: the focus goes back to its search button, not to the line behind it.
        tryCompare(findChild(shell, "lyrics-drawer-search"), "activeFocus", true);
        verify(findChild(shell, "lyrics-reader").opened);
    }
    function test_searchWithoutTheDrawerReturnsFocusToTheVerseLine() {
        // The dialogs stay reachable from the page too: with the drawer closed the focus returns to the line.
        const panel = findChild(shell, "lyrics-panel");
        panel.openSearch();
        const dialog = findChild(shell, "lyrics-query-dialog");
        tryCompare(dialog, "opened", true);
        keyClick(Qt.Key_Escape);
        tryCompare(dialog, "visible", false);
        tryCompare(findChild(shell, "lyrics-line-open"), "activeFocus", true);
    }
    function test_licensedLyricsUsePlainDisplayAndVisibleCredits() {
        lyrics.hasSyncedLyrics = false;
        lyrics.plainText = "<b>Licensed lyric</b>";
        lyrics.lyricsAttribution = "<b>Writer</b> · Publisher";
        lyrics.lyricsCopyProtected = true;
        lyrics.lyricFindConfigured = true;
        const reader = openDrawer();
        const selectable = findChild(reader, "lyrics-reader-plain");
        const protectedText = findChild(reader, "lyrics-reader-protected-text");
        const credit = findChild(reader, "lyrics-reader-credit-text");
        const lyricFind = findChild(reader, "lyrics-reader-lyricfind");
        verify(selectable && protectedText && credit && lyricFind);
        // The Aurora drawer shows the licensed text as plain, non-selectable text with its credit and the LyricFind action.
        verify(protectedText.visible && credit.visible, "the licensed text and its credit are visible in the drawer");
        compare(protectedText.text, "<b>Licensed lyric</b>");
        compare(protectedText.textFormat, Text.PlainText);
        compare(credit.text, "<b>Writer</b> · Publisher");
        compare(credit.textFormat, Text.PlainText);
        verify(!selectable.visible);
        compare(selectable.text, "");
        verify(!findChild(shell, "lyrics-plain-text").visible, "the selectable strip text stays hidden");
        verify(lyricFind.visible && lyricFind.enabled);
        compare(lyricFind.Accessible.name, "Abrir site do LyricFind");
        lyricFind.forceActiveFocus(Qt.TabFocusReason);
        tryCompare(lyricFind, "activeFocus", true);
        clickDrawer("lyrics-drawer-search");
        verify(findChild(shell, "lyrics-query-notice").text.includes("LRCLIB"));
    }
    function test_lyricsKeepPlayingContentInsideTheMinimumWindow() {
        shell.width = 720;
        shell.height = 520;
        tryCompare(shell.contentItem, "height", 520);
        verify(waitForPolish(shell));
        const playing = findChild(shell, "playing-state");
        const layer = findChild(shell, "state-layer");
        verify(playing && layer);
        if (shell.inlinePages) {
            verify(layer.clip && layer.interactive, "The editorial home scrolls inside its clipped viewport");
            const panel = findChild(shell, "lyrics-panel");
            verify(layer.mapToItem(shell.contentItem, 0, layer.height).y <= panel.mapToItem(shell.contentItem, 0, 0).y, "The actual home viewport must not overlap lyrics");
            for (const name of ["open-another-audio", "track-details-playing"]) {
                const action = findChild(shell, name);
                action.forceActiveFocus(Qt.TabFocusReason);
                tryCompare(action, "activeFocus", true);
                verify(waitForPolish(shell));
                tryVerify(() => {
                    const p = action.mapToItem(layer, 0, 0);
                    return p.y >= 0 && p.y + action.height <= layer.height + 1;
                }, 5000, name + " must be revealed above lyrics");
                verify(action.width >= 40 && action.height >= 40);
            }
        } else {
            const contentTop = playing.mapToItem(layer, 0, 0).y;
            const contentBottom = playing.mapToItem(layer, 0, playing.height).y;
            verify(contentTop >= 0, "Playing content starts above its area");
            verify(contentBottom <= layer.height, "Playing content overlaps lyrics");
        }
    }
    function test_searchDialogDisablesPlayerShortcutsAndClosesOnTrackChange() {
        openDrawer();
        clickDrawer("lyrics-drawer-search");
        compare(shell.windowShortcutsEnabled, false);
        compare(lyrics.requests, 0);
        const panel = findChild(shell, "lyrics-panel");
        compare(panel.queryOpen, true);
        panel.trackIdentity = "different-occurrence";
        tryCompare(panel, "queryOpen", false);
        compare(lyrics.requests, 0);
    }

    function drawerPart(drawerName, partName) {
        const drawer = findChild(shell, drawerName);
        verify(drawer, drawerName);
        const part = findChild(drawer, partName);
        verify(part, partName);
        return part;
    }
    function test_lyricsDrawerHeaderAndToolbar() {
        lyrics.variants = [
            {
                id: "v1",
                name: "Romaji",
                language: "ja",
                script: "Latn",
                source: "",
                kind: "romanized",
                generated: false
            }
        ];
        lyrics.selectedVariantId = "v1";
        const reader = openDrawer();
        const footer = findChild(shell, "player-footer");
        compare(reader.width, 400);
        compare(reader.y, 0);
        compare(Math.round(reader.y + reader.height), Math.round(footer.y));
        compare(Math.round(reader.x + reader.width), shell.width);
        compare(drawerPart("lyrics-drawer", "drawer-eyebrow").text, "Letra sincronizada");
        compare(drawerPart("lyrics-drawer", "drawer-title").text, "Original song");
        compare(drawerPart("lyrics-drawer", "drawer-subtitle").text, "Versão: Romaji · Artist");
        const close = findChild(shell, "lyrics-drawer-close");
        compare(close.Accessible.name, "Fechar letra");
        const versions = findChild(shell, "lyrics-drawer-versions");
        const search = findChild(shell, "lyrics-drawer-search");
        compare(versions.text, "Versões");
        compare(search.text, "Buscar letra");
        verify(!findChild(shell, "lyrics-drawer-more").visible, "no romaji, no more menu");
        // Every toolbar control sits inside the drawer.
        for (const name of ["lyrics-drawer-versions", "lyrics-drawer-search", "lyrics-drawer-smaller", "lyrics-drawer-larger"]) {
            const control = findChild(shell, name);
            const corner = control.mapToItem(reader.contentItem, control.width, control.height);
            verify(corner.x <= reader.contentItem.width, name + " fits the drawer");
        }
        clickDrawer("lyrics-drawer-versions");
        tryCompare(findChild(shell, "lyrics-variants-panel"), "visible", true);
        findChild(shell, "lyrics-variants-panel").close();
        tryCompare(versions, "activeFocus", true);
        clickDrawer("lyrics-drawer-search");
        tryCompare(findChild(shell, "lyrics-query-dialog"), "opened", true);
        keyClick(Qt.Key_Escape);
        tryCompare(search, "activeFocus", true);
        // The screen reader path reaches the same dialogs.
        search.Accessible.pressAction();
        tryCompare(findChild(shell, "lyrics-query-dialog"), "opened", true);
        keyClick(Qt.Key_Escape);
        tryCompare(search, "activeFocus", true);
        versions.Accessible.pressAction();
        tryCompare(findChild(shell, "lyrics-variants-panel"), "visible", true);
        findChild(shell, "lyrics-variants-panel").close();
        tryCompare(versions, "activeFocus", true);
        // Without a selected version the subtitle is the artist.
        lyrics.selectedVariantId = "";
        compare(drawerPart("lyrics-drawer", "drawer-subtitle").text, "Artist");
        // The size controls act like the old buttons: A- and A+ request a step of two.
        lyrics.readerVerses = [
            {
                text: "Verse",
                timeMs: 1000
            }
        ];
        const size = shell.presenter.lyricsTextSize;
        clickDrawer("lyrics-drawer-larger");
        tryCompare(shell.presenter, "lyricsTextSize", size + 2);
        clickDrawer("lyrics-drawer-smaller");
        tryCompare(shell.presenter, "lyricsTextSize", size);
    }
    // Qt's Basic Dialog draws a bold title band from Dialog.title: the drawer has its own header, so the track
    // title must not reach that band.
    function test_drawerShowsTheTitleOnlyInItsOwnHeader() {
        const reader = openDrawer();
        compare(drawerPart("lyrics-drawer", "drawer-title").text, "Original song");
        verify(!reader.header || !reader.header.visible, "no second title band above the drawer header");
    }
    function test_frozenReaderKeepsNoTitleBandWithATitledTrack() {
        lyrics.readerVerses = [
            {
                text: "One",
                timeMs: 1000
            }
        ];
        const host = createTemporaryObject(frozenPanelHost, null);
        verify(host);
        host.requestActivate();
        tryCompare(host, "active", true);
        const button = findChild(host, "lyrics-open-reader");
        tryVerify(() => button.visible && button.enabled);
        mouseClick(button);
        const reader = findChild(host, "lyrics-reader");
        tryCompare(reader, "opened", true);
        compare(lyrics.title, "Original song");
        verify(!reader.header || !reader.header.visible, "the M0 reader has no title band");
        compare(reader.implicitHeaderHeight, 0);
    }
    function test_reviewReplacesVersionsInTheToolbarAndStillFits() {
        lyrics.variantsHasPendingChange = true;
        const reader = openDrawer();
        const versions = findChild(shell, "lyrics-drawer-versions");
        tryCompare(versions, "text", "Revisar alterações");
        compare(versions.Accessible.name, "Revisar alterações não salvas da letra");
        lyrics.romajiSourceRevision = "jp-3";
        lyrics.canGenerateRomaji = true;
        const more = findChild(shell, "lyrics-drawer-more");
        tryVerify(() => more.visible);
        verify(waitForPolish(reader.contentItem));
        const larger = findChild(shell, "lyrics-drawer-larger");
        const edge = larger.mapToItem(reader.contentItem, larger.width, 0);
        tryVerify(() => larger.mapToItem(reader.contentItem, larger.width, 0).x <= reader.contentItem.width, 5000, "the size controls stay inside the drawer");
        const search = findChild(shell, "lyrics-drawer-search");
        verify(search.mapToItem(reader.contentItem, search.width, 0).x <= larger.mapToItem(reader.contentItem, 0, 0).x, "the toolbar groups do not overlap");
        verify(edge.x > 0);
    }
    function test_romajiLivesInTheMoreMenu() {
        openDrawer();
        verify(!findChild(shell, "lyrics-drawer-more").visible, "without romaji the more button is not offered");
        enableRomaji("jp-5");
        const more = findChild(shell, "lyrics-drawer-more");
        tryVerify(() => more.visible);
        compare(more.Accessible.name, "Mais ações");
        // The screen reader path opens the menu; the item itself is chosen like any menu item.
        more.Accessible.pressAction();
        const menu = findChild(shell, "lyrics-drawer-more-menu");
        tryCompare(menu, "opened", true);
        const item = findChild(shell, "lyrics-drawer-romaji");
        compare(item.text, "Gerar romaji");
        mouseClick(item);
        tryVerify(() => findChild(shell, "lyrics-variants-panel").visible);
        tryCompare(lyrics, "romajiRequests", 1);
        lyrics.romajiSourceRevision = "";
        tryVerify(() => !more.visible);
    }
    function test_currentVerseIsAccentWithoutBorder() {
        lyrics.readerVerses = [
            {
                text: "Past verse",
                timeMs: 1000
            },
            {
                text: "Current verse",
                timeMs: 2000
            },
            {
                text: "Next verse",
                timeMs: 3000
            }
        ];
        lyrics.activeReaderVerse = 1;
        const reader = openDrawer();
        const list = findChild(reader, "lyrics-reader-verses");
        tryVerify(() => list.itemAtIndex(0) && list.itemAtIndex(1) && list.itemAtIndex(2));
        // Move the keyboard cursor away: the focus ring is the only border a verse may have.
        findChild(shell, "lyrics-drawer-close").forceActiveFocus(Qt.TabFocusReason);
        const contract = shell.uiContract;
        const past = list.itemAtIndex(0);
        const current = list.itemAtIndex(1);
        const next = list.itemAtIndex(2);
        verify(Qt.colorEqual(current.contentItem.color, contract.accentColor));
        compare(current.contentItem.font.weight, Font.DemiBold);
        compare(current.contentItem.font.pixelSize, shell.presenter.lyricsTextSize + 4);
        compare(current.background.border.width, 0);
        verify(Qt.colorEqual(past.contentItem.color, contract.textSecondaryColor));
        verify(Qt.colorEqual(next.contentItem.color, contract.textPrimaryColor));
        compare(past.contentItem.font.weight, Font.Normal);
        // The keyboard cursor keeps its 3 px ring.
        list.forceActiveFocus(Qt.TabFocusReason);
        tryCompare(list.itemAtIndex(1).background.border, "width", 3);
        // High contrast keeps today's system colors: text color with a 2 px accent border on the current verse.
        findChild(shell, "lyrics-drawer-close").forceActiveFocus(Qt.TabFocusReason);
        driver.setHighContrast(true, false);
        const hcContract = shell.uiContract;
        tryVerify(() => Qt.colorEqual(list.itemAtIndex(1).contentItem.color, hcContract.textPrimaryColor));
        compare(list.itemAtIndex(1).background.border.width, 2);
        verify(Qt.colorEqual(list.itemAtIndex(1).background.border.color, hcContract.accentColor));
        compare(list.itemAtIndex(0).background.border.width, 0);
    }
    function test_emptyLyricsDrawer() {
        lyrics.hasSyncedLyrics = false;
        lyrics.plainText = "";
        const button = findChild(shell, "player-lyrics");
        tryVerify(() => button.enabled);
        const reader = openDrawer();
        compare(drawerPart("lyrics-drawer", "drawer-eyebrow").text, "Letra");
        const empty = findChild(shell, "lyrics-drawer-empty");
        tryVerify(() => empty.visible);
        compare(findChild(empty, "empty-title").text, "Esta faixa ainda não tem letra.");
        compare(findChild(empty, "empty-hint").text, "Busque no LRCLIB ou adicione a letra você mesmo.");
        verify(!findChild(reader, "lyrics-reader-verses").visible);
        const primary = findChild(shell, "lyrics-drawer-empty-primary");
        const secondary = findChild(shell, "lyrics-drawer-empty-secondary");
        compare(primary.text, "Buscar letra");
        compare(secondary.text, "Adicionar versão");
        tryCompare(primary, "activeFocus", true);
        mouseClick(primary);
        const dialog = findChild(shell, "lyrics-query-dialog");
        tryCompare(dialog, "opened", true);
        keyClick(Qt.Key_Escape);
        tryCompare(dialog, "visible", false);
        tryCompare(primary, "activeFocus", true);
        mouseClick(secondary);
        tryCompare(findChild(shell, "lyrics-variant-form"), "visible", true);
        keyClick(Qt.Key_Escape);
        tryCompare(findChild(shell, "lyrics-variant-form"), "visible", false);
        findChild(shell, "lyrics-variants-panel").close();
        tryCompare(secondary, "activeFocus", true);
        // The actions follow what the backend can do.
        lyrics.variantsCanEdit = false;
        tryVerify(() => !secondary.visible);
        lyrics.canSearch = false;
        tryVerify(() => !primary.visible);
        lyrics.canSearch = true;
    }
    function test_drawerClosesAndReturnsFocusWhenTrackChanges() {
        const line = findChild(shell, "lyrics-line-open");
        mouseClick(line);
        const reader = findChild(shell, "lyrics-reader");
        tryCompare(reader, "opened", true);
        mouseClick(findChild(shell, "lyrics-drawer-close"));
        tryCompare(reader, "visible", false);
        // The same track: the focus goes back to the verse line that opened the drawer.
        tryCompare(findChild(shell, "lyrics-line-open"), "activeFocus", true);
        mouseClick(findChild(shell, "lyrics-line-open"));
        tryCompare(reader, "opened", true);
        driver.enqueueLocalSelection(["C:/Music/other.mp3"]);
        verify(presenter.addAudio());
        tryCompare(presenter, "selectingAudioFiles", false);
        lyrics.hasSyncedLyrics = false;
        const before = presenter.currentOccurrenceId;
        verify(presenter.nextTrack());
        tryVerify(() => presenter.currentOccurrenceId !== before);
        tryCompare(reader, "visible", false);
        // A new track takes the verse line with it: the player's lyrics button is the way back.
        tryCompare(findChild(shell, "player-lyrics"), "activeFocus", true);
    }
    function test_drawerStatusIsAnnounced() {
        openDrawer();
        const status = findChild(shell, "lyrics-drawer-status");
        compare(status.text, "Letra sincronizada");
        compare(status.Accessible.role, Accessible.StaticText);
        lyrics.statusText = "Letra do LRCLIB aplicada.";
        tryCompare(status, "text", "Letra do LRCLIB aplicada.");
        compare(status.Accessible.name, "Letra do LRCLIB aplicada.");
        tryCompare(status, "lastAnnouncement", "Letra do LRCLIB aplicada.");
        lyrics.statusText = "";
        tryVerify(() => !status.visible);
    }
    function tabUntil(name, modifiers) {
        for (let step = 0; step < 40; ++step) {
            keyClick(Qt.Key_Tab, modifiers);
            if (shell.activeFocusItem && shell.activeFocusItem.objectName === name)
                return;
        }
        fail("Tab never reached " + name);
    }
    // The drawer is not modal and does not trap Tab: the last control hands the focus to the player, the first
    // one to the verse line, and the drawer stays open. Measured in Qt 6.11: Tab from outside does not enter the
    // drawer (popup items are tab fences), so the keyboard way back is the player's lyrics button or the verse
    // line. Everything here is keyboard only.
    function test_lyricsDrawerLetsTabLeave() {
        lyrics.readerVerses = [
            {
                text: "One",
                timeMs: 1000
            }
        ];
        lyrics.activeReaderVerse = 0;
        enableRomaji("jp-9");
        const reader = openDrawer();
        const player = findChild(shell, "player-lyrics");
        // The opening put the focus inside; Shift+Tab from the first control leaves for the verse line.
        const close = findChild(shell, "lyrics-drawer-close");
        close.forceActiveFocus(Qt.TabFocusReason);
        tryCompare(close, "activeFocus", true);
        keyClick(Qt.Key_Tab, Qt.ShiftModifier);
        const line = findChild(shell, "lyrics-line-open");
        tryCompare(line, "activeFocus", true);
        verify(!isInside(shell.activeFocusItem, reader.contentItem));
        verify(reader.opened, "the drawer stays open");
        // Tab from outside does not enter the drawer.
        keyClick(Qt.Key_Tab);
        verify(!isInside(shell.activeFocusItem, reader.contentItem), "Tab from the line stays outside: " + shell.activeFocusItem.objectName);
        const first = findChild(shell, "transport-bar").firstControl;
        compare(shell.activeFocusItem, first);
        keyClick(Qt.Key_Tab, Qt.ShiftModifier);
        compare(shell.activeFocusItem, line);
        // The way back is the verse line (Enter) or the player's lyrics button (Enter): focus moves in, nothing closes.
        keyClick(Qt.Key_Return);
        tryCompare(close, "activeFocus", true);
        verify(reader.opened);
        keyClick(Qt.Key_Tab, Qt.ShiftModifier);
        tryCompare(line, "activeFocus", true);
        tabUntil("player-lyrics", Qt.NoModifier);
        keyClick(Qt.Key_Return);
        tryCompare(close, "activeFocus", true);
        verify(reader.opened);
        // Tab through the whole drawer: the last control leaves for the first control of the player.
        let left = null;
        const walked = [];
        for (let step = 0; step < 20 && !left; ++step) {
            keyClick(Qt.Key_Tab);
            const item = shell.activeFocusItem;
            verify(item, "focus is never lost");
            if (isInside(item, reader.contentItem))
                walked.push(item.objectName);
            else
                left = item;
        }
        verify(left, "Tab leaves the drawer: " + walked.join(","));
        verify(walked.indexOf("lyrics-drawer-larger") >= 0, walked.join(","));
        compare(left, first);
        verify(reader.opened, "the drawer stays open");
        // Back inside by keyboard, Escape closes it and the focus goes to the opener kept from the first opening:
        // the player's button.
        tabUntil("player-lyrics", Qt.NoModifier);
        keyClick(Qt.Key_Return);
        tryCompare(close, "activeFocus", true);
        keyClick(Qt.Key_Escape);
        tryCompare(reader, "visible", false);
        tryCompare(player, "activeFocus", true);
    }
    // The drawer is not modal: closing it while the focus works elsewhere leaves that focus alone.
    function test_trackChangeLeavesTheFocusInAPageField() {
        driver.enqueueLocalSelection(["C:/Music/other.mp3"]);
        verify(presenter.addAudio());
        tryCompare(presenter, "selectingAudioFiles", false);
        shell.library = catalog;
        const libraryButton = findChild(shell, "library");
        tryVerify(() => libraryButton.visible);
        mouseClick(libraryButton);
        tryCompare(findChild(shell, "library-panel"), "opened", true);
        const reader = openDrawer();
        const field = findChild(shell, "library-filter");
        tryVerify(() => field.visible && field.enabled);
        // The field's left end, clear of the drawer.
        mouseClick(field, 12, field.height / 2);
        tryCompare(field, "activeFocus", true);
        verify(reader.opened, "the drawer stays open beside the page");
        const before = presenter.currentOccurrenceId;
        verify(presenter.nextTrack());
        tryVerify(() => presenter.currentOccurrenceId !== before);
        tryCompare(reader, "visible", false);
        verify(waitForRendering(shell.contentItem));
        compare(shell.activeFocusItem, field);
    }
    function test_queueOpenedBesideTheDrawerReturnsTheFocusToItsOwnOpener() {
        const reader = openDrawer();
        const queueButton = findChild(shell, "player-queue");
        tryVerify(() => queueButton.visible && queueButton.enabled);
        queueButton.forceActiveFocus(Qt.TabFocusReason);
        tryCompare(queueButton, "activeFocus", true);
        verify(reader.opened);
        keyClick(Qt.Key_Return);
        tryCompare(shell, "queueOpen", true);
        tryCompare(reader, "visible", false);
        const queue = findChild(shell, "queue-panel");
        tryVerify(() => queue.visible && isInside(shell.activeFocusItem, queue));
        keyClick(Qt.Key_Escape);
        tryCompare(shell, "queueOpen", false);
        tryCompare(queueButton, "activeFocus", true);
    }
    function sceneRect(item) {
        const corner = item.mapToItem(null, 0, 0);
        return Qt.rect(corner.x, corner.y, item.width, item.height);
    }
    function intersects(a, b) {
        return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
    }
    function test_verseLineStaysBesideTheOpenDrawer_data() {
        return [
            {
                tag: "1440",
                width: 1440,
                height: 900
            },
            {
                tag: "720",
                width: 720,
                height: 520
            }
        ];
    }
    // The drawer covers the right side of the window: the verse line ends at its left edge, so Shift+Tab out of
    // the drawer lands on a button the user can see.
    function test_verseLineStaysBesideTheOpenDrawer(data) {
        shell.width = data.width;
        shell.height = data.height;
        tryCompare(shell.contentItem, "width", data.width);
        tryCompare(shell.contentItem, "height", data.height);
        const reader = openDrawer();
        const close = findChild(shell, "lyrics-drawer-close");
        close.forceActiveFocus(Qt.TabFocusReason);
        tryCompare(close, "activeFocus", true);
        keyClick(Qt.Key_Tab, Qt.ShiftModifier);
        tryVerify(() => shell.activeFocusItem && shell.activeFocusItem.objectName === "lyrics-line-open");
        const open = shell.activeFocusItem;
        const line = findChild(shell, "lyrics-line");
        verify(waitForPolish(shell.contentItem));
        const drawer = sceneRect(reader.contentItem.parent);
        tryVerify(() => !intersects(sceneRect(open), drawer), 5000, "the focused open button is not under the drawer");
        verify(!intersects(sceneRect(line), drawer), "the verse line ends before the drawer");
        const button = sceneRect(open);
        verify(open.visible && open.width >= shell.uiContract.controlTargetMinimum);
        verify(button.x >= 0 && button.x + button.width <= drawer.x, "the open button sits in the window, left of the drawer");
        verify(findChild(shell, "lyrics-line-current").visible);
        // Closing the drawer gives the line its whole width back.
        keyClick(Qt.Key_Return);
        tryCompare(close, "activeFocus", true);
        keyClick(Qt.Key_Escape);
        tryCompare(reader, "visible", false);
        tryVerify(() => sceneRect(findChild(shell, "lyrics-line")).x + findChild(shell, "lyrics-line").width > drawer.x);
    }
    // As at startup, the lyrics binding arrives after the window: the drawer gets its first layout only when it
    // opens, after the opening revealed the current verse. Following playback, the verse is brought back into
    // view once the list has its height, like the queue centers its current row.
    function test_drawerOpensWithTheCurrentVerseInViewInTheMinimumWindow() {
        shell.destroy();
        shell = createTemporaryObject(bareShellComponent, null);
        verify(shell);
        shell.width = 720;
        shell.height = 520;
        shell.requestActivate();
        tryCompare(shell, "active", true);
        tryCompare(shell.contentItem, "width", 720);
        tryCompare(shell.contentItem, "height", 520);
        lyrics.readerVerses = ["A maré trouxe o vidro de volta", "E a areia guardou o teu nome", "Eu contei as ondas até a hora", "Em que o céu se acende e some", "Maré de vidro, leva devagar", "O que eu não soube te dizer", "Se a luz voltar pelo mar", "Eu ainda vou estar aqui pra ver", "A cidade dorme na janela", "E o rádio insiste na canção"].map((text, index) => ({
                    text: text,
                    timeMs: index * 20000
                }));
        lyrics.activeReaderVerse = 4;
        shell.lyrics = lyrics;
        verify(waitForPolish(shell.contentItem));
        const reader = openDrawer();
        const list = findChild(reader, "lyrics-reader-verses");
        verify(reader.followingPlayback);
        tryVerify(() => {
            const verse = list.itemAtIndex(4);
            return list.height > 0 && verse !== null && verse.y >= list.contentY && verse.y + verse.height <= list.contentY + list.height;
        }, 5000, "the current verse is inside the visible list");
    }
    function test_lyricsDrawerShiftTabWithoutVerseLineLandsOnThePlayerButton() {
        // Without synchronized lyrics there is no verse line: the way out backwards is the player's button.
        lyrics.hasSyncedLyrics = false;
        lyrics.plainText = "Some lyrics";
        const reader = openDrawer();
        const close = findChild(shell, "lyrics-drawer-close");
        close.forceActiveFocus(Qt.TabFocusReason);
        tryCompare(close, "activeFocus", true);
        verify(!findChild(shell, "lyrics-line").visible);
        keyClick(Qt.Key_Tab, Qt.ShiftModifier);
        const player = findChild(shell, "player-lyrics");
        tryCompare(player, "activeFocus", true);
        verify(reader.opened);
        keyClick(Qt.Key_Return);
        tryCompare(close, "activeFocus", true);
        keyClick(Qt.Key_Escape);
        tryCompare(reader, "visible", false);
        tryCompare(player, "activeFocus", true);
    }
    function test_drawerReturnsFocusToItsRealOpener() {
        const player = findChild(shell, "player-lyrics");
        const reader = findChild(shell, "lyrics-reader");
        // Opened by the player's button: Escape inside the drawer sends the focus back to that button.
        tryVerify(() => player.enabled);
        mouseClick(player);
        tryCompare(reader, "opened", true);
        keyClick(Qt.Key_Escape);
        tryCompare(reader, "visible", false);
        tryCompare(player, "activeFocus", true);
        // Opened by the verse line: the line gets the focus back.
        const line = findChild(shell, "lyrics-line-open");
        line.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        tryCompare(reader, "opened", true);
        clickDrawer("lyrics-drawer-close");
        tryCompare(reader, "visible", false);
        tryCompare(findChild(shell, "lyrics-line-open"), "activeFocus", true);
        // The opener is gone when the drawer closes (the line turned into a notice): the player's button takes over.
        mouseClick(findChild(shell, "lyrics-line-open"));
        tryCompare(reader, "opened", true);
        lyrics.variantsHasPendingChange = true;
        tryVerify(() => !findChild(shell, "lyrics-line-open") || !findChild(shell, "lyrics-line-open").visible);
        clickDrawer("lyrics-drawer-close");
        tryCompare(reader, "visible", false);
        tryCompare(player, "activeFocus", true);
    }
}
