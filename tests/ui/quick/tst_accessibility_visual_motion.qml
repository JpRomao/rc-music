import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "AccessibilityVisualMotion"
    when: windowShown

    Component {
        id: fixtureShellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testPlayerPresenter // qmllint disable unqualified
        }
    }

    Component {
        id: localShellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testLocalPlayerPresenter // qmllint disable unqualified
        }
    }

    property var shell

    function luminance(color) {
        function linear(value) {
            return value <= 0.04045 ? value / 12.92 : Math.pow((value + 0.055) / 1.055, 2.4);
        }
        return 0.2126 * linear(color.r) + 0.7152 * linear(color.g) + 0.0722 * linear(color.b);
    }

    function contrastRatio(firstColor, secondColor) {
        const first = luminance(firstColor);
        const second = luminance(secondColor);
        return (Math.max(first, second) + 0.05) / (Math.min(first, second) + 0.05);
    }

    function activate(candidate) {
        shell = candidate;
        verify(shell);
        shell.width = 720;
        shell.height = 520;
        shell.requestActivate();
        tryVerify(function () {
            return shell.active;
        });
    }

    function init() {
        testScenarioDriver.setHighContrast(false, false); // qmllint disable unqualified
        testScenarioDriver.resetLocalScenario(); // qmllint disable unqualified
        shell = null;
    }

    function cleanup() {
        if (shell)
            testScenarioDriver.cancelDrag(shell); // qmllint disable unqualified
        if (shell)
            shell.destroy();
        shell = null;
        testScenarioDriver.resetLocalScenario(); // qmllint disable unqualified
        testScenarioDriver.setSystemAppearance(false, false); // qmllint disable unqualified
    }

    function verifyPrimaryFocusRing(openAudio) {
        openAudio.forceActiveFocus(Qt.TabFocusReason);
        tryVerify(function () {
            return openAudio.activeFocus;
        });
        compare(openAudio.background.border.width, 3);
        compare(openAudio.background.border.color.toString(), testShellUiContract.accentTextColor.toLowerCase()); // qmllint disable unqualified
        const ratio = contrastRatio(openAudio.background.border.color, openAudio.background.color);
        verify(ratio >= 3, "Primary focus ring contrast: " + ratio);
    }

    function test_primaryButtonFocusRingUsesStrongSemanticContrastInBothThemes() {
        testPlayerPresenter.setThemePreference(PlayerPresenter.SystemTheme); // qmllint disable unqualified
        compare(testPlayerPresenter.themePreference, PlayerPresenter.SystemTheme); // qmllint disable unqualified
        testScenarioDriver.setSystemAppearance(false, false); // qmllint disable unqualified
        activate(createTemporaryObject(fixtureShellComponent, null));

        const openAudio = findChild(shell, "open-audio");
        verify(openAudio);
        verifyPrimaryFocusRing(openAudio);

        testScenarioDriver.setSystemAppearance(true, false); // qmllint disable unqualified
        tryVerify(function () {
            return testShellUiContract.darkTheme; // qmllint disable unqualified
        });
        verifyPrimaryFocusRing(openAudio);
    }

    function test_reducedMotionStopsBusyAnimationButKeepsLoadingStatusVisible() {
        testLocalPlayerPresenter.setMotionPreference(PlayerPresenter.SystemMotion); // qmllint disable unqualified
        compare(testLocalPlayerPresenter.motionPreference, PlayerPresenter.SystemMotion); // qmllint disable unqualified
        testScenarioDriver.setSystemAppearance(false, false); // qmllint disable unqualified
        activate(createTemporaryObject(localShellComponent, null));

        const center = Qt.point(shell.width / 2, shell.height / 2);
        compare(testScenarioDriver.beginUrlDrag(shell, center.x, center.y, ["C:/Entrada"]), Qt.CopyAction); // qmllint disable unqualified
        compare(testScenarioDriver.dropDrag(shell, center.x, center.y), Qt.CopyAction); // qmllint disable unqualified
        tryCompare(testLocalPlayerPresenter, "sourceImportState", PlayerPresenter.Discovering); // qmllint disable unqualified

        const status = findChild(shell, "source-import-status");
        const busy = findChild(shell, "source-import-busy-indicator");
        const progress = findChild(shell, "source-import-progress");
        verify(status && busy && progress);
        verify(status.visible);
        verify(progress.visible && progress.text.length > 0);
        verify(status.Accessible.name.length > 0);
        verify(busy.visible);
        verify(busy.running);

        testScenarioDriver.setSystemAppearance(false, true); // qmllint disable unqualified
        tryVerify(function () {
            return testShellUiContract.reducedMotion; // qmllint disable unqualified
        });
        verify(status.visible);
        verify(progress.visible && progress.text.length > 0);
        verify(status.Accessible.name.length > 0);
        verify(busy.visible);
        verify(!busy.running);
    }
}
