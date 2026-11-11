pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

Dialog {
    id: root
    required property ShellUiContract contract
    property var backend: null
    property Item restoreFocusItem: null
    property var requestOwner: null
    property string previewToken: ""
    property string queryTitle: ""
    property string queryArtist: ""
    property bool initializing: false
    property bool pasteMode: false
    readonly property bool requestCurrent: requestOwner !== null && requestOwner === backend && previewToken.length > 0 && requestOwner.hasRequest && requestOwner.requestToken === previewToken
    readonly property bool actionsCurrent: opened && enabled && requestCurrent
    readonly property bool pasteAvailable: requestCurrent && requestOwner.canSavePasted === true
    readonly property bool versionFieldsEditable: actionsCurrent && (pasteMode ? pasteAvailable : !requestOwner.busy)
    readonly property bool metadataCurrent: requestCurrent && titleField.text.trim() === queryTitle && artistField.text.trim() === queryArtist && requestOwner.title.trim() === queryTitle && requestOwner.artist.trim() === queryArtist
    readonly property var candidateRows: requestCurrent ? requestOwner.candidates : []
    readonly property var selectedCandidate: candidateField.currentIndex >= 0 && candidateField.currentIndex < candidateRows.length ? candidateRows[candidateField.currentIndex] : null

    objectName: "download-lyrics-panel"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(640, parent ? parent.width - root.contract.spacingUnit * 4 : 640)
    height: Math.min(480, parent ? parent.height - root.contract.spacingUnit * 4 : 480)
    padding: root.contract.spacingUnit * 3
    modal: true
    focus: true
    popupType: Popup.Item
    closePolicy: Popup.CloseOnEscape

    function revokePreview() {
        requestOwner = null;
        previewToken = "";
        if (visible)
            close();
    }
    function capturePreview() {
        initializing = true;
        requestOwner = backend;
        previewToken = backend !== null && backend.hasRequest ? backend.requestToken : "";
        queryTitle = backend !== null ? backend.title.trim() : "";
        queryArtist = backend !== null ? backend.artist.trim() : "";
        titleField.text = backend !== null ? backend.title : "";
        artistField.text = backend !== null ? backend.artist : "";
        nameField.text = "";
        languageField.text = "";
        scriptField.text = "";
        pasteMode = false;
        pastedField.text = "";
        kindField.currentIndex = -1;
        candidateField.currentIndex = candidateRows.length > 0 ? 0 : -1;
        initializing = false;
    }
    function search() {
        if (!searchButton.enabled || !actionsCurrent)
            return;
        const owner = requestOwner;
        const token = previewToken;
        const title = titleField.text.trim();
        const artist = artistField.text.trim();
        if (owner.retry(title, artist) !== true || !actionsCurrent || requestOwner !== owner || previewToken !== token)
            return;
        queryTitle = title;
        queryArtist = artist;
    }
    function togglePaste() {
        if (!actionsCurrent || !pasteButton.enabled)
            return;
        const owner = requestOwner;
        const token = previewToken;
        pasteMode = !pasteMode;
        Qt.callLater(function () {
            if (!root.actionsCurrent || root.requestOwner !== owner || root.previewToken !== token)
                return;
            const target = root.pasteMode ? pastedField : titleField;
            if (target.visible && target.enabled)
                target.forceActiveFocus(Qt.TabFocusReason);
        });
    }
    function saveVersion() {
        if (!saveButton.enabled || !actionsCurrent)
            return;
        const kind = kindField.currentIndex === 0 ? "original" : kindField.currentIndex === 1 ? "romanized" : "";
        if (kind.length === 0)
            return;
        const name = nameField.text.trim();
        const language = languageField.text.trim();
        const script = scriptField.text.trim();
        if (pasteMode) {
            if (pasteAvailable)
                requestOwner.savePasted(previewToken, pastedField.text, name, language, script, kind);
        } else if (metadataCurrent && selectedCandidate !== null) {
            requestOwner.saveCandidate(selectedCandidate.index, name, language, script, kind);
        }
    }
    onBackendChanged: {
        if (visible && !initializing)
            revokePreview();
    }
    onRequestCurrentChanged: {
        if (visible && !initializing && !requestCurrent) {
            // Actions already fail the identity guard. Defer changing its
            // inputs until the binding has finished notifying this change.
            Qt.callLater(function () {
                if (root.visible && !root.initializing && !root.requestCurrent)
                    root.revokePreview();
            });
        }
    }
    onEnabledChanged: {
        if (!enabled)
            revokePreview();
    }
    onAboutToShow: capturePreview()
    onOpened: {
        if (!requestCurrent) {
            revokePreview();
            return;
        }
        titleField.forceActiveFocus(Qt.TabFocusReason);
    }
    onClosed: {
        requestOwner = null;
        previewToken = "";
        pasteMode = false;
        pastedField.text = "";
        const target = restoreFocusItem;
        Qt.callLater(function () {
            if (target && target.visible && target.enabled)
                target.forceActiveFocus(Qt.TabFocusReason);
        });
    }

    component FieldLabel: Text {
        Layout.fillWidth: true
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        color: root.contract.textPrimaryColor
        Accessible.ignored: true
    }
    component FormInput: TextField {
        id: input
        Layout.fillWidth: true
        implicitHeight: root.contract.controlTargetPreferred
        maximumLength: 128
        enabled: root.versionFieldsEditable
        activeFocusOnTab: true
        selectByMouse: true
        color: root.contract.textPrimaryColor
        placeholderTextColor: root.contract.textSecondaryColor
        selectionColor: root.contract.accentColor
        selectedTextColor: root.contract.accentTextColor
        Accessible.role: Accessible.EditableText
        Accessible.id: objectName
        Accessible.focusable: enabled
        Accessible.focused: activeFocus
        background: Rectangle {
            color: root.contract.surfaceColor
            radius: root.contract.radiusSmall
            border.width: input.activeFocus ? 3 : 1
            border.color: input.activeFocus ? root.contract.focusColor : root.contract.borderColor
        }
    }
    component FormChoice: ComboBox {
        id: choice
        Layout.fillWidth: true
        implicitHeight: root.contract.controlTargetPreferred
        activeFocusOnTab: true
        Accessible.id: objectName
        Accessible.focusable: enabled
        Accessible.focused: activeFocus
        contentItem: Text {
            text: choice.displayText
            textFormat: Text.PlainText
            color: root.contract.textPrimaryColor
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        delegate: ItemDelegate {
            id: row
            required property int index
            width: choice.width
            text: choice.textAt(row.index)
            highlighted: choice.highlightedIndex === row.index
            contentItem: Text {
                text: row.text
                textFormat: Text.PlainText
                color: root.contract.textPrimaryColor
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }
        background: Rectangle {
            color: root.contract.surfaceColor
            radius: root.contract.radiusSmall
            border.width: choice.activeFocus ? 3 : 1
            border.color: choice.activeFocus ? root.contract.focusColor : root.contract.borderColor
        }
    }
    component LyricsText: TextArea {
        id: input
        Layout.fillWidth: true
        Layout.preferredHeight: 180
        textFormat: TextEdit.PlainText
        selectByMouse: true
        activeFocusOnTab: visible && enabled
        wrapMode: TextEdit.Wrap
        color: root.contract.textPrimaryColor
        placeholderTextColor: root.contract.textSecondaryColor
        selectionColor: root.contract.accentColor
        selectedTextColor: root.contract.accentTextColor
        Accessible.id: objectName
        background: Rectangle {
            color: root.contract.surfaceColor
            radius: root.contract.radiusSmall
            border.width: input.activeFocus ? 3 : 1
            border.color: input.activeFocus ? root.contract.focusColor : root.contract.borderColor
        }
    }
    background: Rectangle {
        color: root.contract.surfaceRaisedColor
        radius: root.contract.radiusMedium
        border.width: root.contract.highContrast ? 2 : 1
        border.color: root.contract.borderColor
    }
    contentItem: ColumnLayout {
        spacing: root.contract.spacingUnit * 2
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Letra do áudio baixado")
        FieldLabel {
            text: qsTr("Letra do áudio baixado")
            font.bold: true
            font.pixelSize: 20
        }
        Text {
            objectName: "download-lyrics-status"
            Layout.fillWidth: true
            text: root.requestCurrent ? root.requestOwner.statusText : ""
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: root.contract.textSecondaryColor
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        ScrollView {
            id: formScroll
            objectName: "download-lyrics-scroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical: AccessibleScrollBar {
                contract: root.contract
            }
            ColumnLayout {
                width: formScroll.availableWidth
                spacing: root.contract.spacingUnit * 2
                FieldLabel {
                    text: qsTr("Título da música")
                    visible: !root.pasteMode
                }
                FormInput {
                    id: titleField
                    objectName: "download-lyrics-title"
                    maximumLength: 512
                    visible: !root.pasteMode
                    Accessible.name: qsTr("Título da música")
                    KeyNavigation.tab: artistField
                }
                FieldLabel {
                    text: qsTr("Artista")
                    visible: !root.pasteMode
                }
                FormInput {
                    id: artistField
                    objectName: "download-lyrics-artist"
                    maximumLength: 512
                    visible: !root.pasteMode
                    Accessible.name: qsTr("Artista")
                    KeyNavigation.tab: searchButton.enabled ? searchButton : pasteButton.enabled ? pasteButton : candidateField
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: root.contract.spacingUnit * 2
                    AccessibleTransportButton {
                        id: searchButton
                        objectName: "download-lyrics-search"
                        contract: root.contract
                        text: qsTr("Buscar letra")
                        visible: !root.pasteMode
                        enabled: visible && root.actionsCurrent && !root.requestOwner.busy && root.requestOwner.canRetry && titleField.text.trim().length > 0 && artistField.text.trim().length > 0
                        KeyNavigation.tab: pasteButton.enabled ? pasteButton : candidateField
                        onClicked: root.search()
                    }
                    AccessibleTransportButton {
                        id: pasteButton
                        objectName: "download-lyrics-paste"
                        contract: root.contract
                        text: root.pasteMode ? qsTr("Voltar à busca") : qsTr("Colar letra")
                        visible: root.pasteMode || root.pasteAvailable
                        enabled: visible && root.actionsCurrent
                        KeyNavigation.tab: root.pasteMode ? pastedField : candidateField.enabled ? candidateField : previewField
                        onClicked: root.togglePaste()
                    }
                }
                FieldLabel {
                    text: qsTr("Versão encontrada")
                    visible: !root.pasteMode
                }
                FormChoice {
                    id: candidateField
                    objectName: "download-lyrics-candidates"
                    model: root.candidateRows
                    textRole: "label"
                    visible: !root.pasteMode
                    enabled: visible && root.actionsCurrent && count > 0
                    Accessible.name: qsTr("Versão encontrada")
                    KeyNavigation.tab: previewField
                    onCountChanged: {
                        if (count > 0 && currentIndex < 0)
                            currentIndex = 0;
                    }
                }
                LyricsText {
                    id: previewField
                    objectName: "download-lyrics-preview"
                    visible: !root.pasteMode
                    text: root.selectedCandidate !== null ? root.selectedCandidate.text : ""
                    readOnly: true
                    Accessible.name: qsTr("Prévia da letra")
                    KeyNavigation.tab: nameField
                }
                FieldLabel {
                    text: qsTr("Letra em texto ou LRC")
                    visible: root.pasteMode
                }
                LyricsText {
                    id: pastedField
                    objectName: "download-lyrics-pasted-content"
                    visible: root.pasteMode
                    enabled: root.actionsCurrent
                    readOnly: !root.pasteAvailable
                    placeholderText: qsTr("Cole a letra aqui")
                    Accessible.name: qsTr("Letra em texto ou LRC")
                    KeyNavigation.tab: nameField
                }
                FieldLabel {
                    text: qsTr("Nome da versão")
                }
                FormInput {
                    id: nameField
                    objectName: "download-lyrics-version-name"
                    Accessible.name: qsTr("Nome da versão")
                    placeholderText: qsTr("Ex.: Japonês original ou Romaji")
                    KeyNavigation.tab: languageField
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: root.contract.spacingUnit * 2
                    FieldLabel {
                        text: qsTr("Idioma (opcional)")
                    }
                    FieldLabel {
                        text: qsTr("Escrita (opcional)")
                    }
                    FormInput {
                        id: languageField
                        objectName: "download-lyrics-language"
                        maximumLength: 64
                        Accessible.name: qsTr("Idioma da letra")
                        placeholderText: qsTr("Ex.: ja")
                        KeyNavigation.tab: scriptField
                    }
                    FormInput {
                        id: scriptField
                        objectName: "download-lyrics-script"
                        maximumLength: 64
                        Accessible.name: qsTr("Escrita da letra")
                        placeholderText: qsTr("Ex.: Jpan ou Latn")
                        KeyNavigation.tab: kindField
                    }
                }
                FieldLabel {
                    text: qsTr("Forma da versão: escolha Original ou Romanizada.")
                }
                FormChoice {
                    id: kindField
                    objectName: "download-lyrics-kind"
                    model: [qsTr("Original"), qsTr("Romanizada")]
                    currentIndex: -1
                    displayText: currentIndex < 0 ? qsTr("Escolha a forma") : currentText
                    enabled: root.versionFieldsEditable
                    Accessible.name: qsTr("Forma da versão")
                    KeyNavigation.tab: saveButton
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: root.contract.spacingUnit * 2
            AccessibleTransportButton {
                id: retrySaveButton
                objectName: "download-lyrics-retry-save"
                contract: root.contract
                text: qsTr("Tentar guardar novamente")
                visible: root.requestCurrent && root.requestOwner.canRetrySave
                enabled: root.actionsCurrent && !root.requestOwner.busy && root.requestOwner.canRetrySave
                KeyNavigation.tab: dismissButton
                onClicked: {
                    if (enabled && root.actionsCurrent)
                        root.requestOwner.retrySave();
                }
            }
            Item {
                Layout.fillWidth: true
            }
            AccessibleTransportButton {
                id: dismissButton
                objectName: "download-lyrics-dismiss"
                contract: root.contract
                text: qsTr("Próxima")
                enabled: root.actionsCurrent && root.requestOwner.canDismiss
                KeyNavigation.tab: closeButton
                onClicked: {
                    if (enabled && root.actionsCurrent)
                        root.requestOwner.dismiss();
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: root.contract.spacingUnit * 2
            AccessibleTransportButton {
                id: closeButton
                objectName: "download-lyrics-close"
                contract: root.contract
                text: qsTr("Fechar")
                KeyNavigation.tab: root.pasteMode ? pasteButton : titleField
                onClicked: root.close()
            }
            Item {
                Layout.fillWidth: true
            }
            AccessibleActionButton {
                id: saveButton
                controlId: "download-lyrics-save"
                contract: root.contract
                text: qsTr("Guardar como nova versão")
                enabled: root.versionFieldsEditable && nameField.text.trim().length > 0 && (kindField.currentIndex === 0 || kindField.currentIndex === 1) && (root.pasteMode ? pastedField.text.trim().length > 0 : root.metadataCurrent && root.requestOwner.canSave && root.selectedCandidate !== null)
                KeyNavigation.tab: retrySaveButton.visible ? retrySaveButton : dismissButton
                onClicked: root.saveVersion()
            }
        }
    }
}
