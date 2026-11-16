import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

// Two-button consent shared by the Activity page (archive, import, discard,
// close without saving). The owner keeps the consent state and focus return.
Popup {
    id: confirmation
    required property ShellUiContract contract
    required property Item host
    property string title: ""
    property string advice: ""
    property string adviceObjectName: ""
    property string cancelText: ""
    property string confirmText: ""
    property string cancelObjectName: ""
    property string confirmObjectName: ""
    property bool confirmEnabled: true
    signal cancelRequested
    signal confirmRequested

    closePolicy: Popup.CloseOnEscape
    dim: false
    focus: true
    height: content.implicitHeight + padding * 2
    modal: true
    padding: contract.spacingUnit * 3
    parent: host.parent
    popupType: Popup.Item
    width: Math.min(host.width, 560)
    x: host.x + (host.width - width) / 2
    y: host.y + (host.height - height) / 2

    background: Rectangle {
        border.color: confirmation.contract.borderColor
        border.width: confirmation.contract.highContrast ? 2 : 1
        color: confirmation.contract.surfaceRaisedColor
        radius: confirmation.contract.radiusMedium
    }
    contentItem: ColumnLayout {
        id: content

        Accessible.name: confirmation.title
        Accessible.role: Accessible.Dialog
        spacing: confirmation.contract.spacingUnit * 3

        Text {
            Layout.fillWidth: true
            color: confirmation.contract.textPrimaryColor
            font.bold: true
            text: confirmation.title
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
        }
        Text {
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            Layout.fillWidth: true
            color: confirmation.contract.textPrimaryColor
            objectName: confirmation.adviceObjectName
            text: confirmation.advice
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true

            AccessibleTransportButton {
                id: cancelButton

                KeyNavigation.backtab: confirmButton
                KeyNavigation.tab: confirmButton
                contract: confirmation.contract
                objectName: confirmation.cancelObjectName
                text: confirmation.cancelText

                onClicked: confirmation.cancelRequested()
            }
            Item {
                Layout.fillWidth: true
            }
            AccessibleTransportButton {
                id: confirmButton

                KeyNavigation.backtab: cancelButton
                KeyNavigation.tab: cancelButton
                contract: confirmation.contract
                enabled: confirmation.confirmEnabled
                objectName: confirmation.confirmObjectName
                text: confirmation.confirmText

                onClicked: confirmation.confirmRequested()
            }
        }
    }
    onOpened: cancelButton.forceActiveFocus(Qt.TabFocusReason)
}
