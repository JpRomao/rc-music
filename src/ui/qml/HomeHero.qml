import QtQuick
import QtQuick.Layouts
import MusicPlayer.M0

// The Aurora home's main card: the empty piece, the current track or the error, one at a time.
Rectangle {
    id: root
    objectName: "home-hero"
    required property ShellUiContract contract
    required property PlayerPresenter presenter
    required property ErrorText labels
    property bool compact: false
    property bool lyricsAvailable: false
    property bool locationFocusRestoreAllowed: true
    // The focus scopes of the playback states: their items vanish with the state.
    readonly property alias emptyScope: emptyPiece
    readonly property alias nowPlayingScope: playingState
    readonly property alias errorScope: errorItem
    readonly property alias errorState: errorItem
    readonly property int padding: compact ? 14 : 22
    readonly property bool emptyShown: presenter.playbackState === PlayerPresenter.Unloaded
    readonly property bool errorShown: presenter.playbackState === PlayerPresenter.Error
    readonly property string artistAlbumText: presenter.currentArtist.length > 0 && presenter.currentAlbum.length > 0 ? transportText.artistAlbum.arg(presenter.currentArtist).arg(presenter.currentAlbum) : (presenter.currentArtist.length > 0 ? presenter.currentArtist : presenter.currentAlbum)
    signal detailsRequested(Item opener)
    signal lyricsRequested(Item opener)
    signal audioFilesOpening(Item opener)

    implicitHeight: (emptyShown ? emptyPiece.implicitHeight : (errorShown ? errorItem.implicitHeight : playingState.implicitHeight)) + padding * 2
    color: contract.surfaceColor
    radius: contract.radiusLarge
    border.width: contract.highContrast ? 2 : 0
    border.color: contract.textPrimaryColor

    NavigationText {
        id: navigationText
    }
    TransportText {
        id: transportText
    }
    // The artist line keeps its slot whether it has text or not, so the buttons below never move.
    FontMetrics {
        id: bodyMetrics
        font.family: root.contract.bodyFontFamily
        font.pixelSize: root.contract.typeBody
    }
    AuroraEmptyState {
        id: emptyPiece
        x: root.padding
        y: root.padding
        width: Math.max(0, parent.width - root.padding * 2)
        visible: root.emptyShown
        contract: root.contract
        controlId: "home-empty"
        iconName: "folder-open"
        title: root.contract.emptyTitle
        hint: root.contract.emptyHint
    }
    RowLayout {
        id: playingState
        objectName: "playing-state"
        x: root.padding
        y: root.padding
        width: Math.max(0, parent.width - root.padding * 2)
        visible: !root.emptyShown && !root.errorShown
        spacing: root.compact ? 16 : 28
        TrackArtwork {
            objectName: "cover-track"
            imageObjectName: "embedded-artwork"
            Layout.preferredWidth: root.compact ? 96 : 168
            Layout.preferredHeight: root.compact ? 96 : 168
            Layout.alignment: Qt.AlignVCenter
            contract: root.contract
            source: root.presenter.currentArtworkSource
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignVCenter
            spacing: 0
            Text {
                objectName: "hero-eyebrow"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.presenter.playbackState === PlayerPresenter.Loading ? navigationText.loadingTrack : (root.presenter.playbackState === PlayerPresenter.Playing ? navigationText.nowPlaying : (root.presenter.playbackState === PlayerPresenter.Paused ? navigationText.paused : navigationText.ready))
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeEyebrow
                font.letterSpacing: 1.4
                color: root.contract.textSecondaryColor
                textFormat: Text.PlainText
                elide: Text.ElideRight
            }
            Text {
                objectName: "current-title"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.topMargin: 6
                text: root.presenter.currentTitle
                font.family: root.contract.headingFontFamily
                font.pixelSize: root.compact ? root.contract.typeSection : root.contract.typeDisplay
                color: root.contract.textPrimaryColor
                textFormat: Text.PlainText
                elide: Text.ElideRight
                maximumLineCount: 1
                Accessible.name: text
            }
            Text {
                objectName: "hero-artist-album"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredHeight: Math.ceil(bodyMetrics.height)
                Layout.topMargin: 6
                text: root.artistAlbumText
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeBody
                color: root.contract.textSecondaryColor
                textFormat: Text.PlainText
                elide: Text.ElideRight
                maximumLineCount: 1
                Accessible.name: text
            }
            RowLayout {
                Layout.topMargin: root.compact ? 10 : 20
                spacing: root.compact ? 8 : 10
                AuroraButton {
                    id: detailsButton
                    contract: root.contract
                    controlId: "track-details-playing"
                    kind: AuroraButton.Tonal
                    iconName: "info"
                    compact: root.compact
                    text: root.compact ? navigationText.detailsShort : root.contract.detailsTitle
                    Accessible.name: root.contract.detailsTitle
                    enabled: root.presenter.detailsAvailable
                    onClicked: root.detailsRequested(detailsButton)
                }
                AuroraButton {
                    id: lyricsButton
                    contract: root.contract
                    controlId: "home-lyrics"
                    kind: AuroraButton.Tonal
                    iconName: "lyrics"
                    compact: root.compact
                    text: transportText.lyricsAction
                    enabled: root.lyricsAvailable
                    onClicked: root.lyricsRequested(lyricsButton)
                }
            }
        }
    }
    ErrorState {
        id: errorItem
        x: root.padding
        y: root.padding
        width: Math.max(0, parent.width - root.padding * 2)
        visible: root.errorShown
        contract: root.contract
        presenter: root.presenter
        labels: root.labels
        locationFocusRestoreAllowed: root.locationFocusRestoreAllowed
        onAudioFilesOpening: opener => root.audioFilesOpening(opener)
        onDetailsRequested: opener => root.detailsRequested(opener)
    }
}
