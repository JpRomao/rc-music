import QtQml

QtObject {
    readonly property string sourceUnavailableTitle: qsTr("Arquivo não encontrado")
    readonly property string sourceUnavailableMessage: qsTr("O arquivo não está mais no lugar de antes. Localize o arquivo ou abra outro áudio.")
    readonly property string sourceVolumeRemovedTitle: qsTr("A unidade da faixa foi desconectada")
    readonly property string sourceVolumeRemovedMessage: qsTr("Reconecte a unidade e tente novamente ou localize o arquivo em outro local.")
    readonly property string invalidMediaTitle: qsTr("Não foi possível reproduzir esta faixa")
    readonly property string invalidMediaMessage: qsTr("O arquivo pode estar corrompido ou em um formato que o RC Music não reconhece. Abra outro áudio.")
    readonly property string accessDeniedTitle: qsTr("Sem permissão para abrir o arquivo")
    readonly property string accessDeniedMessage: qsTr("Verifique as permissões do arquivo ou escolha outro áudio.")
    readonly property string backendFailureTitle: qsTr("Não foi possível continuar tocando")
    readonly property string backendFailureMessage: qsTr("Abra outro áudio para continuar.")
    readonly property string audioOutputUnavailableTitle: qsTr("Saída de áudio indisponível")
    readonly property string audioOutputUnavailableMessage: qsTr("Conecte uma saída de áudio ou escolha outra nas configurações de som do Windows e tente novamente. A faixa volta pausada.")
    readonly property string audioOutputUnavailableInAppMessage: qsTr("Reconecte a saída de áudio e tente novamente, ou escolha outra em Configurações > Reprodução. A faixa volta pausada.")
    readonly property string audioOutputChangedTitle: qsTr("A saída de áudio mudou")
    readonly property string audioOutputChangedMessage: qsTr("A reprodução parou. Tente novamente para retomar a faixa pausada, na mesma posição quando possível.")
    readonly property string audioOutputChangedInAppMessage: qsTr("A reprodução parou. Tente novamente ou escolha outra saída em Configurações > Reprodução. A faixa volta pausada, na mesma posição quando possível.")
    readonly property string unknownErrorTitle: qsTr("Não foi possível reproduzir esta faixa")
    readonly property string unknownErrorMessage: qsTr("Abra outro áudio para continuar.")
    readonly property string openAudioAction: qsTr("Abrir outro áudio")
    readonly property string retryAction: qsTr("Tentar tocar novamente")
    readonly property string retryDescription: qsTr("Abre o mesmo arquivo novamente, sem mudar a fila.")
    readonly property string locateAction: qsTr("Localizar arquivo…")
    readonly property string locateDescription: qsTr("Escolha onde o arquivo está agora. A faixa volta pausada, no mesmo lugar da fila e na mesma posição quando possível.")
    readonly property string locationFailed: qsTr("Não foi possível localizar o arquivo. Tente novamente.")
    readonly property string recoverOutputAction: qsTr("Tentar novamente")
    readonly property string recoverOutputDescription: qsTr("Abre a saída de áudio novamente e retoma esta faixa pausada, sem mudar a fila. A posição volta quando possível.")

    function titleFor(messageKey) {
        switch (messageKey) {
        case "source_unavailable_error":
            return sourceUnavailableTitle;
        case "source_volume_removed_error":
            return sourceVolumeRemovedTitle;
        case "invalid_media_error":
            return invalidMediaTitle;
        case "access_denied_error":
            return accessDeniedTitle;
        case "backend_failure_error":
            return backendFailureTitle;
        case "audio_output_unavailable_error":
            return audioOutputUnavailableTitle;
        case "audio_output_changed_error":
            return audioOutputChangedTitle;
        default:
            return unknownErrorTitle;
        }
    }

    function messageFor(messageKey, canChooseAudioOutput = false) {
        switch (messageKey) {
        case "source_unavailable":
        case "source_unavailable_error":
            return sourceUnavailableMessage;
        case "source_volume_removed":
        case "source_volume_removed_error":
            return sourceVolumeRemovedMessage;
        case "invalid_media":
        case "invalid_media_error":
            return invalidMediaMessage;
        case "access_denied":
        case "access_denied_error":
            return accessDeniedMessage;
        case "backend_failure_error":
            return backendFailureMessage;
        case "audio_output_unavailable_error":
            return canChooseAudioOutput ? audioOutputUnavailableInAppMessage : audioOutputUnavailableMessage;
        case "audio_output_changed_error":
            return canChooseAudioOutput ? audioOutputChangedInAppMessage : audioOutputChangedMessage;
        default:
            return unknownErrorMessage;
        }
    }

    function actionFor(actionId) {
        switch (actionId) {
        case "open-audio":
            return openAudioAction;
        case "recover-audio-output":
            return recoverOutputAction;
        default:
            return openAudioAction;
        }
    }
}
