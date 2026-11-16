import QtQml

// Strings of the online search page (ADR-0019).
QtObject {
    readonly property string placeholder: qsTr("Música, artista ou álbum…")
    readonly property string fieldName: qsTr("Buscar no YouTube")
    readonly property string submit: qsTr("Buscar")
    readonly property string cancel: qsTr("Cancelar busca")
    readonly property string resultsName: qsTr("Resultados da busca")
    readonly property string download: qsTr("Baixar")
    readonly property string downloadName: qsTr("Baixar o áudio de %1")
    readonly property string requested: qsTr("Pedido")
    readonly property string preview: qsTr("Abrir no YouTube")
    readonly property string pasteLinks: qsTr("Colar links")
    readonly property string linksField: qsTr("Links do YouTube e do Letras")
    readonly property string linksPlaceholder: qsTr("Cole um link do YouTube por linha. Um link do Letras na mesma linha ou na linha seguinte leva a letra junto.")
    readonly property string linksSubmit: qsTr("Consultar links")
    readonly property string downloadAll: qsTr("Baixar todos")
    readonly property string downloadAllName: qsTr("Baixar todos os links listados")
    readonly property string previewName: qsTr("Abrir %1 no YouTube")
    readonly property string requestedName: qsTr("%1 já foi para os downloads")
    readonly property string unknownChannel: qsTr("Canal desconhecido")
    readonly property string unavailable: qsTr("A busca online fica disponível quando o componente de downloads do YouTube está ativo.")
}
