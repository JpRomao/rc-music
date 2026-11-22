import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

// Caixinha de marcar com texto que quebra de linha; altura de pelo menos 40.
CheckBox {
    id: control

    required property ShellUiContract contract
    required property string controlId

    readonly property color offColor: {
        if (contract.highContrast)
            return interaction.inactive ? contract.disabledTextColor : contract.textPrimaryColor;
        return contract.textSecondaryColor;
    }

    objectName: controlId
    Accessible.role: Accessible.CheckBox
    Accessible.id: controlId
    Accessible.name: text
    Accessible.checkable: true
    Accessible.checked: checked
    Accessible.focusable: enabled
    Accessible.focused: activeFocus
    // O leitor de tela não aciona um controle desabilitado.
    Accessible.onPressAction: {
        if (control.enabled)
            control.activate();
    }
    activeFocusOnTab: true
    hoverEnabled: true
    opacity: interaction.opacity
    spacing: 10
    leftPadding: 0
    rightPadding: 0
    topPadding: 8
    bottomPadding: 8
    implicitWidth: 240
    implicitHeight: Math.max(contract.controlTargetMinimum, implicitContentHeight + topPadding + bottomPadding)

    font.family: contract.bodyFontFamily
    font.pixelSize: contract.typeBody

    // Enter e Return alternam, como no interruptor.
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

    indicator: Rectangle {
        id: box
        x: control.leftPadding
        y: (control.height - height) / 2
        width: 20
        height: 20
        radius: 6
        color: control.checked && !(control.contract.highContrast && interaction.inactive) ? interaction.tint(control.contract.accentColor) : "transparent"
        border.width: 2
        border.color: control.checked ? interaction.ink(control.contract.accentColor) : control.offColor

        ShellIcon {
            anchors.centerIn: parent
            objectName: "check-icon"
            iconName: "check"
            size: 14
            color: interaction.ink(control.contract.accentTextColor)
            visible: control.checked
        }

        AuroraFocusRing {
            target: control
            contract: control.contract
            targetRadius: box.radius
            areaWidth: box.width
            areaHeight: box.height
        }
    }

    contentItem: Text {
        text: control.text
        font: control.font
        color: interaction.inactive && control.contract.highContrast ? control.contract.disabledTextColor : control.contract.textPrimaryColor
        wrapMode: Text.WordWrap
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + control.spacing
    }

    AuroraInteraction {
        id: interaction
        contract: control.contract
        enabled: control.enabled
        hovered: control.hovered
        pressed: control.down
    }
}
