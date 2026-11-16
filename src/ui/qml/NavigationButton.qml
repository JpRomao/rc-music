import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

AccessibleTransportButton {
    id: root
    property string glyph: ""
    property bool compact: false
    property bool editorial: false
    accessibleName: text
    contentItem: Text {
        text: root.editorial ? (root.compact ? root.glyph : root.glyph + "   " + root.text) : root.text
        font: root.editorial ? Qt.font({
            family: root.font.family,
            pixelSize: root.compact ? root.contract.typeTitle : root.font.pixelSize,
            weight: root.checked && !root.compact ? Font.DemiBold : Font.Normal
        }) : root.font
        leftPadding: root.editorial && !root.compact ? 12 : 0
        color: root.contract.highContrast && !root.enabled ? root.contract.disabledTextColor : (root.contrastHighlighted ? root.contract.accentTextColor : (root.editorial && root.checked && !root.contract.highContrast ? root.contract.accentColor : root.contract.textPrimaryColor))
        horizontalAlignment: root.editorial && !root.compact ? Text.AlignLeft : Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        textFormat: Text.PlainText
    }
    background: Rectangle {
        color: root.contract.highContrast ? (root.contrastHighlighted ? root.contract.accentColor : root.contract.surfaceColor) : (root.editorial ? (root.checked ? root.contract.selectionColor : (root.hovered && root.enabled ? root.contract.hoverColor : root.contract.navigationColor)) : (root.checked ? root.contract.surfaceRaisedColor : root.contract.surfaceColor))
        radius: root.contract.radiusSmall
        border.width: root.activeFocus ? 3 : (root.editorial ? 0 : (root.checked ? 2 : 1))
        border.color: root.contract.highContrast ? (root.contrastHighlighted ? root.contract.accentTextColor : root.contract.focusColor) : (root.activeFocus ? root.contract.focusColor : root.contract.accentColor)
        Rectangle {
            visible: root.editorial && root.checked
            width: 3
            height: parent.height - 20
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            color: root.contract.accentColor
            radius: 1
            Accessible.ignored: true
        }
        Rectangle {
            objectName: "hover-feedback"
            anchors.fill: parent
            radius: root.contract.radiusSmall
            color: root.contract.textPrimaryColor
            visible: !root.contract.highContrast
            // Aurora uses the hover token on the background; the overlay remains for the frozen fixture.
            opacity: root.hovered && root.enabled && !root.contract.highContrast && !root.editorial ? 0.08 : 0
            Accessible.ignored: true
            Behavior on opacity {
                enabled: !root.contract.highContrast
                NumberAnimation {
                    duration: root.contract.feedbackTransitionMs
                }
            }
        }
    }
}
