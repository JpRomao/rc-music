import QtQuick

QtObject {
    readonly property string close: qsTr("Fechar preferências")
    readonly property string closeBehavior: qsTr("Ao fechar")
    readonly property string minimizeToTray: qsTr("Minimizar para bandeja")
    readonly property string exitApp: qsTr("Encerrar")
    readonly property string restoreSession: qsTr("Restaurar última sessão")
    readonly property string startMinimized: qsTr("Iniciar minimizado")
    readonly property string trayRequired: qsTr("Disponível somente com bandeja funcional.")
    readonly property string backwardSeconds: qsTr("Segundos para retroceder")
    readonly property string forwardSeconds: qsTr("Segundos para avançar")
    readonly property string invalidSeconds: qsTr("Use um número inteiro entre 1 e 300.")
    readonly property string queueEnd: qsTr("Ao terminar a fila")
    readonly property string keepSelection: qsTr("Parar e manter seleção")
    readonly property string clearSelection: qsTr("Parar e limpar seleção")
    readonly property string theme: qsTr("Tema")
    readonly property string motion: qsTr("Movimento")
    readonly property string highContrastActive: qsTr("O alto contraste do sistema está ativo. O tema escolhido será usado quando ele for desativado.")
    readonly property string memoryNote: qsTr("Neste experimento, preferências ficam apenas em memória. Sessão e fim da fila reais serão implementados no produto.")
}
