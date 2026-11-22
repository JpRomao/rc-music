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
}
