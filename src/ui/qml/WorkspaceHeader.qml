import QtQuick
import QtQuick.Layouts
import MusicPlayer.M0

Rectangle {
    id: root
    objectName: "workspace-header"
    required property ShellUiContract contract
    property string pageTitle: ""
    property bool compact: false
    property alias themeHost: themeSlot
    implicitHeight: compact ? 66 : 78
    color: contract.canvasColor
    NavigationText {
        id: labels
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: root.compact ? 23 : 38
        anchors.rightMargin: root.compact ? 23 : 38
        spacing: 16
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Text {
                text: labels.localWorkspace
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeEyebrow
                font.letterSpacing: 1.2
                color: root.contract.textSecondaryColor
                textFormat: Text.PlainText
            }
            Text {
                Layout.fillWidth: true
                objectName: "workspace-page-title"
                text: root.pageTitle
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeBody
                color: root.contract.textPrimaryColor
                textFormat: Text.PlainText
                elide: Text.ElideRight
            }
        }
        Item {
            id: themeSlot
            Layout.preferredWidth: 112
            Layout.preferredHeight: root.contract.controlTargetPreferred
        }
    }
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: root.contract.dividerColor
        Accessible.ignored: true
    }
}
