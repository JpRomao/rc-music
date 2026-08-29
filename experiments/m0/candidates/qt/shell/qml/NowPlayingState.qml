import QtQuick
import QtQuick.Layouts
import MusicPlayer.M0

ColumnLayout {
    id: root
    objectName: "playing-state"
    required property ShellUiContract contract
    required property PlayerPresenter presenter
    signal openAudioRequested
    Item {
        implicitWidth: 192
        implicitHeight: 192
        Layout.alignment: Qt.AlignHCenter

        Image {
            objectName: "cover-track"
            anchors.fill: parent
            source: root.contract.trackSource
            sourceSize.width: 192
            sourceSize.height: 192
            Accessible.ignored: true
        }
        Image {
            objectName: "embedded-artwork"
            anchors.fill: parent
            source: root.presenter.currentArtworkSource
            asynchronous: true
            cache: false
            retainWhileLoading: false
            fillMode: Image.PreserveAspectFit
            visible: status === Image.Ready
            Accessible.ignored: true
        }
    }
    Text {
        objectName: "current-title"
        text: root.presenter.currentTitle
        textFormat: Text.PlainText
        elide: Text.ElideRight
        Accessible.name: text
        color: root.contract.textPrimaryColor
        Layout.maximumWidth: root.width
        Layout.alignment: Qt.AlignHCenter
    }
    Text {
        objectName: "current-artist"
        text: root.presenter.currentArtist
        textFormat: Text.PlainText
        elide: Text.ElideRight
        Accessible.name: text
        color: root.contract.textSecondaryColor
        Layout.maximumWidth: root.width
        Layout.alignment: Qt.AlignHCenter
    }
    Text {
        objectName: "current-album"
        text: root.presenter.currentAlbum
        textFormat: Text.PlainText
        elide: Text.ElideRight
        Accessible.name: text
        color: root.contract.textSecondaryColor
        Layout.maximumWidth: root.width
        Layout.alignment: Qt.AlignHCenter
    }
    AccessibleActionButton {
        controlId: "open-another-audio"
        contract: root.contract
        text: root.contract.openAudioText
        Layout.alignment: Qt.AlignHCenter
        onClicked: root.openAudioRequested()
    }
}
