import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "AuroraNavigation"
    when: windowShown
    readonly property var driver: testScenarioDriver // qmllint disable unqualified
    readonly property PlayerPresenter presenter: testLocalPlayerPresenter // qmllint disable unqualified
    readonly property ShellUiContract productContract: testProductUiContract // qmllint disable unqualified
    property var shell

    Component {
        id: shellComponent
        Main {
            uiContract: testCase.productContract
            presenter: testCase.presenter
        }
    }

    ListModel {
        id: fakeRecords
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

    function mountNavigationBackends() {
        fakeLibrary.ready = true;
        fakeLibrary.count = 128;
        fakePlaylists.ready = true;
        shell.library = fakeLibrary;
        shell.playlists = fakePlaylists;
        shell.onlineSearch = fakeSearch;
        shell.room = fakeRoom;
        shell.downloads = fakeDownloads;
        shell.embeddedBrowser = fakeBrowser;
        wait(0);
        verify(waitForPolish(shell));
    }

    function init() {
        driver.resetLocalScenario();
        driver.setHighContrast(false, false);
        driver.setSystemAppearance(false, false);
        presenter.setThemePreference(PlayerPresenter.SystemTheme);
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.requestActivate();
        tryVerify(() => testCase.shell.active);
    }

    function cleanup() {
        if (shell)
            shell.destroy();
        shell = null;
        driver.resetLocalScenario();
        driver.setHighContrast(false, false);
        driver.setSystemAppearance(false, false);
        presenter.setMotionPreference(PlayerPresenter.SystemMotion);
        presenter.setThemePreference(PlayerPresenter.SystemTheme);
    }

    function loadSyntheticQueue() {
        driver.enqueueLocalSelection(["C:/Música/Primeira.mp3", "C:/Música/Segunda.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalPlaying();
        driver.publishLocalPosition(41000, 180000);
        presenter.setVolume(0.52);
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
    }

    function audioCommands() {
        return [driver.localLoadCount(), driver.localUnloadCount(), driver.localPlayCount(), driver.localPauseCount(), driver.localSeekCount()];
    }

    function verifyInside(item) {
        verify(item && item.visible);
        const point = item.mapToItem(shell.contentItem, 0, 0);
        verify(item.width >= 40 && item.height >= 40);
        verify(point.x >= 0 && point.y >= 0);
        verify(point.x + item.width <= shell.width + 1);
        verify(point.y + item.height <= shell.height + 1);
    }

    function test_sidebarChangesTheActualQueuePageWithoutAudioCommands() {
        loadSyntheticQueue();
        verify(findChild(shell, "side-navigation"));
        const transport = findChild(shell, "transport-bar");
        const occurrence = presenter.currentOccurrenceId;
        const commands = audioCommands();
        const queue = findChild(shell, "queue-panel");
        findChild(shell, "player-queue").Accessible.pressAction();
        tryVerify(() => queue.visible);
        // The home stays under the queue drawer, inactive and out of the keyboard path.
        verify(findChild(shell, "state-layer").visible);
        verify(!findChild(shell, "state-layer").enabled);
        compare(findChild(shell, "queue-list").count, 2);
        findChild(shell, "now").Accessible.pressAction();
        tryVerify(() => !queue.visible);
        verify(findChild(shell, "state-layer").visible);
        compare(findChild(shell, "transport-bar"), transport);
        compare(presenter.currentOccurrenceId, occurrence);
        compare(presenter.positionMs, 41000);
        compare(presenter.volume, 0.52);
        compare(audioCommands(), commands);
    }

    function test_auroraSidebarHasNoQueueItem() {
        loadSyntheticQueue();
        const item = findChild(shell, "queue");
        verify(item);
        compare(item.visible, false);
        findChild(shell, "now").forceActiveFocus(Qt.TabFocusReason);
        const transport = findChild(shell, "transport-bar");
        let reached = false;
        for (let step = 0; step < 20 && !reached; ++step) {
            keyClick(Qt.Key_Tab);
            verify(shell.activeFocusItem !== item, "Tab never stops on the hidden sidebar queue item");
            reached = shell.activeFocusItem && (shell.activeFocusItem === transport || (transport.contains(transport.mapFromItem(shell.activeFocusItem, 0, 0)) && shell.activeFocusItem.objectName !== ""));
        }
        verify(reached, "Tab reaches the transport bar");
    }

    function test_ctrlLOpensTheQueueWithoutTheSidebarItem() {
        loadSyntheticQueue();
        const queue = findChild(shell, "queue-panel");
        const playPause = findChild(shell, "play-pause");
        playPause.forceActiveFocus(Qt.TabFocusReason);
        verify(playPause.activeFocus);
        keyClick(Qt.Key_L, Qt.ControlModifier);
        tryVerify(() => queue.visible);
        tryVerify(() => findChild(shell, "queue-list").activeFocus);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !queue.visible);
        tryVerify(() => playPause.activeFocus);
        const opener = findChild(shell, "player-queue");
        opener.forceActiveFocus(Qt.TabFocusReason);
        mouseClick(opener);
        tryVerify(() => queue.visible);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !queue.visible);
        tryVerify(() => opener.activeFocus);
    }

    function test_ctrlLDoesNothingWithoutAQueue() {
        // A local queue is "loaded" exactly when it has tracks, so there is no empty drawer to open.
        verify(!presenter.queueLoaded);
        const queue = findChild(shell, "queue-panel");
        findChild(shell, "now").forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_L, Qt.ControlModifier);
        wait(100);
        verify(!queue.visible);
        verify(!shell.queueOpen);
    }

    function test_queueOpenerIsForgottenWhenTheDrawerClosesAnotherWay() {
        loadSyntheticQueue();
        const queue = findChild(shell, "queue-panel");
        const opener = findChild(shell, "mute");
        opener.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_L, Qt.ControlModifier);
        tryVerify(() => queue.visible);
        // The sidebar closes the drawer without going through it.
        findChild(shell, "now").Accessible.pressAction();
        tryVerify(() => !queue.visible);
        tryVerify(() => findChild(shell, "now").activeFocus);
        // A later focus restore (after a source import) must not jump back to that old opener.
        shell.restoreFocusAfterSourceImport();
        tryVerify(() => findChild(shell, "player-queue").activeFocus);
        verify(!opener.activeFocus);
    }

    function test_queueFocusFallsBackToThePlayerQueueThenPlayPause() {
        loadSyntheticQueue();
        const queue = findChild(shell, "queue-panel");
        const playQueue = findChild(shell, "player-queue");
        const playPause = findChild(shell, "play-pause");
        const opener = findChild(shell, "preferences");
        opener.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_L, Qt.ControlModifier);
        tryVerify(() => queue.visible);
        // The opener becomes unavailable while the drawer is open: focus goes to the player's queue button.
        opener.enabled = false;
        keyClick(Qt.Key_Escape);
        tryVerify(() => !queue.visible);
        tryVerify(() => playQueue.activeFocus);
        opener.enabled = true;
        // Without the opener and the player's queue button, focus ends on play/pause.
        opener.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_L, Qt.ControlModifier);
        tryVerify(() => queue.visible);
        opener.enabled = false;
        playQueue.visible = false;
        keyClick(Qt.Key_Escape);
        tryVerify(() => !queue.visible);
        tryVerify(() => playPause.activeFocus);
    }

    function themeButton() {
        const header = findChild(shell, "workspace-header");
        verify(header);
        const button = findChild(header, "theme-toggle");
        verify(button, "the theme button lives in the header");
        compare(findChild(shell, "side-navigation").themeButton, null);
        return button;
    }

    function test_quickThemeUsesThePreferenceAndPreservesPlayback() {
        loadSyntheticQueue();
        const button = themeButton();
        compare(button.tonal, true);
        compare(button.iconName, "moon");
        compare(button.Accessible.name, "Ativar tema escuro");
        const commands = audioCommands();
        const occurrence = presenter.currentOccurrenceId;
        button.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        tryCompare(productContract, "darkTheme", true);
        compare(presenter.themePreference, PlayerPresenter.DarkTheme);
        compare(String(shell.color).toUpperCase(), "#1B1917");
        compare(button.iconName, "sun");
        compare(button.Accessible.name, "Ativar tema claro");
        // Space stays reserved for play/pause by the window shortcut, as for every shell button.
        button.Accessible.pressAction();
        tryCompare(productContract, "darkTheme", false);
        compare(presenter.themePreference, PlayerPresenter.LightTheme);
        compare(String(shell.color).toUpperCase(), "#F5F1E9");
        compare(presenter.currentOccurrenceId, occurrence);
        compare(presenter.positionMs, 41000);
        compare(presenter.volume, 0.52);
        compare(audioCommands(), commands);
    }

    function test_themeToggleKeepsFocusAndPlayback() {
        loadSyntheticQueue();
        const button = themeButton();
        const commands = audioCommands();
        const positionBefore = presenter.positionMs;
        button.forceActiveFocus(Qt.TabFocusReason);
        for (const dark of [true, false]) {
            keyClick(Qt.Key_Return);
            tryCompare(productContract, "darkTheme", dark);
            verify(button.activeFocus, "the button keeps the focus after the swap");
            compare(presenter.playbackState, PlayerPresenter.Playing);
            verify(presenter.positionMs >= positionBefore);
            compare(audioCommands(), commands);
        }
    }

    function test_nativeFrameUsesAuroraColorsAndPersistentTrackIdentity() {
        loadSyntheticQueue();
        shell.width = 1440;
        shell.height = 900;
        tryCompare(shell.contentItem, "width", 1440);
        tryCompare(shell.contentItem, "height", 900);
        // The captures show the whole sidebar: counts, the profile chip and the external window icon.
        fakeProfile.needsSetup = false;
        shell.profile = fakeProfile;
        mountNavigationBackends();
        verify(findChild(findChild(shell, "library"), "navigation-count").visible);
        verify(findChild(findChild(shell, "playlists"), "navigation-count").visible);
        verify(findChild(findChild(shell, "embedded-browser"), "navigation-trailing-icon").visible);
        verify(findChild(shell, "profile-chip").visible);
        compare(findChild(shell, "navigation-brand").font.family, "Georgia");

        compare(shell.font.family, "Segoe UI");
        compare(shell.font.pixelSize, 14);
        const importStatus = findChild(shell, "source-import-status");
        const footer = findChild(shell, "player-footer");
        tryCompare(importStatus, "y", footer.y - importStatus.height - productContract.spacingUnit * 2);
        compare(findChild(shell, "now").contentItem.font.family, "Segoe UI");
        const transportIcons = {
            "player-shuffle": "shuffle",
            "previous": "previous",
            "play-pause": "pause",
            "next": "next",
            "player-repeat": "repeat",
            "player-queue": "queue",
            // The mute button shows the sound state: audible here.
            "mute": "volume"
        };
        for (const name in transportIcons)
            verifyButtonIcon(findChild(shell, name), transportIcons[name]);
        compare(findChild(shell, "player-time").font.family, "Segoe UI");
        compare(findChild(shell, "player-track-artist").font.family, "Segoe UI");
        // Player C sets the persistent title in the serif heading face.
        compare(findChild(shell, "player-track-title").font.family, "Georgia");
        for (const dark of [true, false]) {
            presenter.setThemePreference(dark ? PlayerPresenter.DarkTheme : PlayerPresenter.LightTheme);
            tryCompare(productContract, "darkTheme", dark);
            verify(waitForRendering(shell.contentItem));
            const image = grabImage(shell.contentItem);
            verify(image.width > 0 && image.height > 0);
            compare(String(image.pixel(image.width - 4, 4)).toUpperCase(), dark ? "#1B1917" : "#F5F1E9");
            const path = driver.artifactPath(dark ? "aurora-native-dark.png" : "aurora-native-light.png");
            if (path.length > 0)
                image.save(path);
            const expectedNavigation = dark ? "#1E1C19" : "#EEE8DD";
            compare(String(findChild(shell, "side-navigation").color).toUpperCase(), expectedNavigation);
            compare(String(image.pixel(4, 4)).toUpperCase(), expectedNavigation);
        }
        const title = findChild(shell, "player-track-title");
        verify(title && title.visible);
        verify(title.text.indexOf(presenter.currentTitle) >= 0);
        findChild(shell, "player-queue").Accessible.pressAction();
        tryVerify(() => findChild(shell, "queue-panel").visible);
        verify(title.visible);
    }

    function luminance(color) {
        function linear(value) {
            return value <= 0.04045 ? value / 12.92 : Math.pow((value + 0.055) / 1.055, 2.4);
        }
        return 0.2126 * linear(color.r) + 0.7152 * linear(color.g) + 0.0722 * linear(color.b);
    }

    function composite(foreground, background, opacity) {
        const alpha = foreground.a * opacity;
        return Qt.rgba(foreground.r * alpha + background.r * (1 - alpha), foreground.g * alpha + background.g * (1 - alpha), foreground.b * alpha + background.b * (1 - alpha), 1);
    }

    function verifyContrast(foreground, background, minimum, context) {
        const first = luminance(foreground);
        const second = luminance(background);
        const ratio = (Math.max(first, second) + 0.05) / (Math.min(first, second) + 0.05);
        verify(ratio >= minimum, context + ": " + ratio + " < " + minimum);
    }

    function focusName() {
        const item = shell.activeFocusItem;
        return item ? (item.objectName !== "" ? String(item.objectName) : String(item) + "<" + String(item.parent) + ">") : "";
    }

    function tabTrail(key, modifier, limit) {
        const trail = [];
        for (let step = 0; step < limit; ++step) {
            keyClick(key, modifier);
            trail.push(focusName());
        }
        return trail;
    }

    function verifyOrderedSubsequence(trail, names) {
        let from = 0;
        for (const name of names) {
            const at = trail.indexOf(name, from);
            verify(at >= 0, name + " must follow in order; trail: " + trail.join(", "));
            from = at + 1;
        }
        return from;
    }

    function test_tabFollowsSidebarHeaderPageAndPlayer() {
        loadSyntheticQueue();
        shell.width = 1440;
        shell.height = 900;
        tryCompare(shell.contentItem, "width", 1440);
        tryCompare(shell.contentItem, "height", 900);
        fakeProfile.needsSetup = false;
        shell.profile = fakeProfile;
        mountNavigationBackends();
        verify(findChild(shell, "profile-chip").visible);
        const sidebar = ["library", "search", "playlists", "room", "downloads", "embedded-browser", "preferences", "profile-chip", "theme-toggle"];
        const player = ["progress", "player-shuffle", "previous", "play-pause", "next", "player-repeat", "player-queue", "mute", "volume"];
        findChild(shell, "now").forceActiveFocus(Qt.TabFocusReason);
        compare(focusName(), "now");
        const forward = tabTrail(Qt.Key_Tab, Qt.NoModifier, 59);
        // The sidebar, then preferences, the profile chip and the theme button come one right after the other.
        for (let i = 0; i < sidebar.length; ++i)
            compare(forward[i], sidebar[i], "step " + i + "; trail: " + forward.join(", "));
        // The page comes next, and only after it the player, in its own order.
        const afterHeader = forward.slice(sidebar.length);
        const progressAt = afterHeader.indexOf("progress");
        verify(progressAt > 0, "the page comes before the player; trail: " + forward.join(", "));
        const playerTrail = afterHeader.slice(progressAt);
        verifyOrderedSubsequence(playerTrail, player);
        // The player buttons are consecutive in this order, with the disabled lyrics button skipped.
        const first = playerTrail.indexOf("progress");
        for (let i = 0; i < player.length; ++i)
            compare(playerTrail[first + i], player[i], "player step " + i + "; trail: " + playerTrail.join(", "));
        verify(!playerTrail.includes("player-lyrics"), "lyrics is disabled without a lyric");

        // Shift+Tab walks the same path backwards, from the volume to the theme button.
        findChild(shell, "volume").forceActiveFocus(Qt.BacktabFocusReason);
        const backward = tabTrail(Qt.Key_Backtab, Qt.NoModifier, 40);
        const reversedPlayer = player.slice(0, -1).reverse();
        for (let i = 0; i < reversedPlayer.length; ++i)
            compare(backward[i], reversedPlayer[i], "back step " + i + "; trail: " + backward.join(", "));
        verifyOrderedSubsequence(backward, ["theme-toggle", "profile-chip", "preferences", "embedded-browser", "downloads", "room", "playlists", "search", "library", "now"]);
    }

    function homeFocusables(item, found) {
        for (const child of item.children) {
            if (!child.visible)
                continue;
            if (child.activeFocusOnTab && child.enabled)
                found.push(child);
            homeFocusables(child, found);
        }
        return found;
    }

    function test_homeScrollsTheFocusedItemIntoView() {
        loadSyntheticQueue();
        shell.width = productContract.minimumWidth;
        shell.height = productContract.minimumHeight;
        tryCompare(shell.contentItem, "height", productContract.minimumHeight);
        const home = findChild(shell, "state-layer");
        verify(home && home.visible);
        tryVerify(() => home.contentHeight > home.height + 1, 5000, "the minimum home has to scroll");
        const items = homeFocusables(home.contentItem, []);
        verify(items.length > 1);
        const visibleIn = item => {
            const y = item.mapToItem(home.contentItem, 0, 0).y;
            return y >= home.contentY - 0.5 && y + item.height <= home.contentY + home.height + 0.5;
        };
        const last = items[items.length - 1];
        last.forceActiveFocus(Qt.TabFocusReason);
        tryVerify(() => home.contentY > 0 && visibleIn(last), 5000, "the last home item scrolls into view");
        items[0].forceActiveFocus(Qt.BacktabFocusReason);
        tryVerify(() => visibleIn(items[0]), 5000, "the first home item scrolls back into view");
    }

    function isInside(item, ancestor) {
        for (let current = item; current; current = current.parent) {
            if (current === ancestor)
                return true;
        }
        return false;
    }

    function test_tabFollowsSidebarHeaderPageAndPlayerOnEveryPage_data() {
        return [
            {
                tag: "activity",
                button: "downloads",
                page: "downloads-panel"
            },
            {
                tag: "library",
                button: "library",
                page: "library-panel"
            },
            {
                tag: "playlists",
                button: "playlists",
                page: "playlists-panel"
            },
            {
                tag: "search",
                button: "search",
                page: "search-panel"
            },
            {
                tag: "room",
                button: "room",
                page: "room-panel",
                // An unavailable room has nothing to focus.
                pageless: true
            },
            {
                tag: "preferences",
                button: "preferences",
                page: "preferences-panel"
            }
        ];
    }

    function test_tabFollowsSidebarHeaderPageAndPlayerOnEveryPage(data) {
        loadSyntheticQueue();
        shell.width = 1440;
        shell.height = 900;
        tryCompare(shell.contentItem, "width", 1440);
        tryCompare(shell.contentItem, "height", 900);
        fakeProfile.needsSetup = false;
        shell.profile = fakeProfile;
        mountNavigationBackends();
        const page = findChild(shell, data.page);
        findChild(shell, data.button).Accessible.pressAction();
        tryVerify(() => page.visible);
        const head = ["now", "library", "search", "playlists", "room", "downloads", "embedded-browser", "preferences", "profile-chip", "theme-toggle"];
        const player = ["progress", "player-shuffle", "previous", "play-pause", "next", "player-repeat", "player-queue", "mute", "volume"];
        const now = findChild(shell, "now");
        now.forceActiveFocus(Qt.TabFocusReason);
        const cycle = [now];
        const names = ["now"];
        for (let step = 0; step < 80; ++step) {
            keyClick(Qt.Key_Tab);
            if (shell.activeFocusItem === now)
                break;
            cycle.push(shell.activeFocusItem);
            names.push(focusName());
        }
        // One cycle through every region, back to the first sidebar item.
        verify(shell.activeFocusItem === now, "Tab comes back to the sidebar; trail: " + names.join(", "));
        for (let i = 0; i < head.length; ++i)
            compare(names[i], head[i], "step " + i + "; trail: " + names.join(", "));
        const progressAt = names.indexOf("progress");
        verify(progressAt >= head.length + (data.pageless ? 0 : 1), "the page comes before the player; trail: " + names.join(", "));
        for (let i = head.length; i < progressAt; ++i)
            verify(isInside(cycle[i], page), names[i] + " belongs to the open page; trail: " + names.join(", "));
        compare(names.slice(progressAt), player, "the player closes the cycle; trail: " + names.join(", "));

        // Shift+Tab walks the very same cycle backwards.
        const backward = [];
        let mismatch = -1;
        for (let step = 0; step < cycle.length; ++step) {
            keyClick(Qt.Key_Backtab);
            backward.push(focusName());
            if (mismatch < 0 && shell.activeFocusItem !== cycle[cycle.length - 1 - step])
                mismatch = step;
        }
        verify(mismatch < 0, "back step " + mismatch + "; trail: " + backward.join(", "));
    }

    function test_activityRelaysFollowTheTabChainWithoutATarget() {
        loadSyntheticQueue();
        shell.width = 1440;
        shell.height = 900;
        tryCompare(shell.contentItem, "width", 1440);
        mountNavigationBackends();
        const page = findChild(shell, "downloads-panel");
        findChild(shell, "downloads").Accessible.pressAction();
        tryVerify(() => page.visible);
        // A missing target and a disabled one: the focus still leaves the page along the window's chain.
        page.nextFocusItem = null;
        page.previousFocusItem = findChild(shell, "skip-forward");
        verify(!page.previousFocusItem.visible);
        const list = findChild(shell, "downloads-list");
        list.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Tab);
        compare(focusName(), "progress");
        const first = findChild(shell, "downloads-close");
        first.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Backtab);
        compare(focusName(), "theme-toggle");
    }

    function test_sidebarFollowsTheApprovedOrder() {
        mountNavigationBackends();
        const viewport = findChild(shell, "navigation-viewport");
        let previous = -1;
        for (const name of ["now", "library", "search", "playlists", "room", "downloads", "embedded-browser", "preferences"]) {
            const item = findChild(shell, name);
            verify(item && item.visible, name);
            const y = item.mapToItem(viewport.contentItem, 0, 0).y;
            verify(y > previous, name + " y=" + y + " after " + previous);
            previous = y;
        }
    }

    function test_sidebarShowsCountsOnlyFromBindings() {
        mountNavigationBackends();
        const library = findChild(shell, "library");
        const playlists = findChild(shell, "playlists");
        const libraryCount = findChild(library, "navigation-count");
        const playlistCount = findChild(playlists, "navigation-count");
        verify(libraryCount.visible);
        compare(libraryCount.text, "128");
        compare(library.Accessible.description, "128 faixas");
        verify(playlistCount.visible);
        compare(playlistCount.text, "2");
        compare(playlists.Accessible.description, "2 playlists");
        verify(!findChild(findChild(shell, "downloads"), "navigation-count").visible);
        fakeLibrary.count = 1;
        compare(libraryCount.text, "1");
        compare(library.Accessible.description, "1 faixa");
        fakeLibrary.ready = false;
        fakePlaylists.ready = false;
        tryVerify(() => !libraryCount.visible && !playlistCount.visible);
        compare(library.Accessible.description, "");
        fakeLibrary.ready = true;
        fakeLibrary.count = 128;
        fakePlaylists.ready = true;
        tryVerify(() => libraryCount.visible && playlistCount.visible);
        shell.width = 720;
        shell.height = 520;
        tryCompare(shell.contentItem, "width", 720);
        verify(waitForPolish(shell));
        verify(!libraryCount.visible);
        verify(!playlistCount.visible);
        compare(library.Accessible.description, "128 faixas");
        compare(playlists.Accessible.description, "2 playlists");
    }

    function test_navegarMarksTheExternalWindow() {
        mountNavigationBackends();
        const browser = findChild(shell, "embedded-browser");
        compare(browser.trailingIconName, "external");
        const icon = findChild(browser, "navigation-trailing-icon");
        verify(icon && icon.visible);
        compare(icon.iconName, "external");
        compare(icon.Accessible.ignored, true);
        // The icon is decoration: a screen reader hears the same warning from the item itself.
        compare(browser.Accessible.description, "Abre em uma janela separada.");
        verify(!findChild(findChild(shell, "downloads"), "navigation-trailing-icon").visible);
        shell.width = 720;
        shell.height = 520;
        tryCompare(shell.contentItem, "width", 720);
        verify(waitForPolish(shell));
        verify(!icon.visible);
        compare(browser.Accessible.description, "Abre em uma janela separada.");
    }

    function test_productContrastUsesActualBackgrounds_data() {
        return [
            {
                tag: "light",
                dark: false
            },
            {
                tag: "dark",
                dark: true
            }
        ];
    }

    function test_productContrastUsesActualBackgrounds(data) {
        loadSyntheticQueue();
        presenter.setMotionPreference(PlayerPresenter.ReducedMotion);
        presenter.setThemePreference(data.dark ? PlayerPresenter.DarkTheme : PlayerPresenter.LightTheme);
        tryCompare(productContract, "darkTheme", data.dark);
        verify(waitForPolish(shell));
        const navigation = findChild(shell, "side-navigation");
        const footer = findChild(shell, "player-footer");
        verifyContrast(findChild(shell, "navigation-caption").color, navigation.color, 4.5, "navigation caption");
        verifyContrast(findChild(shell, "navigation-brand").color, navigation.color, 4.5, "navigation brand");
        verifyContrast(findChild(shell, "player-track-title").color, footer.color, 4.5, "persistent track title");
        verifyContrast(findChild(shell, "player-time").color, footer.color, 4.5, "elapsed and total time");
        verifyContrast(findChild(shell, "player-track-artist").color, footer.color, 4.5, "artist and album");
        for (const name of ["now", "preferences"]) {
            const button = findChild(shell, name);
            verifyContrast(button.contentItem.color, button.background.color, 4.5, name + " text normal/selected");
            mouseMove(button);
            tryVerify(() => button.hovered);
            const hover = findChild(button.background, "hover-feedback");
            tryVerify(() => button.checked || (hover && hover.opacity > 0.001) || Qt.colorEqual(button.background.color, productContract.hoverColor), 5000, name + " shows hover feedback");
            const rendered = hover && hover.opacity > 0 ? composite(hover.color, button.background.color, hover.opacity) : button.background.color;
            verifyContrast(button.contentItem.color, rendered, 4.5, name + " text including hover");
            button.forceActiveFocus(Qt.TabFocusReason);
            verify(button.activeFocus);
            verifyContrast(button.background.border.color, button.background.color, 3, name + " inner focus/selection");
            verifyContrast(button.background.border.color, navigation.color, 3, name + " outer focus/selection");
        }
        const theme = themeButton();
        const canvas = findChild(shell, "workspace-header").color;
        verifyContrast(theme.foregroundColor, theme.fillColor, 3, "theme icon on its tonal fill");
        mouseMove(theme);
        tryVerify(() => theme.hovered);
        verifyContrast(theme.foregroundColor, theme.fillColor, 3, "theme icon on its tonal fill with hover");
        theme.forceActiveFocus(Qt.TabFocusReason);
        verify(theme.activeFocus);
        verifyContrast(findChild(theme, "focus-ring").border.color, canvas, 3, "theme focus ring on the canvas");
        for (const name of ["previous", "play-pause", "next", "mute"])
            verifyContrast(findChild(shell, name).contentItem.color, findChild(shell, name).background.color, 4.5, name + " rendered foreground");
        mountNavigationBackends();
        for (const entry of [["library", "navigation-count", 4.5], ["playlists", "navigation-count", 4.5], ["embedded-browser", "navigation-trailing-icon", 3]]) {
            const button = findChild(shell, entry[0]);
            const mark = findChild(button, entry[1]);
            verify(mark.visible, entry[0] + " " + entry[1]);
            verifyContrast(mark.color, button.background.color, entry[2], entry[0] + " " + entry[1] + " normal");
            mouseMove(button);
            tryVerify(() => button.hovered);
            tryVerify(() => Qt.colorEqual(button.background.color, productContract.hoverColor));
            tryVerify(() => Qt.colorEqual(mark.color, productContract.textPrimaryColor));
            verifyContrast(mark.color, button.background.color, entry[2], entry[0] + " " + entry[1] + " hover");
            mouseMove(shell.contentItem, 5, 5);
            tryVerify(() => !button.hovered);
        }
        const libraryButton = findChild(shell, "library");
        libraryButton.Accessible.pressAction();
        tryVerify(() => libraryButton.checked);
        const selectedCount = findChild(libraryButton, "navigation-count");
        tryVerify(() => Qt.colorEqual(libraryButton.background.color, productContract.selectionColor));
        verifyContrast(selectedCount.color, libraryButton.background.color, 4.5, "library count selected");
        findChild(shell, "now").Accessible.pressAction();
        tryVerify(() => !libraryButton.checked);
        for (const name of ["progress", "volume"]) {
            const slider = findChild(shell, name);
            verify(slider.enabled);
            if (slider.edge) {
                // Player C: the edge track is a divider line; the filled part and the marker carry the position.
                verify(Qt.colorEqual(slider.background.color, productContract.dividerColor), name + " edge track");
                verifyContrast(slider.handle.color, footer.color, 3, name + " marker on actual footer");
                verify(Qt.colorEqual(findChild(slider, "slider-marker-ring").color, footer.color), name + " marker ring");
            } else {
                verifyContrast(slider.background.color, footer.color, 3, name + " track on actual footer");
                verifyContrast(slider.handle.border.color, footer.color, 3, name + " handle outline");
                verifyContrast(slider.handle.border.color, slider.handle.color, 3, name + " handle interior");
            }
            verifyContrast(slider.fillColor, footer.color, 3, name + " fill");
            slider.forceActiveFocus(Qt.TabFocusReason);
            verify(slider.visualFocus);
            const focus = findChild(slider, "slider-focus-indicator");
            verify(focus.visible);
            verifyContrast(focus.border.color, footer.color, 3, name + " focus on actual footer");
        }
    }

    // Aurora replaces Unicode glyphs/emoji with tinted line icons; the button text stays
    // the accessible name and the icon follows the rendered foreground color.
    function verifyButtonIcon(button, iconName) {
        verify(button, iconName);
        const icon = findChild(button, "button-icon");
        verify(icon, button.objectName + " has an icon");
        compare(icon.iconName, iconName, button.objectName);
        verify(icon.visible, button.objectName);
        tryCompare(icon, "status", Image.Ready);
        verify(Qt.colorEqual(icon.color, button.contentItem.color), button.objectName + " icon " + icon.color + " vs text " + button.contentItem.color);
        compare(icon.Accessible.ignored, true);
        verify(button.accessibleName.length > 0, button.objectName);
    }

    function test_navigationUsesLineIconsAndTheAuroraBrandMark_data() {
        return [
            {
                tag: "light",
                dark: false
            },
            {
                tag: "dark",
                dark: true
            }
        ];
    }

    function test_navigationUsesLineIconsAndTheAuroraBrandMark(data) {
        loadSyntheticQueue();
        presenter.setThemePreference(data.dark ? PlayerPresenter.DarkTheme : PlayerPresenter.LightTheme);
        tryCompare(productContract, "darkTheme", data.dark);
        for (const entry of [["now", "home"], ["preferences", "settings"]])
            verifyButtonIcon(findChild(shell, entry[0]), entry[1]);
        const selectedIcon = findChild(findChild(shell, "now"), "button-icon");
        verify(Qt.colorEqual(selectedIcon.color, productContract.accentColor), "selected icon uses the accent");
        // Theme toggle offers the opposite theme: sun to go light, moon to go dark.
        compare(themeButton().iconName, data.dark ? "sun" : "moon");
        const mark = findChild(shell, "navigation-brand-mark");
        verify(mark && mark.visible);
        compare(mark.children.length, 3);
        for (let index = 0; index < 3; ++index)
            verify(Qt.colorEqual(mark.children[index].color, productContract.accentColor));
        verify(mark.children[1].height > mark.children[2].height && mark.children[2].height > mark.children[0].height, "bars follow the approved 12/24/18 rhythm");
        compare(mark.Accessible.ignored, true);
        verify(findChild(shell, "navigation-brand").visible);
    }

    function test_compactNavigationKeepsIconsAndTheMarkOnly() {
        loadSyntheticQueue();
        shell.width = 720;
        shell.height = 520;
        tryCompare(shell.contentItem, "width", 720);
        verify(waitForPolish(shell));
        verifyButtonIcon(findChild(shell, "now"), "home");
        compare(findChild(shell, "now").contentItem.text, "");
        verify(findChild(shell, "navigation-brand-mark").visible);
        verify(!findChild(shell, "navigation-brand").visible, "the 68 px rail shows the mark instead of a clipped name");
    }

    function test_selectedNavigationUsesAuroraSelection_data() {
        return [
            {
                tag: "light",
                dark: false
            },
            {
                tag: "dark",
                dark: true
            }
        ];
    }

    function test_selectedNavigationUsesAuroraSelection(data) {
        // The queue entry is disabled (and correctly shows no hover) until the queue has tracks.
        loadSyntheticQueue();
        presenter.setMotionPreference(PlayerPresenter.ReducedMotion);
        presenter.setThemePreference(data.dark ? PlayerPresenter.DarkTheme : PlayerPresenter.LightTheme);
        tryCompare(productContract, "darkTheme", data.dark);
        verify(waitForPolish(shell));
        const navigation = findChild(shell, "side-navigation");
        const selected = findChild(shell, "now");
        const other = findChild(shell, "preferences");
        verify(selected.checked);
        verify(!other.checked);
        // The selected page must be recognizable without relying on the 3 px bar alone.
        verify(Qt.colorEqual(selected.background.color, productContract.selectionColor), String(selected.background.color));
        verify(!Qt.colorEqual(selected.background.color, navigation.color));
        verify(Qt.colorEqual(selected.contentItem.color, productContract.accentColor), String(selected.contentItem.color));
        verify(selected.contentItem.font.weight > other.contentItem.font.weight);
        verifyContrast(selected.contentItem.color, selected.background.color, 4.5, "selected navigation text");
        verify(Qt.colorEqual(other.background.color, navigation.color), String(other.background.color));
        mouseMove(other);
        tryVerify(() => other.hovered);
        tryVerify(() => Qt.colorEqual(other.background.color, productContract.hoverColor));
        verifyContrast(other.contentItem.color, other.background.color, 4.5, "hovered navigation text");
    }

    function test_navigationHighContrastOverridesTheApprovedTheme() {
        loadSyntheticQueue();
        for (const light of [false, true]) {
            driver.setHighContrast(true, light);
            tryCompare(productContract, "highContrast", true);
            presenter.setThemePreference(light ? PlayerPresenter.DarkTheme : PlayerPresenter.LightTheme);
            const expected = light ? "#FCFCFD" : "#010102";
            compare(String(findChild(shell, "side-navigation").color).toUpperCase(), expected);
            compare(String(shell.color).toUpperCase(), expected);
            verify(waitForRendering(shell.contentItem));
            const image = grabImage(shell.contentItem);
            compare(String(image.pixel(4, 4)).toUpperCase(), expected);
        }
    }

    function test_navigationAndPlayerFitWithoutOverlap_data() {
        return [
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
            }
        ];
    }

    function test_navigationAndPlayerFitWithoutOverlap(data) {
        loadSyntheticQueue();
        shell.width = data.width;
        shell.height = data.height;
        presenter.setThemePreference(data.dark ? PlayerPresenter.DarkTheme : PlayerPresenter.LightTheme);
        tryCompare(productContract, "darkTheme", data.dark);
        tryCompare(shell.contentItem, "width", data.width);
        tryCompare(shell.contentItem, "height", data.height);
        const footer = findChild(shell, "player-footer");
        verify(footer);
        tryVerify(() => Math.abs(footer.y + footer.height - shell.height) < 0.5);
        for (const name of ["now", "player-queue", "preferences", "play-pause", "mute"])
            verifyInside(findChild(shell, name));
        // The compact window drops the volume bar and keeps the mute button.
        if (data.width > 900)
            verifyInside(findChild(shell, "volume"));
        else
            verify(!findChild(shell, "volume").visible, "the compact player hides the volume bar");
        // The progress is the 24 px exception: the player's full-width top edge.
        const progress = findChild(shell, "progress");
        verify(progress && progress.visible);
        compare(progress.height, 24);
        compare(progress.width, shell.width);
        const edge = progress.mapToItem(shell.contentItem, 0, 0);
        compare(edge.x, 0);
        compare(edge.y, footer.mapToItem(shell.contentItem, 0, 0).y);
        verifyInside(themeButton());
        findChild(shell, "player-queue").Accessible.pressAction();
        const queue = findChild(shell, "queue-panel");
        tryVerify(() => queue.visible);
        const q = queue.mapToItem(shell.contentItem, 0, 0);
        const p = footer.mapToItem(shell.contentItem, 0, 0);
        verify(q.y + queue.height <= p.y + 1);
        compare(p.y + footer.height, shell.height);
        verify(queue.overlayMode, "Aurora shows the queue as a side drawer");
    }
}
