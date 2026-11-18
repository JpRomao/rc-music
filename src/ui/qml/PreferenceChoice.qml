pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

ColumnLayout {
    id: root
    required property ShellUiContract contract
    required property string controlId
    required property string label
    required property var options
    required property int selected
    property string unselectedText: ""
    property string accessibleDescription: ""
    readonly property alias control: control
    signal valueActivated(int index)
    spacing: contract.spacingUnit
    onOptionsChanged: {
        if (control.popup && control.popup.visible)
            control.popup.close();
    }

    Text {
        text: root.label
        textFormat: Text.PlainText
        color: root.contract.textPrimaryColor
        Layout.fillWidth: true
    }
    ComboBox {
        id: control
        objectName: root.controlId
        Layout.fillWidth: true
        implicitHeight: root.contract.controlTargetPreferred
        model: root.options
        currentIndex: root.selected
        displayText: currentIndex < 0 ? root.unselectedText : currentText
        activeFocusOnTab: true
        Accessible.role: Accessible.ComboBox
        Accessible.id: objectName
        Accessible.name: root.label
        Accessible.description: root.accessibleDescription
        Accessible.focusable: enabled
        Accessible.focused: activeFocus
        function restoreConfirmedSelection() {
            if (currentIndex !== root.selected)
                currentIndex = Qt.binding(() => root.selected);
        }
        onModelChanged: Qt.callLater(control.restoreConfirmedSelection)
        onActivated: index => {
            root.valueActivated(index);
            restoreConfirmedSelection();
        }
        palette.base: root.contract.surfaceColor
        palette.button: root.contract.surfaceColor
        palette.buttonText: root.contract.highContrast && down ? root.contract.accentTextColor : root.contract.textPrimaryColor
        palette.text: root.contract.textPrimaryColor
        palette.window: root.contract.surfaceColor
        palette.windowText: root.contract.textPrimaryColor
        palette.highlight: root.contract.accentColor
        palette.highlightedText: root.contract.accentTextColor
        palette.mid: root.contract.highContrast ? root.contract.accentColor : root.contract.borderColor
        palette.dark: root.contract.highContrast && down ? root.contract.accentTextColor : root.contract.textPrimaryColor
        delegate: ItemDelegate {
            id: option
            required property int index
            width: option.ListView.view.width
            implicitHeight: Math.max(root.contract.controlTargetPreferred, implicitContentHeight + topPadding + bottomPadding)
            text: control.textAt(index)
            palette.text: control.palette.text
            palette.highlightedText: control.palette.highlightedText
            font.weight: control.currentIndex === index ? Font.DemiBold : Font.Normal
            highlighted: control.highlightedIndex === index
            hoverEnabled: control.hoverEnabled
            contentItem: Text {
                text: option.text
                textFormat: Text.PlainText
                color: option.highlighted ? option.palette.highlightedText : option.palette.text
                font: option.font
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: option.highlighted ? control.palette.highlight : control.palette.base
                border.width: option.visualFocus ? 3 : 0
                border.color: option.highlighted ? control.palette.highlightedText : root.contract.focusColor
            }
        }
        Rectangle {
            objectName: root.controlId + "-focus"
            anchors.fill: parent
            color: "transparent"
            radius: root.contract.radiusSmall
            border.width: 3
            border.color: root.contract.highContrast && control.down ? root.contract.accentTextColor : root.contract.focusColor
            visible: control.activeFocus
            z: 2
            Accessible.ignored: true
        }
    }
}
