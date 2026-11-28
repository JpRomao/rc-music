import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

AbstractButton {
    id: control

    required property ShellUiContract contract
    required property string controlId
    required property string iconName
    property bool tonal: false
    // Acento: círculo ou quadrado cheio em accentColor, com o ícone em accentTextColor.
    property bool accent: false
    // Elevado: fundo de superfície elevada com sombra, para sobrepor a uma capa.
    property bool raised: false
    property bool round: false
    // Tamanho desenhado; a área de toque continua com o mínimo do contrato.
    property int drawnSize: 40
    property int iconSize: 20
    // Motivo do desabilitado ou atalho: vai para o leitor de tela e para a dica.
    property string description

    // Desabilitado em alto contraste: troca de cor em vez de esmaecer.
    readonly property bool hcDisabled: contract.highContrast && interaction.inactive
    readonly property bool outlined: contract.highContrast && (tonal || raised) && !checked && !accent
    readonly property bool hasFill: {
        if (hcDisabled)
            return false;
        if (contract.highContrast)
            return checked || outlined || accent;
        return tonal || accent || raised || checked || hovered || down;
    }
    // Só vale com hasFill; sem preenchimento o fundo fica sem cor.
    readonly property color fillColor: {
        if (contract.highContrast)
            return checked || accent ? contract.accentColor : contract.canvasColor;
        let rest = tonal || checked ? contract.selectionColor : contract.hoverColor;
        if (accent)
            rest = contract.accentColor;
        else if (raised)
            rest = contract.surfaceRaisedColor;
        if (down || (hovered && (tonal || checked || accent || raised)))
            return interaction.tint(rest);
        return rest;
    }
    readonly property color foregroundColor: {
        if (hcDisabled)
            return contract.disabledTextColor;
        if (accent || (checked && contract.highContrast))
            return contract.accentTextColor;
        if (checked)
            return contract.accentColor;
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
    // O leitor de tela não aciona um controle desabilitado.
    Accessible.onPressAction: {
        if (control.enabled)
            control.activate();
    }
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
        x: (control.width - width) / 2
        y: (control.height - height) / 2
        width: control.drawnSize
        height: control.drawnSize
        radius: control.round ? width / 2 : control.contract.radiusControl
        color: control.hasFill ? control.fillColor : "transparent"
        // Em alto contraste a variante de acento cheia leva sempre o contorno, mesmo sem hover.
        border.width: control.hcDisabled || control.outlined || (control.contract.highContrast && (control.accent || (control.hovered && !control.checked))) ? 2 : 0
        border.color: control.hcDisabled ? control.contract.disabledTextColor : (control.contract.highContrast && control.hovered && !control.accent ? control.contract.accentColor : control.contract.textPrimaryColor)

        AuroraShadow {
            contract: control.contract
            radius: parent.radius
            visible: control.raised && !control.contract.highContrast
        }
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
        // Desabilitado em alto contraste, o ponto é a única marca de ligado: ganha a cor de desabilitado.
        color: control.contract.highContrast ? interaction.ink(control.contract.accentTextColor) : control.contract.accentColor
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
        targetRadius: control.round ? control.drawnSize / 2 : control.contract.radiusControl
        areaX: (control.width - control.drawnSize) / 2
        areaY: (control.height - control.drawnSize) / 2
        areaWidth: control.drawnSize
        areaHeight: control.drawnSize
    }
}
