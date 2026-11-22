import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

// Campo numérico com botões de menos e mais: botões nas pontas e o valor editável no centro.
SpinBox {
    id: control

    required property ShellUiContract contract
    required property string controlId
    required property string label
    // Texto do catálogo de quem usa, como s ou ms; vazio não acrescenta nada.
    property string suffix: ""

    readonly property bool fieldFocused: control.activeFocus || input.activeFocus

    objectName: controlId
    editable: true
    activeFocusOnTab: true
    hoverEnabled: true
    opacity: interaction.opacity
    padding: 0
    leftPadding: contract.controlTargetMinimum
    rightPadding: contract.controlTargetMinimum
    implicitWidth: 160
    implicitHeight: contract.controlTargetMinimum
    Accessible.role: Accessible.SpinBox
    Accessible.id: controlId
    Accessible.name: label
    Accessible.focusable: enabled

    textFromValue: (value, locale) => suffix.length > 0 ? String(value) + " " + suffix : String(value)
    valueFromText: (text, locale) => {
        const found = text.match(/-?[0-9]+/);
        return found ? parseInt(found[0], 10) : control.value;
    }

    contentItem: TextInput {
        id: input
        objectName: "stepper-input"
        text: control.displayText
        font.family: control.contract.bodyFontFamily
        font.pixelSize: control.contract.typeBody
        color: interaction.inactive && control.contract.highContrast ? control.contract.disabledTextColor : control.contract.textPrimaryColor
        selectionColor: control.contract.accentColor
        selectedTextColor: control.contract.accentTextColor
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        validator: RegularExpressionValidator {
            regularExpression: /^-?[0-9]*( \S*)?$/
        }
        Accessible.ignored: true
    }

    down.indicator: Rectangle {
        objectName: "stepper-down"
        x: control.mirrored ? control.width - width : 0
        width: control.contract.controlTargetMinimum
        height: control.contract.controlTargetMinimum
        radius: control.contract.radiusControl
        color: control.down.pressed ? control.contract.selectionColor : control.down.hovered ? control.contract.hoverColor : "transparent"

        ShellIcon {
            anchors.centerIn: parent
            iconName: "minus"
            color: control.contract.highContrast && control.value <= control.from ? control.contract.disabledTextColor : interaction.ink(control.contract.textPrimaryColor)
            opacity: control.contract.highContrast || control.value > control.from ? 1 : 0.4
        }
    }

    up.indicator: Rectangle {
        objectName: "stepper-up"
        x: control.mirrored ? 0 : control.width - width
        width: control.contract.controlTargetMinimum
        height: control.contract.controlTargetMinimum
        radius: control.contract.radiusControl
        color: control.up.pressed ? control.contract.selectionColor : control.up.hovered ? control.contract.hoverColor : "transparent"

        ShellIcon {
            anchors.centerIn: parent
            iconName: "plus"
            color: control.contract.highContrast && control.value >= control.to ? control.contract.disabledTextColor : interaction.ink(control.contract.textPrimaryColor)
            opacity: control.contract.highContrast || control.value < control.to ? 1 : 0.4
        }
    }

    background: Item {
        Rectangle {
            objectName: "stepper-center"
            x: control.contract.controlTargetMinimum
            width: control.width - 2 * control.contract.controlTargetMinimum
            height: parent.height
            color: control.contract.highContrast ? control.contract.canvasColor : interaction.tint(control.contract.surfaceColor)
            border.width: control.contract.highContrast ? 2 : 0
            border.color: interaction.inactive ? control.contract.disabledTextColor : control.fieldFocused ? control.contract.accentColor : control.contract.textPrimaryColor

            Rectangle {
                objectName: "field-line"
                visible: !control.contract.highContrast
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: control.fieldFocused ? 2 : 1
                color: control.fieldFocused ? control.contract.focusColor : control.contract.textSecondaryColor
            }
        }
    }

    // O anel marca o centro editável, entre os botões de menos e mais.
    AuroraFocusRing {
        target: control
        contract: control.contract
        shown: control.fieldFocused
        areaX: control.contract.controlTargetMinimum
        areaWidth: control.width - 2 * control.contract.controlTargetMinimum
    }

    AuroraInteraction {
        id: interaction
        contract: control.contract
        enabled: control.enabled
        // O centro é o campo: o hover dos botões de menos e mais não o tinge.
        hovered: control.hovered && !control.up.hovered && !control.down.hovered
    }
}
