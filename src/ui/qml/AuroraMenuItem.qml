import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

// Item de menu Aurora: ícone, texto, atalho à direita e seta quando abre submenu.
MenuItem {
    id: item

    required property ShellUiContract contract
    property string iconName
    property string shortcutText
    property bool danger: false

    readonly property bool hasSubMenu: subMenu !== null
    readonly property color restInk: danger ? contract.errorColor : contract.textPrimaryColor

    implicitWidth: 200
    implicitHeight: contract.controlTargetMinimum
    leftPadding: 12
    rightPadding: 12
    topPadding: 0
    bottomPadding: 0
    spacing: 10
    hoverEnabled: true
    opacity: interaction.opacity
    font.family: contract.bodyFontFamily
    font.pixelSize: contract.typeBody
    Accessible.name: text

    // O destaque do mouse usa hoverColor; o item atual do teclado ganha o anel interno.
    background: Rectangle {
        objectName: "menu-item-background"
        radius: item.contract.radiusControl
        color: item.highlighted && (item.contract.highContrast || !item.visualFocus) ? (item.contract.highContrast ? item.contract.accentColor : item.contract.hoverColor) : "transparent"
    }

    // Seta do submenu: o Menu posiciona o indicador de seta do MenuItem.
    arrow: ShellIcon {
        objectName: "menu-item-caret"
        iconName: item.hasSubMenu ? "caret-right" : ""
        color: item.highlighted && item.contract.highContrast ? item.contract.accentTextColor : interaction.ink(item.contract.textSecondaryColor)
        x: item.width - width - item.rightPadding
        y: (item.height - height) / 2
    }

    contentItem: Item {
        implicitWidth: (icon.visible ? icon.width + item.spacing : 0) + label.implicitWidth + (shortcut.visible ? shortcut.implicitWidth + item.spacing : 0) + (item.hasSubMenu ? 28 : 0)
        implicitHeight: item.contract.controlTargetMinimum

        ShellIcon {
            id: icon
            objectName: "menu-item-icon"
            iconName: item.iconName
            color: label.color
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            id: label
            objectName: "menu-item-label"
            anchors.left: icon.visible ? icon.right : parent.left
            anchors.leftMargin: icon.visible ? item.spacing : 0
            anchors.right: shortcut.visible ? shortcut.left : parent.right
            anchors.rightMargin: shortcut.visible ? item.spacing : item.hasSubMenu ? 28 : 0
            anchors.verticalCenter: parent.verticalCenter
            text: item.text
            font: item.font
            elide: Text.ElideRight
            color: item.highlighted && item.contract.highContrast ? item.contract.accentTextColor : interaction.ink(item.restInk)
        }
        Text {
            id: shortcut
            objectName: "menu-item-shortcut"
            visible: item.shortcutText.length > 0
            anchors.right: parent.right
            anchors.rightMargin: item.hasSubMenu ? 28 : 0
            anchors.verticalCenter: parent.verticalCenter
            text: item.shortcutText
            font.family: item.font.family
            font.pixelSize: item.contract.typeCaption
            color: item.highlighted ? (item.contract.highContrast ? item.contract.accentTextColor : item.contract.textPrimaryColor) : interaction.ink(item.contract.textSecondaryColor)
        }
    }

    AuroraFocusRing {
        target: item
        contract: item.contract
        inside: true
        targetRadius: item.contract.radiusControl
        shown: item.visualFocus
    }

    AuroraInteraction {
        id: interaction
        contract: item.contract
        enabled: item.enabled
        hovered: item.hovered
        pressed: item.down
    }
}
