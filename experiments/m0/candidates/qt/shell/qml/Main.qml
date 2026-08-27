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

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: window.uiContract.spacingUnit * 6
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
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
        TransportBar {
            objectName: "transport-bar"
            Layout.fillWidth: true
            contract: window.uiContract
            presenter: window.presenter
        }
    }
}
