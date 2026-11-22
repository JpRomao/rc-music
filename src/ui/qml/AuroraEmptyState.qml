import QtQuick
import QtQuick.Layouts
import MusicPlayer.M0

// Tela vazia: ícone num círculo, título, dica e até duas ações.
ColumnLayout {
    id: root

    required property ShellUiContract contract
    required property string controlId
    property string iconName
    property string title
    property string hint
    property string primaryActionText
    property string secondaryActionText
    property alias primaryButton: primary

    signal primaryTriggered
    signal secondaryTriggered

    // A raiz não é acessível; o nome fica no objectName, e cada botão tem o seu.
    objectName: controlId
    spacing: 8

    Rectangle {
        objectName: "empty-icon-circle"
        implicitWidth: 64
        implicitHeight: 64
        radius: 32
        color: root.contract.highContrast ? root.contract.canvasColor : root.contract.selectionColor
        border.width: root.contract.highContrast ? 2 : 0
        border.color: root.contract.textPrimaryColor
        Layout.alignment: Qt.AlignHCenter
        Layout.bottomMargin: 8

        ShellIcon {
            objectName: "empty-icon"
            anchors.centerIn: parent
            iconName: root.iconName
            size: 28
            color: root.contract.highContrast ? root.contract.textPrimaryColor : root.contract.accentColor
        }
    }
    Text {
        objectName: "empty-title"
        text: root.title
        color: root.contract.textPrimaryColor
        font.family: root.contract.headingFontFamily
        font.pixelSize: root.contract.typeTitle
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        Accessible.role: Accessible.Heading
        Accessible.name: text
        Layout.fillWidth: true
    }
    Text {
        objectName: "empty-hint"
        visible: text.length > 0
        text: root.hint
        color: root.contract.textSecondaryColor
        font.family: root.contract.bodyFontFamily
        font.pixelSize: root.contract.typeBody
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        Layout.fillWidth: true
    }
    RowLayout {
        spacing: 8
        visible: root.primaryActionText.length > 0 || root.secondaryActionText.length > 0
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 8

        AuroraButton {
            id: primary
            contract: root.contract
            controlId: root.controlId + "-primary"
            kind: AuroraButton.Primary
            text: root.primaryActionText
            visible: text.length > 0
            onClicked: root.primaryTriggered()
        }
        AuroraButton {
            id: secondary
            contract: root.contract
            controlId: root.controlId + "-secondary"
            kind: AuroraButton.Tonal
            text: root.secondaryActionText
            visible: text.length > 0
            onClicked: root.secondaryTriggered()
        }
    }
}
