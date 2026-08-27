import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

ApplicationWindow {
    id: window
    objectName: "mainWindow"
    required property ShellUiContract uiContract
    width: uiContract.clientWidth
    height: uiContract.clientHeight
    minimumWidth: uiContract.minimumWidth
    minimumHeight: uiContract.minimumHeight
    title: uiContract.appName
    color: uiContract.canvasColor
    visible: true
    signal openAudioRequested

    EmptyState {
        anchors.centerIn: parent
        contract: window.uiContract
        onOpenAudioRequested: window.openAudioRequested()
    }
}
