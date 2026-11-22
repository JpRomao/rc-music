import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

// Menu elevado Aurora: fundo com sombra, entrada em fade e itens de 40 px.
Menu {
    id: menu

    required property ShellUiContract contract
    padding: 6
    popupType: Popup.Item
    implicitWidth: Math.max(200, contentItem.implicitWidth + leftPadding + rightPadding)

    delegate: AuroraMenuItem {
        // Dentro do delegate o id aponta para o tipo base do Menu.
        contract: menu.contract // qmllint disable missing-property
    }

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: menu.contract.panelTransitionMs
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1
            to: 0
            duration: menu.contract.panelTransitionMs
        }
    }

    background: Rectangle {
        radius: menu.contract.radiusMedium
        color: menu.contract.highContrast ? menu.contract.canvasColor : menu.contract.surfaceRaisedColor
        border.width: menu.contract.highContrast ? 2 : 0
        border.color: menu.contract.textPrimaryColor

        AuroraShadow {
            contract: menu.contract
            radius: parent.radius
        }
    }
}
