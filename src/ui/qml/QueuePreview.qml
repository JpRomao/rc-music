pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

ColumnLayout {
    id: root
    objectName: "home-queue-shelf"
    required property ShellUiContract contract
    property var model: null
    property string currentOccurrenceId: ""
    property url currentArtworkSource
    property bool interactionAllowed: true
    readonly property int count: cards.count
    signal playRequested(string occurrenceId)
    signal queueRequested
    spacing: 16
    NavigationText {
        id: labels
    }
    RowLayout {
        Layout.fillWidth: true
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Text {
                text: labels.queueShelf
                font.family: root.contract.headingFontFamily
                font.pixelSize: 25
                color: root.contract.textPrimaryColor
                textFormat: Text.PlainText
            }
            Text {
                text: labels.queueCount.arg(cards.count)
                font.family: root.contract.bodyFontFamily
                font.pixelSize: 12
                color: root.contract.textSecondaryColor
                textFormat: Text.PlainText
            }
        }
        AccessibleTransportButton {
            objectName: "home-see-queue"
            contract: root.contract
            text: labels.seeQueue
            enabled: root.interactionAllowed && cards.count > 0
            onClicked: root.queueRequested()
        }
    }
    ListView {
        id: cards
        objectName: "home-queue-list"
        Layout.fillWidth: true
        implicitHeight: cardWidth + 62
        readonly property real cardWidth: Math.min(190, (width - spacing * (columns - 1)) / columns)
        readonly property int columns: width >= 1000 ? 5 : (width >= 700 ? 3 : 2)
        model: root.model
        orientation: ListView.Horizontal
        spacing: 19
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.horizontal: AccessibleScrollBar {
            contract: root.contract
        }
        delegate: ItemDelegate {
            id: card
            required property int index
            required property string occurrenceId
            required property string title
            required property string fileName
            required property string artist
            required property bool available
            readonly property string displayTitle: title.length > 0 ? title : fileName
            width: cards.cardWidth
            height: cards.height
            padding: 0
            enabled: root.interactionAllowed && available
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: labels.playTrack.arg(displayTitle)
            Accessible.onPressAction: card.clicked()
            onActiveFocusChanged: {
                if (activeFocus) {
                    cards.currentIndex = index;
                    cards.positionViewAtIndex(index, ListView.Contain);
                }
            }
            onClicked: {
                if (enabled)
                    root.playRequested(occurrenceId);
            }
            Keys.onReturnPressed: event => {
                if (enabled)
                    root.playRequested(occurrenceId);
                event.accepted = true;
            }
            background: Rectangle {
                color: "transparent"
                border.color: root.contract.focusColor
                border.width: card.activeFocus ? 3 : 0
                radius: root.contract.radiusSmall
            }
            contentItem: ColumnLayout {
                spacing: 6
                TrackArtwork {
                    objectName: "queue-card-artwork"
                    Layout.fillWidth: true
                    Layout.preferredHeight: cards.cardWidth
                    contract: root.contract
                    source: card.occurrenceId === root.currentOccurrenceId ? root.currentArtworkSource : ""
                }
                Text {
                    Layout.fillWidth: true
                    text: card.displayTitle
                    font.family: root.contract.bodyFontFamily
                    font.pixelSize: 14
                    color: root.contract.textPrimaryColor
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
                Text {
                    Layout.fillWidth: true
                    text: card.artist.length > 0 ? card.artist : labels.localAudio
                    font.family: root.contract.bodyFontFamily
                    font.pixelSize: 12
                    color: root.contract.textSecondaryColor
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
                Item {
                    Layout.fillHeight: true
                }
            }
        }
    }
}
