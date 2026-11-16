import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "AccessibleInterfaceTree"
    when: windowShown

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testLocalPlayerPresenter // qmllint disable unqualified
        }
    }

    property var shell

    function snapshot(item) {
        const result = testScenarioDriver.accessibleSnapshot(item); // qmllint disable unqualified
        verify(result.valid, item ? item.objectName : "null item");
        verify(result.parentValid, item.objectName + " must belong to the native accessibility tree");
        return result;
    }

    function verifyControl(item, role, name, focusable) {
        const result = snapshot(item);
        compare(result.role, role);
        compare(result.name, name);
        compare(result.focusable, focusable);
        verify(!result.name.includes("queue-"));
        verify(!result.name.includes("invalid_media"));
        return result;
    }

    function init() {
        testScenarioDriver.resetLocalScenario(); // qmllint disable unqualified
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.requestActivate();
        tryVerify(function () {
            return shell.active;
        });
    }

    function cleanup() {
        if (shell)
            shell.destroy();
        shell = null;
        testScenarioDriver.resetLocalScenario(); // qmllint disable unqualified
    }

    function openLocalTrack() {
        const driver = testScenarioDriver; // qmllint disable unqualified
        const presenter = testLocalPlayerPresenter; // qmllint disable unqualified
        driver.enqueueLocalSelection(["C:/Música/Controle acessível.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
    }

    function test_accessibleProgressActionCommitsWithoutPointerRelease_data() {
        return [
            {
                tag: "increase",
                action: "increase",
                target: 21000
            },
            {
                tag: "decrease",
                action: "decrease",
                target: 19000
            },
            {
                tag: "range-value",
                action: "set",
                target: 42000
            }
        ];
    }

    function test_accessibleProgressActionCommitsWithoutPointerRelease(data) {
        const driver = testScenarioDriver; // qmllint disable unqualified
        const presenter = testLocalPlayerPresenter; // qmllint disable unqualified
        openLocalTrack();
        driver.publishLocalPosition(20000);
        tryCompare(presenter, "confirmedPositionMs", 20000);
        const progress = findChild(shell, "progress");
        const positionBefore = presenter.confirmedPositionMs;
        const seeksBefore = driver.localSeekCount();
        verify(!progress.pressed);
        if (data.action === "set")
            verify(driver.accessibleSetValue(progress, data.target));
        else
            verify(driver.accessibleAdjust(progress, data.action === "increase"));
        compare(driver.localSeekCount(), seeksBefore + 1);
        compare(driver.localSeekTarget(), data.target);
        verify(presenter.seekPending);
        compare(presenter.confirmedPositionMs, positionBefore);
        compare(presenter.playbackState, PlayerPresenter.Playing);
        verify(driver.accessibleSetValue(progress, 60000));
        compare(driver.localSeekCount(), seeksBefore + 1);
        compare(progress.value, presenter.positionMs);
        driver.publishLocalPosition(data.target);
        tryCompare(presenter, "seekPending", false);
        verify(!presenter.seekPreviewActive);
        compare(progress.value, data.target);
        compare(driver.localSeekCount(), seeksBefore + 1);
    }

    function test_accessibleVolumeActionChangesPlayerVolume() {
        const driver = testScenarioDriver; // qmllint disable unqualified
        const presenter = testLocalPlayerPresenter; // qmllint disable unqualified
        openLocalTrack();
        const volume = findChild(shell, "volume");
        verify(presenter.setVolume(0.5));
        verify(driver.accessibleAdjust(volume, true));
        compare(presenter.volume, 0.51);
        verify(driver.accessibleAdjust(volume, false));
        compare(presenter.volume, 0.5);
        verify(driver.accessibleSetValue(volume, 75));
        compare(presenter.volume, 0.75);
        compare(driver.localSeekCount(), 0);
    }

    function test_presenterUpdatesDoNotSeekAndReconcileAccessibleValues() {
        const driver = testScenarioDriver; // qmllint disable unqualified
        const presenter = testLocalPlayerPresenter; // qmllint disable unqualified
        openLocalTrack();
        const progress = findChild(shell, "progress");
        driver.publishLocalPosition(120000);
        tryCompare(progress, "value", 120000);
        driver.publishLocalPosition(5000, 10000);
        tryCompare(progress, "to", 10000);
        compare(progress.value, 5000);
        compare(driver.localSeekCount(), 0);
        verify(!presenter.seekPreviewActive && !presenter.seekPending);
        driver.publishLocalPaused();
        tryCompare(presenter, "playbackState", PlayerPresenter.Paused);
        driver.rejectNextLocalSeek();
        verify(driver.accessibleSetValue(progress, 9000));
        compare(driver.localSeekCount(), 1);
        verify(!presenter.seekPreviewActive && !presenter.seekPending);
        compare(progress.value, presenter.confirmedPositionMs);
        compare(presenter.playbackState, PlayerPresenter.Paused);
        driver.publishLocalUnseekablePlaying();
        tryCompare(progress, "enabled", false);
        verify(driver.accessibleSetValue(progress, 15000));
        verify(driver.accessibleAdjust(progress, true));
        compare(driver.localSeekCount(), 1);
        compare(progress.value, presenter.positionMs);
    }

    function test_accessibleSeekClampsAndKeepsPlaybackPaused() {
        const driver = testScenarioDriver; // qmllint disable unqualified
        const presenter = testLocalPlayerPresenter; // qmllint disable unqualified
        openLocalTrack();
        driver.publishLocalPaused();
        tryCompare(presenter, "playbackState", PlayerPresenter.Paused);
        driver.publishLocalPosition(9500, 10000);
        tryCompare(presenter, "confirmedPositionMs", 9500);
        const progress = findChild(shell, "progress");
        const playsBefore = driver.localPlayCount();
        verify(driver.accessibleAdjust(progress, true));
        compare(driver.localSeekTarget(), 10000);
        compare(driver.localSeekCount(), 1);
        driver.publishLocalPosition(10000, 10000);
        tryCompare(presenter, "seekPending", false);
        verify(driver.accessibleSetValue(progress, -1000));
        compare(driver.localSeekTarget(), 0);
        compare(driver.localSeekCount(), 2);
        driver.publishLocalPosition(0, 10000);
        tryCompare(presenter, "seekPending", false);
        compare(presenter.playbackState, PlayerPresenter.Paused);
        compare(driver.localPlayCount(), playsBefore);
    }

    function test_accessibleVolumeClampsAndDisabledInputsAreInert() {
        const driver = testScenarioDriver; // qmllint disable unqualified
        const presenter = testLocalPlayerPresenter; // qmllint disable unqualified
        openLocalTrack();
        const volume = findChild(shell, "volume");
        verify(driver.accessibleSetValue(volume, 110));
        compare(presenter.volume, 1);
        verify(driver.accessibleAdjust(volume, true));
        compare(presenter.volume, 1);
        verify(driver.accessibleSetValue(volume, -10));
        compare(presenter.volume, 0);
        verify(driver.accessibleAdjust(volume, false));
        compare(presenter.volume, 0);
        verify(presenter.setVolume(0.8));
        compare(volume.value, 80);
        verify(presenter.clearQueue());
        tryCompare(volume, "enabled", false);
        verify(driver.accessibleSetValue(volume, 20));
        verify(driver.accessibleAdjust(volume, false));
        compare(presenter.volume, 0.8);
        compare(volume.value, 80);
    }

    function test_publicControlsExistInTheNativeAccessibleTree() {
        verifyControl(findChild(shell, "open-audio"), Accessible.Button, "Abrir áudio", true);
        verifyControl(findChild(shell, "preferences"), Accessible.Button, "Configurações", true);

        testScenarioDriver.enqueueLocalSelection(["C:/Música/Árvore acessível 日本語.mp3"]); // qmllint disable unqualified
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Loading); // qmllint disable unqualified
        testScenarioDriver.publishLocalPlaying(); // qmllint disable unqualified
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Playing); // qmllint disable unqualified

        const progress = findChild(shell, "progress");
        progress.forceActiveFocus(Qt.TabFocusReason);
        tryVerify(function () {
            return progress.activeFocus;
        });
        const focusedProgress = verifyControl(progress, Accessible.Slider, "Posição da faixa", true);
        compare(focusedProgress.description, "←/→: saltos configurados; ↑/↓: volume; Page Up/Page Down: 10 segundos; Home/End: início/fim.");
        verify(focusedProgress.focused);
        verify(!focusedProgress.disabled);
        verifyControl(findChild(shell, "volume"), Accessible.Slider, "Volume", true);

        findChild(shell, "play-pause").forceActiveFocus(Qt.TabFocusReason);
        testScenarioDriver.publishLocalUnseekablePlaying(); // qmllint disable unqualified
        tryCompare(testLocalPlayerPresenter, "seekable", false); // qmllint disable unqualified
        const disabledProgress = verifyControl(progress, Accessible.Slider, "Posição da faixa", false);
        compare(disabledProgress.description, "Esta fonte não permite avançar ou voltar.");
        verify(!disabledProgress.focused);
        verify(disabledProgress.disabled);
        testScenarioDriver.publishLocalPlaying(); // qmllint disable unqualified
        tryCompare(testLocalPlayerPresenter, "seekable", true); // qmllint disable unqualified

        keyClick(Qt.Key_L, Qt.ControlModifier);
        const list = findChild(shell, "queue-list");
        tryVerify(function () {
            return list.visible && list.currentItem !== null;
        });
        const listSnapshot = verifyControl(list, Accessible.List, "Fila", true);
        verify(listSnapshot.childCount > 0);

        const row = list.currentItem;
        const rowSnapshot = verifyControl(row, Accessible.ListItem, "Árvore acessível 日本語. Não informado. posição 1 de 1.", true);
        compare(rowSnapshot.description, "");
        compare(rowSnapshot.parentRole, Accessible.List);
    }
}
