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
    readonly property Item openAudioControl: openAudioButton
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
            return hero.emptyScope;
        if (state === PlayerPresenter.Error)
            return hero.errorScope;
        return hero.nowPlayingScope;
    }
    // The open-audio action lives in the header, outside every state scope, so only the error has a primary of its own.
    function focusPrimary(state) {
        if (state === PlayerPresenter.Error)
            hero.errorState.focusPrimary();
        else
            openAudioButton.forceActiveFocus(Qt.TabFocusReason);
    }

    PageHeader {
        objectName: "home-header"
        Layout.fillWidth: true
        contract: root.contract
        eyebrow: navigationText.space
        title: navigationText.heading
        description: navigationText.pageDescription
        AuroraButton {
            id: openAudioButton
            contract: root.contract
            controlId: "open-audio"
            kind: AuroraButton.Primary
            iconName: "folder-open"
            text: root.contract.openAudioText
            enabled: root.openingEnabled
            onClicked: root.openAudioRequested(openAudioButton)
        }
    }
    HomeHero {
        id: hero
        Layout.fillWidth: true
        contract: root.contract
        presenter: root.presenter
        labels: root.labels
        compact: root.compact
        lyricsAvailable: root.lyricsAvailable
        locationFocusRestoreAllowed: root.locationFocusRestoreAllowed
        onDetailsRequested: opener => root.detailsRequested(opener)
        onLyricsRequested: opener => root.lyricsRequested(opener)
        onAudioFilesOpening: opener => root.audioFilesOpening(opener)
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
