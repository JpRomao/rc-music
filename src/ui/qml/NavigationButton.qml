import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

AccessibleTransportButton {
    id: root
    property bool compact: false
    property bool editorial: false
    // Aurora only: a number at the right edge (hidden below zero) and an optional trailing icon.
    property int count: -1
    property string countText: ""
    property string trailingIconName: ""
    readonly property bool showsCount: editorial && !compact && count >= 0
    readonly property bool showsTrailingIcon: editorial && !compact && trailingIconName.length > 0
    // Quiet at rest; the stronger text color on the selected and hover backgrounds keeps 4.5:1.
    readonly property color accessoryColor: contract.highContrast ? label.color : (checked || (hovered && enabled) ? contract.textPrimaryColor : contract.textSecondaryColor)
    accessibleName: text
    Accessible.description: countText.length > 0 ? countText : accessibleDescription
    contentItem: Text {
        id: label
        readonly property bool showsIcon: root.editorial && root.iconName.length > 0
        // The compact rail shows only the icon; the name stays available as accessibleName/tooltip.
        text: root.editorial && root.compact ? "" : root.text
        font: root.editorial ? Qt.font({
            family: root.font.family,
            pixelSize: root.compact ? root.contract.typeTitle : root.font.pixelSize,
            weight: root.checked && !root.compact ? Font.DemiBold : Font.Normal
        }) : root.font
        leftPadding: root.editorial && !root.compact ? (label.showsIcon ? 12 + navigationIcon.width + 12 : 12) : 0
        // The name elides before the count or the trailing icon.
        rightPadding: root.showsCount ? navigationCount.implicitWidth + 12 + 8 : (root.showsTrailingIcon ? trailingIcon.width + 12 + 8 : 0)
        color: root.contract.highContrast && !root.enabled ? root.contract.disabledTextColor : (root.contrastHighlighted ? root.contract.accentTextColor : (root.editorial && root.checked && !root.contract.highContrast ? root.contract.accentColor : root.contract.textPrimaryColor))
        horizontalAlignment: root.editorial && !root.compact ? Text.AlignLeft : Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        textFormat: Text.PlainText
        ShellIcon {
            id: navigationIcon
            iconName: label.showsIcon ? root.iconName : ""
            color: label.color
            x: root.compact ? (label.width - width) / 2 : 12
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            id: navigationCount
            objectName: "navigation-count"
            visible: root.showsCount
            text: String(root.count)
            font.family: root.contract.bodyFontFamily
            font.pixelSize: root.contract.typeCaption
            color: root.accessoryColor
            textFormat: Text.PlainText
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            Accessible.ignored: true
        }
        ShellIcon {
            id: trailingIcon
            objectName: "navigation-trailing-icon"
            size: 16
            visible: root.showsTrailingIcon
            iconName: root.trailingIconName
            color: root.accessoryColor
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
        }
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
