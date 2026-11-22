pragma ComponentBehavior: Bound

import QtQuick
import MusicPlayer.M0

// Chave segmentada: um grupo de opções exclusivas, no padrão de grupo de rádio.
// Só a opção escolhida entra na ordem de Tab; as setas movem e escolhem.
FocusScope {
    id: root

    required property ShellUiContract contract
    required property string controlId
    required property string label
    // Lista de textos, um por opção.
    required property var options
    property int currentIndex: 0

    signal activated(int index)

    objectName: controlId
    activeFocusOnTab: false
    implicitWidth: row.implicitWidth
    implicitHeight: contract.controlTargetMinimum
    width: implicitWidth
    height: implicitHeight
    opacity: groupInteraction.opacity
    Accessible.role: Accessible.Grouping
    Accessible.id: controlId
    Accessible.name: label

    // Escolhe a opção e leva o foco até ela; só avisa quando a escolha muda.
    function select(index) {
        if (!enabled || index < 0 || index >= options.length)
            return;
        const changed = index !== currentIndex;
        // O foco muda antes: a opção que o perde não pode largar a ordem de Tab com foco.
        const target = repeater.itemAt(index);
        if (target)
            target.forceActiveFocus(Qt.TabFocusReason);
        currentIndex = index;
        if (changed)
            activated(index);
    }

    Rectangle {
        id: rail
        objectName: "segmented-rail"
        anchors.fill: parent
        radius: root.contract.radiusControl
        color: root.contract.highContrast ? root.contract.canvasColor : root.contract.selectionColor
        border.width: root.contract.highContrast ? 2 : 0
        border.color: groupInteraction.inactive ? root.contract.disabledTextColor : root.contract.textPrimaryColor

        Row {
            id: row
            height: parent.height

            Repeater {
                id: repeater
                model: root.options

                delegate: Item {
                    id: option

                    required property int index
                    required property var modelData
                    readonly property bool chosen: option.index === root.currentIndex
                    readonly property bool hovered: hover.hovered
                    readonly property bool down: tap.pressed
                    readonly property bool filled: chosen && !(root.contract.highContrast && groupInteraction.inactive)

                    objectName: root.controlId + "-" + option.index
                    width: Math.max(64, segmentLabel.implicitWidth + 32)
                    height: root.contract.controlTargetMinimum
                    activeFocusOnTab: chosen
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: String(option.modelData)
                    Accessible.checkable: true
                    Accessible.checked: chosen
                    Accessible.focusable: root.enabled
                    Accessible.focused: activeFocus
                    Accessible.onPressAction: root.select(option.index)

                    Keys.onPressed: event => {
                        let target = -1;
                        if (event.key === Qt.Key_Left)
                            target = option.index - 1;
                        else if (event.key === Qt.Key_Right)
                            target = option.index + 1;
                        else if (event.key === Qt.Key_Home)
                            target = 0;
                        else if (event.key === Qt.Key_End)
                            target = root.options.length - 1;
                        else if (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                            target = option.index;
                        else
                            return;
                        root.select(target);
                        event.accepted = true;
                    }

                    HoverHandler {
                        id: hover
                    }
                    TapHandler {
                        id: tap
                        onTapped: root.select(option.index)
                    }

                    AuroraInteraction {
                        id: interaction
                        contract: root.contract
                        control: option
                    }

                    Rectangle {
                        objectName: "segment-pill"
                        anchors.fill: parent
                        anchors.margins: 3
                        radius: root.contract.radiusControl - 3
                        visible: option.filled || (!root.contract.highContrast && (option.hovered || option.down))
                        color: {
                            if (root.contract.highContrast)
                                return option.filled ? root.contract.accentColor : root.contract.canvasColor;
                            return option.chosen ? interaction.tint(root.contract.surfaceColor) : interaction.tint(root.contract.selectionColor);
                        }
                    }

                    Text {
                        id: segmentLabel
                        objectName: "segment-label"
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: (parent.height - height) / 2 - 2
                        text: String(option.modelData)
                        font.family: root.contract.bodyFontFamily
                        font.pixelSize: root.contract.typeBody
                        font.weight: option.chosen ? Font.DemiBold : Font.Normal
                        color: {
                            if (groupInteraction.inactive && root.contract.highContrast)
                                return root.contract.disabledTextColor;
                            if (option.chosen)
                                return root.contract.highContrast ? root.contract.accentTextColor : root.contract.accentColor;
                            return root.contract.textPrimaryColor;
                        }
                    }

                    Rectangle {
                        objectName: "state-mark"
                        width: segmentLabel.width
                        height: 3
                        radius: 1.5
                        x: segmentLabel.x
                        y: segmentLabel.y + segmentLabel.height + 1
                        color: root.contract.highContrast ? root.contract.accentTextColor : root.contract.accentColor
                        visible: option.chosen
                    }

                    AuroraFocusRing {
                        target: option
                        contract: root.contract
                        targetRadius: root.contract.radiusControl
                    }
                }
            }
        }
    }

    AuroraInteraction {
        id: groupInteraction
        contract: root.contract
        control: root
    }
}
