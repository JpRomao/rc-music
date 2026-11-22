import QtQuick
import MusicPlayer.M0

// Anel de foco: 2 px para fora do alvo, visível só com foco ativo.
// Pensado para ser filho do alvo; quem precisa de outra geometria sobrescreve x, y, width e height.
Rectangle {
    id: root
    required property Item target
    required property ShellUiContract contract
    property real targetRadius: 0

    objectName: "focus-ring"
    x: -2
    y: -2
    width: target.width + 4
    height: target.height + 4
    radius: targetRadius + 2
    color: "transparent"
    border.width: contract.focusRingWidth
    border.color: contract.focusColor
    visible: target.activeFocus
    z: 10
}
