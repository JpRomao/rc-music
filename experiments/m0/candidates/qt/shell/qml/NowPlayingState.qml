import QtQuick
import QtQuick.Layouts
import MusicPlayer.M0

ColumnLayout {
    id: root
    objectName: "playing-state"
    required property ShellUiContract contract
    required property PlayerPresenter presenter
    Image {
        objectName: "cover-track"
        source: root.contract.trackSource
        sourceSize.width: 192
        sourceSize.height: 192
        Layout.alignment: Qt.AlignHCenter
        Accessible.ignored: true
    }
    Text {
        objectName: "current-title"
        text: root.presenter.currentTitle
        textFormat: Text.PlainText
        elide: Text.ElideRight
        Accessible.name: text
        color: root.contract.textPrimaryColor
        Layout.maximumWidth: 560
        Layout.alignment: Qt.AlignHCenter
    }
    Text {
        objectName: "current-artist"
        text: root.presenter.currentArtist
        textFormat: Text.PlainText
        elide: Text.ElideRight
        Accessible.name: text
        color: root.contract.textSecondaryColor
        Layout.maximumWidth: 560
        Layout.alignment: Qt.AlignHCenter
    }
    Text {
        objectName: "current-album"
        text: root.presenter.currentAlbum
        textFormat: Text.PlainText
        elide: Text.ElideRight
        Accessible.name: text
        color: root.contract.textSecondaryColor
        Layout.maximumWidth: 560
        Layout.alignment: Qt.AlignHCenter
    }
}
