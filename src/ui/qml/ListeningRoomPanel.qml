pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

// ADR-0022 (S7.3): the listen-together page. Hosts a room over the local queue or joins one by invite link.
// The page only shows the binding's state and forwards choices; the room rules live in C++.
PageSurface {
    id: root
    NavigationText {
        id: navigationLabels
    }
    RoomText {
        id: roomText
    }
    objectName: "room-panel"
    required property ShellUiContract contract
    property var room: null
    // S9.1: the output offset is a preference of the player.
    property var presenter: null
    readonly property bool available: room !== null && room.available
    readonly property int role: available ? room.role : 0
    readonly property bool busy: available && room.busy
    // Steps outside a room: 0 start, 1 privacy notice, 2 host form, 3 join form.
    readonly property int startStep: 0
    readonly property int noticeStep: 1
    readonly property int hostStep: 2
    readonly property int joinStep: 3
    property int step: startStep
    property int stepAfterNotice: hostStep
    property int capacityIndex: available ? room.defaultListeners - room.minimumListeners : 4
    property int durationIndex: 0
    property bool linkCopied: false
    // S10.1: the per-person menu holds the keyboard while it is open.
    readonly property bool blockingUiOpen: memberMenu.opened
    // S11.2: a guest's view of the host's playlists visible to the room.
    readonly property var catalog: available && role === 2 ? room.catalog : null
    readonly property bool hasCatalog: !!catalog && catalog.count > 0
    embedded: true
    padding: 0
    focus: true
    onSurfaceOpened: {
        if (role === 0)
            showStep(startStep);
        Qt.callLater(root.focusFirst);
    }
    onRoleChanged: {
        linkCopied = false;
        if (role === 0)
            showStep(startStep);
        if (visible)
            Qt.callLater(root.focusFirst);
    }

    function capacityOptions() {
        if (!available)
            return [];
        const options = [];
        for (let count = room.minimumListeners; count <= room.maximumListeners; ++count)
            options.push(count === 1 ? roomText.onePerson : roomText.people.arg(count));
        return options;
    }
    function focusFirst() {
        if (!visible)
            return;
        let target = null;
        if (!available)
            target = null;
        else if (role === 1)
            target = copyButton;
        else if (role === 2)
            target = leaveButton;
        else if (step === noticeStep)
            target = privacyAccept;
        else if (step === hostStep)
            target = capacityPreference.control;
        else if (step === joinStep)
            target = linkField;
        else
            target = createButton;
        if (target !== null && target.visible)
            target.forceActiveFocus(Qt.TabFocusReason);
    }
    function showStep(next) {
        step = next;
        if (visible)
            Qt.callLater(root.focusFirst);
    }
    function begin(next) {
        if (!available || busy)
            return;
        room.dismissError();
        if (room.privacyNoticeRequired) {
            stepAfterNotice = next;
            showStep(noticeStep);
        } else {
            showStep(next);
        }
    }
    function acknowledgeNotice() {
        room.acknowledgePrivacyNotice();
        showStep(stepAfterNotice);
    }
    function openRoom() {
        if (available && !busy)
            room.openRoom(room.minimumListeners + capacityIndex, durationIndex === 1);
    }
    function joinRoom() {
        if (available && !busy && linkField.text.trim().length > 0)
            room.join(linkField.text.trim());
    }
    function guestStatus() {
        if (!available)
            return "";
        if (room.state === "connecting")
            return roomText.connecting;
        if (room.state === "waiting")
            return roomText.waiting;
        if (room.state === "reconnecting")
            return roomText.reconnecting;
        return room.hostNickname.length > 0 ? roomText.joined.arg(room.hostNickname) : roomText.joinedUnnamed;
    }

    background: Rectangle {
        color: root.contract.canvasColor
    }
    contentItem: ColumnLayout {
        spacing: root.contract.spacingUnit * 2
        Accessible.role: Accessible.Pane
        Accessible.name: navigationLabels.listenTogether

        PageHeader {
            Layout.fillWidth: true
            contract: root.contract
            editorial: true
            eyebrow: navigationLabels.listenTogetherEyebrow
            title: navigationLabels.listenTogether
            description: navigationLabels.listenTogetherDescription
            legacyTitleSize: root.contract.typeSection
        }

        Text {
            objectName: "room-unavailable"
            Layout.fillWidth: true
            visible: !root.available
            text: roomText.unavailable
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            font.family: root.contract.bodyFontFamily
            font.pixelSize: root.contract.typeBody
            color: root.contract.textPrimaryColor
        }
        Text {
            Layout.fillWidth: true
            visible: !root.available
            text: roomText.unavailableHint
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            font.family: root.contract.bodyFontFamily
            font.pixelSize: root.contract.typeCaption
            color: root.contract.textSecondaryColor
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.available && root.room.errorText.length > 0
            spacing: root.contract.spacingUnit
            Text {
                objectName: "room-error"
                Layout.fillWidth: true
                text: root.available ? root.room.errorText : ""
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeBody
                color: root.contract.errorColor
                Accessible.role: Accessible.AlertMessage
                Accessible.name: text
            }
            AccessibleTransportButton {
                objectName: "room-error-dismiss"
                contract: root.contract
                flat: true
                iconName: "close"
                text: roomText.dismissError
                onClicked: root.room.dismissError()
            }
        }

        Text {
            objectName: "room-preparing"
            Layout.fillWidth: true
            visible: root.busy
            text: roomText.preparing
            textFormat: Text.PlainText
            font.family: root.contract.bodyFontFamily
            font.pixelSize: root.contract.typeBody
            color: root.contract.textSecondaryColor
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        // Outside a room: create or join.
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.available && root.role === 0 && root.step === root.startStep
            spacing: root.contract.spacingUnit * 2
            Text {
                Layout.fillWidth: true
                text: roomText.idleHint
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeBody
                color: root.contract.textSecondaryColor
            }
            RowLayout {
                spacing: root.contract.spacingUnit
                AccessibleTransportButton {
                    id: createButton
                    objectName: "room-create"
                    contract: root.contract
                    primary: true
                    iconName: "people"
                    iconWithText: true
                    enabled: !root.busy
                    text: roomText.create
                    onClicked: root.begin(root.hostStep)
                }
                AccessibleTransportButton {
                    objectName: "room-join-start"
                    contract: root.contract
                    iconName: "arrow"
                    iconWithText: true
                    enabled: !root.busy
                    text: roomText.joinStart
                    onClicked: root.begin(root.joinStep)
                }
            }
        }

        // FR-058: the one-time notice before the first room.
        Rectangle {
            Layout.fillWidth: true
            Layout.maximumWidth: 640
            visible: root.available && root.role === 0 && root.step === root.noticeStep
            implicitHeight: noticeColumn.implicitHeight + root.contract.spacingUnit * 4
            radius: root.contract.radiusMedium
            color: root.contract.surfaceRaisedColor
            border.width: 1
            border.color: root.contract.borderColor
            ColumnLayout {
                id: noticeColumn
                anchors.fill: parent
                anchors.margins: root.contract.spacingUnit * 2
                spacing: root.contract.spacingUnit * 2
                Text {
                    Layout.fillWidth: true
                    text: roomText.privacyTitle
                    textFormat: Text.PlainText
                    font.family: root.contract.headingFontFamily
                    font.pixelSize: root.contract.typeSection
                    color: root.contract.textPrimaryColor
                }
                Text {
                    objectName: "room-privacy-notice"
                    Layout.fillWidth: true
                    text: roomText.privacyNotice
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    font.family: root.contract.bodyFontFamily
                    font.pixelSize: root.contract.typeBody
                    color: root.contract.textPrimaryColor
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
                RowLayout {
                    spacing: root.contract.spacingUnit
                    AccessibleTransportButton {
                        id: privacyAccept
                        objectName: "room-privacy-accept"
                        contract: root.contract
                        primary: true
                        text: roomText.privacyAccept
                        onClicked: root.acknowledgeNotice()
                    }
                    AccessibleTransportButton {
                        objectName: "room-privacy-back"
                        contract: root.contract
                        flat: true
                        text: roomText.back
                        onClicked: root.showStep(root.startStep)
                    }
                }
            }
        }

        // Host form: how many people and how long the invite lasts.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.maximumWidth: 480
            visible: root.available && root.role === 0 && root.step === root.hostStep
            spacing: root.contract.spacingUnit * 2
            PreferenceChoice {
                id: capacityPreference
                Layout.fillWidth: true
                contract: root.contract
                controlId: "room-capacity"
                label: roomText.capacity
                options: root.capacityOptions()
                selected: root.capacityIndex
                onValueActivated: index => root.capacityIndex = index
            }
            PreferenceChoice {
                Layout.fillWidth: true
                contract: root.contract
                controlId: "room-duration"
                label: roomText.duration
                options: [roomText.untilAppCloses, roomText.untilRevoked]
                selected: root.durationIndex
                onValueActivated: index => root.durationIndex = index
            }
            RowLayout {
                spacing: root.contract.spacingUnit
                AccessibleTransportButton {
                    objectName: "room-open"
                    contract: root.contract
                    primary: true
                    enabled: !root.busy
                    text: roomText.open
                    onClicked: root.openRoom()
                }
                AccessibleTransportButton {
                    objectName: "room-back"
                    contract: root.contract
                    flat: true
                    text: roomText.back
                    onClicked: root.showStep(root.startStep)
                }
            }
        }

        // Join form: paste the invite.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.maximumWidth: 640
            visible: root.available && root.role === 0 && root.step === root.joinStep
            spacing: root.contract.spacingUnit
            Text {
                Layout.fillWidth: true
                text: roomText.linkField
                textFormat: Text.PlainText
                color: root.contract.textPrimaryColor
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: root.contract.spacingUnit
                TextField {
                    id: linkField
                    objectName: "room-link-field"
                    Layout.fillWidth: true
                    enabled: !root.busy
                    maximumLength: 2048
                    placeholderText: roomText.linkPlaceholder
                    font.family: root.contract.bodyFontFamily
                    font.pixelSize: root.contract.typeBody
                    Accessible.name: roomText.linkField
                    onAccepted: root.joinRoom()
                }
                AccessibleTransportButton {
                    objectName: "room-join"
                    contract: root.contract
                    primary: true
                    enabled: !root.busy && linkField.text.trim().length > 0
                    text: roomText.join
                    onClicked: root.joinRoom()
                }
                AccessibleTransportButton {
                    objectName: "room-join-back"
                    contract: root.contract
                    flat: true
                    text: roomText.back
                    onClicked: root.showStep(root.startStep)
                }
            }
        }

        // Hosting: the invite, who is in and who asks to come in.
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.available && root.role === 1
            spacing: root.contract.spacingUnit * 2
            Text {
                text: roomText.inviteTitle
                textFormat: Text.PlainText
                font.family: root.contract.headingFontFamily
                font.pixelSize: root.contract.typeSection
                color: root.contract.textPrimaryColor
            }
            Text {
                objectName: "room-invite-link"
                Layout.fillWidth: true
                text: root.available ? root.room.inviteLink : ""
                textFormat: Text.PlainText
                wrapMode: Text.WrapAnywhere
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeBody
                color: root.contract.textPrimaryColor
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            Text {
                Layout.fillWidth: true
                text: root.available ? roomText.hostCapacity.arg(root.room.maxListeners) : ""
                textFormat: Text.PlainText
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeCaption
                color: root.contract.textSecondaryColor
            }
            RowLayout {
                spacing: root.contract.spacingUnit
                AccessibleTransportButton {
                    id: copyButton
                    objectName: "room-copy-link"
                    contract: root.contract
                    primary: true
                    text: roomText.copyLink
                    onClicked: root.linkCopied = root.room.copyLink()
                }
                AccessibleTransportButton {
                    objectName: "room-revoke"
                    contract: root.contract
                    visible: root.available && root.room.untilRevoked
                    text: roomText.revoke
                    onClicked: root.room.revoke()
                }
                AccessibleTransportButton {
                    objectName: "room-close"
                    contract: root.contract
                    flat: true
                    iconName: "close"
                    iconWithText: true
                    text: roomText.close
                    onClicked: root.room.closeRoom()
                }
            }
            Text {
                objectName: "room-link-copied"
                visible: root.linkCopied
                text: roomText.linkCopied
                textFormat: Text.PlainText
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeCaption
                color: root.contract.textSecondaryColor
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            Text {
                visible: root.available && root.room.pendingRequests.count > 0
                text: roomText.requestsTitle
                textFormat: Text.PlainText
                font.family: root.contract.headingFontFamily
                font.pixelSize: root.contract.typeSection
                color: root.contract.textPrimaryColor
            }
            Repeater {
                model: root.available ? root.room.pendingRequests : null
                delegate: RowLayout {
                    id: request
                    required property string fingerprint
                    required property string nickname
                    required property url avatarSource
                    Layout.fillWidth: true
                    spacing: root.contract.spacingUnit
                    ListenerAvatar {
                        source: request.avatarSource
                        size: 32
                    }
                    Text {
                        objectName: "room-request-name"
                        Layout.fillWidth: true
                        text: roomText.requestName.arg(request.nickname)
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        font.family: root.contract.bodyFontFamily
                        font.pixelSize: root.contract.typeBody
                        color: root.contract.textPrimaryColor
                    }
                    AccessibleTransportButton {
                        objectName: "room-approve"
                        contract: root.contract
                        primary: true
                        text: roomText.approve
                        accessibleName: roomText.approveName.arg(request.nickname)
                        onClicked: root.room.approve(request.fingerprint)
                    }
                    AccessibleTransportButton {
                        objectName: "room-reject"
                        contract: root.contract
                        text: roomText.reject
                        accessibleName: roomText.rejectName.arg(request.nickname)
                        onClicked: root.room.reject(request.fingerprint)
                    }
                }
            }

            // S10.1: roles, then everyone the host knows (blocked people can be let back in).
            RoomRolesEditor {
                objectName: "room-roles-editor"
                Layout.fillWidth: true
                contract: root.contract
                room: root.room
            }
            Text {
                visible: root.available && root.room.knownPeople.count > 0
                text: roomText.knownTitle
                textFormat: Text.PlainText
                font.family: root.contract.headingFontFamily
                font.pixelSize: root.contract.typeSection
                color: root.contract.textPrimaryColor
            }
            Repeater {
                model: root.available && root.role === 1 ? root.room.knownPeople : null
                delegate: RowLayout {
                    id: known
                    required property string fingerprint
                    required property string nickname
                    required property url avatarSource
                    required property string roleName
                    required property bool blocked
                    Layout.fillWidth: true
                    spacing: root.contract.spacingUnit
                    Accessible.role: Accessible.ListItem
                    Accessible.name: knownName.text
                    Accessible.description: roomText.knownRole.arg(known.roleName)
                    ListenerAvatar {
                        source: known.avatarSource
                        size: 32
                    }
                    Text {
                        id: knownName
                        objectName: "room-known-name"
                        Layout.fillWidth: true
                        text: known.nickname + (known.blocked ? " · " + roomText.blockedTag : "")
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        font.family: root.contract.bodyFontFamily
                        font.pixelSize: root.contract.typeBody
                        color: root.contract.textPrimaryColor
                    }
                    Text {
                        text: roomText.knownRole.arg(known.roleName)
                        textFormat: Text.PlainText
                        font.family: root.contract.bodyFontFamily
                        font.pixelSize: root.contract.typeCaption
                        color: root.contract.textSecondaryColor
                    }
                    AccessibleTransportButton {
                        objectName: "room-unblock"
                        visible: known.blocked
                        contract: root.contract
                        text: roomText.unblock
                        accessibleName: roomText.unblockName.arg(known.nickname)
                        onClicked: root.room.unblock(known.fingerprint)
                    }
                }
            }
        }

        // A guest: how the room is going, and the way out.
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.available && root.role === 2
            spacing: root.contract.spacingUnit * 2
            Text {
                objectName: "room-guest-status"
                Layout.fillWidth: true
                text: root.guestStatus()
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeBody
                color: root.contract.textPrimaryColor
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            // S12.1: one of the guest's own audio files to the room's queue.
            AccessibleTransportButton {
                objectName: "room-upload-own"
                visible: root.available && root.room.canUploadOwnTracks
                contract: root.contract
                iconName: "plus"
                iconWithText: true
                text: roomText.uploadOwn
                accessibleName: roomText.uploadOwnName
                onClicked: root.room.roomUploadOwnTrack()
            }
            Text {
                objectName: "room-uploading"
                Layout.fillWidth: true
                visible: root.available && root.room.uploading
                text: roomText.uploading
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeCaption
                color: root.contract.textSecondaryColor
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            AccessibleTransportButton {
                id: leaveButton
                objectName: "room-leave"
                contract: root.contract
                iconName: "close"
                iconWithText: true
                text: roomText.leave
                onClicked: root.room.leave()
            }
            RoomOutputOffsetField {
                Layout.fillWidth: true
                visible: root.presenter !== null
                contract: root.contract
                controlId: "room-guest-output-offset"
                label: roomText.outputOffset
                hint: roomText.outputOffsetHint
                valueFormat: roomText.outputOffsetValue
                value: root.presenter !== null ? root.presenter.roomOutputOffsetMs : 0
                minimum: root.presenter !== null ? root.presenter.roomOutputOffsetMinimumMs : 0
                maximum: root.presenter !== null ? root.presenter.roomOutputOffsetMaximumMs : 0
                onValueEdited: value => root.presenter.setRoomOutputOffsetMs(value)
            }
        }

        // Everyone in the room, host first as the room sends it.
        Text {
            visible: root.available && root.role !== 0 && root.room.members.count > 0
            text: roomText.membersTitle
            textFormat: Text.PlainText
            font.family: root.contract.headingFontFamily
            font.pixelSize: root.contract.typeSection
            color: root.contract.textPrimaryColor
        }
        Repeater {
            model: root.available && root.role !== 0 ? root.room.members : null
            delegate: RowLayout {
                id: member
                required property string fingerprint
                required property string nickname
                required property url avatarSource
                required property bool isHost
                required property bool isSelf
                required property bool hasSyncDrift
                required property int syncDriftMs
                Layout.fillWidth: true
                spacing: root.contract.spacingUnit
                Accessible.role: Accessible.ListItem
                Accessible.name: memberName.text
                Accessible.description: member.hasSyncDrift ? memberDrift.text : ""
                ListenerAvatar {
                    source: member.avatarSource
                    size: 32
                }
                Text {
                    id: memberName
                    objectName: "room-member-name"
                    Layout.fillWidth: true
                    text: member.nickname + (member.isHost ? " · " + roomText.hostTag : "") + (member.isSelf ? " · " + roomText.selfTag : "")
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    font.family: root.contract.bodyFontFamily
                    font.pixelSize: root.contract.typeBody
                    color: root.contract.textPrimaryColor
                }
                Text {
                    id: memberDrift
                    objectName: "room-member-sync-drift"
                    visible: member.hasSyncDrift
                    text: roomText.syncDrift.arg(member.syncDriftMs > 0 ? "+" + member.syncDriftMs : member.syncDriftMs)
                    textFormat: Text.PlainText
                    font.family: root.contract.bodyFontFamily
                    font.pixelSize: root.contract.typeBody
                    color: root.contract.textSecondaryColor
                }
                AccessibleTransportButton {
                    id: memberActions
                    objectName: "room-member-actions"
                    visible: root.role === 1 && !member.isHost && !member.isSelf
                    contract: root.contract
                    flat: true
                    text: roomText.memberActions
                    accessibleName: roomText.memberActionsName.arg(member.nickname)
                    onClicked: memberMenu.openFor(member.fingerprint, member.nickname, memberActions)
                }
            }
        }

        // S11.2, a guest: the host's playlists visible to the room, to add one track or a whole list.
        Text {
            objectName: "room-catalog-title"
            visible: root.hasCatalog
            text: roomText.catalogTitle
            textFormat: Text.PlainText
            font.family: root.contract.headingFontFamily
            font.pixelSize: root.contract.typeSection
            color: root.contract.textPrimaryColor
        }
        Text {
            objectName: "room-catalog-no-permission"
            Layout.fillWidth: true
            visible: root.hasCatalog && !root.room.canAddFromRoom && !root.room.canAddPlaylist
            text: roomText.catalogNoPermission
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            font.family: root.contract.bodyFontFamily
            font.pixelSize: root.contract.typeCaption
            color: root.contract.textSecondaryColor
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        // One row per playlist, then one per track: rows the catalog keeps stay, with their keyboard focus.
        Repeater {
            model: root.catalog
            delegate: Loader {
                id: catalogRow
                required property string kind
                required property string playlistId
                required property string entryId
                required property string name
                required property string title
                required property string artist
                required property int entryCount
                readonly property string shownTitle: title.length > 0 ? title : roomText.untitled
                Layout.fillWidth: true
                sourceComponent: kind === "playlist" ? catalogPlaylist : catalogEntry

                Component {
                    id: catalogPlaylist
                    RowLayout {
                        spacing: root.contract.spacingUnit
                        Text {
                            objectName: "room-catalog-playlist-name"
                            Layout.fillWidth: true
                            text: catalogRow.name
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            font.family: root.contract.bodyFontFamily
                            font.pixelSize: root.contract.typeBody
                            font.weight: Font.DemiBold
                            color: root.contract.textPrimaryColor
                            Accessible.role: Accessible.Heading
                            Accessible.name: text
                        }
                        AccessibleTransportButton {
                            objectName: "room-catalog-add-all"
                            visible: root.room.canAddPlaylist
                            enabled: catalogRow.entryCount > 0
                            contract: root.contract
                            text: roomText.addAll
                            accessibleName: roomText.addAllName.arg(catalogRow.name)
                            onClicked: root.room.roomAddPlaylist(catalogRow.playlistId)
                        }
                    }
                }
                Component {
                    id: catalogEntry
                    RowLayout {
                        objectName: "room-catalog-entry-" + catalogRow.entryId
                        spacing: root.contract.spacingUnit
                        Accessible.role: Accessible.ListItem
                        Accessible.name: catalogRow.shownTitle
                        Accessible.description: catalogRow.artist
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text {
                                objectName: "room-catalog-entry-title"
                                Layout.fillWidth: true
                                text: catalogRow.shownTitle
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                font.family: root.contract.bodyFontFamily
                                font.pixelSize: root.contract.typeBody
                                color: root.contract.textPrimaryColor
                                Accessible.ignored: true
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: text.length > 0
                                text: catalogRow.artist
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                font.family: root.contract.bodyFontFamily
                                font.pixelSize: root.contract.typeCaption
                                color: root.contract.textSecondaryColor
                                Accessible.ignored: true
                            }
                        }
                        AccessibleTransportButton {
                            objectName: "room-catalog-add"
                            visible: root.room.canAddFromRoom
                            contract: root.contract
                            flat: true
                            text: roomText.addItem
                            accessibleName: roomText.addItemName.arg(catalogRow.shownTitle)
                            onClicked: root.room.roomAddItem(catalogRow.entryId)
                        }
                    }
                }
            }
        }
        Item {
            Layout.fillHeight: true
        }
    }

    RoomMemberMenu {
        id: memberMenu
        contract: root.contract
        room: root.room
    }
}
