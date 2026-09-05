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
        driver.deferLocalAudioSelection(true);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(localPresenter, "selectingAudioFiles", true);
        tryVerify(function () {
            return testCase.driver.accessibleAnnouncementCount() === 1;
        });
        compare(driver.lastAccessibleAnnouncement(), "Aguardando seleção de arquivos…");
        wait(250);
        compare(driver.accessibleAnnouncementCount(), 1);
        driver.clearAccessibleAnnouncements();
        driver.completeLocalAudioSelection(["C:/Música/Anúncio seguro 日本語.flac"]);
        tryCompare(localPresenter, "playbackState", PlayerPresenter.Loading);

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

    function test_automaticallySkippedFileIsAnnouncedBeforeTheNextTrack_data() {
        return [
            {
                tag: "invalid",
                missing: false,
                reason: "corrompido"
            },
            {
                tag: "missing",
                missing: true,
                reason: "não está mais disponível"
            },
            {
                tag: "literal-placeholder-in-title",
                missing: false,
                reason: "corrompido",
                title: "Falhou %2 日本"
            }
        ];
    }

    function test_automaticallySkippedFileIsAnnouncedBeforeTheNextTrack(data) {
        createLocalShell();
        const title = data.title || "Falhou 日本";
        driver.enqueueLocalSelection(["C:/Biblioteca privada/" + title + ".mp3", "C:/Biblioteca privada/Seguinte.flac"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(localPresenter, "currentQueueIndex", 0);
        tryCompare(localPresenter, "playbackState", PlayerPresenter.Loading);
        const queue = findChild(shell, "queue-panel");
        if (queue.visible)
            localPresenter.toggleQueue();
        verify(!queue.visible);
        driver.clearAccessibleAnnouncements();

        if (data.missing)
            driver.publishLocalSourceUnavailable();
        else
            driver.publishLocalError();
        tryCompare(localPresenter, "currentQueueIndex", 1);
        compare(localPresenter.playbackState, PlayerPresenter.Loading);
        compare(driver.localLoadCount(), 2);
        compare(driver.accessibleAnnouncementCount(), 1);
        const announcement = driver.lastAccessibleAnnouncement();
        verify(announcement.includes("Faixa pulada: " + title), announcement);
        verify(announcement.includes(data.reason), announcement);
        verify(announcement.includes("próxima faixa"), announcement);
        verify(announcement.includes("detalhes na fila"), announcement);
        verify(!announcement.includes("C:/") && !announcement.includes("Biblioteca privada"));
        verify(!announcement.includes("invalid_media") && !announcement.includes("source_unavailable"));
        compare(driver.lastAccessibleAnnouncementPoliteness(), Accessible.Assertive);
        verify(!queue.visible);

        driver.clearAccessibleAnnouncements();
        driver.publishLocalReady();
        tryCompare(localPresenter, "playbackState", PlayerPresenter.Ready);
        compare(driver.accessibleAnnouncementCount(), 1);
        verify(driver.lastAccessibleAnnouncement().includes("Faixa carregada: Seguinte"));
        driver.clearAccessibleAnnouncements();
        driver.publishLocalPlaying();
        tryCompare(localPresenter, "playbackState", PlayerPresenter.Playing);
        wait(250);
        compare(driver.accessibleAnnouncementCount(), 0);
    }

    function test_duplicateFilesAreAnnouncedForEachSkippedOccurrence() {
        createLocalShell();
        const duplicate = "C:/Música/Duplicada.mp3";
        driver.enqueueLocalSelection([duplicate, duplicate, "C:/Música/Válida.flac"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(localPresenter, "playbackState", PlayerPresenter.Loading);
        driver.clearAccessibleAnnouncements();
        driver.publishLocalError();
        tryCompare(localPresenter, "currentQueueIndex", 1);
        verify(driver.lastAccessibleAnnouncement().includes("Faixa pulada: Duplicada"));
        compare(driver.accessibleAnnouncementCount(), 1);
        driver.publishLocalError();
        tryCompare(localPresenter, "currentQueueIndex", 2);
        verify(driver.lastAccessibleAnnouncement().includes("Faixa pulada: Duplicada"));
        compare(driver.accessibleAnnouncementCount(), 2);
    }

    function test_skippedFilesDuringImportHaveOneDeferredSummary() {
        createLocalShell();
        driver.enqueueLocalSelection(["C:/Música/Falha A.mp3", "C:/Música/Falha B.mp3", "C:/Música/Válida.flac"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(localPresenter, "playbackState", PlayerPresenter.Loading);
        startSourceImport("C:/Música/Outro lote ainda sendo descoberto");
        driver.clearAccessibleAnnouncements();

        driver.publishLocalError();
        tryCompare(localPresenter, "currentQueueIndex", 1);
        driver.publishLocalError();
        tryCompare(localPresenter, "currentQueueIndex", 2);
        driver.publishLocalReady();
        tryCompare(localPresenter, "playbackState", PlayerPresenter.Ready);
        compare(driver.accessibleAnnouncementCount(), 0);

        completeEmptySourceImport();
        compare(driver.accessibleAnnouncementCount(), 1);
        tryVerify(function () {
            return testCase.driver.accessibleAnnouncementCount() === 2;
        });
        verify(driver.lastAccessibleAnnouncement().includes("faixas foram puladas"));
        verify(driver.lastAccessibleAnnouncement().includes("detalhes na fila"));
        wait(250);
        compare(driver.accessibleAnnouncementCount(), 2);
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

    function test_outputErrorAnnouncesThePausedRecoveryActionWithoutBlamingTheFile_data() {
        return [
            {
                tag: "unavailable",
                changed: false
            },
            {
                tag: "changed",
                changed: true
            }
        ];
    }

    function test_outputErrorAnnouncesThePausedRecoveryActionWithoutBlamingTheFile(data) {
        createLocalShell();
        openLocalTrack("C:/Música/Dispositivo interrompido.flac");
        driver.publishLocalReady();
        tryCompare(localPresenter, "playbackState", PlayerPresenter.Ready);
        driver.clearAccessibleAnnouncements();

        driver.publishLocalOutputError(data.changed);
        tryCompare(localPresenter, "playbackState", PlayerPresenter.Error);
        tryVerify(function () {
            return testCase.driver.accessibleAnnouncementCount() === 1;
        });
        const announcement = driver.lastAccessibleAnnouncement();
        verify(announcement.includes(data.changed ? "A saída de áudio mudou" : "Saída de áudio indisponível"));
        verify(announcement.includes("pausada"));
        verify(announcement.includes("Tentar novamente"));
        verify(!announcement.includes("Arquivo não encontrado"));
        verify(!announcement.includes("audio_output"));
        verify(!announcement.includes("C:/"));
        compare(driver.lastAccessibleAnnouncementPoliteness(), Accessible.Assertive);
        driver.publishLocalOutputError(data.changed);
        wait(0);
        compare(driver.accessibleAnnouncementCount(), 1);
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
