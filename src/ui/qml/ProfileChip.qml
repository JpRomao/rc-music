import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

// Chip do perfil no pé da lateral Aurora: avatar, apelido e a legenda do perfil.
// Na lateral compacta só o avatar aparece; o nome fica no nome acessível e na dica.
AbstractButton {
    id: root

    required property ShellUiContract contract
    property var profile: null
    property bool compact: false
    property string controlId: "profile-chip"
    readonly property string nickname: profile !== null && typeof profile.nickname === "string" ? profile.nickname : ""
    readonly property string accessibleName: labels.openProfile
    readonly property string toolTipText: accessibleName
    readonly property bool contrastHighlighted: contract.highContrast && enabled && (hovered || down)
    // The avatar corner cover must match whatever fills the chip right now.
    readonly property color fillColor: contract.highContrast ? (contrastHighlighted ? contract.accentColor : contract.surfaceColor) : (hovered && enabled ? contract.hoverColor : contract.navigationColor)

    objectName: controlId
    activeFocusOnTab: true
    hoverEnabled: true
    implicitHeight: Math.max(contract.controlTargetMinimum, avatar.size + 16)
    implicitWidth: compact ? implicitHeight : 160
    Accessible.role: Accessible.Button
    Accessible.id: controlId
    Accessible.name: accessibleName
    Accessible.description: nickname
    Accessible.focusable: enabled
    Accessible.focused: activeFocus
    Accessible.onPressAction: root.clicked()
    ToolTip.text: toolTipText
    ToolTip.visible: compact && (hovered || activeFocus)
    Keys.onReturnPressed: event => {
        root.clicked();
        event.accepted = true;
    }
    Keys.onEnterPressed: event => {
        root.clicked();
        event.accepted = true;
    }

    ProfileText {
        id: labels
    }
    background: Rectangle {
        color: root.fillColor
        radius: root.contract.radiusSmall
        border.width: root.activeFocus ? 3 : 0
        border.color: root.contract.focusColor
    }
    contentItem: Item {
        AuroraAvatar {
            id: avatar
            objectName: "profile-chip-avatar"
            contract: root.contract
            size: 32
            source: root.profile !== null ? root.profile.avatarSource : ""
            backgroundColor: root.fillColor
            x: root.compact ? (parent.width - width) / 2 : 8
            anchors.verticalCenter: parent.verticalCenter
        }
        Column {
            visible: !root.compact
            anchors.left: avatar.right
            anchors.leftMargin: 10
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            Text {
                objectName: "profile-chip-nickname"
                width: parent.width
                visible: !root.compact && root.nickname.length > 0
                text: root.nickname
                textFormat: Text.PlainText
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeBody
                font.weight: Font.DemiBold
                color: root.contrastHighlighted ? root.contract.accentTextColor : root.contract.textPrimaryColor
                elide: Text.ElideRight
                Accessible.ignored: true
            }
            Text {
                objectName: "profile-chip-caption"
                width: parent.width
                visible: !root.compact
                text: labels.yourProfile
                textFormat: Text.PlainText
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeCaption
                color: root.contrastHighlighted ? root.contract.accentTextColor : (root.contract.highContrast || (root.hovered && root.enabled) ? root.contract.textPrimaryColor : root.contract.textSecondaryColor)
                elide: Text.ElideRight
                Accessible.ignored: true
            }
        }
    }
}
