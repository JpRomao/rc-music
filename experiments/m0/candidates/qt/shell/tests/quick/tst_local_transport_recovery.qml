import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "LocalTransportRecovery"
    when: windowShown
    readonly property var driver: testScenarioDriver // qmllint disable unqualified
    readonly property var presenter: testLocalPlayerPresenter // qmllint disable unqualified
    property var shell

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testLocalPlayerPresenter // qmllint disable unqualified
        }
    }

    function init() {
        driver.resetLocalScenario();
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.requestActivate();
        tryVerify(function () {
            return testCase.shell.active;
        });
        driver.enqueueLocalSelection(["C:/Música/Anterior.mp3", "C:/Música/Indisponível.mp3", "C:/Música/Próxima.mp3"]);
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
    }

    function failMiddleOccurrenceWithoutAutomaticAdvance() {
        verify(presenter.nextTrack());
        tryCompare(presenter, "currentQueueIndex", 1);
        const failedId = presenter.currentOccurrenceId;
        driver.publishLocalError();
        tryCompare(presenter, "currentQueueIndex", 2);
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        verify(presenter.retryQueueOccurrence(failedId));
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalError();
        tryCompare(presenter, "playbackState", PlayerPresenter.Error);
        compare(presenter.currentQueueIndex, 1);
        verify(presenter.canGoPrevious && presenter.canGoNext);
    }

    function test_navigationRecoversFromError_data() {
        return [
            {
                tag: "previous-button",
                action: "previous",
                shortcut: false,
                expectedIndex: 0
            },
            {
                tag: "next-button",
                action: "next",
                shortcut: false,
                expectedIndex: 2
            },
            {
                tag: "previous-shortcut",
                action: "previous",
                shortcut: true,
                expectedIndex: 0
            },
            {
                tag: "next-shortcut",
                action: "next",
                shortcut: true,
                expectedIndex: 2
            }
        ];
    }

    function test_navigationRecoversFromError(data) {
        failMiddleOccurrenceWithoutAutomaticAdvance();
        const playsBefore = driver.localPlayCount();
        const button = findChild(shell, data.action);
        if (!data.shortcut) {
            verify(button.enabled, "A valid queue destination remains actionable after an error");
            button.forceActiveFocus(Qt.TabFocusReason);
            verify(button.activeFocus);
            keyClick(Qt.Key_Return);
        } else {
            findChild(shell, "error-open-audio").forceActiveFocus(Qt.TabFocusReason);
            keyClick(data.action === "previous" ? Qt.Key_Left : Qt.Key_Right, Qt.ControlModifier);
        }
        tryCompare(presenter, "currentQueueIndex", data.expectedIndex);
        compare(driver.localLoadCount(), 5);
        driver.publishLocalReady();
        tryCompare(presenter, "playbackState", PlayerPresenter.Ready);
        // Retry was an explicit request to play; navigation preserves that intent.
        compare(driver.localPlayCount(), playsBefore + 1);
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
    }

    function test_errorKeepsPlaybackControlsDisabledAndModalBlocksNavigation() {
        failMiddleOccurrenceWithoutAutomaticAdvance();
        for (const name of ["play-pause", "progress", "skip-backward", "skip-forward", "mute", "volume"])
            verify(!findChild(shell, name).enabled, name + " must not become enabled with navigation");
        const before = driver.localLoadCount();
        findChild(shell, "error-open-audio").forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        keyClick(Qt.Key_Right);
        keyClick(Qt.Key_M);
        compare(presenter.playbackState, PlayerPresenter.Error);
        compare(driver.localLoadCount(), before);
        keyClick(Qt.Key_Comma, Qt.ControlModifier);
        const preferences = findChild(shell, "preferences-panel");
        tryVerify(function () {
            return preferences.visible;
        });
        keyClick(Qt.Key_Left, Qt.ControlModifier);
        keyClick(Qt.Key_Right, Qt.ControlModifier);
        compare(driver.localLoadCount(), before);
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !preferences.visible;
        });
    }

    function test_errorDoesNotStealFocusFromAnAvailablePreviousButton() {
        verify(presenter.nextTrack());
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        verify(presenter.nextTrack());
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        const previous = findChild(shell, "previous");
        previous.forceActiveFocus(Qt.TabFocusReason);
        verify(previous.activeFocus);
        driver.publishLocalError();
        tryCompare(presenter, "playbackState", PlayerPresenter.Error);
        wait(0);
        verify(previous.enabled && previous.activeFocus);
        verify(!findChild(shell, "next").enabled);
    }

    function test_rejectedDragRestoresPausedProgressAndAnnouncesOnce() {
        driver.publishLocalPaused();
        tryCompare(presenter, "playbackState", PlayerPresenter.Paused);
        const progress = findChild(shell, "progress");
        const originalPosition = progress.value;
        const occurrence = presenter.currentOccurrenceId;
        driver.clearAccessibleAnnouncements();
        driver.rejectNextLocalSeek();
        mousePress(progress, progress.width * 0.25, progress.height / 2);
        mouseMove(progress, progress.width * 0.7, progress.height / 2);
        verify(presenter.seekPreviewActive);
        verify(progress.value > originalPosition);
        mouseRelease(progress, progress.width * 0.7, progress.height / 2);
        tryCompare(progress, "value", originalPosition);
        verify(!presenter.seekPreviewActive && !presenter.seekPending);
        compare(presenter.playbackState, PlayerPresenter.Paused);
        compare(presenter.currentOccurrenceId, occurrence);
        compare(driver.localLoadCount(), 1);
        compare(driver.localSeekCount(), 1);
        compare(driver.accessibleAnnouncementCount(), 1);
        compare(driver.lastAccessibleAnnouncement(), "Não foi possível alterar a posição. A reprodução voltou para a posição anterior.");
        verify(progress.activeFocus && progress.enabled);
    }

    function test_errorRepairsDisabledPlayFocusWhilePreviousRemainsAvailable() {
        verify(presenter.nextTrack());
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        verify(presenter.nextTrack());
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        findChild(shell, "play-pause").forceActiveFocus(Qt.TabFocusReason);
        driver.publishLocalError();
        tryCompare(presenter, "playbackState", PlayerPresenter.Error);
        verify(findChild(shell, "previous").enabled);
        const errorAction = findChild(shell, "error-open-audio");
        tryVerify(function () {
            return errorAction.activeFocus;
        });
    }
}
