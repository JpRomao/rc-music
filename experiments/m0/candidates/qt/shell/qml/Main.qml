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
                anchors.right: parent.right
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

    Shortcut {
        sequence: "Ctrl+L"
        enabled: queueButton.enabled
        onActivated: window.toggleQueue()
    }
    Shortcut {
        sequence: "Esc"
        enabled: window.queueOpen
        onActivated: window.setQueueOpen(false)
    }
}
