import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

Button {
    id: control
    required property ShellUiContract contract
    property string accessibleName: text
    Accessible.role: Accessible.Button
    Accessible.name: accessibleName
    Accessible.focusable: enabled
    Accessible.focused: activeFocus
    Accessible.onPressAction: control.clicked()
    activeFocusOnTab: true
    opacity: enabled ? 1.0 : 0.45
    implicitHeight: contract.controlTargetPreferred
    implicitWidth: Math.max(contract.controlTargetPreferred, contentItem.implicitWidth + 16)
    Keys.onReturnPressed: event => {
        control.clicked();
        event.accepted = true;
    }
    Keys.onEnterPressed: event => {
        control.clicked();
        event.accepted = true;
    }
    background: Rectangle {
        color: control.enabled ? control.contract.surfaceColor : "transparent"
        radius: control.contract.radiusSmall
        border.width: control.activeFocus ? 3 : 1
        border.color: control.activeFocus ? control.contract.focusColor : control.contract.borderColor
    }
    contentItem: Text {
        text: control.text
        color: control.contract.textPrimaryColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        topPadding: control.down ? 2 : 0
    }
}
