import QtQml

QtObject {
    readonly property string loadingAudio: qsTr("Carregando áudio.")
    readonly property string seekRejected: qsTr("Não foi possível alterar a posição. A reprodução voltou para a posição anterior.")

    function trackLoaded(title) {
        return qsTr("Faixa carregada: %1.").arg(title);
    }

    function terminalError(title, message, action) {
        return qsTr("%1. %2 Ação disponível: %3.").arg(title).arg(message).arg(action);
    }
}
