pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

Dialog {
    id: root
    objectName: "playlists-panel"
    required property ShellUiContract contract
    property var playlists: null
    property string selectedEntryId: ""
    property string nameMode: ""
    property var nameTarget: null
    property var nameOwner: null
    property var deleteTarget: null
    property var deleteOwner: null
    property bool nameRejected: false
    property bool deleteRejected: false
    readonly property bool ready: playlists !== null && playlists.ready
    readonly property bool busy: playlists !== null && playlists.busy
    readonly property bool interactionAllowed: enabled && playlists !== null && playlists.interactionAllowed
    readonly property bool canEdit: ready && interactionAllowed && !busy && !playlists.hasPendingChange
    readonly property string selectedPlaylistId: playlists !== null ? playlists.selectedPlaylistId : ""
    readonly property var selectedPlaylist: playlistForId(selectedPlaylistId)
    readonly property var selectedEntry: entryForId(selectedEntryId)
    readonly property int selectedEntryIndex: entryIndex(selectedEntryId)
    readonly property bool nameTargetCurrent: nameOwner === playlists && canEdit && (nameMode === "create" || nameMode === "saveQueue" || (nameMode === "rename" && snapshotCurrent(nameTarget)))
    readonly property bool deleteTargetCurrent: deleteOwner === playlists && canEdit && snapshotCurrent(deleteTarget)
    readonly property string nameDialogTitle: nameMode === "rename" ? qsTr("Renomear playlist") : (nameMode === "saveQueue" ? qsTr("Salvar fila como playlist") : qsTr("Nova playlist"))
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(920, parent ? parent.width - contract.spacingUnit * 8 : 920)
    height: Math.min(680, parent ? parent.height - contract.spacingUnit * 8 : 680)
    padding: contract.spacingUnit * 3
    modal: true
    focus: true
    popupType: Popup.Item
    closePolicy: Popup.CloseOnEscape
    onSelectedPlaylistIdChanged: selectedEntryId = ""
    onInteractionAllowedChanged: {
        if (!interactionAllowed) {
            nameDialog.close();
            deleteDialog.close();
            if (visible)
                close();
        }
    }
    onOpened: {
        if (playlistList.count > 0)
            playlistList.forceActiveFocus(Qt.TabFocusReason);
        else
            createButton.forceActiveFocus(Qt.TabFocusReason);
    }
    onClosed: {
        nameDialog.close();
        deleteDialog.close();
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
    component LibraryRow: ItemDelegate {
        id: row
        required property ShellUiContract contract
        property string primaryText: ""
        property string secondaryText: ""
        property string primaryObjectName: ""
        implicitHeight: 58
        activeFocusOnTab: true
        Accessible.name: primaryText + (secondaryText.length > 0 ? " · " + secondaryText : "")
        background: Rectangle {
            color: row.highlighted ? row.contract.surfaceColor : "transparent"
            radius: row.contract.radiusSmall
            border.width: row.activeFocus ? 3 : (row.highlighted ? 1 : 0)
            border.color: row.activeFocus ? row.contract.focusColor : row.contract.accentColor
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
        color: root.contract.surfaceRaisedColor
        radius: root.contract.radiusMedium
        border.width: 1
        border.color: root.contract.borderColor
    }
    contentItem: ColumnLayout {
        spacing: root.contract.spacingUnit * 2
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Playlists")
        Text {
            Layout.fillWidth: true
            text: qsTr("Playlists")
            textFormat: Text.PlainText
            color: root.contract.textPrimaryColor
            font.bold: true
            font.pixelSize: 22
            Accessible.ignored: true
        }
        RowLayout {
            Layout.fillWidth: true
            AccessibleTransportButton {
                id: createButton
                objectName: "playlist-create"
                contract: root.contract
                text: qsTr("Nova playlist")
                enabled: root.canEdit
                onClicked: root.openName("create")
            }
            AccessibleTransportButton {
                objectName: "playlist-save-queue"
                contract: root.contract
                text: qsTr("Salvar fila como…")
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
            Layout.minimumHeight: 100
            spacing: root.contract.spacingUnit * 3
            ColumnLayout {
                Layout.preferredWidth: 190
                Layout.minimumWidth: 180
                Layout.fillHeight: true
                ListView {
                    id: playlistList
                    objectName: "playlists-list"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: root.playlists !== null ? root.playlists.playlists : []
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    activeFocusOnTab: true
                    ScrollBar.vertical: AccessibleScrollBar {
                        contract: root.contract
                    }
                    delegate: LibraryRow {
                        required property var modelData
                        width: playlistList.width
                        contract: root.contract
                        primaryText: modelData.name
                        secondaryText: qsTr("%1 faixas").arg(modelData.count)
                        primaryObjectName: "playlist-row-name"
                        highlighted: root.selectedPlaylistId === modelData.id
                        enabled: root.interactionAllowed && !root.busy && !root.playlists.hasPendingChange
                        onClicked: root.playlists.selectPlaylist(modelData.id)
                    }
                    Text {
                        anchors.centerIn: parent
                        width: parent.width
                        visible: playlistList.count === 0
                        text: qsTr("Suas playlists aparecerão aqui.")
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
                        text: qsTr("Renomear")
                        enabled: root.canEdit && root.selectedPlaylist !== null
                        onClicked: root.openName("rename")
                    }
                    AccessibleTransportButton {
                        objectName: "playlist-delete"
                        contract: root.contract
                        text: qsTr("Excluir")
                        enabled: root.canEdit && root.selectedPlaylist !== null
                        onClicked: root.requestDelete()
                    }
                }
            }
            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: root.contract.borderColor
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ListView {
                    id: entryList
                    objectName: "playlist-entries"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: root.playlists !== null ? root.playlists.entries : []
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
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
                        highlighted: root.selectedEntryId === modelData.id
                        enabled: root.canEdit
                        onClicked: root.selectedEntryId = modelData.id
                    }
                    Text {
                        anchors.centerIn: parent
                        width: parent.width
                        visible: entryList.count === 0
                        text: root.selectedPlaylist === null ? qsTr("Selecione uma playlist.") : qsTr("Esta playlist está vazia.")
                        textFormat: Text.PlainText
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        color: root.contract.textSecondaryColor
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    AccessibleTransportButton {
                        objectName: "playlist-move-up"
                        contract: root.contract
                        text: "↑"
                        accessibleName: qsTr("Mover faixa para cima")
                        enabled: root.canEdit && root.selectedEntry !== null && root.selectedEntryIndex > 0
                        onClicked: root.playlists.moveEntry(root.selectedPlaylistId, root.selectedEntryId, root.selectedEntryIndex - 1)
                    }
                    AccessibleTransportButton {
                        objectName: "playlist-move-down"
                        contract: root.contract
                        text: "↓"
                        accessibleName: qsTr("Mover faixa para baixo")
                        enabled: root.canEdit && root.selectedEntry !== null && root.selectedEntryIndex + 1 < entryList.count
                        onClicked: root.playlists.moveEntry(root.selectedPlaylistId, root.selectedEntryId, root.selectedEntryIndex + 1)
                    }
                    AccessibleTransportButton {
                        objectName: "playlist-remove-entry"
                        contract: root.contract
                        text: qsTr("Remover")
                        accessibleName: qsTr("Remover faixa da playlist")
                        enabled: root.canEdit && root.selectedEntry !== null
                        onClicked: root.playlists.removeEntry(root.selectedPlaylistId, root.selectedEntryId)
                    }
                    AccessibleTransportButton {
                        objectName: "playlist-locate-file"
                        contract: root.contract
                        text: qsTr("Localizar arquivo…")
                        enabled: root.canEdit && root.selectedPlaylist !== null && root.selectedEntry !== null && root.playlists.canReplaceFile === true
                        onClicked: {
                            if (!enabled || !root.opened || nameDialog.visible || deleteDialog.visible)
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
        RowLayout {
            Layout.fillWidth: true
            AccessibleTransportButton {
                objectName: "playlist-add-files"
                contract: root.contract
                text: qsTr("Adicionar arquivos…")
                enabled: root.canEdit && root.selectedPlaylist !== null && root.playlists.canAddFiles
                onClicked: root.playlists.requestAddFiles(root.selectedPlaylistId)
            }
            AccessibleTransportButton {
                objectName: "playlist-add-queue"
                contract: root.contract
                text: qsTr("Adicionar fila atual")
                enabled: root.canEdit && root.selectedPlaylist !== null
                onClicked: root.playlists.addQueueToPlaylist(root.selectedPlaylistId)
            }
            AccessibleTransportButton {
                objectName: "playlist-enqueue"
                contract: root.contract
                text: qsTr("Adicionar à fila")
                enabled: root.canEdit && root.selectedPlaylist !== null && root.selectedPlaylist.count > 0
                onClicked: root.playlists.enqueuePlaylist(root.selectedPlaylistId)
            }
            Item {
                Layout.fillWidth: true
            }
        }
        Text {
            objectName: "playlists-status"
            Layout.fillWidth: true
            text: root.playlists !== null ? root.playlists.statusText : ""
            textFormat: Text.PlainText
            maximumLineCount: 2
            elide: Text.ElideRight
            wrapMode: Text.WordWrap
            color: root.contract.textPrimaryColor
            Accessible.name: text
        }
        RowLayout {
            Layout.fillWidth: true
            AccessibleTransportButton {
                objectName: "playlists-retry-save"
                contract: root.contract
                text: qsTr("Tentar salvar")
                visible: root.playlists !== null && root.playlists.canRetrySave
                enabled: root.interactionAllowed && !root.busy
                onClicked: root.playlists.retrySave()
            }
            AccessibleTransportButton {
                objectName: "playlists-discard-pending"
                contract: root.contract
                text: qsTr("Recarregar do disco")
                visible: root.playlists !== null && root.playlists.hasPendingChange
                enabled: root.interactionAllowed && !root.busy
                onClicked: root.playlists.discardPending()
            }
            AccessibleTransportButton {
                objectName: "playlists-retry-load"
                contract: root.contract
                text: qsTr("Tentar abrir novamente")
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
                text: qsTr("Fechar")
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
                placeholderText: qsTr("Nome da playlist")
                maximumLength: 256
                selectByMouse: true
                Accessible.name: placeholderText
                onAccepted: nameSubmit.clicked()
            }
            Text {
                Layout.fillWidth: true
                visible: root.nameRejected || !root.nameTargetCurrent
                text: !root.nameTargetCurrent ? qsTr("A seleção mudou. Cancele e escolha a playlist novamente.") : qsTr("Não foi possível aceitar esta alteração. Confira o nome e o estado das playlists.")
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
                    text: qsTr("Cancelar")
                    onClicked: nameDialog.reject()
                }
                AccessibleActionButton {
                    id: nameSubmit
                    controlId: "playlist-name-submit"
                    contract: root.contract
                    text: qsTr("Salvar")
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
            Accessible.name: qsTr("Excluir playlist")
            Text {
                Layout.fillWidth: true
                text: qsTr("Excluir playlist?")
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
                text: qsTr("Somente a lista será excluída. Os arquivos de áudio e a fila atual serão mantidos.")
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: root.contract.textSecondaryColor
            }
            Text {
                Layout.fillWidth: true
                visible: root.deleteRejected || !root.deleteTargetCurrent
                text: !root.deleteTargetCurrent ? qsTr("A seleção mudou. Cancele e escolha a playlist novamente.") : qsTr("Não foi possível aceitar a exclusão. Confira o estado das playlists.")
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
                    text: qsTr("Cancelar")
                    onClicked: deleteDialog.reject()
                }
                AccessibleActionButton {
                    id: deleteSubmit
                    controlId: "playlist-delete-submit"
                    contract: root.contract
                    text: qsTr("Excluir playlist")
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
}
