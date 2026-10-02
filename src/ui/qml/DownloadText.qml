import QtQuick

QtObject {
    readonly property string cancel: qsTr("Cancelar")
    readonly property string close: qsTr("Fechar downloads")
    readonly property string closeWithoutSave: qsTr("Sair sem salvar o histórico")
    readonly property string closeWithoutSaveAdvice: qsTr("O histórico de downloads não foi salvo. Sair agora pode perder os últimos estados. Os arquivos não serão removidos.")
    readonly property string empty: qsTr("Nenhum download nesta sessão.")
    readonly property string keepOpen: qsTr("Voltar")
    readonly property string cancelDiscard: qsTr("Cancelar")
    readonly property string progress: qsTr("Progresso do download")
    readonly property string discardPartials: qsTr("Descartar temporários")
    readonly property string discardPartialsAdvice: qsTr("Descartar somente os arquivos temporários próprios deste download? Eles serão removidos após nova verificação. O MP3 final, se existir, não será removido. Esta ação não importa áudio nem inicia outro download.")
    readonly property string reviewFiles: qsTr("Verificar arquivos")
    readonly property string reviewingFiles: qsTr("Verificando arquivos")
    readonly property string reviewFilesAgain: qsTr("Reverificar arquivos")
    readonly property string retryAdmission: qsTr("Tentar salvar o pedido novamente")
    readonly property string retryFinalSave: qsTr("Tentar salvar o histórico novamente")
    readonly property string retryImport: qsTr("Tentar importar novamente")
    readonly property string retryStartup: qsTr("Tentar carregar o histórico novamente")
    readonly property string title: qsTr("Downloads")
}
