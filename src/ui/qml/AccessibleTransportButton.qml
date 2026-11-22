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
    property bool primary: false
    property string iconName: ""
    property int iconSize: 20
    property bool iconWithText: false
    readonly property string accessibleDescription: !enabled && disabledReason.length > 0 ? disabledReason : shortcutHint
    readonly property string toolTipText: !enabled && disabledReason.length > 0 ? accessibleName + " — " + disabledReason : accessibleName + (shortcutHint.length > 0 ? " (" + shortcutHint + ")" : "")
    readonly property bool contrastHighlighted: contract.highContrast && enabled && (hovered || down || checked)
    readonly property bool quiet: flat && !primary && !contract.highContrast
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
        color: control.contract.highContrast ? (control.contrastHighlighted ? control.contract.accentColor : control.contract.surfaceColor) : (control.quiet ? (control.checked ? control.contract.selectionColor : (control.hovered && control.enabled ? control.contract.hoverColor : control.contract.surfaceColor)) : (control.enabled ? (control.primary ? control.contract.accentColor : (control.checked ? control.contract.surfaceRaisedColor : control.contract.surfaceColor)) : "transparent"))
        radius: control.primary ? height / 2 : control.contract.radiusSmall
        border.width: control.activeFocus ? 3 : (control.quiet ? 0 : (control.checked ? 2 : 1))
        border.color: control.contract.highContrast ? (!control.enabled ? control.contract.disabledTextColor : (control.contrastHighlighted ? control.contract.accentTextColor : control.contract.focusColor)) : (control.activeFocus ? (control.primary ? control.contract.accentTextColor : control.contract.focusColor) : (control.checked ? control.contract.accentColor : control.contract.borderColor))
        Rectangle {
            anchors.fill: parent
            anchors.margins: -5
            visible: control.primary && control.activeFocus
            color: "transparent"
            border.width: 2
            border.color: control.contract.focusColor
            radius: height / 2
            Accessible.ignored: true
        }
        Rectangle {
            objectName: "hover-feedback"
            anchors.fill: parent
            radius: control.contract.radiusSmall
            color: control.contract.textPrimaryColor
            visible: !control.contract.highContrast
            opacity: !control.contract.highContrast && !control.quiet && control.hovered && control.enabled ? 0.08 : 0
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
        id: buttonLabel
        readonly property int iconGap: 8
        // With an icon, the text is either replaced (icon-only glyph buttons) or kept beside it.
        text: control.iconName.length > 0 && !control.iconWithText ? "" : control.text
        font: control.font
        color: control.contract.highContrast && !control.enabled ? control.contract.disabledTextColor : (control.contrastHighlighted || (control.primary && !control.contract.highContrast && control.enabled) ? control.contract.accentTextColor : (control.quiet && control.checked ? control.contract.accentColor : control.contract.textPrimaryColor))
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        topPadding: control.down ? 2 : 0
        leftPadding: control.iconName.length > 0 && control.iconWithText ? buttonIcon.width + iconGap : 0
        ShellIcon {
            id: buttonIcon
            iconName: control.iconName
            size: control.iconSize
            color: buttonLabel.color
            // Centers the icon alone, or the icon + text pair as one group.
            x: control.iconWithText ? (buttonLabel.width + buttonLabel.leftPadding - buttonLabel.contentWidth) / 2 - buttonLabel.iconGap - width : (buttonLabel.width - width) / 2
            y: (buttonLabel.height - height) / 2 + buttonLabel.topPadding / 2
        }
    }
}
