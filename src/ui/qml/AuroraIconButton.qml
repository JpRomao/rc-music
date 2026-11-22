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
    // Motivo do desabilitado ou atalho: vai para o leitor de tela e para a dica.
    property string description

    readonly property bool inactive: contract.highContrast && interaction.inactive
    readonly property bool outlined: contract.highContrast && tonal && !checked
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
        if (down || (hovered && (tonal || checked)))
            return interaction.tint(rest);
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
    Accessible.description: description
    Accessible.checkable: checkable
    Accessible.checked: checked
    Accessible.focusable: enabled
    Accessible.focused: activeFocus
    Accessible.onPressAction: control.activate()
    activeFocusOnTab: true
    hoverEnabled: true
    opacity: interaction.opacity
    implicitWidth: contract.controlTargetMinimum
    implicitHeight: contract.controlTargetMinimum
    width: implicitWidth
    height: implicitHeight
    // A dica nomeia o botão só de ícone também para quem usa o teclado.
    ToolTip.text: controlsText.toolTip(text, description)
    ToolTip.visible: (hovered || visualFocus) && text.length > 0
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
        if (checkable) {
            toggle();
            toggled();
        }
        clicked();
    }

    ControlsText {
        id: controlsText
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

    AuroraInteraction {
        id: interaction
        contract: control.contract
        enabled: control.enabled
        hovered: control.hovered
        pressed: control.down
    }

    AuroraFocusRing {
        target: control
        contract: control.contract
        targetRadius: control.contract.radiusControl
    }
}
