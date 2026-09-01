pragma ComponentBehavior: Bound

import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "AccessibilityAnnouncements"
    when: windowShown

    readonly property PlayerPresenter localPresenter: testLocalPlayerPresenter // qmllint disable unqualified
    readonly property PlayerPresenter fixturePresenter: testPlayerPresenter // qmllint disable unqualified
    readonly property var driver: testScenarioDriver // qmllint disable unqualified
    property var shell

    Component {
        id: localShellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testCase.localPresenter
        }
    }
    Component {
        id: fixtureShellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testCase.fixturePresenter
        }
    }

    function createLocalShell() {
        shell = createTemporaryObject(localShellComponent, null);
        verify(shell);
        driver.clearAccessibleAnnouncements();
    }

    function createFixtureShell() {
        if (!fixturePresenter.queueLoaded)
            verify(fixturePresenter.loadFrozenQueue());
        if (fixturePresenter.playbackState === PlayerPresenter.Error)
            verify(fixturePresenter.skipUnavailable());
        if (fixturePresenter.playbackState === PlayerPresenter.Paused || fixturePresenter.playbackState === PlayerPresenter.Ready)
            verify(fixturePresenter.togglePlayback());
        compare(fixturePresenter.playbackState, PlayerPresenter.Playing);
        shell = createTemporaryObject(fixtureShellComponent, null);
        verify(shell);
        wait(0);
        driver.clearAccessibleAnnouncements();
    }

    function openLocalTrack(path) {
        driver.enqueueLocalSelection([path]);
        const openAudio = findChild(shell, "open-audio");
        verify(openAudio && openAudio.visible && openAudio.enabled);
        openAudio.Accessible.pressAction();
        tryCompare(localPresenter, "playbackState", PlayerPresenter.Loading);
    }

    function startSourceImport(path) {
        const centerX = shell.width / 2;
        const centerY = shell.height / 2;
        compare(driver.beginUrlDrag(shell, centerX, centerY, [path]), Qt.CopyAction);
        compare(driver.dropDrag(shell, centerX, centerY), Qt.CopyAction);
        compare(localPresenter.sourceImportState, PlayerPresenter.Discovering);
        verify(localPresenter.sourceImportActive);
    }

    function completeEmptySourceImport() {
        driver.enqueueSourceCompletion(0, 0, 0);
        driver.pollSourceImport();
        compare(localPresenter.sourceImportState, PlayerPresenter.Completed);
    }

    function init() {
        driver.resetLocalScenario();
        shell = null;
    }

    function cleanup() {
        if (shell)
            shell.destroy();
        shell = null;
        driver.resetLocalScenario();
    }

    function test_loadingAndCurrentTrackAreAnnouncedOnceWithoutPlaybackTickSpam() {
        createLocalShell();
        openLocalTrack("C:/Música/Anúncio seguro 日本語.flac");

        tryVerify(function () {
            return testCase.driver.accessibleAnnouncementCount() === 1;
        });
        verify(driver.lastAccessibleAnnouncement().includes("Carregando áudio"));
        verify(!driver.lastAccessibleAnnouncement().includes("C:/"));

        driver.clearAccessibleAnnouncements();
        driver.publishLocalReady();
        tryCompare(localPresenter, "playbackState", PlayerPresenter.Ready);
        tryVerify(function () {
            return testCase.driver.accessibleAnnouncementCount() === 1;
        });
        verify(driver.lastAccessibleAnnouncement().includes("Faixa carregada"));
        verify(driver.lastAccessibleAnnouncement().includes("Anúncio seguro 日本語"));
        verify(!driver.lastAccessibleAnnouncement().includes("C:/"));

        driver.clearAccessibleAnnouncements();
        driver.publishLocalPlaying();
        tryCompare(localPresenter, "playbackState", PlayerPresenter.Playing);
        wait(450);
        compare(driver.accessibleAnnouncementCount(), 0);
    }

    function test_terminalErrorIsAssertiveSafeAndActionable() {
        createLocalShell();
        openLocalTrack("C:/Música/Segredo local.mp3");
        driver.clearAccessibleAnnouncements();

        driver.publishLocalError();
        tryCompare(localPresenter, "playbackState", PlayerPresenter.Error);
        tryVerify(function () {
            return testCase.driver.accessibleAnnouncementCount() === 1;
        });
        const announcement = driver.lastAccessibleAnnouncement();
        verify(announcement.includes("Não foi possível reproduzir esta faixa"));
        verify(announcement.includes("Abrir outro áudio"));
        verify(!announcement.includes("invalid_media"));
        verify(!announcement.includes("C:/"));
        compare(driver.lastAccessibleAnnouncementPoliteness(), Accessible.Assertive);

        driver.publishLocalError();
        wait(0);
        compare(driver.accessibleAnnouncementCount(), 1);
    }

    function test_fixtureTerminalErrorAnnouncesTheVisibleRecoveryAction() {
        createFixtureShell();

        verify(driver.enterUnavailable());
        tryCompare(fixturePresenter, "playbackState", PlayerPresenter.Error);
        tryVerify(function () {
            return testCase.driver.accessibleAnnouncementCount() === 1;
        });
        const announcement = driver.lastAccessibleAnnouncement();
        verify(announcement.includes(testShellUiContract.errorTitle)); // qmllint disable unqualified
        verify(announcement.includes(testShellUiContract.errorBody)); // qmllint disable unqualified
        verify(announcement.includes(testShellUiContract.actionSkip)); // qmllint disable unqualified
        verify(!announcement.includes("Abrir outro áudio"));
        compare(driver.lastAccessibleAnnouncementPoliteness(), Accessible.Assertive);
    }

    function test_seekRejectionIsAnnouncedOnceAndTimerTicksStaySilent() {
        createFixtureShell();
        const target = Math.min(fixturePresenter.durationMs, fixturePresenter.confirmedPositionMs + 1000);
        verify(target !== fixturePresenter.confirmedPositionMs);
        driver.rejectNextFixtureSeek();
        verify(fixturePresenter.beginSeekPreview(target));
        verify(fixturePresenter.commitSeekPreview());
        tryVerify(function () {
            return !testCase.fixturePresenter.seekPending;
        });
        tryVerify(function () {
            return testCase.driver.accessibleAnnouncementCount() === 1;
        });
        verify(driver.lastAccessibleAnnouncement().includes("posição anterior"));
        compare(driver.lastAccessibleAnnouncementPoliteness(), Accessible.Assertive);

        driver.clearAccessibleAnnouncements();
        wait(450);
        compare(driver.accessibleAnnouncementCount(), 0);
    }

    function test_terminalErrorWaitsForTheActiveImportSummaryThenRemainsActionable() {
        createLocalShell();
        openLocalTrack("C:/Música/Erro durante importação.mp3");
        driver.publishLocalReady();
        tryCompare(localPresenter, "playbackState", PlayerPresenter.Ready);
        startSourceImport("C:/Música/Lote em descoberta");
        driver.clearAccessibleAnnouncements();

        driver.publishLocalError();
        tryCompare(localPresenter, "playbackState", PlayerPresenter.Error);
        wait(0);
        compare(driver.accessibleAnnouncementCount(), 0);

        completeEmptySourceImport();
        const importStatus = findChild(shell, "source-import-status");
        compare(driver.accessibleAnnouncementCount(), 1);
        compare(driver.lastAccessibleAnnouncement(), importStatus.accessibilityAnnouncement);
        tryVerify(function () {
            return testCase.driver.accessibleAnnouncementCount() === 2;
        });
        verify(driver.lastAccessibleAnnouncement().includes("Não foi possível reproduzir esta faixa"));
        verify(driver.lastAccessibleAnnouncement().includes("Abrir outro áudio"));
        compare(driver.lastAccessibleAnnouncementPoliteness(), Accessible.Assertive);
        wait(0);
        compare(driver.accessibleAnnouncementCount(), 2);
    }

    function test_seekRejectionDoesNotCompeteWithAnActiveImportOrBecomeStale() {
        createLocalShell();
        openLocalTrack("C:/Música/Seek durante importação.mp3");
        driver.publishLocalReady();
        tryCompare(localPresenter, "playbackState", PlayerPresenter.Ready);
        startSourceImport("C:/Música/Outro lote");
        driver.clearAccessibleAnnouncements();

        localPresenter.seekRejected();
        wait(0);
        compare(driver.accessibleAnnouncementCount(), 0);

        completeEmptySourceImport();
        compare(driver.accessibleAnnouncementCount(), 1);
        verify(!driver.lastAccessibleAnnouncement().includes("posição anterior"));
        wait(0);
        compare(driver.accessibleAnnouncementCount(), 1);
    }
}
