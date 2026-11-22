import QtQuick
import MusicPlayer.M0

// Confirmacao Aurora: pergunta, consequencia, Cancelar e confirmar.
// O foco abre em Cancelar; confirmar emite o sinal e fecha.
AuroraDialog {
    id: dialog

    required property string question
    required property string consequence
    required property string confirmText
    property bool destructive: false

    signal confirmed

    closable: false
    title: question

    onOpened: cancelButton.forceActiveFocus()

    Text {
        objectName: "confirm-consequence"
        width: parent ? parent.width : implicitWidth
        text: dialog.consequence
        color: dialog.contract.textPrimaryColor
        font.pixelSize: dialog.contract.typeBody
        wrapMode: Text.Wrap
    }

    actions: [
        AuroraButton {
            id: cancelButton
            contract: dialog.contract
            controlId: dialog.controlId + "-cancel"
            kind: AuroraButton.Quiet
            text: confirmControls.cancel
            onClicked: dialog.close()
        },
        AuroraButton {
            contract: dialog.contract
            controlId: dialog.controlId + "-confirm"
            kind: dialog.destructive ? AuroraButton.DangerFilled : AuroraButton.Primary
            text: dialog.confirmText
            onClicked: {
                dialog.confirmed();
                dialog.close();
            }
        }
    ]

    ControlsText {
        id: confirmControls
    }
}
