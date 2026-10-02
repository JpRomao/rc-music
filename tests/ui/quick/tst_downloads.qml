pragma ComponentBehavior: Bound

import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase

    property var downloads
    readonly property var scenarioFactory: testScenarioDriver // qmllint disable unqualified
    property var scenario: null
    property var driver: null
    property var presenter: null
    property var shell

    function addJob(jobId, state, status, canCancel, canRetryImport, indeterminate, progress) {
        jobs.append({
            "jobId": jobId,
            "title": "Áudio " + jobId,
            "state": state,
            "statusText": status,
            "completedUnits": 3,
            "totalUnits": 10,
            "indeterminate": indeterminate,
            "progressRatio": progress,
            "errorText": "",
            "canCancel": canCancel,
            "canRetryImport": canRetryImport,
            "canReviewFiles": false,
            "reviewText": "",
            "reviewBusy": false,
            "outputPath": "C:/private/never-show-me.mp3",
            "url": "https://private.invalid/never-show-me?token=secret"
        });
    }
    function cleanup() {
        if (shell)
            shell.destroy();
        shell = null;
        wait(0);
        downloads = null;
        if (driver)
            driver.setHighContrast(false, false);
        if (scenario)
            verify(scenarioFactory.releaseDownloadsScenario(scenario));
        scenario = null;
        driver = null;
        presenter = null;
    }
    function init() {
        scenario = scenarioFactory.createDownloadsScenario();
        verify(scenario);
        driver = scenario.driver;
        presenter = scenario.presenter;
        verify(driver && presenter);
        verify(!presenter.exiting && !presenter.exitCommitted, "Each case starts with a fresh lifecycle");
        compare(scenario.localExitCalls(), 0);
        driver.resetLocalScenario();
        driver.setHighContrast(false, false);
        jobs.clear();
        jobs.cancelledIds = [];
        jobs.retriedIds = [];
        downloads = createTemporaryObject(bindingComponent, testCase);
        verify(downloads);
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.width = 720;
        shell.height = 520;
        shell.requestActivate();
        tryVerify(function () {
            return testCase.shell.active;
        });
    }
    function inside(item, ancestor) {
        while (item) {
            if (item === ancestor)
                return true;
            item = item.parent;
        }
        return false;
    }
    function openDownloads() {
        verify(shell.downloads !== undefined, "Main must expose an optional downloads binding");
        shell.downloads = downloads;
        const opener = findChild(shell, "downloads");
        verify(opener && opener.visible && opener.enabled, "The optional binding must expose Downloads");
        compare(opener.Accessible.name, "Downloads");
        opener.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        const panel = findChild(shell, "downloads-panel");
        verify(panel);
        tryVerify(function () {
            return panel.opened;
        });
        verify(waitForPolish(shell));
        return panel;
    }
    function rowAt(index) {
        const list = findChild(shell, "downloads-list");
        verify(list);
        list.positionViewAtIndex(index, ListView.Contain);
        verify(waitForPolish(shell));
        tryVerify(function () {
            return list.itemAtIndex(index) !== null;
        });
        return list.itemAtIndex(index);
    }
    function test_absentBindingPreservesLocalPlayer() {
        compare(shell.downloads, null, "Downloads must be absent by default");
        const list = findChild(shell, "downloads-list");
        verify(list);
        compare(list.KeyNavigation.backtab, findChild(shell, "downloads-close"), "Hidden optional panels must still have a defined focus target");
        const opener = findChild(shell, "downloads");
        verify(!opener || !opener.visible);
        verify(findChild(shell, "preferences").visible);
        verify(findChild(shell, "open-audio").enabled);
        verify(shell.windowShortcutsEnabled);
        compare(presenter.playbackState, PlayerPresenter.Unloaded);
    }
    function test_finalSaveFailureRequiresExplicitSeparateConfirmation() {
        downloads.warningText = "Não foi possível salvar o histórico de downloads.";
        downloads.canCloseAfterFinalSaveFailure = true;
        openDownloads();
        const leave = findChild(shell, "downloads-close-without-save");
        verifyTarget(leave);
        mouseClick(leave);
        compare(downloads.unsafeCloses, 0, "Opening the consent dialog is not consent");
        verify(!presenter.exiting, "Opening the consent dialog must not request exit");
        verify(!presenter.exitCommitted);
        compare(scenario.localExitCalls(), 0, "Opening consent must not exit the local platform");
        const confirmation = findChild(shell, "downloads-exit-confirmation");
        verify(confirmation);
        tryVerify(function () {
            return confirmation.opened;
        });
        verify(visibleText(confirmation.contentItem).includes("Sair sem salvar o histórico"));
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !confirmation.visible;
        });
        compare(downloads.unsafeCloses, 0);
        verify(!presenter.exiting, "Cancelling consent must not request exit");
        verify(!presenter.exitCommitted);
        compare(scenario.localExitCalls(), 0, "Cancelling consent must not exit the local platform");
        tryVerify(function () {
            return leave.activeFocus;
        });
        mouseClick(leave);
        tryVerify(function () {
            return confirmation.opened;
        });
        const confirm = findChild(shell, "downloads-confirm-close-without-save");
        verifyTarget(confirm);
        mouseClick(confirm);
        compare(downloads.unsafeCloses, 1);
        verify(!downloads.exitObservedDuringAbandonment, "The owner must accept abandonment before a new exit intent");
        compare(downloads.exitCallsObservedDuringAbandonment, 0);
        verify(presenter.exiting, "A successful explicit choice must request actual app exit");
        verify(presenter.exitCommitted, "A successful explicit choice must commit actual app exit");
        compare(scenario.localExitCalls(), 1, "A successful explicit choice must exit the local platform exactly once");
    }
    function test_refusedHistoryAbandonmentDoesNotRequestExit() {
        downloads.acceptHistoryAbandonment = false;
        downloads.canCloseAfterFinalSaveFailure = true;
        openDownloads();
        mouseClick(findChild(shell, "downloads-close-without-save"));
        const confirmation = findChild(shell, "downloads-exit-confirmation");
        tryVerify(function () {
            return confirmation.opened;
        });
        mouseClick(findChild(shell, "downloads-confirm-close-without-save"));
        compare(downloads.unsafeCloses, 1);
        verify(!downloads.exitObservedDuringAbandonment);
        compare(downloads.exitCallsObservedDuringAbandonment, 0);
        verify(!presenter.exiting, "A refused abandonment must not request exit");
        verify(!presenter.exitCommitted);
        compare(scenario.localExitCalls(), 0, "A refused abandonment must not exit the local platform");
    }
    function test_importedHistoryIsFinalWithoutActionsOrFileCapability() {
        addJob("history", "imported", "Já importado na fila", false, false, false, 1);
        openDownloads();
        const row = rowAt(0);
        compare(findChild(row, "download-status").text, "Já importado na fila");
        compare(findChild(row, "download-progress").value, 1);
        verify(!findChild(row, "download-progress").indeterminate);
        const cancel = findChild(row, "download-cancel");
        const retry = findChild(row, "download-retry-import");
        verify(!cancel.visible || !cancel.enabled);
        verify(!retry.visible || !retry.enabled);
        verify(!visibleText(row).includes("never-show-me"));
        compare(jobs.cancelledIds.length, 0);
        compare(jobs.retriedIds.length, 0);
    }
    function test_interruptedFilesReviewUsesOnlyStableJobIdAndIsKeyboardAccessible() {
        addJob("interrupted-a", "interrupted", "Interrompido; revisão necessária", false, false, false, 0.3);
        jobs.setProperty(0, "canReviewFiles", true);
        openDownloads();
        const row = rowAt(0);
        const review = findChild(row, "download-review-files");
        verifyTarget(review);
        compare(review.text, "Verificar arquivos");
        review.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        compare(downloads.reviewedIds.length, 1);
        compare(downloads.reviewedIds[0], "interrupted-a");
        jobs.setProperty(0, "reviewBusy", true);
        jobs.setProperty(0, "canReviewFiles", false);
        jobs.setProperty(0, "reviewText", "Verificando arquivos locais; nada será alterado.");
        tryCompare(review, "enabled", false);
        verify(visibleText(row).includes("Verificando arquivos locais"));
        jobs.setProperty(0, "reviewBusy", false);
        jobs.setProperty(0, "canReviewFiles", true);
        jobs.setProperty(0, "reviewText", "Arquivo final reconhecido; conteúdo ainda não validado ou importado.");
        tryCompare(review, "text", "Reverificar arquivos");
        verify(!visibleText(row).includes("never-show-me"));
        compare(jobs.cancelledIds.length, 0);
        compare(jobs.retriedIds.length, 0);
        compare(presenter.playbackState, PlayerPresenter.Unloaded);
        keyClick(Qt.Key_Tab);
        tryVerify(function () {
            return findChild(shell, "downloads-close").activeFocus;
        });
    }
    function test_jobActionsUseCurrentStableIdsAndCapabilities() {
        addJob("cancel-a", "queued", "Na fila", true, false, true, 0);
        addJob("retry-b", "import_failed", "Arquivo pronto; importação pendente", false, true, false, 1);
        openDownloads();
        const first = rowAt(0);
        const cancel = findChild(first, "download-cancel");
        verifyTarget(cancel);
        verify(cancel.enabled);
        mouseClick(cancel);
        compare(jobs.cancelledIds.length, 1);
        compare(jobs.cancelledIds[0], "cancel-a");
        jobs.remove(0);
        const second = rowAt(0);
        const retry = findChild(second, "download-retry-import");
        verifyTarget(retry);
        mouseClick(retry);
        compare(jobs.retriedIds.length, 1);
        compare(jobs.retriedIds[0], "retry-b");
        jobs.setProperty(0, "canRetryImport", false);
        tryVerify(function () {
            return !retry.visible || !retry.enabled;
        });
    }
    function test_modalFocusKeyboardAndReturnDoNotAffectPlayback() {
        driver.enqueueLocalSelection(["C:/Music/A.mp3", "C:/Music/B.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        addJob("active", "downloading", "Baixando", true, false, true, 0);
        const panel = openDownloads();
        verify(panel.modal);
        compare(panel.contentItem.Accessible.role, Accessible.Dialog);
        verify(!shell.windowShortcutsEnabled);
        verify(!findChild(shell, "source-drop-area").enabled);
        const close = findChild(shell, "downloads-close");
        tryVerify(function () {
            return close.activeFocus;
        });
        verify(close.background.border.width >= 3);
        for (let index = 0; index < 8; ++index) {
            keyClick(Qt.Key_Tab);
            verify(inside(shell.activeFocusItem, panel.contentItem), "Tab must stay within the modal panel");
        }
        for (let index = 0; index < 8; ++index) {
            keyClick(Qt.Key_Backtab);
            verify(inside(shell.activeFocusItem, panel.contentItem), "Backtab must stay within the modal panel");
        }
        const list = findChild(shell, "downloads-list");
        list.forceActiveFocus(Qt.TabFocusReason);
        const pauses = driver.localPauseCount();
        const seeks = driver.localSeekCount();
        const loads = driver.localLoadCount();
        const volume = presenter.volume;
        const shuffle = presenter.shuffleQueueEnabled;
        const repeat = presenter.repeatQueueEnabled;
        [Qt.Key_Space, Qt.Key_M, Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down, Qt.Key_F1, Qt.Key_F2].forEach(function (key) {
            keyClick(key);
        });
        keyClick(Qt.Key_Right, Qt.ShiftModifier);
        keyClick(Qt.Key_Left, Qt.ShiftModifier);
        keyClick(Qt.Key_O, Qt.ControlModifier);
        keyClick(Qt.Key_Q, Qt.ControlModifier);
        verify(panel.visible);
        compare(driver.localPauseCount(), pauses);
        compare(driver.localSeekCount(), seeks);
        compare(driver.localLoadCount(), loads);
        compare(presenter.volume, volume);
        compare(presenter.shuffleQueueEnabled, shuffle);
        compare(presenter.repeatQueueEnabled, repeat);
        verify(!presenter.exiting && !presenter.selectingAudioFiles);
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !panel.visible;
        });
        tryVerify(function () {
            return findChild(testCase.shell, "downloads").activeFocus;
        });
        verify(shell.windowShortcutsEnabled);
    }
    function test_ownerRetryActionsFollowLiveCapabilities() {
        downloads.canRetryStartup = true;
        downloads.canRetryAdmission = true;
        downloads.canRetryFinalSave = true;
        openDownloads();
        const startup = findChild(shell, "downloads-retry-startup");
        const admission = findChild(shell, "downloads-retry-admission");
        const finalSave = findChild(shell, "downloads-retry-final-save");
        verifyTarget(startup);
        verifyTarget(admission);
        verifyTarget(finalSave);
        mouseClick(startup);
        mouseClick(admission);
        mouseClick(finalSave);
        compare(downloads.startupRetries, 1);
        compare(downloads.admissionRetries, 1);
        compare(downloads.finalSaveRetries, 1);
        downloads.canRetryStartup = false;
        downloads.canRetryAdmission = false;
        downloads.canRetryFinalSave = false;
        tryVerify(function () {
            return !startup.visible || !startup.enabled;
        });
        tryVerify(function () {
            return !admission.visible || !admission.enabled;
        });
        tryVerify(function () {
            return !finalSave.visible || !finalSave.enabled;
        });
    }
    function test_revokedCloseCapabilityCannotReuseConsent() {
        downloads.canCloseAfterFinalSaveFailure = true;
        openDownloads();
        mouseClick(findChild(shell, "downloads-close-without-save"));
        const confirmation = findChild(shell, "downloads-exit-confirmation");
        tryVerify(function () {
            return confirmation.opened;
        });
        downloads.canCloseAfterFinalSaveFailure = false;
        const confirm = findChild(shell, "downloads-confirm-close-without-save");
        tryVerify(function () {
            return !confirmation.visible || !confirm.enabled;
        });
        compare(downloads.unsafeCloses, 0);
        verify(!presenter.exiting, "Revoked consent must not request exit");
        verify(!presenter.exitCommitted);
        compare(scenario.localExitCalls(), 0, "Revoked consent must not exit the local platform");
    }
    function test_rowsExposePlainStatusAndProgressWithoutPrivateFields() {
        addJob("active", "downloading", "Baixando", true, false, false, 0.3);
        jobs.setProperty(0, "title", "<b>Nome literal</b>");
        jobs.setProperty(0, "errorText", "<i>Erro literal</i>");
        downloads.statusText = "<b>Estado literal</b>";
        downloads.warningText = "<i>Aviso literal</i>";
        const panel = openDownloads();
        const row = rowAt(0);
        const title = findChild(row, "download-title");
        const status = findChild(row, "download-status");
        const error = findChild(row, "download-error");
        compare(title.text, "<b>Nome literal</b>");
        compare(status.text, "Baixando");
        compare(error.text, "<i>Erro literal</i>");
        compare(title.textFormat, Text.PlainText);
        compare(status.textFormat, Text.PlainText);
        compare(error.textFormat, Text.PlainText);
        compare(findChild(shell, "downloads-status").textFormat, Text.PlainText);
        compare(findChild(shell, "downloads-warning").textFormat, Text.PlainText);
        const progress = findChild(row, "download-progress");
        compare(progress.value, 0.3);
        verify(!progress.indeterminate);
        jobs.setProperty(0, "indeterminate", true);
        tryCompare(progress, "indeterminate", true);
        const rendered = visibleText(panel.contentItem);
        verify(!rendered.includes("never-show-me"));
        verify(!rendered.includes("token=secret"));
        compare(findChild(shell, "downloads-list").count, 1);
    }
    function test_smallWindowUsesThemeAndKeepsControlsVisible(data) {
        driver.setSystemAppearance(data.dark, false);
        driver.setHighContrast(data.contrast, false);
        addJob("active", "downloading", "Baixando", true, false, false, 0.5);
        const panel = openDownloads();
        verify(panel.width <= 672 && panel.height <= 472);
        verify(Qt.colorEqual(panel.background.color, shell.uiContract.surfaceRaisedColor));
        compare(panel.background.color.a, 1);
        verify(Qt.colorEqual(findChild(rowAt(0), "download-title").color, shell.uiContract.textPrimaryColor));
        verifyTarget(findChild(shell, "downloads-close"));
        verifyTarget(findChild(rowAt(0), "download-cancel"));
        if (data.contrast)
            verify(panel.background.border.width >= 2);
    }
    function test_smallWindowUsesThemeAndKeepsControlsVisible_data() {
        return [
            {
                "tag": "light",
                "dark": false,
                "contrast": false
            },
            {
                "tag": "dark",
                "dark": true,
                "contrast": false
            },
            {
                "tag": "high-contrast",
                "dark": false,
                "contrast": true
            }
        ];
    }
    function verifyTarget(control) {
        verify(control);
        verify(control.width >= 40 && control.height >= 40);
        const p = control.mapToItem(shell.contentItem, 0, 0);
        verify(p.x >= 0 && p.y >= 0);
        verify(p.x + control.width <= shell.width + 0.5);
        verify(p.y + control.height <= shell.height + 0.5);
    }
    function visibleText(item) {
        let result = "";
        if (!item || !item.visible)
            return result;
        if (typeof item.text === "string")
            result += item.text + "\n";
        const children = item.children || [];
        for (let index = 0; index < children.length; ++index)
            result += visibleText(children[index]);
        return result;
    }

    name: "OptionalDownloads"
    when: windowShown

    // This proxy and ListModel are UI-only harnesses. They neither execute a
    // download nor stand in for the production owner/binding integration tests.
    ListModel {
        id: jobs

        property var cancelledIds: []
        property var retriedIds: []

        function cancel(jobId) {
            cancelledIds = cancelledIds.concat([jobId]);
            return true;
        }
        function retryImport(jobId) {
            retriedIds = retriedIds.concat([jobId]);
            return true;
        }
    }
    Component {
        id: bindingComponent

        QtObject {
            property bool acceptHistoryAbandonment: true
            property int admissionRetries: 0
            property bool canCloseAfterFinalSaveFailure: false
            property bool canRetryAdmission: false
            property bool canRetryFinalSave: false
            property bool canRetryStartup: false
            property int finalSaveRetries: 0
            property bool exitObservedDuringAbandonment: false
            property int exitCallsObservedDuringAbandonment: -1
            property var jobsModel: jobs
            property int startupRetries: 0
            property var reviewedIds: []
            property string statusText: "Downloads disponíveis"
            property int unsafeCloses: 0
            property string warningText: ""

            function closeAfterFinalSaveFailure() {
                ++unsafeCloses;
                exitObservedDuringAbandonment = testCase.presenter.exiting;
                exitCallsObservedDuringAbandonment = testCase.scenario.localExitCalls();
                return acceptHistoryAbandonment;
            }
            function retryAdmission() {
                ++admissionRetries;
            }
            function retryFinalSave() {
                ++finalSaveRetries;
            }
            function retryStartup() {
                ++startupRetries;
            }
            function reviewFiles(jobId) {
                reviewedIds = reviewedIds.concat([jobId]);
                return true;
            }
        }
    }
    Component {
        id: shellComponent

        Main {
            presenter: testCase.presenter
            uiContract: testShellUiContract // qmllint disable unqualified
        }
    }
}
