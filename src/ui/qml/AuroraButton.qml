import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

Button {
    id: control

    enum Kind {
        Primary,
        Tonal,
        Quiet,
        Danger,
        DangerFilled
    }

    required property ShellUiContract contract
    required property string controlId
    property int kind: AuroraButton.Primary
    property string iconName
    property bool compact: false

    readonly property bool filled: kind === AuroraButton.Primary || kind === AuroraButton.DangerFilled
    readonly property bool outlined: contract.highContrast && !filled && kind !== AuroraButton.Quiet
    readonly property bool inactive: contract.highContrast && !enabled
    readonly property color layerColor: Qt.color(contract.textPrimaryColor)
    readonly property bool hasFill: {
        if (inactive)
            return false;
        if (contract.highContrast)
            return filled || outlined;
        return kind !== AuroraButton.Quiet || hovered || down;
    }
    // Só vale com hasFill; sem preenchimento o fundo fica sem cor.
    readonly property color fillColor: {
        if (contract.highContrast)
            return filled ? contract.accentColor : contract.canvasColor;
        if (kind === AuroraButton.Quiet)
            return down ? Qt.tint(contract.hoverColor, Qt.rgba(layerColor.r, layerColor.g, layerColor.b, 0.06)) : contract.hoverColor;
        let rest = contract.selectionColor;
        if (kind === AuroraButton.Primary)
            rest = contract.accentColor;
        else if (kind === AuroraButton.DangerFilled)
            rest = contract.errorColor;
        if (down)
            return Qt.tint(rest, Qt.rgba(layerColor.r, layerColor.g, layerColor.b, 0.12));
        if (hovered)
            return Qt.tint(rest, Qt.rgba(layerColor.r, layerColor.g, layerColor.b, 0.06));
        return rest;
    }
    readonly property color foregroundColor: {
        if (inactive)
            return contract.disabledTextColor;
        if (contract.highContrast && filled)
            return contract.accentTextColor;
        switch (kind) {
        case AuroraButton.Primary:
        case AuroraButton.DangerFilled:
            return contract.accentTextColor;
        case AuroraButton.Danger:
            return contract.errorColor;
        default:
            return contract.textPrimaryColor;
        }
    }

    objectName: controlId
    Accessible.role: Accessible.Button
    Accessible.id: controlId
    Accessible.name: text
    Accessible.focusable: enabled
    Accessible.focused: activeFocus
    Accessible.onPressAction: control.clicked()
    activeFocusOnTab: true
    hoverEnabled: true
    opacity: enabled || contract.highContrast ? 1 : 0.4
    font.family: contract.bodyFontFamily
    font.pixelSize: contract.typeBody
    font.weight: Font.DemiBold
    leftPadding: compact ? 12 : 16
    rightPadding: leftPadding
    topPadding: 0
    bottomPadding: 0
    implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
    implicitHeight: contract.controlTargetMinimum

    Keys.onReturnPressed: event => {
        control.clicked();
        event.accepted = true;
    }
    Keys.onEnterPressed: event => {
        control.clicked();
        event.accepted = true;
    }

    background: Rectangle {
        objectName: "button-background"
        y: control.compact ? (control.height - height) / 2 : 0
        width: control.width
        height: control.compact ? 32 : control.height
        radius: control.contract.radiusControl
        color: control.hasFill ? control.fillColor : "transparent"
        border.width: control.inactive || control.outlined || (control.contract.highContrast && control.hovered) ? 2 : 0
        border.color: control.inactive ? control.contract.disabledTextColor : (control.contract.highContrast && control.hovered ? control.contract.accentColor : control.contract.textPrimaryColor)
    }

    contentItem: RowLayout {
        spacing: 8
        ShellIcon {
            objectName: "button-icon"
            iconName: control.iconName
            color: control.foregroundColor
            Layout.alignment: Qt.AlignVCenter
        }
        Text {
            objectName: "button-label"
            text: control.text
            font: control.font
            color: control.foregroundColor
            elide: Text.ElideRight
            maximumLineCount: 1
            wrapMode: Text.NoWrap
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignVCenter
        }
    }

    AuroraFocusRing {
        target: control
        contract: control.contract
        targetRadius: control.contract.radiusControl
        y: control.background.y - 2
        height: control.background.height + 4
    }
}
