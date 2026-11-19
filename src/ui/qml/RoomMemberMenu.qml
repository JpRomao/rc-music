pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

// S10.1: what the host can change for one person in the room: the role, a per-permission override
// (as the role, allowed or denied) and the way out (kick, or kick and block). The person is shown by
// nickname; the fingerprint is only the key the binding uses.
Popup {
    id: root
    objectName: "room-member-menu"
    required property ShellUiContract contract
    property var room: null
    property string fingerprint: ""
    property string nickname: ""
    property var person: ({})
    property Item opener: null
    readonly property bool available: room !== null && room.available && room.role === 1
    readonly property var overrideChoices: [roomText.overrideFromRole, roomText.overrideAllow, roomText.overrideDeny]
    modal: true
    focus: true
    padding: contract.spacingUnit * 2
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    anchors.centerIn: Overlay.overlay
    width: Math.min(640, parent ? parent.width - 32 : 640)

    RoomText {
        id: roomText
    }

    function openFor(personFingerprint, personNickname, openerItem) {
        fingerprint = personFingerprint;
        nickname = personNickname;
        opener = openerItem;
        reload();
        open();
        Qt.callLater(() => roleChoice.control.forceActiveFocus(Qt.TabFocusReason));
    }
    function reload() {
        person = available ? room.person(fingerprint) : ({});
    }
    // overrides: -1 as the role, 1 allowed, 0 denied <-> choices 0, 1, 2.
    function overrideIndex(permission) {
        const overrides = person.overrides || [];
        const value = permission < overrides.length ? overrides[permission] : -1;
        return value < 0 ? 0 : (value === 1 ? 1 : 2);
    }
    function setOverride(permission, index) {
        room.setOverride(fingerprint, permission, index === 0 ? -1 : (index === 1 ? 1 : 0));
        reload();
    }
    function leaveWith(action) {
        if (action === 1)
            room.block(fingerprint);
        else
            room.kick(fingerprint);
        close();
    }
    onClosed: {
        if (opener !== null && opener.visible)
            opener.forceActiveFocus(Qt.TabFocusReason);
    }

    background: Rectangle {
        color: root.contract.surfaceRaisedColor
        radius: root.contract.radiusMedium
        border.width: 1
        border.color: root.contract.borderColor
    }
    contentItem: ColumnLayout {
        spacing: root.contract.spacingUnit
        Accessible.role: Accessible.Dialog
        Accessible.name: roomText.memberMenuTitle.arg(root.nickname)
        Text {
            Layout.fillWidth: true
            text: roomText.memberMenuTitle.arg(root.nickname)
            textFormat: Text.PlainText
            elide: Text.ElideRight
            font.family: root.contract.headingFontFamily
            font.pixelSize: root.contract.typeSection
            color: root.contract.textPrimaryColor
        }
        PreferenceChoice {
            id: roleChoice
            Layout.fillWidth: true
            contract: root.contract
            controlId: "room-member-role"
            label: roomText.memberRole.arg(root.nickname)
            options: root.available ? root.room.roles : null
            textRole: "name"
            valueRole: "roleId"
            selected: roleChoice.control.count > 0 && root.person.known === true ? roleChoice.control.indexOfValue(root.person.roleId) : -1
            onValueActivated: index => {
                root.room.assignRole(root.fingerprint, roleChoice.control.valueAt(index));
                root.reload();
            }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: root.contract.spacingUnit
            rowSpacing: root.contract.spacingUnit
            Repeater {
                model: root.available ? root.room.permissionCount : 0
                delegate: PreferenceChoice {
                    id: overrideChoice
                    required property int index
                    Layout.fillWidth: true
                    contract: root.contract
                    controlId: "room-member-override-" + index
                    label: roomText.overrideName.arg(roomText.permissionName(index)).arg(root.nickname)
                    options: root.overrideChoices
                    selected: root.overrideIndex(overrideChoice.index)
                    onValueActivated: choice => root.setOverride(overrideChoice.index, choice)
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: root.contract.spacingUnit
            AccessibleTransportButton {
                objectName: "room-member-kick"
                contract: root.contract
                text: roomText.kick
                accessibleName: roomText.kickName.arg(root.nickname)
                onClicked: root.leaveWith(0)
            }
            AccessibleTransportButton {
                objectName: "room-member-block"
                contract: root.contract
                text: roomText.block
                accessibleName: roomText.blockName.arg(root.nickname)
                onClicked: root.leaveWith(1)
            }
            Item {
                Layout.fillWidth: true
            }
            AccessibleTransportButton {
                objectName: "room-member-close"
                contract: root.contract
                flat: true
                text: roomText.closeMenu
                onClicked: root.close()
            }
        }
    }
}
