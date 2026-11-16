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
        findChild(shell, "queue").Accessible.pressAction();
        tryVerify(() => queue.visible);
        verify(!findChild(shell, "state-layer").visible);
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

    function test_quickThemeUsesThePreferenceAndPreservesPlayback() {
        loadSyntheticQueue();
        const button = findChild(shell, "theme-toggle");
        verify(button);
        const commands = audioCommands();
        const occurrence = presenter.currentOccurrenceId;
        button.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        tryCompare(productContract, "darkTheme", true);
        compare(presenter.themePreference, PlayerPresenter.DarkTheme);
        compare(String(shell.color).toUpperCase(), "#1B1917");
        button.Accessible.pressAction();
        tryCompare(productContract, "darkTheme", false);
        compare(presenter.themePreference, PlayerPresenter.LightTheme);
        compare(String(shell.color).toUpperCase(), "#F5F1E9");
        compare(presenter.currentOccurrenceId, occurrence);
        compare(presenter.positionMs, 41000);
        compare(presenter.volume, 0.52);
        compare(audioCommands(), commands);
    }

    function test_nativeFrameUsesAuroraColorsAndPersistentTrackIdentity() {
        loadSyntheticQueue();
        shell.width = 1440;
        shell.height = 900;
        tryCompare(shell.contentItem, "width", 1440);
        tryCompare(shell.contentItem, "height", 900);
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
            "mute": "mute"
        };
        for (const name in transportIcons)
            verifyButtonIcon(findChild(shell, name), transportIcons[name]);
        compare(findChild(shell, "elapsed-time").font.family, "Segoe UI");
        compare(findChild(shell, "player-track-title").font.family, "Segoe UI");
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
        findChild(shell, "queue").Accessible.pressAction();
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
        verifyContrast(findChild(shell, "elapsed-time").color, footer.color, 4.5, "elapsed time");
        for (const name of ["now", "queue", "theme-toggle", "preferences"]) {
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
        for (const name of ["previous", "play-pause", "next", "mute"])
            verifyContrast(findChild(shell, name).contentItem.color, findChild(shell, name).background.color, 4.5, name + " rendered foreground");
        for (const name of ["progress", "volume"]) {
            const slider = findChild(shell, name);
            verify(slider.enabled);
            verifyContrast(slider.background.color, footer.color, 3, name + " track on actual footer");
            verifyContrast(slider.handle.border.color, footer.color, 3, name + " handle outline");
            verifyContrast(slider.handle.border.color, slider.handle.color, 3, name + " handle interior");
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
        for (const entry of [["now", "home"], ["queue", "queue"], ["preferences", "settings"]])
            verifyButtonIcon(findChild(shell, entry[0]), entry[1]);
        const selectedIcon = findChild(findChild(shell, "now"), "button-icon");
        verify(Qt.colorEqual(selectedIcon.color, productContract.accentColor), "selected icon uses the accent");
        // Theme toggle offers the opposite theme: sun to go light, moon to go dark.
        verifyButtonIcon(findChild(shell, "theme-toggle"), data.dark ? "sun" : "moon");
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
        const other = findChild(shell, "queue");
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
        for (const name of ["now", "queue", "preferences", "theme-toggle", "play-pause", "volume", "progress"])
            verifyInside(findChild(shell, name));
        findChild(shell, "queue").Accessible.pressAction();
        const queue = findChild(shell, "queue-panel");
        tryVerify(() => queue.visible);
        const q = queue.mapToItem(shell.contentItem, 0, 0);
        const p = footer.mapToItem(shell.contentItem, 0, 0);
        verify(q.y + queue.height <= p.y + 1);
        compare(p.y + footer.height, shell.height);
        verify(!queue.overlayMode);
    }
}
