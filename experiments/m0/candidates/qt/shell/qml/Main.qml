import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import MusicPlayer.M0

ApplicationWindow {
    id: window
    objectName: "mainWindow"
    required property ShellUiContract uiContract
    required property PlayerPresenter presenter
    width: uiContract.clientWidth
    height: uiContract.clientHeight
    minimumWidth: uiContract.minimumWidth
    minimumHeight: uiContract.minimumHeight
    title: uiContract.appName
    color: uiContract.canvasColor
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
    contentItem.enabled: !presenter.exiting
    visible: true
    signal openAudioRequested
    property bool queueOpen: false
    ErrorText {
        id: errorText
    }
    PlaybackAccessibilityStatus {
        contract: window.uiContract
        presenter: window.presenter
        errorLabels: errorText
    }
    readonly property bool windowShortcutsEnabled: !windowNotice.visible && !queuePanel.clearConfirmationOpen && !trackDetailsPanel.visible && !preferencePersistenceNotice.replacementConfirmationOpen && !presenter.exiting
    readonly property bool transportShortcutsEnabled: windowShortcutsEnabled && !preferencesPanel.visible && transport.enabled
    readonly property bool spaceShortcutEnabled: transportShortcutsEnabled && !focusOwnsSpaceKey()
    readonly property bool arrowShortcutsEnabled: transportShortcutsEnabled && !focusOwnsArrowKeys()
    readonly property bool seekShortcutsEnabled: arrowShortcutsEnabled && presenter.seekable && !presenter.seekPreviewActive && !presenter.seekPending
    property int previousPlaybackState: -1
    property int playbackFocusRepairGeneration: 0
    property Item lastActiveFocusItem: null

    Component.onCompleted: previousPlaybackState = presenter.playbackState
    onActiveFocusItemChanged: {
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
        const wholeTransportBecameUnavailable = transportEnabledForState(oldState) && !transportEnabledForState(nextState);
        const focusedSeekControlBecameUnavailable = transport.isSeekControl(focusBeforeTransition) && !presenter.seekable;
        const focusCameFromDisabledTransport = isInside(focusBeforeTransition, transport) && (wholeTransportBecameUnavailable || focusedSeekControlBecameUnavailable);
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
            if (!window.active || windowNotice.visible || preferencesPanel.visible || trackDetailsPanel.visible || preferencePersistenceNotice.replacementConfirmationOpen || queuePanel.clearConfirmationOpen || window.presenter.exiting)
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
    function focusOwnsSpaceKey() {
        if (focusOwnsTextInput())
            return true;
        let item = activeFocusItem;
        // Styled controls share the template base, not Controls.Control's QML type.
        // Stop before ApplicationWindow's internal Control above its content item.
        while (item && item !== contentItem) {
            if (item instanceof T.Control || item instanceof Flickable)
                return true;
            item = item.parent;
        }
        return false;
    }
    function focusOwnsArrowKeys() {
        if (focusOwnsTextInput())
            return true;
        let item = activeFocusItem;
        while (item && item !== contentItem) {
            if (item instanceof T.Slider || item instanceof T.ComboBox || item instanceof Flickable)
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
        queueOpen = open;
        Qt.callLater(function () {
            if (windowNotice.visible || preferencesPanel.visible || trackDetailsPanel.visible || preferencePersistenceNotice.replacementConfirmationOpen || window.presenter.exiting)
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
    function openAudio() {
        openAudioRequested();
        presenter.openAudio();
    }
    function changeVolume(percentagePoints) {
        const percent = presenter.volume * 100;
        presenter.setVolume(Math.max(0, Math.min(100, percent + percentagePoints)) / 100);
    }
    function openPreferences() {
        queueOpen = false;
        if (preferencesPanel.opened)
            preferencesPanel.focusFirst();
        else
            preferencesPanel.open();
    }
    function openTrackDetails(opener) {
        queueOpen = false;
        trackDetailsPanel.openFrom(opener);
    }
    function restoreFocusAfterSourceImport() {
        if (window.presenter.exiting || preferencesPanel.visible || trackDetailsPanel.visible || preferencePersistenceNotice.replacementConfirmationOpen || windowNotice.visible || queuePanel.clearConfirmationOpen)
            return;
        if (window.queueOpen && queuePanel.visible)
            queuePanel.focusCurrent();
        else if (queueButton.enabled)
            queueButton.forceActiveFocus(Qt.TabFocusReason);
        else
            emptyState.focusPrimary();
    }

    ColumnLayout {
        id: mainLayout
        anchors.fill: parent
        anchors.margins: window.uiContract.spacingUnit * 6
        Item {
            id: contentArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            AccessibleTransportButton {
                id: queueButton
                objectName: "queue"
                anchors.top: parent.top
                anchors.right: preferencesButton.left
                anchors.rightMargin: window.uiContract.spacingUnit * 2
                z: 20
                contract: window.uiContract
                text: window.uiContract.queueTitle
                accessibleName: window.uiContract.queueTitle
                enabled: window.presenter.queueLoaded
                checkable: true
                checked: window.queueOpen
                Accessible.checkable: true
                Accessible.checked: checked
                onClicked: window.toggleQueue()
            }
            AccessibleTransportButton {
                id: preferencesButton
                objectName: "preferences"
                anchors.top: parent.top
                anchors.right: parent.right
                z: 20
                contract: window.uiContract
                text: window.uiContract.preferencesTitle
                accessibleName: window.uiContract.preferencesTitle
                onClicked: window.openPreferences()
            }
            Item {
                id: stateLayer
                objectName: "state-layer"
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.rightMargin: queuePanel.visible && !queuePanel.overlayMode ? queuePanel.width + window.uiContract.spacingUnit * 4 : 0
                EmptyState {
                    id: emptyState
                    anchors.centerIn: parent
                    width: Math.min(parent.width > 0 ? parent.width : 560, 560)
                    visible: window.presenter.playbackState === PlayerPresenter.Unloaded
                    contract: window.uiContract
                    onOpenAudioRequested: window.openAudio()
                }
                NowPlayingState {
                    id: nowPlayingState
                    anchors.centerIn: parent
                    width: Math.min(parent.width, 560)
                    visible: window.presenter.playbackState !== PlayerPresenter.Unloaded && window.presenter.playbackState !== PlayerPresenter.Error
                    contract: window.uiContract
                    presenter: window.presenter
                    onOpenAudioRequested: window.openAudio()
                    onDetailsRequested: opener => window.openTrackDetails(opener)
                }
                ErrorState {
                    id: errorState
                    anchors.centerIn: parent
                    visible: window.presenter.playbackState === PlayerPresenter.Error
                    contract: window.uiContract
                    presenter: window.presenter
                    labels: errorText
                    onDetailsRequested: opener => window.openTrackDetails(opener)
                }
            }
        }
        TransportBar {
            id: transport
            objectName: "transport-bar"
            Layout.fillWidth: true
            contract: window.uiContract
            presenter: window.presenter
        }
    }

    QueuePanel {
        id: queuePanel
        x: mainLayout.x + contentArea.x + contentArea.width - width
        y: mainLayout.y + contentArea.y + queueButton.height + window.uiContract.spacingUnit * 2
        height: contentArea.height - queueButton.height - window.uiContract.spacingUnit * 2
        width: overlayMode ? window.uiContract.queuePanelMinimumWidth : window.uiContract.queuePanelMaximumWidth
        z: 10
        visible: window.queueOpen && window.presenter.queueLoaded
        overlayMode: window.width < 900
        contract: window.uiContract
        presenter: window.presenter
        onCloseRequested: window.setQueueOpen(false)
        onPlayRequested: occurrenceId => window.presenter.playQueueOccurrence(occurrenceId)
    }
    SourceImportStatus {
        id: sourceImportStatus
        parent: window.contentItem
        x: mainLayout.x
        y: Math.max(mainLayout.y, mainLayout.y + transport.y - height - window.uiContract.spacingUnit * 2)
        width: mainLayout.width
        z: 30
        contract: window.uiContract
        presenter: window.presenter
        focusTakeoverAllowed: window.active && !window.presenter.exiting && !preferencesPanel.visible && !trackDetailsPanel.visible && !preferencePersistenceNotice.replacementConfirmationOpen && !windowNotice.visible && !queuePanel.clearConfirmationOpen
        onFocusRestoreRequested: window.restoreFocusAfterSourceImport()
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
        fallbackFocusItem: preferencesButton
        interactionAllowed: window.active && !window.presenter.exiting && !preferencesPanel.visible && !trackDetailsPanel.visible && !windowNotice.visible && !queuePanel.clearConfirmationOpen
        onReviewRequested: window.openPreferences()
        onDismissRequested: window.presenter.dismissPreferenceWarning()
        onRetryRequested: window.presenter.retryPreferenceSave()
        onReplacementConfirmed: window.presenter.confirmPreferenceReplacement()
    }
    PreferencesPanel {
        id: preferencesPanel
        parent: window.contentItem
        width: Math.min(window.width - window.uiContract.spacingUnit * 12, 600)
        height: Math.min(contentArea.height, 400)
        x: (window.width - width) / 2
        y: mainLayout.y + contentArea.y
        contract: window.uiContract
        presenter: window.presenter
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
        focusRestoreAllowed: window.active && !window.presenter.exiting && !preferencesPanel.visible && !preferencePersistenceNotice.replacementConfirmationOpen && !windowNotice.visible && !queuePanel.clearConfirmationOpen
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

    DropArea {
        id: sourceDropArea
        parent: window.contentItem
        objectName: "source-drop-area"
        anchors.fill: parent
        z: 100
        enabled: !window.presenter.usesFrozenFixture && !window.presenter.exiting && !window.presenter.sourceImportActive && !preferencesPanel.visible && !trackDetailsPanel.visible && !preferencePersistenceNotice.replacementConfirmationOpen && !windowNotice.visible && !queuePanel.clearConfirmationOpen
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

                SourceImportText {
                    id: sourceImportText
                }

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
        onActivated: window.presenter.togglePlayback()
    }
    Shortcut {
        sequence: "M"
        context: Qt.WindowShortcut
        autoRepeat: false
        enabled: window.transportShortcutsEnabled && !window.focusOwnsTextInput()
        onActivated: window.presenter.toggleMute()
    }
    Shortcut {
        sequence: "Left"
        context: Qt.WindowShortcut
        enabled: window.seekShortcutsEnabled
        onActivated: window.presenter.skipBackward()
    }
    Shortcut {
        sequence: "Right"
        context: Qt.WindowShortcut
        enabled: window.seekShortcutsEnabled
        onActivated: window.presenter.skipForward()
    }
    Shortcut {
        sequence: "Up"
        context: Qt.WindowShortcut
        enabled: window.arrowShortcutsEnabled
        onActivated: window.changeVolume(1)
    }
    Shortcut {
        sequence: "Down"
        context: Qt.WindowShortcut
        enabled: window.arrowShortcutsEnabled
        onActivated: window.changeVolume(-1)
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
        sequence: "Ctrl+Left"
        context: Qt.WindowShortcut
        enabled: window.transportShortcutsEnabled && window.presenter.canGoPrevious && !window.focusOwnsTextInput()
        onActivated: window.presenter.previousTrack()
    }
    Shortcut {
        sequence: "Ctrl+Right"
        context: Qt.WindowShortcut
        enabled: window.transportShortcutsEnabled && window.presenter.canGoNext && !window.focusOwnsTextInput()
        onActivated: window.presenter.nextTrack()
    }
    Shortcut {
        sequence: "Esc"
        context: Qt.WindowShortcut
        autoRepeat: false
        enabled: window.windowShortcutsEnabled && window.queueOpen && !preferencesPanel.visible
        onActivated: window.setQueueOpen(false)
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
