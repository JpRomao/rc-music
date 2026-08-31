import QtQuick
import QtQuick.Layouts
import MusicPlayer.M0

RowLayout {
    id: root
    required property ShellUiContract contract
    required property string controlId
    required property string label
    required property bool valueKnown
    required property string valueText
    spacing: contract.spacingUnit * 2

    Text {
        Layout.preferredWidth: 152
        Layout.alignment: Qt.AlignTop
        text: root.label
        textFormat: Text.PlainText
        color: root.contract.textSecondaryColor
        wrapMode: Text.WordWrap
        Accessible.ignored: true
    }
    Text {
        objectName: root.controlId + "-value"
        Layout.fillWidth: true
        text: root.valueKnown ? root.valueText : root.contract.notInformed
        textFormat: Text.PlainText
        color: root.contract.textPrimaryColor
        wrapMode: Text.Wrap
        Accessible.id: objectName
        Accessible.name: root.label + ": " + text
    }
}
