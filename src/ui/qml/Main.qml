import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

ApplicationWindow {
    id: window
    objectName: "mainWindow"
    required property ShellUiContract uiContract
    required property PlayerPresenter presenter
    property var downloads: null
    property var embeddedBrowser: null
    property var lyrics: null
    property var playlists: null
    property var library: null
    property bool startupReady: true
    width: uiContract.clientWidth
    height: uiContract.clientHeight
    minimumWidth: uiContract.minimumWidth
    minimumHeight: uiContract.minimumHeight
    title: uiContract.appName
    color: uiContract.canvasColor
    font: presenter.usesFrozenFixture ? uiContract.applicationFont : Qt.font({
        family: uiContract.bodyFontFamily,
        pixelSize: 14
    })
    palette.window: uiContract.canvasColor
    palette.base: uiContract.surfaceColor
    palette.alternateBase: uiContract.surfaceRaisedColor
    palette.windowText: uiContract.textPrimaryColor
    palette.text: uiContract.textPrimaryColor
    palette.button: uiContract.surfaceColor
    palette.buttonText: uiContract.textPrimaryColor
    palette.highlight: uiContract.accentColor
    palette.highlightedText: uiContract.accentTextColor
    palette.mid: uiContract.borderColor
    palette.midlight: uiContract.borderColor
    palette.dark: uiContract.textPrimaryColor
    palette.light: uiContract.surfaceRaisedColor
    palette.placeholderText: uiContract.textSecondaryColor
    contentItem.enabled: !presenter.exiting || downloadFinalSaveDecisionRequired
    visible: startupReady
    signal openAudioRequested
    property bool queueOpen: false
    readonly property bool inlinePages: !presenter.usesFrozenFixture
    readonly property string currentPage: preferencesPanel.visible ? "preferences" : (libraryPanel.visible ? "library" : (playlistsPanel.visible ? "playlists" : (downloadsPanel.visible ? "downloads" : (queuePageActive ? "queue" : "home"))))
    readonly property Item pageReturnFocusItem: inlinePages ? (currentPage === "preferences" ? preferencesPanel.lastFocusItem : (currentPage === "library" ? libraryPanel.lastFocusItem : (currentPage === "playlists" ? playlistsPanel.lastFocusItem : (currentPage === "downloads" ? downloadsPanel.lastFocusItem : null)))) : null
    readonly property bool queuePageActive: queueOpen && presenter.queueLoaded
    property alias queueButton: sideNavigation.queueButton
    property alias preferencesButton: sideNavigation.preferencesButton
    property alias playlistsButton: sideNavigation.playlistsButton
    property alias libraryButton: sideNavigation.libraryButton
    property alias downloadsButton: sideNavigation.downloadsButton
    property alias browserButton: sideNavigation.browserButton
    NavigationText {
        id: navigationText
    }
    ErrorText {
        id: errorText
    }
    SourceImportText {
        id: sourceImportText
    }
    DownloadText {
        id: downloadText
    }
    BrowserText {
        id: browserText
    }
    PlaybackAccessibilityStatus {
        contract: window.uiContract
        presenter: window.presenter
        errorLabels: errorText
    }
    readonly property bool downloadFinalSaveDecisionRequired: downloads !== null && (downloads.canRetryFinalSave || downloads.canCloseAfterFinalSaveFailure)
    readonly property bool windowShortcutsEnabled: (!playlistsPanel.visible || (inlinePages && !isInside(activeFocusItem, playlistsPanel.contentItem))) && (!libraryPanel.visible || !isInside(activeFocusItem, libraryPanel.contentItem)) && !libraryPanel.blockingUiOpen && !lyricsPanel.queryOpen && (!downloadsPanel.visible || (inlinePages && !isInside(activeFocusItem, downloadsPanel.contentItem))) && !playlistsPanel.blockingUiOpen && !downloadsPanel.blockingUiOpen && !preferencesPanel.blockingUiOpen && !browserError.visible && !windowNotice.visible && !queuePanel.clearConfirmationOpen && !queuePanel.contextMenuOpen && !trackDetailsPanel.visible && !preferencePersistenceNotice.replacementConfirmationOpen && !presenter.exiting && !presenter.selectingAudioFiles && presenter.locatingOccurrenceId.length === 0
    readonly property bool navigationShortcutsEnabled: windowShortcutsEnabled && (!preferencesPanel.visible || (inlinePages && !isInside(activeFocusItem, preferencesPanel.contentItem))) && transport.enabled
    readonly property bool transportShortcutsEnabled: navigationShortcutsEnabled && transport.playbackEnabled
    readonly property bool primaryKeysEnabled: windowShortcutsEnabled && (!preferencesPanel.visible || (inlinePages && !isInside(activeFocusItem, preferencesPanel.contentItem))) && !focusOwnsTextInput()
    readonly property bool spaceShortcutEnabled: primaryKeysEnabled
    readonly property bool arrowShortcutsEnabled: primaryKeysEnabled
    readonly property bool seekShortcutsEnabled: arrowShortcutsEnabled && transportShortcutsEnabled && presenter.seekable && !presenter.seekPreviewActive && !presenter.seekPending
    readonly property bool inlinePageClosingAllowed: inlinePages && currentPage !== "home" && currentPage !== "queue" && !playlistsPanel.blockingUiOpen && !libraryPanel.blockingUiOpen && !downloadsPanel.blockingUiOpen && !preferencesPanel.blockingUiOpen && !lyricsPanel.queryOpen && !browserError.visible && !windowNotice.visible && !trackDetailsPanel.visible && !preferencePersistenceNotice.replacementConfirmationOpen && !presenter.exiting && !presenter.selectingAudioFiles && presenter.locatingOccurrenceId.length === 0
    property int previousPlaybackState: -1
    property int playbackFocusRepairGeneration: 0
    property Item lastActiveFocusItem: null
    property Item audioSelectionRestoreItem: null
    property bool audioSelectionPending: false
    property bool audioSelectionTerminalObserved: false
    property int audioSelectionFocusRevision: 0
    property bool playlistExitRecoveryPending: false
    readonly property bool playlistsOpeningAllowed: playlists !== null && !presenter.exiting && !presenter.selectingAudioFiles && presenter.locatingOccurrenceId.length === 0

    Component.onCompleted: {
        previousPlaybackState = presenter.playbackState;
        revealDownloadFinalSaveFailure();
    }
    onActiveFocusItemChanged: {
        sideNavigation.revealFocus(activeFocusItem);
        revealHomeFocus(activeFocusItem);
        if (activeFocusItem && !isPassiveFocusFallback(activeFocusItem))
            lastActiveFocusItem = activeFocusItem;
    }

    function isInside(item, ancestor) {
        while (item) {
            if (item === ancestor)
                return true;
            item = item.parent;
        }
        return false;
    }

    function revealHomeFocus(item) {
        if (!window.inlinePages || window.currentPage !== "home" || !item || !window.isInside(item, stateLayer))
            return;
        const point = item.mapToItem(stateLayer.contentItem, 0, 0);
        if (point.y < stateLayer.contentY)
            stateLayer.contentY = Math.max(0, point.y);
        else if (point.y + item.height > stateLayer.contentY + stateLayer.height)
            stateLayer.contentY = Math.max(0, Math.min(stateLayer.contentHeight - stateLayer.height, point.y + item.height - stateLayer.height));
    }

    function refreshHomeFocus() {
        if (window)
            window.revealHomeFocus(window.activeFocusItem);
    }

    function playbackFocusScope(state) {
        if (state === PlayerPresenter.Unloaded)
            return emptyState;
        if (state === PlayerPresenter.Error)
            return errorState;
        return nowPlayingState;
    }

    function isPassiveFocusFallback(item) {
        return !item || item === contentItem || item === stateLayer || !item.activeFocusOnTab;
    }

    function transportEnabledForState(state) {
        return state === PlayerPresenter.Ready || state === PlayerPresenter.Paused || state === PlayerPresenter.Playing;
    }

    function focusPlaybackPrimary(state) {
        if (window.inlinePages && window.currentPage !== "home" && window.currentPage !== "queue") {
            const button = window.currentPage === "library" ? libraryButton : window.currentPage === "playlists" ? playlistsButton : (window.currentPage === "downloads" ? downloadsButton : preferencesButton);
            if (button.visible && button.enabled)
                button.forceActiveFocus(Qt.TabFocusReason);
            return;
        }
        if (window.queuePageActive) {
            queuePanel.focusCurrent();
            return;
        }
        if (state === PlayerPresenter.Unloaded)
            emptyState.focusPrimary();
        else if (state === PlayerPresenter.Error)
            errorState.focusPrimary();
        else
            nowPlayingState.focusPrimary();
    }

    function repairPlaybackStateFocus() {
        const nextState = presenter.playbackState;
        const oldState = previousPlaybackState;
        const oldScope = playbackFocusScope(previousPlaybackState);
        const nextScope = playbackFocusScope(nextState);
        previousPlaybackState = nextState;
        const focusBeforeTransition = isPassiveFocusFallback(activeFocusItem) ? lastActiveFocusItem : activeFocusItem;
        const focusCameFromDisappearingState = oldScope !== nextScope && isInside(focusBeforeTransition, oldScope);
        const playbackBecameUnavailable = transportEnabledForState(oldState) && !transportEnabledForState(nextState);
        const focusedSeekControlBecameUnavailable = transport.isSeekControl(focusBeforeTransition) && !presenter.seekable;
        const focusCameFromDisabledTransport = isInside(focusBeforeTransition, transport) && ((playbackBecameUnavailable && !transport.isAvailableNavigationControl(focusBeforeTransition)) || focusedSeekControlBecameUnavailable);
        if (!focusCameFromDisappearingState && !focusCameFromDisabledTransport)
            return;
        const repairSource = focusCameFromDisabledTransport ? transport : oldScope;

        const repairGeneration = ++playbackFocusRepairGeneration;
        Qt.callLater(function () {
            if (repairGeneration !== window.playbackFocusRepairGeneration || window.playbackFocusScope(window.presenter.playbackState) !== nextScope)
                return;
            const currentFocus = window.activeFocusItem;
            if (!window.isPassiveFocusFallback(currentFocus) && !window.isInside(currentFocus, repairSource))
                return;
            if (!window.active || !window.windowShortcutsEnabled || preferencesPanel.visible)
                return;
            if (focusCameFromDisabledTransport && window.transportEnabledForState(window.presenter.playbackState))
                transport.focusPrimary();
            else
                window.focusPlaybackPrimary(window.presenter.playbackState);
        });
    }

    Connections {
        target: window.presenter

        function onPlaybackChanged() {
            window.repairPlaybackStateFocus();
        }
        function onPlaylistExitRecoveryRequested() {
            window.playlistExitRecoveryPending = true;
            window.revealPlaylistExitRecovery();
        }
        function onStateChanged() {
            window.revealDownloadFinalSaveFailure();
            if (!window.audioSelectionPending)
                return;
            if (window.presenter.selectingAudioFiles) {
                if (window.audioSelectionTerminalObserved)
                    window.forgetAudioSelection();
                return;
            }
            window.audioSelectionTerminalObserved = true;
            const revision = window.audioSelectionFocusRevision;
            // Cancellation/rebinding need not emit Finished. Give a normal
            // completion its synchronous Finished before discarding ownership.
            Qt.callLater(function () {
                if (revision === window.audioSelectionFocusRevision && window.audioSelectionPending && !window.presenter.selectingAudioFiles)
                    window.forgetAudioSelection();
            });
        }
        function onAudioFileSelectionFinished(changed) {
            if (!window.audioSelectionPending)
                return;
            window.audioSelectionPending = false;
            window.audioSelectionTerminalObserved = false;
            const opener = window.audioSelectionRestoreItem;
            const revision = window.audioSelectionFocusRevision;
            window.audioSelectionRestoreItem = null;
            Qt.callLater(function () {
                if (revision !== window.audioSelectionFocusRevision || !window.active || !window.windowShortcutsEnabled || preferencesPanel.visible)
                    return;
                if (opener && opener.visible && opener.enabled)
                    opener.forceActiveFocus(Qt.TabFocusReason);
                else if (window.queueOpen && queuePanel.visible)
                    queuePanel.focusCurrent();
                else
                    window.focusPlaybackPrimary(window.presenter.playbackState);
            });
        }
    }

    function focusOwnsTextInput() {
        let item = activeFocusItem;
        while (item && item !== contentItem) {
            if (item instanceof TextInput || item instanceof TextEdit)
                return true;
            item = item.parent;
        }
        return false;
    }
    onClosing: close => {
        close.accepted = presenter.exitCommitted;
        if (!close.accepted)
            presenter.requestClose();
    }

    function setQueueOpen(open) {
        if (open && !presenter.queueLoaded)
            return;
        if (window.inlinePages && open) {
            libraryPanel.close();
            playlistsPanel.close();
            downloadsPanel.close();
            preferencesPanel.close();
        }
        queueOpen = open;
        Qt.callLater(function () {
            if (!window.active || !window.windowShortcutsEnabled || preferencesPanel.visible)
                return;
            if (window.queueOpen)
                queuePanel.focusCurrent();
            else if (queueButton.enabled)
                queueButton.forceActiveFocus(Qt.TabFocusReason);
            else
                emptyState.focusPrimary();
        });
    }
    function toggleQueue() {
        setQueueOpen(!queueOpen);
    }
    function prepareAudioSelection(opener) {
        audioSelectionRestoreItem = opener && opener.activeFocus ? opener : activeFocusItem;
        audioSelectionPending = !presenter.usesFrozenFixture;
        audioSelectionTerminalObserved = false;
        ++audioSelectionFocusRevision;
    }
    function forgetAudioSelection() {
        audioSelectionPending = false;
        audioSelectionTerminalObserved = false;
        audioSelectionRestoreItem = null;
        ++audioSelectionFocusRevision;
    }
    function openAudio(opener) {
        if (presenter.exiting || presenter.selectingAudioFiles || presenter.locatingOccurrenceId.length > 0)
            return;
        prepareAudioSelection(opener);
        openAudioRequested();
        if (!presenter.openAudio())
            audioSelectionPending = false;
    }
    function changeVolume(percentagePoints) {
        const percent = presenter.volume * 100;
        presenter.setVolume(Math.max(0, Math.min(100, percent + percentagePoints)) / 100);
    }
    function openPreferences() {
        if ((!window.inlinePages && (playlistsPanel.visible || downloadsPanel.visible)) || presenter.exiting)
            return;
        queueOpen = false;
        if (window.inlinePages) {
            libraryPanel.close();
            playlistsPanel.close();
            downloadsPanel.close();
            trackDetailsPanel.close();
        }
        if (preferencesPanel.opened)
            preferencesPanel.focusFirst();
        else
            preferencesPanel.open();
    }
    function openTrackDetails(opener) {
        if (playlistsPanel.visible || downloadsPanel.visible || presenter.exiting)
            return;
        queueOpen = false;
        trackDetailsPanel.openFrom(opener);
    }
    function openDownloads() {
        if (downloads === null || presenter.selectingAudioFiles || presenter.locatingOccurrenceId.length > 0)
            return;
        queueOpen = false;
        libraryPanel.close();
        playlistsPanel.close();
        preferencesPanel.close();
        trackDetailsPanel.close();
        downloadsPanel.openFrom(downloadsButton);
    }
    function openPlaylists() {
        if (!playlistsOpeningAllowed)
            return;
        queueOpen = false;
        libraryPanel.close();
        preferencesPanel.close();
        trackDetailsPanel.close();
        downloadsPanel.close();
        playlistsPanel.open();
    }
    function openLibrary() {
        if (library === null || !window.inlinePages || presenter.exiting || presenter.selectingAudioFiles || presenter.locatingOccurrenceId.length > 0)
            return;
        queueOpen = false;
        playlistsPanel.close();
        preferencesPanel.close();
        trackDetailsPanel.close();
        downloadsPanel.close();
        libraryPanel.open();
    }
    function revealPlaylistExitRecovery() {
        if (!playlistExitRecoveryPending || !playlistsOpeningAllowed)
            return;
        playlistExitRecoveryPending = false;
        const alreadyOpen = playlistsPanel.opened;
        openPlaylists();
        if (alreadyOpen)
            playlistsPanel.focusSaveRecovery();
    }
    onPlaylistsOpeningAllowedChanged: revealPlaylistExitRecovery()
    function revealDownloadFinalSaveFailure() {
        if (downloadsPanel && presenter.exiting && downloadFinalSaveDecisionRequired && !downloadsPanel.visible)
            openDownloads();
    }
    onDownloadFinalSaveDecisionRequiredChanged: revealDownloadFinalSaveFailure()
    function restoreFocusAfterSourceImport() {
        if (!window.active || !window.windowShortcutsEnabled || preferencesPanel.visible)
            return;
        if (window.queueOpen && queuePanel.visible)
            queuePanel.focusCurrent();
        else if (queueButton.enabled)
            queueButton.forceActiveFocus(Qt.TabFocusReason);
        else
            emptyState.focusPrimary();
    }

    SideNavigation {
        id: sideNavigation
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: playerFooter.top
        width: window.inlinePages ? (window.width <= window.uiContract.compactWidth ? 68 : (window.width <= window.uiContract.wideWidth ? 196 : 224)) : (compact ? 144 : 224)
        compact: window.inlinePages ? window.width <= window.uiContract.compactWidth : window.width < 1000
        editorial: window.inlinePages
        themeHost: window.inlinePages ? workspaceHeader.themeHost : null
        contract: window.uiContract
        queueAvailable: window.presenter.queueLoaded
        queueActive: window.queuePageActive
        activePage: window.currentPage
        pageReturnFocusItem: window.pageReturnFocusItem
        exiting: window.presenter.exiting
        hasPlaylists: window.playlists !== null
        hasLibrary: window.library !== null
        hasDownloads: window.downloads !== null
        hasBrowser: window.embeddedBrowser !== null
        downloadsExitRecovery: window.downloadFinalSaveDecisionRequired
        preferencesOpen: preferencesPanel.visible
        onNowRequested: {
            if (window.inlinePages) {
                libraryPanel.close();
                playlistsPanel.close();
                downloadsPanel.close();
                preferencesPanel.close();
            }
            window.queueOpen = false;
            nowButton.forceActiveFocus(Qt.TabFocusReason);
        }
        onQueueRequested: window.toggleQueue()
        onPreferencesRequested: window.openPreferences()
        onPlaylistsRequested: window.openPlaylists()
        onLibraryRequested: window.openLibrary()
        onDownloadsRequested: window.openDownloads()
        onThemeRequested: window.presenter.setThemePreference(window.uiContract.darkTheme ? PlayerPresenter.LightTheme : PlayerPresenter.DarkTheme)
        onBrowserRequested: {
            window.embeddedBrowser.open();
            if (window.embeddedBrowser !== null && window.embeddedBrowser.errorText.length > 0)
                browserError.open();
        }
    }
    WorkspaceHeader {
        id: workspaceHeader
        anchors.left: sideNavigation.right
        anchors.right: parent.right
        anchors.top: parent.top
        height: implicitHeight
        visible: window.inlinePages
        contract: window.uiContract
        compact: window.width <= window.uiContract.compactWidth
        pageTitle: window.currentPage === "home" ? navigationText.home : (window.currentPage === "queue" ? window.uiContract.queueTitle : (window.currentPage === "library" ? navigationText.library : window.currentPage === "playlists" ? navigationText.playlists : (window.currentPage === "downloads" ? navigationText.activity : navigationText.settings)))
    }
    ColumnLayout {
        id: mainLayout
        anchors.left: sideNavigation.right
        anchors.right: parent.right
        anchors.top: window.inlinePages ? workspaceHeader.bottom : parent.top
        anchors.bottom: playerFooter.top
        anchors.margins: window.inlinePages ? 30 : window.uiContract.spacingUnit * 6
        anchors.leftMargin: window.inlinePages ? (window.width <= window.uiContract.compactWidth ? 23 : 38) : window.uiContract.spacingUnit * 6
        anchors.rightMargin: window.inlinePages ? (window.width <= window.uiContract.compactWidth ? 23 : 38) : window.uiContract.spacingUnit * 6
        Item {
            id: contentArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            Flickable {
                id: stateLayer
                objectName: "state-layer"
                // Aurora keeps the home under the queue drawer, dimmed and out of the Tab order.
                visible: window.inlinePages ? (window.currentPage === "home" || window.currentPage === "queue") : !window.queuePageActive
                enabled: !window.presenter.exiting && !(window.inlinePages && window.queuePageActive)
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                contentWidth: width
                contentHeight: window.inlinePages ? Math.max(height, homeLayout.implicitHeight) : height
                interactive: window.inlinePages
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick
                clip: true
                onHeightChanged: {
                    if (window)
                        Qt.callLater(window.refreshHomeFocus);
                }
                onContentHeightChanged: {
                    if (window)
                        Qt.callLater(window.refreshHomeFocus);
                }
                ScrollBar.vertical: AccessibleScrollBar {
                    contract: window.uiContract
                }
                ColumnLayout {
                    id: homeLayout
                    width: stateLayer.width
                    spacing: 28
                    visible: window.inlinePages
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Text {
                            objectName: "home-heading"
                            Layout.fillWidth: true
                            text: navigationText.heading
                            font.family: window.uiContract.headingFontFamily
                            font.pixelSize: window.uiContract.typeDisplay
                            color: window.uiContract.textPrimaryColor
                            textFormat: Text.PlainText
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            text: navigationText.pageDescription
                            font.family: window.uiContract.bodyFontFamily
                            font.pixelSize: window.uiContract.typeBody
                            color: window.uiContract.textSecondaryColor
                            textFormat: Text.PlainText
                            wrapMode: Text.WordWrap
                        }
                    }
                    Rectangle {
                        id: heroRegion
                        objectName: "home-hero"
                        Layout.fillWidth: true
                        implicitHeight: (window.presenter.playbackState === PlayerPresenter.Unloaded ? emptyState.implicitHeight : (window.presenter.playbackState === PlayerPresenter.Error ? errorState.implicitHeight : nowPlayingState.implicitHeight)) + 44
                        color: window.uiContract.surfaceColor
                        radius: window.uiContract.radiusMedium
                        border.width: 1
                        border.color: window.uiContract.borderColor
                    }
                    QueuePreview {
                        id: queuePreview
                        Layout.fillWidth: true
                        visible: count > 0
                        contract: window.uiContract
                        model: window.inlinePages ? window.presenter.queueModel : null
                        currentOccurrenceId: window.presenter.currentOccurrenceId
                        currentArtworkSource: window.presenter.currentArtworkSource
                        interactionAllowed: !window.presenter.exiting && !window.presenter.selectingAudioFiles && window.presenter.locatingOccurrenceId.length === 0
                        onQueueRequested: window.setQueueOpen(true)
                        onPlayRequested: occurrenceId => window.presenter.playQueueOccurrence(occurrenceId)
                    }
                }
                Text {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    visible: !window.inlinePages && parent.height > 500
                    text: navigationText.heading
                    textFormat: Text.PlainText
                    font.family: window.uiContract.headingFontFamily
                    font.pixelSize: window.uiContract.typeHeading
                    color: window.uiContract.textPrimaryColor
                    elide: Text.ElideRight
                }
                EmptyState {
                    id: emptyState
                    parent: window.inlinePages ? heroRegion : stateLayer.contentItem
                    anchors.centerIn: window.inlinePages ? undefined : parent
                    x: window.inlinePages ? 22 : 0
                    y: window.inlinePages ? 22 : 0
                    width: window.inlinePages ? Math.max(0, parent.width - 44) : Math.min(parent.width > 0 ? parent.width : 560, 560)
                    editorial: window.inlinePages
                    compact: stateLayer.height < 400
                    visible: window.presenter.playbackState === PlayerPresenter.Unloaded
                    contract: window.uiContract
                    openingEnabled: !window.presenter.exiting && !window.presenter.selectingAudioFiles && window.presenter.locatingOccurrenceId.length === 0
                    onOpenAudioRequested: opener => window.openAudio(opener)
                }
                NowPlayingState {
                    id: nowPlayingState
                    parent: window.inlinePages ? heroRegion : stateLayer.contentItem
                    anchors.centerIn: window.inlinePages ? undefined : parent
                    x: window.inlinePages ? 22 : 0
                    y: window.inlinePages ? 22 : 0
                    width: window.inlinePages ? Math.max(0, parent.width - 44) : Math.min(parent.width, 560)
                    visible: window.presenter.playbackState !== PlayerPresenter.Unloaded && window.presenter.playbackState !== PlayerPresenter.Error
                    contract: window.uiContract
                    presenter: window.presenter
                    compact: lyricsPanel.visible || stateLayer.height < 400
                    onOpenAudioRequested: opener => window.openAudio(opener)
                    onDetailsRequested: opener => window.openTrackDetails(opener)
                }
                ErrorState {
                    id: errorState
                    parent: window.inlinePages ? heroRegion : stateLayer.contentItem
                    anchors.centerIn: window.inlinePages ? undefined : parent
                    x: window.inlinePages ? 22 : 0
                    y: window.inlinePages ? 22 : 0
                    width: window.inlinePages ? Math.max(0, parent.width - 44) : implicitWidth
                    visible: window.presenter.playbackState === PlayerPresenter.Error
                    contract: window.uiContract
                    presenter: window.presenter
                    labels: errorText
                    locationFocusRestoreAllowed: window.active && window.windowShortcutsEnabled && !preferencesPanel.visible
                    onAudioFilesOpening: opener => window.prepareAudioSelection(opener)
                    onDetailsRequested: opener => window.openTrackDetails(opener)
                }
            }
        }
        Text {
            id: audioSelectionStatus
            objectName: "audio-file-selection-status"
            Layout.fillWidth: true
            visible: window.presenter.selectingAudioFiles || window.presenter.audioFileSelectionFailed
            text: window.presenter.selectingAudioFiles ? sourceImportText.selectingFiles : sourceImportText.fileSelectionFailed
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            color: window.uiContract.textSecondaryColor
            Accessible.role: Accessible.StaticText
            Accessible.name: text
            property string lastAnnouncement: ""
            function updateAnnouncement() {
                const message = visible ? text : "";
                if (message === lastAnnouncement)
                    return;
                lastAnnouncement = message;
                if (message.length > 0)
                    Accessible.announce(message, Accessible.Polite);
            }
            onTextChanged: Qt.callLater(updateAnnouncement)
            onVisibleChanged: Qt.callLater(updateAnnouncement)
        }
        LyricsPanel {
            id: lyricsPanel
            Layout.fillWidth: true
            visible: window.lyrics !== null && (window.presenter.hasCurrentTrack || queuedSaveFeedback || (typeof window.lyrics.variantsHasPendingChange !== "undefined" && window.lyrics.variantsHasPendingChange))
            contract: window.uiContract
            lyrics: window.lyrics
            trackIdentity: window.presenter.currentOccurrenceId
            readerCanSeek: window.presenter.seekable
            readerBottomInset: playerFooter.height
            readerTextSize: window.presenter.lyricsTextSize
            enabled: !window.presenter.exiting
            onReaderSeekRequested: timeMs => transport.commitImmediateSeek(timeMs)
            onReaderTextSizeRequested: size => window.presenter.setLyricsTextSize(size)
            onQueuedVariantFormClosed: Qt.callLater(function () {
                if (queuePanel.visible && window.active && window.windowShortcutsEnabled)
                    queuePanel.listView.forceActiveFocus(Qt.TabFocusReason);
            })
        }
    }
    Rectangle {
        id: playerFooter
        objectName: "player-footer"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: window.inlinePages ? Math.max(transport.implicitHeight, playerIdentity.implicitHeight) + 24 : transport.implicitHeight + footerTitle.implicitHeight + window.uiContract.spacingUnit * 8
        color: window.uiContract.surfaceColor
        Rectangle {
            width: parent.width
            height: 1
            color: window.inlinePages ? window.uiContract.dividerColor : window.uiContract.borderColor
            opacity: window.uiContract.highContrast || window.inlinePages ? 1 : 0.4
        }
        TransportBar {
            id: transport
            objectName: "transport-bar"
            pageReturnFocusItem: window.pageReturnFocusItem
            anchors.left: window.inlinePages ? playerIdentity.right : parent.left
            anchors.right: parent.right
            anchors.bottom: window.inlinePages ? undefined : parent.bottom
            anchors.verticalCenter: window.inlinePages ? parent.verticalCenter : undefined
            anchors.margins: window.uiContract.spacingUnit * 3
            anchors.leftMargin: window.inlinePages ? 24 : window.uiContract.spacingUnit * 3
            anchors.rightMargin: window.inlinePages ? (window.width <= window.uiContract.compactWidth ? 18 : 28) : window.uiContract.spacingUnit * 3
            contract: window.uiContract
            presenter: window.presenter
            enabled: !window.presenter.exiting && (playbackEnabled || window.presenter.canGoPrevious || window.presenter.canGoNext || queueAvailable || lyricsAvailable)
            queueCount: queuePreview.count
            queueAvailable: window.presenter.queueLoaded && queuePreview.count > 0 && !window.presenter.exiting
            lyricsAvailable: lyricsPanel.readerAvailable
            onQueueRequested: window.setQueueOpen(true)
            onLyricsRequested: lyricsPanel.showReader()
        }
        Item {
            id: playerIdentity
            objectName: "player-identity"
            anchors.left: parent.left
            anchors.leftMargin: window.inlinePages ? (window.width <= window.uiContract.compactWidth ? 18 : 28) : window.uiContract.spacingUnit * 3
            anchors.top: window.inlinePages ? undefined : parent.top
            anchors.topMargin: window.uiContract.spacingUnit * 3
            anchors.verticalCenter: window.inlinePages ? parent.verticalCenter : undefined
            width: window.inlinePages ? (window.width <= window.uiContract.compactWidth ? 160 : 220) : parent.width - window.uiContract.spacingUnit * 6
            implicitHeight: window.inlinePages ? (window.width <= window.uiContract.compactWidth ? 44 : 56) : footerTitle.implicitHeight
            TrackArtwork {
                id: footerArtwork
                objectName: "player-artwork"
                visible: window.inlinePages
                width: parent.height
                height: width
                contract: window.uiContract
                source: window.presenter.currentArtworkSource
            }
            Text {
                id: footerTitle
                objectName: "player-track-title"
                font: window.font
                anchors.left: window.inlinePages ? footerArtwork.right : parent.left
                anchors.leftMargin: window.inlinePages ? 12 : 0
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: window.inlinePages ? (window.width <= window.uiContract.compactWidth ? 4 : 10) : 0
                text: window.presenter.hasCurrentTrack ? window.presenter.currentTitle + (!window.inlinePages && window.presenter.currentArtist.length > 0 ? " · " + window.presenter.currentArtist : "") : window.uiContract.emptyTitle
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: window.uiContract.textPrimaryColor
                Accessible.name: text
            }
            Text {
                objectName: "player-track-artist"
                anchors.left: footerTitle.left
                anchors.right: parent.right
                anchors.top: footerTitle.bottom
                anchors.topMargin: 4
                visible: window.inlinePages
                text: window.presenter.currentArtist.length > 0 ? window.presenter.currentArtist : navigationText.localAudio
                font.family: window.uiContract.bodyFontFamily
                font.pixelSize: window.uiContract.typeCaption
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: window.uiContract.textSecondaryColor
                Accessible.name: text
            }
        }
    }

    Rectangle {
        // Backdrop of the Aurora queue drawer; clicking it closes the drawer.
        objectName: "queue-scrim"
        x: 0
        y: 0
        width: queuePanel.x
        height: playerFooter.y
        z: 9
        visible: window.inlinePages && window.queuePageActive
        color: window.uiContract.scrimColor
        Accessible.ignored: true
        MouseArea {
            anchors.fill: parent
            onClicked: window.setQueueOpen(false)
        }
    }
    QueuePanel {
        id: queuePanel
        // Aurora: side drawer from the top to the player. Legacy: fills the content area.
        x: window.inlinePages ? window.width - width : mainLayout.x + contentArea.x + contentArea.width - width
        y: window.inlinePages ? 0 : mainLayout.y + contentArea.y
        height: window.inlinePages ? playerFooter.y : contentArea.height
        width: window.inlinePages ? Math.min(window.uiContract.queuePanelMaximumWidth, contentArea.width) : contentArea.width
        z: 10
        visible: window.queuePageActive
        overlayMode: window.inlinePages
        Rectangle {
            visible: window.inlinePages
            width: window.uiContract.highContrast ? 2 : 1
            height: parent.height
            z: 100
            color: window.uiContract.highContrast ? window.uiContract.borderColor : window.uiContract.dividerColor
            Accessible.ignored: true
        }
        contract: window.uiContract
        presenter: window.presenter
        lyricVariantActionSupported: lyricsPanel.queueVariantActionSupported
        lyricVariantActionAvailable: lyricsPanel.queueVariantActionAvailable
        locationFocusRestoreAllowed: window.active && window.windowShortcutsEnabled && !preferencesPanel.visible
        onCloseRequested: window.setQueueOpen(false)
        onPlayRequested: occurrenceId => window.presenter.playQueueOccurrence(occurrenceId)
        onAddLyricVariantRequested: (occurrenceId, displayTitle) => lyricsPanel.openQueuedVariant(occurrenceId, displayTitle)
    }
    SourceImportStatus {
        id: sourceImportStatus
        parent: window.contentItem
        x: mainLayout.x
        y: Math.max(mainLayout.y, (audioSelectionStatus.visible ? mainLayout.y + audioSelectionStatus.y : playerFooter.y) - height - window.uiContract.spacingUnit * 2)
        width: mainLayout.width
        z: 30
        contract: window.uiContract
        presenter: window.presenter
        focusTakeoverAllowed: window.active && window.windowShortcutsEnabled && !preferencesPanel.visible
        onFocusRestoreRequested: window.restoreFocusAfterSourceImport()
        onAudioFilesOpening: opener => window.prepareAudioSelection(opener)
    }
    PreferencePersistenceNotice {
        id: preferencePersistenceNotice
        parent: window.contentItem
        x: mainLayout.x
        y: Math.max(mainLayout.y, sourceImportStatus.y - height - window.uiContract.spacingUnit * 2)
        width: mainLayout.width
        z: 40
        contract: window.uiContract
        warning: window.presenter.preferenceWarning
        warningScope: window.presenter.persistenceWarningScope
        warningRevision: window.presenter.persistenceWarningRevision
        fallbackFocusItem: preferencesButton
        interactionAllowed: window.active && !window.presenter.exiting && !window.presenter.selectingAudioFiles && !playlistsPanel.visible && !downloadsPanel.visible && !preferencesPanel.visible && !trackDetailsPanel.visible && !windowNotice.visible && !queuePanel.clearConfirmationOpen
        onReviewRequested: window.openPreferences()
        onDismissRequested: window.presenter.dismissPreferenceWarning()
        onRetryRequested: window.presenter.retryPreferenceSave()
        onReplacementConfirmed: revision => window.presenter.confirmPreferenceReplacement(revision)
    }
    PreferencesPanel {
        id: preferencesPanel
        parent: window.contentItem
        embedded: window.inlinePages
        previousFocusItem: preferencesButton
        nextFocusItem: transport.primaryControl.enabled ? transport.primaryControl : (sideNavigation.nowButton.enabled ? sideNavigation.nowButton : downloadsButton)
        width: embedded ? contentArea.width : Math.min(window.width - window.uiContract.spacingUnit * 12, 600)
        height: embedded ? contentArea.height : Math.min(contentArea.height, 400)
        x: embedded ? mainLayout.x + contentArea.x : (window.width - width) / 2
        y: mainLayout.y + contentArea.y
        contract: window.uiContract
        presenter: window.presenter
        downloads: window.downloads
        onClosed: {
            if (!window.presenter.exiting) {
                if (windowNotice.visible)
                    windowNotice.focusFirst();
                else
                    preferencesButton.forceActiveFocus(Qt.TabFocusReason);
            }
        }
    }
    TrackDetailsPanel {
        id: trackDetailsPanel
        parent: window.contentItem
        width: Math.min(window.width - window.uiContract.spacingUnit * 12, 760)
        height: Math.min(window.height - window.uiContract.spacingUnit * 12, 640)
        x: (window.width - width) / 2
        y: (window.height - height) / 2
        contract: window.uiContract
        presenter: window.presenter
        fallbackFocusItem: preferencesButton
        focusRestoreAllowed: window.active && !window.presenter.exiting && !downloadsPanel.visible && !preferencesPanel.visible && !preferencePersistenceNotice.replacementConfirmationOpen && !windowNotice.visible && !queuePanel.clearConfirmationOpen
    }
    WindowNotice {
        id: windowNotice
        parent: window.contentItem
        width: Math.min(window.width - window.uiContract.spacingUnit * 12, 560)
        x: (window.width - width) / 2
        y: (window.height - height) / 2
        contract: window.uiContract
        presenter: window.presenter
        fallbackFocusItem: preferencesButton
    }
    DownloadsPanel {
        id: downloadsPanel
        parent: window.contentItem
        embedded: window.inlinePages
        previousFocusItem: downloadsButton
        nextFocusItem: transport.primaryControl.enabled ? transport.primaryControl : (sideNavigation.nowButton.enabled ? sideNavigation.nowButton : downloadsButton)
        width: embedded ? contentArea.width : Math.min(window.width - window.uiContract.spacingUnit * 12, 760)
        height: embedded ? contentArea.height : Math.min(window.height - window.uiContract.spacingUnit * 12, 640)
        x: embedded ? mainLayout.x + contentArea.x : (window.width - width) / 2
        y: embedded ? mainLayout.y + contentArea.y : (window.height - height) / 2
        contract: window.uiContract
        downloads: window.downloads
        fallbackFocusItem: downloadsButton
        focusRestoreAllowed: window.active && (!window.inlinePages || window.currentPage === "home") && (!window.presenter.exiting || window.downloadFinalSaveDecisionRequired)
        onExitRequested: window.presenter.requestExit()
    }

    Connections {
        target: window.embeddedBrowser
        function onErrorChanged() {
            if (window.embeddedBrowser !== null && window.embeddedBrowser.errorText.length > 0)
                browserError.open();
        }
    }
    LibraryPanel {
        id: libraryPanel
        previousFocusItem: libraryButton
        nextFocusItem: transport.primaryControl.enabled ? transport.primaryControl : sideNavigation.nowButton
        parent: window.contentItem
        x: mainLayout.x + contentArea.x
        y: mainLayout.y + contentArea.y
        width: contentArea.width
        height: contentArea.height
        contract: window.uiContract
        library: window.library
        enabled: !window.presenter.exiting
        onClosed: {
            if (window.active && !window.presenter.exiting && !playlistsPanel.visible && !downloadsPanel.visible && !preferencesPanel.visible && libraryButton.visible)
                libraryButton.forceActiveFocus(Qt.TabFocusReason);
        }
    }
    PlaylistsPanel {
        id: playlistsPanel
        embedded: window.inlinePages
        previousFocusItem: playlistsButton
        nextFocusItem: transport.primaryControl.enabled ? transport.primaryControl : (sideNavigation.nowButton.enabled ? sideNavigation.nowButton : downloadsButton)
        parent: embedded ? window.contentItem : Overlay.overlay
        x: embedded ? mainLayout.x + contentArea.x : (parent.width - width) / 2
        y: embedded ? mainLayout.y + contentArea.y : (parent.height - height) / 2
        width: embedded ? contentArea.width : Math.min(920, parent ? parent.width - contract.spacingUnit * 8 : 920)
        height: embedded ? contentArea.height : Math.min(680, parent ? parent.height - contract.spacingUnit * 8 : 680)
        contract: window.uiContract
        queueHasTracks: !window.presenter.usesFrozenFixture && window.presenter.queueLoaded
        playlists: window.playlists
        enabled: !window.presenter.exiting
        onClosed: {
            if (window.active && !window.presenter.exiting && !downloadsPanel.visible)
                playlistsButton.forceActiveFocus(Qt.TabFocusReason);
        }
    }
    Dialog {
        id: browserError
        objectName: "embedded-browser-error"
        anchors.centerIn: parent
        width: Math.min(window.width - window.uiContract.spacingUnit * 8, 460)
        title: browserText.unavailable
        modal: true
        standardButtons: Dialog.Ok
        contentItem: Label {
            text: window.embeddedBrowser !== null ? window.embeddedBrowser.errorText : ""
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
        }
        onClosed: browserButton.forceActiveFocus()
    }
    DropArea {
        id: sourceDropArea
        parent: window.contentItem
        objectName: "source-drop-area"
        anchors.fill: parent
        z: 100
        enabled: !window.presenter.usesFrozenFixture && !window.presenter.exiting && !window.presenter.selectingAudioFiles && !window.presenter.sourceImportActive && !playlistsPanel.visible && !downloadsPanel.visible && !preferencesPanel.visible && !trackDetailsPanel.visible && !preferencePersistenceNotice.replacementConfirmationOpen && !windowNotice.visible && !queuePanel.clearConfirmationOpen
        property bool dragActive: false
        property bool overQueue: false
        property int acceptedDropAction: Qt.IgnoreAction
        property int insertionIndex: window.presenter.queueModel.rowCount()

        function updateInsertion(x, y) {
            const queuePoint = queuePanel.mapFromItem(sourceDropArea, x, y);
            const queueIndex = queuePanel.dropInsertionIndexAt(queuePoint.x, queuePoint.y);
            overQueue = queueIndex >= 0;
            insertionIndex = overQueue ? queueIndex : window.presenter.queueModel.rowCount();
        }

        function rejectDrag(drag) {
            drag.accepted = false;
            dragActive = false;
            overQueue = false;
            acceptedDropAction = Qt.IgnoreAction;
        }

        onEntered: drag => {
            if (!drag.hasUrls) {
                rejectDrag(drag);
                return;
            }
            updateInsertion(drag.x, drag.y);
            drag.accept(Qt.CopyAction);
            acceptedDropAction = Qt.CopyAction;
            dragActive = true;
        }
        onPositionChanged: drag => {
            if (!drag.hasUrls) {
                rejectDrag(drag);
                return;
            }
            updateInsertion(drag.x, drag.y);
            drag.accept(Qt.CopyAction);
            acceptedDropAction = Qt.CopyAction;
            dragActive = true;
        }
        onExited: {
            dragActive = false;
            overQueue = false;
            acceptedDropAction = Qt.IgnoreAction;
        }
        onDropped: drop => {
            if (!drop.hasUrls) {
                rejectDrag(drop);
                return;
            }
            updateInsertion(drop.x, drop.y);
            const imported = window.presenter.importDroppedUrls(drop.urls, insertionIndex);
            if (imported)
                drop.accept(Qt.CopyAction);
            else
                drop.accepted = false;
            dragActive = false;
            overQueue = false;
            acceptedDropAction = Qt.IgnoreAction;
        }

        Rectangle {
            id: sourceDropOverlay
            objectName: "source-drop-overlay"
            anchors.fill: parent
            visible: sourceDropArea.dragActive
            color: window.uiContract.highContrast ? "transparent" : Qt.alpha(window.uiContract.accentColor, 0.12)
            border.width: window.uiContract.highContrast ? 4 : 2
            border.color: window.uiContract.focusColor
            radius: window.uiContract.radiusMedium
            z: 1
            Accessible.ignored: true

            Rectangle {
                objectName: "source-drop-prompt"
                anchors.centerIn: parent
                width: Math.min(parent.width - window.uiContract.spacingUnit * 12, prompt.implicitWidth + window.uiContract.spacingUnit * 8)
                height: prompt.implicitHeight + window.uiContract.spacingUnit * 6
                color: window.uiContract.surfaceRaisedColor
                radius: window.uiContract.radiusMedium
                border.width: window.uiContract.highContrast ? 2 : 1
                border.color: window.uiContract.focusColor

                Text {
                    id: prompt
                    anchors.centerIn: parent
                    width: Math.max(0, parent.width - window.uiContract.spacingUnit * 4)
                    text: sourceImportText.dropPrompt
                    textFormat: Text.PlainText
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: window.uiContract.textPrimaryColor
                    font.bold: true
                    Accessible.ignored: true
                }
            }
        }

        Rectangle {
            id: queueDropIndicator
            objectName: "queue-drop-indicator"
            property int insertionIndex: sourceDropArea.insertionIndex
            readonly property point listOrigin: visible ? sourceDropArea.mapFromItem(queuePanel.listView, 0, 0) : Qt.point(0, 0)
            x: listOrigin.x
            y: {
                if (!visible)
                    return 0;
                const top = listOrigin.y;
                const bottom = top + queuePanel.listView.height - height;
                const proposed = top + queuePanel.dropIndicatorViewportY(insertionIndex) - height / 2;
                return Math.max(top, Math.min(bottom, proposed));
            }
            width: queuePanel.listView.width
            height: window.uiContract.highContrast ? 4 : 3
            radius: height / 2
            color: window.uiContract.focusColor
            visible: sourceDropArea.dragActive && sourceDropArea.overQueue && insertionIndex >= 0
            z: 2
            Accessible.ignored: true
        }
    }

    Shortcut {
        sequence: "Ctrl+O"
        context: Qt.WindowShortcut
        autoRepeat: false
        enabled: window.windowShortcutsEnabled && !preferencesPanel.visible && (!window.presenter.usesFrozenFixture || window.presenter.playbackState === PlayerPresenter.Unloaded)
        onActivated: window.openAudio()
    }
    Shortcut {
        sequence: "Space"
        context: Qt.WindowShortcut
        autoRepeat: false
        enabled: window.spaceShortcutEnabled
        // Reserve Space even in Empty/Loading/Error: never click the last button.
        onActivated: {
            if (window.transportShortcutsEnabled)
                window.presenter.togglePlayback();
        }
    }
    Shortcut {
        sequence: "M"
        context: Qt.WindowShortcut
        autoRepeat: false
        enabled: window.primaryKeysEnabled
        onActivated: {
            if (window.transportShortcutsEnabled)
                window.presenter.toggleMute();
        }
    }
    Shortcut {
        sequence: "Left"
        context: Qt.WindowShortcut
        enabled: window.arrowShortcutsEnabled
        onActivated: {
            if (window.seekShortcutsEnabled)
                window.presenter.skipBackward();
        }
    }
    Shortcut {
        sequence: "Right"
        context: Qt.WindowShortcut
        enabled: window.arrowShortcutsEnabled
        onActivated: {
            if (window.seekShortcutsEnabled)
                window.presenter.skipForward();
        }
    }
    Shortcut {
        sequence: "Up"
        context: Qt.WindowShortcut
        enabled: window.arrowShortcutsEnabled
        onActivated: {
            if (window.transportShortcutsEnabled)
                window.changeVolume(1);
        }
    }
    Shortcut {
        sequence: "Down"
        context: Qt.WindowShortcut
        enabled: window.arrowShortcutsEnabled
        onActivated: {
            if (window.transportShortcutsEnabled)
                window.changeVolume(-1);
        }
    }
    Shortcut {
        sequence: "Ctrl+Q"
        // Bypass the preferences popup's shortcut context, but only in this active window.
        context: Qt.ApplicationShortcut
        autoRepeat: false
        enabled: window.active && window.windowShortcutsEnabled
        onActivated: window.presenter.requestExit()
    }
    Shortcut {
        sequence: "Ctrl+L"
        context: Qt.WindowShortcut
        autoRepeat: false
        enabled: window.windowShortcutsEnabled && queueButton.enabled && !preferencesPanel.visible
        onActivated: window.toggleQueue()
    }
    Shortcut {
        sequences: ["Shift+Left", "Ctrl+Left"]
        context: Qt.WindowShortcut
        enabled: window.primaryKeysEnabled
        onActivated: {
            if (window.navigationShortcutsEnabled && window.presenter.canGoPrevious)
                window.presenter.previousTrack();
        }
    }
    Shortcut {
        sequences: ["Shift+Right", "Ctrl+Right"]
        context: Qt.WindowShortcut
        enabled: window.primaryKeysEnabled
        onActivated: {
            if (window.navigationShortcutsEnabled && window.presenter.canGoNext)
                window.presenter.nextTrack();
        }
    }
    Shortcut {
        sequence: "F1"
        context: Qt.WindowShortcut
        autoRepeat: false
        enabled: window.primaryKeysEnabled
        onActivated: queuePanel.toggleShuffle()
    }
    Shortcut {
        sequence: "F2"
        context: Qt.WindowShortcut
        autoRepeat: false
        enabled: window.primaryKeysEnabled
        onActivated: queuePanel.toggleRepeat()
    }
    Shortcut {
        sequence: "Esc"
        context: Qt.WindowShortcut
        autoRepeat: false
        enabled: window.inlinePageClosingAllowed || (window.windowShortcutsEnabled && window.queueOpen && !preferencesPanel.visible)
        onActivated: {
            if (window.inlinePageClosingAllowed) {
                preferencesPanel.close();
                libraryPanel.close();
                playlistsPanel.close();
                downloadsPanel.close();
            } else {
                window.setQueueOpen(false);
            }
        }
    }
    Shortcut {
        sequence: "Ctrl+,"
        // Refocusing an open modal needs the same active-window exception as Ctrl+Q.
        context: Qt.ApplicationShortcut
        autoRepeat: false
        enabled: window.active && window.windowShortcutsEnabled
        onActivated: window.openPreferences()
    }
}
