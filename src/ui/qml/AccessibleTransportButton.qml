import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

Button {
    id: control
    required property ShellUiContract contract
    property string controlId: objectName
    property string accessibleName: text
    property string shortcutHint: ""
    property string disabledReason: ""
    readonly property string accessibleDescription: !enabled && disabledReason.length > 0 ? disabledReason : shortcutHint
    readonly property string toolTipText: !enabled && disabledReason.length > 0 ? accessibleName + " — " + disabledReason : accessibleName + (shortcutHint.length > 0 ? " (" + shortcutHint + ")" : "")
    readonly property bool contrastHighlighted: contract.highContrast && enabled && (hovered || down || checked)
    Accessible.role: Accessible.Button
    Accessible.id: controlId
    Accessible.name: accessibleName
    Accessible.description: accessibleDescription
    Accessible.focusable: enabled
    Accessible.focused: activeFocus
    Accessible.onPressAction: control.clicked()
    activeFocusOnTab: true
    hoverEnabled: true
    opacity: enabled || contract.highContrast ? 1.0 : 0.45
    implicitHeight: contract.controlTargetPreferred
    implicitWidth: Math.max(contract.controlTargetPreferred, contentItem.implicitWidth + 16)
    ToolTip.text: toolTipText
    ToolTip.visible: hovered || activeFocus
    Keys.onReturnPressed: event => {
        control.clicked();
        event.accepted = true;
    }
    Keys.onEnterPressed: event => {
        control.clicked();
        event.accepted = true;
    }
    background: Rectangle {
        color: control.contract.highContrast ? (control.contrastHighlighted ? control.contract.accentColor : control.contract.surfaceColor) : (control.enabled ? (control.checked ? control.contract.surfaceRaisedColor : control.contract.surfaceColor) : "transparent")
        radius: control.contract.radiusSmall
        border.width: control.activeFocus ? 3 : (control.checked ? 2 : 1)
        border.color: control.contract.highContrast ? (!control.enabled ? control.contract.disabledTextColor : (control.contrastHighlighted ? control.contract.accentTextColor : control.contract.focusColor)) : (control.activeFocus ? control.contract.focusColor : (control.checked ? control.contract.accentColor : control.contract.borderColor))
        Rectangle {
            objectName: "hover-feedback"
            anchors.fill: parent
            radius: control.contract.radiusSmall
            color: control.contract.textPrimaryColor
            visible: !control.contract.highContrast
            opacity: !control.contract.highContrast && control.hovered && control.enabled ? 0.08 : 0
            Accessible.ignored: true
            Behavior on opacity {
                enabled: !control.contract.highContrast
                NumberAnimation {
                    duration: control.contract.feedbackTransitionMs
                }
            }
        }
    }
    contentItem: Text {
        text: control.text
        color: control.contract.highContrast && !control.enabled ? control.contract.disabledTextColor : (control.contrastHighlighted ? control.contract.accentTextColor : control.contract.textPrimaryColor)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        topPadding: control.down ? 2 : 0
    }
}
