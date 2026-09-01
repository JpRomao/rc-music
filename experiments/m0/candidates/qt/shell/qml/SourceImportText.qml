import QtQuick

QtObject {
    readonly property string dropPrompt: qsTr("Solte para adicionar à fila")
    readonly property string discovering: qsTr("Procurando arquivos de áudio…")
    readonly property string cancelling: qsTr("Cancelando importação…")
    readonly property string completed: qsTr("Importação concluída")
    readonly property string completedWithIssues: qsTr("Importação concluída com itens ignorados")
    readonly property string cancelled: qsTr("Importação cancelada")
    readonly property string rejected: qsTr("Não foi possível importar os itens")
    readonly property string progress: qsTr("Adicionados: {accepted} · Examinados: {inspected} · Ignorados: {issues}")
    readonly property string completedWithIssuesAdvice: qsTr("Alguns itens não eram arquivos de áudio locais válidos ou não puderam ser lidos.")
    readonly property string rejectedAdvice: qsTr("Escolha arquivos ou pastas locais disponíveis.")
    readonly property string cancel: qsTr("Cancelar")
    readonly property string dismiss: qsTr("Fechar resumo da importação")
    readonly property string recover: qsTr("Escolher outros arquivos")
    readonly property string completedAnnouncement: qsTr("Importação concluída. Adicionados: {accepted}. Ignorados: {issues}.")
    readonly property string cancelledAnnouncement: qsTr("Importação cancelada. Adicionados: {accepted}. Ignorados: {issues}.")
    readonly property string rejectedAnnouncement: qsTr("A importação não foi iniciada. Ignorados: {issues}.")
}
