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
    property bool pasteFromLetras: false
    property var stagedVersions: []
    property var draftOwner: null
    property string draftToken: ""
    property var discardTarget: null
    property bool discardRevoked: false
    readonly property bool requestCurrent: requestOwner !== null && requestOwner === backend && previewToken.length > 0 && requestOwner.hasRequest && requestOwner.requestToken === previewToken
    readonly property bool actionsCurrent: opened && enabled && requestCurrent
    readonly property string pendingDiscardToken: requestCurrent && !requestOwner.busy && typeof requestOwner.pendingDiscardToken === "string" ? requestOwner.pendingDiscardToken : ""
    readonly property bool discardTargetCurrent: actionsCurrent && discardTarget !== null && discardTarget.owner === requestOwner && discardTarget.requestToken === previewToken && discardTarget.token === pendingDiscardToken
    readonly property bool pasteAvailable: requestCurrent && requestOwner.canSavePasted === true
    readonly property bool hasLyricsPage: requestCurrent && requestOwner.hasLyricsPage === true
    readonly property bool versionFieldsEditable: actionsCurrent && (pasteMode ? pasteAvailable : !protectedLyrics && !requestOwner.busy)
    readonly property var versionKinds: ["original", "romanized", "translation"]
    readonly property string versionKind: versionKinds[kindField.currentIndex] ?? ""
    readonly property bool draftReady: nameField.text.trim().length > 0 && versionKind.length > 0 && pastedField.text.trim().length > 0
    readonly property bool draftEmpty: !pastedField.text.trim() && !nameField.text.trim() && !languageField.text.trim() && !scriptField.text.trim() && !versionKind
    readonly property bool metadataCurrent: requestCurrent && titleField.text.trim() === queryTitle && artistField.text.trim() === queryArtist && requestOwner.title.trim() === queryTitle && requestOwner.artist.trim() === queryArtist
    readonly property var candidateRows: requestCurrent ? requestOwner.candidates : []
    readonly property var selectedCandidate: candidateField.currentIndex >= 0 && candidateField.currentIndex < candidateRows.length ? candidateRows[candidateField.currentIndex] : null
    readonly property bool protectedLyrics: requestCurrent && requestOwner.lyricsCopyProtected === true
    readonly property Item afterPageLink: pasteMode ? pastedField : candidateField.enabled ? candidateField : protectedLyrics ? providerLink : previewField

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
        const restoreDraft = draftOwner === requestOwner && draftToken === previewToken && stagedVersions.length > 0;
        queryTitle = backend !== null ? backend.title.trim() : "";
        queryArtist = backend !== null ? backend.artist.trim() : "";
        titleField.text = backend !== null ? backend.title : "";
        artistField.text = backend !== null ? backend.artist : "";
        if (!restoreDraft) {
            resetPastedForm();
            pasteMode = false;
            pasteFromLetras = false;
            stagedVersions = [];
        }
        draftOwner = requestOwner;
        draftToken = previewToken;
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
    function pastedVersion() {
        return {
            content: pastedField.text,
            name: nameField.text.trim(),
            language: languageField.text.trim(),
            script: scriptField.text.trim(),
            kind: versionKind
        };
    }
    function stageVersion() {
        if (!stageButton.enabled || !actionsCurrent)
            return;
        stagedVersions = stagedVersions.concat([pastedVersion()]);
        resetPastedForm();
        pastedField.forceActiveFocus(Qt.TabFocusReason);
    }
    function resetPastedForm() {
        pastedField.clear();
        nameField.clear();
        languageField.clear();
        scriptField.clear();
        kindField.currentIndex = -1;
    }
    function saveVersion() {
        if (!saveButton.enabled || !actionsCurrent)
            return;
        const version = pastedVersion();
        if (pasteMode) {
            const owner = requestOwner;
            const token = previewToken;
            const accepted = stagedVersions.length > 0 ? owner.saveLetrasVersions(token, draftReady ? stagedVersions.concat([version]) : stagedVersions) : owner.savePasted(token, version.content, version.name, version.language, version.script, version.kind, hasLyricsPage && pasteFromLetras);
            if (accepted && requestOwner === owner && previewToken === token) {
                if (stagedVersions.length > 0) {
                    stagedVersions = [];
                    resetPastedForm();
                }
                pasteFromLetras = false;
            }
        } else if (metadataCurrent && selectedCandidate !== null) {
            requestOwner.saveCandidate(selectedCandidate.index, version.name, version.language, version.script, version.kind);
        }
    }
    function openDiscardConfirmation() {
        if (!discardButton.enabled || discardConfirmation.visible)
            return;
        discardRevoked = false;
        discardTarget = {
            owner: requestOwner,
            requestToken: previewToken,
            token: pendingDiscardToken
        };
        discardConfirmation.open();
    }
    function confirmDiscard() {
        if (!discardConfirmation.opened || !discardTargetCurrent || discardRevoked)
            return;
        const target = discardTarget;
        if (target.owner.discardPendingSave(target.requestToken, target.token) === true)
            discardConfirmation.close();
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
    onDiscardTargetCurrentChanged: {
        if (!discardTargetCurrent && discardConfirmation.visible) {
            discardRevoked = true;
            const target = discardTarget;
            Qt.callLater(function () {
                if (root.discardTarget === target && discardConfirmation.visible)
                    discardConfirmation.close();
            });
        }
    }
    onAboutToShow: capturePreview()
    onAboutToHide: discardConfirmation.close()
    onOpened: {
        if (!requestCurrent) {
            revokePreview();
            return;
        }
        const field = pasteMode ? pastedField : titleField;
        field.forceActiveFocus(Qt.TabFocusReason);
    }
    onClosed: {
        requestOwner = null;
        previewToken = "";
        if (stagedVersions.length === 0 || draftOwner !== backend || backend === null || !backend.hasRequest || draftToken !== backend.requestToken) {
            draftOwner = null;
            draftToken = "";
            stagedVersions = [];
            pasteMode = false;
            pasteFromLetras = false;
            resetPastedForm();
        }
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
                    KeyNavigation.tab: searchButton.enabled ? searchButton : pasteButton.enabled ? pasteButton : pageButton.enabled ? pageButton : root.afterPageLink
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
                        KeyNavigation.tab: pasteButton.enabled ? pasteButton : pageButton.enabled ? pageButton : root.afterPageLink
                        onClicked: root.search()
                    }
                    AccessibleTransportButton {
                        id: pasteButton
                        objectName: "download-lyrics-paste"
                        contract: root.contract
                        text: root.pasteMode ? qsTr("Voltar à busca") : qsTr("Colar letra")
                        visible: root.pasteMode || root.pasteAvailable
                        enabled: visible && root.actionsCurrent && root.stagedVersions.length === 0
                        KeyNavigation.tab: pageButton.enabled ? pageButton : root.afterPageLink
                        onClicked: root.togglePaste()
                    }
                    AccessibleTransportButton {
                        id: pageButton
                        objectName: "download-lyrics-open-letras-page"
                        contract: root.contract
                        text: qsTr("Abrir no Letras.mus.br")
                        visible: root.hasLyricsPage
                        enabled: visible && root.actionsCurrent
                        KeyNavigation.tab: root.afterPageLink
                        onClicked: {
                            if (enabled && root.actionsCurrent)
                                root.requestOwner.openLyricsPage(root.previewToken);
                        }
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
                    KeyNavigation.tab: root.protectedLyrics ? providerLink : previewField
                    onCountChanged: {
                        if (count > 0 && currentIndex < 0)
                            currentIndex = 0;
                    }
                }
                LyricsText {
                    id: previewField
                    objectName: "download-lyrics-preview"
                    visible: !root.pasteMode && !root.protectedLyrics
                    text: !root.protectedLyrics && root.selectedCandidate !== null ? root.selectedCandidate.text : ""
                    readOnly: true
                    Accessible.name: qsTr("Prévia da letra")
                    KeyNavigation.tab: nameField
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: !root.pasteMode && root.protectedLyrics
                    Text {
                        objectName: "download-lyrics-provider-credit"
                        Layout.fillWidth: true
                        text: root.protectedLyrics ? root.requestOwner.lyricsAttribution : ""
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        color: root.contract.textSecondaryColor
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                    }
                    AccessibleTransportButton {
                        id: providerLink
                        objectName: "download-lyrics-provider-link"
                        contract: root.contract
                        text: qsTr("LyricFind")
                        KeyNavigation.tab: dismissButton
                        onClicked: {
                            if (root.actionsCurrent && root.protectedLyrics)
                                Qt.openUrlExternally("https://www.lyricfind.com/");
                        }
                    }
                }
                Text {
                    objectName: "download-lyrics-protected-preview"
                    Layout.fillWidth: true
                    visible: !root.pasteMode && root.protectedLyrics
                    text: visible && root.selectedCandidate !== null ? root.selectedCandidate.text : ""
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: root.contract.textPrimaryColor
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
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
                    KeyNavigation.priority: KeyNavigation.BeforeItem
                    KeyNavigation.tab: pageSource.enabled ? pageSource : nameField
                }
                PreferenceToggle {
                    id: pageSource
                    Layout.fillWidth: true
                    contract: root.contract
                    controlId: "download-lyrics-pasted-letras-source"
                    text: qsTr("Usar página do Letras como origem")
                    value: root.pasteFromLetras
                    visible: root.pasteMode && root.hasLyricsPage
                    enabled: visible && root.versionFieldsEditable && root.stagedVersions.length === 0
                    onValueEdited: checked => root.pasteFromLetras = checked
                    KeyNavigation.tab: nameField
                }
                FieldLabel {
                    visible: root.pasteMode || !root.protectedLyrics
                    text: qsTr("Nome da versão")
                }
                FormInput {
                    id: nameField
                    visible: root.pasteMode || !root.protectedLyrics
                    objectName: "download-lyrics-version-name"
                    Accessible.name: qsTr("Nome da versão")
                    placeholderText: qsTr("Ex.: Japonês original ou Romaji")
                    KeyNavigation.tab: languageField
                }
                GridLayout {
                    visible: root.pasteMode || !root.protectedLyrics
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
                    visible: root.pasteMode || !root.protectedLyrics
                    text: qsTr("Forma da versão")
                }
                FormChoice {
                    id: kindField
                    visible: root.pasteMode || !root.protectedLyrics
                    objectName: "download-lyrics-kind"
                    model: [qsTr("Original"), qsTr("Romanizada"), qsTr("Tradução")]
                    currentIndex: -1
                    displayText: currentIndex < 0 ? qsTr("Escolha a forma") : currentText
                    enabled: root.versionFieldsEditable
                    Accessible.name: qsTr("Forma da versão")
                    KeyNavigation.tab: stageButton.enabled ? stageButton : undoStageButton.enabled ? undoStageButton : saveButton
                }
                FieldLabel {
                    visible: root.pasteMode && root.stagedVersions.length > 0
                    text: qsTr("A guardar: %1").arg(root.stagedVersions.map(version => version.name).join(", "))
                }
                RowLayout {
                    visible: root.pasteMode && root.hasLyricsPage && root.pasteFromLetras
                    Layout.fillWidth: true
                    spacing: root.contract.spacingUnit * 2
                    AccessibleTransportButton {
                        id: stageButton
                        objectName: "download-lyrics-stage-version"
                        contract: root.contract
                        text: qsTr("Colar outra versão")
                        enabled: visible && root.versionFieldsEditable && root.draftReady && root.stagedVersions.length < 15
                        KeyNavigation.tab: undoStageButton.enabled ? undoStageButton : saveButton
                        onClicked: root.stageVersion()
                    }
                    AccessibleTransportButton {
                        id: undoStageButton
                        objectName: "download-lyrics-undo-staged-version"
                        contract: root.contract
                        text: qsTr("Editar a última adicionada")
                        visible: root.stagedVersions.length > 0
                        enabled: visible && root.versionFieldsEditable && root.draftEmpty
                        KeyNavigation.tab: saveButton
                        onClicked: {
                            const last = root.stagedVersions[root.stagedVersions.length - 1];
                            root.stagedVersions = root.stagedVersions.slice(0, -1);
                            pastedField.text = last.content;
                            nameField.text = last.name;
                            languageField.text = last.language;
                            scriptField.text = last.script;
                            kindField.currentIndex = root.versionKinds.indexOf(last.kind);
                            pastedField.forceActiveFocus(Qt.TabFocusReason);
                        }
                    }
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
                KeyNavigation.tab: discardButton.enabled ? discardButton : dismissButton
                onClicked: {
                    if (enabled && root.actionsCurrent)
                        root.requestOwner.retrySave();
                }
            }
            AccessibleTransportButton {
                id: discardButton
                objectName: "download-lyrics-discard"
                contract: root.contract
                text: qsTr("Descartar tentativa")
                visible: root.pendingDiscardToken.length > 0
                enabled: visible && root.actionsCurrent
                KeyNavigation.tab: dismissButton
                onClicked: root.openDiscardConfirmation()
            }
            Item {
                Layout.fillWidth: true
            }
            AccessibleTransportButton {
                id: dismissButton
                objectName: "download-lyrics-dismiss"
                contract: root.contract
                text: qsTr("Concluir revisão")
                enabled: root.actionsCurrent && root.requestOwner.canDismiss && root.stagedVersions.length === 0
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
                text: root.pasteMode && root.stagedVersions.length > 0 ? qsTr("Guardar versões") : qsTr("Guardar como nova versão")
                enabled: root.versionFieldsEditable && (root.pasteMode ? root.draftReady || (root.stagedVersions.length > 0 && root.draftEmpty) : root.versionKind.length > 0 && nameField.text.trim().length > 0 && root.metadataCurrent && root.requestOwner.canSave && root.selectedCandidate !== null)
                KeyNavigation.tab: retrySaveButton.enabled ? retrySaveButton : discardButton.enabled ? discardButton : dismissButton
                onClicked: root.saveVersion()
            }
        }
    }
    Popup {
        id: discardConfirmation
        objectName: "download-lyrics-discard-confirmation"
        parent: root.parent
        anchors.centerIn: parent
        width: Math.min(root.width, 480)
        height: discardContent.implicitHeight + padding * 2
        padding: root.contract.spacingUnit * 3
        closePolicy: Popup.CloseOnEscape
        modal: true
        dim: false
        focus: true
        popupType: Popup.Item
        onOpened: cancelDiscardButton.forceActiveFocus(Qt.TabFocusReason)
        onClosed: {
            const target = root.discardTarget;
            root.discardTarget = null;
            Qt.callLater(function () {
                if (!root.actionsCurrent || target === null || root.requestOwner !== target.owner || root.previewToken !== target.requestToken || discardConfirmation.visible)
                    return;
                const focusItem = discardButton.enabled ? discardButton : root.pasteMode ? pastedField : titleField;
                if (focusItem.visible && focusItem.enabled)
                    focusItem.forceActiveFocus(Qt.TabFocusReason);
            });
        }
        background: Rectangle {
            color: root.contract.surfaceRaisedColor
            radius: root.contract.radiusMedium
            border.width: root.contract.highContrast ? 2 : 1
            border.color: root.contract.borderColor
        }
        contentItem: ColumnLayout {
            id: discardContent
            spacing: root.contract.spacingUnit * 2
            Accessible.role: Accessible.Dialog
            Accessible.name: qsTr("Descartar tentativa de guardar letra")
            FieldLabel {
                text: qsTr("Descartar tentativa de guardar letra?")
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("O áudio e as versões já guardadas serão preservados. Você pode continuar esta revisão.")
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: root.contract.textPrimaryColor
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                AccessibleTransportButton {
                    id: cancelDiscardButton
                    objectName: "download-lyrics-discard-cancel"
                    contract: root.contract
                    text: qsTr("Cancelar")
                    KeyNavigation.tab: confirmDiscardButton
                    KeyNavigation.backtab: confirmDiscardButton
                    onClicked: discardConfirmation.close()
                }
                AccessibleTransportButton {
                    id: confirmDiscardButton
                    objectName: "download-lyrics-discard-confirm"
                    contract: root.contract
                    text: qsTr("Descartar tentativa")
                    enabled: root.discardTargetCurrent && !root.discardRevoked
                    KeyNavigation.tab: cancelDiscardButton
                    KeyNavigation.backtab: cancelDiscardButton
                    onClicked: root.confirmDiscard()
                }
            }
        }
    }
}
