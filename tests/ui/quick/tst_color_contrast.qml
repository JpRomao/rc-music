import QtQuick
import QtQuick.Controls
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "ColorContrast"
    when: windowShown
    readonly property var driver: testScenarioDriver // qmllint disable unqualified
    readonly property var presenter: testLocalPlayerPresenter // qmllint disable unqualified
    readonly property var contract: testShellUiContract // qmllint disable unqualified
    property var shell

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testLocalPlayerPresenter // qmllint disable unqualified
        }
    }

    Component {
        id: primaryActionComponent
        AccessibleActionButton {
            contract: testShellUiContract // qmllint disable unqualified
            controlId: "contrast-primary-action"
            text: "Abrir áudio"
        }
    }

    // Just enough of a lyrics binding for the Aurora shell to offer the verse line and the lyrics drawer.
    QtObject {
        id: fakeLyrics
        property string title: "Cantiga"
        property string artist: "Artista"
        property string album: "Disco"
        property int durationMs: 180000
        property string statusText: ""
        property string currentLine: "Primeiro verso"
        property string previousLine: ""
        property string nextLine: "Segundo verso"
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
        property var readerVerses: [
            {
                text: "Verso passado",
                timeMs: 1000
            },
            {
                text: "Verso atual",
                timeMs: 2000
            },
            {
                text: "Verso seguinte",
                timeMs: 3000
            }
        ]
        property int activeReaderVerse: 1
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
    }

    Component {
        id: auroraShellComponent
        Main {
            uiContract: testProductUiContract // qmllint disable unqualified
            presenter: testLocalPlayerPresenter // qmllint disable unqualified
            lyrics: fakeLyrics
        }
    }

    // Just enough of a library catalog for the Aurora library page: three records (one gone).
    ListModel {
        id: libraryRecords
        ListElement {
            recordId: "3"
            title: "Canção do Mar"
            artist: "Lia Campos"
            album: "Depois da Chuva"
            durationText: "3:37"
            available: true
            fileName: "Canção do Mar.mp3"
        }
        ListElement {
            recordId: "4"
            title: "faixa sem nome"
            artist: ""
            album: ""
            durationText: ""
            available: false
            fileName: "faixa sem nome.flac"
        }
        ListElement {
            recordId: "5"
            title: "Faixa ao vivo"
            artist: "Rui Alves"
            album: "Ao Vivo"
            durationText: "4:01"
            available: true
            fileName: "ao vivo.mp3"
        }
    }
    ListModel {
        id: libraryRecent
    }
    QtObject {
        id: libraryFake
        property bool ready: true
        property bool busy: false
        property int count: 3
        property int visibleCount: 3
        property bool hasPendingChange: false
        property bool canRetrySave: false
        property string statusText: "Sua biblioteca está pronta."
        property bool statusProblem: false
        property string filterText: ""
        property string playingRecordId: ""
        property var model: libraryRecords
        property var recentModel: libraryRecent
        property bool canLocate: true
        property string artworkBase: ""
        function enqueue(id) {
            return true;
        }
        function playNow(id) {
            return true;
        }
        function remove(id) {
            return true;
        }
        function locate(id) {
            return true;
        }
        function cancelLocate() {
        }
        function addToPlaylist(id, playlist) {
            return true;
        }
        function retryLoad() {
            return true;
        }
        function retrySave() {
            return true;
        }
    }

    Component {
        id: libraryShellComponent
        Main {
            uiContract: testProductUiContract // qmllint disable unqualified
            presenter: testLocalPlayerPresenter // qmllint disable unqualified
            lyrics: fakeLyrics
            library: libraryFake
        }
    }

    // Replaces the fixture shell with the Aurora one, with nothing opened yet.
    function startAurora(dark) {
        shell.destroy();
        shell = null;
        driver.resetLocalScenario();
        driver.setSystemAppearance(dark, true);
        shell = createTemporaryObject(auroraShellComponent, null);
        verify(shell);
        shell.requestActivate();
        tryVerify(function () {
            return testCase.shell.active;
        });
        compare(shell.uiContract.darkTheme, dark);
        return shell.uiContract;
    }

    // The Aurora shell playing the second of three tracks.
    function openAurora(dark) {
        startAurora(dark);
        driver.enqueueLocalSelection(["C:/Contrast/Primeira.mp3", "C:/Contrast/Segunda.mp3", "C:/Contrast/Terceira.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        verify(presenter.nextTrack());
        tryCompare(presenter, "currentQueueIndex", 1);
        verify(waitForPolish(shell));
        return shell.uiContract;
    }

    // The Aurora shell on the library page. With `playing`, record 3 is the track that plays, as the player reports it.
    function openLibraryPage(dark, playing) {
        shell.destroy();
        shell = null;
        driver.resetLocalScenario();
        driver.setSystemAppearance(dark, true);
        libraryFake.playingRecordId = "";
        libraryFake.statusProblem = false;
        libraryFake.statusText = "Sua biblioteca está pronta.";
        shell = createTemporaryObject(libraryShellComponent, null);
        verify(shell);
        shell.width = 1440;
        shell.height = 900;
        shell.requestActivate();
        tryVerify(() => testCase.shell.active);
        compare(shell.uiContract.darkTheme, dark);
        if (playing) {
            driver.enqueueLocalSelection(["C:/Contrast/Primeira.mp3"]);
            findChild(shell, "open-audio").Accessible.pressAction();
            tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
            driver.publishLocalPlaying();
            tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
            libraryFake.playingRecordId = "3";
        }
        mouseClick(findChild(shell, "library"));
        const panel = findChild(shell, "library-panel");
        tryCompare(panel, "opened", true);
        verify(waitForPolish(shell));
        return panel;
    }

    function libraryRow(panel, id) {
        const list = findChild(panel, "library-list");
        tryVerify(() => findChild(list, "library-row-" + id) !== null);
        return findChild(list, "library-row-" + id);
    }

    function libraryCard(panel, id) {
        const grid = findChild(panel, "library-grid");
        tryVerify(() => findChild(grid, "library-card-" + id) !== null);
        return findChild(grid, "library-card-" + id);
    }

    // The rendered ground of a row: its own background laid over the page canvas.
    function rowGround(row, canvas) {
        const background = findChild(row, "track-row-background");
        verify(background && background.visible);
        return composite(Qt.color(background.color), canvas, 1);
    }

    function hoverRow(row) {
        mouseMove(row, row.width / 2, row.height / 2);
        tryVerify(() => row.actionsShown);
    }

    function restRows() {
        mouseMove(shell.contentItem, shell.width - 2, 2);
    }

    function openQueueDrawer() {
        shell.setQueueOpen(true);
        const drawer = findChild(shell, "queue-panel");
        tryVerify(() => drawer.visible);
        const list = findChild(drawer, "queue-list");
        tryCompare(list, "count", 3);
        tryVerify(() => list.itemAtIndex(2) !== null);
        verify(waitForPolish(shell));
        return drawer;
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

    function themes() {
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

    function init() {
        driver.resetLocalScenario();
        driver.setHighContrast(false, false);
        testPlayerPresenter.setThemePreference(PlayerPresenter.SystemTheme); // qmllint disable unqualified
        testPlayerPresenter.setMotionPreference(PlayerPresenter.SystemMotion); // qmllint disable unqualified
        driver.setSystemAppearance(false, true);
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.requestActivate();
        tryVerify(function () {
            return testCase.shell.active;
        });
        const paths = [];
        for (let index = 0; index < 20; ++index)
            paths.push("C:/Contrast/Track-" + index + ".mp3");
        driver.enqueueLocalSelection(paths);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
    }

    function cleanup() {
        if (shell)
            shell.destroy();
        shell = null;
        driver.resetLocalScenario();
        driver.setSystemAppearance(false, false);
    }

    function applyTheme(data) {
        driver.setSystemAppearance(data.dark, true);
        compare(contract.darkTheme, data.dark);
        compare(contract.highContrast, false);
        verify(waitForPolish(shell));
    }

    function test_publishedTextPairsInBothThemes_data() {
        return themes();
    }
    function test_publishedTextPairsInBothThemes(data) {
        applyTheme(data);
        for (const backgroundName of ["canvasColor", "surfaceColor", "surfaceRaisedColor"]) {
            const background = Qt.darker(contract[backgroundName], 1);
            for (const foregroundName of ["textPrimaryColor", "textSecondaryColor", "errorColor", "warningColor", "successColor"])
                verifyContrast(Qt.darker(contract[foregroundName], 1), background, 4.5, foregroundName + " on " + backgroundName);
        }
        verifyContrast(Qt.darker(contract.accentTextColor, 1), Qt.darker(contract.accentColor, 1), 4.5, "selection text");
        const action = createTemporaryObject(primaryActionComponent, shell.contentItem, {
            x: 8,
            y: 8,
            z: 50
        });
        verify(action && action.enabled && action.visible);
        mouseMove(shell.contentItem, shell.width - 2, 2);
        tryVerify(function () {
            return !action.hovered && !action.down;
        });
        verifyContrast(action.contentItem.color, action.background.color, 4.5, "primary action normal");
        mouseMove(action);
        verify(action.hovered);
        verifyContrast(action.contentItem.color, action.background.color, 4.5, "primary action hovered");
        mousePress(action);
        try {
            verify(action.down);
            verifyContrast(action.contentItem.color, action.background.color, 4.5, "primary action pressed");
        } finally {
            mouseRelease(action);
        }
        action.forceActiveFocus(Qt.TabFocusReason);
        verify(action.activeFocus);
        verifyContrast(action.background.border.color, action.background.color, 3, "primary action focused");
    }

    function test_sliderIndicatorsContrastWithActualCanvas_data() {
        return themes();
    }
    function test_sliderIndicatorsContrastWithActualCanvas(data) {
        applyTheme(data);
        for (const name of ["progress", "volume"]) {
            const slider = findChild(shell, name);
            verify(slider && slider.visible && slider.enabled, name);
            verify(!slider.visualFocus, name + " must start without its focus outline");
            if (slider.edge) {
                // Player C: the edge track is a divider line; the filled part and the marker carry the position.
                verify(Qt.colorEqual(slider.background.color, contract.dividerColor), name + " edge track");
                verifyContrast(slider.handle.color, shell.color, 3, name + " marker");
                verify(Qt.colorEqual(findChild(slider, "slider-marker-ring").color, contract.surfaceColor), name + " marker ring");
            } else {
                verifyContrast(slider.background.color, shell.color, 3, name + " unfilled track");
                verifyContrast(slider.handle.border.color, shell.color, 3, name + " handle outline");
                verifyContrast(slider.handle.border.color, slider.handle.color, 3, name + " handle interior");
            }
            verifyContrast(slider.fillColor, shell.color, 3, name + " filled track");
            slider.forceActiveFocus(Qt.TabFocusReason);
            verify(slider.visualFocus && slider.focusIndicatorVisible);
            verifyContrast(slider.edge ? slider.handle.color : slider.handle.border.color, shell.color, 3, name + " focused handle");
            const focus = findChild(slider, "slider-focus-indicator");
            verify(focus && focus.visible && focus.border.width >= 1);
            verifyContrast(focus.border.color, shell.color, 3, name + " outer focus indicator");
            findChild(shell, "play-pause").forceActiveFocus(Qt.TabFocusReason);
        }
    }

    function test_activeQueueScrollThumbIncludesOpacity_data() {
        return themes();
    }
    function test_activeQueueScrollThumbIncludesOpacity(data) {
        applyTheme(data);
        keyClick(Qt.Key_L, Qt.ControlModifier);
        const queue = findChild(shell, "queue-list");
        const bar = findChild(shell, "queue-scrollbar");
        verify(queue && bar);
        tryVerify(function () {
            return queue.visible && queue.count === 20 && bar.size < 1;
        });
        compare(bar.policy, ScrollBar.AsNeeded);
        bar.active = true;
        const thumb = bar.contentItem;
        tryVerify(function () {
            return thumb.visible && thumb.opacity > 0;
        });
        verify(bar.enabled && bar.opacity === 1);
        const background = Qt.darker(contract.surfaceColor, 1);
        verifyContrast(composite(thumb.color, background, thumb.opacity), background, 3, "active queue thumb including opacity");
    }

    function test_verseLineTextContrast_data() {
        return themes();
    }
    function test_verseLineTextContrast(data) {
        const aurora = openAurora(data.dark);
        const line = findChild(shell, "lyrics-line");
        verify(line && line.visible);
        const surface = Qt.darker(aurora.surfaceColor, 1);
        const background = composite(Qt.color(line.color), surface, 1);
        verify(Qt.colorEqual(background, surface), "the line sits on the surface");
        for (const name of ["lyrics-line-current", "lyrics-line-next"]) {
            const text = findChild(line, name);
            verify(text && text.visible, name);
            verifyContrast(Qt.color(text.color), background, 4.5, name + " on the verse line, " + data.tag);
        }
        const icon = findChild(line, "lyrics-line-icon");
        verify(icon && icon.visible);
        verifyContrast(Qt.color(icon.color), background, 3, "verse line icon, " + data.tag);
    }

    function test_drawerCurrentVerseContrast_data() {
        return themes();
    }
    function test_drawerCurrentVerseContrast(data) {
        const aurora = openAurora(data.dark);
        mouseClick(findChild(shell, "player-lyrics"));
        const reader = findChild(shell, "lyrics-reader");
        tryCompare(reader, "opened", true);
        const list = findChild(reader, "lyrics-reader-verses");
        tryVerify(() => list.itemAtIndex(0) && list.itemAtIndex(1) && list.itemAtIndex(2));
        findChild(shell, "lyrics-drawer-close").forceActiveFocus(Qt.TabFocusReason);
        const surface = Qt.darker(aurora.surfaceColor, 1);
        const current = list.itemAtIndex(1);
        verify(Qt.colorEqual(current.contentItem.color, aurora.accentColor));
        const background = composite(Qt.color(current.background.color), surface, 1);
        verifyContrast(Qt.color(current.contentItem.color), background, 4.5, "current verse on the lyrics drawer, " + data.tag);
        verifyContrast(Qt.color(list.itemAtIndex(0).contentItem.color), surface, 4.5, "past verse on the lyrics drawer, " + data.tag);
        verifyContrast(Qt.color(list.itemAtIndex(2).contentItem.color), surface, 4.5, "next verse on the lyrics drawer, " + data.tag);
    }

    function test_queueStatusAndNowContrast_data() {
        return themes();
    }
    function test_queueStatusAndNowContrast(data) {
        const aurora = openAurora(data.dark);
        const drawer = openQueueDrawer();
        const list = findChild(drawer, "queue-list");
        const surface = Qt.darker(aurora.surfaceColor, 1);
        const row = list.itemAtIndex(1);
        verify(row.ListView.isCurrentItem && row.isPlaying);
        const rowBackground = composite(Qt.color(row.background.color), surface, 1);
        const title = findChild(row, "queue-row-title");
        verify(Qt.colorEqual(title.color, aurora.accentColor));
        verifyContrast(Qt.color(title.color), rowBackground, 4.5, "current row title on its row, " + data.tag);
        const icon = findChild(row, "queue-row-now-icon");
        verify(icon && icon.visible && icon.iconName === "waveform");
        verifyContrast(Qt.color(icon.color), rowBackground, 3, "waveform icon on the current row, " + data.tag);
        // The warning text on the plain drawer surface, where an ordinary row sits.
        verifyContrast(Qt.darker(aurora.warningColor, 1), surface, 4.5, "warning status on the drawer surface, " + data.tag);
        // A hovered ordinary row: the status must also hold on the hover layer.
        const other = list.itemAtIndex(0);
        mouseMove(other, other.width / 2, other.height / 2);
        tryVerify(() => other.hovered);
        const hoverBackground = composite(Qt.color(other.background.color), surface, 1);
        verifyContrast(Qt.darker(aurora.warningColor, 1), hoverBackground, 4.5, "warning status on a hovered row, " + data.tag);
    }

    function test_unavailableCurrentRowStatusContrast_data() {
        return themes();
    }
    function test_unavailableCurrentRowStatusContrast(data) {
        shell.destroy();
        shell = null;
        driver.resetLocalScenario();
        driver.setSystemAppearance(data.dark, true);
        shell = createTemporaryObject(auroraShellComponent, null);
        verify(shell);
        shell.requestActivate();
        tryVerify(() => testCase.shell.active);
        const aurora = shell.uiContract;
        compare(aurora.darkTheme, data.dark);
        driver.enqueueLocalSelection(["C:/Contrast/Arquivo movido.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalSourceUnavailable();
        tryCompare(presenter, "playbackState", PlayerPresenter.Error);
        shell.setQueueOpen(true);
        const drawer = findChild(shell, "queue-panel");
        tryVerify(() => drawer.visible);
        const list = findChild(drawer, "queue-list");
        tryVerify(() => list.itemAtIndex(0) !== null);
        const surface = Qt.darker(aurora.surfaceColor, 1);
        const failing = list.itemAtIndex(0);
        const status = findChild(failing, "queue-row-status");
        tryVerify(() => status.visible);
        verify(Qt.colorEqual(status.color, aurora.warningColor));
        list.currentIndex = 0;
        tryVerify(() => failing.ListView.isCurrentItem);
        const background = composite(Qt.color(failing.background.color), surface, 1);
        verifyContrast(Qt.color(status.color), background, 4.5, "warning status on the selected row, " + data.tag);
        verifyContrast(Qt.color(findChild(failing, "queue-row-title").color), background, 4.5, "unavailable selected title, " + data.tag);
    }

    function test_clearQueueInkContrast_data() {
        return themes();
    }
    function test_clearQueueInkContrast(data) {
        const aurora = openAurora(data.dark);
        const drawer = openQueueDrawer();
        const surface = Qt.darker(aurora.surfaceColor, 1);
        const clear = findChild(drawer, "queue-clear");
        verify(clear && clear.visible && clear.enabled && clear.errorInk);
        const label = findChild(clear, "button-label");
        const icon = findChild(clear, "button-icon");
        mouseMove(shell.contentItem, shell.width - 2, 2);
        tryVerify(() => !clear.hovered && !clear.down);
        function check(state) {
            const background = composite(Qt.color(clear.background.color), surface, 1);
            verifyContrast(Qt.color(label.color), background, 4.5, "Limpar fila " + state + ", " + data.tag);
            verifyContrast(Qt.color(icon.color), background, 3, "trash icon " + state + ", " + data.tag);
        }
        check("at rest");
        mouseMove(clear, clear.width / 2, clear.height / 2);
        tryVerify(() => clear.hovered);
        check("hovered");
        mousePress(clear, clear.width / 2, clear.height / 2);
        try {
            verify(clear.down);
            check("pressed");
        } finally {
            mouseRelease(clear, clear.width / 2, clear.height / 2);
        }
    }

    function test_clearQueueInkUsesSystemColorsInHighContrast() {
        openAurora(false);
        const drawer = openQueueDrawer();
        driver.setHighContrast(true, false);
        const hc = shell.uiContract;
        tryVerify(() => hc.highContrast);
        const clear = findChild(drawer, "queue-clear");
        const label = findChild(clear, "button-label");
        verify(Qt.colorEqual(label.color, hc.textPrimaryColor), "the ink is the system text color, not a custom red");
        verify(Qt.colorEqual(findChild(clear, "button-icon").color, hc.textPrimaryColor));
        const surface = Qt.darker(hc.surfaceColor, 1);
        verifyContrast(Qt.color(label.color), surface, 4.5, "Limpar fila in high contrast");
        mouseMove(clear, clear.width / 2, clear.height / 2);
        tryVerify(() => clear.hovered);
        compare(clear.background.border.width, 2);
        verifyContrast(Qt.color(clear.background.border.color), composite(Qt.color(clear.background.color), surface, 1), 3, "hover outline in high contrast");
    }

    function test_heroTextContrast_data() {
        return themes();
    }
    function test_heroTextContrast(data) {
        const aurora = openAurora(data.dark);
        const hero = findChild(shell, "home-hero");
        verify(Qt.colorEqual(hero.color, aurora.surfaceColor), "the hero is painted on the surface");
        const surface = Qt.darker(aurora.surfaceColor, 1);
        for (const name of ["hero-eyebrow", "hero-artist-album"]) {
            const text = findChild(hero, name);
            verify(text && text.visible, name);
            verifyContrast(Qt.color(text.color), surface, 4.5, name + " on the hero, " + data.tag);
        }
    }

    function test_shelfTextContrast_data() {
        return themes();
    }
    function test_shelfTextContrast(data) {
        // The first of three files is gone, so the queue shelf also has a warning subtitle.
        const aurora = startAurora(data.dark);
        driver.enqueueLocalSelection(["C:/Contrast/Movida.mp3", "C:/Contrast/Segunda.mp3", "C:/Contrast/Terceira.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        const first = presenter.currentOccurrenceId;
        driver.publishLocalSourceUnavailable();
        tryVerify(() => presenter.currentOccurrenceId !== first);
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        verify(waitForPolish(shell));
        const canvas = Qt.darker(aurora.canvasColor, 1);
        const shelf = findChild(shell, "home-queue-shelf");
        tryVerify(() => shelf.visible);
        const subtitle = findChild(shelf, "shelf-subtitle");
        verify(subtitle.visible && subtitle.text.length > 0);
        verifyContrast(Qt.color(subtitle.color), canvas, 4.5, "shelf subtitle, " + data.tag);
        const list = findChild(shelf, "home-queue-list");
        tryCompare(list, "count", 3);
        tryVerify(() => list.itemAtIndex(0) !== null && list.itemAtIndex(1) !== null);
        const available = findChild(list.itemAtIndex(1), "shelf-card-subtitle");
        verifyContrast(Qt.color(available.color), canvas, 4.5, "card subtitle, " + data.tag);
        const unavailable = findChild(list.itemAtIndex(0), "shelf-card-subtitle");
        verify(Qt.colorEqual(unavailable.color, aurora.warningColor), "the missing file warns");
        verifyContrast(Qt.color(unavailable.color), canvas, 4.5, "warning card subtitle, " + data.tag);
    }

    function test_playingBadgeContrast_data() {
        return themes();
    }
    function test_playingBadgeContrast(data) {
        openAurora(data.dark);
        // The chip shows only while the track plays.
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        const list = findChild(shell, "home-queue-list");
        tryCompare(list, "count", 3);
        tryVerify(() => list.itemAtIndex(1) !== null);
        const badge = findChild(list.itemAtIndex(1), "shelf-card-playing");
        verify(badge && badge.visible);
        // The chip sits over the cover; its own background is opaque enough to be the ground of its label.
        const background = findChild(badge, "chip-background");
        const ground = composite(Qt.color(background.color), Qt.darker(shell.uiContract.canvasColor, 1), 1);
        verifyContrast(Qt.color(findChild(badge, "chip-label").color), ground, 4.5, "playing badge, " + data.tag);
    }

    function test_stateIconsContrast_data() {
        return themes();
    }
    function test_stateIconsContrast(data) {
        const aurora = startAurora(data.dark);
        const surface = Qt.darker(aurora.surfaceColor, 1);
        const empty = findChild(shell, "home-empty");
        verify(empty && empty.visible);
        const emptyCircle = findChild(empty, "empty-icon-circle");
        verifyContrast(Qt.color(findChild(emptyCircle, "empty-icon").color), composite(Qt.color(emptyCircle.color), surface, 1), 3, "empty icon, " + data.tag);
        driver.enqueueLocalSelection(["C:/Contrast/Arquivo movido.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalSourceUnavailable();
        tryCompare(presenter, "playbackState", PlayerPresenter.Error);
        verify(waitForPolish(shell));
        const errorCircle = findChild(shell, "error-icon-circle");
        verify(errorCircle && errorCircle.visible);
        verifyContrast(Qt.color(findChild(errorCircle, "empty-icon").color), composite(Qt.color(errorCircle.color), surface, 1), 3, "error icon, " + data.tag);
    }

    function test_libraryRowTextContrast_data() {
        return themes();
    }
    function test_libraryRowTextContrast(data) {
        const panel = openLibraryPage(data.dark, true);
        const aurora = shell.uiContract;
        const canvas = Qt.darker(aurora.canvasColor, 1);
        const hover = composite(Qt.color(aurora.hoverColor), canvas, 1);
        restRows();
        const quiet = libraryRow(panel, "5");
        tryVerify(() => !quiet.actionsShown);
        verify(Qt.colorEqual(rowGround(quiet, canvas), canvas), "a resting row sits on the canvas");
        for (const name of ["track-row-number", "track-row-subtitle", "track-row-album", "track-row-duration"]) {
            const text = findChild(quiet, name);
            verify(text && text.visible && text.text.length > 0, name);
            verifyContrast(Qt.color(text.color), canvas, 4.5, name + " on the canvas, " + data.tag);
        }
        const playingRow = libraryRow(panel, "3");
        verify(playingRow.current && playingRow.playing);
        const title = findChild(playingRow, "track-row-title");
        verify(title.visible && Qt.colorEqual(title.color, aurora.accentColor), "the playing title is the accent");
        verifyContrast(Qt.color(title.color), canvas, 4.5, "playing title on the canvas, " + data.tag);
        // The hovered row: the secondary texts turn primary and the number gives way to the play button.
        hoverRow(quiet);
        verify(Qt.colorEqual(rowGround(quiet, canvas), hover), "a hovered row sits on the hover color");
        verify(!findChild(quiet, "track-row-number").visible);
        for (const name of ["track-row-subtitle", "track-row-album", "track-row-duration"]) {
            const text = findChild(quiet, name);
            verify(text.visible && text.text.length > 0, name);
            verifyContrast(Qt.color(text.color), hover, 4.5, name + " on the hover row, " + data.tag);
        }
        verifyContrast(Qt.color(findChild(quiet, "track-row-title").color), hover, 4.5, "title on the hover row, " + data.tag);
        hoverRow(playingRow);
        verify(Qt.colorEqual(rowGround(playingRow, canvas), hover));
        verify(Qt.colorEqual(findChild(playingRow, "track-row-title").color, aurora.accentColor));
        verifyContrast(Qt.color(findChild(playingRow, "track-row-title").color), hover, 4.5, "playing title on the hover row, " + data.tag);
    }

    function test_libraryUnavailableContrast_data() {
        return themes();
    }
    function test_libraryUnavailableContrast(data) {
        const panel = openLibraryPage(data.dark, false);
        const aurora = shell.uiContract;
        const canvas = Qt.darker(aurora.canvasColor, 1);
        const hover = composite(Qt.color(aurora.hoverColor), canvas, 1);
        restRows();
        const gone = libraryRow(panel, "4");
        verify(!gone.available);
        tryVerify(() => !gone.actionsShown);
        const warning = findChild(gone, "track-row-unavailable");
        verify(warning && warning.visible);
        verify(Qt.colorEqual(warning.color, aurora.warningColor), "the warning is the warning color");
        const title = findChild(gone, "track-row-title");
        verify(title.visible);
        verifyContrast(Qt.color(warning.color), canvas, 4.5, "unavailable warning on the canvas, " + data.tag);
        verifyContrast(Qt.color(title.color), canvas, 4.5, "unavailable title on the canvas, " + data.tag);
        hoverRow(gone);
        verify(Qt.colorEqual(rowGround(gone, canvas), hover));
        verifyContrast(Qt.color(warning.color), hover, 4.5, "unavailable warning on the hover row, " + data.tag);
        verifyContrast(Qt.color(title.color), hover, 4.5, "unavailable title on the hover row, " + data.tag);
    }

    function test_libraryIconsContrast_data() {
        return themes();
    }
    function test_libraryIconsContrast(data) {
        const panel = openLibraryPage(data.dark, true);
        const aurora = shell.uiContract;
        const canvas = Qt.darker(aurora.canvasColor, 1);
        const hover = composite(Qt.color(aurora.hoverColor), canvas, 1);
        restRows();
        const playingRow = libraryRow(panel, "3");
        tryVerify(() => !playingRow.actionsShown);
        const wave = findChild(playingRow, "track-row-wave");
        verify(wave && wave.visible);
        verifyContrast(Qt.color(wave.color), canvas, 3, "playing wave on the canvas, " + data.tag);
        // The play button: a filled circle over the hover row, with the icon in the accent's text color.
        const quiet = libraryRow(panel, "5");
        hoverRow(quiet);
        const play = findChild(quiet, "track-row-play");
        verify(play && play.visible && play.enabled);
        const circle = findChild(play, "icon-button-background");
        const disc = composite(Qt.color(circle.color), hover, 1);
        const icon = play.contentItem.children[0];
        verify(icon && icon.visible);
        verifyContrast(Qt.color(icon.color), disc, 3, "play icon on its circle, " + data.tag);
        // The focus ring of the current row of a focused list, over the row's own ground.
        restRows();
        const list = findChild(panel, "library-list");
        list.currentIndex = 2;
        list.forceActiveFocus(Qt.TabFocusReason);
        tryVerify(() => quiet.highlighted);
        const ring = quiet.children.find(child => child.objectName === "focus-ring");
        verify(ring && ring.visible && ring.border.width >= 1);
        verifyContrast(Qt.color(ring.border.color), rowGround(quiet, canvas), 3, "row focus ring, " + data.tag);
    }

    function test_libraryBadgesContrast_data() {
        return themes();
    }
    function test_libraryBadgesContrast(data) {
        const panel = openLibraryPage(data.dark, true);
        const canvas = Qt.darker(shell.uiContract.canvasColor, 1);
        mouseClick(findChild(panel, "library-view-1"));
        tryVerify(() => findChild(panel, "library-grid").visible);
        const cards = {
            "shelf-card-playing": libraryCard(panel, "3"),
            "shelf-card-unavailable": libraryCard(panel, "4")
        };
        for (const name of Object.keys(cards)) {
            const badge = findChild(cards[name], name);
            verify(badge && badge.visible, name);
            const background = findChild(badge, "chip-background");
            const label = findChild(badge, "chip-label");
            verify(label.visible && label.text.length > 0, name);
            // The chip sits over the cover; its own background is the ground of its label.
            const ground = composite(Qt.color(background.color), canvas, 1);
            verifyContrast(Qt.color(label.color), ground, 4.5, name + " label, " + data.tag);
            verifyContrast(Qt.color(findChild(badge, "chip-icon").color), ground, 3, name + " icon, " + data.tag);
        }
    }

    function test_libraryFeedbackContrast_data() {
        return themes();
    }
    function test_libraryFeedbackContrast(data) {
        const panel = openLibraryPage(data.dark, false);
        const aurora = shell.uiContract;
        const canvas = Qt.darker(aurora.canvasColor, 1);
        // Locating a missing file is the library's own status line: the catalog's text, as success or as error.
        restRows();
        const locate = findChild(libraryRow(panel, "4"), "track-row-locate");
        tryVerify(() => locate.visible && locate.enabled);
        mouseClick(locate);
        const feedback = findChild(panel, "library-feedback");
        const text = findChild(feedback, "feedback-text");
        tryVerify(() => feedback.visible && text.text.length > 0);
        verify(Qt.colorEqual(text.color, aurora.successColor), "a plain status is a success");
        verifyContrast(Qt.color(text.color), canvas, 4.5, "success feedback, " + data.tag);
        verifyContrast(Qt.color(findChild(feedback, "feedback-icon").color), canvas, 3, "success icon, " + data.tag);
        libraryFake.statusText = "Não foi possível salvar a biblioteca.";
        libraryFake.statusProblem = true;
        tryVerify(() => Qt.colorEqual(text.color, aurora.errorColor));
        verify(feedback.visible);
        verifyContrast(Qt.color(text.color), canvas, 4.5, "error feedback, " + data.tag);
        verifyContrast(Qt.color(findChild(feedback, "feedback-icon").color), canvas, 3, "error icon, " + data.tag);
    }

    function test_libraryHighContrastRowContrast_data() {
        return [
            {
                tag: "dark",
                light: false
            },
            {
                tag: "light",
                light: true
            }
        ];
    }
    function test_libraryHighContrastRowContrast(data) {
        const panel = openLibraryPage(false, true);
        driver.setHighContrast(true, data.light);
        const hc = shell.uiContract;
        tryVerify(() => hc.highContrast);
        verify(waitForPolish(shell));
        const canvas = Qt.darker(hc.canvasColor, 1);
        const list = findChild(panel, "library-list");
        restRows();
        list.forceActiveFocus(Qt.TabFocusReason);
        // The current row, playing or gone or plain: the selection ground carries every text and icon.
        for (const entry of [
            {
                at: 0,
                id: "3"
            },
            {
                at: 1,
                id: "4"
            },
            {
                at: 2,
                id: "5"
            }
        ]) {
            list.currentIndex = entry.at;
            const current = libraryRow(panel, entry.id);
            tryVerify(() => current.highlighted);
            const ground = rowGround(current, canvas);
            // A missing file keeps the window ground: its disabled play button has none of its own.
            const expected = current.available ? Qt.darker(hc.selectionColor, 1) : Qt.darker(hc.hoverColor, 1);
            verify(Math.abs(ground.r - expected.r) + Math.abs(ground.g - expected.g) + Math.abs(ground.b - expected.b) < 0.01, "row " + entry.id + " ground, " + data.tag + ": " + ground + " vs " + expected);
            const names = ["track-row-title", "track-row-subtitle", "track-row-album", "track-row-duration"];
            if (!current.available)
                names.push("track-row-unavailable");
            for (const name of names) {
                const text = findChild(current, name);
                verify(text && text.visible, name);
                if (text.text.length > 0)
                    verifyContrast(Qt.color(text.color), ground, 4.5, name + " on the current row " + entry.id + ", " + data.tag);
            }
            const buttons = ["track-row-play", "track-row-enqueue", "track-row-more"];
            for (const name of buttons) {
                const button = findChild(current, name);
                if (!button.visible)
                    continue;
                const circle = findChild(button, "icon-button-background");
                const fill = circle.color.a > 0 ? composite(Qt.color(circle.color), ground, 1) : ground;
                verifyContrast(Qt.color(button.contentItem.children[0].color), fill, 3, name + " icon on the current row " + entry.id + ", " + data.tag);
            }
        }
        // The wave of the playing row, when the row is not current, sits on the canvas.
        const playingRow = libraryRow(panel, "3");
        tryVerify(() => !playingRow.highlighted);
        const wave = findChild(playingRow, "track-row-wave");
        verify(wave.visible);
        verifyContrast(Qt.color(wave.color), rowGround(playingRow, canvas), 3, "wave on the canvas, " + data.tag);
        verifyContrast(Qt.color(findChild(playingRow, "track-row-title").color), rowGround(playingRow, canvas), 4.5, "playing title on the canvas, " + data.tag);
    }
}
