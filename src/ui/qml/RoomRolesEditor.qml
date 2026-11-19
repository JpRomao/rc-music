pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

// S10.1: the host's roles. Each role has a name and one check box per permission; the invite's initial
// role is chosen here and cannot be removed. Everything works from the keyboard (Tab, Space, Enter).
ColumnLayout {
    id: root
    required property ShellUiContract contract
    property var room: null
    readonly property bool available: room !== null && room.available && room.role === 1
    property string message: ""
    spacing: contract.spacingUnit * 2

    RoomText {
        id: roomText
    }

    function say(text) {
        message = text;
    }
    function createRole() {
        const name = newRoleName.text.trim();
        if (!available || name.length === 0)
            return;
        if (room.createRole(name, 0) > 0) {
            newRoleName.text = "";
            say("");
        } else {
            say(roomText.roleRefused);
        }
    }
    function removeRole(roleId) {
        const result = room.removeRole(roleId);
        if (result === 1)
            say(roomText.chooseAnotherInitialRole);
        else if (result === 2)
            say(roomText.roleRefused);
        else
            say("");
    }

    Text {
        text: roomText.rolesTitle
        textFormat: Text.PlainText
        font.family: root.contract.headingFontFamily
        font.pixelSize: root.contract.typeSection
        color: root.contract.textPrimaryColor
    }
    Text {
        Layout.fillWidth: true
        text: roomText.rolesHint
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        font.family: root.contract.bodyFontFamily
        font.pixelSize: root.contract.typeCaption
        color: root.contract.textSecondaryColor
    }
    Text {
        objectName: "room-roles-message"
        Layout.fillWidth: true
        visible: root.message.length > 0
        text: root.message
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        font.family: root.contract.bodyFontFamily
        font.pixelSize: root.contract.typeBody
        color: root.contract.errorColor
        Accessible.role: Accessible.AlertMessage
        Accessible.name: text
    }
    Text {
        id: rosterUnsavedNotice
        objectName: "room-roster-unsaved"
        Layout.fillWidth: true
        visible: root.available && root.room.rosterUnsaved === true
        onVisibleChanged: {
            if (visible)
                rosterUnsavedNotice.Accessible.announce(text, Accessible.Assertive);
        }
        text: roomText.rosterUnsaved
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        font.family: root.contract.bodyFontFamily
        font.pixelSize: root.contract.typeBody
        color: root.contract.errorColor
        Accessible.role: Accessible.AlertMessage
        Accessible.name: text
    }

    PreferenceChoice {
        id: initialRoleChoice
        Layout.fillWidth: true
        Layout.maximumWidth: 480
        contract: root.contract
        controlId: "room-initial-role"
        label: roomText.initialRole
        options: root.available ? root.room.roles : null
        textRole: "name"
        valueRole: "roleId"
        selected: root.available && initialRoleChoice.control.count > 0 ? initialRoleChoice.control.indexOfValue(root.room.initialRoleId) : -1
        onValueActivated: index => {
            if (root.room.setInitialRole(initialRoleChoice.control.valueAt(index)))
                root.say("");
        }
    }

    Repeater {
        model: root.available ? root.room.roles : null
        delegate: Rectangle {
            id: roleCard
            required property int roleId
            required property string name
            required property int permissions
            required property bool initial
            required property int holders
            objectName: "room-role-" + roleId
            Layout.fillWidth: true
            implicitHeight: roleColumn.implicitHeight + root.contract.spacingUnit * 2
            radius: root.contract.radiusMedium
            color: root.contract.surfaceRaisedColor
            border.width: 1
            border.color: root.contract.borderColor
            ColumnLayout {
                id: roleColumn
                anchors.fill: parent
                anchors.margins: root.contract.spacingUnit
                spacing: root.contract.spacingUnit
                RowLayout {
                    Layout.fillWidth: true
                    spacing: root.contract.spacingUnit
                    TextField {
                        id: nameField
                        objectName: "room-role-name-" + roleCard.roleId
                        Layout.fillWidth: true
                        text: roleCard.name
                        maximumLength: 64
                        font.family: root.contract.bodyFontFamily
                        font.pixelSize: root.contract.typeBody
                        Accessible.name: roomText.roleNameField.arg(roleCard.name)
                        function commit() {
                            const next = text.trim();
                            if (next === roleCard.name)
                                return;
                            if (root.room.renameRole(roleCard.roleId, next)) {
                                root.say("");
                            } else {
                                root.say(roomText.roleRefused);
                                text = roleCard.name;
                            }
                        }
                        onAccepted: commit()
                        onEditingFinished: commit()
                    }
                    Text {
                        visible: roleCard.initial
                        text: roomText.initialTag
                        textFormat: Text.PlainText
                        font.family: root.contract.bodyFontFamily
                        font.pixelSize: root.contract.typeCaption
                        color: root.contract.textSecondaryColor
                    }
                    Text {
                        text: roomText.roleHolders.arg(roleCard.holders)
                        textFormat: Text.PlainText
                        font.family: root.contract.bodyFontFamily
                        font.pixelSize: root.contract.typeCaption
                        color: root.contract.textSecondaryColor
                    }
                    AccessibleTransportButton {
                        objectName: "room-role-remove-" + roleCard.roleId
                        contract: root.contract
                        flat: true
                        text: roomText.removeRole
                        accessibleName: roomText.removeRoleName.arg(roleCard.name)
                        onClicked: root.removeRole(roleCard.roleId)
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: root.contract.spacingUnit
                    Repeater {
                        model: root.available ? root.room.permissionCount : 0
                        delegate: PreferenceToggle {
                            id: permissionBox
                            required property int index
                            contract: root.contract
                            controlId: "room-role-" + roleCard.roleId + "-permission-" + index
                            text: roomText.permissionName(index)
                            value: (roleCard.permissions & (1 << index)) !== 0
                            Accessible.name: roomText.permissionForRole.arg(roomText.permissionName(index)).arg(roleCard.name)
                            onValueEdited: checked => {
                                root.room.setRolePermission(roleCard.roleId, permissionBox.index, checked);
                                // The role decides what is shown, refused or not.
                                permissionBox.checked = Qt.binding(() => permissionBox.value);
                            }
                        }
                    }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.maximumWidth: 640
        spacing: root.contract.spacingUnit
        TextField {
            id: newRoleName
            objectName: "room-new-role-name"
            Layout.fillWidth: true
            maximumLength: 64
            placeholderText: roomText.newRolePlaceholder
            font.family: root.contract.bodyFontFamily
            font.pixelSize: root.contract.typeBody
            Accessible.name: roomText.newRoleName
            onAccepted: root.createRole()
        }
        AccessibleTransportButton {
            objectName: "room-create-role"
            contract: root.contract
            enabled: newRoleName.text.trim().length > 0
            text: roomText.createRole
            onClicked: root.createRole()
        }
    }
}
