import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
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

    function setQueueOpen(open) {
        if (open && !presenter.queueLoaded)
            return;
        queueOpen = open;
        if (open)
            Qt.callLater(queuePanel.focusCurrent);
        else
            Qt.callLater(function () {
                queueButton.forceActiveFocus(Qt.TabFocusReason);
            });
    }
    function toggleQueue() {
        setQueueOpen(!queueOpen);
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
                    onOpenAudioRequested: {
                        window.openAudioRequested();
                        window.presenter.loadFrozenQueue();
                    }
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
        onClosed: preferencesButton.forceActiveFocus(Qt.TabFocusReason)
    }

    Shortcut {
        sequence: "Ctrl+L"
        enabled: queueButton.enabled && !preferencesPanel.visible
        onActivated: window.toggleQueue()
    }
    Shortcut {
        sequence: "Ctrl+Left"
        enabled: window.presenter.canGoPrevious && !preferencesPanel.visible
        onActivated: window.presenter.previousTrack()
    }
    Shortcut {
        sequence: "Ctrl+Right"
        enabled: window.presenter.canGoNext && !preferencesPanel.visible
        onActivated: window.presenter.nextTrack()
    }
    Shortcut {
        sequence: "Esc"
        enabled: window.queueOpen && !preferencesPanel.visible
        onActivated: window.setQueueOpen(false)
    }
    Shortcut {
        sequence: "Ctrl+,"
        onActivated: window.openPreferences()
    }
}
