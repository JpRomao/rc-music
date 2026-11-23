import QtQuick
import QtQuick.Layouts
import MusicPlayer.M0

GridLayout {
    id: root
    objectName: "emptyState"
    required property ShellUiContract contract
    property string titleText: contract.emptyTitle
    property string hintText: contract.emptyHint
    property string actionText: contract.openAudioText
    property bool openingEnabled: true
    property bool compact: false
    readonly property real coverExtent: compact && parent ? Math.max(0, Math.min(128, parent.height - titleLabel.implicitHeight - hintLabel.implicitHeight - openAudioButton.implicitHeight - rowSpacing * 3 - 4)) : 192
    signal openAudioRequested(Item opener)
    columns: 1
    rowSpacing: contract.spacingUnit * 4

    function focusPrimary() {
        openAudioButton.forceActiveFocus(Qt.TabFocusReason);
    }

    TrackArtwork {
        fallbackObjectName: "cover-placeholder"
        Layout.row: 0
        Layout.column: 0
        Layout.preferredWidth: root.coverExtent
        Layout.preferredHeight: root.coverExtent
        contract: root.contract
        frozen: true
        fallbackSource: root.contract.placeholderSource
        Layout.alignment: Qt.AlignHCenter
        Accessible.ignored: true
    }
    Text {
        id: titleLabel
        objectName: "empty-title"
        Layout.row: 1
        Layout.column: 0
        text: root.titleText
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        font: root.contract.applicationFont
        horizontalAlignment: Text.AlignHCenter
        color: root.contract.textPrimaryColor
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignHCenter
    }
    Text {
        id: hintLabel
        objectName: "empty-hint"
        Layout.row: 2
        Layout.column: 0
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
        Layout.row: 3
        Layout.column: 0
        contract: root.contract
        text: root.actionText
        enabled: root.openingEnabled
        Layout.maximumWidth: root.width
        Layout.alignment: Qt.AlignHCenter
        onClicked: root.openAudioRequested(openAudioButton)
    }
}
