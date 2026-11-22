import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

// Cartão flutuante sobre a janela, com sombra (sem sombra em alto contraste).
Control {
    id: root

    required property ShellUiContract contract
    default property alias content: holder.data

    padding: 16
    implicitWidth: holder.childrenRect.width + leftPadding + rightPadding
    implicitHeight: holder.childrenRect.height + topPadding + bottomPadding

    background: Rectangle {
        radius: root.contract.radiusLarge
        color: root.contract.highContrast ? root.contract.canvasColor : root.contract.surfaceRaisedColor
        border.width: root.contract.highContrast ? 2 : 0
        border.color: root.contract.textPrimaryColor

        AuroraShadow {
            contract: root.contract
            radius: parent.radius
        }
    }

    contentItem: Item {
        id: holder
    }
}
