import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

Slider {
    id: control
    required property ShellUiContract contract
    // Player C: the slider runs along a container's top edge, a thin track flush with the top of a
    // full-width click area, with a small marker centered on the track.
    property bool edge: false
    property int handleSize: 28
    readonly property int edgeTrackHeight: 4
    readonly property int edgeRingWidth: 3
    readonly property bool focusIndicatorVisible: activeFocus && enabled
    readonly property color outlineColor: contract.highContrast && !enabled ? contract.disabledTextColor : contract.textSecondaryColor
    readonly property color fillColor: contract.highContrast && !enabled ? contract.disabledTextColor : contract.accentColor
    implicitHeight: edge ? 24 : contract.controlTargetMinimum
    padding: edge ? 0 : 6
    palette.window: contract.surfaceColor
    palette.dark: fillColor

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.edge ? (control.edgeTrackHeight - height) / 2 : control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: control.handleSize
        implicitHeight: control.handleSize
        radius: width / 2
        color: control.edge ? control.fillColor : (control.pressed && !control.contract.highContrast ? control.contract.surfaceRaisedColor : control.contract.surfaceColor)
        border.width: control.edge ? 0 : (control.visualFocus ? 2 : 1)
        border.color: control.visualFocus && !control.contract.highContrast ? control.fillColor : control.outlineColor
        // The surface-colored ring separates the edge marker from the track and the content above it.
        Rectangle {
            objectName: "slider-marker-ring"
            visible: control.edge
            anchors.centerIn: parent
            width: parent.width + control.edgeRingWidth * 2
            height: width
            radius: width / 2
            color: control.contract.surfaceColor
            z: -1
        }
    }
    background: Rectangle {
        x: control.leftPadding
        y: control.edge ? 0 : control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: 200
        implicitHeight: control.edge ? control.edgeTrackHeight : 6
        width: control.availableWidth
        height: implicitHeight
        radius: control.edge ? 0 : 3
        // The edge track is a divider line; in high contrast it becomes an outlined canvas track, so the
        // system highlight of the filled part stands apart from it.
        color: control.contract.highContrast ? (control.edge ? control.contract.canvasColor : control.contract.surfaceColor) : (control.edge ? control.contract.dividerColor : control.outlineColor)
        border.width: control.contract.highContrast ? 1 : 0
        border.color: control.outlineColor
        scale: control.mirrored ? -1 : 1
        Rectangle {
            width: control.position * parent.width
            height: parent.height
            radius: control.edge ? 0 : 3
            color: control.fillColor
        }
    }
    Rectangle {
        objectName: "slider-focus-indicator"
        // On the edge the ring surrounds the marker; otherwise it outlines the whole control.
        anchors.fill: control.edge ? control.handle : parent
        anchors.margins: control.edge ? -(control.edgeRingWidth * 2) : 0
        color: "transparent"
        radius: control.edge ? width / 2 : control.contract.radiusSmall
        border.color: control.contract.focusColor
        border.width: 3
        visible: control.focusIndicatorVisible
        z: 10
    }
}
