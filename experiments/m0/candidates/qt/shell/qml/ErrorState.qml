import QtQuick
import QtQuick.Layouts
import MusicPlayer.M0

ColumnLayout {
    id: root
    objectName: "error-state"
    required property ShellUiContract contract
    required property PlayerPresenter presenter
    Accessible.role: Accessible.AlertMessage
    Accessible.name: contract.errorTitle + (presenter.usesFrozenFixture ? ". " + contract.errorBody : "")
    Text {
        objectName: "error-title"
        text: root.contract.errorTitle
        color: root.contract.textPrimaryColor
        Layout.alignment: Qt.AlignHCenter
    }
    Text {
        objectName: "error-body"
        visible: root.presenter.usesFrozenFixture
        text: root.contract.errorBody
        color: root.contract.textSecondaryColor
        Layout.alignment: Qt.AlignHCenter
    }
    Text {
        objectName: "error-context-title"
        text: root.presenter.contextTitle
        textFormat: Text.PlainText
        elide: Text.ElideRight
        Accessible.name: text
        color: root.contract.textSecondaryColor
        Layout.maximumWidth: 560
        Layout.alignment: Qt.AlignHCenter
    }
    AccessibleActionButton {
        controlId: root.presenter.usesFrozenFixture ? "error-skip" : "error-open-audio"
        contract: root.contract
        text: root.presenter.usesFrozenFixture ? root.contract.actionSkip : root.contract.openAudioText
        Layout.alignment: Qt.AlignHCenter
        onClicked: {
            if (root.presenter.usesFrozenFixture)
                root.presenter.skipUnavailable();
            else
                root.presenter.openAudio();
        }
    }
}
