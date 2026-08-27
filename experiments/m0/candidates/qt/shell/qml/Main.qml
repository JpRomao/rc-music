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
    visible: true
    signal openAudioRequested
    property bool queueOpen: false
    readonly property bool windowShortcutsEnabled: !windowNotice.visible && !presenter.exiting
    readonly property bool transportShortcutsEnabled: windowShortcutsEnabled && !preferencesPanel.visible && transport.enabled
    readonly property bool spaceShortcutEnabled: transportShortcutsEnabled && !focusOwnsSpaceKey()
    readonly property bool arrowShortcutsEnabled: transportShortcutsEnabled && !focusOwnsArrowKeys()
    readonly property bool seekShortcutsEnabled: arrowShortcutsEnabled && presenter.seekable && !presenter.seekPreviewActive && !presenter.seekPending

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
        close.accepted = presenter.exiting;
        if (!close.accepted)
            presenter.requestClose();
    }

    function setQueueOpen(open) {
        if (open && !presenter.queueLoaded)
            return;
        queueOpen = open;
        Qt.callLater(function () {
            if (windowNotice.visible || preferencesPanel.visible || window.presenter.exiting)
                return;
            if (window.queueOpen)
                queuePanel.focusCurrent();
            else
                queueButton.forceActiveFocus(Qt.TabFocusReason);
        });
    }
    function toggleQueue() {
        setQueueOpen(!queueOpen);
    }
    function openFrozenAudio() {
        openAudioRequested();
        presenter.loadFrozenQueue();
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
                    anchors.centerIn: parent
                    visible: window.presenter.playbackState === PlayerPresenter.Unloaded
                    contract: window.uiContract
                    onOpenAudioRequested: window.openFrozenAudio()
                }
                NowPlayingState {
                    anchors.centerIn: parent
                    width: Math.min(parent.width, 560)
                    visible: window.presenter.playbackState !== PlayerPresenter.Unloaded && window.presenter.playbackState !== PlayerPresenter.Error
                    contract: window.uiContract
                    presenter: window.presenter
                }
                ErrorState {
                    anchors.centerIn: parent
                    visible: window.presenter.playbackState === PlayerPresenter.Error
                    contract: window.uiContract
                    presenter: window.presenter
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

    Shortcut {
        sequence: "Ctrl+O"
        context: Qt.WindowShortcut
        autoRepeat: false
        enabled: window.windowShortcutsEnabled && !preferencesPanel.visible && window.presenter.playbackState === PlayerPresenter.Unloaded
        onActivated: window.openFrozenAudio()
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
