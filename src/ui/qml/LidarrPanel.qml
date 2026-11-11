pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

Dialog {
    id: root
    objectName: "lidarr-panel"
    required property ShellUiContract contract
    property var lidarr: null
    property string selectedKey: ""
    property var confirmationRow: null
    readonly property bool connected: lidarr !== null && lidarr.connected
    readonly property bool busy: lidarr !== null && lidarr.busy
    readonly property bool interactionAllowed: enabled && lidarr !== null && lidarr.interactionAllowed
    readonly property var selectedResult: resultForKey(selectedKey)
    readonly property bool confirmationCurrent: confirmationRow !== null && confirmationMatches()
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(820, parent ? parent.width - contract.spacingUnit * 8 : 820)
    height: Math.min(640, parent ? parent.height - contract.spacingUnit * 8 : 640)
    modal: true
    focus: true
    popupType: Popup.Item
    closePolicy: Popup.CloseOnEscape
    padding: contract.spacingUnit * 3
    onAboutToShow: {
        addressField.text = lidarr !== null && lidarr.serverAddress.length > 0 ? lidarr.serverAddress : "http://localhost:8686";
        keyField.text = "";
    }
    onOpened: {
        if (root.connected)
            queryField.forceActiveFocus(Qt.TabFocusReason);
        else
            keyField.forceActiveFocus(Qt.TabFocusReason);
    }
    onClosed: {
        keyField.text = "";
        confirmation.close();
        confirmationRow = null;
    }
    onLidarrChanged: selectedKey = ""
    onConnectedChanged: {
        if (!connected) {
            selectedKey = "";
            confirmation.close();
        }
    }
    onInteractionAllowedChanged: {
        if (!interactionAllowed) {
            confirmation.close();
            if (visible)
                close();
        }
    }

    function resultForKey(key) {
        if (lidarr === null || key.length === 0)
            return null;
        const rows = lidarr.results;
        for (let index = 0; index < rows.length; ++index) {
            if (rows[index].key === key)
                return rows[index];
        }
        return null;
    }
    function confirmationMatches() {
        if (lidarr !== confirmationRow.owner)
            return false;
        const row = resultForKey(confirmationRow.key);
        return interactionAllowed && connected && !busy && row !== null && row.kind === "album" && row.title === confirmationRow.title && row.artist === confirmationRow.artist && row.existingId === confirmationRow.existingId;
    }
    function requestConfirmation() {
        const row = selectedResult;
        if (!interactionAllowed || !connected || busy || row === null || row.kind !== "album")
            return;
        confirmationRow = {
            owner: lidarr,
            key: row.key,
            title: row.title,
            artist: row.artist,
            existingId: row.existingId
        };
        rootFolderChoice.currentIndex = 0;
        qualityChoice.currentIndex = 0;
        metadataChoice.currentIndex = 0;
        confirmation.open();
    }
    function submitSearch() {
        if (interactionAllowed && connected && !busy && queryField.text.trim().length > 0)
            lidarr.search(queryField.text.trim(), kindChoice.currentIndex === 1 ? "artist" : "album");
    }
    function submitConnection() {
        if (!interactionAllowed || busy || addressField.text.trim().length === 0 || keyField.text.length === 0)
            return;
        const address = addressField.text.trim();
        const key = keyField.text;
        keyField.text = "";
        lidarr.connectServer(address, key);
    }
    component ServerChoice: ComboBox {
        id: choice
        required property ShellUiContract contract
        implicitHeight: contract.controlTargetPreferred
        contentItem: Text {
            text: choice.displayText
            textFormat: Text.PlainText
            color: choice.contract.textPrimaryColor
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideMiddle
            font: choice.font
        }
        delegate: ItemDelegate {
            id: choiceDelegate
            required property int index
            width: choice.width
            text: choice.textAt(index)
            highlighted: choice.highlightedIndex === index
            contentItem: Text {
                objectName: choice.objectName + "-option-" + choiceDelegate.index
                text: choiceDelegate.text
                textFormat: Text.PlainText
                color: choice.contract.textPrimaryColor
                font: choice.font
                elide: Text.ElideMiddle
            }
        }
        background: Rectangle {
            color: choice.contract.surfaceColor
            radius: choice.contract.radiusSmall
            border.width: choice.activeFocus ? 3 : 1
            border.color: choice.activeFocus ? choice.contract.focusColor : choice.contract.borderColor
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
        Accessible.name: qsTr("Lidarr")
        Text {
            Layout.fillWidth: true
            text: qsTr("Lidarr")
            textFormat: Text.PlainText
            font.bold: true
            font.pixelSize: 22
            color: root.contract.textPrimaryColor
            Accessible.ignored: true
        }
        ColumnLayout {
            Layout.fillWidth: true
            visible: !root.connected
            spacing: root.contract.spacingUnit * 2
            TextField {
                id: addressField
                objectName: "lidarr-address"
                Layout.fillWidth: true
                implicitHeight: root.contract.controlTargetPreferred
                placeholderText: qsTr("Endereço do Lidarr")
                maximumLength: 2048
                enabled: root.interactionAllowed && !root.busy
                selectByMouse: true
                Accessible.name: qsTr("Endereço do Lidarr")
                onAccepted: keyField.forceActiveFocus(Qt.TabFocusReason)
                background: Rectangle {
                    color: root.contract.surfaceColor
                    radius: root.contract.radiusSmall
                    border.width: addressField.activeFocus ? 3 : 1
                    border.color: addressField.activeFocus ? root.contract.focusColor : root.contract.borderColor
                }
            }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: keyField
                    objectName: "lidarr-api-key"
                    Layout.fillWidth: true
                    implicitHeight: root.contract.controlTargetPreferred
                    placeholderText: qsTr("Chave de API do Lidarr")
                    echoMode: TextInput.Password
                    maximumLength: 256
                    enabled: root.interactionAllowed && !root.busy
                    selectByMouse: true
                    Accessible.name: qsTr("Chave de API do Lidarr")
                    onAccepted: root.submitConnection()
                    background: Rectangle {
                        color: root.contract.surfaceColor
                        radius: root.contract.radiusSmall
                        border.width: keyField.activeFocus ? 3 : 1
                        border.color: keyField.activeFocus ? root.contract.focusColor : root.contract.borderColor
                    }
                }
                AccessibleTransportButton {
                    objectName: "lidarr-connect"
                    contract: root.contract
                    text: qsTr("Conectar")
                    enabled: root.interactionAllowed && !root.busy && addressField.text.trim().length > 0 && keyField.text.length > 0
                    onClicked: root.submitConnection()
                }
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("A chave fica somente nesta sessão. HTTP não criptografa a conexão; prefira HTTPS fora deste computador.")
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: root.contract.textSecondaryColor
                Accessible.name: text
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.connected
            Text {
                Layout.fillWidth: true
                text: root.lidarr !== null ? root.lidarr.serverAddress : ""
                textFormat: Text.PlainText
                elide: Text.ElideMiddle
                color: root.contract.textSecondaryColor
                Accessible.name: qsTr("Instância do Lidarr: %1").arg(text)
            }
            AccessibleTransportButton {
                objectName: "lidarr-disconnect"
                contract: root.contract
                text: qsTr("Desconectar")
                enabled: root.interactionAllowed
                onClicked: root.lidarr.disconnectServer()
            }
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Pesquise artistas ou álbuns. O Lidarr solicita álbuns completos; para uma faixa, procure seu artista ou álbum.")
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: root.contract.textSecondaryColor
            Accessible.name: text
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.connected
            TextField {
                id: queryField
                objectName: "lidarr-query"
                Layout.fillWidth: true
                implicitHeight: root.contract.controlTargetPreferred
                placeholderText: qsTr("Pesquisar artistas ou álbuns")
                maximumLength: 512
                enabled: root.interactionAllowed && !root.busy
                selectByMouse: true
                Accessible.name: placeholderText
                onAccepted: root.submitSearch()
                background: Rectangle {
                    color: root.contract.surfaceColor
                    radius: root.contract.radiusSmall
                    border.width: queryField.activeFocus ? 3 : 1
                    border.color: queryField.activeFocus ? root.contract.focusColor : root.contract.borderColor
                }
            }
            ComboBox {
                id: kindChoice
                objectName: "lidarr-kind"
                model: [qsTr("Álbuns"), qsTr("Artistas")]
                implicitHeight: root.contract.controlTargetPreferred
                enabled: root.interactionAllowed && !root.busy
                Accessible.name: qsTr("Pesquisar por")
            }
            AccessibleTransportButton {
                objectName: "lidarr-search"
                contract: root.contract
                text: qsTr("Buscar")
                enabled: root.interactionAllowed && !root.busy && queryField.text.trim().length > 0
                onClicked: root.submitSearch()
            }
        }
        ListView {
            id: resultsList
            objectName: "lidarr-results"
            readonly property bool selectionAllowed: root.interactionAllowed && root.connected && !root.busy
            property string cursorKey: ""
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 64
            visible: root.connected
            model: root.lidarr !== null ? root.lidarr.results : []
            activeFocusOnTab: selectionAllowed && count > 0
            keyNavigationEnabled: false
            clip: true
            spacing: root.contract.spacingUnit
            boundsBehavior: Flickable.StopAtBounds
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Resultados da pesquisa do Lidarr")
            Accessible.focusable: activeFocusOnTab
            Accessible.focused: activeFocus

            function indexOfKey(key) {
                for (let index = 0; index < model.length; ++index) {
                    if (model[index].key === key)
                        return index;
                }
                return -1;
            }
            function moveCursor(index, reveal) {
                const next = model.length > 0 ? Math.max(0, Math.min(index, model.length - 1)) : -1;
                cursorKey = next < 0 ? "" : model[next].key;
                currentIndex = next;
                if (reveal && next >= 0)
                    positionViewAtIndex(next, ListView.Contain);
            }
            function restoreCursor() {
                const index = indexOfKey(cursorKey);
                moveCursor(index >= 0 ? index : indexOfKey(root.selectedKey), activeFocus);
            }
            function activateRow(key, reason) {
                const index = indexOfKey(key);
                if (!selectionAllowed || index < 0)
                    return;
                moveCursor(index, true);
                forceActiveFocus(reason);
                root.selectedKey = key;
            }

            Component.onCompleted: restoreCursor()
            onModelChanged: restoreCursor()
            onActiveFocusChanged: {
                if (activeFocus)
                    restoreCursor();
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
                        activateRow(cursorKey, Qt.TabFocusReason);
                    break;
                default:
                    return;
                }
                event.accepted = true;
            }
            ScrollBar.vertical: AccessibleScrollBar {
                contract: root.contract
            }
            delegate: ItemDelegate {
                id: rowDelegate
                required property var modelData
                required property int index
                readonly property string libraryStatus: modelData.kind !== "album" ? "" : (modelData.existingId > 0 ? qsTr("Já cadastrado no Lidarr") : qsTr("No catálogo do Lidarr"))
                readonly property bool cursorFocused: resultsList.activeFocus && ListView.isCurrentItem
                objectName: "lidarr-result-" + index
                width: resultsList.width
                implicitHeight: 62
                highlighted: root.selectedKey === modelData.key
                enabled: resultsList.selectionAllowed
                activeFocusOnTab: false
                focusPolicy: Qt.NoFocus
                Accessible.role: Accessible.ListItem
                Accessible.name: modelData.title + (modelData.artist.length > 0 ? " · " + modelData.artist : "") + (libraryStatus.length > 0 ? " · " + libraryStatus : "")
                Accessible.focusable: enabled
                Accessible.focused: cursorFocused
                Accessible.selectable: true
                Accessible.selected: highlighted
                Accessible.onPressAction: resultsList.activateRow(modelData.key, Qt.OtherFocusReason)
                onClicked: resultsList.activateRow(modelData.key, Qt.MouseFocusReason)
                background: Rectangle {
                    color: rowDelegate.highlighted ? root.contract.surfaceColor : "transparent"
                    radius: root.contract.radiusSmall
                    border.width: rowDelegate.cursorFocused ? 3 : (rowDelegate.highlighted ? 1 : 0)
                    border.color: rowDelegate.cursorFocused ? root.contract.focusColor : root.contract.accentColor
                }
                contentItem: Column {
                    spacing: root.contract.spacingUnit
                    Text {
                        objectName: "lidarr-result-title-" + rowDelegate.index
                        width: parent.width
                        text: rowDelegate.modelData.title
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: root.contract.textPrimaryColor
                        font.bold: true
                        Accessible.ignored: true
                    }
                    Text {
                        objectName: "lidarr-result-detail-" + rowDelegate.index
                        width: parent.width
                        text: rowDelegate.modelData.artist + (rowDelegate.modelData.year.length > 0 ? " · " + rowDelegate.modelData.year : "") + (rowDelegate.libraryStatus.length > 0 ? " · " + rowDelegate.libraryStatus : "")
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: root.contract.textSecondaryColor
                        Accessible.ignored: true
                    }
                }
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.connected
        }
        Text {
            objectName: "lidarr-status"
            Layout.fillWidth: true
            text: root.lidarr !== null ? root.lidarr.statusText : ""
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            elide: Text.ElideRight
            color: root.contract.textPrimaryColor
            Accessible.name: text
        }
        RowLayout {
            Layout.fillWidth: true
            AccessibleTransportButton {
                objectName: "lidarr-cancel"
                contract: root.contract
                text: qsTr("Cancelar operação")
                visible: root.busy
                enabled: root.interactionAllowed && root.busy
                onClicked: root.lidarr.cancel()
            }
            Item {
                Layout.fillWidth: true
            }
            AccessibleTransportButton {
                objectName: "lidarr-request"
                contract: root.contract
                text: qsTr("Solicitar download")
                visible: root.connected
                enabled: root.interactionAllowed && !root.busy && root.selectedResult !== null && root.selectedResult.kind === "album"
                onClicked: root.requestConfirmation()
            }
            AccessibleTransportButton {
                objectName: "lidarr-close"
                contract: root.contract
                text: qsTr("Fechar")
                onClicked: root.close()
            }
        }
    }

    Dialog {
        id: confirmation
        objectName: "lidarr-confirmation"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(620, parent ? parent.width - root.contract.spacingUnit * 8 : 620)
        modal: true
        focus: true
        popupType: Popup.Item
        closePolicy: Popup.CloseOnEscape
        padding: root.contract.spacingUnit * 3
        onClosed: root.confirmationRow = null
        background: Rectangle {
            color: root.contract.surfaceRaisedColor
            radius: root.contract.radiusMedium
            border.width: 1
            border.color: root.contract.borderColor
        }
        contentItem: ColumnLayout {
            spacing: root.contract.spacingUnit * 2
            Accessible.role: Accessible.Dialog
            Accessible.name: qsTr("Solicitar download no Lidarr")
            Text {
                Layout.fillWidth: true
                text: qsTr("Solicitar download no Lidarr")
                textFormat: Text.PlainText
                font.bold: true
                font.pixelSize: 20
                color: root.contract.textPrimaryColor
                Accessible.ignored: true
            }
            Text {
                objectName: "lidarr-confirm-title"
                Layout.fillWidth: true
                text: root.confirmationRow !== null ? root.confirmationRow.title : ""
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                color: root.contract.textPrimaryColor
                font.bold: true
                Accessible.name: text
            }
            Text {
                Layout.fillWidth: true
                text: root.confirmationRow !== null ? root.confirmationRow.artist : ""
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: root.contract.textSecondaryColor
                Accessible.name: text
            }
            Text {
                objectName: "lidarr-confirm-advice"
                Layout.fillWidth: true
                text: root.confirmationRow === null ? "" : (root.confirmationRow.existingId > 0 ? qsTr("O Lidarr buscará o álbum completo já cadastrado. Isso pode iniciar um download.") : qsTr("O Lidarr cadastrará este álbum e, se necessário, seu artista. A busca do álbum completo pode iniciar um download."))
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: root.contract.textPrimaryColor
                Accessible.name: text
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: root.confirmationRow !== null && root.confirmationRow.existingId === 0
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Pasta e perfis configurados no servidor Lidarr")
                    textFormat: Text.PlainText
                    color: root.contract.textSecondaryColor
                    Accessible.name: text
                }
                ServerChoice {
                    id: rootFolderChoice
                    objectName: "lidarr-root-folder"
                    contract: root.contract
                    Layout.fillWidth: true
                    implicitHeight: root.contract.controlTargetPreferred
                    model: root.lidarr !== null ? root.lidarr.rootFolders : []
                    Accessible.name: qsTr("Pasta no servidor Lidarr")
                }
                RowLayout {
                    Layout.fillWidth: true
                    ServerChoice {
                        id: qualityChoice
                        objectName: "lidarr-quality-profile"
                        contract: root.contract
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        implicitHeight: root.contract.controlTargetPreferred
                        model: root.lidarr !== null ? root.lidarr.qualityProfiles : []
                        textRole: "name"
                        valueRole: "id"
                        Accessible.name: qsTr("Perfil de qualidade")
                    }
                    ServerChoice {
                        id: metadataChoice
                        objectName: "lidarr-metadata-profile"
                        contract: root.contract
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        implicitHeight: root.contract.controlTargetPreferred
                        model: root.lidarr !== null ? root.lidarr.metadataProfiles : []
                        textRole: "name"
                        valueRole: "id"
                        Accessible.name: qsTr("Perfil de metadados")
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: rootFolderChoice.count === 0 || qualityChoice.count === 0 || metadataChoice.count === 0
                    text: qsTr("Configure uma pasta e os perfis no Lidarr e reconecte para solicitar este álbum.")
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    color: root.contract.textPrimaryColor
                    Accessible.name: text
                }
            }
            Text {
                Layout.fillWidth: true
                visible: root.confirmationRow !== null && !root.confirmationCurrent
                text: qsTr("Esta seleção não está mais disponível. Cancele e escolha o álbum novamente.")
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: root.contract.textPrimaryColor
                Accessible.name: text
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                AccessibleTransportButton {
                    contract: root.contract
                    text: qsTr("Cancelar")
                    onClicked: confirmation.reject()
                }
                AccessibleActionButton {
                    id: confirmSubmit
                    controlId: "lidarr-confirm-submit"
                    contract: root.contract
                    text: qsTr("Solicitar")
                    enabled: root.confirmationCurrent && (root.confirmationRow.existingId > 0 || (rootFolderChoice.currentIndex >= 0 && rootFolderChoice.count > 0 && qualityChoice.currentIndex >= 0 && qualityChoice.count > 0 && metadataChoice.currentIndex >= 0 && metadataChoice.count > 0))
                    onClicked: {
                        if (!confirmSubmit.enabled || root.lidarr === null)
                            return;
                        const row = root.confirmationRow;
                        const folder = rootFolderChoice.currentText;
                        const quality = qualityChoice.currentValue === undefined ? 0 : qualityChoice.currentValue;
                        const metadata = metadataChoice.currentValue === undefined ? 0 : metadataChoice.currentValue;
                        confirmation.accept();
                        if (root.lidarr === row.owner)
                            row.owner.requestDownload(row.key, folder, quality, metadata);
                    }
                }
            }
        }
    }
}
