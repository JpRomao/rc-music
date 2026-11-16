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
        verify(drawer.width <= productContract.queuePanelMaximumWidth, "the queue is a side drawer, not a full page");
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

    function test_footerStaysCompactWithoutOverlaps_data() {
        return [
            {
                tag: "minimum",
                width: 720,
                height: 520,
                skips: false
            },
            {
                tag: "default",
                width: 960,
                height: 640,
                skips: true
            },
            {
                tag: "medium",
                width: 1024,
                height: 768,
                skips: true
            },
            {
                tag: "wide",
                width: 1440,
                height: 900,
                skips: true
            }
        ];
    }

    function test_footerStaysCompactWithoutOverlaps(data) {
        loadSyntheticQueue();
        resize(data.width, data.height);
        const footer = findChild(shell, "player-footer");
        verify(footer.height <= 120, "footer is " + footer.height + " px at " + data.tag);
        compare(footer.y + footer.height, shell.height);
        // ±5 s stay on the keyboard (←/→) and the progress bar when the rail is narrow.
        compare(findChild(shell, "skip-backward").visible, data.skips);
        compare(findChild(shell, "skip-forward").visible, data.skips);
        const names = ["player-identity", "player-shuffle", "previous", "play-pause", "next", "player-repeat", "player-lyrics", "player-queue", "mute", "volume", "progress"];
        if (data.skips)
            names.push("skip-backward", "skip-forward");
        const rects = {};
        for (const name of names) {
            const item = findChild(shell, name);
            verify(item && item.visible, name);
            const area = rect(item);
            verify(area.x >= 0 && area.x + area.width <= shell.width + 0.5, name + " fits horizontally");
            verify(area.y >= footer.y - 0.5 && area.y + area.height <= shell.height + 0.5, name + " stays inside the footer");
            if (name !== "player-identity" && name !== "volume" && name !== "progress")
                verify(item.width >= productContract.controlTargetMinimum, name + " keeps a usable target");
            rects[name] = area;
        }
        for (let first = 0; first < names.length; ++first) {
            for (let second = first + 1; second < names.length; ++second)
                verify(!overlaps(rects[names[first]], rects[names[second]]), names[first] + " overlaps " + names[second]);
        }
    }
}
