pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

// First launch: asks for the nickname and shows the drawn avatar, which can be changed here.
// It cannot be dismissed; it closes once the profile is saved.
Popup {
    id: root
    objectName: "profile-setup-dialog"
    required property ShellUiContract contract
    property var profile: null
    property Item fallbackFocusItem: null
    property bool saveFailed: false
    readonly property bool draftValid: profile !== null && profile.acceptsNickname(nicknameField.text)
    readonly property bool draftInvalid: nicknameField.text.length > 0 && !draftValid
    modal: true
    dim: true
    focus: true
    popupType: Popup.Item
    closePolicy: Popup.NoAutoClose
    padding: contract.spacingUnit * 3

    function submit() {
        if (!draftValid)
            return;
        saveFailed = !profile.setNickname(nicknameField.text);
    }

    ProfileText {
        id: labels
    }
    onAboutToShow: {
        saveFailed = false;
        nicknameField.text = profile !== null ? profile.nickname : "";
    }
    onOpened: Qt.callLater(function () {
        nicknameField.forceActiveFocus(Qt.TabFocusReason);
    })
    onClosed: {
        if (fallbackFocusItem && fallbackFocusItem.enabled && fallbackFocusItem.visible)
            Qt.callLater(function () {
                root.fallbackFocusItem.forceActiveFocus(Qt.TabFocusReason);
            });
    }
    Overlay.modal: Rectangle {
        color: root.contract.scrimColor
    }
    background: Rectangle {
        color: root.contract.surfaceRaisedColor
        radius: root.contract.radiusMedium
        border.width: 1
        border.color: root.contract.borderColor
    }
    contentItem: ColumnLayout {
        spacing: root.contract.spacingUnit * 2
        Accessible.role: Accessible.Dialog
        Accessible.name: labels.setupTitle
        Accessible.description: labels.setupMessage

        Text {
            Layout.fillWidth: true
            text: labels.setupTitle
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: root.contract.textPrimaryColor
            font.family: root.contract.headingFontFamily
            font.pixelSize: root.contract.typeTitle
            font.bold: true
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }
        Text {
            Layout.fillWidth: true
            text: labels.setupMessage
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: root.contract.textSecondaryColor
            Accessible.ignored: true
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: root.contract.spacingUnit * 2
            ListenerAvatar {
                objectName: "profile-current-avatar"
                size: 64
                source: root.profile !== null ? root.profile.avatarSource : ""
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: root.contract.spacingUnit
                Text {
                    Layout.fillWidth: true
                    text: labels.nickname
                    textFormat: Text.PlainText
                    color: root.contract.textPrimaryColor
                    Accessible.ignored: true
                }
                TextField {
                    id: nicknameField
                    objectName: "profile-nickname"
                    Layout.fillWidth: true
                    implicitHeight: root.contract.controlTargetPreferred
                    maximumLength: 128
                    selectByMouse: true
                    activeFocusOnTab: true
                    color: root.contract.textPrimaryColor
                    selectionColor: root.contract.accentColor
                    selectedTextColor: root.contract.accentTextColor
                    Accessible.role: Accessible.EditableText
                    Accessible.id: objectName
                    Accessible.name: labels.nickname
                    Accessible.description: nicknameError.visible ? nicknameError.text : ""
                    Accessible.focusable: true
                    Accessible.focused: activeFocus
                    onTextEdited: root.saveFailed = false
                    onAccepted: root.submit()
                    background: Rectangle {
                        color: root.contract.surfaceColor
                        radius: root.contract.radiusSmall
                        border.width: nicknameField.activeFocus ? 3 : 1
                        border.color: nicknameField.activeFocus ? root.contract.focusColor : (root.draftInvalid ? root.contract.errorColor : root.contract.borderColor)
                    }
                }
            }
        }
        Text {
            id: nicknameError
            objectName: "profile-nickname-error"
            Layout.fillWidth: true
            visible: root.draftInvalid || root.saveFailed
            text: root.saveFailed ? labels.saveFailed : labels.nicknameInvalid
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            font.pixelSize: root.contract.typeCaption
            color: root.contract.errorColor
            Accessible.role: Accessible.AlertMessage
            Accessible.id: objectName
            Accessible.name: text
        }
        Text {
            Layout.fillWidth: true
            text: labels.avatar
            textFormat: Text.PlainText
            color: root.contract.textPrimaryColor
            Accessible.ignored: true
        }
        AvatarPicker {
            objectName: "profile-avatar-picker"
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: implicitWidth
            Layout.preferredHeight: implicitHeight
            contract: root.contract
            avatarCount: root.profile !== null ? root.profile.avatarCount : 0
            selectedIndex: root.profile !== null ? root.profile.avatarIndex : -1
            onAvatarChosen: index => {
                if (root.profile !== null)
                    root.profile.chooseBundledAvatar(index);
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item {
                Layout.fillWidth: true
            }
            AccessibleActionButton {
                objectName: "profile-continue"
                controlId: objectName
                contract: root.contract
                text: labels.continueLabel
                enabled: root.draftValid
                onClicked: root.submit()
            }
        }
    }
}
