import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "ErrorMicrocopy"
    when: windowShown

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testLocalPlayerPresenter // qmllint disable unqualified
        }
    }
    ErrorText {
        id: expandedLabels
    }
    QueueText {
        id: queueLabels
    }
    Component {
        id: expandedErrorHarnessComponent
        Item {
            width: 320
            height: 520
            property alias errorState: expandedErrorState
            ErrorState {
                id: expandedErrorState
                anchors.centerIn: parent
                contract: testShellUiContract // qmllint disable unqualified
                presenter: testLocalPlayerPresenter // qmllint disable unqualified
                labels: expandedLabels
                titleText: "⟦Não foi possível reproduzir esta faixa enquanto o aplicativo preserva todo o contexto necessário para você decidir o próximo passo⟧"
                bodyText: "⟦O arquivo selecionado não pôde ser reproduzido. Esta mensagem deliberadamente expandida comprova que uma tradução mais longa continua legível, centralizada e totalmente contida na janela mínima do aplicativo.⟧"
                actionText: "⟦Abrir outro áudio para continuar⟧"
            }
        }
    }
    Component {
        id: expandedEmptyHarnessComponent
        Item {
            width: 720
            height: 520
            property alias emptyState: expandedEmptyState
            EmptyState {
                id: expandedEmptyState
                anchors.centerIn: parent
                width: Math.min(parent.width, 560)
                contract: testShellUiContract // qmllint disable unqualified
                titleText: "⟦Abra um arquivo de áudio local para começar a ouvir sua biblioteca pessoal neste computador⟧"
                hintText: "⟦Você também pode arrastar para esta janela vários arquivos ou uma pasta inteira contendo músicas compatíveis⟧"
                actionText: "⟦Escolher agora outro arquivo de áudio local disponível neste computador para iniciar a reprodução⟧"
            }
        }
    }

    property var shell

    function init() {
        testScenarioDriver.resetLocalScenario(); // qmllint disable unqualified
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
    }

    function cleanup() {
        if (shell)
            shell.destroy();
        shell = null;
        testScenarioDriver.resetLocalScenario(); // qmllint disable unqualified
    }

    function enterInvalidMediaError() {
        testScenarioDriver.enqueueLocalSelection(["C:/Música/Falha contextual 日本語.mp3"]); // qmllint disable unqualified
        verify(waitForPolish(shell));
        const openAudio = findChild(shell, "open-audio");
        verify(openAudio && openAudio.visible && openAudio.enabled);
        verify(openAudio.width > 0 && openAudio.height > 0);
        openAudio.Accessible.pressAction();
        tryVerify(function () {
            return testLocalPlayerPresenter.queueLoaded; // qmllint disable unqualified
        });
        testScenarioDriver.publishLocalError(); // qmllint disable unqualified
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Error); // qmllint disable unqualified
    }

    function verifyInside(container, item) {
        const position = item.mapToItem(container, 0, 0);
        verify(item.visible);
        verify(item.width > 0);
        verify(item.height > 0);
        verify(position.x >= 0);
        verify(position.y >= 0);
        verify(position.x + item.width <= container.width + 0.5);
        verify(position.y + item.height <= container.height + 0.5);
    }

    function test_realInvalidMediaKeepsContextAndOffersAnExecutableLocalizedAction() {
        enterInvalidMediaError();
        compare(testLocalPlayerPresenter.errorCode, "invalid_media"); // qmllint disable unqualified
        compare(testLocalPlayerPresenter.errorMessageKey, "invalid_media_error"); // qmllint disable unqualified
        compare(testLocalPlayerPresenter.errorActionId, "open-audio"); // qmllint disable unqualified

        const errorState = findChild(shell, "error-state");
        const title = findChild(shell, "error-title");
        const body = findChild(shell, "error-body");
        const context = findChild(shell, "error-context-title");
        const action = findChild(shell, "error-open-audio");
        const details = findChild(shell, "track-details-error");
        verify(errorState.visible);
        compare(title.text, "Não foi possível reproduzir esta faixa");
        compare(body.text, "O arquivo pode estar corrompido ou em um formato que o RC Music não reconhece. Abra outro áudio.");
        verify(body.visible);
        compare(context.text, "Falha contextual 日本語");
        compare(action.text, "Abrir outro áudio");
        compare(action.Accessible.name, "Abrir outro áudio");
        compare(errorState.Accessible.name, "Não foi possível reproduzir esta faixa. O arquivo pode estar corrompido ou em um formato que o RC Music não reconhece. Abra outro áudio.");
        verify(details.enabled);
        verify(testLocalPlayerPresenter.detailsAvailable); // qmllint disable unqualified
        verify(title.text.indexOf("invalid_media") < 0);
        verify(body.text.indexOf("invalid_media") < 0);
        verify(title.text.indexOf("C:/Música") < 0);
        verify(body.text.indexOf("C:/Música") < 0);

        testScenarioDriver.enqueueLocalSelection(["C:/Música/Recuperada pela ação ☕.flac"]); // qmllint disable unqualified
        action.Accessible.pressAction();
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Loading); // qmllint disable unqualified
        compare(testScenarioDriver.localLoadCount(), 2); // qmllint disable unqualified
        compare(testLocalPlayerPresenter.currentTitle, "Recuperada pela ação ☕"); // qmllint disable unqualified
        verify(!testLocalPlayerPresenter.hasError); // qmllint disable unqualified
    }

    function test_auroraErrorUsesTheEmptyStateLook() {
        enterInvalidMediaError();
        const contract = testShellUiContract; // qmllint disable unqualified
        const state = findChild(shell, "error-state");
        const circle = findChild(state, "error-icon-circle");
        verify(circle && circle.visible);
        const icon = findChild(circle, "empty-icon");
        verify(icon);
        compare(icon.iconName, "warning-circle");
        verify(Qt.colorEqual(icon.color, contract.errorColor));
        const title = findChild(state, "error-title");
        compare(title.font.family, contract.headingFontFamily);
        compare(title.font.pixelSize, contract.typeTitle);
        const body = findChild(state, "error-body");
        compare(body.font.family, contract.bodyFontFamily);
        compare(body.font.pixelSize, contract.typeBody);
        const primary = findChild(state, "error-open-audio");
        const details = findChild(state, "track-details-error");
        compare(primary.kind, AuroraButton.Primary);
        compare(details.kind, AuroraButton.Tonal);
        compare(details.iconName, "info");
        compare(primary.height, 40);
        compare(details.height, 40);
        compare(findChild(primary, "button-background").border.width, 0);
        compare(findChild(details, "button-background").border.width, 0);
    }

    function test_auroraErrorKeepsEveryRecoveryAction() {
        shell.width = 720;
        shell.height = 520;
        testScenarioDriver.enqueueLocalSelection(["C:/Música/Arquivo movido 日本語.mp3"]); // qmllint disable unqualified
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Loading); // qmllint disable unqualified
        testScenarioDriver.publishLocalSourceUnavailable(); // qmllint disable unqualified
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Error); // qmllint disable unqualified
        verify(testLocalPlayerPresenter.canRetryCurrentOccurrence); // qmllint disable unqualified
        verify(testLocalPlayerPresenter.canLocateCurrentOccurrence); // qmllint disable unqualified
        const state = findChild(shell, "error-state");
        const primary = findChild(state, "error-open-audio");
        const retry = findChild(state, "error-retry-occurrence");
        const locate = findChild(state, "error-locate-occurrence");
        compare(retry.text, "Tentar tocar novamente");
        compare(locate.text, "Localizar arquivo…");
        for (const action of [retry, locate]) {
            verify(action.visible);
            compare(action.kind, AuroraButton.Tonal);
        }
        for (const action of [primary, retry, locate, findChild(state, "track-details-error")]) {
            verify(action.visible && action.width > 0 && action.height > 0);
            const position = action.mapToItem(shell.contentItem, 0, 0);
            verify(position.x >= 0 && position.y >= 0);
            verify(position.x + action.width <= shell.width + 0.5);
            verify(position.y + action.height <= shell.height + 0.5);
        }
        primary.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Tab);
        tryVerify(function () {
            return retry.activeFocus;
        });
        keyClick(Qt.Key_Tab);
        tryVerify(function () {
            return locate.activeFocus;
        });
    }

    function test_outputErrorsDescribePausedRecoveryInsteadOfAFileFailure() {
        compare(expandedLabels.titleFor("audio_output_unavailable_error"), "Saída de áudio indisponível");
        compare(expandedLabels.messageFor("audio_output_unavailable_error"), "Conecte uma saída de áudio ou escolha outra nas configurações de som do Windows e tente novamente. A faixa volta pausada.");
        compare(expandedLabels.titleFor("audio_output_changed_error"), "A saída de áudio mudou");
        compare(expandedLabels.messageFor("audio_output_changed_error"), "A reprodução parou. Tente novamente para retomar a faixa pausada, na mesma posição quando possível.");
        for (const key of ["audio_output_unavailable_error", "audio_output_changed_error"]) {
            const message = expandedLabels.messageFor(key, true);
            verify(message.includes("Configurações > Reprodução"));
            verify(message.includes("pausada"));
        }
        compare(expandedLabels.actionFor("recover-audio-output"), "Tentar novamente");
    }

    function test_removedSourceVolumeOffersReconnectionOrFileLocation() {
        compare(expandedLabels.titleFor("source_volume_removed_error"), "A unidade da faixa foi desconectada");
        compare(expandedLabels.messageFor("source_volume_removed_error"), "Reconecte a unidade e tente novamente ou localize o arquivo em outro local.");
        compare(queueLabels.unavailableReason("source_volume_removed"), "Unidade desconectada.");
    }

    function enterOutputError(changed) {
        testScenarioDriver.enqueueLocalSelection(["C:/Música/Saída interrompida 日本語.flac", "C:/Música/Próxima faixa.mp3"]); // qmllint disable unqualified
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Loading); // qmllint disable unqualified
        testScenarioDriver.publishLocalReady(); // qmllint disable unqualified
        testScenarioDriver.publishLocalPlaying(); // qmllint disable unqualified
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Playing); // qmllint disable unqualified
        testScenarioDriver.publishLocalOutputError(changed); // qmllint disable unqualified
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Error); // qmllint disable unqualified
    }

    function test_outputRecoveryKeepsTheAvailableOccurrenceAndOffersPausedRetry_data() {
        return [
            {
                tag: "unavailable-keyboard",
                changed: false,
                accessible: false
            },
            {
                tag: "unavailable-accessible",
                changed: false,
                accessible: true
            },
            {
                tag: "changed-keyboard",
                changed: true,
                accessible: false
            },
            {
                tag: "changed-accessible",
                changed: true,
                accessible: true
            }
        ];
    }

    function test_outputRecoveryKeepsTheAvailableOccurrenceAndOffersPausedRetry(data) {
        enterOutputError(data.changed);
        shell.requestActivate();
        tryVerify(function () {
            return shell.active;
        });
        const occurrenceId = testLocalPlayerPresenter.currentOccurrenceId; // qmllint disable unqualified
        const playsBeforeRecovery = testScenarioDriver.localPlayCount(); // qmllint disable unqualified
        compare(testLocalPlayerPresenter.errorCode, data.changed ? "audio_output_changed" : "audio_output_unavailable"); // qmllint disable unqualified
        compare(testLocalPlayerPresenter.errorActionId, "recover-audio-output"); // qmllint disable unqualified
        verify(!testLocalPlayerPresenter.canRetryCurrentOccurrence); // qmllint disable unqualified
        compare(testScenarioDriver.localLoadCount(), 1); // qmllint disable unqualified

        const state = findChild(shell, "error-state");
        const recovery = findChild(state, "error-recover-audio-output");
        const openAnother = findChild(state, "error-open-another-audio");
        const retryFile = findChild(state, "error-retry-occurrence");
        const details = findChild(state, "track-details-error");
        verify(recovery && recovery.visible && recovery.enabled);
        compare(recovery.Accessible.role, Accessible.Button);
        compare(recovery.Accessible.name, "Tentar novamente");
        verify(recovery.Accessible.description.includes("pausada"));
        verify(recovery.Accessible.description.includes("A posição volta quando possível"));
        compare(findChild(state, "error-title").text, data.changed ? "A saída de áudio mudou" : "Saída de áudio indisponível");
        compare(findChild(state, "error-context-title").text, "Saída interrompida 日本語");
        const body = findChild(state, "error-body").text;
        verify(body.includes("pausada"));
        verify(body.includes("Configurações > Reprodução"));
        tryVerify(function () {
            return testScenarioDriver.lastAccessibleAnnouncement().includes("Configurações > Reprodução"); // qmllint disable unqualified
        });
        verify(!retryFile.visible);
        verify(openAnother.visible && openAnother.enabled);
        compare(openAnother.Accessible.name, "Abrir outro áudio");
        verify(details.enabled);

        // Abrir áudio keeps the focus it had when the file was chosen, so the drawer's opener is set explicitly.
        findChild(shell, "player-queue").forceActiveFocus(Qt.TabFocusReason);
        shell.setQueueOpen(true);
        const queue = findChild(shell, "queue-list");
        tryVerify(function () {
            return queue.visible && queue.count === 2 && queue.currentItem !== null;
        });
        compare(queue.currentItem.occurrenceId, occurrenceId);
        verify(queue.currentItem.available);
        shell.setQueueOpen(false);
        tryVerify(function () {
            return !queue.visible && findChild(shell, "player-queue").activeFocus;
        });
        recovery.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Tab);
        tryVerify(function () {
            return openAnother.activeFocus;
        });
        keyClick(Qt.Key_Backtab);
        tryVerify(function () {
            return recovery.activeFocus;
        });

        if (data.accessible)
            recovery.Accessible.pressAction();
        else
            keyClick(Qt.Key_Return);
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Loading); // qmllint disable unqualified
        compare(testLocalPlayerPresenter.currentOccurrenceId, occurrenceId); // qmllint disable unqualified
        compare(testScenarioDriver.localLoadCount(), 2); // qmllint disable unqualified
        compare(testScenarioDriver.localLoadedPath(1), "C:/Música/Saída interrompida 日本語.flac"); // qmllint disable unqualified
        recovery.Accessible.pressAction();
        compare(testScenarioDriver.localLoadCount(), 2); // qmllint disable unqualified
        testScenarioDriver.publishLocalReady(); // qmllint disable unqualified
        tryVerify(function () {
            return testScenarioDriver.localPauseCount() === 1;
        }); // qmllint disable unqualified
        compare(testScenarioDriver.localPlayCount(), playsBeforeRecovery); // qmllint disable unqualified
        testScenarioDriver.publishLocalPaused(); // qmllint disable unqualified
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Paused); // qmllint disable unqualified
        verify(!state.visible);
        compare(queue.count, 2);
    }

    function test_outputErrorCanOpenAnotherAudioInsteadOfRetryingTheDevice() {
        enterOutputError(false);
        testScenarioDriver.enqueueLocalSelection(["C:/Música/Outra escolha.mp3"]); // qmllint disable unqualified
        const openAnother = findChild(shell, "error-open-another-audio");
        verify(openAnother && openAnother.visible && openAnother.enabled);
        openAnother.Accessible.pressAction();
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Loading); // qmllint disable unqualified
        compare(testScenarioDriver.localLoadCount(), 2); // qmllint disable unqualified
        compare(testScenarioDriver.localLoadedPath(1), "C:/Música/Outra escolha.mp3"); // qmllint disable unqualified
        compare(testLocalPlayerPresenter.currentTitle, "Outra escolha"); // qmllint disable unqualified
    }

    function test_expandedOutputRecoveryKeepsBothActionsInsideTheMinimumWindow() {
        enterOutputError(false);
        const harness = createTemporaryObject(expandedErrorHarnessComponent, shell.contentItem);
        verify(harness);
        const state = harness.errorState;
        const title = findChild(state, "error-title");
        const body = findChild(state, "error-body");
        const recovery = findChild(state, "error-recover-audio-output");
        const openAnother = findChild(state, "error-open-another-audio");
        const details = findChild(state, "track-details-error");
        wait(0);
        verify(state.width <= harness.width);
        verify(state.height <= harness.height);
        verifyInside(harness, title);
        verifyInside(harness, body);
        verifyInside(harness, recovery);
        verifyInside(harness, openAnother);
        verifyInside(harness, details);
        verify(!findChild(state, "error-retry-occurrence").visible);
        verify(findChild(recovery, "button-label").paintedWidth <= findChild(recovery, "button-label").width + 0.5);
        verify(findChild(openAnother, "button-label").paintedWidth <= findChild(openAnother, "button-label").width + 0.5);
        verify(!findChild(recovery, "button-label").truncated);
        verify(!findChild(openAnother, "button-label").truncated);
    }

    function test_retryUnavailableOccurrence_data() {
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

    function test_retryUnavailableOccurrence(data) {
        enterInvalidMediaError();
        shell.requestActivate();
        tryVerify(function () {
            return shell.active;
        });
        const occurrenceId = testLocalPlayerPresenter.currentOccurrenceId; // qmllint disable unqualified
        const openAnother = findChild(shell, "error-open-audio");
        const retry = findChild(shell, "error-retry-occurrence");
        verify(retry && retry.visible && retry.enabled);
        compare(retry.Accessible.role, Accessible.Button);
        compare(retry.Accessible.name, "Tentar tocar novamente");
        verify(retry.Accessible.description.length > 0);
        compare(openAnother.text, "Abrir outro áudio");
        verify(openAnother.enabled);
        openAnother.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Tab);
        tryVerify(function () {
            return retry.activeFocus;
        });

        if (data.accessible)
            retry.Accessible.pressAction();
        else
            keyClick(Qt.Key_Return);
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Loading); // qmllint disable unqualified
        compare(testLocalPlayerPresenter.currentOccurrenceId, occurrenceId); // qmllint disable unqualified
        compare(testLocalPlayerPresenter.retryingOccurrenceId, occurrenceId); // qmllint disable unqualified
        compare(testScenarioDriver.localLoadCount(), 2); // qmllint disable unqualified
        compare(testScenarioDriver.localLoadedPath(1), "C:/Música/Falha contextual 日本語.mp3"); // qmllint disable unqualified
        verify(!retry.enabled);
        retry.Accessible.pressAction();
        compare(testScenarioDriver.localLoadCount(), 2); // qmllint disable unqualified
        tryVerify(function () {
            return findChild(shell, "open-audio").activeFocus;
        });

        testScenarioDriver.publishLocalReady(); // qmllint disable unqualified
        tryCompare(testLocalPlayerPresenter, "retryingOccurrenceId", ""); // qmllint disable unqualified
        testScenarioDriver.publishLocalPlaying(); // qmllint disable unqualified
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Playing); // qmllint disable unqualified
        verify(!retry.visible);
    }

    function test_expandedLocalizedCopyFitsTheMinimumWindow() {
        enterInvalidMediaError();
        const harness = createTemporaryObject(expandedErrorHarnessComponent, shell.contentItem);
        verify(harness);
        const state = harness.errorState;
        const title = findChild(state, "error-title");
        const body = findChild(state, "error-body");
        const action = findChild(state, "error-open-audio");
        const retry = findChild(state, "error-retry-occurrence");
        wait(0);

        verify(state.width <= harness.width);
        verify(state.height <= harness.height);
        verifyInside(harness, title);
        verifyInside(harness, body);
        verifyInside(harness, action);
        verifyInside(harness, retry);
        compare(title.wrapMode, Text.Wrap);
        compare(body.wrapMode, Text.Wrap);
        verify(title.paintedWidth <= title.width + 0.5);
        verify(body.paintedWidth <= body.width + 0.5);
        verify(findChild(action, "button-label").paintedWidth <= findChild(action, "button-label").width + 0.5);
        verify(!findChild(action, "button-label").truncated);
    }

    function test_expandedEmptyCopyFitsTheMinimumWindowWithoutLosingTheAccessibleAction() {
        const harness = createTemporaryObject(expandedEmptyHarnessComponent, shell.contentItem);
        verify(harness);
        const state = harness.emptyState;
        const title = findChild(state, "empty-title");
        const hint = findChild(state, "empty-hint");
        const action = findChild(state, "open-audio");
        wait(0);

        verify(state.width <= 560);
        verify(state.height <= harness.height);
        verifyInside(harness, title);
        verifyInside(harness, hint);
        verifyInside(harness, action);
        compare(title.wrapMode, Text.Wrap);
        compare(hint.wrapMode, Text.Wrap);
        verify(title.paintedWidth <= title.width + 0.5);
        verify(hint.paintedWidth <= hint.width + 0.5);
        verify(action.contentItem.paintedWidth <= action.contentItem.width + 0.5);
        verify(!action.contentItem.truncated);
        compare(action.Accessible.name, state.actionText);
    }
}
