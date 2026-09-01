import QtQuick
import QtQuick.Layouts
import MusicPlayer.M0

ColumnLayout {
    id: root
    objectName: "error-state"
    width: Math.min(parent ? parent.width : 560, 560)
    required property ShellUiContract contract
    required property PlayerPresenter presenter
    required property ErrorText labels
    property string titleText: presenter.usesFrozenFixture ? contract.errorTitle : labels.titleFor(presenter.errorMessageKey)
    property string bodyText: presenter.usesFrozenFixture ? contract.errorBody : labels.messageFor(presenter.errorMessageKey)
    property string actionText: presenter.usesFrozenFixture ? contract.actionSkip : labels.actionFor(presenter.errorActionId)
    signal detailsRequested(Item opener)
    Accessible.role: Accessible.AlertMessage
    Accessible.name: titleText + (bodyText.length > 0 ? ". " + bodyText : "")
    Text {
        objectName: "error-title"
        text: root.titleText
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        color: root.contract.textPrimaryColor
        Layout.fillWidth: true
        Layout.maximumWidth: 560
        Layout.alignment: Qt.AlignHCenter
    }
    Text {
        objectName: "error-body"
        visible: root.bodyText.length > 0
        text: root.bodyText
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        color: root.contract.textSecondaryColor
        Layout.fillWidth: true
        Layout.maximumWidth: 560
        Layout.alignment: Qt.AlignHCenter
    }
    Text {
        objectName: "error-context-title"
        text: root.presenter.contextTitle
        textFormat: Text.PlainText
        elide: Text.ElideRight
        Accessible.name: text
        color: root.contract.textSecondaryColor
        Layout.fillWidth: true
        Layout.maximumWidth: 560
        Layout.alignment: Qt.AlignHCenter
    }
    AccessibleActionButton {
        controlId: root.presenter.usesFrozenFixture ? "error-skip" : "error-open-audio"
        contract: root.contract
        text: root.actionText
        Layout.fillWidth: true
        Layout.maximumWidth: 560
        Layout.alignment: Qt.AlignHCenter
        onClicked: root.presenter.executeErrorAction()
    }
    AccessibleTransportButton {
        id: detailsButton
        objectName: "track-details-error"
        contract: root.contract
        text: root.contract.detailsTitle
        accessibleName: root.contract.detailsTitle
        enabled: root.presenter.detailsAvailable
        Layout.alignment: Qt.AlignHCenter
        onClicked: root.detailsRequested(detailsButton)
    }
}
