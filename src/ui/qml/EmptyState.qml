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
    property bool openingEnabled: true
    property bool compact: false
    readonly property real coverExtent: compact && parent ? Math.max(0, Math.min(128, parent.height - titleLabel.implicitHeight - hintLabel.implicitHeight - openAudioButton.implicitHeight - spacing * 3 - 4)) : 192
    signal openAudioRequested(Item opener)
    spacing: contract.spacingUnit * 4

    function focusPrimary() {
        openAudioButton.forceActiveFocus(Qt.TabFocusReason);
    }

    Image {
        objectName: "cover-placeholder"
        Layout.preferredWidth: root.coverExtent
        Layout.preferredHeight: root.coverExtent
        source: root.contract.placeholderSource
        sourceSize.width: 192
        sourceSize.height: 192
        Layout.alignment: Qt.AlignHCenter
        Accessible.ignored: true
    }
    Text {
        id: titleLabel
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
        id: hintLabel
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
        enabled: root.openingEnabled
        Layout.maximumWidth: root.width
        Layout.alignment: Qt.AlignHCenter
        onClicked: root.openAudioRequested(openAudioButton)
    }
}
