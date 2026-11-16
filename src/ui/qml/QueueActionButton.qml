import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

ToolButton {
    id: control
    required property string controlId
    required property string accessibleName
    required property ShellUiContract contract
    property string shortcutHint: ""
    readonly property bool contrastHovered: contract.highContrast && enabled && hovered && !down
    objectName: controlId
    activeFocusOnTab: true
    hoverEnabled: true
    opacity: enabled || contract.highContrast ? 1 : 0.45
    implicitWidth: contract.controlTargetPreferred
    implicitHeight: contract.controlTargetPreferred
    Accessible.role: Accessible.Button
    Accessible.id: controlId
    Accessible.name: accessibleName
    Accessible.description: shortcutHint
    Accessible.focusable: enabled
    Accessible.focused: activeFocus
    Accessible.onPressAction: {
        if (control.enabled)
            control.clicked();
    }
    Keys.onReturnPressed: event => {
        control.clicked();
        event.accepted = true;
    }
    Keys.onEnterPressed: event => {
        control.clicked();
        event.accepted = true;
    }
    ToolTip.text: accessibleName + (shortcutHint.length > 0 ? " (" + shortcutHint + ")" : "")
    ToolTip.visible: hovered || activeFocus

    background: Rectangle {
        color: control.contract.highContrast ? (control.contrastHovered ? control.contract.accentColor : control.contract.surfaceColor) : (control.down ? Qt.darker(control.contract.surfaceRaisedColor, 1.08) : control.contract.surfaceRaisedColor)
        radius: control.contract.radiusSmall
        border.width: control.activeFocus ? 3 : (control.contract.highContrast || control.hovered ? 1 : 0)
        border.color: control.contract.highContrast ? (!control.enabled ? control.contract.disabledTextColor : (control.down ? control.contract.accentColor : (control.contrastHovered ? control.contract.accentTextColor : control.contract.focusColor))) : (control.activeFocus ? control.contract.focusColor : control.contract.borderColor)
    }
    contentItem: Text {
        text: control.text
        color: control.contract.highContrast && !control.enabled ? control.contract.disabledTextColor : (control.contrastHovered ? control.contract.accentTextColor : (control.enabled ? control.contract.textPrimaryColor : control.contract.textSecondaryColor))
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.pixelSize: control.contract.typeTitle
    }
}
