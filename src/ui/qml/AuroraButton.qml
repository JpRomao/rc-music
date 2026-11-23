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
    // Quiet with the error ink, for a discreet destructive action such as clearing the queue.
    property bool errorInk: false
    // Motivo do desabilitado ou atalho, lido pelo leitor de tela.
    property string description

    readonly property bool filled: kind === AuroraButton.Primary || kind === AuroraButton.DangerFilled
    readonly property bool outlined: contract.highContrast && !filled && kind !== AuroraButton.Quiet
    // Desabilitado em alto contraste: troca de cor em vez de esmaecer.
    readonly property bool hcDisabled: contract.highContrast && interaction.inactive
    readonly property bool hasFill: {
        if (hcDisabled)
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
            return down ? interaction.tint(contract.hoverColor) : contract.hoverColor;
        let rest = contract.selectionColor;
        if (kind === AuroraButton.Primary)
            rest = contract.accentColor;
        else if (kind === AuroraButton.DangerFilled)
            rest = contract.errorColor;
        return interaction.tint(rest);
    }
    readonly property color foregroundColor: {
        if (hcDisabled)
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
            return errorInk ? contract.errorColor : contract.textPrimaryColor;
        }
    }

    objectName: controlId
    Accessible.role: Accessible.Button
    Accessible.id: controlId
    Accessible.name: text
    Accessible.description: description
    Accessible.focusable: enabled
    Accessible.focused: activeFocus
    // O leitor de tela não aciona um botão desabilitado.
    Accessible.onPressAction: {
        if (control.enabled)
            control.clicked();
    }
    activeFocusOnTab: true
    hoverEnabled: true
    opacity: interaction.opacity
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
        border.width: control.hcDisabled || control.outlined || (control.contract.highContrast && control.hovered) ? 2 : 0
        border.color: control.hcDisabled ? control.contract.disabledTextColor : (control.contract.highContrast && control.hovered ? control.contract.accentColor : control.contract.textPrimaryColor)
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

    AuroraInteraction {
        id: interaction
        contract: control.contract
        enabled: control.enabled
        hovered: control.hovered
        pressed: control.down
        // O texto em errorColor sobre Seleção ou Hover cairia abaixo de 4,5:1 com a camada inteira.
        layerScale: control.kind === AuroraButton.Danger || control.errorInk ? 0.5 : 1
    }

    AuroraFocusRing {
        target: control
        contract: control.contract
        targetRadius: control.contract.radiusControl
        areaY: control.background.y
        areaHeight: control.background.height
    }
}
