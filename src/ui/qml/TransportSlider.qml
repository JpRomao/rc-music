import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

Slider {
    id: control
    required property ShellUiContract contract
    readonly property bool focusIndicatorVisible: activeFocus && enabled
    readonly property color outlineColor: contract.highContrast && !enabled ? contract.disabledTextColor : contract.textSecondaryColor
    readonly property color fillColor: contract.highContrast && !enabled ? contract.disabledTextColor : contract.accentColor
    implicitHeight: contract.controlTargetMinimum
    padding: 6
    palette.window: contract.surfaceColor
    palette.dark: fillColor

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: 28
        implicitHeight: 28
        radius: width / 2
        color: control.pressed && !control.contract.highContrast ? control.contract.surfaceRaisedColor : control.contract.surfaceColor
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus && !control.contract.highContrast ? control.fillColor : control.outlineColor
    }
    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: 200
        implicitHeight: 6
        width: control.availableWidth
        height: implicitHeight
        radius: 3
        color: control.contract.highContrast ? control.contract.surfaceColor : control.outlineColor
        border.width: control.contract.highContrast ? 1 : 0
        border.color: control.outlineColor
        scale: control.mirrored ? -1 : 1
        Rectangle {
            width: control.position * parent.width
            height: parent.height
            radius: 3
            color: control.fillColor
        }
    }
    Rectangle {
        objectName: "slider-focus-indicator"
        anchors.fill: parent
        color: "transparent"
        radius: control.contract.radiusSmall
        border.color: control.contract.focusColor
        border.width: 3
        visible: control.focusIndicatorVisible
        z: 10
    }
}
