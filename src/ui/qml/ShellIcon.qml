import QtQuick
import QtQuick.Controls.impl

// Monochrome line icon from :/rc_music/icons, tinted with the theme color.
// Icons are decorative: the owning control keeps the accessible name.
IconImage {
    id: root
    property string iconName: ""
    property int size: 20
    objectName: "button-icon"
    visible: iconName.length > 0
    source: iconName.length > 0 ? "qrc:/rc_music/icons/" + iconName + ".svg" : ""
    width: size
    height: size
    sourceSize: Qt.size(size, size)
    fillMode: Image.PreserveAspectFit
    smooth: true
    Accessible.ignored: true
}
