import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

Button {
    id: control
    required property string controlId
    objectName: controlId
    Accessible.role: Accessible.Button
    Accessible.id: controlId
    Accessible.name: text
    Accessible.focusable: enabled
    Accessible.focused: activeFocus
    Accessible.onPressAction: control.clicked()
    activeFocusOnTab: true
    hoverEnabled: true
    Keys.onReturnPressed: event => {
        control.clicked();
        event.accepted = true;
    }
    Keys.onEnterPressed: event => {
        control.clicked();
        event.accepted = true;
    }
    implicitWidth: Math.max(contract.controlTargetPreferred, contentItem.implicitWidth + 32)
    implicitHeight: Math.max(contract.controlTargetPreferred, label.contentHeight + 16)
    required property ShellUiContract contract

    background: Rectangle {
        color: control.down && !control.contract.highContrast ? Qt.darker(control.contract.accentColor, 1.1) : control.contract.accentColor
        radius: control.contract.radiusMedium
        border.width: control.activeFocus ? 3 : (control.hovered || control.down ? 2 : 0)
        border.color: control.activeFocus || control.contract.highContrast ? control.contract.accentTextColor : control.contract.focusColor
    }
    contentItem: Text {
        id: label
        text: control.text
        color: control.contract.accentTextColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
        topPadding: control.down ? 2 : 0
    }
}
