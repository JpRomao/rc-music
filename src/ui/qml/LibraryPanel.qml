pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

// ADR-0019: the permanent library page, opening in List view. Only confirmed
// records appear; removing touches the catalog alone.
PageSurface {
    id: root
    NavigationText {
        id: navigationLabels
    }
    LibraryText {
        id: libraryText
    }
    objectName: "library-panel"
    required property ShellUiContract contract
    property var library: null
    property string removeId: ""
    property string removeTitle: ""
    property string notice: ""
    readonly property bool ready: library !== null && library.ready
    readonly property int total: library !== null ? library.count : 0
    readonly property int visibleTotal: library !== null ? library.visibleCount : 0
    embedded: true
    padding: 0
    focus: true
    blockingUiOpen: removeConfirmation.visible
    onSurfaceOpened: {
        if (ready && total > 0)
            filterField.forceActiveFocus(Qt.TabFocusReason);
        else if (retryLoadButton.visible)
            retryLoadButton.forceActiveFocus(Qt.TabFocusReason);
    }
    onClosed: removeConfirmation.close()

    function requestRemove(recordId, title) {
        removeId = recordId;
        removeTitle = title;
        removeConfirmation.open();
    }

    background: Rectangle {
        color: root.contract.canvasColor
    }
    contentItem: ColumnLayout {
        spacing: root.contract.spacingUnit * 2
        Accessible.role: Accessible.Pane
        Accessible.name: navigationLabels.library

        PageHeader {
            Layout.fillWidth: true
            contract: root.contract
            editorial: true
            eyebrow: navigationLabels.libraryEyebrow
            title: navigationLabels.library
            description: navigationLabels.libraryDescription
            legacyTitleSize: root.contract.typeSection
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.library !== null && root.library.statusText.length > 0 && (!root.ready || root.library.canRetrySave)
            Text {
                objectName: "library-status"
                Layout.fillWidth: true
                text: root.library !== null ? root.library.statusText : ""
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: root.contract.textSecondaryColor
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeBody
            }
            AccessibleTransportButton {
                id: retryLoadButton
                objectName: "library-retry-load"
                contract: root.contract
                visible: root.library !== null && !root.ready && !root.library.busy
                text: libraryText.retryLoad
                onClicked: root.library.retryLoad()
            }
            AccessibleTransportButton {
                objectName: "library-retry-save"
                contract: root.contract
                visible: root.library !== null && root.library.canRetrySave
                text: libraryText.retrySave
                onClicked: root.library.retrySave()
            }
        }
        TextField {
            id: filterField
            objectName: "library-filter"
            Layout.fillWidth: true
            visible: root.ready && root.total > 0
            placeholderText: libraryText.filterPlaceholder
            text: root.library !== null ? root.library.filterText : ""
            font.family: root.contract.bodyFontFamily
            font.pixelSize: root.contract.typeBody
            Accessible.name: libraryText.filterName
            onTextEdited: root.library.filterText = text
        }
        Text {
            objectName: "library-count"
            visible: root.ready && root.total > 0
            text: libraryText.countText.arg(root.visibleTotal).arg(root.total)
            textFormat: Text.PlainText
            color: root.contract.textPrimaryColor
            font.family: root.contract.bodyFontFamily
            font.pixelSize: root.contract.typeCaption
        }
        Text {
            objectName: "library-notice"
            visible: root.notice.length > 0
            Layout.fillWidth: true
            text: root.notice
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: root.contract.textSecondaryColor
            font.family: root.contract.bodyFontFamily
            font.pixelSize: root.contract.typeCaption
        }
        ColumnLayout {
            objectName: "library-empty"
            visible: root.ready && root.total === 0
            Layout.fillWidth: true
            spacing: root.contract.spacingUnit
            Text {
                text: libraryText.emptyTitle
                textFormat: Text.PlainText
                color: root.contract.textPrimaryColor
                font.family: root.contract.headingFontFamily
                font.pixelSize: root.contract.typeSubtitle
            }
            Text {
                Layout.fillWidth: true
                text: libraryText.emptyAdvice
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: root.contract.textSecondaryColor
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeBody
            }
        }
        Text {
            objectName: "library-no-matches"
            visible: root.ready && root.total > 0 && root.visibleTotal === 0
            Layout.fillWidth: true
            text: libraryText.noMatches.arg(root.library !== null ? root.library.filterText.trim() : "")
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: root.contract.textSecondaryColor
            font.family: root.contract.bodyFontFamily
            font.pixelSize: root.contract.typeBody
        }
        RowLayout {
            objectName: "library-columns"
            visible: root.ready && root.visibleTotal > 0
            Layout.fillWidth: true
            spacing: root.contract.spacingUnit * 2
            Repeater {
                model: [libraryText.numberHeader, libraryText.trackHeader, libraryText.albumHeader, libraryText.timeHeader]
                delegate: Text {
                    required property string modelData
                    required property int index
                    Layout.preferredWidth: index === 0 ? 32 : (index === 3 ? 56 : -1)
                    Layout.fillWidth: index === 1 || index === 2
                    text: modelData
                    textFormat: Text.PlainText
                    color: root.contract.textSecondaryColor
                    font.family: root.contract.bodyFontFamily
                    font.pixelSize: root.contract.typeEyebrow
                    font.letterSpacing: 1.2
                    Accessible.ignored: true
                }
            }
            Item {
                Layout.preferredWidth: 80
            }
        }
        ListView {
            id: list
            objectName: "library-list"
            visible: root.ready && root.visibleTotal > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 640)
            clip: true
            reuseItems: true
            activeFocusOnTab: true
            keyNavigationEnabled: true
            model: root.library !== null ? root.library.model : null
            Accessible.role: Accessible.List
            Accessible.name: navigationLabels.library
            delegate: Rectangle {
                id: row
                required property int index
                required property string recordId
                required property string title
                required property string artist
                required property string album
                required property string durationText
                required property bool available
                objectName: "library-row-" + recordId
                width: list.width
                height: 60
                color: hover.hovered || ListView.isCurrentItem ? root.contract.hoverColor : "transparent"
                Accessible.role: Accessible.ListItem
                Accessible.name: title + (artist.length > 0 ? ", " + artist : "") + (available ? "" : ", " + libraryText.unavailable)
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
                            objectName: "library-row-title"
                            Layout.fillWidth: true
                            text: row.title
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: row.available ? root.contract.textPrimaryColor : root.contract.textSecondaryColor
                            font.family: root.contract.bodyFontFamily
                            font.pixelSize: root.contract.typeBody
                            Accessible.ignored: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: row.available ? (row.artist.length > 0 ? row.artist : libraryText.unknownArtist) : libraryText.unavailable
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: root.contract.textSecondaryColor
                            font.family: root.contract.bodyFontFamily
                            font.pixelSize: root.contract.typeCaption
                            Accessible.ignored: true
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: row.album
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: root.contract.textSecondaryColor
                        font.family: root.contract.bodyFontFamily
                        font.pixelSize: root.contract.typeCaption
                        Accessible.ignored: true
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
                        objectName: "library-enqueue"
                        contract: root.contract
                        flat: true
                        iconName: "plus"
                        enabled: row.available
                        accessibleName: libraryText.enqueue.arg(row.title)
                        onClicked: root.notice = root.library.enqueue(row.recordId) ? libraryText.enqueued.arg(row.title) : libraryText.enqueueFailed
                    }
                    AccessibleTransportButton {
                        objectName: "library-remove"
                        contract: root.contract
                        flat: true
                        iconName: "minus"
                        accessibleName: libraryText.remove.arg(row.title)
                        onClicked: root.requestRemove(row.recordId, row.title)
                    }
                }
            }
        }
        Item {
            Layout.fillHeight: true
        }
    }
    DownloadConfirmation {
        id: removeConfirmation
        objectName: "library-remove-confirmation"
        contract: root.contract
        host: root
        title: libraryText.removeTitle.arg(root.removeTitle)
        advice: libraryText.removeAdvice
        cancelText: libraryText.cancel
        confirmText: libraryText.removeConfirm
        cancelObjectName: "library-remove-cancel"
        confirmObjectName: "library-remove-confirm"
        onCancelRequested: close()
        onConfirmRequested: {
            root.library.remove(root.removeId);
            close();
        }
    }
}
