import QtQuick
import MusicPlayer.M0

// Anel de foco: 3 px em focusColor, separado do alvo por uma folga de 2 px,
// visível só com foco ativo. Pensado para ser filho do alvo; quem marca outra
// área ajusta areaX, areaY, areaWidth e areaHeight, nas coordenadas do pai do anel.
// Com inside, o anel fica por dentro da área, para itens colados (menus e listas).
Rectangle {
    id: root
    required property Item target
    required property ShellUiContract contract
    property real targetRadius: 0
    property real areaX: 0
    property real areaY: 0
    property real areaWidth: target.width
    property real areaHeight: target.height
    // Quando o anel aparece; por padrão, com o foco ativo do alvo.
    property bool shown: target.activeFocus
    property bool inside: false

    readonly property real gap: 2
    readonly property real outset: inside ? 0 : gap + contract.focusRingWidth

    objectName: "focus-ring"
    x: areaX - outset
    y: areaY - outset
    width: areaWidth + 2 * outset
    height: areaHeight + 2 * outset
    radius: targetRadius + outset
    color: "transparent"
    border.width: contract.focusRingWidth
    border.color: contract.focusColor
    visible: shown
    z: 10
}
