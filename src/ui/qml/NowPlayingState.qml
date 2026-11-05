import QtQuick
import QtQuick.Layouts
import MusicPlayer.M0

ColumnLayout {
    id: root
    objectName: "playing-state"
    required property ShellUiContract contract
    required property PlayerPresenter presenter
    property bool compact: false
    readonly property real coverExtent: compact && parent ? Math.max(0, Math.min(128, parent.height - titleLabel.implicitHeight - artistLabel.implicitHeight - albumLabel.implicitHeight - openAudioButton.implicitHeight - detailsButton.implicitHeight - spacing * 5 - 4)) : 192
    signal openAudioRequested(Item opener)
    signal detailsRequested(Item opener)

    function focusPrimary() {
        openAudioButton.forceActiveFocus(Qt.TabFocusReason);
    }

    Item {
        implicitWidth: root.coverExtent
        implicitHeight: root.coverExtent
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
        id: titleLabel
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
        id: artistLabel
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
        id: albumLabel
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
        id: openAudioButton
        controlId: "open-another-audio"
        contract: root.contract
        text: root.contract.openAudioText
        enabled: !root.presenter.exiting && !root.presenter.selectingAudioFiles && root.presenter.locatingOccurrenceId.length === 0
        Layout.alignment: Qt.AlignHCenter
        onClicked: root.openAudioRequested(openAudioButton)
    }
    AccessibleTransportButton {
        id: detailsButton
        objectName: "track-details-playing"
        contract: root.contract
        text: root.contract.detailsTitle
        accessibleName: root.contract.detailsTitle
        enabled: root.presenter.detailsAvailable
        Layout.alignment: Qt.AlignHCenter
        onClicked: root.detailsRequested(detailsButton)
    }
}
