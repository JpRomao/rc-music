import QtQml

QtObject {
    readonly property string sourceUnavailableTitle: qsTr("Arquivo não encontrado")
    readonly property string sourceUnavailableMessage: qsTr("A faixa não está mais disponível neste local.")
    readonly property string sourceVolumeRemovedTitle: qsTr("A unidade da faixa foi desconectada")
    readonly property string sourceVolumeRemovedMessage: qsTr("Reconecte a unidade e tente novamente ou localize o arquivo em outro local.")
    readonly property string invalidMediaTitle: qsTr("Não foi possível reproduzir esta faixa")
    readonly property string invalidMediaMessage: qsTr("O arquivo está corrompido ou usa um formato não reconhecido.")
    readonly property string accessDeniedTitle: qsTr("Acesso ao arquivo negado")
    readonly property string accessDeniedMessage: qsTr("Verifique as permissões do arquivo ou escolha outro áudio.")
    readonly property string backendFailureTitle: qsTr("A reprodução encontrou um problema")
    readonly property string backendFailureMessage: qsTr("Escolha outro áudio para continuar.")
    readonly property string audioOutputUnavailableTitle: qsTr("Saída de áudio indisponível")
    readonly property string audioOutputUnavailableMessage: qsTr("Conecte uma saída de áudio ou selecione outra nas configurações de som do Windows. Depois, tente novamente. A faixa será recuperada pausada.")
    readonly property string audioOutputUnavailableInAppMessage: qsTr("Reconecte a saída de áudio e tente novamente, ou escolha outra em Preferências > Reprodução. A faixa será recuperada pausada.")
    readonly property string audioOutputChangedTitle: qsTr("A saída de áudio mudou")
    readonly property string audioOutputChangedMessage: qsTr("A saída de áudio mudou e a reprodução foi interrompida. Tente novamente para recuperar esta faixa pausada. A posição será restaurada quando o arquivo permitir.")
    readonly property string audioOutputChangedInAppMessage: qsTr("A saída de áudio mudou e a reprodução foi interrompida. Tente novamente ou escolha outra saída em Preferências > Reprodução. A faixa será recuperada pausada. A posição será restaurada quando o arquivo permitir.")
    readonly property string unknownErrorTitle: qsTr("Não foi possível reproduzir esta faixa")
    readonly property string unknownErrorMessage: qsTr("Escolha outro áudio para continuar.")
    readonly property string openAudioAction: qsTr("Abrir outro áudio")
    readonly property string retryAction: qsTr("Tentar reproduzir novamente")
    readonly property string retryDescription: qsTr("Tenta abrir o mesmo arquivo novamente, preservando sua posição na fila.")
    readonly property string locateAction: qsTr("Localizar arquivo…")
    readonly property string locateDescription: qsTr("Localiza o arquivo e recupera esta faixa pausada, preservando sua posição na fila. A posição no áudio será restaurada quando o arquivo permitir.")
    readonly property string locationFailed: qsTr("Não foi possível localizar o arquivo. Tente novamente.")
    readonly property string recoverOutputAction: qsTr("Tentar novamente")
    readonly property string recoverOutputDescription: qsTr("Reconecta a saída de áudio e recupera esta faixa pausada, preservando a fila. A posição será restaurada quando o arquivo permitir.")

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
