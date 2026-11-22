import QtQuick
import MusicPlayer.M0

// Retorno em linha, como a confirmação de cópia: ícone e texto na cor do tom.
Row {
    id: root

    enum Tone {
        Success,
        Warning,
        Error
    }

    required property ShellUiContract contract
    property string text
    property int tone: AuroraFeedback.Success

    readonly property color toneColor: tone === AuroraFeedback.Error ? contract.errorColor : (tone === AuroraFeedback.Warning ? contract.warningColor : contract.successColor)

    spacing: 6
    visible: text.length > 0
    Accessible.role: Accessible.StaticText
    Accessible.name: text

    ShellIcon {
        objectName: "feedback-icon"
        iconName: root.tone === AuroraFeedback.Error ? "warning-circle" : (root.tone === AuroraFeedback.Warning ? "warning" : "check-circle")
        size: 16
        color: root.toneColor
        anchors.verticalCenter: parent.verticalCenter
    }
    Text {
        objectName: "feedback-text"
        text: root.text
        color: root.toneColor
        font.family: root.contract.bodyFontFamily
        font.pixelSize: root.contract.typeCaption
        anchors.verticalCenter: parent.verticalCenter
    }
}
