import QtQuick
import MusicPlayer.M0

// Círculo de 64 px com um ícone: a peça de cima dos estados vazio e de erro.
Rectangle {
    id: root

    required property ShellUiContract contract
    property string iconName
    property color iconColor: contract.highContrast ? contract.textPrimaryColor : contract.accentColor

    objectName: "empty-icon-circle"
    implicitWidth: 64
    implicitHeight: 64
    radius: 32
    color: contract.highContrast ? contract.canvasColor : contract.selectionColor
    border.width: contract.highContrast ? 2 : 0
    border.color: contract.textPrimaryColor

    ShellIcon {
        objectName: "empty-icon"
        anchors.centerIn: parent
        iconName: root.iconName
        size: 28
        color: root.iconColor
    }
}
