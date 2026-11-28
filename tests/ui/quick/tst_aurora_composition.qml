import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "AuroraComposition"
    when: windowShown
    readonly property var driver: testScenarioDriver // qmllint disable unqualified
    readonly property PlayerPresenter presenter: testLocalPlayerPresenter // qmllint disable unqualified
    readonly property ShellUiContract contract: testProductUiContract // qmllint disable unqualified
    property var shell
    Component {
        id: shellComponent
        Main {
            uiContract: testCase.contract
            presenter: testCase.presenter
        }
    }
    ListModel {
        id: fakeRecords
    }
    ListModel {
        id: fakeRecent
    }
    QtObject {
        id: fakeLyrics
        property string title: "Cantiga"
        property string artist: "Artista"
        property string album: "Disco"
        property int durationMs: 180000
        property string statusText: ""
        property string currentLine: "Primeiro verso"
        property string nextLine: "Segundo verso"
        property string plainText: ""
        property bool hasSyncedLyrics: true
        property bool busy: false
        property bool canSearch: true
        property bool lyricsCopyProtected: false
    }
    QtObject {
        id: captureLyrics
        property string title: "Maré de Vidro"
        property string artist: "Lia Campos"
        property string album: "Depois da Chuva"
        property int durationMs: 217000
        property string statusText: "Letra sincronizada"
        property string currentLine: "Maré de vidro, leva devagar"
        property string previousLine: "Em que o céu se acende e some"
        property string nextLine: "O que eu não soube te dizer"
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
        property string lyricsAttribution: ""
        property bool recordingSearchBusy: false
        property string recordingSearchStatus: ""
        property string recordingSearchRevision: ""
        property var recordingCandidates: []
        property bool canSaveRecording: false
        signal romajiDraftReady(string token, string content)
        function generateRomaji() {
            return "";
        }
        function discardRomaji(token) {
        }
        function searchRecordings(title, artist, album, duration) {
            return true;
        }
        function previewRecording(index, format, revision) {
            return "";
        }
        function useRecording(index, format, revision) {
            return false;
        }
        function cancelRecordingSearch() {
        }
    }
    QtObject {
        id: fakeLibrary
        property bool ready: true
        property bool busy: false
        property int count: 128
        property int visibleCount: 0
        property bool hasPendingChange: false
        property bool canRetrySave: false
        property string statusText: ""
        property string filterText: ""
        property string playingRecordId: ""
        property bool statusProblem: false
        property var model: fakeRecords
        property var recentModel: fakeRecent
        property bool canLocate: false
        property string artworkBase: ""
        property var played: []
        function playNow(id) {
            played = played.concat([id]);
            return true;
        }
        function retryLoad() {
            return true;
        }
        function cancelLocate() {
        }
        function retrySave() {
            return true;
        }
    }
    QtObject {
        id: fakePlaylists
        property bool ready: true
        property bool busy: false
        property bool interactionAllowed: true
        property bool hasPendingChange: false
        property bool canRetrySave: false
        property bool canAddFiles: false
        property bool canReplaceFile: false
        property string statusText: ""
        property string selectedPlaylistId: ""
        property var playlists: [
            {
                id: "a",
                name: "Uma",
                count: 0
            },
            {
                id: "b",
                name: "Outra",
                count: 0
            }
        ]
        property var entries: []
        function retryLoad() {
            return true;
        }
    }
    QtObject {
        id: fakeSearch
        property bool available: true
        property bool busy: false
        property bool requesting: false
        property bool searched: false
        property string query: ""
        property string statusText: ""
        property var results: []
        property bool linksMode: false
        property var linkIssues: []
        property bool canDownloadAll: false
    }
    QtObject {
        id: fakeRoom
        property bool available: false
        property int role: 0
    }
    QtObject {
        id: fakeDownloads
        property bool canCloseAfterFinalSaveFailure: false
        property bool canRetryAdmission: false
        property bool canRetryFinalSave: false
        property bool canRetryStartup: false
        property bool canRenewNamespace: false
        property string warningText: ""
        property string statusText: ""
        property string importConsentJobId: ""
        property string importConsentToken: ""
        property string discardConsentJobId: ""
        property string discardConsentToken: ""
        property var jobsModel: null
    }
    QtObject {
        id: fakeProfile
        property bool needsSetup: false
        property bool available: true
        property string nickname: "Bia"
        property int avatarIndex: 7
        readonly property int avatarCount: 20
        readonly property url avatarSource: "qrc:/rc_music/avatars/avatar-08.svg"
        property var customAvatars: []
        function acceptsNickname(text) {
            return text.length > 0;
        }
        function setNickname(text) {
            return false;
        }
        function chooseBundledAvatar(index) {
            return false;
        }
        function chooseCustomAvatar(file) {
            return false;
        }
    }
    QtObject {
        id: fakeBrowser
        property string errorText: ""
        signal errorChanged
        function open() {
        }
    }

    // The captures show the whole sidebar: counts, the profile chip and the external window icon.
    function mountShellBackends() {
        fakeProfile.needsSetup = false;
        shell.library = fakeLibrary;
        shell.playlists = fakePlaylists;
        shell.onlineSearch = fakeSearch;
        shell.room = fakeRoom;
        shell.downloads = fakeDownloads;
        shell.embeddedBrowser = fakeBrowser;
        shell.profile = fakeProfile;
        wait(0);
        verify(waitForPolish(shell));
    }
    function init() {
        driver.resetLocalScenario();
        driver.setLocalQueueArtworkBase("");
        fakeLibrary.ready = true;
        fakeLibrary.count = 128;
        fakeLibrary.played = [];
        fakeLibrary.playingRecordId = "";
        fakeRecent.clear();
        driver.setHighContrast(false, false);
        driver.setSystemAppearance(false, false);
        presenter.setThemePreference(PlayerPresenter.LightTheme);
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
        driver.setLocalQueueArtworkBase("");
        driver.setHighContrast(false, false);
        presenter.setThemePreference(PlayerPresenter.SystemTheme);
    }
    function loadQueue() {
        driver.enqueueLocalSelection(["C:/Fixture/Primeira.mp3", "C:/Fixture/Repetida.mp3", "C:/Fixture/Repetida.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalPlaying();
        driver.publishLocalPosition(41000, 180000);
        presenter.setVolume(0.52);
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
    }
    function commands() {
        return [driver.localLoadCount(), driver.localUnloadCount(), driver.localPlayCount(), driver.localPauseCount(), driver.localSeekCount()];
    }
    function point(item) {
        return item.mapToItem(shell.contentItem, 0, 0);
    }
    function assertControlInside(item) {
        verify(item && item.visible && item.width >= 40 && item.height >= 40);
        const p = point(item);
        verify(p.x >= 0 && p.y >= 0);
        verify(p.x + item.width <= shell.width + 1);
        verify(p.y + item.height <= shell.height + 1);
    }
    function test_editorialHomeShowsRealQueueAndKeepsIdentity() {
        loadQueue();
        const audio = commands();
        const identity = presenter.currentOccurrenceId;
        const header = findChild(shell, "workspace-header");
        verify(header && header.visible, "The musical workspace needs its own header");
        const list = findChild(shell, "home-queue-list");
        verify(list, "The initial page must project the real queue");
        tryCompare(list, "count", 3);
        verify(waitForPolish(shell));
        const cover = findChild(shell, "cover-track");
        const title = findChild(shell, "current-title");
        verify(point(title).x >= point(cover).x + cover.width + 20, "The hero must be horizontal rather than a centered compact player");
        compare(title.text, "Primeira");
        const transport = findChild(shell, "transport-bar");
        findChild(shell, "home-see-queue").Accessible.pressAction();
        tryCompare(shell, "currentPage", "queue");
        findChild(shell, "now").Accessible.pressAction();
        tryCompare(shell, "currentPage", "home");
        compare(findChild(shell, "transport-bar"), transport);
        compare(presenter.currentOccurrenceId, identity);
        compare(presenter.positionMs, 41000);
        compare(presenter.volume, 0.52);
        compare(commands(), audio);
    }
    function test_queueCardsKeepExactOccurrenceIdentity() {
        driver.setLocalQueueArtworkBase("image://queueart/");
        loadQueue();
        const list = findChild(shell, "home-queue-list");
        verify(list);
        tryCompare(list, "count", 3);
        list.forceLayout();
        tryVerify(() => list.itemAtIndex(2) !== null);
        const second = list.itemAtIndex(1);
        const third = list.itemAtIndex(2);
        verify(second.occurrenceId !== third.occurrenceId, "Duplicate paths are distinct queue occurrences");
        compare(second.displayTitle, "Repetida");
        compare(third.displayTitle, "Repetida");
        const thirdId = third.occurrenceId;
        const audio = commands();
        driver.publishArtworkLocalMetadata();
        tryVerify(() => presenter.currentArtworkSource.toString().length > 0);
        tryCompare(list.itemAtIndex(0), "displayTitle", "Título controlado");
        const secondCover = findChild(second, "shelf-card-artwork").source.toString();
        compare(secondCover, "image://queueart/" + occurrenceValue(second.occurrenceId), "Each card shows its own occurrence's cover");
        verify(secondCover !== presenter.currentArtworkSource.toString(), "Do not attribute the playing track's cover to another occurrence");
        compare(commands(), audio);
        const loads = driver.localLoadCount();
        third.Accessible.pressAction();
        tryCompare(presenter, "currentOccurrenceId", thirdId);
        compare(driver.localLoadCount(), loads + 1, "Only explicit card playback issues the load");
        verify(presenter.removeQueueOccurrence(second.occurrenceId));
        tryCompare(list, "count", 2);
        list.forceLayout();
        tryVerify(() => list.itemAtIndex(1) !== null);
        compare(list.itemAtIndex(1).occurrenceId, thirdId);
    }
    function occurrenceValue(occurrenceId) {
        return occurrenceId.substring(occurrenceId.indexOf("-") + 1);
    }
    function shelfCard(list, index) {
        list.positionViewAtIndex(index, ListView.Contain);
        list.forceLayout();
        tryVerify(() => list.itemAtIndex(index) !== null);
        return list.itemAtIndex(index);
    }
    function test_queueShelfMatchesTheMockup() {
        driver.setLocalQueueArtworkBase("image://queueart/");
        loadEightTracks(true);
        verify(waitForPolish(shell));
        const shelf = findChild(shell, "home-queue-shelf");
        tryVerify(() => shelf.visible);
        compare(findChild(shelf, "shelf-title").text, "Na sua fila");
        const see = findChild(shelf, "home-see-queue");
        compare(see.text, "Ver fila");
        const list = findChild(shelf, "home-queue-list");
        tryCompare(list, "count", 8);
        tryCompare(presenter, "currentQueueIndex", 1);
        for (let index = 0; index < 3; ++index) {
            const card = shelfCard(list, index);
            compare(card.width, 150);
            const artwork = findChild(card, "shelf-card-artwork");
            compare(artwork.width, 150);
            compare(artwork.height, 150);
            compare(findChild(card, "shelf-card-playing").visible, index === 1, "only the current card says it is playing");
        }
        compare(findChild(findChild(shelfCard(list, 1), "shelf-card-playing"), "chip-label").text, "Tocando");
        const third = shelfCard(list, 2);
        compare(third.Accessible.name, "Tocar Cais");
        compare(findChild(third, "shelf-card-artwork").source.toString(), "image://queueart/" + occurrenceValue(third.occurrenceId));
        see.Accessible.pressAction();
        tryCompare(shell, "currentPage", "queue");
    }
    // The chip says what the hero says: only a track that is playing is "playing".
    function test_playingChipFollowsThePlaybackState() {
        loadQueue();
        const list = findChild(shell, "home-queue-list");
        tryCompare(list, "count", 3);
        const chip = findChild(shelfCard(list, 0), "shelf-card-playing");
        verify(chip.visible);
        driver.publishLocalPaused();
        tryCompare(presenter, "playbackState", PlayerPresenter.Paused);
        tryCompare(chip, "visible", false);
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        tryCompare(chip, "visible", true);
        // An output error keeps the same occurrence current instead of moving on to the next one.
        driver.publishLocalOutputError(false);
        tryCompare(presenter, "playbackState", PlayerPresenter.Error);
        compare(presenter.currentQueueIndex, 0);
        tryCompare(chip, "visible", false);
    }
    function test_queueSubtitleNamesTheNextOnlyWhenKnown() {
        loadEightTracks(true);
        const subtitle = findChild(findChild(shell, "home-queue-shelf"), "shelf-subtitle");
        tryCompare(subtitle, "text", "8 faixas, a próxima é “Cais”");
        verify(presenter.toggleShuffleQueue());
        tryCompare(subtitle, "text", "8 faixas");
        verify(presenter.toggleShuffleQueue());
        verify(!presenter.shuffleQueueEnabled);
        tryVerify(() => subtitle.text.startsWith("8 faixas, a próxima é “"));
        const list = findChild(shell, "home-queue-list");
        verify(presenter.playQueueOccurrence(shelfCard(list, 7).occurrenceId));
        tryCompare(presenter, "currentQueueIndex", 7);
        tryCompare(subtitle, "text", "8 faixas");
        verify(presenter.clearQueue());
        driver.enqueueLocalSelection(["C:/Música/Sozinha.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        tryCompare(subtitle, "text", "1 faixa");
    }
    function test_cardTitleWrapsToTwoLinesAndKeepsTheFullName() {
        loadQueue();
        const longTitle = "Palavra longa ".repeat(9).substring(0, 120);
        compare(longTitle.length, 120);
        driver.publishNamedLocalMetadata(longTitle, "Artista", "");
        const list = findChild(shell, "home-queue-list");
        const card = shelfCard(list, 0);
        const title = findChild(card, "shelf-card-title");
        tryCompare(title, "text", longTitle);
        verify(waitForPolish(shell));
        compare(title.elide, Text.ElideRight);
        verify(title.lineCount <= 2, "the title takes at most two lines, got " + title.lineCount);
        compare(title.lineCount, 2, "a long title wraps instead of being cut on one line");
        verify(title.truncated);
        compare(card.Accessible.name, "Tocar " + longTitle);
    }
    function test_unavailableQueueCardSaysSoAndIsSkipped() {
        driver.setLocalQueueArtworkBase("image://queueart/");
        loadEightTracks(true);
        verify(waitForPolish(shell));
        const list = findChild(shell, "home-queue-list");
        tryCompare(list, "count", 8);
        const first = shelfCard(list, 0);
        tryCompare(first, "enabled", false);
        const subtitle = findChild(first, "shelf-card-subtitle");
        compare(subtitle.text, "Arquivo não disponível.");
        verify(Qt.colorEqual(subtitle.color, contract.warningColor), "the unavailable file is a warning");
        compare(findChild(first, "shelf-card-artwork").source.toString(), "", "no cover is read for a missing file");
        const see = findChild(shell, "home-see-queue");
        see.forceActiveFocus(Qt.TabFocusReason);
        tryCompare(see, "activeFocus", true);
        keyClick(Qt.Key_Tab);
        tryVerify(() => shelfCard(list, 1).activeFocus, 5000, "Tab skips the unavailable card");
        verify(!first.activeFocus);
    }
    function fillRecent(total) {
        for (let index = 0; index < total; ++index)
            fakeRecent.append({
                recordId: String(200 - index),
                title: "Faixa " + index,
                artist: "Artista " + index,
                available: index !== 3
            });
    }
    function test_libraryShelfCardsPlayNow() {
        fillRecent(12);
        mountShellBackends();
        const shelf = findChild(shell, "home-library-shelf");
        tryVerify(() => shelf.visible);
        compare(findChild(shelf, "shelf-title").text, "Da sua biblioteca");
        compare(findChild(shelf, "shelf-subtitle").text, "128 faixas");
        const list = findChild(shelf, "home-library-list");
        tryCompare(list, "count", 12);
        const card = shelfCard(list, 0);
        compare(card.Accessible.role, Accessible.Button);
        compare(card.Accessible.name, "Tocar Faixa 0 agora");
        compare(findChild(card, "shelf-card-title").text, "Faixa 0");
        compare(findChild(card, "shelf-card-subtitle").text, "Artista 0");
        const unavailableCard = shelfCard(list, 3);
        // As in the mockup: the artist stays in the subtitle, the chip says it, the card is dimmed, the reason is read.
        compare(findChild(unavailableCard, "shelf-card-subtitle").text, "Artista 3");
        verify(findChild(unavailableCard, "shelf-card-unavailable").visible);
        compare(findChild(unavailableCard, "shelf-card-unavailable").text, "Indisponível");
        for (const name of ["shelf-card-artwork", "shelf-card-title", "shelf-card-subtitle"])
            compare(findChild(unavailableCard, name).opacity, 0.6, name);
        compare(findChild(unavailableCard, "shelf-card-unavailable").opacity, 1);
        compare(unavailableCard.enabled, false);
        compare(unavailableCard.Accessible.description, "Arquivo não encontrado. Localize o arquivo para tocar a faixa.");
        verify(!findChild(card, "shelf-card-playing").visible);
        mouseClick(card);
        tryCompare(fakeLibrary, "played", ["200"]);
        const second = shelfCard(list, 1);
        second.forceActiveFocus(Qt.TabFocusReason);
        tryCompare(second, "activeFocus", true);
        keyClick(Qt.Key_Return);
        tryCompare(fakeLibrary, "played", ["200", "199"]);
        mouseClick(unavailableCard);
        compare(fakeLibrary.played.length, 2, "an unavailable card plays nothing");
        // The wave badge follows the track that plays, and only while it plays.
        loadQueue();
        fakeLibrary.playingRecordId = "200";
        tryVerify(() => findChild(shelfCard(list, 0), "shelf-card-playing").visible);
        compare(shelfCard(list, 0).Accessible.description, "Tocando");
        verify(!findChild(shelfCard(list, 1), "shelf-card-playing").visible);
        driver.publishLocalPaused();
        tryCompare(presenter, "playbackState", PlayerPresenter.Paused);
        tryVerify(() => !findChild(shelfCard(list, 0), "shelf-card-playing").visible);
        const see = findChild(shelf, "home-see-library");
        compare(see.text, "Ver toda a biblioteca");
        see.Accessible.pressAction();
        tryCompare(shell, "currentPage", "library");
    }
    // A home card has one gesture, the click that plays: hovering it shows no play, enqueue or more button, the
    // playing chip stays, and a click where the play button would be still plays the card.
    function verifyHoverShowsNoActions(card, playing) {
        const spot = card.coverSize - 28;
        // The focus scrolls the home to the card, so the pointer lands on it.
        card.forceActiveFocus(Qt.TabFocusReason);
        verify(waitForPolish(shell));
        mouseMove(card, spot, spot);
        tryCompare(card, "hovered", true, 5000, card.title);
        for (const name of ["shelf-card-play", "shelf-card-enqueue", "shelf-card-more"])
            verify(!findChild(card, name).visible, name + " shows on a home card");
        compare(findChild(card, "shelf-card-playing").visible, playing, "the playing chip ignores the hover");
        return spot;
    }
    function test_hoveringAHomeCardShowsNoActions() {
        fillRecent(3);
        mountShellBackends();
        loadQueue();
        fakeLibrary.playingRecordId = "200";
        const queueList = findChild(shell, "home-queue-list");
        tryCompare(queueList, "count", 3);
        verifyHoverShowsNoActions(shelfCard(queueList, 0), true);
        const next = shelfCard(queueList, 1);
        const nextId = next.occurrenceId;
        const queueSpot = verifyHoverShowsNoActions(next, false);
        mouseClick(next, queueSpot, queueSpot);
        tryCompare(presenter, "currentOccurrenceId", nextId);
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        const libraryList = findChild(shell, "home-library-list");
        tryCompare(libraryList, "count", 3);
        verifyHoverShowsNoActions(shelfCard(libraryList, 0), true);
        const second = shelfCard(libraryList, 1);
        const librarySpot = verifyHoverShowsNoActions(second, false);
        mouseClick(second, librarySpot, librarySpot);
        tryCompare(fakeLibrary, "played", ["199"]);
    }
    // The open file chooser is a real reason to refuse opening; the room guest is refused by the presenter itself.
    function test_libraryShelfCardsAreDisabledWithoutOpening() {
        fillRecent(3);
        mountShellBackends();
        const list = findChild(shell, "home-library-list");
        tryCompare(list, "count", 3);
        verify(shelfCard(list, 0).enabled);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "selectingAudioFiles", true);
        tryCompare(shelfCard(list, 0), "enabled", false);
        tryCompare(shelfCard(list, 1), "enabled", false);
        const see = findChild(shell, "home-see-library");
        see.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Tab);
        verify(!focusIsInside(list), "the disabled cards are out of the Tab order");
        driver.cancelLocalAudioSelection();
        tryCompare(presenter, "selectingAudioFiles", false);
        tryCompare(shelfCard(list, 0), "enabled", true);
    }
    function focusIsInside(list) {
        for (let item = shell.activeFocusItem; item; item = item.parent) {
            if (item === list)
                return true;
        }
        return false;
    }
    // A guard on the page: a save that only changes another record leaves the shelf where it was. The real
    // fix, the model without a reset, is C++ work with its own test; the fake model never resets.
    function test_libraryShelfKeepsItsPlaceOnSave() {
        fillRecent(12);
        mountShellBackends();
        const list = findChild(shell, "home-library-list");
        tryCompare(list, "count", 12);
        const card = shelfCard(list, 8);
        card.forceActiveFocus(Qt.TabFocusReason);
        tryCompare(card, "activeFocus", true);
        verify(waitForPolish(shell));
        const before = list.contentX;
        verify(before > 0, "the list is scrolled to the eighth card");
        fakeRecent.setProperty(2, "artist", "Outro artista");
        // The changed card is only looked at: asking for it by index would scroll the list to it.
        tryVerify(() => list.itemAtIndex(2) !== null && findChild(list.itemAtIndex(2), "shelf-card-subtitle").text === "Outro artista");
        compare(list.contentX, before);
        verify(list.itemAtIndex(8).activeFocus, "the focus stays on the eighth card");
    }
    // Opening the library from the shelf link hides the whole home: the focus goes to the library, not to open-audio.
    function test_openingTheLibraryFromTheShelfLinkKeepsTheFocusThere() {
        fillRecent(3);
        mountShellBackends();
        const see = findChild(shell, "home-see-library");
        tryVerify(() => see.visible);
        see.forceActiveFocus(Qt.TabFocusReason);
        tryCompare(see, "activeFocus", true);
        keyClick(Qt.Key_Return);
        tryCompare(shell, "currentPage", "library");
        const panel = findChild(shell, "library-panel");
        tryVerify(() => focusInside(panel), 5000, "focus: " + shell.activeFocusItem);
        verify(!findChild(shell, "open-audio").activeFocus);
        wait(0);
        verify(focusInside(panel), "the focus stays in the library; now: " + shell.activeFocusItem);
    }
    // A click on a card starts playback and leaves the home shown: the focus stays on the home.
    function test_clickingAShelfCardKeepsTheFocusOnTheHome() {
        fillRecent(3);
        mountShellBackends();
        const list = findChild(shell, "home-library-list");
        tryCompare(list, "count", 3);
        const card = shelfCard(list, 0);
        mouseClick(card);
        tryCompare(fakeLibrary, "played", ["200"]);
        tryCompare(card, "activeFocus", true);
        verify(!findChild(shell, "open-audio").activeFocus);
    }
    function focusInside(root) {
        for (let item = shell.activeFocusItem; item; item = item.parent) {
            if (item === root)
                return true;
        }
        return false;
    }
    function test_focusLeavesAHidingLibraryShelfForOpenAudio() {
        fillRecent(3);
        mountShellBackends();
        const shelf = findChild(shell, "home-library-shelf");
        tryVerify(() => shelf.visible);
        const see = findChild(shell, "home-see-library");
        see.forceActiveFocus(Qt.TabFocusReason);
        tryCompare(see, "activeFocus", true);
        fakeLibrary.ready = false;
        tryVerify(() => !shelf.visible);
        tryVerify(() => findChild(shell, "open-audio").activeFocus, 5000, "focus: " + shell.activeFocusItem);
        fakeLibrary.ready = true;
        tryVerify(() => shelf.visible);
        const card = shelfCard(findChild(shell, "home-library-list"), 1);
        card.forceActiveFocus(Qt.TabFocusReason);
        tryCompare(card, "activeFocus", true);
        fakeLibrary.ready = false;
        tryVerify(() => !shelf.visible);
        tryVerify(() => findChild(shell, "open-audio").activeFocus, 5000, "focus: " + shell.activeFocusItem);
    }
    function test_libraryShelfHidesWithoutTracks() {
        fakeRecent.append({
            recordId: "1",
            title: "Faixa",
            artist: "Artista",
            available: true
        });
        const shelf = findChild(shell, "home-library-shelf");
        verify(shelf);
        compare(shell.library, null);
        verify(!shelf.visible, "no library, no shelf");
        mountShellBackends();
        tryVerify(() => shelf.visible);
        fakeLibrary.count = 0;
        tryVerify(() => !shelf.visible, 5000, "an empty library shows no shelf");
    }
    // The user's text is substituted once: a title with a placeholder keeps it literally.
    function test_shelfTextsKeepPlaceholdersInTitles() {
        fakeRecent.append({
            recordId: "1",
            title: "Faixa %2 ao vivo",
            artist: "Artista",
            available: true
        });
        mountShellBackends();
        const list = findChild(shell, "home-library-list");
        tryCompare(list, "count", 1);
        compare(shelfCard(list, 0).Accessible.name, "Tocar Faixa %2 ao vivo agora");
        driver.enqueueLocalSelection(["C:/Música/Primeira.mp3", "C:/Música/Cem %1 por cento.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        const subtitle = findChild(findChild(shell, "home-queue-shelf"), "shelf-subtitle");
        tryCompare(subtitle, "text", "2 faixas, a próxima é “Cem %1 por cento”");
    }
    // A queue whose every file is gone has no card to focus: Tab and Backtab cross the shelf both ways.
    function test_allUnavailableQueueShelfDoesNotTrapFocus() {
        fakeRecent.append({
            recordId: "1",
            title: "Faixa",
            artist: "Artista",
            available: true
        });
        mountShellBackends();
        driver.enqueueLocalSelection(["C:/Música/Ausente 1.mp3", "C:/Música/Ausente 2.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        const first = presenter.currentOccurrenceId;
        driver.publishLocalSourceUnavailable();
        tryVerify(() => presenter.currentOccurrenceId !== first);
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalSourceUnavailable();
        const list = findChild(shell, "home-queue-list");
        tryCompare(list, "count", 2);
        tryVerify(() => !shelfCard(list, 0).enabled && !shelfCard(list, 1).enabled, 5000, "every queue card is unavailable");
        const seeQueue = findChild(shell, "home-see-queue");
        const seeLibrary = findChild(shell, "home-see-library");
        tryVerify(() => findChild(shell, "home-queue-shelf").visible && seeQueue.enabled && seeLibrary.visible);
        seeLibrary.forceActiveFocus(Qt.TabFocusReason);
        tryCompare(seeLibrary, "activeFocus", true);
        keyClick(Qt.Key_Backtab);
        tryCompare(seeQueue, "activeFocus", true);
        keyClick(Qt.Key_Tab);
        tryCompare(seeLibrary, "activeFocus", true);
        keyClick(Qt.Key_Backtab);
        tryCompare(seeQueue, "activeFocus", true);
    }
    function test_headerHeroAndFooterUseSeparateResponsiveRegions_data() {
        return [
            {
                tag: "wide-light",
                width: 1440,
                height: 900,
                dark: false
            },
            {
                tag: "wide-dark",
                width: 1440,
                height: 900,
                dark: true
            },
            {
                tag: "medium-light",
                width: 1024,
                height: 768,
                dark: false
            },
            {
                tag: "medium-dark",
                width: 1024,
                height: 768,
                dark: true
            },
            {
                tag: "minimum-light",
                width: 720,
                height: 520,
                dark: false
            },
            {
                tag: "minimum-dark",
                width: 720,
                height: 520,
                dark: true
            }
        ];
    }
    function test_headerHeroAndFooterUseSeparateResponsiveRegions(data) {
        loadQueue();
        mountShellBackends();
        shell.width = data.width;
        shell.height = data.height;
        presenter.setThemePreference(data.dark ? PlayerPresenter.DarkTheme : PlayerPresenter.LightTheme);
        tryCompare(contract, "darkTheme", data.dark);
        tryCompare(shell.contentItem, "width", data.width);
        tryCompare(shell.contentItem, "height", data.height);
        verify(waitForPolish(shell));
        const header = findChild(shell, "workspace-header");
        const footer = findChild(shell, "player-footer");
        const home = findChild(shell, "state-layer");
        verify(header && header.visible);
        compare(header.height, 60);
        verify(point(home).y >= header.height);
        verify(point(home).y + home.height <= footer.y + 1);
        // Player C: the track identity sits inside the player, beside the controls.
        const identity = findChild(shell, "player-identity");
        verify(identity && identity.visible);
        let identityOwner = identity.parent;
        while (identityOwner && identityOwner.objectName !== "transport-bar")
            identityOwner = identityOwner.parent;
        verify(identityOwner, "the identity belongs to the transport bar");
        assertControlInside(identity);
        verify(footer.height <= (data.width >= 1200 ? 130 : 184), "The footer must not remain a full-width enlarged compact player");
        compare(footer.y + footer.height, shell.height);
        for (const name of ["now", "player-queue", "preferences", "theme-toggle", "play-pause", "mute", "player-shuffle", "player-repeat"])
            assertControlInside(findChild(shell, name));
        // The compact window drops the volume bar and keeps the mute button.
        if (data.width > 900)
            assertControlInside(findChild(shell, "volume"));
        else
            verify(!findChild(shell, "volume").visible, "the compact player hides the volume bar");
        // The progress is the 24 px exception: the player's full-width top edge.
        const progress = findChild(shell, "progress");
        verify(progress && progress.visible);
        compare(progress.height, 24);
        compare(point(progress).x, point(footer).x);
        compare(point(progress).y, point(footer).y);
        compare(progress.width, footer.width);
        if (data.width === 720) {
            compare(findChild(shell, "side-navigation").width, 68);
            verify(home.contentHeight > home.height, "The small workspace must scroll instead of squeezing controls");
        }
        // The sidebar pieces the captures must show; the compact sidebar keeps the icons only.
        const compactSidebar = data.width <= contract.compactWidth;
        compare(findChild(findChild(shell, "library"), "navigation-count").visible, !compactSidebar);
        compare(findChild(findChild(shell, "playlists"), "navigation-count").visible, !compactSidebar);
        compare(findChild(findChild(shell, "embedded-browser"), "navigation-trailing-icon").visible, !compactSidebar);
        assertControlInside(findChild(shell, "profile-chip"));
        shell.update();
        verify(waitForRendering(shell.contentItem));
        const image = grabImage(shell.contentItem);
        compare(image.width, data.width);
        compare(image.height, data.height);
        // The capture is evidence for visual review: saved only when RC_MUSIC_UI_ARTIFACT_DIR is set.
        const artifact = driver.artifactPath("aurora-redesign-" + data.tag + ".png");
        if (artifact.length > 0)
            image.save(artifact);
    }
    function test_homeKeepsFocusedActionVisibleWhenResized() {
        loadQueue();
        const action = findChild(shell, "open-audio");
        action.forceActiveFocus(Qt.TabFocusReason);
        tryCompare(action, "activeFocus", true);
        shell.width = 720;
        shell.height = 520;
        tryCompare(shell.contentItem, "height", 520);
        verify(waitForPolish(shell));
        const home = findChild(shell, "state-layer");
        tryVerify(() => {
            const p = action.mapToItem(home, 0, 0);
            return p.y >= 0 && p.y + action.height <= home.height + 1;
        }, 5000, "Resizing must reveal the already focused action, without refocusing it");
        assertControlInside(action);
    }
    function test_homeHeaderOffersOpenAudio() {
        const header = findChild(shell, "home-header");
        verify(header && header.visible);
        compare(findChild(header, "page-eyebrow").text, "SEU ESPAÇO MUSICAL");
        compare(findChild(header, "page-title").text, "Bom ter sua música por perto.");
        compare(findChild(header, "page-description").text, "O que está tocando, sua fila e sua biblioteca.");
        const open = findChild(header, "open-audio");
        verify(open);
        compare(open.kind, AuroraButton.Primary);
        compare(open.iconName, "folder-open");
        compare(open.height, 40);
        verify(open.visible && open.enabled);
        loadQueue();
        verify(open.visible && open.enabled, "Abrir áudio stays available with a track playing");
    }
    function test_emptyHeroUsesTheEmptyStatePiece() {
        const empty = findChild(shell, "home-empty");
        verify(empty && empty.visible);
        compare(findChild(empty, "empty-title").text, "Abra um áudio para começar");
        compare(findChild(empty, "empty-hint").text, "ou arraste arquivos para esta janela");
        verify(!findChild(shell, "home-empty-primary").visible);
        verify(!findChild(shell, "home-empty-secondary").visible);
        const hero = findChild(shell, "home-hero");
        compare(hero.border.width, 0);
        compare(hero.radius, 16);
    }
    Component {
        id: floatingCardComponent
        AuroraFloatingCard {
            contract: testCase.contract
        }
    }
    // How many AuroraShadow items under the root are visible: the shadow hides itself in high contrast.
    function visibleShadows(root) {
        let count = 0;
        const stack = [root];
        while (stack.length > 0) {
            const item = stack.pop();
            if (item.objectName === "surface-shadow" && item.visible)
                ++count;
            for (const child of item.children)
                stack.push(child);
        }
        return count;
    }
    function test_homeHighContrast() {
        // The control: the same search sees the shadow of a real Aurora surface, and sees it leave in high contrast.
        const card = createTemporaryObject(floatingCardComponent, shell.contentItem);
        verify(card);
        compare(visibleShadows(card), 1, "the search finds the shadow of a floating card");
        driver.setHighContrast(true, false);
        tryVerify(() => contract.highContrast);
        compare(visibleShadows(card), 0, "the search sees the card shadow go away in high contrast");
        loadEightTracks(true);
        verify(waitForPolish(shell));
        const hero = findChild(shell, "home-hero");
        compare(hero.border.width, 2);
        verify(Qt.colorEqual(hero.border.color, contract.textPrimaryColor), "the hero outline is the system text color");
        compare(visibleShadows(hero), 0, "no shadow inside the hero");
        compare(visibleShadows(hero.parent), 0, "no shadow beside the hero");
        for (const id of ["track-details-playing", "home-lyrics"]) {
            const button = findChild(hero, id);
            verify(button && button.visible, id);
            compare(button.background.border.width, 2, id + " is outlined");
        }
        verify(Qt.colorEqual(findChild(hero, "hero-eyebrow").color, contract.textSecondaryColor));
        verify(Qt.colorEqual(findChild(hero, "hero-artist-album").color, contract.textSecondaryColor));
        verify(Qt.colorEqual(findChild(hero, "current-title").color, contract.textPrimaryColor));
        const shelf = findChild(shell, "home-queue-shelf");
        tryVerify(() => shelf.visible);
        verify(Qt.colorEqual(findChild(shelf, "shelf-title").color, contract.textPrimaryColor));
        verify(Qt.colorEqual(findChild(shelf, "shelf-subtitle").color, contract.textSecondaryColor));
        const list = findChild(shelf, "home-queue-list");
        tryCompare(list, "count", 8);
        const missing = findChild(shelfCard(list, 0), "shelf-card-subtitle");
        verify(Qt.colorEqual(missing.color, contract.warningColor));
        verify(Qt.colorEqual(findChild(shelfCard(list, 2), "shelf-card-subtitle").color, contract.textSecondaryColor));
        verify(Qt.colorEqual(findChild(shelfCard(list, 2), "shelf-card-title").color, contract.textPrimaryColor));
        // The error state: the same outline on the hero and on its tonal button.
        verify(presenter.clearQueue());
        driver.enqueueLocalSelection(["C:/Música/Arquivo movido.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalSourceUnavailable();
        tryCompare(presenter, "playbackState", PlayerPresenter.Error);
        verify(waitForPolish(shell));
        compare(hero.border.width, 2);
        const details = findChild(hero, "track-details-error");
        verify(details && details.visible);
        compare(details.background.border.width, 2);
        compare(findChild(hero, "error-icon-circle").border.width, 2);
        verify(Qt.colorEqual(findChild(findChild(hero, "error-icon-circle"), "empty-icon").color, contract.textPrimaryColor));
    }
    function test_playingHeroMatchesTheMockup() {
        loadQueue();
        verify(waitForPolish(shell));
        const cover = findChild(shell, "cover-track");
        compare(cover.width, 168);
        compare(cover.height, 168);
        const title = findChild(shell, "current-title");
        compare(title.font.family, contract.headingFontFamily);
        compare(title.font.pixelSize, 36);
        const artistAlbum = findChild(shell, "hero-artist-album");
        compare(artistAlbum.text, "");
        driver.publishCompleteLocalMetadata();
        tryCompare(artistAlbum, "text", "Artista · Álbum");
        driver.enqueueLocalSelection(["C:/Fixture/So artista.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "currentTitle", "So artista");
        compare(artistAlbum.text, "");
        driver.publishNamedLocalMetadata("So artista", "Artista", "");
        tryCompare(artistAlbum, "text", "Artista");
        const details = findChild(shell, "track-details-playing");
        compare(details.text, "Detalhes da faixa");
        // The lyrics button needs both lyrics and a current track.
        const lyricsButton = findChild(shell, "home-lyrics");
        compare(lyricsButton.text, "Letra");
        verify(!lyricsButton.enabled);
        mountShellBackends();
        shell.lyrics = captureLyrics;
        tryVerify(() => lyricsButton.enabled);
        const reader = findChild(shell, "lyrics-reader");
        mouseClick(lyricsButton);
        tryCompare(reader, "opened", true);
        keyClick(Qt.Key_Escape);
        tryCompare(reader, "opened", false);
        tryCompare(lyricsButton, "activeFocus", true);
        // A placeholder inside the tags stays text: the album keeps its own slot.
        driver.enqueueLocalSelection(["C:/Fixture/Marcadores.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "currentTitle", "Marcadores");
        driver.publishNamedLocalMetadata("Marcadores", "AC%2DC %1", "Álbum");
        tryCompare(artistAlbum, "text", "AC%2DC %1 · Álbum");
    }
    // The error's recovery actions use the hero's width: one centered line when they fit, and every
    // wrapped line centered on its own when they do not.
    function test_errorActionsLinesAreCentered_data() {
        return [
            {
                tag: "wide",
                width: 1440,
                height: 900,
                oneLine: true
            },
            {
                tag: "minimum",
                width: 720,
                height: 520,
                oneLine: false
            }
        ];
    }
    function test_errorActionsLinesAreCentered(data) {
        shell.width = data.width;
        shell.height = data.height;
        tryCompare(shell.contentItem, "width", data.width);
        tryCompare(shell.contentItem, "height", data.height);
        driver.enqueueLocalSelection(["C:/Música/Arquivo movido.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalSourceUnavailable();
        tryCompare(presenter, "playbackState", PlayerPresenter.Error);
        tryVerify(() => presenter.canRetryCurrentOccurrence && presenter.canLocateCurrentOccurrence);
        verify(waitForPolish(shell));
        const state = findChild(shell, "error-state");
        const lines = {};
        for (const name of ["error-open-audio", "error-retry-occurrence", "error-locate-occurrence", "track-details-error"]) {
            const action = findChild(state, name);
            verify(action && action.visible, name);
            const p = action.mapToItem(state, 0, 0);
            const key = Math.round(p.y);
            if (!lines[key])
                lines[key] = {
                    left: p.x,
                    right: p.x + action.width
                };
            lines[key].left = Math.min(lines[key].left, p.x);
            lines[key].right = Math.max(lines[key].right, p.x + action.width);
        }
        const keys = Object.keys(lines);
        if (data.oneLine)
            compare(keys.length, 1, "the hero has room for every action in one line");
        for (const key of keys) {
            const line = lines[key];
            const middle = (line.left + line.right) / 2;
            verify(Math.abs(middle - state.width / 2) <= 1, "line at " + key + " is centered: " + middle + " vs " + state.width / 2);
        }
    }
    function test_compactHeroAtMinimumWindow() {
        mountShellBackends();
        shell.lyrics = fakeLyrics;
        loadQueue();
        shell.width = 720;
        shell.height = 520;
        tryCompare(shell.contentItem, "width", 720);
        tryCompare(shell.contentItem, "height", 520);
        verify(waitForPolish(shell));
        compare(findChild(shell, "cover-track").width, 96);
        compare(findChild(shell, "current-title").font.pixelSize, 24);
        const details = findChild(shell, "track-details-playing");
        compare(details.text, "Detalhes");
        compare(details.Accessible.name, "Detalhes da faixa");
        // The verse line no longer makes the hero compact: only the window width does.
        shell.width = 960;
        shell.height = 640;
        tryCompare(shell.contentItem, "width", 960);
        tryCompare(shell.contentItem, "height", 640);
        verify(waitForPolish(shell));
        tryVerify(() => findChild(shell, "lyrics-line").visible);
        compare(findChild(shell, "cover-track").width, 168);
        compare(findChild(shell, "current-title").font.pixelSize, 36);
    }
    function test_heroElidesLongMetadataAtMinimumWindow() {
        const longTitle = "T".repeat(200);
        const longArtist = "A".repeat(200);
        shell.width = 720;
        shell.height = 520;
        tryCompare(shell.contentItem, "height", 520);
        loadQueue();
        driver.publishNamedLocalMetadata(longTitle, longArtist, "");
        const title = findChild(shell, "current-title");
        tryCompare(title, "text", longTitle);
        verify(waitForPolish(shell));
        compare(title.elide, Text.ElideRight);
        compare(title.Accessible.name, longTitle);
        const hero = findChild(shell, "home-hero");
        for (const name of ["track-details-playing", "home-lyrics"]) {
            const button = findChild(shell, name);
            compare(button.height, 40);
            const inHero = button.mapToItem(hero, 0, 0);
            verify(inHero.x >= 0 && inHero.y >= 0 && inHero.x + button.width <= hero.width && inHero.y + button.height <= hero.height, name + " stays inside the hero");
            const inWindow = point(button);
            verify(inWindow.x + button.width <= shell.width && inWindow.y + button.height <= shell.height, name + " stays inside the window");
        }
    }
    function test_queueCardTabRevealsTheFocusedOccurrence() {
        driver.enqueueLocalSelection(["C:/Fixture/1.mp3", "C:/Fixture/2.mp3", "C:/Fixture/3.mp3", "C:/Fixture/4.mp3", "C:/Fixture/5.mp3", "C:/Fixture/6.mp3", "C:/Fixture/7.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalPlaying();
        const list = findChild(shell, "home-queue-list");
        tryCompare(list, "count", 7);
        list.forceLayout();
        tryVerify(() => list.itemAtIndex(6) !== null);
        const audio = commands();
        list.itemAtIndex(0).forceActiveFocus(Qt.TabFocusReason);
        for (let index = 1; index < 7; ++index) {
            keyClick(Qt.Key_Tab);
            const card = list.itemAtIndex(index);
            tryCompare(card, "activeFocus", true);
            const p = card.mapToItem(list, 0, 0);
            verify(p.x >= -1 && p.x + card.width <= list.width + 1, "Tab must scroll the horizontal shelf to occurrence " + index);
        }
        compare(commands(), audio);
    }
    function test_virtualizedShelfUsesModelOrderForTabAndBacktab() {
        const sources = [];
        for (let index = 0; index < 24; ++index)
            sources.push("C:/Fixture/Virtual " + index + ".mp3");
        driver.enqueueLocalSelection(sources);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalPlaying();
        shell.width = 720;
        shell.height = 520;
        tryCompare(shell.contentItem, "width", 720);
        verify(waitForPolish(shell));
        const list = findChild(shell, "home-queue-list");
        tryCompare(list, "count", 24);
        list.cacheBuffer = 0;
        const ids = [];
        for (let index = 0; index < list.count; ++index) {
            list.positionViewAtIndex(index, ListView.Contain);
            list.forceLayout();
            tryVerify(() => list.itemAtIndex(index) !== null);
            ids.push(list.itemAtIndex(index).occurrenceId);
        }
        list.positionViewAtIndex(0, ListView.Beginning);
        list.forceLayout();
        tryVerify(() => list.itemAtIndex(0) !== null);
        list.itemAtIndex(0).forceActiveFocus(Qt.TabFocusReason);
        const audio = commands();
        function verifyFocused(index) {
            tryCompare(list, "currentIndex", index);
            tryVerify(() => list.currentItem !== null && list.currentItem.activeFocus);
            compare(list.currentItem.occurrenceId, ids[index]);
            const p = list.currentItem.mapToItem(list, 0, 0);
            verify(p.x >= -1 && p.x + list.currentItem.width <= list.width + 1);
        }
        for (let index = 0; index < ids.length; ++index) {
            verifyFocused(index);
            keyClick(Qt.Key_Tab);
        }
        verify(!shell.isInside(shell.activeFocusItem, list), "Tab must exit at the model's last occurrence");
        keyClick(Qt.Key_Backtab);
        for (let index = ids.length - 1; index >= 0; --index) {
            verifyFocused(index);
            keyClick(Qt.Key_Backtab);
        }
        tryCompare(findChild(shell, "home-see-queue"), "activeFocus", true);
        list.positionViewAtIndex(10, ListView.Contain);
        list.forceLayout();
        tryVerify(() => list.itemAtIndex(10) !== null);
        list.itemAtIndex(10).forceActiveFocus(Qt.TabFocusReason);
        const focusedId = ids[10];
        const movedId = ids[19];
        verify(presenter.moveQueueOccurrence(movedId, 5));
        ids.splice(19, 1);
        ids.splice(5, 0, movedId);
        tryVerify(() => list.currentItem !== null && list.currentItem.occurrenceId === focusedId);
        keyClick(Qt.Key_Tab);
        verifyFocused(ids.indexOf(focusedId) + 1);
        const nextId = list.currentItem.occurrenceId;
        verify(presenter.removeQueueOccurrence(ids[2]));
        ids.splice(2, 1);
        tryVerify(() => list.currentItem !== null && list.currentItem.occurrenceId === nextId);
        keyClick(Qt.Key_Backtab);
        verifyFocused(ids.indexOf(focusedId));
        compare(commands(), audio, "Focus, reorder and removal of other occurrences must not play audio");
    }
    function test_verseLineSitsAboveThePlayer_data() {
        return [
            {
                tag: "wide",
                width: 1440,
                height: 900,
                lineHeight: 48
            },
            {
                tag: "compact",
                width: 720,
                height: 520,
                lineHeight: 40
            }
        ];
    }
    function test_verseLineSitsAboveThePlayer(data) {
        loadQueue();
        mountShellBackends();
        shell.lyrics = fakeLyrics;
        shell.width = data.width;
        shell.height = data.height;
        tryCompare(shell.contentItem, "width", data.width);
        tryCompare(shell.contentItem, "height", data.height);
        verify(waitForPolish(shell));
        const line = findChild(shell, "lyrics-line");
        const footer = findChild(shell, "player-footer");
        tryVerify(() => line.visible);
        compare(line.height, data.lineHeight);
        compare(Math.round(point(footer).y - (point(line).y + line.height)), 12);
        // The page and the line do not overlap.
        const layer = findChild(shell, "state-layer");
        verify(point(layer).y + layer.height <= point(line).y + 1);
    }
    // Synchronized lyrics without a current track: the panel is hidden, so no verse line is there to supply the
    // 12 px below the page, and the page keeps its own 30 px margin above the player.
    function test_pageKeepsItsBottomMarginWithoutAShownVerseLine() {
        mountShellBackends();
        shell.lyrics = fakeLyrics;
        verify(waitForPolish(shell));
        const panel = findChild(shell, "lyrics-panel");
        verify(panel.lineVisible, "the lyrics say a line would be shown");
        verify(!panel.visible, "but there is no current track, so the panel is hidden");
        const layer = findChild(shell, "state-layer");
        const footer = findChild(shell, "player-footer");
        compare(Math.round(footer.y - (point(layer).y + layer.height)), 30);
        // With a track the line is shown and the page gives its margin to the line.
        loadQueue();
        tryVerify(() => panel.visible);
        verify(waitForPolish(shell));
        const line = findChild(shell, "lyrics-line");
        tryVerify(() => line.visible);
        compare(Math.round(footer.y - (point(line).y + line.height)), 12);
    }
    // Plain-text lyrics show no verse line: the panel is visible with no height, and the layout spacing must not
    // add a gap, so the page ends exactly 30 px above the player.
    function test_hiddenVerseLineAddsNoGap() {
        mountShellBackends();
        captureLyrics.hasSyncedLyrics = false;
        captureLyrics.plainText = "Letra em texto simples";
        shell.lyrics = captureLyrics;
        loadQueue();
        const panel = findChild(shell, "lyrics-panel");
        tryVerify(() => panel.visible);
        verify(waitForPolish(shell));
        compare(panel.implicitHeight, 0);
        const layer = findChild(shell, "state-layer");
        const footer = findChild(shell, "player-footer");
        compare(Math.round(footer.y - (point(layer).y + layer.height)), 30);
    }
    // Captures of the plan 3 scenes: the verse line, both drawers, the empty queue and the clear confirmation.
    // They are evidence for visual review and are saved only when RC_MUSIC_UI_ARTIFACT_DIR is set.
    readonly property var captureVerses: ["A maré trouxe o vidro de volta", "E a areia guardou o teu nome", "Eu contei as ondas até a hora", "Em que o céu se acende e some", "Maré de vidro, leva devagar", "O que eu não soube te dizer", "Se a luz voltar pelo mar", "Eu ainda vou estar aqui pra ver", "A cidade dorme na janela", "E o rádio insiste na canção"]
    // Every track gets its duration (2:41 to 4:12) by being played once, so the queue knows its total. With
    // missingFirst the first file is then gone: the player moves on to the second and the first row shows the status.
    function loadEightTracks(missingFirst) {
        const names = ["Maré de Vidro", "Luz de Inverno", "Cais", "Papel de Seda", "Arquivo movido", "Estação Norte", "Janela", "Rádio Distante"];
        const durations = [217000, 192000, 161000, 205000, 252000, 233000, 178000, 199000];
        driver.enqueueLocalSelection(names.map(name => "C:/Música/" + name + ".mp3"));
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        const list = findChild(shell, "home-queue-list");
        tryCompare(list, "count", names.length);
        const ids = [];
        for (let index = 0; index < names.length; ++index) {
            list.positionViewAtIndex(index, ListView.Contain);
            list.forceLayout();
            tryVerify(() => list.itemAtIndex(index) !== null);
            ids.push(list.itemAtIndex(index).occurrenceId);
        }
        function playOnce(index, position) {
            if (index > 0 || presenter.currentOccurrenceId !== ids[0])
                verify(presenter.playQueueOccurrence(ids[index]));
            tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
            driver.publishLocalPlaying();
            driver.publishLocalPosition(position, durations[index]);
            tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
            // The queue row learns its length from the track's metadata.
            driver.publishNamedLocalMetadata(names[index], "", "", durations[index]);
            tryVerify(() => driver.localMetadataPollsPending() === 0);
        }
        for (let index = 0; index < names.length; ++index)
            playOnce(index, 0);
        // Back to the scene's track: the first one, or the second after the first fails to load.
        verify(presenter.playQueueOccurrence(ids[0]));
        tryCompare(presenter, "currentOccurrenceId", ids[0]);
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        if (missingFirst) {
            driver.publishLocalSourceUnavailable();
            tryVerify(() => presenter.currentOccurrenceId !== ids[0]);
            tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
            driver.publishLocalPlaying();
            driver.publishLocalPosition(41000, durations[1]);
        } else {
            driver.publishLocalPlaying();
            driver.publishLocalPosition(41000, durations[0]);
        }
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
    }
    // The loop above leaves the shelf scrolled to its end. The captures show it from the first card.
    function showQueueShelfStart() {
        const list = findChild(shell, "home-queue-list");
        list.positionViewAtIndex(0, ListView.Beginning);
        list.forceLayout();
        tryVerify(() => list.itemAtIndex(0) !== null);
    }
    function saveCapture(scene, data) {
        shell.update();
        verify(waitForRendering(shell.contentItem));
        const image = grabImage(shell.contentItem);
        compare(image.width, data.width);
        compare(image.height, data.height);
        const artifact = driver.artifactPath("aurora-gavetas-" + scene + "-" + data.width + "-" + (data.dark ? "dark" : "light") + ".png");
        if (artifact.length > 0)
            image.save(artifact);
    }
    function test_drawerCaptures_data() {
        const rows = [];
        for (const scene of ["verse-line", "lyrics-drawer", "lyrics-empty", "queue", "queue-empty", "queue-clear"]) {
            for (const size of [[1440, 900], [720, 520]]) {
                for (const dark of [false, true]) {
                    rows.push({
                        tag: scene + "-" + size[0] + "-" + (dark ? "dark" : "light"),
                        scene: scene,
                        width: size[0],
                        height: size[1],
                        dark: dark
                    });
                }
            }
        }
        return rows;
    }
    function test_drawerCaptures(data) {
        const lyricsScene = data.scene.startsWith("lyrics") || data.scene === "verse-line";
        if (lyricsScene) {
            const empty = data.scene === "lyrics-empty";
            captureLyrics.hasSyncedLyrics = !empty;
            captureLyrics.readerVerses = empty ? [] : testCase.captureVerses.map((text, index) => ({
                        text: text,
                        timeMs: index * 20000
                    }));
            captureLyrics.activeReaderVerse = empty ? -1 : 4;
            captureLyrics.title = "Maré de Vidro";
            captureLyrics.statusText = empty ? "" : "Letra sincronizada";
        }
        mountShellBackends();
        if (lyricsScene)
            shell.lyrics = captureLyrics;
        shell.width = data.width;
        shell.height = data.height;
        presenter.setThemePreference(data.dark ? PlayerPresenter.DarkTheme : PlayerPresenter.LightTheme);
        tryCompare(contract, "darkTheme", data.dark);
        tryCompare(shell.contentItem, "width", data.width);
        tryCompare(shell.contentItem, "height", data.height);
        loadEightTracks(data.scene === "queue");
        // The empty lyrics drawer names the track the player has, not a fixed title.
        if (data.scene === "lyrics-empty")
            captureLyrics.title = presenter.currentTitle;
        showQueueShelfStart();
        verify(waitForPolish(shell));
        const audio = commands();
        if (data.scene === "verse-line") {
            tryVerify(() => findChild(shell, "lyrics-line").visible);
            // Opening the audio left the focus on the page's lower action, and the home keeps a focused action
            // visible by scrolling (the shorter page above the verse line makes that a few pixels). The capture
            // shows the page from its top, with the focus on the header.
            findChild(shell, "theme-toggle").forceActiveFocus(Qt.TabFocusReason);
            const layer = findChild(shell, "state-layer");
            layer.contentY = 0;
            tryCompare(layer, "contentY", 0);
        } else if (lyricsScene) {
            const button = findChild(shell, "player-lyrics");
            tryVerify(() => button.enabled);
            mouseClick(button);
            tryCompare(findChild(shell, "lyrics-reader"), "opened", true);
            tryVerify(() => findChild(shell, "lyrics-drawer").visible);
            if (data.scene === "lyrics-empty")
                tryVerify(() => findChild(shell, "lyrics-drawer-empty").visible);
        } else {
            shell.setQueueOpen(true);
            const drawer = findChild(shell, "queue-panel");
            tryVerify(() => drawer.visible);
            const list = findChild(drawer, "queue-list");
            tryVerify(() => list.itemAtIndex(0) !== null);
            if (data.scene === "queue") {
                tryVerify(() => findChild(list.itemAtIndex(0), "queue-row-status").visible);
            } else if (data.scene === "queue-empty") {
                verify(presenter.clearQueue());
                tryVerify(() => findChild(drawer, "queue-empty").visible);
            } else if (data.scene === "queue-clear") {
                const clear = findChild(drawer, "queue-clear");
                tryVerify(() => clear.visible && clear.enabled);
                clear.Accessible.pressAction();
                tryVerify(() => findChild(shell, "queue-clear-confirmation").visible);
            }
        }
        wait(300);
        verify(waitForPolish(shell));
        saveCapture(data.scene, data);
        compare(commands().slice(2), audio.slice(2), "the captures never start or stop the audio");
    }
    // Captures of the home (slice 4): empty, playing, playing with the verse line, and the missing-file error.
    // They are evidence for visual review and are saved only when RC_MUSIC_UI_ARTIFACT_DIR is set.
    function test_homeCaptures_data() {
        const rows = [];
        for (const scene of ["home-empty", "home-playing", "home-playing-lyrics", "home-error"]) {
            for (const size of [[1440, 900], [720, 520]]) {
                for (const dark of [false, true]) {
                    rows.push({
                        tag: scene + "-" + size[0] + "-" + (dark ? "dark" : "light"),
                        scene: scene,
                        width: size[0],
                        height: size[1],
                        dark: dark
                    });
                }
            }
        }
        return rows;
    }
    function test_homeCaptures(data) {
        const recent = [["Maré de Vidro", "Lia Campos"], ["Luz de Inverno", "Coral do Sul"], ["Cais", "Rui Amaral"], ["Papel de Seda", "Lia Campos"], ["Estação Norte", "Orquestra de Câmara"], ["Janela", "Dani Ferraz"], ["Rádio Distante", "Coral do Sul"], ["Areia", "Mar Aberto"], ["Farol", "Rui Amaral"], ["Fio de Prata", "Dani Ferraz"], ["Ventania", "Mar Aberto"], ["Última Estação", "Lia Campos"]];
        recent.forEach((row, index) => fakeRecent.append({
                recordId: String(300 - index),
                title: row[0],
                artist: row[1],
                available: true
            }));
        mountShellBackends();
        shell.width = data.width;
        shell.height = data.height;
        presenter.setThemePreference(data.dark ? PlayerPresenter.DarkTheme : PlayerPresenter.LightTheme);
        tryCompare(contract, "darkTheme", data.dark);
        tryCompare(shell.contentItem, "width", data.width);
        tryCompare(shell.contentItem, "height", data.height);
        driver.setLocalQueueArtworkBase("image://queueart/");
        if (data.scene === "home-playing" || data.scene === "home-playing-lyrics") {
            loadEightTracks(true);
            showQueueShelfStart();
            if (data.scene === "home-playing-lyrics") {
                shell.lyrics = captureLyrics;
                captureLyrics.title = presenter.currentTitle;
                tryVerify(() => findChild(shell, "home-lyrics").visible);
            }
        } else if (data.scene === "home-error") {
            driver.enqueueLocalSelection(["C:/Música/Arquivo movido.mp3"]);
            findChild(shell, "open-audio").Accessible.pressAction();
            tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
            driver.publishLocalSourceUnavailable();
            tryCompare(presenter, "playbackState", PlayerPresenter.Error);
            tryVerify(() => presenter.canLocateCurrentOccurrence);
        } else {
            tryVerify(() => findChild(shell, "home-empty").visible);
            tryVerify(() => findChild(shell, "home-library-shelf").visible);
        }
        verify(waitForPolish(shell));
        const audio = commands();
        findChild(shell, "theme-toggle").forceActiveFocus(Qt.TabFocusReason);
        const layer = findChild(shell, "state-layer");
        layer.contentY = 0;
        tryCompare(layer, "contentY", 0);
        wait(300);
        verify(waitForPolish(shell));
        shell.update();
        verify(waitForRendering(shell.contentItem));
        const image = grabImage(shell.contentItem);
        compare(image.width, data.width);
        compare(image.height, data.height);
        const artifact = driver.artifactPath("aurora-inicio-" + data.scene + "-" + data.width + "-" + (data.dark ? "dark" : "light") + ".png");
        if (artifact.length > 0)
            image.save(artifact);
        compare(commands(), audio, "the captures never start or stop the audio");
    }
    // Clearing the queue with the shelf scrolled back to its first card made the ListView warn "DelegateModel::cancel:
    // index out range" (a fatal warning in the suite) about half of the time: the cards beyond the view were still
    // being built asynchronously, by the cache buffer, when the rows went away. The shelves build only what shows.
    function test_shelvesBuildOnlyTheVisibleCards() {
        mountShellBackends();
        for (const name of ["home-queue-list", "home-library-list"])
            compare(findChild(shell, name).cacheBuffer, 0, name);
    }
    function test_clearingAScrolledQueueShelfDoesNotWarn() {
        mountShellBackends();
        shell.width = 720;
        shell.height = 520;
        tryCompare(shell.contentItem, "width", 720);
        for (let round = 0; round < 4; ++round) {
            loadEightTracks(false);
            showQueueShelfStart();
            verify(waitForPolish(shell));
            shell.setQueueOpen(true);
            const drawer = findChild(shell, "queue-panel");
            tryVerify(() => drawer.visible);
            tryVerify(() => findChild(drawer, "queue-list").itemAtIndex(0) !== null);
            verify(presenter.clearQueue());
            tryVerify(() => findChild(drawer, "queue-empty").visible);
            wait(300);
            shell.setQueueOpen(false);
            tryVerify(() => !drawer.visible);
        }
    }
    function test_emptyHomeDoesNotInventACollection() {
        const list = findChild(shell, "home-queue-list");
        verify(list);
        compare(list.count, 0);
        verify(findChild(shell, "home-empty").visible);
        verify(!findChild(shell, "home-queue-shelf").visible);
        assertControlInside(findChild(shell, "open-audio"));
        compare(findChild(shell, "library-results"), null);
        compare(commands(), [0, 0, 0, 0, 0]);
    }
}
