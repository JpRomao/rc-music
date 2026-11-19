import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

// S9.1: the listening room's output offset, in steps of 10 ms. A positive value plays earlier.
// Up and Down change it from the keyboard.
ColumnLayout {
    id: root
    required property ShellUiContract contract
    required property string controlId
    required property string label
    required property string hint
    // The value text; its %1 is the signed number.
    required property string valueFormat
    required property int value
    required property int minimum
    required property int maximum
    signal valueEdited(int value)
    spacing: contract.spacingUnit
    onValueChanged: spin.value = value

    function signedText(number) {
        return number > 0 ? "+" + number : String(number).replace("-", "−");
    }

    Text {
        text: root.label
        textFormat: Text.PlainText
        color: root.contract.textPrimaryColor
        Layout.fillWidth: true
    }
    SpinBox {
        id: spin
        objectName: root.controlId
        Layout.fillWidth: true
        implicitHeight: root.contract.controlTargetPreferred
        from: root.minimum
        to: root.maximum
        stepSize: 10
        value: root.value
        editable: false
        activeFocusOnTab: true
        textFromValue: number => root.valueFormat.arg(root.signedText(number))
        Accessible.role: Accessible.SpinBox
        Accessible.id: objectName
        Accessible.name: root.label
        Accessible.description: root.hint
        Accessible.focusable: enabled
        Accessible.focused: activeFocus
        onValueModified: root.valueEdited(value)
        // Only the SpinBox is a Tab stop. SpinBox makes its content item one when it gets it, which would
        // keep Tab inside this read-only text.
        Component.onCompleted: contentItem.activeFocusOnTab = false
        contentItem: Text {
            text: spin.displayText
            textFormat: Text.PlainText
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: root.contract.typeBody
            color: root.contract.textPrimaryColor
        }
        background: Rectangle {
            color: root.contract.surfaceColor
            radius: root.contract.radiusSmall
            border.width: spin.activeFocus ? 3 : 1
            border.color: spin.activeFocus ? root.contract.focusColor : root.contract.borderColor
        }
    }
    Text {
        text: root.hint
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        font.pixelSize: root.contract.typeCaption
        color: root.contract.textSecondaryColor
        Layout.fillWidth: true
    }
}
