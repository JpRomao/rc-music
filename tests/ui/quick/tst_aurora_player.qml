import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "AuroraPlayer"
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

    // A guest in a listening room, without network: only what the shell reads while in a room.
    QtObject {
        id: fakeRoom
        property bool available: true
        property int role: 0
        property string state: "idle"
        property bool busy: false
        property string inviteLink: ""
        property ListModel members: ListModel {}
        property ListModel pendingRequests: ListModel {}
        property ListModel queue: ListModel {}
        property ListModel roles: ListModel {}
        property ListModel knownPeople: ListModel {}
        property int initialRoleId: 1
        readonly property int permissionCount: 7
        property int permissions: 0
        property bool canSkip: false
        property bool canPause: false
        property bool canEditQueue: false
        property ListModel catalog: ListModel {}
        property bool canAddFromRoom: false
        property bool canAddPlaylist: false
        property bool canUploadOwnTracks: false
        property bool uploading: false
        property bool roomPlaying: false
        property bool roomHasCurrent: false
        property bool rosterUnsaved: false
        readonly property int memberCount: members.count
        property string hostNickname: ""
        property int maxListeners: 5
        property bool untilRevoked: false
        property string errorText: ""
        property bool privacyNoticeRequired: false
        readonly property int minimumListeners: 1
        readonly property int maximumListeners: 10
        readonly property int defaultListeners: 5
    }

    Component {
        id: roomShellComponent
        Main {
            uiContract: testCase.productContract
            presenter: testCase.presenter
            room: fakeRoom
        }
    }

    TransportText {
        id: transportText
    }

    NavigationText {
        id: navigationText
    }

    QueueText {
        id: queueText
    }

    function init() {
        driver.resetLocalScenario();
        driver.setHighContrast(false, false);
        driver.setSystemAppearance(false, false);
        presenter.setThemePreference(PlayerPresenter.SystemTheme);
        presenter.setMotionPreference(PlayerPresenter.ReducedMotion);
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.requestActivate();
        tryVerify(() => testCase.shell.active);
    }

    function cleanup() {
        if (shell)
            shell.destroy();
        shell = null;
        fakeRoom.role = 0;
        fakeRoom.state = "idle";
        fakeRoom.hostNickname = "";
        fakeRoom.members.clear();
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

    function resize(width, height) {
        shell.width = width;
        shell.height = height;
        tryCompare(shell.contentItem, "width", width);
        tryCompare(shell.contentItem, "height", height);
        verify(waitForPolish(shell));
    }

    function rect(item) {
        const point = item.mapToItem(shell.contentItem, 0, 0);
        return {
            x: point.x,
            y: point.y,
            width: item.width,
            height: item.height
        };
    }

    function overlaps(first, second) {
        return first.x < second.x + second.width - 0.5 && second.x < first.x + first.width - 0.5 && first.y < second.y + second.height - 0.5 && second.y < first.y + first.height - 0.5;
    }

    function luminance(color) {
        function linear(value) {
            return value <= 0.04045 ? value / 12.92 : Math.pow((value + 0.055) / 1.055, 2.4);
        }
        return 0.2126 * linear(color.r) + 0.7152 * linear(color.g) + 0.0722 * linear(color.b);
    }

    function verifyContrast(foreground, background, minimum, context) {
        const first = luminance(foreground);
        const second = luminance(background);
        const ratio = (Math.max(first, second) + 0.05) / (Math.min(first, second) + 0.05);
        verify(ratio >= minimum, context + ": " + ratio + " < " + minimum);
    }

    function windowSizes() {
        return [
            {
                tag: "wide",
                width: 1440,
                height: 900,
                artwork: 64
            },
            {
                tag: "minimum",
                width: 720,
                height: 520,
                artwork: 48
            }
        ];
    }

    function test_progressRunsAlongTheTopEdge_data() {
        return windowSizes();
    }

    // Player C: the progress bar is the player's top edge and replaces the divider line.
    function test_progressRunsAlongTheTopEdge(data) {
        loadSyntheticQueue();
        resize(data.width, data.height);
        const footer = findChild(shell, "player-footer");
        const progress = findChild(shell, "progress");
        verify(progress.edge, "the progress runs along the player's edge");
        compare(progress.width, footer.width);
        const bar = rect(progress);
        const edge = rect(footer);
        compare(bar.x, edge.x);
        verify(Math.abs(bar.y - edge.y) <= 1, "progress top " + bar.y + " vs footer top " + edge.y);
        compare(progress.height, 24);
        compare(progress.background.height, 4);
        compare(progress.background.y, 0);
        verify(Qt.colorEqual(progress.background.color, productContract.dividerColor));
        compare(progress.handle.width, 14);
        compare(progress.handle.height, 14);
        verify(Qt.colorEqual(progress.handle.color, productContract.accentColor));
        verifyContrast(progress.handle.color, footer.color, 3, "progress marker on the player");
        compare(progress.handle.y + progress.handle.height / 2, progress.background.y + progress.background.height / 2);
        for (const child of footer.children)
            verify(!(child.visible && child.height <= 1), "the player no longer draws a divider line");
    }

    function test_centerControlsFollowPlayerC_data() {
        return windowSizes();
    }

    function test_centerControlsFollowPlayerC(data) {
        loadSyntheticQueue();
        resize(data.width, data.height);
        const play = findChild(shell, "play-pause");
        compare(play.width, 52);
        compare(play.height, 52);
        compare(play.background.radius, 26);
        compare(findChild(play, "button-icon").width, 22);
        for (const name of ["player-shuffle", "player-repeat"]) {
            const mode = findChild(shell, name);
            compare(mode.width, 40, name);
            compare(mode.height, 40, name);
            compare(mode.background.width, 36, name + " draws a smaller background");
            compare(mode.background.height, 36, name);
            compare(findChild(mode, "button-icon").width, 18, name);
        }
        for (const name of ["previous", "next"]) {
            const step = findChild(shell, name);
            compare(step.width, 40, name);
            compare(step.height, 40, name);
            compare(findChild(step, "button-icon").width, 22, name);
        }
        // The ±5 s leave the player: ←/→ and the progress bar keep that function.
        verify(!findChild(shell, "skip-backward").visible);
        verify(!findChild(shell, "skip-forward").visible);
        play.forceActiveFocus(Qt.TabFocusReason);
        const seeks = driver.localSeekCount();
        const start = presenter.positionMs;
        keyClick(Qt.Key_Right);
        compare(driver.localSeekCount(), seeks + 1);
        compare(driver.localSeekTarget(), start + presenter.skipForwardSeconds * 1000);
        driver.publishLocalPosition(driver.localSeekTarget());
        tryCompare(presenter, "seekPending", false);
        compare(presenter.positionMs, start + presenter.skipForwardSeconds * 1000);
        keyClick(Qt.Key_Left);
        compare(driver.localSeekCount(), seeks + 2);
        compare(driver.localSeekTarget(), start + (presenter.skipForwardSeconds - presenter.skipBackwardSeconds) * 1000);
        driver.publishLocalPosition(driver.localSeekTarget());
        tryCompare(presenter, "seekPending", false);
        const progress = findChild(shell, "progress");
        // A quarter in, clear of the focused play button's tooltip.
        mouseClick(progress, progress.width / 4, progress.height / 2);
        tryVerify(() => driver.localSeekCount() === seeks + 3);
        verify(Math.abs(driver.localSeekTarget() - presenter.durationMs / 4) <= 1000, "a click on the bar seeks to " + driver.localSeekTarget());
    }

    function test_identityShowsCoverTitleArtistAlbumAndTime_data() {
        return windowSizes();
    }

    function test_identityShowsCoverTitleArtistAlbumAndTime(data) {
        loadSyntheticQueue();
        resize(data.width, data.height);
        const transport = findChild(shell, "transport-bar");
        const identity = findChild(shell, "player-identity");
        verify(identity && identity.visible);
        let ancestor = identity.parent;
        while (ancestor && ancestor !== transport)
            ancestor = ancestor.parent;
        verify(ancestor === transport, "the identity lives in the player");
        const artwork = findChild(shell, "player-artwork");
        compare(artwork.width, data.artwork);
        compare(artwork.height, data.artwork);
        const title = findChild(shell, "player-track-title");
        compare(title.font.family, productContract.headingFontFamily);
        compare(title.font.pixelSize, productContract.typeTitle);
        const artist = findChild(shell, "player-track-artist");
        const time = findChild(shell, "player-time");
        // An untagged file has no artist yet.
        compare(artist.text, navigationText.localAudio);
        compare(artist.text, "Artista desconhecido");
        compare(time.text, "0:41 / 3:00");
        compare(time.text, transportText.elapsedOfDuration.arg(transport.formattedCompactTime(presenter.positionMs)).arg(transport.formattedCompactTime(presenter.durationMs)));
        driver.publishCompleteLocalMetadata();
        tryCompare(title, "text", "Título controlado");
        compare(artist.text, transportText.artistAlbum.arg(presenter.currentArtist).arg(presenter.currentAlbum));
        compare(artist.text, "Artista · Álbum");
        // Without an album the line keeps only the artist.
        verify(driver.followRoom("Primeira", "Só a artista", "", 600000));
        tryCompare(title, "text", "Primeira");
        compare(artist.text, "Só a artista");
        compare(transport.formattedCompactTime(59000), "0:59");
        compare(transport.formattedCompactTime(600000), "10:00");
        compare(transport.formattedCompactTime(3723000), "1:02:03");
    }

    function test_progressEdgeStaysDistinguishableInHighContrast_data() {
        return [
            {
                tag: "light",
                light: true
            },
            {
                tag: "dark",
                light: false
            }
        ];
    }

    // System high contrast: the filled part, the empty track and the marker stay apart.
    function test_progressEdgeStaysDistinguishableInHighContrast(data) {
        loadSyntheticQueue();
        driver.setHighContrast(true, data.light);
        tryVerify(() => productContract.highContrast);
        const footer = findChild(shell, "player-footer");
        const progress = findChild(shell, "progress");
        verify(progress.edge && progress.enabled);
        const track = progress.background;
        const fill = track.children[0];
        verifyContrast(fill.color, track.color, 3, "filled part against the empty track");
        verifyContrast(progress.handle.color, footer.color, 3, "marker on the player");
        // The empty part shows either by its own color or by its outline.
        const trackEdge = track.border.width > 0 ? track.border.color : track.color;
        verifyContrast(trackEdge, footer.color, 3, "empty track on the player");
        verifyContrast(fill.color, footer.color, 3, "filled part on the player");
    }

    // A "%1" or "%2" inside a name is text, never a placeholder.
    function test_artistLineKeepsPlaceholdersInsideNames() {
        verify(driver.followRoom("Faixa", "Banda %2", "%1 ao vivo", 600000));
        const artist = findChild(shell, "player-track-artist");
        tryCompare(artist, "text", "Banda %2 · %1 ao vivo");
    }

    // Without a track the player shows only the empty title: no "Artista desconhecido", no "0:00 / 0:00".
    function test_identityWithoutATrackShowsOnlyTheEmptyTitle() {
        compare(presenter.hasCurrentTrack, false);
        compare(findChild(shell, "player-track-title").text, productContract.emptyTitle);
        const artist = findChild(shell, "player-track-artist");
        const time = findChild(shell, "player-time");
        verify(!artist.visible, "no artist line without a track");
        verify(!time.visible, "no time without a track");
        loadSyntheticQueue();
        verify(artist.visible);
        verify(time.visible);
        compare(time.text, "0:41 / 3:00");
    }

    // Without a duration there is no position to mark: no marker at the left edge, no "0:00 / 0:00".
    function test_progressMarkerAndTimeWaitForTheDuration() {
        const progress = findChild(shell, "progress");
        verify(!presenter.hasDuration);
        verify(!progress.handle.visible, "no marker without a track");
        driver.enqueueLocalSelection(["C:/Música/Primeira.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        const time = findChild(shell, "player-time");
        verify(presenter.hasCurrentTrack);
        verify(!presenter.hasDuration);
        verify(time.visible);
        verify(!progress.handle.visible, "no marker while the duration is unknown");
        compare(time.text, "0:00");
        driver.publishLocalPlaying();
        driver.publishLocalPosition(41000, 180000);
        tryVerify(() => presenter.hasDuration);
        verify(progress.handle.visible);
        compare(time.text, "0:41 / 3:00");
    }

    function test_identityElidesLongMetadataAtMinimumWindow() {
        resize(720, 520);
        let longTitle = "";
        let longArtist = "";
        while (longTitle.length < 200) {
            longTitle += "Um título que não acaba nunca ";
            longArtist += "Uma artista de nome comprido ";
        }
        longTitle = longTitle.slice(0, 200);
        longArtist = longArtist.slice(0, 200);
        verify(driver.followRoom(longTitle, longArtist, "", 600000));
        const title = findChild(shell, "player-track-title");
        tryCompare(title, "text", longTitle);
        verify(waitForPolish(shell));
        verify(title.truncated);
        verify(findChild(shell, "player-track-artist").truncated);
        // Eliding is visual only: a screen reader still hears the whole title and artist.
        compare(title.Accessible.name, longTitle);
        compare(findChild(shell, "player-track-artist").Accessible.name, longArtist);
        const identity = rect(findChild(shell, "player-identity"));
        const shuffle = rect(findChild(shell, "player-shuffle"));
        verify(!overlaps(identity, shuffle), "identity ends at " + (identity.x + identity.width) + ", shuffle starts at " + shuffle.x);
    }

    function test_secondaryTransportControlsAreFlatAndPlayIsRound_data() {
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

    function test_secondaryTransportControlsAreFlatAndPlayIsRound(data) {
        loadSyntheticQueue();
        presenter.setThemePreference(data.dark ? PlayerPresenter.DarkTheme : PlayerPresenter.LightTheme);
        tryCompare(productContract, "darkTheme", data.dark);
        findChild(shell, "progress").forceActiveFocus(Qt.TabFocusReason);
        for (const name of ["player-shuffle", "previous", "next", "player-repeat", "mute", "player-lyrics", "player-queue"]) {
            const button = findChild(shell, name);
            verify(button && button.visible, name);
            verify(button.flat, name + " is a quiet secondary control");
            compare(button.background.border.width, 0, name);
            verify(Qt.colorEqual(button.background.color, productContract.surfaceColor), name + " rests on the footer color");
        }
        const play = findChild(shell, "play-pause");
        verify(!play.flat);
        compare(play.width, play.height);
        compare(play.background.radius, play.height / 2);
        verify(Qt.colorEqual(play.background.color, productContract.accentColor));
        const next = findChild(shell, "next");
        mouseMove(next);
        tryVerify(() => next.hovered);
        tryVerify(() => Qt.colorEqual(next.background.color, productContract.hoverColor));
        const shuffle = findChild(shell, "player-shuffle");
        const before = presenter.shuffleQueueEnabled;
        if (!before)
            presenter.toggleShuffleQueue();
        tryVerify(() => shuffle.checked);
        verify(Qt.colorEqual(shuffle.background.color, productContract.selectionColor));
        verify(Qt.colorEqual(findChild(shuffle, "button-icon").color, productContract.accentColor), "an active mode uses the accent");
        if (!before)
            presenter.toggleShuffleQueue();
    }

    function test_queueButtonShowsTheCountAndOpensTheQueue() {
        loadSyntheticQueue();
        const button = findChild(shell, "player-queue");
        verify(button && button.enabled);
        compare(button.iconName, "queue");
        compare(findChild(button, "player-queue-count").text, "2");
        verify(button.accessibleName.indexOf("2") >= 0, button.accessibleName);
        const commands = audioCommands();
        mouseClick(button);
        tryVerify(() => findChild(shell, "queue-panel").visible);
        compare(audioCommands(), commands, "opening the queue must not touch playback");
    }

    // The badge counts something only with a queue, and dims with its button.
    function test_queueBadgeFollowsTheCountAndTheButtonState() {
        const button = findChild(shell, "player-queue");
        const badge = findChild(button, "player-queue-count");
        const text = findChild(button, "player-queue-count-text");
        verify(!presenter.queueLoaded);
        verify(!badge.visible, "no badge without a queue");
        loadSyntheticQueue();
        verify(badge.visible);
        compare(badge.text, "2");
        driver.setHighContrast(true, true);
        tryVerify(() => productContract.highContrast);
        verify(Qt.colorEqual(text.color, productContract.textPrimaryColor));
        verify(Qt.colorEqual(badge.border.color, productContract.textPrimaryColor));
        findChild(shell, "transport-bar").queueAvailable = false;
        verify(!button.enabled);
        verify(Qt.colorEqual(text.color, productContract.disabledTextColor), "count " + text.color);
        verify(Qt.colorEqual(badge.border.color, productContract.disabledTextColor), "outline " + badge.border.color);
    }

    function test_queueOpensAsADrawerOverTheHome_data() {
        return [
            {
                tag: "wide",
                width: 1440,
                height: 900
            },
            {
                tag: "minimum",
                width: 720,
                height: 520
            }
        ];
    }

    function test_queueOpensAsADrawerOverTheHome(data) {
        loadSyntheticQueue();
        resize(data.width, data.height);
        const commands = audioCommands();
        mouseClick(findChild(shell, "player-queue"));
        const drawer = findChild(shell, "queue-panel");
        tryVerify(() => drawer.visible);
        verify(waitForPolish(shell));
        const area = rect(drawer);
        const footer = findChild(shell, "player-footer");
        compare(shell.drawerWidth, 400);
        compare(drawer.width, shell.drawerWidth, "the queue is a 400 px side drawer, not a full page");
        compare(area.x + area.width, shell.width);
        compare(area.y, 0);
        compare(area.y + area.height, footer.y);
        // The page underneath stays in place, dimmed and out of the keyboard path.
        const home = findChild(shell, "state-layer");
        verify(home.visible);
        verify(!home.enabled);
        const scrim = findChild(shell, "queue-scrim");
        verify(scrim && scrim.visible);
        verify(Qt.colorEqual(scrim.color, productContract.scrimColor));
        mouseClick(scrim, 20, 20);
        tryVerify(() => !drawer.visible);
        verify(home.enabled);
        compare(audioCommands(), commands, "opening and closing the drawer must not touch playback");
    }

    function test_queueDrawerUsesTheAuroraHeaderIconsAndRows() {
        loadSyntheticQueue();
        resize(1440, 900);
        mouseClick(findChild(shell, "player-queue"));
        const drawer = findChild(shell, "queue-panel");
        tryVerify(() => drawer.visible);
        verify(drawer.overlayMode);
        const surface = findChild(drawer, "queue-panel-background");
        compare(surface.radius, 0);
        compare(surface.border.width, 0);
        // The drawer header replaces the old eyebrow and title, which stay for the fixture only.
        for (const name of ["queue-eyebrow", "queue-title"]) {
            const old = findChild(drawer, name);
            verify(!old || !old.visible, name + " is not shown in the Aurora drawer");
        }
        const header = findChild(drawer, "queue-header");
        verify(header && header.visible);
        const title = findChild(header, "drawer-title");
        compare(title.font.family, productContract.headingFontFamily);
        compare(title.font.pixelSize, productContract.typeTitle);
        const icons = {
            "queue-add": "plus",
            "queue-move-up": "arrow-up",
            "queue-move-down": "arrow-down",
            "queue-remove": "minus",
            "queue-clear": "trash"
        };
        for (const name in icons) {
            const button = findChild(drawer, name);
            verify(button, name);
            const icon = findChild(button, "button-icon");
            verify(icon && icon.visible, name + " shows a line icon");
            compare(icon.iconName, icons[name]);
            tryCompare(icon, "status", Image.Ready);
            // Only Limpar fila keeps a visible label; the others are icon only, without the old glyphs.
            const label = findChild(button, "button-label");
            if (name === "queue-clear")
                compare(label.text, "Limpar fila…");
            else
                verify(!label, name + " is icon only");
        }
        for (const name of ["queue-shuffle", "queue-repeat"]) {
            const mode = findChild(drawer, name);
            verify(!mode || !mode.visible, name + " lives in the player, not in the Aurora drawer");
        }
        const list = findChild(drawer, "queue-list");
        tryVerify(() => list.itemAtIndex(0) !== null);
        const first = list.itemAtIndex(0);
        // The playing track swaps its number for the waveform icon; the rows have no divider.
        verify(!findChild(first, "queue-row-number").visible);
        verify(findChild(first, "queue-row-now-icon").visible);
        verify(Qt.colorEqual(findChild(first, "queue-row-title").color, productContract.accentColor), "the playing track is highlighted");
        verify(!findChild(first, "queue-row-divider"), "the rows have no divider");
        const second = list.itemAtIndex(1);
        compare(findChild(second, "queue-row-number").text, "2");
        verify(findChild(second, "queue-row-number").visible);
    }

    function openDrawerWithThreeTracks() {
        driver.enqueueLocalSelection(["C:/Música/Primeira.mp3", "C:/Música/Segunda.mp3", "C:/Música/Terceira.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        verify(presenter.nextTrack());
        tryCompare(presenter, "currentQueueIndex", 1);
        shell.setQueueOpen(true);
        const drawer = findChild(shell, "queue-panel");
        tryVerify(() => drawer.visible);
        const list = findChild(drawer, "queue-list");
        tryCompare(list, "count", 3);
        tryVerify(() => list.itemAtIndex(2) !== null);
        return drawer;
    }

    function test_queueRowsMatchTheDrawerDesign() {
        const drawer = openDrawerWithThreeTracks();
        const list = findChild(drawer, "queue-list");
        for (let index = 0; index < 3; ++index) {
            const row = list.itemAtIndex(index);
            const current = index === 1;
            compare(row.height, 56);
            const artwork = findChild(row, "queue-row-artwork");
            verify(artwork && artwork.visible);
            compare(artwork.width, 40);
            compare(artwork.height, 40);
            const icon = findChild(row, "queue-row-now-icon");
            const number = findChild(row, "queue-row-number");
            verify(!findChild(row, "queue-row-divider"), "the rows have no divider");
            compare(!!icon && icon.visible, current);
            compare(number.visible, !current);
            const title = findChild(row, "queue-row-title");
            if (current) {
                compare(icon.iconName, "waveform");
                verify(Qt.colorEqual(icon.color, productContract.accentColor));
                verify(Qt.colorEqual(title.color, productContract.accentColor));
            } else {
                verify(!Qt.colorEqual(title.color, productContract.accentColor));
            }
            const expected = drawer.rowAccessibleName(row.title, row.fileName, row.artist, row.durationMs, row.available, row.errorCode, index + 1, 3, false, false, false);
            compare(row.Accessible.name, expected);
        }
    }

    function test_unavailableRowShowsStatusInWarning() {
        driver.enqueueLocalSelection(["C:/Música/Arquivo movido.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalSourceUnavailable();
        tryCompare(presenter, "playbackState", PlayerPresenter.Error);
        shell.setQueueOpen(true);
        const drawer = findChild(shell, "queue-panel");
        tryVerify(() => drawer.visible);
        const list = findChild(drawer, "queue-list");
        tryVerify(() => list.itemAtIndex(0) !== null);
        const row = list.itemAtIndex(0);
        const status = findChild(row, "queue-row-status");
        verify(status.visible);
        compare(status.text, "Arquivo não disponível.");
        verify(Qt.colorEqual(status.color, productContract.warningColor));
        const artist = findChild(row, "queue-row-artist");
        verify(artist && !artist.visible, "the status takes the place of the artist");
    }

    // The drawer is never opened empty, but it stays open when its queue empties underneath it.
    function test_emptyQueueShowsTheEmptyState() {
        const drawer = openDrawerWithThreeTracks();
        verify(presenter.clearQueue());
        tryVerify(() => !testCase.presenter.queueLoaded);
        verify(drawer.visible, "the open drawer stays open");
        const empty = findChild(drawer, "queue-empty");
        tryVerify(() => empty.visible);
        compare(findChild(empty, "empty-title").text, "A fila está vazia.");
        const primary = findChild(empty, "queue-empty-primary");
        verify(primary && primary.visible);
        compare(primary.text, "Abrir áudio");
        verify(!findChild(drawer, "queue-list").visible);
        // Vertically centered in the list area.
        const area = empty.parent;
        verify(Math.abs(empty.y + empty.height / 2 - area.height / 2) <= 1, "centered: " + empty.y + " " + empty.height + " " + area.height);
        driver.enqueueLocalSelection(["C:/Música/Nova.mp3"]);
        mouseClick(primary);
        tryVerify(() => testCase.presenter.queueLoaded);
        compare(presenter.queueModel.rowCount(), 1);
    }

    function test_emptyDrawerClosesAndStaysClosed() {
        const drawer = openDrawerWithThreeTracks();
        verify(presenter.clearQueue());
        tryVerify(() => !testCase.presenter.queueLoaded);
        tryVerify(() => findChild(drawer, "queue-empty").visible);
        tryVerify(() => testCase.shell.activeFocusItem !== null && testCase.shell.activeFocusItem.objectName === "queue-empty-primary", 5000, "the focus moves to the empty action");
        keyClick(Qt.Key_Escape);
        tryVerify(() => !drawer.visible);
        verify(!shell.queueOpen);
        wait(100);
        verify(!drawer.visible, "the empty drawer does not reopen");
        const focused = shell.activeFocusItem;
        verify(focused && focused.visible && focused.enabled, "the focus rests on a visible, enabled item");
    }

    // Opens a queue with one file per duration (null leaves the duration unknown).
    function loadQueueWithDurations(durations) {
        driver.enqueueLocalSelection(durations.map((duration, index) => "C:/Música/Faixa " + (index + 1) + ".mp3"));
        findChild(shell, "open-audio").Accessible.pressAction();
        tryVerify(() => testCase.presenter.queueLoaded);
        for (let index = 0; index < durations.length; ++index) {
            if (index > 0)
                verify(presenter.nextTrack());
            tryCompare(presenter, "currentQueueIndex", index);
            if (durations[index] === null)
                continue;
            driver.publishLocalPlaying();
            driver.publishLocalPosition(0, durations[index]);
            // The player's duration wins over the tags' one once it is confirmed.
            tryCompare(presenter, "durationMs", durations[index]);
            driver.publishCompleteLocalMetadata();
            tryCompare(presenter, "currentTitle", "Título controlado");
        }
    }

    function test_queueDrawerHeaderShowsCountAndDuration() {
        loadQueueWithDurations([600000, 600000, 600000]);
        compare(presenter.queueTotalDurationMs, 1800000);
        const opener = findChild(shell, "player-queue");
        opener.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        const drawer = findChild(shell, "queue-panel");
        tryVerify(() => drawer.visible);
        verify(waitForPolish(shell));
        compare(drawer.width, 400);
        const header = findChild(drawer, "queue-header");
        verify(header && header.visible);
        compare(findChild(header, "drawer-eyebrow").text, "Reprodução");
        compare(findChild(header, "drawer-title").text, "Fila");
        compare(findChild(header, "drawer-subtitle").text, "3 faixas · 30 min");
        compare(drawer.subtitleText, "3 faixas · 30 min");
        const close = findChild(drawer, "queue-header-close");
        verify(close && close.visible && close.enabled);
        compare(close.Accessible.name, "Fechar fila");
        mouseClick(close);
        tryVerify(() => !drawer.visible);
        tryVerify(() => opener.activeFocus, 5000, "closing gives the focus back to whoever opened the drawer");
    }

    function test_subtitleOmitsDurationWhenUnknown() {
        loadQueueWithDurations([null]);
        verify(presenter.queueTotalDurationMs === undefined || presenter.queueTotalDurationMs === null, String(presenter.queueTotalDurationMs));
        shell.setQueueOpen(true);
        const drawer = findChild(shell, "queue-panel");
        tryVerify(() => drawer.visible);
        compare(findChild(drawer, "drawer-subtitle").text, "1 faixa");
        compare(drawer.subtitleText, "1 faixa");
    }

    function test_durationRoundsUpAndShowsHours_data() {
        return [
            {
                tag: "an hour and a second",
                durations: [3000000, 601000],
                subtitle: "2 faixas · 1 h 1 min"
            },
            {
                tag: "a minute and a second",
                durations: [61000],
                subtitle: "1 faixa · 2 min"
            },
            {
                tag: "just under an hour rounds up to the hour",
                durations: [3599500],
                subtitle: "1 faixa · 1 h 0 min"
            }
        ];
    }

    function test_durationRoundsUpAndShowsHours(data) {
        loadQueueWithDurations(data.durations);
        shell.setQueueOpen(true);
        const drawer = findChild(shell, "queue-panel");
        tryVerify(() => drawer.visible);
        compare(drawer.subtitleText, data.subtitle);
        compare(findChild(drawer, "drawer-subtitle").text, data.subtitle);
    }

    function verifyToolbarButton(drawer, name, accessibleName, description) {
        const button = findChild(drawer, name);
        verify(button && button.visible, name);
        verify(findChild(button, "icon-button-background"), name + " is an AuroraIconButton");
        compare(button.width, 40, name);
        compare(button.height, 40, name);
        compare(button.Accessible.name, accessibleName, name);
        compare(button.Accessible.description, description, name);
        return button;
    }

    function test_queueToolbarUsesAuroraIconButtons() {
        loadSyntheticQueue();
        shell.setQueueOpen(true);
        const drawer = findChild(shell, "queue-panel");
        tryVerify(() => drawer.visible);
        verify(waitForPolish(shell));
        verifyToolbarButton(drawer, "queue-add", "Adicionar arquivos…", "");
        verifyToolbarButton(drawer, "queue-move-up", "Mover para cima", "Alt+↑");
        verifyToolbarButton(drawer, "queue-move-down", "Mover para baixo", "Alt+↓");
        verifyToolbarButton(drawer, "queue-remove", "Remover da fila", "Delete");
        const clear = findChild(drawer, "queue-clear");
        verify(clear && clear.visible && clear.enabled);
        compare(clear.kind, AuroraButton.Quiet);
        const label = findChild(clear, "button-label");
        compare(label.text, "Limpar fila…");
        compare(clear.Accessible.name, "Limpar a fila");
        compare(findChild(clear, "button-icon").iconName, "trash");
        verify(Qt.colorEqual(label.color, productContract.errorColor), "the clear ink is the error color: " + label.color);
        verify(Qt.colorEqual(findChild(clear, "button-icon").color, productContract.errorColor), "so is the trash icon");
        verifyContrast(label.color, Qt.color(productContract.surfaceColor), 4.5, "Limpar fila on the light drawer");
        mouseMove(clear, clear.width / 2, clear.height / 2);
        tryVerify(() => clear.hovered);
        verifyContrast(label.color, clear.background.color, 4.5, "hovered Limpar fila on the light drawer");
        presenter.setThemePreference(PlayerPresenter.DarkTheme);
        tryVerify(() => testCase.productContract.darkTheme);
        verifyContrast(label.color, clear.background.color, 4.5, "hovered Limpar fila on the dark drawer");
        mouseMove(drawer, 1, drawer.height - 1);
        tryVerify(() => !clear.hovered);
        verifyContrast(label.color, Qt.color(productContract.surfaceColor), 4.5, "Limpar fila on the dark drawer");
        // The clear button sits at the right end of the toolbar, after the icon buttons.
        const remove = findChild(drawer, "queue-remove");
        verify(clear.mapToItem(drawer, 0, 0).x > remove.mapToItem(drawer, 0, 0).x + remove.width);
    }

    // A guest without the host's permission keeps today's reasons on the drawer buttons.
    function test_queueToolbarKeepsTheRoomReasons() {
        shell.destroy();
        fakeRoom.role = 2;
        fakeRoom.state = "joined";
        shell = createTemporaryObject(roomShellComponent, null);
        verify(shell);
        shell.requestActivate();
        tryVerify(() => testCase.shell.active);
        shell.setQueueOpen(true);
        const drawer = findChild(shell, "queue-panel");
        tryVerify(() => drawer.visible);
        verify(drawer.roomGuest);
        // The header leaves out the local count: a guest sees the room queue.
        compare(drawer.subtitleText, "");
        verify(!findChild(drawer, "drawer-subtitle").visible);
        const add = verifyToolbarButton(drawer, "queue-add", "Adicionar arquivos…", queueText.guestLocalQueueHeld);
        verify(!add.enabled);
        for (const [name, accessibleName] of [["queue-move-up", "Mover para cima"], ["queue-move-down", "Mover para baixo"], ["queue-remove", "Remover da fila"]]) {
            const button = verifyToolbarButton(drawer, name, accessibleName, queueText.hostNotAllowed);
            verify(!button.enabled, name);
        }
        verify(!findChild(drawer, "queue-clear").enabled, "a guest does not clear the room queue");
    }

    function test_lyricsButtonIsDisabledWithAReasonWithoutLyrics() {
        loadSyntheticQueue();
        const button = findChild(shell, "player-lyrics");
        verify(button && button.visible);
        compare(button.iconName, "lyrics");
        verify(!button.enabled);
        verify(button.disabledReason.length > 0);
    }

    function test_volumeShowsItsPercentageWhenThereIsRoom() {
        loadSyntheticQueue();
        resize(1440, 900);
        const percent = findChild(shell, "player-volume-percent");
        verify(percent && percent.visible);
        compare(percent.text, "52%");
        presenter.setVolume(0.8);
        tryCompare(percent, "text", "80%");
    }

    // Player C: Lyrics and Queue are icons only, with their names in the tooltip and to assistive technology.
    function test_rightClusterIsIconOnly() {
        loadSyntheticQueue();
        resize(1440, 900);
        const lyrics = findChild(shell, "player-lyrics");
        verify(lyrics && lyrics.visible);
        compare(lyrics.contentItem.text, "");
        compare(lyrics.iconName, "lyrics");
        compare(lyrics.accessibleName, transportText.lyricsAction);
        compare(lyrics.accessibleName, "Letra");
        verify(!lyrics.enabled, "the synthetic queue has no lyrics");
        compare(lyrics.disabledReason, transportText.lyricsUnavailable);
        compare(lyrics.disabledReason, "Esta faixa ainda não tem letra.");
        compare(lyrics.toolTipText, "Letra — Esta faixa ainda não tem letra.");
        const queue = findChild(shell, "player-queue");
        compare(queue.contentItem.text, "");
        compare(queue.iconName, "queue");
        const count = findChild(queue, "player-queue-count");
        verify(count && count.visible);
        compare(count.text, "2");
        compare(count.height, 16);
        verify(count.width >= 16, "count is " + count.width + " px wide");
        compare(count.radius, 8);
        verify(Qt.colorEqual(count.color, productContract.selectionColor));
        const digits = findChild(count, "player-queue-count-text");
        compare(digits.text, "2");
        compare(digits.font.pixelSize, 10);
        compare(digits.font.weight, Font.DemiBold);
        verify(Qt.colorEqual(digits.color, productContract.textPrimaryColor));
        verifyContrast(digits.color, count.color, 4.5, "queue count on its badge");
        // On the icon's top-right corner: 4 px from the top, 2 px from the right.
        compare(count.y, 4);
        compare(count.x + count.width, queue.width - 2);
        // High contrast: an outline instead of the fill.
        driver.setHighContrast(true, true);
        tryVerify(() => productContract.highContrast);
        verify(Qt.colorEqual(count.color, productContract.canvasColor));
        compare(count.border.width, 2);
        verify(Qt.colorEqual(count.border.color, productContract.textPrimaryColor));
        verify(Qt.colorEqual(digits.color, productContract.textPrimaryColor));
        verifyContrast(digits.color, count.color, 4.5, "queue count in high contrast");
    }

    function test_volumeBarHidesOnCompactWindow() {
        loadSyntheticQueue();
        resize(1440, 900);
        const volume = findChild(shell, "volume");
        const percent = findChild(shell, "player-volume-percent");
        const mute = findChild(shell, "mute");
        verify(volume.visible);
        compare(volume.width, 90);
        compare(volume.handle.width, 16);
        verify(percent.visible);
        compare(percent.width, 34);
        compare(percent.horizontalAlignment, Text.AlignRight);
        compare(percent.text, "52%");
        resize(720, 520);
        verify(!volume.visible, "the volume bar leaves a compact window");
        verify(!percent.visible, "the percentage leaves a compact window");
        verify(mute.visible && mute.enabled, "mute stays");
        verify(mute.width >= productContract.controlTargetMinimum);
        const muted = presenter.muted;
        findChild(shell, "play-pause").forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_M);
        tryCompare(presenter, "muted", !muted);
        keyClick(Qt.Key_M);
        tryCompare(presenter, "muted", muted);
    }

    function test_playStaysCenteredBetweenEqualSides_data() {
        return [
            {
                tag: "wide",
                width: 1440,
                height: 900
            },
            {
                tag: "medium",
                width: 1024,
                height: 768
            }
        ];
    }

    // Both sides measure 260 px, so the play button sits on the player's center.
    function test_playStaysCenteredBetweenEqualSides(data) {
        loadSyntheticQueue();
        resize(data.width, data.height);
        compare(findChild(shell, "player-identity").width, 260);
        compare(findChild(shell, "player-side-actions").width, 260);
        const footer = rect(findChild(shell, "player-footer"));
        const play = rect(findChild(shell, "play-pause"));
        const offset = play.x + play.width / 2 - (footer.x + footer.width / 2);
        verify(Math.abs(offset) <= 2, "play is " + offset + " px off the player's center at " + data.tag);
    }

    function test_roomBadgeFitsTheRightClusterAtMinimumWindow() {
        shell.destroy();
        shell = createTemporaryObject(roomShellComponent, null);
        verify(shell);
        shell.requestActivate();
        tryVerify(() => testCase.shell.active);
        const footer = findChild(shell, "player-footer");
        resize(1440, 900);
        const wideHeight = footer.height;
        resize(720, 520);
        let nickname = "";
        while (nickname.length < 40)
            nickname += "Anfitriã ";
        nickname = nickname.slice(0, 40);
        fakeRoom.role = 2;
        fakeRoom.state = "joined";
        fakeRoom.hostNickname = nickname;
        fakeRoom.members.append({
            fingerprint: "fp-host",
            nickname: nickname,
            avatarSource: "qrc:/rc_music/avatars/avatar-03.svg",
            isHost: true,
            isSelf: false,
            hasSyncDrift: false,
            syncDriftMs: 0
        });
        fakeRoom.members.append({
            fingerprint: "fp-eu",
            nickname: "Eu",
            avatarSource: "qrc:/rc_music/avatars/avatar-04.svg",
            isHost: false,
            isSelf: true,
            hasSyncDrift: false,
            syncDriftMs: 0
        });
        const badge = findChild(shell, "player-room-badge");
        tryVerify(() => badge.visible);
        verify(badge.text.indexOf(nickname) >= 0, badge.text);
        verify(waitForPolish(shell));
        verify(badge.width <= 200, "badge is " + badge.width + " px wide");
        const side = rect(findChild(shell, "player-side-actions"));
        const area = rect(badge);
        verify(area.x >= side.x - 0.5 && area.x + area.width <= side.x + side.width + 0.5, "badge fits the side horizontally");
        verify(area.y >= side.y - 0.5 && area.y + area.height <= side.y + side.height + 0.5, "badge fits the side vertically");
        verify(area.x + area.width <= shell.width + 0.5);
        const lyrics = rect(findChild(shell, "player-lyrics"));
        verify(area.y + area.height <= lyrics.y + 0.5, "badge sits above the icons");
        for (const name of ["player-lyrics", "player-queue", "mute", "player-identity", "player-repeat"])
            verify(!overlaps(area, rect(findChild(shell, name))), "badge overlaps " + name);
        // A compact window is shorter than badge + icons (60 px): there the player may grow 4 to 8 px while
        // in a room, up to the 120 px limit. A wider window has room for both and keeps its height.
        verify(footer.height <= 120, "footer is " + footer.height + " px in a room");
        compare(findChild(shell, "player-shuffle").disabledReason, queueText.hostControls);
        compare(findChild(shell, "player-shuffle").disabledReason, "O host controla");
        compare(findChild(shell, "player-repeat").disabledReason, queueText.hostControls);
        // Skipping waits for the host's permission, which this guest does not have.
        verify(!fakeRoom.canSkip);
        for (const name of ["previous", "next"]) {
            const button = findChild(shell, name);
            verify(!button.enabled, name);
            compare(button.disabledReason, queueText.hostNotAllowed, name);
            compare(button.disabledReason, "O host não liberou", name);
        }
        resize(1440, 900);
        compare(footer.height, wideHeight, "the badge does not grow a wide player");
    }

    function test_footerStaysCompactWithoutOverlaps_data() {
        return [
            {
                tag: "minimum",
                width: 720,
                height: 520
            },
            {
                tag: "default",
                width: 960,
                height: 640
            },
            {
                tag: "medium",
                width: 1024,
                height: 768
            },
            {
                tag: "wide",
                width: 1440,
                height: 900
            }
        ];
    }

    function test_footerStaysCompactWithoutOverlaps(data) {
        loadSyntheticQueue();
        resize(data.width, data.height);
        const footer = findChild(shell, "player-footer");
        verify(footer.height <= 120, "footer is " + footer.height + " px at " + data.tag);
        compare(footer.y + footer.height, shell.height);
        // ±5 s stay on the keyboard (←/→) and the progress bar at every size.
        verify(!findChild(shell, "skip-backward").visible);
        verify(!findChild(shell, "skip-forward").visible);
        const names = ["player-identity", "player-shuffle", "previous", "play-pause", "next", "player-repeat", "player-lyrics", "player-queue", "mute", "progress"];
        // A compact window keeps only the mute button (test_volumeBarHidesOnCompactWindow).
        if (data.width > productContract.compactWidth)
            names.push("volume", "player-volume-percent");
        const rects = {};
        for (const name of names) {
            const item = findChild(shell, name);
            verify(item && item.visible, name);
            const area = rect(item);
            verify(area.x >= 0 && area.x + area.width <= shell.width + 0.5, name + " fits horizontally");
            verify(area.y >= footer.y - 0.5 && area.y + area.height <= shell.height + 0.5, name + " stays inside the footer");
            if (name !== "player-identity" && name !== "volume" && name !== "player-volume-percent" && name !== "progress")
                verify(item.width >= productContract.controlTargetMinimum, name + " keeps a usable target");
            rects[name] = area;
        }
        for (let first = 0; first < names.length; ++first) {
            for (let second = first + 1; second < names.length; ++second)
                verify(!overlaps(rects[names[first]], rects[names[second]]), names[first] + " overlaps " + names[second]);
        }
    }

    // S13.1: a guest following a room sees the room's item and its time in the player, not "Abra um áudio para
    // começar" and 0:00; leaving goes back to its own (here empty) player.
    function test_aGuestFollowingARoomShowsTheRoomTrackAndItsTime() {
        const title = findChild(shell, "player-track-title");
        const time = findChild(shell, "player-time");
        compare(title.text, productContract.emptyTitle);
        verify(driver.followRoom("Primeira", "Artista", "Album", 600000));
        tryVerify(() => title.text.startsWith("Primeira"), 5000, title.text);
        compare(presenter.currentArtist, "Artista");
        verify(!findChild(shell, "emptyState").visible);

        driver.publishRoomPlayback(true, 1000, 600000);
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        tryCompare(time, "text", "0:01 / 10:00");
        driver.publishRoomPlayback(true, 62000, 600000);
        tryCompare(time, "text", "1:02 / 10:00");
        driver.publishRoomPlayback(false, 62500, 600000);
        tryCompare(presenter, "playbackState", PlayerPresenter.Paused);
        // Nothing of the room entered the guest's own queue.
        verify(!presenter.queueLoaded);

        driver.stopFollowingRoom();
        tryCompare(title, "text", productContract.emptyTitle);
        compare(presenter.hasCurrentTrack, false);
    }
}
