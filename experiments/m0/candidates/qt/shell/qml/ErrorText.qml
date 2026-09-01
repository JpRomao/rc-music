import QtQml

QtObject {
    readonly property string sourceUnavailableTitle: qsTr("Arquivo não encontrado")
    readonly property string sourceUnavailableMessage: qsTr("A faixa não está mais disponível neste local.")
    readonly property string invalidMediaTitle: qsTr("Não foi possível reproduzir esta faixa")
    readonly property string invalidMediaMessage: qsTr("O arquivo está corrompido ou usa um formato não reconhecido.")
    readonly property string accessDeniedTitle: qsTr("Acesso ao arquivo negado")
    readonly property string accessDeniedMessage: qsTr("Verifique as permissões do arquivo ou escolha outro áudio.")
    readonly property string backendFailureTitle: qsTr("A reprodução encontrou um problema")
    readonly property string backendFailureMessage: qsTr("Escolha outro áudio para continuar.")
    readonly property string unknownErrorTitle: qsTr("Não foi possível reproduzir esta faixa")
    readonly property string unknownErrorMessage: qsTr("Escolha outro áudio para continuar.")
    readonly property string openAudioAction: qsTr("Abrir outro áudio")

    function titleFor(messageKey) {
        switch (messageKey) {
        case "source_unavailable_error":
            return sourceUnavailableTitle;
        case "invalid_media_error":
            return invalidMediaTitle;
        case "access_denied_error":
            return accessDeniedTitle;
        case "backend_failure_error":
            return backendFailureTitle;
        default:
            return unknownErrorTitle;
        }
    }

    function messageFor(messageKey) {
        switch (messageKey) {
        case "source_unavailable_error":
            return sourceUnavailableMessage;
        case "invalid_media_error":
            return invalidMediaMessage;
        case "access_denied_error":
            return accessDeniedMessage;
        case "backend_failure_error":
            return backendFailureMessage;
        default:
            return unknownErrorMessage;
        }
    }

    function actionFor(actionId) {
        switch (actionId) {
        case "open-audio":
            return openAudioAction;
        default:
            return openAudioAction;
        }
    }
}
