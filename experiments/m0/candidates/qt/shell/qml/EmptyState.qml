import QtQuick
import QtQuick.Layouts
import MusicPlayer.M0

ColumnLayout {
    id: root
    objectName: "emptyState"
    required property ShellUiContract contract
    property string titleText: contract.emptyTitle
    property string hintText: contract.emptyHint
    property string actionText: contract.openAudioText
    signal openAudioRequested
    spacing: contract.spacingUnit * 4

    function focusPrimary() {
        openAudioButton.forceActiveFocus(Qt.TabFocusReason);
    }

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
        text: root.titleText
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        color: root.contract.textPrimaryColor
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignHCenter
    }
    Text {
        objectName: "empty-hint"
        text: root.hintText
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        color: root.contract.textSecondaryColor
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignHCenter
    }
    AccessibleActionButton {
        id: openAudioButton
        controlId: "open-audio"
        contract: root.contract
        text: root.actionText
        Layout.maximumWidth: root.width
        Layout.alignment: Qt.AlignHCenter
        onClicked: root.openAudioRequested()
    }
}
