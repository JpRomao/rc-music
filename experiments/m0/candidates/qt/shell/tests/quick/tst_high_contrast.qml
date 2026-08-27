import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "ShellHighContrast"
    when: windowShown

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testPlayerPresenter // qmllint disable unqualified
        }
    }

    function luminance(color) {
        function linear(value) {
            return value <= 0.04045 ? value / 12.92 : Math.pow((value + 0.055) / 1.055, 2.4);
        }
        return 0.2126 * linear(color.r) + 0.7152 * linear(color.g) + 0.0722 * linear(color.b);
    }
    function verifyTextContrast(foregroundColor, backgroundColor) {
        const foreground = luminance(foregroundColor);
        const background = luminance(backgroundColor);
        const ratio = (Math.max(foreground, background) + 0.05) / (Math.min(foreground, background) + 0.05);
        verify(ratio >= 4.5, "Text contrast: " + ratio);
    }
    function selectChoice(control, index) {
        control.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        tryVerify(function () {
            return control.popup.opened;
        });
        verifyTextContrast(control.contentItem.color, control.background.color);
        verifyTextContrast(control.indicator.color, control.background.color);
        const focus = findChild(control, control.objectName + "-focus");
        verify(focus && focus.visible);
        verifyTextContrast(focus.border.color, control.background.color);
        compare(focus.border.color, control.palette.highlightedText);
        keyClick(Qt.Key_Home);
        for (let step = 0; step < index; ++step)
            keyClick(Qt.Key_Down);
        for (let rowIndex = 0; rowIndex < control.count; ++rowIndex) {
            const row = control.popup.contentItem.itemAtIndex(rowIndex);
            verify(row);
            verifyTextContrast(row.contentItem.color, row.background.color);
        }
        keyClick(Qt.Key_Return);
        tryVerify(function () {
            return !control.popup.visible;
        });
        compare(control.currentIndex, index);
    }
    function verifySelectedRow(row, background, foreground) {
        compare(row.background.color.toString(), background);
        compare(row.background.color.a, 1);
        const title = findChild(row, "queue-row-title");
        compare(title.color.toString(), foreground);
        verifyTextContrast(title.color, row.background.color);
        compare(row.background.border.color.toString(), foreground);
        verify(row.background.border.width >= 2);
    }

    function test_highContrastJourney() {
        testScenarioDriver.setHighContrast(true, false); // qmllint disable unqualified
        testScenarioDriver.setSystemAppearance(false, false); // qmllint disable unqualified
        const shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.width = 720;
        shell.height = 520;
        shell.requestActivate();
        tryVerify(function () {
            return shell.active;
        });
        verify(testShellUiContract.highContrast); // qmllint disable unqualified
        compare(shell.color.toString(), "#010102");
        compare(findChild(shell, "empty-title").color.toString(), "#fafafa");
        const playPause = findChild(shell, "play-pause");
        verify(!playPause.enabled);
        compare(playPause.opacity, 1);
        compare(playPause.contentItem.color.toString(), "#bbbbbb");
        compare(playPause.background.color.toString(), "#010102");
        const progress = findChild(shell, "progress");
        compare(progress.background.color.toString(), "#010102");
        compare(progress.background.border.width, 1);
        compare(progress.background.border.color.toString(), "#bbbbbb");
        const openAudio = findChild(shell, "open-audio");
        openAudio.forceActiveFocus(Qt.TabFocusReason);
        compare(openAudio.background.border.color.toString(), "#010101");
        mousePress(openAudio);
        compare(openAudio.background.color.toString(), "#f8f800");
        verifyTextContrast(openAudio.contentItem.color, openAudio.background.color);
        mouseRelease(openAudio);
        verify(testPlayerPresenter.queueLoaded); // qmllint disable unqualified
        mouseClick(playPause);
        compare(testPlayerPresenter.playbackState, PlayerPresenter.Paused); // qmllint disable unqualified
        const positionBefore = testPlayerPresenter.positionMs; // qmllint disable unqualified
        const modelBefore = testPlayerPresenter.queueModel; // qmllint disable unqualified
        compare(progress.background.color.toString(), "#010102");
        compare(progress.background.border.color.toString(), "#fafafa");
        compare(progress.background.children[0].color.toString(), "#f8f800");
        progress.forceActiveFocus(Qt.TabFocusReason);
        verify(progress.visualFocus);
        compare(progress.handle.color.toString(), "#010102");
        compare(progress.handle.border.color.toString(), "#fafafa");
        playPause.forceActiveFocus(Qt.TabFocusReason);
        compare(playPause.background.border.width, 3);
        mouseMove(playPause);
        compare(playPause.background.color.toString(), "#f8f800");
        compare(playPause.contentItem.color.toString(), "#010101");
        compare(findChild(playPause.background, "hover-feedback").opacity, 0);
        keyClick(Qt.Key_L, Qt.ControlModifier);
        const queue = findChild(shell, "queue-list");
        tryVerify(function () {
            return queue.activeFocus && queue.currentItem !== null;
        });
        verifySelectedRow(queue.currentItem, "#f8f800", "#010101");
        const scrollBar = findChild(shell, "queue-scrollbar");
        verify(scrollBar);
        compare(scrollBar.contentItem.opacity, 1);
        compare(scrollBar.contentItem.color.toString(), "#fafafa");
        testScenarioDriver.setHighContrast(true, true); // qmllint disable unqualified
        compare(shell.color.toString(), "#fcfcfd");
        verifySelectedRow(queue.currentItem, "#1a217a", "#fcfcfd");
        verify(queue.activeFocus);
        keyClick(Qt.Key_Down);
        verifySelectedRow(queue.currentItem, "#1a217a", "#fcfcfd");
        keyClick(Qt.Key_Comma, Qt.ControlModifier);
        const panel = findChild(shell, "preferences-panel");
        tryVerify(function () {
            return panel.opened;
        });
        const startMinimized = findChild(shell, "start-minimized");
        verify(!startMinimized.enabled);
        compare(startMinimized.contentItem.color.toString(), "#5a5a62");
        mouseClick(findChild(shell, "preferences-appearance"));
        const note = findChild(shell, "preferences-high-contrast-note");
        verify(note && note.visible);
        compare(note.Accessible.id, "preferences-high-contrast-note");
        compare(note.text, "O alto contraste do sistema está ativo. O tema escolhido será usado quando ele for desativado.");
        const theme = findChild(shell, "theme-mode");
        const motion = findChild(shell, "motion-mode");
        testScenarioDriver.setHighContrast(true, false); // qmllint disable unqualified
        selectChoice(theme, 1);
        compare(testPlayerPresenter.themePreference, PlayerPresenter.LightTheme); // qmllint disable unqualified
        compare(shell.color.toString(), "#010102");
        testScenarioDriver.setHighContrast(true, true); // qmllint disable unqualified
        selectChoice(theme, 2);
        compare(testPlayerPresenter.themePreference, PlayerPresenter.DarkTheme); // qmllint disable unqualified
        compare(shell.color.toString(), "#fcfcfd");
        selectChoice(motion, 1);
        verify(testShellUiContract.reducedMotion); // qmllint disable unqualified
        compare(testShellUiContract.panelTransitionMs, 0); // qmllint disable unqualified
        verify(testShellUiContract.highContrast); // qmllint disable unqualified
        testScenarioDriver.setSystemAppearance(true, false); // qmllint disable unqualified
        compare(shell.color.toString(), "#fcfcfd");
        selectChoice(motion, 0);
        verify(!testShellUiContract.reducedMotion); // qmllint disable unqualified
        compare(testShellUiContract.panelTransitionMs, 160); // qmllint disable unqualified
        mouseClick(findChild(shell, "preferences-playback"));
        const seconds = findChild(shell, "skip-backward-seconds");
        seconds.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_A, Qt.ControlModifier);
        keyClick(Qt.Key_0);
        const error = findChild(shell, "skip-backward-seconds-error");
        verify(error.visible);
        compare(error.color.toString(), "#111115");
        compare(seconds.background.border.color.toString(), "#111115");
        verifyTextContrast(error.color, panel.background.color);
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !panel.visible;
        });
        compare(testPlayerPresenter.currentOccurrenceId, "queue-0017"); // qmllint disable unqualified
        compare(testPlayerPresenter.queueModel, modelBefore); // qmllint disable unqualified
        compare(testPlayerPresenter.positionMs, positionBefore); // qmllint disable unqualified
        compare(testPlayerPresenter.playbackState, PlayerPresenter.Paused); // qmllint disable unqualified
        const preferences = findChild(shell, "preferences");
        verify(preferences.activeFocus);
        testScenarioDriver.setHighContrast(false, false); // qmllint disable unqualified
        verify(!testShellUiContract.highContrast); // qmllint disable unqualified
        compare(shell.color.toString(), "#111318");
        compare(testPlayerPresenter.themePreference, PlayerPresenter.DarkTheme); // qmllint disable unqualified
        verify(preferences.activeFocus);
        compare(testPlayerPresenter.queueModel, modelBefore); // qmllint disable unqualified
        compare(testPlayerPresenter.positionMs, positionBefore); // qmllint disable unqualified
        testScenarioDriver.setSystemAppearance(true, true); // qmllint disable unqualified
        verify(testShellUiContract.reducedMotion); // qmllint disable unqualified
        keyClick(Qt.Key_L, Qt.ControlModifier);
        scrollBar.active = true;
        compare(scrollBar.contentItem.opacity, 0.75);
        scrollBar.active = false;
        compare(scrollBar.contentItem.opacity, 0);
    }
}
