import QtQuick

QtObject {
    readonly property string add: qsTr("Adicionar arquivos")
    readonly property string moveUp: qsTr("Mover para cima")
    readonly property string moveDown: qsTr("Mover para baixo")
    readonly property string remove: qsTr("Remover")
    readonly property string clear: qsTr("Limpar fila")
    readonly property string playNow: qsTr("Tocar agora")
    readonly property string clearTitle: qsTr("Limpar fila?")
    readonly property string clearOneMessage: qsTr("O item da fila será removido. Esta ação não pode ser desfeita.")
    readonly property string clearManyMessage: qsTr("Os {count} itens da fila serão removidos. Esta ação não pode ser desfeita.")
    readonly property string cancel: qsTr("Cancelar")
    readonly property string movedAnnouncement: qsTr("{title} movida para a posição {position} de {total}.")
    readonly property string positionOfTotal: qsTr("posição {position} de {total}")
    readonly property string sourceUnavailable: qsTr("Arquivo não disponível.")
    readonly property string invalidMedia: qsTr("Arquivo inválido ou não suportado.")
    readonly property string accessDenied: qsTr("Acesso ao arquivo negado.")
    readonly property string openRejected: qsTr("Não foi possível abrir o arquivo.")
    readonly property string unavailable: qsTr("Faixa indisponível.")
    readonly property string shortcutMoveUp: qsTr("Alt+↑")
    readonly property string shortcutMoveDown: qsTr("Alt+↓")
    readonly property string shortcutRemove: qsTr("Delete")

    function unavailableReason(errorCode) {
        switch (errorCode) {
        case "source_unavailable":
        case "fixture_source_unavailable":
            return sourceUnavailable;
        case "invalid_media":
            return invalidMedia;
        case "access_denied":
            return accessDenied;
        case "open_rejected":
            return openRejected;
        default:
            return unavailable;
        }
    }
}
