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
        RowLayout {
            // Breadcrumb of the approved prototype: root, separator and current page.
            Layout.fillWidth: true
            spacing: 10
            Text {
                objectName: "workspace-breadcrumb-root"
                text: labels.breadcrumbRoot
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeBody
                color: root.contract.textSecondaryColor
                textFormat: Text.PlainText
            }
            Text {
                text: labels.breadcrumbSeparator
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeBody
                color: root.contract.textSecondaryColor
                textFormat: Text.PlainText
                Accessible.ignored: true
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
