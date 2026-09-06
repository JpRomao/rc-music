import QtQuick
import QtQuick.Controls
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "DetailsScrollContrast"
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

    function luminance(color) {
        function linear(value) {
            return value <= 0.04045 ? value / 12.92 : Math.pow((value + 0.055) / 1.055, 2.4);
        }
        return 0.2126 * linear(color.r) + 0.7152 * linear(color.g) + 0.0722 * linear(color.b);
    }

    function verifyThumbContrast(bar, background) {
        const thumb = bar.contentItem;
        const alpha = thumb.color.a * thumb.opacity * bar.opacity;
        const effective = Qt.rgba(thumb.color.r * alpha + background.r * (1 - alpha), thumb.color.g * alpha + background.g * (1 - alpha), thumb.color.b * alpha + background.b * (1 - alpha), 1);
        const first = luminance(effective);
        const second = luminance(background);
        const ratio = (Math.max(first, second) + 0.05) / (Math.min(first, second) + 0.05);
        verify(ratio >= 3, "Active details thumb including opacity: " + ratio + " < 3");
    }

    function init() {
        driver.resetLocalScenario();
        driver.setHighContrast(false, false);
        testPlayerPresenter.setThemePreference(PlayerPresenter.SystemTheme); // qmllint disable unqualified
        testPlayerPresenter.setMotionPreference(PlayerPresenter.SystemMotion); // qmllint disable unqualified
        shell = null;
    }

    function cleanup() {
        if (shell) {
            const panel = findChild(shell, "track-details-panel");
            if (panel.visible) {
                panel.close();
                tryVerify(function () {
                    return !panel.visible;
                });
            }
            wait(0);
            shell.destroy();
        }
        shell = null;
        driver.resetLocalScenario();
        driver.setHighContrast(false, false);
        driver.setSystemAppearance(false, false);
    }

    function test_detailsScrollingKeepsItsVisibleThumbContrasting_data() {
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

    function test_detailsScrollingKeepsItsVisibleThumbContrasting(data) {
        driver.setSystemAppearance(data.dark, true);
        compare(contract.darkTheme, data.dark);
        shell = createTemporaryObject(shellComponent, null, {
            width: 720,
            height: 520
        });
        verify(shell);
        shell.requestActivate();
        tryVerify(function () {
            return testCase.shell.active;
        });
        driver.enqueueLocalSelection(["C:/Contrast/Details.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishCompleteLocalMetadata();
        tryCompare(presenter, "currentTitle", "Título controlado");
        mouseClick(findChild(shell, "track-details-playing"));
        const panel = findChild(shell, "track-details-panel");
        const scroll = findChild(shell, "track-details-scroll");
        tryVerify(function () {
            return panel.opened;
        });
        verify(waitForPolish(shell));
        const bar = scroll.ScrollBar.vertical;
        verify(bar && bar.enabled && bar.interactive);
        compare(scroll.ScrollBar.horizontal.policy, ScrollBar.AlwaysOff);
        tryVerify(function () {
            return bar.size > 0 && bar.size < 1;
        });
        bar.policy = ScrollBar.AlwaysOn;
        tryVerify(function () {
            return bar.contentItem.opacity > 0;
        });
        wait(0);

        const initialPosition = bar.position;
        mouseWheel(scroll, scroll.width / 2, 8, 0, -120);
        tryVerify(function () {
            return bar.position > initialPosition;
        }, 1000, "Wheel should move details from " + initialPosition + " with viewport " + scroll.height + " and content " + scroll.contentItem.contentHeight);
        tryCompare(scroll.contentItem, "moving", false);
        const beforeDrag = bar.position;
        const thumb = bar.contentItem;
        mouseDrag(thumb, thumb.width / 2, thumb.height / 2, 0, Math.min(30, bar.height / 5));
        tryVerify(function () {
            return bar.position > beforeDrag;
        });
        verifyThumbContrast(bar, panel.background.color);

        driver.setHighContrast(true, data.dark);
        compare(contract.highContrast, true);
        compare(bar.contentItem.color.toString(), contract.textPrimaryColor.toLowerCase());
        compare(bar.contentItem.opacity, 1);
        verifyThumbContrast(bar, panel.background.color);
        compare(scroll.ScrollBar.horizontal.policy, ScrollBar.AlwaysOff);
    }
}
