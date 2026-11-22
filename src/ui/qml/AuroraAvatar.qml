import QtQuick
import MusicPlayer.M0

// Round avatar without graphical effects: a cover in the background color
// hides the square corners of the image.
Item {
    id: root

    required property ShellUiContract contract
    property url source
    property int size: 40
    property color backgroundColor: contract.surfaceColor
    property bool selected: false

    readonly property int coverWidth: Math.ceil(size / 4)

    implicitWidth: size
    implicitHeight: size
    Accessible.ignored: true

    Item {
        anchors.fill: parent
        clip: true

        Image {
            anchors.fill: parent
            source: root.source
            sourceSize: Qt.size(root.size * 2, root.size * 2)
            fillMode: Image.PreserveAspectCrop
            smooth: true
            mipmap: true
            Accessible.ignored: true
        }
        Rectangle {
            objectName: "avatar-corner-cover"
            x: -root.coverWidth
            y: -root.coverWidth
            width: root.size + 2 * root.coverWidth
            height: width
            radius: root.size / 2 + root.coverWidth
            color: "transparent"
            border.width: root.coverWidth
            border.color: root.backgroundColor
        }
    }

    Rectangle {
        objectName: "avatar-selection-ring"
        anchors.fill: parent
        anchors.margins: -4
        radius: width / 2
        visible: root.selected
        color: "transparent"
        border.width: 2
        border.color: root.contract.accentColor
    }
}
