import QtQuick
import QtQuick.Layouts
import MusicPlayer.M0

ColumnLayout {
    id: root
    objectName: "home-page"
    required property ShellUiContract contract
    required property PlayerPresenter presenter
    required property ErrorText labels
    property var library: null
    property bool compact: false
    property bool lyricsAvailable: false
    property bool openingEnabled: true
    property bool locationFocusRestoreAllowed: true
    readonly property int queueCount: queuePreview.count
    signal openAudioRequested(Item opener)
    signal detailsRequested(Item opener)
    signal lyricsRequested(Item opener)
    signal audioFilesOpening(Item opener)
    signal queueRequested
    signal libraryRequested
    signal playRequested(string occurrenceId)
    spacing: 28

    NavigationText {
        id: navigationText
    }
    // The focus scope that owns a playback state: its items vanish with the state.
    function scopeFor(state) {
        if (state === PlayerPresenter.Unloaded)
            return emptyState;
        if (state === PlayerPresenter.Error)
            return errorState;
        return nowPlayingState;
    }
    function focusPrimary(state) {
        scopeFor(state).focusPrimary();
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10
        Text {
            objectName: "home-heading"
            Layout.fillWidth: true
            text: navigationText.heading
            font.family: root.contract.headingFontFamily
            font.pixelSize: root.contract.typeDisplay
            color: root.contract.textPrimaryColor
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            text: navigationText.pageDescription
            font.family: root.contract.bodyFontFamily
            font.pixelSize: root.contract.typeBody
            color: root.contract.textSecondaryColor
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
        }
    }
    Rectangle {
        id: heroRegion
        objectName: "home-hero"
        Layout.fillWidth: true
        implicitHeight: (root.presenter.playbackState === PlayerPresenter.Unloaded ? emptyState.implicitHeight : (root.presenter.playbackState === PlayerPresenter.Error ? errorState.implicitHeight : nowPlayingState.implicitHeight)) + 44
        color: root.contract.surfaceColor
        radius: root.contract.radiusMedium
        border.width: 1
        border.color: root.contract.borderColor
        EmptyState {
            id: emptyState
            x: 22
            y: 22
            width: Math.max(0, parent.width - 44)
            editorial: true
            compact: root.compact
            visible: root.presenter.playbackState === PlayerPresenter.Unloaded
            contract: root.contract
            openingEnabled: root.openingEnabled
            onOpenAudioRequested: opener => root.openAudioRequested(opener)
        }
        NowPlayingState {
            id: nowPlayingState
            x: 22
            y: 22
            width: Math.max(0, parent.width - 44)
            visible: root.presenter.playbackState !== PlayerPresenter.Unloaded && root.presenter.playbackState !== PlayerPresenter.Error
            contract: root.contract
            presenter: root.presenter
            compact: root.lyricsAvailable || root.compact
            onOpenAudioRequested: opener => root.openAudioRequested(opener)
            onDetailsRequested: opener => root.detailsRequested(opener)
        }
        ErrorState {
            id: errorState
            x: 22
            y: 22
            width: Math.max(0, parent.width - 44)
            visible: root.presenter.playbackState === PlayerPresenter.Error
            contract: root.contract
            presenter: root.presenter
            labels: root.labels
            locationFocusRestoreAllowed: root.locationFocusRestoreAllowed
            onAudioFilesOpening: opener => root.audioFilesOpening(opener)
            onDetailsRequested: opener => root.detailsRequested(opener)
        }
    }
    QueuePreview {
        id: queuePreview
        Layout.fillWidth: true
        visible: count > 0
        contract: root.contract
        model: root.presenter.queueModel
        currentOccurrenceId: root.presenter.currentOccurrenceId
        currentArtworkSource: root.presenter.currentArtworkSource
        interactionAllowed: root.openingEnabled
        onQueueRequested: root.queueRequested()
        onPlayRequested: occurrenceId => root.playRequested(occurrenceId)
    }
}
