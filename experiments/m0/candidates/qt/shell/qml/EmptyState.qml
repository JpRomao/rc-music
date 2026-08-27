import QtQuick
import QtQuick.Layouts
import MusicPlayer.M0

ColumnLayout {
    id: root
    objectName: "emptyState"
    required property ShellUiContract contract
    signal openAudioRequested
    spacing: contract.spacingUnit * 4

    Image {
        objectName: "cover-placeholder"
        source: root.contract.placeholderSource
        sourceSize.width: 192
        sourceSize.height: 192
        Layout.alignment: Qt.AlignHCenter
        Accessible.ignored: true
    }
    Text {
        objectName: "empty-title"
        text: root.contract.emptyTitle
        color: root.contract.textPrimaryColor
        Layout.alignment: Qt.AlignHCenter
    }
    Text {
        objectName: "empty-hint"
        text: root.contract.emptyHint
        color: root.contract.textSecondaryColor
        Layout.alignment: Qt.AlignHCenter
    }
    AccessibleActionButton {
        controlId: "open-audio"
        contract: root.contract
        text: root.contract.openAudioText
        Layout.alignment: Qt.AlignHCenter
        onClicked: root.openAudioRequested()
    }
}
