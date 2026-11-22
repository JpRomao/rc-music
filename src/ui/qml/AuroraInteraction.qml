import QtQuick
import MusicPlayer.M0

// Regras de estado compartilhadas pelas peças Aurora interativas: opacidade do
// desabilitado e camada de hover e pressionado sobre a cor de repouso.
// As entradas são booleanas: quem usa liga cada uma ao estado certo do controle.
QtObject {
    id: root
    required property ShellUiContract contract
    required property bool enabled
    property bool hovered: false
    property bool pressed: false

    readonly property bool inactive: !enabled
    // Em alto contraste o desabilitado não esmaece: troca de cor.
    readonly property real opacity: contract.highContrast || enabled ? 1 : 0.4
    readonly property color layerColor: Qt.color(contract.textPrimaryColor)

    // Cor de tinta: em alto contraste e desabilitado vira a cor de desabilitado.
    function ink(color) {
        return contract.highContrast && inactive ? contract.disabledTextColor : color;
    }

    function tint(rest) {
        if (pressed)
            return Qt.tint(rest, Qt.rgba(layerColor.r, layerColor.g, layerColor.b, 0.12));
        if (hovered)
            return Qt.tint(rest, Qt.rgba(layerColor.r, layerColor.g, layerColor.b, 0.06));
        return rest;
    }
}
