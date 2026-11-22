pragma ComponentBehavior: Bound
import QtQuick
import MusicPlayer.M0

// Sombra em camadas feita de retângulos translúcidos, sem módulo de efeitos.
// Vai como filho do fundo da superfície e fica atrás dele.
Item {
    id: root

    required property ShellUiContract contract
    // Raio da superfície que projeta a sombra.
    property real radius: 0

    objectName: "surface-shadow"
    anchors.fill: parent
    z: -1
    visible: !contract.highContrast

    Repeater {
        model: [
            {
                spread: 2,
                offset: 1,
                layerOpacity: 1
            },
            {
                spread: 6,
                offset: 3,
                layerOpacity: 0.6
            },
            {
                spread: 12,
                offset: 6,
                layerOpacity: 0.3
            }
        ]

        Rectangle {
            id: layer
            required property var modelData
            x: -layer.modelData.spread
            y: -layer.modelData.spread + layer.modelData.offset
            width: root.width + layer.modelData.spread * 2
            height: root.height + layer.modelData.spread * 2
            radius: root.radius + layer.modelData.spread
            color: root.contract.shadowColor
            opacity: layer.modelData.layerOpacity
        }
    }
}
