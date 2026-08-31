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
    readonly property string shortcutMoveUp: qsTr("Alt+↑")
    readonly property string shortcutMoveDown: qsTr("Alt+↓")
    readonly property string shortcutRemove: qsTr("Delete")
}
