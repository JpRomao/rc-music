import QtQuick
import QtQuick.Layouts
import MusicPlayer.M0

Rectangle {
    id: root
    objectName: "workspace-header"
    required property ShellUiContract contract
    property string pageTitle: ""
    property bool compact: false
    // Só o Aurora tem o botão aqui; o fixture congelado o mantém na lateral.
    property bool themeButtonActive: true
    property bool darkTheme: false
    property bool exiting: false
    readonly property Item themeButton: themeLoader.item as Item
    signal themeRequested
    implicitHeight: 60
    color: contract.canvasColor
    NavigationText {
        id: labels
    }
    Component {
        id: themeButtonComponent
        // qmllint disable unqualified
        AuroraIconButton {
            contract: root.contract
            controlId: "theme-toggle"
            tonal: true
            iconName: root.darkTheme ? "sun" : "moon"
            text: root.darkTheme ? labels.themeLight : labels.themeDark
            enabled: !root.exiting
            onClicked: root.themeRequested()
        }
        // qmllint enable unqualified
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 28
        anchors.rightMargin: 28
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
        Loader {
            id: themeLoader
            active: root.themeButtonActive
            Layout.alignment: Qt.AlignVCenter
            sourceComponent: themeButtonComponent
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
