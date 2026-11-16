pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

// ADR-0019: online search through the packaged yt-dlp, metadata only. Choosing
// a result sends an ordinary download request by video ID; the audio arrives in
// the library and the queue through Activity.
PageSurface {
    id: root
    NavigationText {
        id: navigationLabels
    }
    SearchText {
        id: searchText
    }
    objectName: "search-panel"
    required property ShellUiContract contract
    property var onlineSearch: null
    readonly property bool available: onlineSearch !== null && onlineSearch.available
    readonly property bool busy: available && onlineSearch.busy
    readonly property var results: available ? onlineSearch.results : []
    embedded: true
    padding: 0
    focus: true
    onSurfaceOpened: {
        if (available)
            queryField.forceActiveFocus(Qt.TabFocusReason);
    }

    function submit() {
        if (available && !busy && queryField.text.trim().length > 0)
            onlineSearch.search(queryField.text);
    }

    background: Rectangle {
        color: root.contract.canvasColor
    }
    contentItem: ColumnLayout {
        spacing: root.contract.spacingUnit * 2
        Accessible.role: Accessible.Pane
        Accessible.name: navigationLabels.search

        PageHeader {
            Layout.fillWidth: true
            contract: root.contract
            editorial: true
            eyebrow: navigationLabels.searchEyebrow
            title: navigationLabels.search
            description: navigationLabels.searchDescription
            legacyTitleSize: root.contract.typeSection
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.available
            spacing: root.contract.spacingUnit
            TextField {
                id: queryField
                objectName: "search-field"
                Layout.fillWidth: true
                enabled: !root.busy
                maximumLength: 400
                placeholderText: searchText.placeholder
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeBody
                Accessible.name: searchText.fieldName
                onAccepted: root.submit()
            }
            AccessibleTransportButton {
                objectName: "search-submit"
                contract: root.contract
                visible: !root.busy
                enabled: queryField.text.trim().length > 0
                iconName: "search"
                iconWithText: true
                text: searchText.submit
                onClicked: root.submit()
            }
            AccessibleTransportButton {
                objectName: "search-cancel"
                contract: root.contract
                visible: root.busy
                text: searchText.cancel
                onClicked: root.onlineSearch.cancel()
            }
        }
        Text {
            objectName: "search-status"
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.onlineSearch !== null ? root.onlineSearch.statusText : searchText.unavailable
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: root.contract.textSecondaryColor
            font.family: root.contract.bodyFontFamily
            font.pixelSize: root.contract.typeBody
        }
        ListView {
            id: list
            objectName: "search-results"
            visible: root.results.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 640)
            clip: true
            activeFocusOnTab: true
            keyNavigationEnabled: true
            model: root.results
            Accessible.role: Accessible.List
            Accessible.name: searchText.resultsName
            delegate: Rectangle {
                id: row
                required property int index
                required property var modelData
                readonly property string videoId: modelData.videoId
                readonly property string title: modelData.title
                readonly property string channel: modelData.channel
                readonly property string durationText: modelData.durationText
                readonly property bool requested: modelData.requested
                objectName: "search-row-" + videoId
                width: list.width
                height: 60
                color: hover.hovered || ListView.isCurrentItem ? root.contract.hoverColor : "transparent"
                Accessible.role: Accessible.ListItem
                Accessible.name: title + (channel.length > 0 ? ", " + channel : "") + (durationText.length > 0 ? ", " + durationText : "")
                HoverHandler {
                    id: hover
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: root.contract.highContrast ? root.contract.borderColor : root.contract.dividerColor
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: root.contract.spacingUnit
                    anchors.rightMargin: root.contract.spacingUnit
                    spacing: root.contract.spacingUnit * 2
                    Text {
                        Layout.preferredWidth: 32
                        text: String(row.index + 1).padStart(2, "0")
                        textFormat: Text.PlainText
                        color: root.contract.textSecondaryColor
                        font.family: root.contract.bodyFontFamily
                        font.pixelSize: root.contract.typeCaption
                        Accessible.ignored: true
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            text: row.title
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: root.contract.textPrimaryColor
                            font.family: root.contract.bodyFontFamily
                            font.pixelSize: root.contract.typeBody
                            Accessible.ignored: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: row.channel.length > 0 ? row.channel : searchText.unknownChannel
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: root.contract.textSecondaryColor
                            font.family: root.contract.bodyFontFamily
                            font.pixelSize: root.contract.typeCaption
                            Accessible.ignored: true
                        }
                    }
                    Text {
                        Layout.preferredWidth: 56
                        horizontalAlignment: Text.AlignRight
                        text: row.durationText
                        textFormat: Text.PlainText
                        color: root.contract.textSecondaryColor
                        font.family: root.contract.bodyFontFamily
                        font.pixelSize: root.contract.typeCaption
                        Accessible.ignored: true
                    }
                    AccessibleTransportButton {
                        objectName: "search-preview"
                        contract: root.contract
                        flat: true
                        iconName: "browser"
                        iconWithText: true
                        enabled: root.available
                        text: searchText.preview
                        accessibleName: searchText.previewName.arg(row.title)
                        onClicked: root.onlineSearch.openPreview(row.videoId)
                    }
                    AccessibleTransportButton {
                        objectName: "search-download"
                        contract: root.contract
                        flat: true
                        iconName: "arrow-down"
                        iconWithText: true
                        enabled: !row.requested && root.available && !root.onlineSearch.requesting
                        text: row.requested ? searchText.requested : searchText.download
                        accessibleName: (row.requested ? searchText.requestedName : searchText.downloadName).arg(row.title)
                        onClicked: root.onlineSearch.download(row.videoId)
                    }
                }
            }
        }
        Item {
            Layout.fillHeight: true
        }
    }
}
