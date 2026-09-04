import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "FileLocation"
    when: windowShown
    readonly property PlayerPresenter presenter: testLocalPlayerPresenter // qmllint disable unqualified
    readonly property var driver: testScenarioDriver // qmllint disable unqualified
    property var shell

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testCase.presenter
        }
    }

    function init() {
        driver.resetLocalScenario();
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
        driver.resetLocalScenario();
    }

    function openMissing(paths) {
        driver.enqueueLocalSelection(paths);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalSourceUnavailable();
    }

    function test_currentErrorLocation_data() {
        return [
            {
                tag: "keyboard",
                accessible: false
            },
            {
                tag: "accessible",
                accessible: true
            }
        ];
    }

    function test_currentErrorLocation(data) {
        openMissing(["C:/Música/Arquivo movido 日本語.mp3"]);
        tryCompare(presenter, "playbackState", PlayerPresenter.Error);
        const occurrenceId = presenter.currentOccurrenceId;
        const locate = findChild(shell, "error-locate-occurrence");
        verify(locate && locate.visible && locate.enabled);
        compare(locate.Accessible.name, "Localizar arquivo…");
        compare(locate.Accessible.role, Accessible.Button);
        verify(locate.Accessible.description.includes("pausada"));
        verify(findChild(shell, "error-open-audio").enabled);
        verify(findChild(shell, "error-retry-occurrence").enabled);
        locate.forceActiveFocus(Qt.TabFocusReason);
        if (data.accessible)
            locate.Accessible.pressAction();
        else
            keyClick(Qt.Key_Return);
        tryCompare(presenter, "locatingOccurrenceId", occurrenceId);
        compare(driver.localLocateCount(), 1);
        compare(driver.localLocateSource(0), "C:/Música/Arquivo movido 日本語.mp3");
        verify(!locate.enabled);
        locate.Accessible.pressAction();
        compare(driver.localLocateCount(), 1);
        compare(driver.localLoadCount(), 1);

        driver.completeLocalLocation("C:/Música/Novo local ☕.flac");
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        compare(presenter.currentOccurrenceId, occurrenceId);
        compare(driver.localLoadCount(), 2);
        compare(driver.localLoadedPath(1), "C:/Música/Novo local ☕.flac");
        driver.publishLocalReady();
        tryVerify(function () {
            return driver.localPauseCount() === 1;
        });
        compare(driver.localPlayCount(), 0);
        driver.publishLocalPaused();
        tryCompare(presenter, "playbackState", PlayerPresenter.Paused);
        tryVerify(function () {
            return findChild(shell, "open-another-audio").activeFocus;
        });
    }

    function test_cancelAndFailureKeepContextAndRestoreTheActionFocus() {
        openMissing(["C:/Música/Arquivo movido.mp3"]);
        tryCompare(presenter, "playbackState", PlayerPresenter.Error);
        const occurrenceId = presenter.currentOccurrenceId;
        const locate = findChild(shell, "error-locate-occurrence");
        verify(locate && locate.enabled);
        locate.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        tryCompare(presenter, "locatingOccurrenceId", occurrenceId);
        driver.cancelLocalLocation();
        tryCompare(presenter, "locatingOccurrenceId", "");
        tryVerify(function () {
            return locate.activeFocus && locate.enabled;
        });
        compare(driver.localLoadCount(), 1);
        compare(presenter.currentOccurrenceId, occurrenceId);
        compare(presenter.fileLocationErrorOccurrenceId, "");
        keyClick(Qt.Key_Return);
        tryCompare(presenter, "locatingOccurrenceId", occurrenceId);
        driver.failLocalLocation();
        tryCompare(presenter, "fileLocationErrorOccurrenceId", occurrenceId);
        const feedback = findChild(shell, "error-location-feedback");
        verify(feedback && feedback.visible);
        compare(feedback.text, "Não foi possível localizar o arquivo. Tente novamente.");
        tryVerify(function () {
            return locate.activeFocus && locate.enabled;
        });
        compare(driver.localLoadCount(), 1);
        compare(presenter.playbackState, PlayerPresenter.Error);

        findChild(shell, "error-retry-occurrence").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalReady();
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        shell.setQueueOpen(true);
        const queue = findChild(shell, "queue-list");
        tryVerify(function () {
            return queue.visible && queue.currentItem !== null && queue.currentItem.available;
        });
        verify(!findChild(queue.currentItem, "queue-row-status").visible);
        verify(!queue.currentItem.Accessible.name.includes("Não foi possível localizar"));
    }

    function test_nonCurrentLocationPreservesPlaybackAndQueueSelection_data() {
        return [
            {
                tag: "keyboard",
                accessible: false
            },
            {
                tag: "accessible",
                accessible: true
            }
        ];
    }

    function test_nonCurrentLocationPreservesPlaybackAndQueueSelection(data) {
        openMissing(["C:/Música/Duplicada.mp3", "C:/Música/Duplicada.mp3"]);
        tryCompare(presenter, "currentQueueIndex", 1);
        driver.publishLocalReady();
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        const currentId = presenter.currentOccurrenceId;
        const playsBefore = driver.localPlayCount();
        shell.setQueueOpen(true);
        const queue = findChild(shell, "queue-list");
        tryVerify(function () {
            return queue.visible && queue.activeFocus && queue.count === 2;
        });
        queue.currentIndex = 0;
        queue.forceLayout();
        const missingId = queue.currentItem.occurrenceId;
        verify(missingId !== currentId);
        keyClick(Qt.Key_F10, Qt.ShiftModifier);
        const menu = findChild(shell, "queue-row-menu");
        tryVerify(function () {
            return menu.opened;
        });
        const locate = findChild(shell, "queue-menu-locate");
        verify(locate && locate.visible && locate.enabled);
        compare(locate.Accessible.name, "Localizar arquivo…");
        verify(locate.Accessible.description.includes("sem interromper"));
        if (data.accessible)
            locate.Accessible.pressAction();
        else {
            for (let attempt = 0; attempt < 8 && !locate.activeFocus; ++attempt)
                keyClick(Qt.Key_Down);
            verify(locate.activeFocus);
            keyClick(Qt.Key_Return);
        }
        tryCompare(presenter, "locatingOccurrenceId", missingId);
        verify(!menu.visible);
        driver.failLocalLocation();
        tryCompare(presenter, "fileLocationErrorOccurrenceId", missingId);
        const failureStatus = findChild(queue.currentItem, "queue-row-status");
        compare(failureStatus.text, "Não foi possível localizar o arquivo. Tente novamente.");
        verify(queue.currentItem.Accessible.name.includes(failureStatus.text));
        tryVerify(function () {
            return queue.activeFocus;
        });
        keyClick(Qt.Key_Menu);
        tryVerify(function () {
            return menu.opened;
        });
        locate.Accessible.pressAction();
        tryCompare(presenter, "locatingOccurrenceId", missingId);
        driver.completeLocalLocation("C:/Música/Relocalizada 日本語.mp3");
        tryCompare(presenter, "locatingOccurrenceId", "");
        compare(presenter.currentOccurrenceId, currentId);
        compare(presenter.playbackState, PlayerPresenter.Playing);
        compare(driver.localLoadCount(), 2);
        compare(driver.localPlayCount(), playsBefore);
        compare(driver.localPauseCount(), 0);
        compare(queue.count, 2);
        tryVerify(function () {
            return queue.activeFocus && queue.currentItem.occurrenceId === missingId;
        });
        verify(!queue.currentItem.available);
        const status = findChild(queue.currentItem, "queue-row-status");
        compare(status.text, "Novo arquivo selecionado. Tente reproduzir para verificar.");
        verify(queue.currentItem.Accessible.name.includes(status.text));
        verify(!queue.currentItem.Accessible.name.includes("source_relocated"));
        verify(queue.itemAtIndex(1).available);
    }

    function test_currentLocationActionsFitMinimumWindow() {
        shell.width = 720;
        shell.height = 520;
        openMissing(["C:/Música/Arquivo movido com nome muito longo para preservar contexto 日本語.mp3"]);
        tryCompare(presenter, "playbackState", PlayerPresenter.Error);
        const state = findChild(shell, "error-state");
        for (const name of ["error-open-audio", "error-retry-occurrence", "error-locate-occurrence", "track-details-error"]) {
            const action = findChild(state, name);
            verify(action && action.visible && action.width > 0 && action.height > 0);
            const position = action.mapToItem(shell.contentItem, 0, 0);
            verify(position.x >= 0 && position.y >= 0);
            verify(position.x + action.width <= shell.width + 0.5);
            verify(position.y + action.height <= shell.height + 0.5);
        }
    }
}
