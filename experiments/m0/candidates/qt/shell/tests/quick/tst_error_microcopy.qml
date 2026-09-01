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
        compare(body.text, "O arquivo está corrompido ou usa um formato não reconhecido.");
        verify(body.visible);
        compare(context.text, "Falha contextual 日本語");
        compare(action.text, "Abrir outro áudio");
        compare(action.Accessible.name, "Abrir outro áudio");
        compare(errorState.Accessible.name, "Não foi possível reproduzir esta faixa. O arquivo está corrompido ou usa um formato não reconhecido.");
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

    function test_expandedLocalizedCopyFitsTheMinimumWindow() {
        enterInvalidMediaError();
        const harness = createTemporaryObject(expandedErrorHarnessComponent, shell.contentItem);
        verify(harness);
        const state = harness.errorState;
        const title = findChild(state, "error-title");
        const body = findChild(state, "error-body");
        const action = findChild(state, "error-open-audio");
        wait(0);

        verify(state.width <= harness.width);
        verify(state.height <= harness.height);
        verifyInside(harness, title);
        verifyInside(harness, body);
        verifyInside(harness, action);
        compare(title.wrapMode, Text.Wrap);
        compare(body.wrapMode, Text.Wrap);
        verify(title.paintedWidth <= title.width + 0.5);
        verify(body.paintedWidth <= body.width + 0.5);
        verify(action.contentItem.paintedWidth <= action.contentItem.width + 0.5);
        verify(!action.contentItem.truncated);
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
