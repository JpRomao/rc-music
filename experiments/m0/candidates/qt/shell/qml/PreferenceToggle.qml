import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

CheckBox {
    id: control
    required property ShellUiContract contract
    required property string controlId
    required property bool value
    signal valueEdited(bool value)
    objectName: controlId
    checked: value
    implicitHeight: contract.controlTargetPreferred
    activeFocusOnTab: true
    Accessible.role: Accessible.CheckBox
    Accessible.id: controlId
    Accessible.name: text
    Accessible.focusable: enabled
    Accessible.focused: activeFocus
    Accessible.checkable: true
    Accessible.checked: checked
    onToggled: valueEdited(checked)
    palette.windowText: contract.highContrast && !enabled ? contract.disabledTextColor : contract.textPrimaryColor
    palette.text: contract.highContrast && !enabled ? contract.disabledTextColor : contract.textPrimaryColor
    palette.base: contract.surfaceColor
    palette.highlight: contract.accentColor
    palette.highlightedText: contract.accentTextColor
    palette.mid: contract.highContrast && !enabled ? contract.disabledTextColor : contract.borderColor
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        radius: control.contract.radiusSmall
        border.width: 3
        border.color: control.contract.focusColor
        visible: control.activeFocus
        z: 2
        Accessible.ignored: true
    }
}
