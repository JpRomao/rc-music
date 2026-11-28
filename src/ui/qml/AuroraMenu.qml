import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

// Menu elevado Aurora: fundo com sombra, entrada em fade e itens de 40 px.
Menu {
    id: menu

    required property ShellUiContract contract
    // Ícone do item que abre este menu, quando ele é um submenu.
    property string iconName
    // Accessible description of the item that opens this menu, when it is a submenu (for example, why it is off).
    property string itemDescription
    // Largura do item mais largo; cada item tem no mínimo 200.
    readonly property real widestItem: {
        let widest = 200;
        for (let i = 0; i < count; ++i) {
            const entry = itemAt(i);
            if (entry)
                widest = Math.max(widest, entry.implicitWidth);
        }
        return widest;
    }

    // The accessible description of an item the delegate builds: its submenu's, when it opens one.
    function submenuDescription(subMenu) {
        if (subMenu)
            return subMenu.itemDescription;
        return "";
    }

    padding: 6
    popupType: Popup.Item
    // Acompanha o item mais largo, com teto na largura da janela.
    implicitWidth: Math.min(widestItem + leftPadding + rightPadding, Overlay.overlay ? Overlay.overlay.width : Number.POSITIVE_INFINITY)

    delegate: AuroraMenuItem {
        // Dentro do delegate o id aponta para o tipo base do Menu.
        contract: menu.contract // qmllint disable missing-property
        // Item criado de uma Action leva o objectName dela como controlId; o de um submenu, o do submenu.
        controlId: action ? action.objectName : (subMenu ? subMenu.objectName : "") // qmllint disable missing-property
        iconName: subMenu ? subMenu.iconName : "" // qmllint disable missing-property
        Accessible.description: menu.submenuDescription(subMenu) // qmllint disable missing-property
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
