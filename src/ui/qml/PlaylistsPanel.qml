pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

PageSurface {
    id: root
    NavigationText {
        id: navigationLabels
    }
    PlaylistsText {
        id: playlistsText
    }
    objectName: "playlists-panel"
    required property ShellUiContract contract
    required property bool queueHasTracks
    property var playlists: null
    property var fileSelectionOwner: null
    property string selectedEntryId: ""
    property string nameMode: ""
    property var nameTarget: null
    property var nameOwner: null
    property var deleteTarget: null
    property var deleteOwner: null
    property var abandonOwner: null
    property bool nameRejected: false
    property bool deleteRejected: false
    readonly property bool ready: playlists !== null && playlists.ready
    readonly property bool busy: playlists !== null && playlists.busy
    readonly property bool interactionAllowed: enabled && playlists !== null && playlists.interactionAllowed
    readonly property bool canEdit: ready && interactionAllowed && !busy && !playlists.hasPendingChange
    readonly property bool canAbandonPending: interactionAllowed && !busy && playlists.hasPendingChange
    readonly property string selectedPlaylistId: playlists !== null ? playlists.selectedPlaylistId : ""
    readonly property var selectedPlaylist: playlistForId(selectedPlaylistId)
    readonly property var selectedEntry: entryForId(selectedEntryId)
    readonly property int selectedEntryIndex: entryIndex(selectedEntryId)
    readonly property bool nameTargetCurrent: nameOwner === playlists && canEdit && (nameMode === "create" || nameMode === "saveQueue" || (nameMode === "rename" && snapshotCurrent(nameTarget)))
    readonly property bool deleteTargetCurrent: deleteOwner === playlists && canEdit && snapshotCurrent(deleteTarget)
    readonly property bool abandonTargetCurrent: abandonOwner !== null && abandonOwner === playlists && canAbandonPending
    readonly property string nameDialogTitle: nameMode === "rename" ? playlistsText.renameTitle : (nameMode === "saveQueue" ? playlistsText.saveQueueTitle : playlistsText.create)
    parent: Overlay.overlay
    anchors.centerIn: embedded ? undefined : parent
    width: Math.min(920, parent ? parent.width - contract.spacingUnit * 8 : 920)
    height: Math.min(680, parent ? parent.height - contract.spacingUnit * 8 : 680)
    padding: embedded ? 0 : contract.spacingUnit * 3
    modal: !embedded
    focus: true
    popupType: Popup.Item
    closePolicy: Popup.CloseOnEscape
    function revokeFileSelection(owner) {
        if (owner && typeof owner.cancelFileSelection === "function")
            owner.cancelFileSelection();
    }
    onPlaylistsChanged: {
        const previous = fileSelectionOwner;
        fileSelectionOwner = playlists;
        if (previous !== playlists)
            revokeFileSelection(previous);
    }
    onAboutToHide: revokeFileSelection(playlists)
    Component.onCompleted: fileSelectionOwner = playlists
    Component.onDestruction: revokeFileSelection(fileSelectionOwner)
    onSelectedPlaylistIdChanged: selectedEntryId = ""
    onAbandonTargetCurrentChanged: {
        if (!abandonTargetCurrent && abandonDialog.visible)
            abandonDialog.close();
    }
    onInteractionAllowedChanged: {
        if (!interactionAllowed) {
            nameDialog.close();
            deleteDialog.close();
            if (visible)
                close();
        }
    }
    blockingUiOpen: nameDialog.visible || deleteDialog.visible || abandonDialog.visible
    onSurfaceOpened: {
        if (focusSaveRecovery())
            return;
        if (retryLoadButton.visible && retryLoadButton.enabled)
            retryLoadButton.forceActiveFocus(Qt.TabFocusReason);
        else if (playlistList.count > 0)
            playlistList.forceActiveFocus(Qt.TabFocusReason);
        else
            createButton.forceActiveFocus(Qt.TabFocusReason);
    }
    onClosed: {
        nameDialog.close();
        deleteDialog.close();
        abandonDialog.close();
    }
    function focusSaveRecovery(): bool {
        if (!retrySaveButton.visible || !retrySaveButton.enabled)
            return false;
        retrySaveButton.forceActiveFocus(Qt.TabFocusReason);
        return true;
    }
    function playlistForId(id) {
        if (playlists === null || id.length === 0)
            return null;
        const rows = playlists.playlists;
        for (let index = 0; index < rows.length; ++index) {
            if (rows[index].id === id)
                return rows[index];
        }
        return null;
    }
    function entryIndex(id) {
        if (playlists === null || id.length === 0)
            return -1;
        const rows = playlists.entries;
        for (let index = 0; index < rows.length; ++index) {
            if (rows[index].id === id)
                return index;
        }
        return -1;
    }
    function entryForId(id) {
        const index = entryIndex(id);
        return index >= 0 ? playlists.entries[index] : null;
    }
    function snapshotCurrent(snapshot) {
        if (snapshot === null || snapshot.id !== selectedPlaylistId)
            return false;
        const row = playlistForId(snapshot.id);
        return row !== null && row.name === snapshot.name;
    }
    function openName(mode) {
        if (!canEdit || (mode === "rename" && selectedPlaylist === null))
            return;
        nameMode = mode;
        nameOwner = playlists;
        nameTarget = selectedPlaylist !== null ? {
            id: selectedPlaylist.id,
            name: selectedPlaylist.name
        } : null;
        nameField.text = mode === "rename" ? selectedPlaylist.name : "";
        nameRejected = false;
        nameDialog.open();
    }
    function requestDelete() {
        if (!canEdit || selectedPlaylist === null)
            return;
        deleteOwner = playlists;
        deleteTarget = {
            id: selectedPlaylist.id,
            name: selectedPlaylist.name
        };
        deleteRejected = false;
        deleteDialog.open();
    }
    component LibraryList: ListView {
        id: list
        required property bool selectionAllowed
        required property string selectedId
        property string cursorId: ""
        signal rowActivated(string rowId)

        activeFocusOnTab: selectionAllowed && count > 0
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        keyNavigationEnabled: false
        Accessible.role: Accessible.List
        Accessible.focusable: activeFocusOnTab
        Accessible.focused: activeFocus

        function indexOfId(rowId) {
            const rows = model || [];
            for (let index = 0; index < rows.length; ++index) {
                if (rows[index].id === rowId)
                    return index;
            }
            return -1;
        }
        function moveCursor(index, reveal) {
            const rows = model || [];
            const next = rows.length > 0 ? Math.max(0, Math.min(index, rows.length - 1)) : -1;
            cursorId = next < 0 ? "" : rows[next].id;
            currentIndex = next;
            if (reveal && next >= 0)
                positionViewAtIndex(next, ListView.Contain);
        }
        function restoreCursor(preferredId) {
            const preferred = indexOfId(preferredId);
            moveCursor(preferred >= 0 ? preferred : indexOfId(selectedId), activeFocus);
        }
        function activateRow(rowId, reason) {
            const index = indexOfId(rowId);
            if (!selectionAllowed || index < 0)
                return;
            moveCursor(index, true);
            forceActiveFocus(reason);
            rowActivated(rowId);
        }

        Component.onCompleted: restoreCursor(selectedId)
        onModelChanged: restoreCursor(cursorId)
        onSelectedIdChanged: restoreCursor(selectedId)
        onActiveFocusChanged: {
            if (activeFocus)
                restoreCursor(cursorId);
        }
        Keys.onPressed: event => {
            if (!selectionAllowed || count === 0 || (event.modifiers !== Qt.NoModifier && event.modifiers !== Qt.KeypadModifier))
                return;
            switch (event.key) {
            case Qt.Key_Up:
                moveCursor(currentIndex - 1, true);
                break;
            case Qt.Key_Down:
                moveCursor(currentIndex + 1, true);
                break;
            case Qt.Key_Home:
                moveCursor(0, true);
                break;
            case Qt.Key_End:
                moveCursor(model.length - 1, true);
                break;
            case Qt.Key_Return:
            case Qt.Key_Enter:
            case Qt.Key_Space:
                if (!event.isAutoRepeat)
                    activateRow(cursorId, Qt.TabFocusReason);
                break;
            default:
                return;
            }
            event.accepted = true;
        }
    }
    component LibraryRow: ItemDelegate {
        id: row
        required property ShellUiContract contract
        property string primaryText: ""
        property string secondaryText: ""
        property string primaryObjectName: ""
        property bool cursorFocused: false
        property bool editorial: false
        implicitHeight: 58
        activeFocusOnTab: false
        focusPolicy: Qt.NoFocus
        Accessible.role: Accessible.ListItem
        Accessible.name: primaryText + (secondaryText.length > 0 ? " · " + secondaryText : "")
        Accessible.focusable: enabled
        Accessible.focused: cursorFocused
        Accessible.selectable: true
        Accessible.selected: highlighted
        Accessible.onPressAction: {
            if (enabled)
                clicked();
        }
        background: Rectangle {
            color: row.editorial && !row.contract.highContrast ? (row.highlighted ? row.contract.selectionColor : (row.hovered && row.enabled ? row.contract.hoverColor : "transparent")) : (row.highlighted ? row.contract.surfaceColor : "transparent")
            radius: row.contract.radiusSmall
            border.width: row.cursorFocused ? 3 : (row.highlighted && (!row.editorial || row.contract.highContrast) ? 1 : 0)
            border.color: row.cursorFocused ? row.contract.focusColor : row.contract.accentColor
        }
        contentItem: Column {
            spacing: row.contract.spacingUnit
            Text {
                objectName: row.primaryObjectName
                width: parent.width
                text: row.primaryText
                textFormat: Text.PlainText
                color: row.contract.textPrimaryColor
                elide: Text.ElideRight
                font.bold: true
                Accessible.ignored: true
            }
            Text {
                width: parent.width
                text: row.secondaryText
                textFormat: Text.PlainText
                color: row.contract.textSecondaryColor
                elide: Text.ElideMiddle
                Accessible.ignored: true
            }
        }
    }
    background: Rectangle {
        // Inline Aurora pages sit on the canvas; the legacy dialog keeps its card.
        color: root.embedded ? root.contract.canvasColor : root.contract.surfaceRaisedColor
        radius: root.embedded ? 0 : root.contract.radiusMedium
        border.width: root.embedded ? 0 : 1
        border.color: root.contract.borderColor
    }
    contentItem: ColumnLayout {
        spacing: root.contract.spacingUnit * 2
        Accessible.role: root.embedded ? Accessible.Pane : Accessible.Dialog
        Accessible.name: playlistsText.title
        PageHeader {
            Layout.fillWidth: true
            contract: root.contract
            editorial: root.embedded
            eyebrow: root.embedded ? navigationLabels.playlistsEyebrow : ""
            title: playlistsText.title
            description: root.embedded ? navigationLabels.playlistsDescription : ""
            legacyTitleSize: root.contract.typeSection
        }
        RowLayout {
            Layout.fillWidth: true
            AccessibleTransportButton {
                id: createButton
                objectName: "playlist-create"
                primary: root.embedded
                contract: root.contract
                text: playlistsText.create
                enabled: root.canEdit
                onClicked: root.openName("create")
            }
            AccessibleTransportButton {
                objectName: "playlist-save-queue"
                contract: root.contract
                text: playlistsText.saveQueue
                enabled: root.canEdit
                onClicked: root.openName("saveQueue")
            }
            Item {
                Layout.fillWidth: true
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: root.embedded ? 224 : 100
            spacing: root.contract.spacingUnit * 3
            ColumnLayout {
                Layout.preferredWidth: 190
                Layout.minimumWidth: 180
                Layout.fillHeight: true
                LibraryList {
                    id: playlistList
                    objectName: "playlists-list"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: root.playlists !== null ? root.playlists.playlists : []
                    selectionAllowed: root.canEdit
                    selectedId: root.selectedPlaylistId
                    Accessible.name: playlistsText.savedPlaylists
                    onRowActivated: rowId => root.playlists.selectPlaylist(rowId)
                    ScrollBar.vertical: AccessibleScrollBar {
                        contract: root.contract
                    }
                    delegate: LibraryRow {
                        required property var modelData
                        width: playlistList.width
                        contract: root.contract
                        primaryText: modelData.name
                        secondaryText: playlistsText.trackCount.arg(modelData.count)
                        primaryObjectName: "playlist-row-name"
                        editorial: root.embedded
                        highlighted: root.selectedPlaylistId === modelData.id
                        cursorFocused: playlistList.activeFocus && ListView.isCurrentItem
                        enabled: playlistList.selectionAllowed
                        onClicked: playlistList.activateRow(modelData.id, Qt.MouseFocusReason)
                    }
                    Text {
                        anchors.centerIn: parent
                        width: parent.width
                        visible: playlistList.count === 0
                        text: playlistsText.empty
                        textFormat: Text.PlainText
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        color: root.contract.textSecondaryColor
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    AccessibleTransportButton {
                        objectName: "playlist-rename"
                        contract: root.contract
                        text: playlistsText.rename
                        enabled: root.canEdit && root.selectedPlaylist !== null
                        onClicked: root.openName("rename")
                    }
                    AccessibleTransportButton {
                        objectName: "playlist-delete"
                        contract: root.contract
                        text: playlistsText.deleteAction
                        enabled: root.canEdit && root.selectedPlaylist !== null
                        onClicked: root.requestDelete()
                    }
                }
            }
            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: root.embedded && !root.contract.highContrast ? root.contract.dividerColor : root.contract.borderColor
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ColumnLayout {
                    // Detail header of the selected list (Aurora): eyebrow, serif name, track count.
                    objectName: "playlist-detail-header"
                    visible: root.embedded && root.selectedPlaylist !== null
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.bottomMargin: 8
                    spacing: 4
                    Text {
                        objectName: "playlist-detail-eyebrow"
                        Layout.fillWidth: true
                        text: playlistsText.detailEyebrow
                        textFormat: Text.PlainText
                        font.family: root.contract.bodyFontFamily
                        font.pixelSize: root.contract.typeEyebrow
                        font.letterSpacing: 1.2
                        color: root.contract.textSecondaryColor
                    }
                    Text {
                        objectName: "playlist-detail-name"
                        Layout.fillWidth: true
                        text: root.selectedPlaylist !== null ? root.selectedPlaylist.name : ""
                        textFormat: Text.PlainText
                        font.family: root.contract.headingFontFamily
                        font.pixelSize: root.contract.typeSection
                        color: root.contract.textPrimaryColor
                        elide: Text.ElideRight
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                    }
                    Text {
                        objectName: "playlist-detail-meta"
                        Layout.fillWidth: true
                        text: playlistsText.trackCount.arg(root.selectedPlaylist !== null ? root.selectedPlaylist.count : 0)
                        textFormat: Text.PlainText
                        font.family: root.contract.bodyFontFamily
                        font.pixelSize: root.contract.typeCaption
                        color: root.contract.textSecondaryColor
                    }
                }
                LibraryList {
                    id: entryList
                    objectName: "playlist-entries"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: root.playlists !== null ? root.playlists.entries : []
                    selectionAllowed: root.canEdit
                    selectedId: root.selectedEntryId
                    Accessible.name: playlistsText.tracks
                    onRowActivated: rowId => root.selectedEntryId = rowId
                    ScrollBar.vertical: AccessibleScrollBar {
                        contract: root.contract
                    }
                    delegate: LibraryRow {
                        required property var modelData
                        width: entryList.width
                        contract: root.contract
                        primaryText: modelData.title
                        secondaryText: modelData.path
                        primaryObjectName: "playlist-entry-title"
                        editorial: root.embedded
                        highlighted: root.selectedEntryId === modelData.id
                        cursorFocused: entryList.activeFocus && ListView.isCurrentItem
                        enabled: entryList.selectionAllowed
                        onClicked: entryList.activateRow(modelData.id, Qt.MouseFocusReason)
                    }
                    Text {
                        anchors.centerIn: parent
                        width: parent.width
                        visible: entryList.count === 0
                        text: root.selectedPlaylist === null ? playlistsText.selectPlaylist : playlistsText.emptyPlaylist
                        textFormat: Text.PlainText
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        color: root.contract.textSecondaryColor
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: root.contract.spacingUnit * 2
                    AccessibleTransportButton {
                        objectName: "playlist-move-up"
                        contract: root.contract
                        text: "↑"
                        accessibleName: playlistsText.moveTrackUp
                        enabled: root.canEdit && root.selectedEntry !== null && root.selectedEntryIndex > 0
                        onClicked: root.playlists.moveEntry(root.selectedPlaylistId, root.selectedEntryId, root.selectedEntryIndex - 1)
                    }
                    AccessibleTransportButton {
                        objectName: "playlist-move-down"
                        contract: root.contract
                        text: "↓"
                        accessibleName: playlistsText.moveTrackDown
                        enabled: root.canEdit && root.selectedEntry !== null && root.selectedEntryIndex + 1 < entryList.count
                        onClicked: root.playlists.moveEntry(root.selectedPlaylistId, root.selectedEntryId, root.selectedEntryIndex + 1)
                    }
                    AccessibleTransportButton {
                        objectName: "playlist-remove-entry"
                        contract: root.contract
                        text: playlistsText.remove
                        accessibleName: playlistsText.removeTrack
                        enabled: root.canEdit && root.selectedEntry !== null
                        onClicked: root.playlists.removeEntry(root.selectedPlaylistId, root.selectedEntryId)
                    }
                    AccessibleTransportButton {
                        objectName: "playlist-locate-file"
                        contract: root.contract
                        text: playlistsText.locate
                        enabled: root.canEdit && root.selectedPlaylist !== null && root.selectedEntry !== null && root.playlists.canReplaceFile === true
                        onClicked: {
                            if (!enabled || !root.opened || nameDialog.visible || deleteDialog.visible || abandonDialog.visible)
                                return;
                            root.playlists.requestReplaceFile(root.selectedPlaylistId, root.selectedEntryId);
                        }
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: root.contract.spacingUnit * 2
            AccessibleTransportButton {
                objectName: "playlist-add-files"
                contract: root.contract
                text: playlistsText.addFiles
                enabled: root.canEdit && root.selectedPlaylist !== null && root.playlists.canAddFiles
                onClicked: root.playlists.requestAddFiles(root.selectedPlaylistId)
            }
            AccessibleTransportButton {
                objectName: "playlist-add-queue"
                contract: root.contract
                text: playlistsText.addCurrentQueue
                enabled: root.canEdit && root.selectedPlaylist !== null && root.queueHasTracks
                onClicked: root.playlists.addQueueToPlaylist(root.selectedPlaylistId)
            }
            AccessibleTransportButton {
                objectName: "playlist-enqueue"
                primary: root.embedded
                contract: root.contract
                text: playlistsText.addToQueue
                enabled: root.canEdit && root.selectedPlaylist !== null && root.selectedPlaylist.count > 0
                onClicked: root.playlists.enqueuePlaylist(root.selectedPlaylistId)
            }
            Item {
                Layout.fillWidth: true
            }
        }
        Text {
            id: statusLabel
            objectName: "playlists-status"
            Layout.fillWidth: true
            text: root.playlists !== null ? root.playlists.statusText : ""
            textFormat: Text.PlainText
            maximumLineCount: 2
            elide: Text.ElideRight
            wrapMode: Text.WordWrap
            color: root.contract.textPrimaryColor
            Accessible.role: Accessible.StaticText
            Accessible.name: text
            function announceResult() {
                if (root.opened && !root.busy && text.length > 0)
                    statusLabel.Accessible.announce(text, Accessible.Polite);
            }
            onTextChanged: {
                if (root.opened)
                    Qt.callLater(announceResult);
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: root.contract.spacingUnit * 2
            AccessibleTransportButton {
                id: retrySaveButton
                objectName: "playlists-retry-save"
                contract: root.contract
                text: playlistsText.retrySave
                visible: root.playlists !== null && root.playlists.canRetrySave
                enabled: root.interactionAllowed && !root.busy
                onClicked: root.playlists.retrySave()
            }
            AccessibleTransportButton {
                objectName: "playlists-discard-pending"
                contract: root.contract
                text: playlistsText.reloadFromDisk
                visible: root.playlists !== null && root.playlists.hasPendingChange
                enabled: root.interactionAllowed && !root.busy
                onClicked: root.playlists.discardPending()
            }
            AccessibleTransportButton {
                id: abandonButton
                objectName: "playlists-abandon-pending"
                contract: root.contract
                text: playlistsText.discardEdit
                visible: root.playlists !== null && root.playlists.hasPendingChange
                enabled: root.canAbandonPending
                onClicked: {
                    if (root.opened && root.canAbandonPending) {
                        root.abandonOwner = root.playlists;
                        abandonDialog.open();
                    }
                }
            }
            AccessibleTransportButton {
                id: retryLoadButton
                objectName: "playlists-retry-load"
                contract: root.contract
                text: playlistsText.retryLoad
                visible: !root.ready && !root.busy
                enabled: root.interactionAllowed && !root.busy
                onClicked: root.playlists.retryLoad()
            }
            Item {
                Layout.fillWidth: true
            }
            AccessibleTransportButton {
                objectName: "playlists-close"
                contract: root.contract
                flat: root.embedded
                iconName: root.embedded ? "close" : ""
                iconWithText: true
                text: playlistsText.close
                onClicked: root.close()
            }
        }
    }
    Dialog {
        id: nameDialog
        objectName: "playlist-name-dialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(480, parent ? parent.width - root.contract.spacingUnit * 8 : 480)
        modal: true
        focus: true
        popupType: Popup.Item
        closePolicy: Popup.CloseOnEscape
        padding: root.contract.spacingUnit * 3
        onOpened: {
            nameField.forceActiveFocus(Qt.TabFocusReason);
            nameField.selectAll();
        }
        onClosed: {
            root.nameTarget = null;
            root.nameOwner = null;
            root.nameMode = "";
        }
        contentItem: ColumnLayout {
            spacing: root.contract.spacingUnit * 2
            Accessible.role: Accessible.Dialog
            Accessible.name: root.nameDialogTitle
            Text {
                Layout.fillWidth: true
                text: root.nameDialogTitle
                textFormat: Text.PlainText
                color: root.contract.textPrimaryColor
                font.bold: true
            }
            TextField {
                id: nameField
                objectName: "playlist-name"
                Layout.fillWidth: true
                implicitHeight: root.contract.controlTargetPreferred
                placeholderText: playlistsText.name
                maximumLength: 256
                selectByMouse: true
                Accessible.name: placeholderText
                onAccepted: nameSubmit.clicked()
            }
            Text {
                Layout.fillWidth: true
                visible: root.nameRejected || !root.nameTargetCurrent
                text: !root.nameTargetCurrent ? playlistsText.selectionChanged : playlistsText.editRejected
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: root.contract.textPrimaryColor
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                AccessibleTransportButton {
                    contract: root.contract
                    text: playlistsText.cancel
                    onClicked: nameDialog.reject()
                }
                AccessibleActionButton {
                    id: nameSubmit
                    controlId: "playlist-name-submit"
                    contract: root.contract
                    text: playlistsText.save
                    enabled: root.nameTargetCurrent && nameField.text.trim().length > 0
                    onClicked: {
                        if (!enabled || root.nameOwner === null)
                            return;
                        const owner = root.nameOwner;
                        const name = nameField.text.trim();
                        const accepted = root.nameMode === "rename" ? owner.renamePlaylist(root.nameTarget.id, name) : (root.nameMode === "saveQueue" ? owner.saveQueueAsPlaylist(name) : owner.createPlaylist(name));
                        if (accepted)
                            nameDialog.accept();
                        else
                            root.nameRejected = true;
                    }
                }
            }
        }
    }
    Dialog {
        id: deleteDialog
        objectName: "playlist-delete-dialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(480, parent ? parent.width - root.contract.spacingUnit * 8 : 480)
        modal: true
        focus: true
        popupType: Popup.Item
        closePolicy: Popup.CloseOnEscape
        padding: root.contract.spacingUnit * 3
        onClosed: {
            root.deleteTarget = null;
            root.deleteOwner = null;
        }
        contentItem: ColumnLayout {
            spacing: root.contract.spacingUnit * 2
            Accessible.role: Accessible.Dialog
            Accessible.name: playlistsText.deletePlaylist
            Text {
                Layout.fillWidth: true
                text: playlistsText.confirmDelete
                textFormat: Text.PlainText
                color: root.contract.textPrimaryColor
                font.bold: true
            }
            Text {
                objectName: "playlist-delete-name"
                Layout.fillWidth: true
                text: root.deleteTarget !== null ? root.deleteTarget.name : ""
                textFormat: Text.PlainText
                maximumLineCount: 2
                wrapMode: Text.WordWrap
                elide: Text.ElideRight
                color: root.contract.textPrimaryColor
            }
            Text {
                Layout.fillWidth: true
                text: playlistsText.deleteAdvice
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: root.contract.textSecondaryColor
            }
            Text {
                Layout.fillWidth: true
                visible: root.deleteRejected || !root.deleteTargetCurrent
                text: !root.deleteTargetCurrent ? playlistsText.selectionChanged : playlistsText.deleteRejected
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: root.contract.textPrimaryColor
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                AccessibleTransportButton {
                    contract: root.contract
                    text: playlistsText.cancel
                    onClicked: deleteDialog.reject()
                }
                AccessibleActionButton {
                    id: deleteSubmit
                    controlId: "playlist-delete-submit"
                    contract: root.contract
                    text: playlistsText.deletePlaylist
                    enabled: root.deleteTargetCurrent
                    onClicked: {
                        if (!enabled || root.deleteOwner === null)
                            return;
                        if (root.deleteOwner.deletePlaylist(root.deleteTarget.id))
                            deleteDialog.accept();
                        else
                            root.deleteRejected = true;
                    }
                }
            }
        }
    }
    Dialog {
        id: abandonDialog
        objectName: "playlist-abandon-dialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(480, parent ? parent.width - root.contract.spacingUnit * 8 : 480)
        modal: true
        focus: true
        popupType: Popup.Item
        closePolicy: Popup.CloseOnEscape
        padding: root.contract.spacingUnit * 3
        onOpened: abandonCancel.forceActiveFocus(Qt.TabFocusReason)
        onClosed: {
            Qt.callLater(function () {
                if (abandonDialog.visible)
                    return;
                root.abandonOwner = null;
                if (!root.opened || !root.interactionAllowed)
                    return;
                if (abandonButton.visible && abandonButton.enabled)
                    abandonButton.forceActiveFocus(Qt.TabFocusReason);
                else if (retryLoadButton.visible && retryLoadButton.enabled)
                    retryLoadButton.forceActiveFocus(Qt.TabFocusReason);
            });
        }
        contentItem: ColumnLayout {
            spacing: root.contract.spacingUnit * 2
            Accessible.role: Accessible.Dialog
            Accessible.name: playlistsText.discardTitle
            Text {
                Layout.fillWidth: true
                text: playlistsText.confirmDiscard
                textFormat: Text.PlainText
                color: root.contract.textPrimaryColor
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: playlistsText.discardAdvice
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: root.contract.textSecondaryColor
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                AccessibleTransportButton {
                    id: abandonCancel
                    objectName: "playlist-abandon-cancel"
                    contract: root.contract
                    text: playlistsText.cancel
                    onClicked: abandonDialog.reject()
                }
                AccessibleTransportButton {
                    objectName: "playlist-abandon-confirm"
                    contract: root.contract
                    text: playlistsText.confirmDiscardAction
                    enabled: root.abandonTargetCurrent
                    onClicked: {
                        if (abandonDialog.opened && root.abandonTargetCurrent)
                            root.abandonOwner.discardUnsavedChange();
                    }
                }
            }
        }
    }
}
