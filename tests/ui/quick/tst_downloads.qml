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
            "canRetryDownload": false,
            "retryDownloadReason": "",
            "canReviewFiles": false,
            "canDiscardPartials": false,
            "recoveryWithoutFiles": false,
            "canValidateMedia": false,
            "canImportRecovered": false,
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
        libraryLedger.downloadJobs = ["in-library"];
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
    function clickVisible(item) {
        const page = findChild(shell, "downloads-panel");
        if (page && page.embedded) {
            page.revealFocusedItem(item);
            verify(waitForPolish(shell));
            const point = item.mapToItem(shell.contentItem, 0, 0);
            verify(point.x >= 0 && point.y >= 0);
            verify(point.x + item.width <= shell.width + 1);
            verify(point.y + item.height <= findChild(shell, "player-footer").y + 1 || !inside(item, page.contentItem));
        }
        mouseClick(item);
    }
    function openDownloads() {
        verify(shell.downloads !== undefined, "Main must expose an optional downloads binding");
        shell.downloads = downloads;
        const opener = findChild(shell, "downloads");
        verify(opener && opener.visible && opener.enabled, "The optional binding must expose Downloads");
        compare(opener.Accessible.name, "Atividade");
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
    function test_auroraRowsSitOnTheCanvasWithAThinProgressBar() {
        addJob("active", "downloading", "Baixando", true, false, false, 0.5);
        const panel = openDownloads();
        verify(panel.embedded);
        const row = rowAt(0);
        findChild(shell, "downloads-close").forceActiveFocus(Qt.TabFocusReason);
        const card = findChild(row, "download-row-background");
        verify(card);
        compare(card.border.width, 0, "a job is a list item, not a bordered card");
        compare(card.color.a, 0);
        const divider = findChild(row, "download-row-divider");
        verify(divider && divider.visible);
        verify(Qt.colorEqual(divider.color, shell.uiContract.dividerColor));
        const title = findChild(row, "download-title");
        compare(title.font.family, shell.uiContract.headingFontFamily);
        compare(title.font.pixelSize, shell.uiContract.typeSubtitle);
        const progress = findChild(row, "download-progress");
        compare(progress.height, 4);
        compare(progress.background.border.width, 0);
        verify(Qt.colorEqual(progress.background.color, shell.uiContract.dividerColor));
    }
    function test_failedDownloadRetryUsesOnlyCurrentExactRowIdentity() {
        addJob("failed-a", "failed", "Falhou", false, false, false, 0);
        jobs.setProperty(0, "canRetryDownload", true);
        jobs.setProperty(0, "retryDownloadReason", "Uma nova tentativa será salva antes de iniciar.");
        const panel = openDownloads();
        const row = rowAt(0);
        const retry = findChild(row, "download-retry");
        verify(retry && retry.visible && retry.enabled, "Failed downloads need a distinct retry action");
        verifyTarget(retry);
        compare(retry.Accessible.name, "Tentar novamente");
        retry.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        compare(downloads.downloadRetries, ["failed-a"]);
        jobs.setProperty(0, "canRetryDownload", false);
        retry.clicked();
        compare(downloads.downloadRetries.length, 1);
        verify(findChild(row, "download-retry-reason").text.length > 0);
        const old = downloads;
        const replacement = createTemporaryObject(bindingComponent, testCase);
        shell.downloads = replacement;
        jobs.setProperty(0, "canRetryDownload", true);
        retry.clicked();
        compare(old.downloadRetries.length, 1);
        compare(replacement.downloadRetries.length, 0, "A retained delegate cannot act on a different owner");
        panel.close();
    }
    function test_informationIsSelectablePlainTextWithKeyboardCopyFeedback() {
        addJob("history-a", "failed", "Falhou", false, false, false, 0);
        jobs.setProperty(0, "retryDownloadReason", "Envie este item novamente pelo navegador.");
        openDownloads();
        const row = rowAt(0);
        const information = findChild(row, "download-information");
        verify(information && information.visible && information.enabled, "History needs selectable/copyable information");
        information.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        const dialog = findChild(shell, "download-information-dialog");
        const text = findChild(shell, "download-information-text");
        const copy = findChild(shell, "download-copy-information");
        const close = findChild(shell, "download-information-close");
        tryCompare(dialog, "opened", true);
        tryCompare(text, "activeFocus", true);
        compare(text.textFormat, TextEdit.PlainText);
        verify(text.readOnly && text.selectByMouse && text.activeFocusOnTab);
        compare(text.text, downloads.downloadInformation("history-a"));
        keyClick(Qt.Key_A, Qt.ControlModifier);
        compare(text.selectedText, text.text);
        verify(!text.text.includes("never-show-me"));
        keyClick(Qt.Key_Tab);
        tryCompare(copy, "activeFocus", true);
        verifyTarget(copy);
        keyClick(Qt.Key_Return);
        compare(downloads.copiedIds, ["history-a"]);
        const feedback = findChild(shell, "download-copy-feedback");
        compare(feedback.text, "Informações copiadas.");
        compare(feedback.textFormat, Text.PlainText);
        downloads.copyAccepted = false;
        keyClick(Qt.Key_Return);
        compare(feedback.text, "Não foi possível copiar as informações.");
        keyClick(Qt.Key_Tab);
        tryCompare(close, "activeFocus", true);
        keyClick(Qt.Key_Escape);
        tryCompare(information, "activeFocus", true);
        verify(!dialog.visible);
        compare(presenter.playbackState, PlayerPresenter.Unloaded);
    }
    function test_informationRevokesRemovedHistoryAndChangedBackend() {
        addJob("history-a", "failed", "Falhou", false, false, false, 0);
        openDownloads();
        const information = findChild(rowAt(0), "download-information");
        verify(information);
        clickVisible(information);
        const dialog = findChild(shell, "download-information-dialog");
        const copy = findChild(shell, "download-copy-information");
        tryCompare(dialog, "opened", true);
        jobs.clear();
        tryCompare(dialog, "visible", false);
        copy.clicked();
        compare(downloads.copiedIds.length, 0);
        addJob("history-b", "failed", "Falhou", false, false, false, 0);
        const replacementRow = rowAt(0);
        compare(replacementRow.jobId, "history-b");
        verify(replacementRow.validationIdentityCurrent(), "New history must have a current delegate identity");
        const replacementInformation = findChild(replacementRow, "download-information");
        verify(replacementInformation.enabled, "New history must enable information");
        verifyTarget(replacementInformation);
        clickVisible(replacementInformation);
        tryCompare(dialog, "opened", true);
        const old = downloads;
        shell.downloads = createTemporaryObject(bindingComponent, testCase);
        tryCompare(dialog, "visible", false);
        copy.clicked();
        compare(old.copiedIds.length, 0);
        compare(shell.downloads.copiedIds.length, 0);
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
    function test_openDownloadsFolderUsesCurrentBackendAndReportsFailure() {
        const panel = openDownloads();
        const close = findChild(shell, "downloads-close");
        const openFolder = findChild(shell, "downloads-open-folder");
        verifyTarget(openFolder);
        compare(openFolder.Accessible.name, "Abrir pasta de downloads");
        verify(openFolder.visible && openFolder.enabled);
        tryCompare(close, "activeFocus", true);
        keyClick(Qt.Key_Tab);
        tryCompare(openFolder, "activeFocus", true);
        keyClick(Qt.Key_Return);
        compare(downloads.folderOpens, 1);
        verify(panel.opened);
        compare(presenter.playbackState, PlayerPresenter.Unloaded);

        downloads.downloadDirectoryPending = true;
        tryCompare(openFolder, "enabled", false);
        openFolder.clicked();
        compare(downloads.folderOpens, 1);
        downloads.downloadDirectoryPending = false;
        tryCompare(openFolder, "enabled", true);
        downloads.downloadDirectoryStatus = "Não foi possível abrir a pasta de downloads.";
        const feedback = findChild(shell, "downloads-folder-feedback");
        tryCompare(feedback, "visible", true);
        compare(feedback.textFormat, Text.PlainText);
        compare(feedback.Accessible.name, downloads.downloadDirectoryStatus);
        downloads.downloadDirectoryText = "";
        tryCompare(openFolder, "visible", false);
    }
    function test_historyCapacityReportsExactCountersAtMinimumWindow() {
        downloads.historyCapacityText = "Histórico: 3/64 pedidos · 5/128 itens · 2048/1048576 bytes de pedidos";
        const panel = openDownloads();
        const capacity = findChild(shell, "downloads-capacity");
        verify(capacity, "Downloads must expose the retained history usage and limits");
        verify(capacity.visible);
        compare(capacity.text, downloads.historyCapacityText);
        compare(capacity.textFormat, Text.PlainText);
        compare(capacity.wrapMode, Text.Wrap);
        compare(capacity.Accessible.role, Accessible.StaticText);
        compare(capacity.Accessible.name, capacity.text);
        downloads.historyCapacityText = "Histórico: 64/64 pedidos · 128/128 itens · 1048576/1048576 bytes de pedidos";
        tryCompare(capacity, "text", downloads.historyCapacityText);
        verify(waitForPolish(shell));
        verify(panel.width <= 672 && panel.height <= 472);
        const position = capacity.mapToItem(shell.contentItem, 0, 0);
        verify(position.x >= 0 && position.y >= 0);
        verify(position.x + capacity.width <= shell.width + 0.5);
        verify(position.y + capacity.height <= shell.height + 0.5);
        verify(capacity.height >= capacity.implicitHeight);
        verifyTarget(findChild(shell, "downloads-close"));
    }
    function test_historyCapacityHiddenWhenUnavailable() {
        openDownloads();
        const capacity = findChild(shell, "downloads-capacity");
        verify(capacity);
        verify(!capacity.visible);
        shell.downloads = {
            "statusText": "Downloads disponíveis",
            "warningText": "",
            "canCloseAfterFinalSaveFailure": false,
            "canRetryAdmission": false,
            "canRetryFinalSave": false,
            "canRetryStartup": false,
            "canRenewNamespace": false,
            "jobsModel": jobs
        };
        verify(waitForPolish(shell));
        compare(capacity.text, "");
        verify(!capacity.visible, "Backends without capacity support must keep the existing panel usable");
    }
    function test_archiveConfirmationStartsOnlyFromCurrentConsent() {
        const panel = openDownloads();
        const action = findChild(shell, "downloads-archive");
        verify(action, "Downloads must expose the conditional archive action");
        verify(!action.visible);
        downloads.canArchiveSettled = true;
        verifyTarget(action);
        keyClick(Qt.Key_Tab);
        tryCompare(findChild(shell, "downloads-open-folder"), "activeFocus", true);
        keyClick(Qt.Key_Tab);
        tryVerify(function () {
            return action.activeFocus;
        });
        keyClick(Qt.Key_Return);
        const confirmation = findChild(shell, "downloads-archive-confirmation");
        const cancel = findChild(shell, "downloads-cancel-archive");
        const confirm = findChild(shell, "downloads-confirm-archive");
        tryVerify(function () {
            return confirmation.opened && cancel.activeFocus;
        });
        compare(downloads.archives, 0);
        verify(visibleText(confirmation.contentItem).includes("fila"));
        verify(visibleText(confirmation.contentItem).includes("MP3"));
        verifyTarget(cancel);
        verifyTarget(confirm);
        verify(panel.width <= 672 && panel.height <= 472);
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !confirmation.visible && action.activeFocus;
        });
        compare(downloads.archiveConsentToken, "");
        keyClick(Qt.Key_Return);
        tryVerify(function () {
            return confirmation.opened;
        });
        downloads.archiveConsentToken = "";
        tryVerify(function () {
            return !confirmation.visible;
        });
        compare(downloads.archives, 0);
        clickVisible(action);
        tryVerify(function () {
            return confirmation.opened;
        });
        clickVisible(confirm);
        compare(downloads.archives, 1);
        tryVerify(function () {
            return !confirmation.visible && !action.visible;
        });
    }
    function test_archiveSaveRetryUsesTheArchiveAction() {
        downloads.canRetryArchive = true;
        openDownloads();
        const action = findChild(shell, "downloads-archive");
        verifyTarget(action);
        verify(action.visible && action.enabled);
        verify(action.text.includes("novamente"));
        clickVisible(action);
        compare(downloads.archiveRetries, 1);
        compare(downloads.archives, 0);
        compare(downloads.finalSaveRetries, 0);
        compare(downloads.admissionRetries, 0);
        verify(!findChild(shell, "downloads-archive-confirmation").visible);
    }
    function test_archiveConfirmationDoesNotOpenAfterSynchronousPanelClose() {
        downloads.canArchiveSettled = true;
        const panel = openDownloads();
        downloads.onArchiveRequest = function () {
            panel.close();
        };
        clickVisible(findChild(shell, "downloads-archive"));
        tryVerify(function () {
            return !panel.visible;
        });
        verify(!findChild(shell, "downloads-archive-confirmation").visible);
        compare(downloads.archiveConsentToken, "");
        compare(downloads.archives, 0);
    }
    function test_finalSaveFailureRequiresExplicitSeparateConfirmation() {
        downloads.warningText = "Não foi possível salvar o histórico de downloads.";
        downloads.canCloseAfterFinalSaveFailure = true;
        openDownloads();
        const leave = findChild(shell, "downloads-close-without-save");
        verifyTarget(leave);
        clickVisible(leave);
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
        clickVisible(leave);
        tryVerify(function () {
            return confirmation.opened;
        });
        const confirm = findChild(shell, "downloads-confirm-close-without-save");
        verifyTarget(confirm);
        clickVisible(confirm);
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
        clickVisible(findChild(shell, "downloads-close-without-save"));
        const confirmation = findChild(shell, "downloads-exit-confirmation");
        tryVerify(function () {
            return confirmation.opened;
        });
        clickVisible(findChild(shell, "downloads-confirm-close-without-save"));
        compare(downloads.unsafeCloses, 1);
        verify(!downloads.exitObservedDuringAbandonment);
        compare(downloads.exitCallsObservedDuringAbandonment, 0);
        verify(!presenter.exiting, "A refused abandonment must not request exit");
        verify(!presenter.exitCommitted);
        compare(scenario.localExitCalls(), 0, "A refused abandonment must not exit the local platform");
    }
    function test_finishedDownloadsSayWhereTheMp3Went() {
        addJob("in-library", "completed", "Concluído", false, false, false, 1);
        addJob("pending", "imported", "Já importado na fila", false, false, false, 1);
        addJob("not-queued", "import_failed", "Arquivo pronto; importação pendente", false, true, false, 1);
        addJob("running", "downloading", "Baixando", true, false, false, 0.3);
        shell.library = libraryLedger;
        openDownloads();
        compare(findChild(rowAt(0), "download-destinations").text, "MP3 pronto · Na fila de reprodução · Na biblioteca");
        compare(findChild(rowAt(1), "download-destinations").text, "MP3 pronto · Na fila de reprodução · Biblioteca pendente");
        compare(findChild(rowAt(2), "download-destinations").text, "MP3 pronto · Biblioteca pendente");
        verify(!findChild(rowAt(3), "download-destinations").visible);
        libraryLedger.downloadJobs = ["in-library", "pending"];
        tryCompare(findChild(rowAt(1), "download-destinations"), "text", "MP3 pronto · Na fila de reprodução · Na biblioteca");
        shell.library = null;
        tryCompare(findChild(rowAt(0), "download-destinations"), "text", "MP3 pronto · Na fila de reprodução");
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
        tryCompare(row.informationControl, "activeFocus", true);
        keyClick(Qt.Key_Tab);
        tryVerify(function () {
            return !inside(shell.activeFocusItem, findChild(shell, "downloads-panel").contentItem);
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
        compare(discard.text, "Encerrar recuperação…");
        compare(discard.Accessible.name, discard.text);
        discard.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        compare(downloads.discardedIds.length, 0);
        const popup = findChild(shell, "downloads-discard-confirmation");
        verify(popup);
        tryVerify(function () {
            return popup.opened;
        });
        const advice = visibleText(popup.contentItem);
        verify(advice.includes("MP3 final"));
        verify(advice.includes("temporários ainda presentes"));
        verify(advice.includes("arquivar o pedido"));
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
        clickVisible(discard);
        tryVerify(function () {
            return popup.opened;
        });
        const confirm = findChild(shell, "download-discard-confirm");
        verifyTarget(confirm);
        compare(confirm.text, "Encerrar recuperação");
        compare(confirm.Accessible.name, confirm.text);
        clickVisible(confirm);
        compare(downloads.discardedIds.length, 1);
        compare(downloads.discardedIds[0], "partials-a");
        compare(presenter.playbackState, PlayerPresenter.Unloaded);
    }
    function test_plannedRecoveryExplainsThatConfirmationClosesTheRecord() {
        addJob("planned-a", "interrupted", "Interrompido", false, false, false, 0);
        jobs.setProperty(0, "canDiscardPartials", true);
        jobs.setProperty(0, "recoveryWithoutFiles", true);
        openDownloads();
        const discard = findChild(rowAt(0), "download-discard-partials");
        discard.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        const popup = findChild(shell, "downloads-discard-confirmation");
        const cancel = findChild(shell, "download-discard-cancel");
        const confirm = findChild(shell, "download-discard-confirm");
        tryCompare(popup, "opened", true);
        tryCompare(cancel, "activeFocus", true);
        compare(findChild(shell, "download-discard-advice").text, "A verificação confirmou a ausência dos arquivos locais esperados. O RC Music fará uma nova verificação antes de registrar o encerramento. Depois de encerrar todos os itens, você poderá arquivar o pedido.");
        compare(downloads.discardedIds.length, 0);
        keyClick(Qt.Key_Tab);
        tryCompare(confirm, "activeFocus", true);
        keyClick(Qt.Key_Return);
        tryCompare(popup, "visible", false);
        compare(downloads.discardedIds.length, 1);
        compare(downloads.discardedIds[0], "planned-a");
        compare(downloads.archives, 0);
        compare(presenter.playbackState, PlayerPresenter.Unloaded);
    }
    function test_discardConsentCannotOpenAfterSynchronousCloseOrOwnerReplacement(data) {
        addJob("planned-a", "interrupted", "Interrompido", false, false, false, 0);
        jobs.setProperty(0, "canDiscardPartials", true);
        jobs.setProperty(0, "recoveryWithoutFiles", true);
        const panel = openDownloads();
        const original = downloads;
        const replacement = data.replaceOwner ? createTemporaryObject(bindingComponent, testCase) : null;
        if (replacement)
            verify(replacement.requestDiscardPartials("planned-a"));
        original.onDiscardRequest = function () {
            if (replacement) {
                downloads = replacement;
                shell.downloads = replacement;
            } else {
                panel.close();
            }
        };
        clickVisible(findChild(rowAt(0), "download-discard-partials"));
        verify(!findChild(shell, "downloads-discard-confirmation").visible);
        compare(original.discardConsentToken, "");
        compare(original.discardedIds.length, 0);
        if (replacement) {
            compare(replacement.discardConsentToken, "1");
            compare(replacement.discardedIds.length, 0);
        }
    }
    function test_discardConsentCannotOpenAfterSynchronousCloseOrOwnerReplacement_data() {
        return [
            {
                tag: "closed",
                replaceOwner: false
            },
            {
                tag: "replaced",
                replaceOwner: true
            }
        ];
    }
    function test_partialDiscardConsentCannotFollowAReplacedOwnerOrRevokedRevision() {
        addJob("partials-a", "interrupted", "Interrompido", false, false, false, 0);
        jobs.setProperty(0, "canDiscardPartials", true);
        openDownloads();
        clickVisible(findChild(rowAt(0), "download-discard-partials"));
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
        clickVisible(findChild(rowAt(0), "download-discard-partials"));
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
        clickVisible(cancel);
        compare(jobs.cancelledIds.length, 1);
        compare(jobs.cancelledIds[0], "cancel-a");
        jobs.remove(0);
        const second = rowAt(0);
        const retry = findChild(second, "download-retry-import");
        verifyTarget(retry);
        clickVisible(retry);
        compare(jobs.retriedIds.length, 1);
        compare(jobs.retriedIds[0], "retry-b");
        jobs.setProperty(0, "canRetryImport", false);
        tryVerify(function () {
            return !retry.visible || !retry.enabled;
        });
    }
    function test_pageFocusKeyboardAndReturnDoNotAffectPlayback() {
        driver.enqueueLocalSelection(["C:/Music/A.mp3", "C:/Music/B.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        addJob("active", "downloading", "Baixando", true, false, true, 0);
        const panel = openDownloads();
        verify(!panel.modal);
        compare(panel.contentItem.Accessible.role, Accessible.Pane);
        verify(!shell.windowShortcutsEnabled);
        verify(!findChild(shell, "source-drop-area").enabled);
        const close = findChild(shell, "downloads-close");
        tryVerify(function () {
            return close.activeFocus;
        });
        verify(close.background.border.width >= 3);
        let leftPage = false;
        for (let index = 0; index < 40; ++index) {
            keyClick(Qt.Key_Tab);
            if (!inside(shell.activeFocusItem, panel.contentItem)) {
                leftPage = true;
                break;
            }
        }
        verify(leftPage, "Tab must reach the persistent player instead of looping in a page");
        close.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Backtab);
        tryCompare(findChild(shell, "downloads"), "activeFocus", true);
        const list = findChild(shell, "downloads-list");
        // ListView is a focus scope; re-enter its navigation item rather than
        // restoring the last focused row action (the same target as the header).
        (list.currentItem || list).forceActiveFocus(Qt.TabFocusReason);
        tryVerify(() => shell.activeFocusItem === list || shell.activeFocusItem === list.currentItem);
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
    function test_keyboardShowsWhichVisibleDownloadOwnsItsActions() {
        addJob("cancel-first", "queued", "Na fila", true, false, true, 0);
        addJob("retry-second", "import_failed", "Importação pendente", false, true, false, 1);
        openDownloads();
        const list = findChild(shell, "downloads-list");
        keyClick(Qt.Key_Tab);
        tryCompare(findChild(shell, "downloads-open-folder"), "activeFocus", true);
        keyClick(Qt.Key_Tab);
        tryCompare(list, "activeFocus", true);
        const first = rowAt(0);
        // Unfocused rows: no border on the Aurora canvas, a 1 px card in the legacy dialog.
        const restingBorder = findChild(shell, "downloads-panel").embedded ? 0 : 1;
        const second = rowAt(1);
        compare(first.children[0].border.width, 3);
        compare(second.children[0].border.width, restingBorder);
        compare(first.Accessible.role, Accessible.ListItem);
        compare(first.Accessible.focused, true);
        compare(first.Accessible.name, "Áudio cancel-first");
        compare(second.Accessible.focused, false);

        keyClick(Qt.Key_Down);
        tryCompare(list, "currentIndex", 1);
        compare(first.children[0].border.width, restingBorder);
        compare(second.children[0].border.width, 3);
        compare(first.Accessible.focused, false);
        compare(second.Accessible.focused, true);
        compare(second.Accessible.name, "Áudio retry-second");
        keyClick(Qt.Key_Tab);
        tryCompare(second.retryControl, "activeFocus", true);
        compare(second.Accessible.focused, false);
        keyClick(Qt.Key_Home);
        compare(list.currentIndex, 1);
        keyClick(Qt.Key_Return);
        compare(jobs.cancelledIds.length, 0);
        compare(jobs.retriedIds, ["retry-second"]);
    }
    function test_keyboardHomeAndEndReachDownloadsOutsideTheVisibleRows() {
        const count = 40;
        for (let index = 0; index < count; ++index)
            addJob("job-" + index, "queued", "Na fila", true, false, true, 0);
        openDownloads();
        const list = findChild(shell, "downloads-list");
        keyClick(Qt.Key_Tab);
        tryCompare(findChild(shell, "downloads-open-folder"), "activeFocus", true);
        keyClick(Qt.Key_Tab);
        tryCompare(list, "activeFocus", true);
        verify(list.itemAtIndex(count - 1) === null);
        keyClick(Qt.Key_End);
        tryCompare(list, "currentIndex", count - 1);
        tryVerify(function () {
            const row = list.itemAtIndex(count - 1);
            return row && row.mapToItem(list, 0, row.height).y <= list.height + 1;
        });
        compare(jobs.cancelledIds.length, 0);
        const last = list.currentItem;
        keyClick(Qt.Key_Tab);
        tryCompare(last.cancelControl, "activeFocus", true);
        keyClick(Qt.Key_Return);
        compare(jobs.cancelledIds, ["job-39"]);
        keyClick(Qt.Key_Backtab);
        tryCompare(shell, "activeFocusItem", last);
        keyClick(Qt.Key_Home);
        tryCompare(list, "currentIndex", 0);
        tryVerify(function () {
            const row = list.itemAtIndex(0);
            return row && row.mapToItem(list, 0, 0).y >= -1;
        });
        compare(jobs.cancelledIds, ["job-39"]);
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
        clickVisible(startup);
        clickVisible(admission);
        clickVisible(finalSave);
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
    function test_historyRecoveryUsesOnlyItsLiveCapability() {
        downloads.canRenewNamespace = true;
        downloads.warningText = "Os dados e as confirmações de importação serão preservados.";
        const playback = presenter.playbackState;
        const loads = driver.localLoadCount();
        const panel = openDownloads();
        const recover = findChild(shell, "downloads-renew-namespace");
        verifyTarget(recover);
        compare(recover.text, "Recuperar histórico");
        compare(recover.Accessible.name, recover.text);
        compare(findChild(shell, "downloads-retry-startup").visible, false);
        compare(downloads.namespaceRenewals, 0);
        clickVisible(recover);
        compare(downloads.namespaceRenewals, 1);
        compare(downloads.startupRetries, 0);
        compare(presenter.playbackState, playback);
        compare(driver.localLoadCount(), loads);
        compare(jobs.count, 0);
        verify(panel.visible);
        downloads.canRenewNamespace = false;
        tryVerify(function () {
            return !recover.visible || !recover.enabled;
        });
        recover.clicked();
        compare(downloads.namespaceRenewals, 1, "Revoked recovery must not dispatch another command");
    }
    function test_conflictingReceiptsDoNotExposeAnIneffectiveRecoveryAction() {
        downloads.warningText = "O histórico diverge das confirmações de importação. Os dados foram preservados; revisão necessária.";
        const panel = openDownloads();
        const recover = findChild(shell, "downloads-renew-namespace");
        verify(recover);
        compare(recover.visible, false);
        compare(findChild(shell, "downloads-retry-startup").visible, false);
        compare(findChild(shell, "downloads-warning").textFormat, Text.PlainText);
        compare(findChild(shell, "downloads-warning").text, downloads.warningText);
        recover.clicked();
        compare(downloads.namespaceRenewals, 0);
        compare(downloads.startupRetries, 0);
        verify(panel.visible);
    }
    function test_historyRecoveryFitsMinimumWindowAndKeepsKeyboardFocus(data) {
        driver.setSystemAppearance(data.dark, false);
        driver.setHighContrast(data.contrast, false);
        downloads.canRenewNamespace = true;
        downloads.warningText = "Não foi possível reconciliar o histórico. Selecione Recuperar histórico para verificar novamente. Os dados e as confirmações de importação serão preservados.";
        const panel = openDownloads();
        const recover = findChild(shell, "downloads-renew-namespace");
        verifyTarget(recover);
        verify(panel.width <= 672 && panel.height <= 472);
        const position = recover.mapToItem(shell.contentItem, 0, 0);
        verify(position.x >= 0 && position.y >= 0);
        verify(position.x + recover.width <= shell.width + 0.5);
        verify(position.y + recover.height <= shell.height + 0.5);
        recover.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Tab);
        tryVerify(function () {
            return findChild(shell, "downloads-list").activeFocus;
        });
        keyClick(Qt.Key_Backtab);
        tryVerify(function () {
            return recover.activeFocus;
        });
        verify(!visibleText(panel.contentItem).includes("namespace"));
        compare(downloads.namespaceRenewals, 0);
    }
    function test_historyRecoveryFitsMinimumWindowAndKeepsKeyboardFocus_data() {
        return test_smallWindowUsesThemeAndKeepsControlsVisible_data();
    }
    function test_revokedCloseCapabilityCannotReuseConsent() {
        downloads.canCloseAfterFinalSaveFailure = true;
        openDownloads();
        clickVisible(findChild(shell, "downloads-close-without-save"));
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
        // Inline Aurora pages sit on the canvas; the legacy dialog keeps its raised card.
        verify(Qt.colorEqual(panel.background.color, panel.embedded ? shell.uiContract.canvasColor : shell.uiContract.surfaceRaisedColor));
        compare(panel.background.color.a, 1);
        verify(Qt.colorEqual(findChild(rowAt(0), "download-title").color, shell.uiContract.textPrimaryColor));
        verifyTarget(findChild(shell, "downloads-close"));
        verifyTarget(findChild(rowAt(0), "download-cancel"));
        if (data.contrast)
            verify(panel.embedded ? panel.background.border.width === 0 : panel.background.border.width >= 2);
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
        clickVisible(release);
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
        tryCompare(row.informationControl, "activeFocus", true);
        keyClick(Qt.Key_Tab);
        tryVerify(() => !inside(shell.activeFocusItem, findChild(shell, "downloads-panel").contentItem));
        keyClick(Qt.Key_Backtab);
        tryCompare(row.informationControl, "activeFocus", true);
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
        tryCompare(row.informationControl, "activeFocus", true);
        keyClick(Qt.Key_Tab);
        tryVerify(() => !inside(shell.activeFocusItem, findChild(shell, "downloads-panel").contentItem));
        keyClick(Qt.Key_Backtab);
        tryCompare(row.informationControl, "activeFocus", true);
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
    function test_validationActionsWrapAndKeepTheEightControlFocusOrder() {
        addJob("all-actions", "interrupted", "Interrompido", true, true, false, 0);
        jobs.setProperty(0, "canReviewFiles", true);
        jobs.setProperty(0, "canDiscardPartials", true);
        jobs.setProperty(0, "canValidateMedia", true);
        jobs.setProperty(0, "canImportRecovered", true);
        jobs.setProperty(0, "canReleaseValidation", true);
        openDownloads();
        const row = rowAt(0);
        const controls = [row.cancelControl, row.retryControl, row.reviewControl, row.discardControl, row.validateControl, row.importRecoveredControl, row.releaseValidationControl, row.informationControl];
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
        tryVerify(() => !inside(shell.activeFocusItem, findChild(shell, "downloads-panel").contentItem));
        for (let index = controls.length - 1; index >= 0; --index) {
            keyClick(Qt.Key_Backtab);
            tryCompare(controls[index], "activeFocus", true);
        }
        keyClick(Qt.Key_Backtab);
        tryCompare(findChild(shell, "downloads-list"), "activeFocus", true);
        compare(downloads.validatedIds.length, 0);
        compare(downloads.releasedIds.length, 0);
    }
    function test_recoveredImportRequiresSeparateOneShotConsentWithoutAutoplay() {
        addJob("final-a", "interrupted", "Interrompido", false, false, false, 1);
        jobs.setProperty(0, "canImportRecovered", true);
        jobs.setProperty(0, "canReleaseValidation", true);
        jobs.setProperty(0, "validationText", "MP3 validado; ainda não importado.");
        openDownloads();
        const row = rowAt(0);
        const action = row.importRecoveredControl;
        verifyTarget(action);
        compare(action.text, "Adicionar à fila");
        compare(action.Accessible.name, "Adicionar à fila");
        compare(downloads.importedIds.length, 0);
        clickVisible(action);
        const popup = findChild(shell, "download-import-confirmation");
        verify(popup);
        tryVerify(function () {
            return popup.opened;
        });
        const cancel = findChild(shell, "download-import-cancel");
        const confirm = findChild(shell, "download-import-confirm");
        verifyTarget(cancel);
        verifyTarget(confirm);
        tryCompare(cancel, "activeFocus", true);
        compare(downloads.importedIds.length, 0, "Opening consent never imports the audio");
        compare(downloads.releasedIds.length, 0);
        const advice = findChild(shell, "download-import-advice");
        verify(advice);
        compare(advice.textFormat, Text.PlainText);
        verify(advice.text.includes("fila"));
        verify(advice.text.includes("reprodução"));
        verify(!visibleText(popup.contentItem).includes("never-show-me"));
        verify(!visibleText(popup.contentItem).includes("token=secret"));
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !popup.visible;
        });
        compare(downloads.importConsentToken, "");
        compare(downloads.importedIds.length, 0);
        compare(downloads.releasedIds.length, 0, "Cancelling consent keeps the validation retained");
        tryCompare(action, "activeFocus", true);
        clickVisible(action);
        tryVerify(function () {
            return popup.opened;
        });
        const token = downloads.importConsentToken;
        verify(token.length > 0);
        clickVisible(confirm);
        compare(downloads.importedIds.length, 1);
        compare(downloads.importedIds[0], "final-a");
        compare(downloads.importConsentToken, "");
        confirm.clicked();
        verify(!downloads.confirmRecoveredImport("final-a", token));
        compare(downloads.importedIds.length, 1, "A consumed token cannot import twice");
        compare(downloads.validatedIds.length, 0);
        compare(jobs.retriedIds.length, 0);
        compare(jobs.cancelledIds.length, 0);
        compare(presenter.playbackState, PlayerPresenter.Unloaded);
    }
    function test_recoveredImportConsentIsRevokedByLiveCapabilityIdentityTokenAndOwner() {
        addJob("final-a", "interrupted", "Interrompido", false, false, false, 1);
        jobs.setProperty(0, "canImportRecovered", true);
        openDownloads();
        const action = rowAt(0).importRecoveredControl;
        verifyTarget(action);
        clickVisible(action);
        const popup = findChild(shell, "download-import-confirmation");
        verify(popup);
        tryVerify(function () {
            return popup.opened;
        });
        const token = downloads.importConsentToken;
        jobs.setProperty(0, "canImportRecovered", false);
        tryVerify(function () {
            return !popup.visible;
        });
        compare(downloads.importConsentToken, "");
        verify(!downloads.confirmRecoveredImport("final-a", token));
        action.clicked();
        compare(downloads.importedIds.length, 0);
        jobs.setProperty(0, "canImportRecovered", true);
        clickVisible(action);
        tryVerify(function () {
            return popup.opened;
        });
        jobs.setProperty(0, "jobId", "replacement-row");
        tryVerify(function () {
            return !popup.visible;
        });
        compare(downloads.importConsentToken, "");
        action.clicked();
        compare(downloads.importedIds.length, 0);
        jobs.setProperty(0, "jobId", "final-a");
        clickVisible(action);
        tryVerify(function () {
            return popup.opened;
        });
        downloads.cancelRecoveredImportConfirmation(downloads.importConsentJobId, downloads.importConsentToken);
        tryVerify(function () {
            return !popup.visible;
        });
        clickVisible(action);
        tryVerify(function () {
            return popup.opened;
        });
        const previous = downloads;
        const previousToken = previous.importConsentToken;
        const replacement = createTemporaryObject(bindingComponent, testCase);
        verify(replacement);
        downloads = replacement;
        shell.downloads = replacement;
        tryVerify(function () {
            return !popup.visible;
        });
        compare(previous.importConsentToken, "");
        verify(!previous.confirmRecoveredImport("final-a", previousToken));
        action.clicked();
        compare(previous.importedIds.length, 0);
        compare(replacement.importedIds.length, 0);
    }
    function test_recoveredImportIsUnavailableWithoutCapabilityAndDuringPersistence() {
        addJob("final-a", "interrupted", "Interrompido", false, false, false, 1);
        openDownloads();
        const row = rowAt(0);
        verify(row.importRecoveredControl !== undefined);
        verify(!row.importRecoveredControl.visible || !row.importRecoveredControl.enabled);
        row.importRecoveredControl.clicked();
        compare(downloads.importRequests.length, 0);
        jobs.setProperty(0, "state", "importing");
        jobs.setProperty(0, "validationBusy", true);
        jobs.setProperty(0, "validationText", "Aguardando a confirmação da fila; o arquivo continua protegido.");
        jobs.setProperty(0, "canReleaseValidation", false);
        row.importRecoveredControl.clicked();
        row.releaseValidationControl.clicked();
        compare(downloads.importRequests.length, 0);
        compare(downloads.importedIds.length, 0);
        compare(downloads.releasedIds.length, 0);
        verify(visibleText(row).includes("Aguardando a confirmação da fila"));
        verify(!visibleText(row).includes("Já importado"));
        compare(presenter.playbackState, PlayerPresenter.Unloaded);
    }
    function test_recoveredImportConsentDoesNotStackWithOtherDialogs() {
        addJob("all-actions", "interrupted", "Interrompido", false, false, false, 1);
        jobs.setProperty(0, "canImportRecovered", true);
        jobs.setProperty(0, "canDiscardPartials", true);
        downloads.canCloseAfterFinalSaveFailure = true;
        downloads.warningText = "Não foi possível salvar o histórico.";
        const panel = openDownloads();
        const row = rowAt(0);
        clickVisible(row.importRecoveredControl);
        const popup = findChild(shell, "download-import-confirmation");
        verify(popup);
        tryVerify(function () {
            return popup.opened;
        });
        panel.requestDiscardConsent("all-actions", row.discardControl);
        panel.requestCloseWithoutSave();
        verify(!findChild(shell, "downloads-discard-confirmation").visible);
        verify(!findChild(shell, "downloads-exit-confirmation").visible);
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !popup.visible;
        });
        clickVisible(row.discardControl);
        const discardPopup = findChild(shell, "downloads-discard-confirmation");
        tryVerify(function () {
            return discardPopup.opened;
        });
        row.importRecoveredControl.clicked();
        verify(!popup.visible);
        compare(downloads.importedIds.length, 0);
    }
    function test_recoveredImportConsentFitsMinimumWindowAndKeepsKeyboardFocus(data) {
        driver.setSystemAppearance(data.dark, false);
        driver.setHighContrast(data.contrast, false);
        addJob("final-a", "interrupted", "Interrompido", false, false, false, 1);
        jobs.setProperty(0, "canImportRecovered", true);
        jobs.setProperty(0, "canReleaseValidation", true);
        openDownloads();
        const row = rowAt(0);
        const action = row.importRecoveredControl;
        const release = row.releaseValidationControl;
        verifyTarget(action);
        verifyTarget(release);
        action.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Tab);
        tryCompare(release, "activeFocus", true);
        keyClick(Qt.Key_Backtab);
        tryCompare(action, "activeFocus", true);
        keyClick(Qt.Key_Return);
        const popup = findChild(shell, "download-import-confirmation");
        verify(popup);
        tryVerify(function () {
            return popup.opened;
        });
        const cancel = findChild(shell, "download-import-cancel");
        const confirm = findChild(shell, "download-import-confirm");
        tryCompare(cancel, "activeFocus", true);
        keyClick(Qt.Key_Tab);
        tryCompare(confirm, "activeFocus", true);
        keyClick(Qt.Key_Tab);
        tryCompare(cancel, "activeFocus", true);
        verify(waitForPolish(shell));
        verify(popup.x >= 0 && popup.y >= 0);
        verify(popup.x + popup.width <= shell.width + 0.5);
        verify(popup.y + popup.height <= shell.height + 0.5);
        verifyTarget(cancel);
        verifyTarget(confirm);
        verify(!visibleText(popup.contentItem).includes("never-show-me"));
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !popup.visible;
        });
        tryCompare(action, "activeFocus", true);
        compare(downloads.importedIds.length, 0);
    }
    function test_recoveredImportConsentFitsMinimumWindowAndKeepsKeyboardFocus_data() {
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
    QtObject {
        id: libraryLedger
        property bool ready: true
        property bool busy: false
        property int count: 0
        property int visibleCount: 0
        property bool hasPendingChange: false
        property bool canRetrySave: false
        property bool canLocate: false
        property string statusText: ""
        property string filterText: ""
        property var model: null
        property var downloadJobs: ["in-library"]
        function cancelLocate() {
        }
    }
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
            property string archiveConsentToken: ""
            property int archiveRequests: 0
            property int archiveRetries: 0
            property int archives: 0
            property var onArchiveRequest: null
            property bool canArchiveSettled: false
            property bool canCloseAfterFinalSaveFailure: false
            property bool canRetryAdmission: false
            property bool canRetryArchive: false
            property bool canRetryFinalSave: false
            property bool canRetryStartup: false
            property bool canRenewNamespace: false
            property int finalSaveRetries: 0
            property string downloadDirectoryText: "C:/Músicas"
            property bool downloadDirectoryPending: false
            property string downloadDirectoryStatus: ""
            property int folderOpens: 0
            property bool exitObservedDuringAbandonment: false
            property int exitCallsObservedDuringAbandonment: -1
            property var jobsModel: jobs
            property var downloadRetries: []
            property var copiedIds: []
            property bool copyAccepted: true
            property int startupRetries: 0
            property int namespaceRenewals: 0
            property var reviewedIds: []
            property var discardedIds: []
            property var validatedIds: []
            property var releasedIds: []
            property var importedIds: []
            property var importRequests: []
            property string importConsentJobId: ""
            property string importConsentToken: ""
            property int importRevision: 0
            property string discardConsentJobId: ""
            property string discardConsentToken: ""
            property bool discardConsentWithoutFiles: false
            property int discardRevision: 0
            property var onDiscardRequest: null
            property string historyCapacityText: ""
            property string statusText: "Downloads disponíveis"
            property int unsafeCloses: 0
            property string warningText: ""

            function retryDownload(jobId) {
                downloadRetries = downloadRetries.concat([jobId]);
                return true;
            }
            function downloadInformation(jobId) {
                for (let index = 0; index < jobs.count; ++index) {
                    if (jobs.get(index).jobId === jobId)
                        return "Título: <b>Áudio literal</b>\nID: " + jobId + "\nEstado: failed\nFalha: no-audio\nUnidades na fase: 9007199254740993\nRecuperação: nenhuma";
                }
                return "";
            }
            function copyDownloadInformation(jobId) {
                copiedIds = copiedIds.concat([jobId]);
                return copyAccepted;
            }
            function openDownloadDirectory() {
                ++folderOpens;
                return true;
            }
            function closeAfterFinalSaveFailure() {
                ++unsafeCloses;
                exitObservedDuringAbandonment = testCase.presenter.exiting;
                exitCallsObservedDuringAbandonment = testCase.scenario.localExitCalls();
                return acceptHistoryAbandonment;
            }
            function retryAdmission() {
                ++admissionRetries;
            }
            function requestArchiveConfirmation() {
                archiveConsentToken = "archive-" + ++archiveRequests;
                if (onArchiveRequest)
                    onArchiveRequest();
                return true;
            }
            function cancelArchiveConfirmation(token) {
                if (archiveConsentToken === token)
                    archiveConsentToken = "";
            }
            function confirmArchive(token) {
                if (!canArchiveSettled || archiveConsentToken !== token)
                    return false;
                ++archives;
                archiveConsentToken = "";
                canArchiveSettled = false;
                return true;
            }
            function retryArchive() {
                ++archiveRetries;
                canRetryArchive = false;
                return true;
            }
            function retryFinalSave() {
                ++finalSaveRetries;
            }
            function retryStartup() {
                ++startupRetries;
            }
            function renewNamespace() {
                ++namespaceRenewals;
                return true;
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
            function requestRecoveredImport(jobId) {
                importRequests = importRequests.concat([jobId]);
                importConsentJobId = jobId;
                importConsentToken = String(++importRevision);
                return true;
            }
            function confirmRecoveredImport(jobId, token) {
                if (jobId !== importConsentJobId || token !== importConsentToken || token.length === 0)
                    return false;
                importConsentJobId = "";
                importConsentToken = "";
                importedIds = importedIds.concat([jobId]);
                return true;
            }
            function cancelRecoveredImportConfirmation(jobId, token) {
                if (jobId === importConsentJobId && token === importConsentToken) {
                    importConsentJobId = "";
                    importConsentToken = "";
                }
            }
            function requestDiscardPartials(jobId) {
                discardConsentWithoutFiles = false;
                for (let index = 0; index < jobs.count; ++index) {
                    const job = jobs.get(index);
                    if (job.jobId === jobId) {
                        discardConsentWithoutFiles = job.recoveryWithoutFiles;
                        break;
                    }
                }
                discardConsentJobId = jobId;
                discardConsentToken = String(++discardRevision);
                if (onDiscardRequest)
                    onDiscardRequest();
                return true;
            }
            function confirmDiscardPartials(jobId, token) {
                if (jobId !== discardConsentJobId || token !== discardConsentToken || token.length === 0)
                    return false;
                discardConsentJobId = "";
                discardConsentToken = "";
                discardConsentWithoutFiles = false;
                discardedIds = discardedIds.concat([jobId]);
                return true;
            }
            function cancelDiscardPartialsConfirmation(jobId, token) {
                if (jobId === discardConsentJobId && token === discardConsentToken) {
                    discardConsentJobId = "";
                    discardConsentToken = "";
                    discardConsentWithoutFiles = false;
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
