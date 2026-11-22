import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

// Interruptor liga/desliga. O texto visível fica na linha de quem usa; label é o nome acessível.
Switch {
    id: control

    required property ShellUiContract contract
    required property string controlId
    required property string label

    // Cor do contorno e do polegar desligados.
    readonly property color offColor: {
        if (contract.highContrast)
            return interaction.inactive ? contract.disabledTextColor : contract.textPrimaryColor;
        return contract.textSecondaryColor;
    }

    objectName: controlId
    Accessible.role: Accessible.CheckBox
    Accessible.id: controlId
    Accessible.name: label
    Accessible.checkable: true
    Accessible.checked: checked
    Accessible.focusable: enabled
    Accessible.focused: activeFocus
    Accessible.onPressAction: control.activate()
    activeFocusOnTab: true
    hoverEnabled: true
    opacity: interaction.opacity
    padding: 0
    implicitWidth: 40
    implicitHeight: contract.controlTargetMinimum
    width: implicitWidth
    height: implicitHeight

    Keys.onReturnPressed: event => {
        activate();
        event.accepted = true;
    }
    Keys.onEnterPressed: event => {
        activate();
        event.accepted = true;
    }
    function activate() {
        toggle();
        toggled();
        clicked();
    }

    background: null
    contentItem: Item {}

    indicator: Item {
        id: indicatorBox
        x: control.leftPadding
        y: (control.height - height) / 2
        width: 40
        height: control.contract.controlTargetMinimum

        Rectangle {
            id: track
            objectName: "switch-track"
            y: (parent.height - height) / 2
            width: 40
            height: 24
            radius: 12
            color: control.checked && !(control.contract.highContrast && interaction.inactive) ? interaction.tint(control.contract.accentColor) : "transparent"
            border.width: 2
            border.color: control.checked ? interaction.ink(control.contract.accentColor) : control.offColor
        }

        Rectangle {
            id: thumb
            objectName: "switch-thumb"
            // Posição final com o interruptor ligado; exposta para os testes.
            readonly property real checkedX: track.width - 3 - 18
            readonly property real uncheckedX: 5
            width: control.checked ? 18 : 14
            height: width
            radius: width / 2
            x: control.checked ? checkedX : uncheckedX
            y: track.y + (track.height - height) / 2
            color: control.checked ? interaction.ink(control.contract.accentTextColor) : control.offColor

            Behavior on x {
                NumberAnimation {
                    duration: control.contract.feedbackTransitionMs
                }
            }
        }

        AuroraFocusRing {
            target: control
            contract: control.contract
            targetRadius: track.radius
            areaX: track.x
            areaY: track.y
            areaWidth: track.width
            areaHeight: track.height
        }
    }

    AuroraInteraction {
        id: interaction
        contract: control.contract
        enabled: control.enabled
        hovered: control.hovered
        pressed: control.down
    }
}
