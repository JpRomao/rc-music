import QtQuick
import MusicPlayer.M0

Rectangle {
    id: root
    required property ShellUiContract contract
    property url source
    property bool frozen: false
    property url fallbackSource: contract.trackSource
    property string fallbackObjectName: ""
    property string imageObjectName: ""
    implicitWidth: 192
    implicitHeight: 192
    radius: contract.radiusMedium
    color: frozen ? "transparent" : contract.surfaceRaisedColor
    Accessible.ignored: true
    clip: true

    Image {
        objectName: root.fallbackObjectName
        anchors.fill: parent
        source: root.frozen ? root.fallbackSource : ""
        sourceSize.width: 192
        sourceSize.height: 192
        visible: root.frozen
        Accessible.ignored: true
    }
    Rectangle {
        visible: !root.frozen
        anchors.centerIn: parent
        width: parent.width * 0.64
        height: width
        radius: width / 2
        color: root.contract.textPrimaryColor
        opacity: root.contract.highContrast ? 1 : 0.82
        Accessible.ignored: true
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.64
            height: width
            radius: width / 2
            color: root.contract.accentColor
            Accessible.ignored: true
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 0.22
                height: width
                radius: width / 2
                color: root.contract.surfaceRaisedColor
                Accessible.ignored: true
            }
        }
    }
    Image {
        objectName: root.imageObjectName
        anchors.fill: parent
        source: root.source
        asynchronous: true
        cache: false
        retainWhileLoading: false
        fillMode: Image.PreserveAspectFit
        visible: status === Image.Ready
        Accessible.ignored: true
    }
}
