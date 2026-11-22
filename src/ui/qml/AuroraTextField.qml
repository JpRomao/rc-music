import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

// Campo preenchido, sem contorno: a linha na base marca o estado (normal, foco, erro).
ColumnLayout {
    id: root

    required property ShellUiContract contract
    required property string controlId
    required property string label
    property alias text: field.text
    property alias placeholderText: field.placeholderText
    property alias readOnly: field.readOnly
    property string errorText: ""
    property string leadingIconName: ""
    readonly property alias input: field
    readonly property bool hasError: errorText.length > 0

    signal accepted

    activeFocusOnTab: false
    spacing: 4

    TextField {
        id: field
        objectName: root.controlId
        Layout.fillWidth: true
        implicitHeight: root.contract.controlTargetMinimum
        activeFocusOnTab: true
        hoverEnabled: true
        opacity: interaction.opacity
        font.family: root.contract.bodyFontFamily
        font.pixelSize: root.contract.typeBody
        color: interaction.inactive && root.contract.highContrast ? root.contract.disabledTextColor : root.contract.textPrimaryColor
        placeholderTextColor: root.contract.textSecondaryColor
        selectionColor: root.contract.accentColor
        selectedTextColor: root.contract.accentTextColor
        verticalAlignment: TextInput.AlignVCenter
        leftPadding: root.leadingIconName.length > 0 ? 40 : 12
        rightPadding: 12
        topPadding: 0
        bottomPadding: 0
        Accessible.name: root.label
        Accessible.description: root.hasError ? root.errorText : ""
        onAccepted: root.accepted()

        background: Rectangle {
            objectName: "field-background"
            radius: root.contract.radiusControl
            color: root.contract.highContrast ? root.contract.canvasColor : interaction.tint(root.contract.surfaceColor)
            border.width: root.contract.highContrast ? 2 : 0
            border.color: interaction.inactive ? root.contract.disabledTextColor : (field.activeFocus || root.hasError) ? root.contract.accentColor : root.contract.textPrimaryColor

            Rectangle {
                objectName: "field-line"
                visible: !root.contract.highContrast
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: field.activeFocus || root.hasError ? 2 : 1
                color: root.hasError ? root.contract.errorColor : field.activeFocus ? root.contract.focusColor : root.contract.textSecondaryColor
            }
        }

        ShellIcon {
            objectName: "field-icon"
            iconName: root.leadingIconName
            color: root.contract.textSecondaryColor
            x: 12
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    Text {
        objectName: root.controlId + "-error"
        visible: root.hasError
        text: root.errorText
        color: root.contract.errorColor
        font.family: root.contract.bodyFontFamily
        font.pixelSize: root.contract.typeCaption
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        Layout.leftMargin: 4
        Accessible.ignored: true
    }

    AuroraInteraction {
        id: interaction
        contract: root.contract
        control: field
    }
}
