import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

// Aviso em linha sobre o fundo Seleção, com até duas ações.
Control {
    id: root

    enum Tone {
        Warning,
        Error,
        Info
    }

    required property ShellUiContract contract
    required property string controlId
    property int tone: AuroraNotice.Warning
    property string title
    property string text
    property string primaryActionText
    property string secondaryActionText
    property alias primaryButton: primary

    signal primaryTriggered
    signal secondaryTriggered

    readonly property string iconName: tone === AuroraNotice.Error ? "warning-circle" : (tone === AuroraNotice.Info ? "info" : "warning")
    readonly property color toneColor: tone === AuroraNotice.Error ? contract.errorColor : (tone === AuroraNotice.Info ? contract.accentColor : contract.warningColor)

    ControlsText {
        id: controlsText
    }

    objectName: controlId
    padding: 16
    implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding

    Accessible.role: Accessible.AlertMessage
    Accessible.id: controlId
    Accessible.name: controlsText.noticeName(title, text)

    background: Rectangle {
        radius: root.contract.radiusLarge
        color: root.contract.highContrast ? root.contract.canvasColor : root.contract.selectionColor
        border.width: root.contract.highContrast ? 2 : 0
        border.color: root.contract.textPrimaryColor
    }

    contentItem: RowLayout {
        spacing: 12

        ShellIcon {
            objectName: "notice-icon"
            iconName: root.iconName
            size: 20
            color: root.toneColor
            Layout.alignment: Qt.AlignTop
        }
        ColumnLayout {
            spacing: 4
            Layout.fillWidth: true

            Text {
                objectName: "notice-title"
                visible: text.length > 0
                text: root.title
                color: root.contract.textPrimaryColor
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeBody
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Text {
                objectName: "notice-text"
                visible: text.length > 0
                text: root.text
                color: root.contract.textPrimaryColor
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeBody
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            RowLayout {
                spacing: 8
                visible: root.primaryActionText.length > 0 || root.secondaryActionText.length > 0
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
    }
}
