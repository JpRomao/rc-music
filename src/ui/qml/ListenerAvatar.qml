import QtQuick

// A listener avatar: a bundled SVG or the reduced custom image.
// Decorative by default: the owner names the person next to it.
Item {
    id: root
    property url source
    property int size: 40
    implicitWidth: size
    implicitHeight: size
    Accessible.ignored: true

    Image {
        anchors.fill: parent
        source: root.source
        sourceSize: Qt.size(root.size * 2, root.size * 2)
        fillMode: Image.PreserveAspectCrop
        smooth: true
        mipmap: true
        Accessible.ignored: true
    }
}
