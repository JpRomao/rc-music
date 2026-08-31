import QtQuick

QtObject {
    readonly property string dropPrompt: qsTr("Solte para adicionar à fila")
    readonly property string discovering: qsTr("Procurando arquivos de áudio…")
    readonly property string cancelling: qsTr("Cancelando importação…")
    readonly property string completed: qsTr("Importação concluída")
    readonly property string completedWithIssues: qsTr("Importação concluída com itens ignorados")
    readonly property string cancelled: qsTr("Importação cancelada")
    readonly property string rejected: qsTr("Não foi possível iniciar a importação")
    readonly property string progress: qsTr("{accepted} adicionados · {inspected} examinados · {issues} ignorados")
    readonly property string cancel: qsTr("Cancelar")
    readonly property string dismiss: qsTr("Fechar")
    readonly property string completedAnnouncement: qsTr("Importação concluída: {accepted} adicionados e {issues} ignorados.")
    readonly property string cancelledAnnouncement: qsTr("Importação cancelada: {accepted} adicionados e {issues} ignorados.")
    readonly property string rejectedAnnouncement: qsTr("Importação rejeitada: {issues} itens ignorados.")
}
