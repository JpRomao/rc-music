pragma ComponentBehavior: Bound

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
    readonly property int queueCount: queueShelf.count
    readonly property bool libraryShown: library !== null && library.ready && library.count > 0
    readonly property Item openAudioControl: openAudioButton
    signal openAudioRequested(Item opener)
    signal detailsRequested(Item opener)
    signal lyricsRequested(Item opener)
    signal audioFilesOpening(Item opener)
    signal queueRequested
    signal libraryRequested
    signal playRequested(string occurrenceId)
    spacing: 26

    NavigationText {
        id: navigationText
    }
    QueueText {
        id: queueText
    }
    LibraryText {
        id: libraryText
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
    HomeShelf {
        id: queueShelf
        Layout.fillWidth: true
        visible: root.presenter.queueLoaded && count > 0
        contract: root.contract
        controlId: "home-queue-shelf"
        listObjectName: "home-queue-list"
        linkObjectName: "home-see-queue"
        title: navigationText.queueShelf
        subtitle: root.presenter.nextQueueTitle.length > 0 ? navigationText.fillTwo(navigationText.queueNext, queueText.trackCount(count), root.presenter.nextQueueTitle) : queueText.trackCount(count)
        linkText: navigationText.seeQueue
        linkEnabled: root.openingEnabled && count > 0
        cardsFocusable: root.openingEnabled
        model: root.presenter.queueModel
        onLinkTriggered: root.queueRequested()
        // The model object, not required roles: the card has its own title, and the fixture's queue has no cover role.
        delegate: HomeShelfCard {
            required property var model
            readonly property string occurrenceId: model.occurrenceId
            readonly property string displayTitle: model.title.length > 0 ? model.title : model.fileName
            readonly property bool available: model.available
            readonly property bool current: occurrenceId === root.presenter.currentOccurrenceId
            contract: root.contract
            title: displayTitle
            subtitle: available ? (model.artist.length > 0 ? model.artist : navigationText.localAudio) : queueText.sourceUnavailable
            subtitleWarning: !available
            artworkSource: current && root.presenter.currentArtworkSource.toString().length > 0 ? root.presenter.currentArtworkSource : (model.artworkSource ?? "")
            playing: current
            enabled: root.openingEnabled && available
            accessibleName: navigationText.playTrack.arg(displayTitle)
            onActivated: root.playRequested(occurrenceId)
        }
    }
    HomeShelf {
        id: libraryShelf
        Layout.fillWidth: true
        visible: root.libraryShown
        contract: root.contract
        controlId: "home-library-shelf"
        listObjectName: "home-library-list"
        linkObjectName: "home-see-library"
        title: navigationText.libraryShelf
        subtitle: navigationText.trackCount(root.libraryShown ? root.library.count : 0)
        linkText: navigationText.seeLibrary
        linkEnabled: root.openingEnabled
        model: root.libraryShown ? root.library.recentModel : null
        onLinkTriggered: root.libraryRequested()
        delegate: HomeShelfCard {
            required property var model
            required property string recordId
            required property string artist
            required property bool available
            readonly property string artistText: artist.length > 0 ? artist : navigationText.localAudio
            readonly property string artworkBase: root.library !== null ? root.library.artworkBase : ""
            contract: root.contract
            interactive: false
            title: model.title
            subtitle: available ? artistText : libraryText.unavailable
            subtitleWarning: !available
            artworkSource: artworkBase.length > 0 ? artworkBase + recordId : ""
            accessibleName: navigationText.fillTwo(navigationText.titleAndArtist, model.title, artistText)
        }
    }
}
