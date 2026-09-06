import QtQml

QtObject {
    readonly property string loadingAudio: qsTr("Carregando áudio.")
    readonly property string seekRejected: qsTr("Não foi possível alterar a posição. A reprodução voltou para a posição anterior.")
    readonly property string skippedDuringImport: qsTr("Uma ou mais faixas foram puladas por estarem indisponíveis. Consulte os detalhes na fila.")

    function trackLoaded(title) {
        return qsTr("Faixa carregada: %1.").arg(title);
    }

    function terminalError(title, message, action) {
        return qsTr("%1. %2 Ação disponível: %3.").arg(title).arg(message).arg(action);
    }

    function automaticallySkipped(title, reason) {
        // Substitute only template tokens, never percent markers in a title.
        return qsTr("Faixa pulada: %1. %2 Carregando a próxima faixa. Consulte os detalhes na fila.").replace(/%[12]/g, function (token) {
            return token === "%1" ? title : reason;
        });
    }
}
