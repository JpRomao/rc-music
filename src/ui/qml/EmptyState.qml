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
    property bool editorial: false
    readonly property bool spread: editorial && width >= 500
    readonly property real coverExtent: editorial ? (width < 660 ? 130 : 204) : (compact && parent ? Math.max(0, Math.min(128, parent.height - titleLabel.implicitHeight - hintLabel.implicitHeight - openAudioButton.implicitHeight - rowSpacing * 3 - 4)) : 192)
    signal openAudioRequested(Item opener)
    columns: spread ? 2 : 1
    rowSpacing: contract.spacingUnit * 4
    columnSpacing: editorial ? 30 : 0

    function focusPrimary() {
        openAudioButton.forceActiveFocus(Qt.TabFocusReason);
    }

    TrackArtwork {
        objectName: root.editorial ? "cover-placeholder" : ""
        fallbackObjectName: root.editorial ? "" : "cover-placeholder"
        Layout.row: 0
        Layout.column: 0
        Layout.rowSpan: root.spread ? 3 : 1
        Layout.preferredWidth: root.coverExtent
        Layout.preferredHeight: root.coverExtent
        contract: root.contract
        frozen: !root.editorial
        fallbackSource: root.contract.placeholderSource
        Layout.alignment: Qt.AlignHCenter
        Accessible.ignored: true
    }
    Text {
        id: titleLabel
        objectName: "empty-title"
        Layout.row: root.spread ? 0 : 1
        Layout.column: root.spread ? 1 : 0
        text: root.titleText
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        font: root.editorial ? Qt.font({
            family: root.contract.headingFontFamily,
            pixelSize: 30
        }) : root.contract.applicationFont
        horizontalAlignment: root.spread ? Text.AlignLeft : Text.AlignHCenter
        color: root.contract.textPrimaryColor
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignHCenter
    }
    Text {
        id: hintLabel
        objectName: "empty-hint"
        Layout.row: root.spread ? 1 : 2
        Layout.column: root.spread ? 1 : 0
        text: root.hintText
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        horizontalAlignment: root.spread ? Text.AlignLeft : Text.AlignHCenter
        color: root.contract.textSecondaryColor
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignHCenter
    }
    AccessibleActionButton {
        id: openAudioButton
        controlId: "open-audio"
        Layout.row: root.spread ? 2 : 3
        Layout.column: root.spread ? 1 : 0
        contract: root.contract
        text: root.actionText
        enabled: root.openingEnabled
        Layout.maximumWidth: root.width
        Layout.alignment: root.spread ? Qt.AlignLeft : Qt.AlignHCenter
        onClicked: root.openAudioRequested(openAudioButton)
    }
}
