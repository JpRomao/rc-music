import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

AbstractButton {
    id: control

    required property ShellUiContract contract
    required property string controlId
    required property string iconName
    property bool tonal: false
    property int iconSize: 20

    readonly property bool inactive: contract.highContrast && !enabled
    readonly property bool outlined: contract.highContrast && tonal && !checked
    readonly property color layerColor: Qt.color(contract.textPrimaryColor)
    readonly property bool hasFill: {
        if (inactive)
            return false;
        if (contract.highContrast)
            return checked || outlined;
        return tonal || checked || hovered || down;
    }
    // Só vale com hasFill; sem preenchimento o fundo fica sem cor.
    readonly property color fillColor: {
        if (contract.highContrast)
            return checked ? contract.accentColor : contract.canvasColor;
        const rest = tonal || checked ? contract.selectionColor : contract.hoverColor;
        const pressedTint = Qt.rgba(layerColor.r, layerColor.g, layerColor.b, 0.12);
        const hoverTint = Qt.rgba(layerColor.r, layerColor.g, layerColor.b, 0.06);
        if (down)
            return Qt.tint(rest, pressedTint);
        if (hovered && (tonal || checked))
            return Qt.tint(rest, hoverTint);
        return rest;
    }
    readonly property color foregroundColor: {
        if (inactive)
            return contract.disabledTextColor;
        if (checked)
            return contract.highContrast ? contract.accentTextColor : contract.accentColor;
        return contract.textPrimaryColor;
    }

    objectName: controlId
    Accessible.role: Accessible.Button
    Accessible.id: controlId
    Accessible.name: text
    Accessible.checkable: checkable
    Accessible.checked: checked
    Accessible.focusable: enabled
    Accessible.focused: activeFocus
    Accessible.onPressAction: control.clicked()
    activeFocusOnTab: true
    hoverEnabled: true
    opacity: enabled || contract.highContrast ? 1 : 0.4
    implicitWidth: contract.controlTargetMinimum
    implicitHeight: contract.controlTargetMinimum
    width: implicitWidth
    height: implicitHeight
    ToolTip.text: text
    ToolTip.visible: hovered && text.length > 0
    ToolTip.delay: 600

    Keys.onReturnPressed: event => {
        activate();
        event.accepted = true;
    }
    Keys.onEnterPressed: event => {
        activate();
        event.accepted = true;
    }
    function activate() {
        if (checkable)
            toggle();
        clicked();
    }

    background: Rectangle {
        objectName: "icon-button-background"
        radius: control.contract.radiusControl
        color: control.hasFill ? control.fillColor : "transparent"
        border.width: control.inactive || control.outlined || (control.contract.highContrast && control.hovered && !control.checked) ? 2 : 0
        border.color: control.inactive ? control.contract.disabledTextColor : (control.contract.highContrast && control.hovered ? control.contract.accentColor : control.contract.textPrimaryColor)
    }

    contentItem: Item {
        ShellIcon {
            anchors.centerIn: parent
            iconName: control.iconName
            size: control.iconSize
            color: control.foregroundColor
        }
    }

    Rectangle {
        objectName: "state-mark"
        width: 4
        height: 4
        radius: 2
        x: (control.width - width) / 2
        y: control.height - height - 4
        color: control.contract.highContrast ? control.contract.accentTextColor : control.contract.accentColor
        visible: control.checked
    }

    AuroraFocusRing {
        target: control
        contract: control.contract
        targetRadius: control.contract.radiusControl
    }
}
