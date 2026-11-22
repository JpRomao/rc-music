import QtQuick
import QtQuick.Layouts
import MusicPlayer.M0

// Cabeçalho de gaveta: rótulo, título com serifa, subtítulo e botão de fechar.
Item {
    id: root

    required property ShellUiContract contract
    required property string controlId
    required property string closeText
    property string eyebrow
    property string title
    property string subtitle

    // The close button, for a drawer that wraps the focus around itself.
    readonly property Item closeButton: closeControl

    signal closeRequested

    // A raiz não é acessível; o nome fica no objectName, e o botão de fechar tem o seu.
    objectName: controlId
    implicitWidth: layout.implicitWidth
    implicitHeight: Math.max(layout.implicitHeight, contract.controlTargetMinimum)

    RowLayout {
        id: layout
        anchors.fill: parent
        spacing: 8

        ColumnLayout {
            spacing: 2
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter

            Text {
                objectName: "drawer-eyebrow"
                visible: text.length > 0
                text: root.eyebrow
                color: root.contract.textSecondaryColor
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeEyebrow
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 1
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
                Layout.fillWidth: true
            }
            Text {
                objectName: "drawer-title"
                visible: text.length > 0
                text: root.title
                color: root.contract.textPrimaryColor
                font.family: root.contract.headingFontFamily
                font.pixelSize: root.contract.typeTitle
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
                Accessible.role: Accessible.Heading
                Accessible.name: text
                Layout.fillWidth: true
            }
            Text {
                objectName: "drawer-subtitle"
                visible: text.length > 0
                text: root.subtitle
                color: root.contract.textSecondaryColor
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeBody
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
                Layout.fillWidth: true
            }
        }
        AuroraIconButton {
            id: closeControl
            contract: root.contract
            controlId: root.controlId + "-close"
            iconName: "close"
            text: root.closeText
            Layout.alignment: Qt.AlignTop
            onClicked: root.closeRequested()
        }
    }
}
