import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

Control {
    id: control

    enum Tone {
        Neutral,
        Accent,
        Warning,
        Success,
        Error
    }

    required property ShellUiContract contract
    property string text
    property string iconName
    property int tone: AuroraChip.Neutral

    readonly property color toneColor: {
        switch (tone) {
        case AuroraChip.Accent:
            return contract.accentColor;
        case AuroraChip.Warning:
            return contract.warningColor;
        case AuroraChip.Success:
            return contract.successColor;
        case AuroraChip.Error:
            return contract.errorColor;
        default:
            return contract.textPrimaryColor;
        }
    }

    focusPolicy: Qt.NoFocus
    Accessible.role: Accessible.StaticText
    Accessible.name: text
    leftPadding: 10
    rightPadding: 10
    topPadding: 0
    bottomPadding: 0
    implicitHeight: 24
    implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding

    background: Rectangle {
        objectName: "chip-background"
        radius: 12
        color: control.contract.highContrast ? control.contract.canvasColor : control.contract.selectionColor
        border.width: control.contract.highContrast ? 1 : 0
        border.color: control.contract.textPrimaryColor
    }

    contentItem: RowLayout {
        spacing: 4
        ShellIcon {
            objectName: "chip-icon"
            iconName: control.iconName
            size: 14
            color: control.toneColor
            Layout.alignment: Qt.AlignVCenter
        }
        Text {
            objectName: "chip-label"
            text: control.text
            font.family: control.contract.bodyFontFamily
            font.pixelSize: control.contract.typeCaption
            font.weight: Font.DemiBold
            color: control.toneColor
            elide: Text.ElideRight
            maximumLineCount: 1
            verticalAlignment: Text.AlignVCenter
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
