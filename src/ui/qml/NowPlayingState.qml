import QtQuick
import QtQuick.Layouts
import MusicPlayer.M0

GridLayout {
    id: root
    objectName: "playing-state"
    required property ShellUiContract contract
    required property PlayerPresenter presenter
    property bool compact: false
    readonly property bool editorial: !presenter.usesFrozenFixture
    readonly property bool spread: editorial && width >= 560
    readonly property real coverExtent: editorial ? (width < 660 ? 130 : (width < 800 ? 158 : 204)) : (compact && parent ? Math.max(0, Math.min(128, parent.height - titleLabel.implicitHeight - artistLabel.implicitHeight - albumLabel.implicitHeight - openAudioButton.implicitHeight - detailsButton.implicitHeight - rowSpacing * 5 - 4)) : 192)
    columns: spread ? 2 : 1
    columnSpacing: editorial ? 30 : 0
    rowSpacing: editorial ? 10 : 5
    NavigationText {
        id: labels
    }
    signal openAudioRequested(Item opener)
    signal detailsRequested(Item opener)

    function focusPrimary() {
        openAudioButton.forceActiveFocus(Qt.TabFocusReason);
    }

    TrackArtwork {
        objectName: root.editorial ? "cover-track" : ""
        fallbackObjectName: root.editorial ? "" : "cover-track"
        imageObjectName: "embedded-artwork"
        Layout.row: 0
        Layout.column: 0
        Layout.rowSpan: root.spread ? 5 : 1
        Layout.preferredWidth: root.coverExtent
        Layout.preferredHeight: root.coverExtent
        Layout.alignment: root.spread ? Qt.AlignTop : Qt.AlignHCenter
        contract: root.contract
        source: root.presenter.currentArtworkSource
        frozen: !root.editorial
    }
    Text {
        Layout.row: root.spread ? 0 : 1
        Layout.column: root.spread ? 1 : 0
        Layout.fillWidth: root.spread
        visible: root.editorial
        text: root.presenter.playbackState === PlayerPresenter.Loading ? labels.loadingTrack : (root.presenter.playbackState === PlayerPresenter.Playing ? labels.nowPlaying : (root.presenter.playbackState === PlayerPresenter.Paused ? labels.paused : labels.ready))
        font.family: root.contract.bodyFontFamily
        font.pixelSize: root.contract.typeEyebrow
        font.letterSpacing: 1.4
        color: root.contract.textSecondaryColor
        textFormat: Text.PlainText
    }
    Text {
        id: titleLabel
        objectName: "current-title"
        Layout.row: root.spread ? 1 : (root.editorial ? 2 : 1)
        Layout.column: root.spread ? 1 : 0
        Layout.fillWidth: root.spread
        Layout.minimumWidth: 0
        font: root.presenter.usesFrozenFixture ? root.contract.applicationFont : Qt.font({
            family: root.contract.headingFontFamily,
            pixelSize: root.spread ? 38 : 30
        })
        text: root.presenter.currentTitle
        textFormat: Text.PlainText
        elide: Text.ElideRight
        Accessible.name: text
        color: root.contract.textPrimaryColor
        Layout.maximumWidth: root.width
        Layout.alignment: root.spread ? Qt.AlignLeft : Qt.AlignHCenter
    }
    Text {
        id: artistLabel
        objectName: "current-artist"
        Layout.row: root.spread ? 2 : (root.editorial ? 3 : 2)
        Layout.column: root.spread ? 1 : 0
        Layout.fillWidth: root.spread
        Layout.minimumHeight: root.editorial ? 18 : 0
        text: root.presenter.currentArtist
        textFormat: Text.PlainText
        elide: Text.ElideRight
        Accessible.name: text
        color: root.contract.textSecondaryColor
        Layout.maximumWidth: root.width
        Layout.alignment: root.spread ? Qt.AlignLeft : Qt.AlignHCenter
    }
    Text {
        id: albumLabel
        objectName: "current-album"
        Layout.row: root.spread ? 3 : (root.editorial ? 4 : 3)
        Layout.column: root.spread ? 1 : 0
        Layout.fillWidth: root.spread
        Layout.minimumHeight: root.editorial ? 18 : 0
        text: root.presenter.currentAlbum
        textFormat: Text.PlainText
        elide: Text.ElideRight
        Accessible.name: text
        color: root.contract.textSecondaryColor
        Layout.maximumWidth: root.width
        Layout.alignment: root.spread ? Qt.AlignLeft : Qt.AlignHCenter
    }
    GridLayout {
        Layout.row: root.spread ? 4 : (root.editorial ? 5 : 4)
        Layout.column: root.spread ? 1 : 0
        Layout.alignment: root.spread ? Qt.AlignLeft : Qt.AlignHCenter
        columns: root.spread ? 2 : 1
        columnSpacing: root.editorial ? 12 : 5
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
