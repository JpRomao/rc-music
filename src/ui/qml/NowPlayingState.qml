import QtQuick
import QtQuick.Layouts
import MusicPlayer.M0

GridLayout {
    id: root
    objectName: "playing-state"
    required property ShellUiContract contract
    required property PlayerPresenter presenter
    property bool compact: false
    readonly property real coverExtent: compact && parent ? Math.max(0, Math.min(128, parent.height - titleLabel.implicitHeight - artistLabel.implicitHeight - albumLabel.implicitHeight - openAudioButton.implicitHeight - detailsButton.implicitHeight - rowSpacing * 5 - 4)) : 192
    columns: 1
    rowSpacing: 5
    signal openAudioRequested(Item opener)
    signal detailsRequested(Item opener)

    function focusPrimary() {
        openAudioButton.forceActiveFocus(Qt.TabFocusReason);
    }

    TrackArtwork {
        fallbackObjectName: "cover-track"
        imageObjectName: "embedded-artwork"
        Layout.row: 0
        Layout.column: 0
        Layout.preferredWidth: root.coverExtent
        Layout.preferredHeight: root.coverExtent
        Layout.alignment: Qt.AlignHCenter
        contract: root.contract
        source: root.presenter.currentArtworkSource
        frozen: true
    }
    Text {
        id: titleLabel
        objectName: "current-title"
        Layout.row: 1
        Layout.column: 0
        Layout.minimumWidth: 0
        font: root.contract.applicationFont
        text: root.presenter.currentTitle
        textFormat: Text.PlainText
        elide: Text.ElideRight
        Accessible.name: text
        color: root.contract.textPrimaryColor
        Layout.maximumWidth: root.width
        Layout.alignment: Qt.AlignHCenter
    }
    Text {
        id: artistLabel
        objectName: "current-artist"
        Layout.row: 2
        Layout.column: 0
        text: root.presenter.currentArtist
        textFormat: Text.PlainText
        elide: Text.ElideRight
        Accessible.name: text
        color: root.contract.textSecondaryColor
        Layout.maximumWidth: root.width
        Layout.alignment: Qt.AlignHCenter
    }
    Text {
        id: albumLabel
        objectName: "current-album"
        Layout.row: 3
        Layout.column: 0
        text: root.presenter.currentAlbum
        textFormat: Text.PlainText
        elide: Text.ElideRight
        Accessible.name: text
        color: root.contract.textSecondaryColor
        Layout.maximumWidth: root.width
        Layout.alignment: Qt.AlignHCenter
    }
    GridLayout {
        Layout.row: 4
        Layout.column: 0
        Layout.alignment: Qt.AlignHCenter
        columns: 1
        columnSpacing: 5
        rowSpacing: 5
        AccessibleActionButton {
            id: openAudioButton
            controlId: "open-another-audio"
            contract: root.contract
            text: root.contract.openAudioText
            enabled: !root.presenter.exiting && !root.presenter.selectingAudioFiles && root.presenter.locatingOccurrenceId.length === 0
            Layout.alignment: Qt.AlignHCenter
            onClicked: root.openAudioRequested(openAudioButton)
        }
        AccessibleTransportButton {
            id: detailsButton
            objectName: "track-details-playing"
            contract: root.contract
            text: root.contract.detailsTitle
            accessibleName: root.contract.detailsTitle
            enabled: root.presenter.detailsAvailable
            Layout.alignment: Qt.AlignHCenter
            onClicked: root.detailsRequested(detailsButton)
        }
    }
}
