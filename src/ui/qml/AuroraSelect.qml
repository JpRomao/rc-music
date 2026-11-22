pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

// Caixa de seleção com o visual do campo Aurora e um menu como popup.
ComboBox {
    id: control

    required property ShellUiContract contract
    required property string controlId
    required property string label
    // Marca de foco com o foco ativo, como no campo, ou com o menu aberto.
    readonly property bool marked: activeFocus || popup.visible
    // A opção destacada veio do teclado: ela ganha o anel interno em vez do hoverColor.
    // Liga com qualquer tecla, desliga quando o mouse passa por uma opção.
    property bool keyboardHighlight: false

    objectName: controlId
    activeFocusOnTab: true
    hoverEnabled: true
    opacity: interaction.opacity
    implicitWidth: 200
    implicitHeight: contract.controlTargetMinimum
    font.family: contract.bodyFontFamily
    font.pixelSize: contract.typeBody
    leftPadding: 12
    rightPadding: 40
    topPadding: 0
    bottomPadding: 0
    Accessible.name: label
    Accessible.id: controlId

    Keys.onPressed: keyboardHighlight = true

    background: Rectangle {
        objectName: "select-background"
        radius: control.contract.radiusControl
        color: control.contract.highContrast ? control.contract.canvasColor : interaction.tint(control.contract.surfaceColor)
        border.width: control.contract.highContrast ? 2 : 0
        border.color: interaction.inactive ? control.contract.disabledTextColor : control.marked ? control.contract.accentColor : control.contract.textPrimaryColor

        Rectangle {
            objectName: "field-line"
            visible: !control.contract.highContrast
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: control.marked ? 2 : 1
            color: control.marked ? control.contract.focusColor : control.contract.textSecondaryColor
        }
    }

    contentItem: Text {
        text: control.displayText
        font: control.font
        color: interaction.inactive && control.contract.highContrast ? control.contract.disabledTextColor : control.contract.textPrimaryColor
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }

    indicator: ShellIcon {
        objectName: "select-indicator"
        iconName: "caret-down"
        color: control.contract.textSecondaryColor
        x: control.width - width - 12
        y: (control.height - height) / 2
    }

    delegate: ItemDelegate {
        id: option
        required property int index
        readonly property bool current: control.currentIndex === option.index
        readonly property bool marked: control.highlightedIndex === option.index
        width: ListView.view ? ListView.view.width : control.width
        height: control.contract.controlTargetMinimum
        leftPadding: 12
        rightPadding: 12
        topPadding: 0
        bottomPadding: 0
        hoverEnabled: true
        highlighted: marked
        text: control.textAt(option.index)
        font: control.font
        Accessible.name: text
        onHoveredChanged: {
            if (hovered)
                control.keyboardHighlight = false;
        }
        background: Rectangle {
            radius: control.contract.radiusControl
            color: option.marked && (control.contract.highContrast || !control.keyboardHighlight) ? (control.contract.highContrast ? control.contract.accentColor : control.contract.hoverColor) : "transparent"
        }
        contentItem: Item {
            Text {
                anchors.left: parent.left
                anchors.right: mark.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: option.text
                font: option.font
                elide: Text.ElideRight
                color: option.marked && control.contract.highContrast ? control.contract.accentTextColor : control.contract.textPrimaryColor
            }
            ShellIcon {
                id: mark
                objectName: "select-current-mark"
                iconName: "check"
                visible: option.current
                color: option.marked && control.contract.highContrast ? control.contract.accentTextColor : control.contract.accentColor
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        AuroraFocusRing {
            target: option
            contract: control.contract
            inside: true
            targetRadius: control.contract.radiusControl
            shown: option.marked && control.keyboardHighlight && control.popup.visible
        }
    }

    popup: Popup {
        y: control.height + 4
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, 320)
        padding: 4
        popupType: Popup.Item
        // Entra e sai em fade, como o menu; com movimento reduzido a duração é 0.
        enter: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: control.contract.panelTransitionMs
            }
        }
        exit: Transition {
            NumberAnimation {
                property: "opacity"
                from: 1
                to: 0
                duration: control.contract.panelTransitionMs
            }
        }
        // Aberto pelo teclado, o item atual já mostra o anel; pelo mouse, não.
        onAboutToShow: control.keyboardHighlight = control.visualFocus

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            radius: control.contract.radiusMedium
            color: control.contract.highContrast ? control.contract.canvasColor : control.contract.surfaceRaisedColor
            border.width: control.contract.highContrast ? 2 : 0
            border.color: control.contract.textPrimaryColor

            AuroraShadow {
                contract: control.contract
                radius: parent.radius
            }
        }
    }

    AuroraFocusRing {
        target: control
        contract: control.contract
        targetRadius: control.contract.radiusControl
    }

    AuroraInteraction {
        id: interaction
        contract: control.contract
        enabled: control.enabled
        hovered: control.hovered
        pressed: control.down
    }
}
