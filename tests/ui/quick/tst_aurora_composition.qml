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
        id: fakeLibrary
        property bool ready: true
        property bool busy: false
        property int count: 128
        property int visibleCount: 0
        property bool hasPendingChange: false
        property bool canRetrySave: false
        property string statusText: ""
        property string filterText: ""
        property var model: fakeRecords
        property bool canLocate: false
        property string artworkBase: ""
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
        compare(findChild(second, "queue-card-artwork").source.toString(), "", "Do not attribute the playing track's cover to another occurrence");
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
        const action = findChild(shell, "open-another-audio");
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
    function test_emptyHomeDoesNotInventACollection() {
        const list = findChild(shell, "home-queue-list");
        verify(list);
        compare(list.count, 0);
        verify(findChild(shell, "emptyState").visible);
        verify(!findChild(shell, "home-queue-shelf").visible);
        assertControlInside(findChild(shell, "open-audio"));
        compare(findChild(shell, "library-results"), null);
        compare(commands(), [0, 0, 0, 0, 0]);
    }
}
