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
            "canDiscardPartials": false,
            "canValidateMedia": false,
            "canReleaseValidation": false,
            "reviewText": "",
            "reviewBusy": false,
            "validationText": "",
            "validationBusy": false,
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
    function test_partialDiscardRequiresSeparateConsentAndPreservesFinalAdvice() {
        addJob("partials-a", "interrupted", "Interrompido; revisão necessária", false, false, false, 0.3);
        jobs.setProperty(0, "canReviewFiles", true);
        jobs.setProperty(0, "canDiscardPartials", true);
        jobs.setProperty(0, "reviewText", "Arquivos parciais reconhecidos.");
        openDownloads();
        const discard = findChild(rowAt(0), "download-discard-partials");
        verifyTarget(discard);
        discard.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        compare(downloads.discardedIds.length, 0);
        const popup = findChild(shell, "downloads-discard-confirmation");
        verify(popup);
        tryVerify(function () {
            return popup.opened;
        });
        verify(visibleText(popup.contentItem).includes("MP3 final"));
        verify(visibleText(popup.contentItem).includes("temporários"));
        compare(findChild(shell, "download-discard-advice").textFormat, Text.PlainText);
        compare(findChild(shell, "download-discard-cancel").Accessible.name, "Cancelar");
        verify(popup.x >= 0 && popup.y >= 0);
        verify(popup.x + popup.width <= shell.width && popup.y + popup.height <= shell.height);
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !popup.visible;
        });
        compare(downloads.discardedIds.length, 0);
        compare(downloads.discardConsentToken, "");
        mouseClick(discard);
        tryVerify(function () {
            return popup.opened;
        });
        const confirm = findChild(shell, "download-discard-confirm");
        verifyTarget(confirm);
        mouseClick(confirm);
        compare(downloads.discardedIds.length, 1);
        compare(downloads.discardedIds[0], "partials-a");
        compare(presenter.playbackState, PlayerPresenter.Unloaded);
    }
    function test_partialDiscardConsentCannotFollowAReplacedOwnerOrRevokedRevision() {
        addJob("partials-a", "interrupted", "Interrompido", false, false, false, 0);
        jobs.setProperty(0, "canDiscardPartials", true);
        openDownloads();
        mouseClick(findChild(rowAt(0), "download-discard-partials"));
        const popup = findChild(shell, "downloads-discard-confirmation");
        tryVerify(function () {
            return popup.opened;
        });
        const previous = downloads;
        previous.cancelDiscardPartialsConfirmation(previous.discardConsentJobId, previous.discardConsentToken);
        tryVerify(function () {
            return !popup.visible;
        });
        compare(previous.discardedIds.length, 0);
        mouseClick(findChild(rowAt(0), "download-discard-partials"));
        tryVerify(function () {
            return popup.opened;
        });
        const oldToken = previous.discardConsentToken;
        const replacement = createTemporaryObject(bindingComponent, testCase);
        verify(replacement);
        downloads = replacement;
        shell.downloads = replacement;
        tryVerify(function () {
            return !popup.visible;
        });
        compare(previous.discardConsentToken, "");
        verify(!previous.confirmDiscardPartials("partials-a", oldToken));
        compare(previous.discardedIds.length, 0);
        compare(replacement.discardedIds.length, 0);
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
    function test_recoveredMediaValidationIsExplicitAndDoesNotImportTheJob() {
        addJob("final-a", "interrupted", "Interrompido; revisão necessária", false, false, false, 0.3);
        jobs.setProperty(0, "canValidateMedia", true);
        jobs.setProperty(0, "reviewText", "Arquivo final reconhecido; conteúdo ainda não validado.");
        openDownloads();
        const row = rowAt(0);
        const validate = row.validateControl;
        verifyTarget(validate);
        compare(validate.text, "Validar MP3");
        compare(validate.Accessible.name, "Validar MP3");
        compare(downloads.validatedIds.length, 0, "Opening the panel never validates media");
        validate.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        compare(downloads.validatedIds.length, 1);
        compare(downloads.validatedIds[0], "final-a");
        jobs.setProperty(0, "canValidateMedia", false);
        jobs.setProperty(0, "validationBusy", true);
        jobs.setProperty(0, "canReleaseValidation", true);
        jobs.setProperty(0, "validationText", "Validando MP3 local; nenhum áudio será importado.");
        tryVerify(function () {
            return !validate.visible || !validate.enabled;
        });
        const release = row.releaseValidationControl;
        verify(waitForPolish(shell));
        verifyTarget(release);
        verify(!findChild(row, "download-review-status").visible, "Validation supersedes its stale observation text");
        compare(release.text, "Cancelar validação");
        compare(release.Accessible.name, "Cancelar validação");
        jobs.setProperty(0, "validationBusy", false);
        const message = "<b>MP3 validado localmente; ainda não importado.</b>";
        jobs.setProperty(0, "validationText", message);
        tryCompare(release, "text", "Liberar validação");
        compare(release.Accessible.name, "Liberar validação");
        const status = findChild(row, "download-validation-status");
        verify(status);
        compare(status.text, message);
        compare(status.textFormat, Text.PlainText);
        compare(status.Accessible.name, message);
        verify(!visibleText(row).includes("conteúdo ainda não validado"));
        compare(jobs.get(0).state, "interrupted", "Validated is not Imported");
        compare(findChild(row, "download-status").text, "Interrompido; revisão necessária");
        verify(!visibleText(row).includes("never-show-me"));
        verify(!visibleText(row).includes("token=secret"));
        mouseClick(release);
        compare(downloads.releasedIds.length, 1);
        compare(downloads.releasedIds[0], "final-a");
        compare(jobs.retriedIds.length, 0);
        compare(jobs.cancelledIds.length, 0);
        compare(downloads.discardedIds.length, 0);
        compare(presenter.playbackState, PlayerPresenter.Unloaded);
    }
    function test_validationControlsAreAbsentWithoutCapabilities() {
        addJob("final-a", "interrupted", "Interrompido", false, false, false, 0);
        openDownloads();
        const row = rowAt(0);
        verify(row.validateControl !== undefined, "The row exposes the guarded validation control");
        verify(row.releaseValidationControl !== undefined, "The row exposes the guarded release control");
        verify(!row.validateControl.visible || !row.validateControl.enabled);
        verify(!row.releaseValidationControl.visible || !row.releaseValidationControl.enabled);
        row.validateControl.clicked();
        row.releaseValidationControl.clicked();
        compare(downloads.validatedIds.length, 0);
        compare(downloads.releasedIds.length, 0);
        const status = findChild(row, "download-validation-status");
        verify(status);
        verify(!status.visible);
    }
    function test_validationKeyboardNavigationIncludesBothActions() {
        addJob("final-a", "interrupted", "Interrompido", false, false, false, 0);
        jobs.setProperty(0, "canValidateMedia", true);
        openDownloads();
        const list = findChild(shell, "downloads-list");
        const close = findChild(shell, "downloads-close");
        const row = rowAt(0);
        const validate = row.validateControl;
        verifyTarget(validate);
        list.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Tab);
        tryCompare(validate, "activeFocus", true);
        keyClick(Qt.Key_Tab);
        tryCompare(close, "activeFocus", true);
        keyClick(Qt.Key_Backtab);
        tryCompare(validate, "activeFocus", true);
        keyClick(Qt.Key_Backtab);
        tryCompare(list, "activeFocus", true);
        jobs.setProperty(0, "canValidateMedia", false);
        jobs.setProperty(0, "canReleaseValidation", true);
        jobs.setProperty(0, "validationBusy", true);
        const release = row.releaseValidationControl;
        verify(waitForPolish(shell));
        verifyTarget(release);
        list.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Tab);
        tryCompare(release, "activeFocus", true);
        keyClick(Qt.Key_Return);
        compare(downloads.releasedIds.length, 1);
        compare(downloads.releasedIds[0], "final-a");
        keyClick(Qt.Key_Tab);
        tryCompare(close, "activeFocus", true);
        keyClick(Qt.Key_Backtab);
        tryCompare(release, "activeFocus", true);
        keyClick(Qt.Key_Backtab);
        tryCompare(list, "activeFocus", true);
    }
    function test_validationActionsRecheckLiveCapabilitiesRowIdentityAndOwner() {
        addJob("final-a", "interrupted", "Interrompido", false, false, false, 0);
        jobs.setProperty(0, "canValidateMedia", true);
        jobs.setProperty(0, "canReleaseValidation", true);
        openDownloads();
        const row = rowAt(0);
        const validate = row.validateControl;
        const release = row.releaseValidationControl;
        verifyTarget(validate);
        verifyTarget(release);
        jobs.setProperty(0, "canValidateMedia", false);
        jobs.setProperty(0, "canReleaseValidation", false);
        validate.clicked();
        release.clicked();
        compare(downloads.validatedIds.length, 0);
        compare(downloads.releasedIds.length, 0);
        jobs.setProperty(0, "canValidateMedia", true);
        jobs.setProperty(0, "canReleaseValidation", true);
        jobs.setProperty(0, "jobId", "replacement-row");
        validate.clicked();
        release.clicked();
        compare(downloads.validatedIds.length, 0, "A recycled row cannot retarget a previous validation control");
        compare(downloads.releasedIds.length, 0, "A recycled row cannot release another job");
        jobs.setProperty(0, "jobId", "final-a");
        const previous = downloads;
        const replacement = createTemporaryObject(bindingComponent, testCase);
        verify(replacement);
        downloads = replacement;
        shell.downloads = replacement;
        validate.clicked();
        release.clicked();
        compare(previous.validatedIds.length, 0);
        compare(previous.releasedIds.length, 0);
        compare(replacement.validatedIds.length, 0, "An old delegate cannot act on its replacement owner");
        compare(replacement.releasedIds.length, 0);
    }
    function test_validationActionsWrapAndKeepTheSixControlFocusOrder() {
        addJob("all-actions", "interrupted", "Interrompido", true, true, false, 0);
        jobs.setProperty(0, "canReviewFiles", true);
        jobs.setProperty(0, "canDiscardPartials", true);
        jobs.setProperty(0, "canValidateMedia", true);
        jobs.setProperty(0, "canReleaseValidation", true);
        openDownloads();
        const row = rowAt(0);
        const controls = [row.cancelControl, row.retryControl, row.reviewControl, row.discardControl, row.validateControl, row.releaseValidationControl];
        for (let index = 0; index < controls.length; ++index) {
            verifyTarget(controls[index]);
            verify(controls[index].Accessible.name.length > 0);
        }
        controls[0].forceActiveFocus(Qt.TabFocusReason);
        for (let index = 1; index < controls.length; ++index) {
            keyClick(Qt.Key_Tab);
            tryCompare(controls[index], "activeFocus", true);
        }
        const close = findChild(shell, "downloads-close");
        keyClick(Qt.Key_Tab);
        tryCompare(close, "activeFocus", true);
        for (let index = controls.length - 1; index >= 0; --index) {
            keyClick(Qt.Key_Backtab);
            tryCompare(controls[index], "activeFocus", true);
        }
        keyClick(Qt.Key_Backtab);
        tryCompare(findChild(shell, "downloads-list"), "activeFocus", true);
        compare(downloads.validatedIds.length, 0);
        compare(downloads.releasedIds.length, 0);
    }
    function test_validationAtMinimumWindowKeepsControlsAndPlainStatusVisible(data) {
        driver.setSystemAppearance(data.dark, false);
        driver.setHighContrast(data.contrast, false);
        addJob("final-a", "interrupted", "Interrompido", false, false, false, 0.5);
        jobs.setProperty(0, "canReviewFiles", true);
        jobs.setProperty(0, "canValidateMedia", !data.retained);
        jobs.setProperty(0, "canReleaseValidation", data.retained);
        jobs.setProperty(0, "validationText", "A validação de MP3 é somente leitura; ela não importa áudio, não retoma downloads e preserva os arquivos locais.");
        const panel = openDownloads();
        const row = rowAt(0);
        verify(panel.width <= 672 && panel.height <= 472);
        verifyTarget(row.reviewControl);
        verifyTarget(data.retained ? row.releaseValidationControl : row.validateControl);
        const status = findChild(row, "download-validation-status");
        verify(status);
        compare(status.textFormat, Text.PlainText);
        verify(Qt.colorEqual(status.color, shell.uiContract.textSecondaryColor));
        const p = status.mapToItem(shell.contentItem, 0, 0);
        verify(p.x >= 0 && p.y >= 0);
        verify(p.x + status.width <= shell.width + 0.5);
        verify(p.y + status.height <= shell.height + 0.5);
        verify(!visibleText(panel.contentItem).includes("never-show-me"));
        verify(!visibleText(panel.contentItem).includes("token=secret"));
    }
    function test_validationAtMinimumWindowKeepsControlsAndPlainStatusVisible_data() {
        return [
            {
                "tag": "light-validate",
                "dark": false,
                "contrast": false,
                "retained": false
            },
            {
                "tag": "dark-release",
                "dark": true,
                "contrast": false,
                "retained": true
            },
            {
                "tag": "contrast-release",
                "dark": false,
                "contrast": true,
                "retained": true
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
            property var discardedIds: []
            property var validatedIds: []
            property var releasedIds: []
            property string discardConsentJobId: ""
            property string discardConsentToken: ""
            property int discardRevision: 0
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
            function validateMedia(jobId) {
                validatedIds = validatedIds.concat([jobId]);
                return true;
            }
            function releaseValidation(jobId) {
                releasedIds = releasedIds.concat([jobId]);
                return true;
            }
            function requestDiscardPartials(jobId) {
                discardConsentJobId = jobId;
                discardConsentToken = String(++discardRevision);
                return true;
            }
            function confirmDiscardPartials(jobId, token) {
                if (jobId !== discardConsentJobId || token !== discardConsentToken || token.length === 0)
                    return false;
                discardConsentJobId = "";
                discardConsentToken = "";
                discardedIds = discardedIds.concat([jobId]);
                return true;
            }
            function cancelDiscardPartialsConfirmation(jobId, token) {
                if (jobId === discardConsentJobId && token === discardConsentToken) {
                    discardConsentJobId = "";
                    discardConsentToken = "";
                }
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
