import QtQuick
import MusicPlayer.M0

// Regras de estado compartilhadas pelas peças Aurora interativas: opacidade do
// desabilitado e camada de hover e pressionado sobre a cor de repouso.
QtObject {
    id: root
    required property ShellUiContract contract
    // Controle com enabled, hovered e down (um AbstractButton); var evita acoplar o tipo.
    required property var control

    readonly property bool inactive: !(control && control.enabled)
    // Em alto contraste o desabilitado não esmaece: troca de cor.
    readonly property real opacity: contract.highContrast || (control && control.enabled) ? 1 : 0.4
    readonly property color layerColor: Qt.color(contract.textPrimaryColor)

    // Cor de tinta: em alto contraste e desabilitado vira a cor de desabilitado.
    function ink(color) {
        return contract.highContrast && inactive ? contract.disabledTextColor : color;
    }

    function tint(rest) {
        if (!control)
            return rest;
        if (control.down)
            return Qt.tint(rest, Qt.rgba(layerColor.r, layerColor.g, layerColor.b, 0.12));
        if (control.hovered)
            return Qt.tint(rest, Qt.rgba(layerColor.r, layerColor.g, layerColor.b, 0.06));
        return rest;
    }
}
