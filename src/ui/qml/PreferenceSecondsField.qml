import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

ColumnLayout {
    id: root
    required property ShellUiContract contract
    required property string controlId
    required property string label
    required property string errorText
    required property int value
    signal valueEdited(int value)
    spacing: contract.spacingUnit

    function resetDraft() {
        field.text = String(value);
    }
    function validSeconds(text) {
        return /^[0-9]+$/.test(text) && Number(text) >= 1 && Number(text) <= 300;
    }
    onValueChanged: {
        if (!field.activeFocus)
            resetDraft();
    }
    Text {
        text: root.label
        textFormat: Text.PlainText
        color: root.contract.textPrimaryColor
        Layout.fillWidth: true
    }
    TextField {
        id: field
        objectName: root.controlId
        Layout.fillWidth: true
        implicitHeight: root.contract.controlTargetPreferred
        readonly property bool draftValid: root.validSeconds(text)
        text: String(root.value)
        inputMethodHints: Qt.ImhDigitsOnly
        activeFocusOnTab: true
        selectByMouse: true
        Accessible.role: Accessible.EditableText
        Accessible.id: objectName
        Accessible.name: root.label
        Accessible.focusable: enabled
        Accessible.focused: activeFocus
        Accessible.description: draftValid ? "" : root.errorText
        color: root.contract.textPrimaryColor
        selectionColor: root.contract.accentColor
        selectedTextColor: root.contract.accentTextColor
        onTextEdited: {
            if (root.validSeconds(text))
                root.valueEdited(Number(text));
        }
        background: Rectangle {
            color: root.contract.surfaceColor
            radius: root.contract.radiusSmall
            border.width: field.activeFocus ? 3 : 1
            border.color: field.activeFocus ? root.contract.focusColor : (field.draftValid ? root.contract.borderColor : root.contract.errorColor)
        }
    }
    Item {
        Layout.fillWidth: true
        implicitHeight: 28
        Text {
            objectName: root.controlId + "-error"
            anchors.fill: parent
            text: root.errorText
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            font.pixelSize: 12
            color: root.contract.errorColor
            visible: !field.draftValid
            Accessible.name: text
        }
    }
}
