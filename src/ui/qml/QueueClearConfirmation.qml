import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

Popup {
    id: root
    objectName: "queue-clear-confirmation"
    required property ShellUiContract contract
    required property int queueCount
    required property Item fallbackFocusItem
    property bool accepted: false
    signal confirmed
    modal: true
    dim: false
    focus: true
    popupType: Popup.Item
    closePolicy: Popup.CloseOnEscape
    padding: contract.spacingUnit * 4

    QueueText {
        id: labels
    }
    onAboutToShow: accepted = false
    onOpened: Qt.callLater(function () {
        cancelButton.forceActiveFocus(Qt.TabFocusReason);
    })
    onClosed: {
        if (!accepted && fallbackFocusItem.enabled && fallbackFocusItem.visible)
            Qt.callLater(function () {
                fallbackFocusItem.forceActiveFocus(Qt.TabFocusReason);
            });
    }
    background: Rectangle {
        color: root.contract.surfaceRaisedColor
        radius: root.contract.radiusMedium
        border.width: 1
        border.color: root.contract.borderColor
    }
    contentItem: ColumnLayout {
        spacing: root.contract.spacingUnit * 3
        Accessible.role: Accessible.Dialog
        Accessible.name: labels.clearTitle

        Text {
            Layout.fillWidth: true
            text: labels.clearTitle
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: root.contract.textPrimaryColor
            font.pixelSize: root.contract.typeTitle
            font.bold: true
            Accessible.ignored: true
        }
        Text {
            objectName: "queue-clear-confirmation-message"
            Layout.fillWidth: true
            text: root.queueCount === 1 ? labels.clearOneMessage : labels.clearManyMessage.replace("{count}", root.queueCount)
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: root.contract.textPrimaryColor
            Accessible.id: objectName
            Accessible.name: text
        }
        RowLayout {
            Layout.fillWidth: true
            Item {
                Layout.fillWidth: true
            }
            AccessibleTransportButton {
                id: cancelButton
                objectName: "queue-clear-cancel"
                controlId: objectName
                contract: root.contract
                text: labels.cancel
                onClicked: root.close()
            }
            AccessibleActionButton {
                objectName: "queue-clear-confirm"
                controlId: objectName
                contract: root.contract
                text: labels.clear
                onClicked: root.confirmed()
            }
        }
    }
}
