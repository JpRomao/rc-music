import QtQuick
import QtQuick.Controls
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "VolumeToolTip"
    when: windowShown

    readonly property var driver: testScenarioDriver // qmllint disable unqualified
    readonly property PlayerPresenter presenter: testLocalPlayerPresenter // qmllint disable unqualified
    property var shell
    property var volume
    property real originalVolume: 0.8
    property bool originalMuted: false

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testCase.presenter
        }
    }

    function init() {
        originalVolume = presenter.volume;
        originalMuted = presenter.muted;
        driver.resetLocalScenario();
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.requestActivate();
        tryVerify(function () {
            return testCase.shell.active;
        });
        driver.enqueueLocalSelection(["C:/Música/Volume.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        volume = findChild(shell, "volume");
        verify(volume);
        presenter.setVolume(0.8);
        compare(presenter.volume, 0.8);
        mouseMove(shell.contentItem, 2, 2);
    }

    function cleanup() {
        if (shell)
            shell.destroy();
        shell = null;
        volume = null;
        wait(0);
        presenter.setVolume(originalVolume);
        if (presenter.muted !== originalMuted)
            presenter.toggleMute();
        driver.resetLocalScenario();
    }

    function hoverVolume() {
        mouseMove(volume, volume.width / 2, volume.height / 2);
        tryCompare(volume.ToolTip, "visible", true);
        tryCompare(volume.ToolTip.toolTip, "visible", true);
    }

    function test_hoverShowsCurrentPercentageAndLeavingHidesItWithoutChangingAudio() {
        const previousVolume = presenter.volume;
        const previousMuted = presenter.muted;
        compare(volume.ToolTip.visible, false);
        hoverVolume();
        compare(volume.ToolTip.text, "80%");
        compare(volume.ToolTip.toolTip.text, "80%");
        mouseMove(shell.contentItem, 2, 2);
        tryCompare(volume.ToolTip, "visible", false);
        tryCompare(volume.ToolTip.toolTip, "visible", false);
        compare(presenter.volume, previousVolume);
        compare(presenter.muted, previousMuted);
    }

    function test_hoverTracksActualVolume_data() {
        return [
            {
                tag: "zero",
                value: 0,
                label: "0%"
            },
            {
                tag: "rounded",
                value: 0.375,
                label: "38%"
            },
            {
                tag: "full",
                value: 1,
                label: "100%"
            }
        ];
    }

    function test_hoverTracksActualVolume(data) {
        hoverVolume();
        verify(presenter.setVolume(data.value));
        tryCompare(volume.ToolTip, "text", data.label);
        tryCompare(volume.ToolTip.toolTip, "text", data.label);
        compare(volume.ToolTip.visible, true);
    }

    function test_keyboardVolumeChangesUpdateTheVisiblePercentage() {
        volume.forceActiveFocus(Qt.TabFocusReason);
        hoverVolume();
        keyClick(Qt.Key_Up);
        tryCompare(presenter, "volume", 0.81);
        tryCompare(volume.ToolTip, "text", "81%");
        keyClick(Qt.Key_Down);
        tryCompare(presenter, "volume", 0.8);
        tryCompare(volume.ToolTip, "text", "80%");
    }

    function test_dragKeepsThePercentageVisibleOutsideTheSliderUntilRelease() {
        verify(presenter.setVolume(0.5));
        mousePress(volume, volume.width / 2, volume.height / 2, Qt.LeftButton);
        tryCompare(volume, "pressed", true);
        mouseMove(volume, volume.width * 0.75, volume.height / 2, 20);
        tryCompare(volume.ToolTip, "visible", true);
        compare(volume.ToolTip.text, Math.round(presenter.volume * 100) + "%");
        mouseMove(volume, volume.width + 35, volume.height / 2, 20);
        compare(volume.pressed, true);
        tryCompare(volume, "hovered", false);
        compare(volume.ToolTip.visible, true);
        compare(volume.ToolTip.toolTip.visible, true);
        compare(volume.ToolTip.text, Math.round(presenter.volume * 100) + "%");
        mouseRelease(volume, volume.width + 35, volume.height / 2, Qt.LeftButton);
        tryCompare(volume, "pressed", false);
        tryCompare(volume.ToolTip, "visible", false);
        tryCompare(volume.ToolTip.toolTip, "visible", false);
    }
}
