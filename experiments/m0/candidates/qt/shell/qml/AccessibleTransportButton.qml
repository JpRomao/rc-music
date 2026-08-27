import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

Button {
    id: control
    required property ShellUiContract contract
    property string controlId: objectName
    property string accessibleName: text
    Accessible.role: Accessible.Button
    Accessible.id: controlId
    Accessible.name: accessibleName
    Accessible.focusable: enabled
    Accessible.focused: activeFocus
    Accessible.onPressAction: control.clicked()
    activeFocusOnTab: true
    hoverEnabled: true
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
        color: control.enabled ? (control.checked ? control.contract.surfaceRaisedColor : control.contract.surfaceColor) : "transparent"
        radius: control.contract.radiusSmall
        border.width: control.activeFocus ? 3 : (control.checked ? 2 : 1)
        border.color: control.activeFocus ? control.contract.focusColor : (control.checked ? control.contract.accentColor : control.contract.borderColor)
        Rectangle {
            objectName: "hover-feedback"
            anchors.fill: parent
            radius: control.contract.radiusSmall
            color: control.contract.textPrimaryColor
            opacity: control.hovered && control.enabled ? 0.08 : 0
            Accessible.ignored: true
            Behavior on opacity {
                NumberAnimation {
                    duration: control.contract.feedbackTransitionMs
                }
            }
        }
    }
    contentItem: Text {
        text: control.text
        color: control.contract.textPrimaryColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        topPadding: control.down ? 2 : 0
    }
}
