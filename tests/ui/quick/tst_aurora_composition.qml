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
        verify(header.height >= 64);
        verify(point(home).y >= header.height);
        verify(point(home).y + home.height <= footer.y + 1);
        verify(findChild(shell, "player-identity"));
        verify(footer.height <= (data.width >= 1200 ? 130 : 184), "The footer must not remain a full-width enlarged compact player");
        compare(footer.y + footer.height, shell.height);
        for (const name of ["now", "queue", "preferences", "theme-toggle", "play-pause", "volume", "progress", "player-shuffle", "player-repeat"])
            assertControlInside(findChild(shell, name));
        if (data.width === 720) {
            compare(findChild(shell, "side-navigation").width, 68);
            verify(home.contentHeight > home.height, "The small workspace must scroll instead of squeezing controls");
        }
        shell.update();
        verify(waitForRendering(shell.contentItem));
        const artifact = driver.artifactPath("aurora-redesign-" + data.tag + ".png");
        verify(artifact.length > 0);
        const image = grabImage(shell.contentItem);
        compare(image.width, data.width);
        compare(image.height, data.height);
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
