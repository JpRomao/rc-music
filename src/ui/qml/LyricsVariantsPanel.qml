pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as NativeDialogs
import QtQuick.Layouts
import MusicPlayer.M0

Dialog {
    id: root
    objectName: "lyrics-variants-panel"
    required property ShellUiContract contract
    property var lyrics: null
    property string trackIdentity: ""
    property string queuedFeedbackText: ""
    property string formMode: ""
    property var formOwner: null
    property string formTrack: ""
    property string formLyricsRevision: ""
    property string editToken: ""
    property string editId: ""
    property bool lrcPickerArmed: false
    property int lrcImportSerial: 0
    property string lrcImportToken: ""
    property string lrcImportPreviousContent: ""
    property bool lrcImportRejected: false
    property string romajiToken: ""
    property string romajiRequestRevision: ""
    property var romajiRequestOwner: null
    property string queuedToken: ""
    property string queuedTitle: ""
    property string managerToken: ""
    property string managerTitle: ""
    property string managerArtist: ""
    property string managerAlbum: ""
    property double managerDurationMs: 0
    property string managerSelection: ""
    readonly property bool managingQueue: managerToken.length > 0
    readonly property var storedVariants: supported ? (managingQueue ? lyrics.queuedManagerVariants : lyrics.variants) : []
    property bool formRejected: false
    property string confirmMode: ""
    property var confirmOwner: null
    property string confirmTrack: ""
    property var confirmTarget: null
    readonly property bool supported: lyrics !== null && typeof lyrics.variants !== "undefined"
    readonly property bool busy: supported && (managingQueue ? lyrics.queuedManagerBusy : lyrics.variantsBusy)
    readonly property bool canEdit: visible && enabled && supported && (managingQueue ? lyrics.queuedManagerCanEdit : lyrics.variantsCanEdit) && !busy
    readonly property bool recordingSearchSupported: supported && (managingQueue ? typeof lyrics.searchQueuedRecordings === "function" : typeof lyrics.searchRecordings === "function")
    readonly property bool recordingSearchAvailable: visible && enabled && recordingSearchSupported && lyrics.recordingSearchBusy !== true && (managingQueue ? lyrics.queuedManagerSearchAvailable === true : !lyrics.busy && lyrics.canSearch)
    readonly property bool canSelect: visible && enabled && supported && (managingQueue || lyrics.variantsReady) && !busy && !lyrics.variantsHasPendingChange
    readonly property bool canReload: visible && enabled && supported && !busy
    readonly property bool noTrackToReload: !managingQueue && trackIdentity.length === 0
    readonly property string selectedId: supported ? (managingQueue ? managerSelection : lyrics.selectedVariantId) : ""
    readonly property var onlineCandidates: supported && typeof lyrics.onlineLyricsCandidates !== "undefined" ? lyrics.onlineLyricsCandidates : []
    readonly property int selectedOnlineCandidate: supported && typeof lyrics.selectedOnlineLyricsCandidate !== "undefined" ? lyrics.selectedOnlineLyricsCandidate : -1
    readonly property bool showOnlineCandidates: !managingQueue && selectedId.length === 0 && onlineCandidates.length > 1
    readonly property string currentLyricsRevision: supported && typeof lyrics.currentLyricsRevision !== "undefined" ? lyrics.currentLyricsRevision : ""
    readonly property bool canSaveCurrent: !managingQueue && canEdit && currentLyricsRevision.length > 0 && lyrics.canSaveCurrentLyrics
    readonly property string romajiSourceRevision: supported && typeof lyrics.romajiSourceRevision === "string" ? lyrics.romajiSourceRevision : ""
    readonly property string romajiStatusText: supported && typeof lyrics.romajiStatusText === "string" ? lyrics.romajiStatusText : ""
    readonly property bool canGenerateRomaji: !managingQueue && canEdit && romajiSourceRevision.length > 0 && lyrics.canGenerateRomaji === true
    readonly property var selectedVariant: variantForId(selectedId)
    readonly property var rows: variantRows()
    readonly property var versionKinds: [
        {
            label: qsTr("Original"),
            value: "original"
        },
        {
            label: qsTr("Romanizada"),
            value: "romanized"
        },
        {
            label: qsTr("Tradução"),
            value: "translation"
        }
    ]
    readonly property bool childDialogOpen: formDialog.visible || confirmation.visible
    readonly property bool textEntryMode: formMode === "add" || formMode === "queued" || formMode === "romaji" || formMode === "edit"
    readonly property bool editing: formMode === "edit"
    readonly property bool canEditSelected: canEdit && selectedVariant !== null && typeof lyrics.prepareVariantEdit === "function"
    readonly property bool lrcImportAvailable: !managingQueue && formMode === "add" && formOwner !== null && typeof formOwner.importVariantLrc === "function"
    readonly property bool queuedFormAvailable: enabled && supported && typeof lyrics.queueVariantAvailable === "boolean" && lyrics.queueVariantAvailable
    readonly property string actionTrack: managingQueue ? managerToken : trackIdentity
    readonly property bool formCurrent: formDialog.visible && formOwner === lyrics && (formMode === "queued" ? enabled && queuedToken.length > 0 : formTrack === actionTrack && canEdit && (formMode === "add" || (formMode === "current" && canSaveCurrent && formLyricsRevision === currentLyricsRevision) || (formMode === "romaji" && romajiToken.length > 0 && romajiRequestRevision === romajiSourceRevision) || (editing && editToken.length > 0 && selectedId === editId)))
    readonly property bool confirmCurrent: confirmation.visible && confirmOwner === lyrics && confirmTrack === actionTrack && (confirmMode === "reload" ? (canReload && lyrics.variantsHasPendingChange) : (canEdit && targetCurrent()))
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(660, parent ? parent.width - contract.spacingUnit * 8 : 660)
    height: Math.min(560, parent ? parent.height - contract.spacingUnit * 8 : 560)
    padding: contract.spacingUnit * 3
    modal: true
    focus: true
    popupType: Popup.Item
    closePolicy: Popup.CloseOnEscape
    signal queuedFormClosed(bool submitted)
    signal recordingSearchRequested(string token, string title, string artist, string album, double durationMs)
    onOpened: {
        variantList.restoreCursor(selectedId);
        if (canSelect)
            variantList.forceActiveFocus(Qt.TabFocusReason);
        else
            closeButton.forceActiveFocus(Qt.TabFocusReason);
    }
    onClosed: {
        if (formDialog && formMode !== "queued")
            formDialog.close();
        if (confirmation)
            confirmation.close();
        clearRomajiRequest();
        if (managingQueue) {
            const token = managerToken;
            managerToken = "";
            managerSelection = "";
            if (lyrics && typeof lyrics.closeQueuedManager === "function")
                lyrics.closeQueuedManager(token);
            queuedFormClosed(false);
        }
    }
    onLyricsChanged: revokeDialogs()
    onTrackIdentityChanged: {
        if (formMode !== "queued" && !managingQueue)
            revokeDialogs();
    }
    onCurrentLyricsRevisionChanged: {
        if (formMode === "current" && formLyricsRevision !== currentLyricsRevision && formDialog && formDialog.visible)
            formDialog.close();
    }
    onSelectedIdChanged: {
        if (editing && editId !== selectedId && formDialog.visible)
            formDialog.close();
    }
    onFormCurrentChanged: {
        if (!formCurrent)
            cancelLrcImport();
    }
    onRomajiSourceRevisionChanged: {
        if (romajiToken.length > 0 && romajiRequestRevision !== romajiSourceRevision) {
            if (formMode === "romaji" && formDialog.visible)
                formDialog.close();
            clearRomajiRequest();
        }
    }
    Connections {
        target: root.lyrics
        ignoreUnknownSignals: true
        function onRomajiDraftReady(token, content) {
            if (token !== root.romajiToken || root.romajiRequestOwner !== root.lyrics)
                return;
            if (!root.visible || !root.enabled || root.childDialogOpen || root.romajiRequestRevision !== root.romajiSourceRevision) {
                root.clearRomajiRequest();
                return;
            }
            root.startForm("romaji", content);
        }
        function onImportedVariantLrc(token, name, content) {
            root.completeLrcImport(token, name, content);
        }
    }
    onEnabledChanged: {
        if (!enabled)
            revokeDialogs();
    }
    function revokeDialogs() {
        clearRomajiRequest();
        if (formDialog)
            formDialog.close();
        if (confirmation)
            confirmation.close();
        if (visible)
            close();
    }
    function clearRomajiRequest() {
        const token = romajiToken;
        const owner = romajiRequestOwner;
        romajiToken = "";
        romajiRequestRevision = "";
        romajiRequestOwner = null;
        if (token.length > 0 && owner && typeof owner.discardRomaji === "function")
            owner.discardRomaji(token);
    }
    function variantRows() {
        if (managingQueue)
            return storedVariants;
        const result = [
            {
                id: "",
                name: qsTr("Arquivo / busca atual"),
                language: "",
                script: "",
                source: "",
                kind: "",
                generated: false
            }
        ];
        if (supported) {
            const stored = storedVariants;
            for (let index = 0; index < stored.length; ++index)
                result.push(stored[index]);
        }
        return result;
    }
    function variantForId(id) {
        if (!supported || id.length === 0)
            return null;
        const stored = storedVariants;
        for (let index = 0; index < stored.length; ++index) {
            if (stored[index].id === id)
                return stored[index];
        }
        return null;
    }
    function targetCurrent() {
        if (confirmTarget === null || confirmTarget.id !== selectedId)
            return false;
        const row = variantForId(confirmTarget.id);
        return row !== null && row.name === confirmTarget.name;
    }
    function rowDetails(row) {
        const pieces = [];
        const kind = versionKinds.find(choice => choice.value === row.kind);
        if (kind)
            pieces.push(kind.label);
        if (row.language.length > 0)
            pieces.push(row.language);
        if (row.script.length > 0)
            pieces.push(row.script);
        if (row.generated)
            pieces.push(qsTr("Gerada por ferramenta"));
        if (row.source.length > 0)
            pieces.push(row.source);
        return pieces.join(" · ");
    }
    function openForm(mode) {
        if (!canEdit || (mode === "current" && !canSaveCurrent))
            return;
        startForm(mode);
    }
    function openQueuedForm(occurrenceId, title) {
        if (!queuedFormAvailable || visible || childDialogOpen)
            return;
        const owner = lyrics;
        const token = owner.prepareQueuedVariant(occurrenceId);
        if (token.length === 0)
            return;
        if (owner !== lyrics || !queuedFormAvailable) {
            owner.cancelQueuedVariant(token);
            return;
        }
        queuedToken = token;
        queuedTitle = title;
        startForm("queued");
    }
    function openQueuedManager(occurrenceId, title, artist, album, durationMs) {
        if (!queuedFormAvailable || visible || childDialogOpen || typeof lyrics.openQueuedManager !== "function")
            return;
        const token = lyrics.openQueuedManager(occurrenceId);
        if (!token.length)
            return;
        managerToken = token;
        managerTitle = title;
        managerArtist = artist;
        managerAlbum = album;
        managerDurationMs = durationMs;
        managerSelection = "";
        open();
    }
    function focusRecordingSearch() {
        if (opened && recordingSearchButton.enabled)
            recordingSearchButton.forceActiveFocus(Qt.TabFocusReason);
        else if (opened)
            closeButton.forceActiveFocus(Qt.TabFocusReason);
    }
    function startForm(mode, draftText = "", draft = null) {
        formMode = mode;
        formOwner = lyrics;
        formTrack = actionTrack;
        formLyricsRevision = mode === "current" ? currentLyricsRevision : "";
        formRejected = false;
        lrcImportRejected = false;
        const romaji = mode === "romaji";
        nameField.text = draft ? draft.name : (romaji ? qsTr("Romaji") : "");
        languageField.text = draft ? draft.language : (romaji ? "ja" : "");
        scriptField.text = draft ? draft.script : (romaji ? "Latn" : "");
        sourceField.text = draft ? draft.source : (romaji ? "pykakasi" : "");
        contentField.text = draft ? draft.content : draftText;
        kindField.currentIndex = draft ? Math.max(0, versionKinds.findIndex(choice => choice.value === draft.kind)) : (romaji ? 1 : 0);
        generatedField.checked = draft ? draft.generated : romaji;
        timingField.checked = false;
        formDialog.open();
    }
    function chooseLrcFile() {
        if (!lrcImportAvailable || !formCurrent || lrcPickerArmed || lrcImportToken.length > 0)
            return;
        formRejected = false;
        lrcImportRejected = false;
        lrcPickerArmed = true;
        lrcPicker.open();
    }
    function cancelLrcImport() {
        lrcPickerArmed = false;
        lrcImportToken = "";
        lrcImportPreviousContent = "";
        if (lrcPicker && lrcPicker.visible)
            lrcPicker.close();
    }
    function restoreLrcPickerFocus() {
        Qt.callLater(function () {
            if (root.formCurrent && !root.lrcPickerArmed && importLrcButton.enabled)
                importLrcButton.forceActiveFocus(Qt.TabFocusReason);
        });
    }
    function startLrcImport(file) {
        if (!lrcPickerArmed || !lrcImportAvailable || !formCurrent)
            return;
        lrcPickerArmed = false;
        const token = String(++lrcImportSerial);
        lrcImportPreviousContent = contentField.text;
        lrcImportToken = token;
        if (!formOwner.importVariantLrc(file, formTrack, token))
            completeLrcImport(token, "", "");
    }
    function completeLrcImport(token, name, content) {
        if (!formCurrent || !lrcImportAvailable || lrcImportToken !== token || token.length === 0)
            return;
        const unchanged = contentField.text === lrcImportPreviousContent;
        lrcImportToken = "";
        lrcImportPreviousContent = "";
        if (content.length === 0 || !unchanged) {
            formRejected = true;
            lrcImportRejected = true;
        } else {
            contentField.text = content;
            if (nameField.text.trim().length === 0)
                nameField.text = name;
        }
        restoreLrcPickerFocus();
    }
    function requestEdit() {
        if (!canEditSelected || childDialogOpen)
            return;
        const owner = lyrics;
        const track = actionTrack;
        const id = selectedId;
        const draft = managingQueue ? owner.prepareQueuedVariantEdit(managerToken, id) : owner.prepareVariantEdit(id);
        if (!draft || typeof draft.token !== "string" || draft.token.length === 0)
            return;
        if (owner !== lyrics || track !== actionTrack || id !== selectedId || !canEditSelected || childDialogOpen) {
            owner.cancelVariantEdit(draft.token);
            return;
        }
        editId = id;
        editToken = draft.token;
        startForm("edit", "", draft);
    }
    function submitForm(asNew) {
        if (!formSubmit.enabled || !formCurrent)
            return;
        const owner = formOwner;
        const mode = formMode;
        const name = nameField.text.trim();
        const language = languageField.text.trim();
        const script = scriptField.text.trim();
        const kind = kindField.currentValue;
        let accepted;
        if (mode === "edit")
            accepted = owner.saveVariantEdit(editToken, name, language, script, kind, contentField.text, asNew, timingField.checked);
        else if (mode === "current")
            accepted = owner.saveCurrentLyrics(name, language, script, kind, formLyricsRevision);
        else if (mode === "romaji")
            accepted = owner.saveRomaji(romajiToken, name, contentField.text);
        else if (mode === "queued")
            accepted = owner.addQueuedVariant(queuedToken, name, language, script, sourceField.text.trim(), kind, contentField.text, generatedField.checked);
        else if (managingQueue)
            accepted = owner.addQueuedManagerVariant(managerToken, name, language, script, sourceField.text.trim(), kind, contentField.text, generatedField.checked);
        else
            accepted = owner.addVariant(name, language, script, sourceField.text.trim(), kind, contentField.text, generatedField.checked);
        if (!formDialog.visible || owner !== formOwner || mode !== formMode)
            return;
        if (accepted) {
            queuedToken = "";
            editToken = "";
            if (mode === "romaji")
                romajiToken = "";
            formDialog.accept();
        } else {
            formRejected = true;
            lrcImportRejected = false;
        }
    }
    function requestRomaji() {
        if (!canGenerateRomaji || childDialogOpen)
            return;
        const owner = lyrics;
        const revision = romajiSourceRevision;
        const token = owner.generateRomaji();
        if (owner === lyrics && revision === romajiSourceRevision && visible) {
            romajiToken = token;
            romajiRequestRevision = revision;
            romajiRequestOwner = owner;
        } else if (token.length > 0) {
            owner.discardRomaji(token);
        }
    }
    function requestRemoval() {
        if (!canEdit || selectedVariant === null)
            return;
        confirmMode = "remove";
        confirmOwner = lyrics;
        confirmTrack = actionTrack;
        confirmTarget = {
            id: selectedVariant.id,
            name: selectedVariant.name,
            title: managingQueue ? managerTitle : lyrics.title ?? ""
        };
        confirmation.open();
    }
    function requestReload() {
        if (!canReload)
            return;
        if (!lyrics.variantsHasPendingChange) {
            if (managingQueue)
                lyrics.reloadQueuedManager(managerToken);
            else
                lyrics.reloadVariants();
            return;
        }
        confirmMode = "reload";
        confirmOwner = lyrics;
        confirmTrack = actionTrack;
        confirmTarget = null;
        confirmation.open();
    }
    component FieldLabel: Text {
        Layout.fillWidth: true
        textFormat: Text.PlainText
        color: root.contract.textSecondaryColor
        wrapMode: Text.WordWrap
        Accessible.ignored: true
    }
    component VersionInput: TextField {
        id: input
        Layout.fillWidth: true
        implicitHeight: root.contract.controlTargetPreferred
        enabled: root.formCurrent
        activeFocusOnTab: true
        selectByMouse: true
        maximumLength: 128
        color: root.contract.textPrimaryColor
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
    background: Rectangle {
        color: root.contract.surfaceRaisedColor
        radius: root.contract.radiusMedium
        border.width: 1
        border.color: root.contract.borderColor
    }
    contentItem: ColumnLayout {
        spacing: root.contract.spacingUnit * 2
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Versões da letra")
        Text {
            Layout.fillWidth: true
            text: root.managingQueue ? qsTr("Versões da letra · %1").arg(root.managerTitle) : qsTr("Versões da letra")
            textFormat: Text.PlainText
            color: root.contract.textPrimaryColor
            font.bold: true
            font.pixelSize: 22
            Accessible.ignored: true
        }
        Text {
            objectName: "lyrics-variants-status"
            Layout.fillWidth: true
            text: root.managingQueue ? root.lyrics.queuedManagerStatus : root.queuedFeedbackText.length > 0 ? root.queuedFeedbackText : (root.supported ? root.lyrics.variantsStatusText : "")
            textFormat: Text.PlainText
            color: root.contract.textSecondaryColor
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.showOnlineCandidates
            Text {
                text: qsTr("Resultado da busca")
                textFormat: Text.PlainText
                color: root.contract.textPrimaryColor
                Accessible.ignored: true
            }
            ComboBox {
                id: onlineChoice
                objectName: "lyrics-online-candidates"
                Layout.fillWidth: true
                implicitHeight: root.contract.controlTargetPreferred
                enabled: root.canSelect && root.showOnlineCandidates
                model: root.onlineCandidates
                textRole: "label"
                currentIndex: root.onlineCandidates.findIndex(candidate => candidate.index === root.selectedOnlineCandidate)
                activeFocusOnTab: true
                Accessible.name: qsTr("Resultado da busca")
                Accessible.id: objectName
                Accessible.focusable: enabled
                Accessible.focused: activeFocus
                onActivated: index => {
                    const row = root.onlineCandidates[index];
                    if (enabled && row)
                        root.lyrics.selectOnlineLyricsCandidate(row.index);
                }
                contentItem: Text {
                    text: onlineChoice.displayText
                    textFormat: Text.PlainText
                    color: root.contract.textPrimaryColor
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                background: Rectangle {
                    color: root.contract.surfaceColor
                    radius: root.contract.radiusSmall
                    border.width: onlineChoice.activeFocus ? 3 : 1
                    border.color: onlineChoice.activeFocus ? root.contract.focusColor : root.contract.borderColor
                }
            }
        }
        Text {
            objectName: "lyrics-online-preview"
            Layout.fillWidth: true
            visible: root.showOnlineCandidates && text.length > 0
            text: root.showOnlineCandidates ? root.lyrics.plainText : ""
            textFormat: Text.PlainText
            maximumLineCount: 2
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            color: root.contract.textSecondaryColor
            Accessible.role: Accessible.StaticText
        }
        RowLayout {
            Layout.fillWidth: true
            AccessibleTransportButton {
                id: recordingSearchButton
                objectName: "lyrics-variant-search-lrclib"
                contract: root.contract
                text: qsTr("Buscar no LRCLIB")
                visible: root.recordingSearchSupported
                enabled: root.recordingSearchAvailable && !root.childDialogOpen
                onClicked: {
                    if (!enabled)
                        return;
                    if (root.managingQueue)
                        root.recordingSearchRequested(root.managerToken, root.managerTitle, root.managerArtist, root.managerAlbum, root.managerDurationMs);
                    else
                        root.recordingSearchRequested("", root.lyrics.title, root.lyrics.artist, root.lyrics.album ?? "", root.lyrics.durationMs ?? 0);
                }
            }
            AccessibleTransportButton {
                id: addButton
                objectName: "lyrics-variant-add"
                contract: root.contract
                text: qsTr("Adicionar versão")
                enabled: root.canEdit
                onClicked: root.openForm("add")
            }
            AccessibleTransportButton {
                id: saveCurrentButton
                objectName: "lyrics-variant-save-current"
                contract: root.contract
                text: qsTr("Salvar letra atual")
                visible: !root.managingQueue
                enabled: root.canSaveCurrent
                onClicked: root.openForm("current")
            }
            AccessibleTransportButton {
                id: generateRomajiButton
                objectName: "lyrics-variant-generate-romaji"
                contract: root.contract
                text: qsTr("Gerar romaji")
                visible: root.romajiSourceRevision.length > 0
                enabled: root.canGenerateRomaji
                onClicked: root.requestRomaji()
            }
        }
        Text {
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.romajiStatusText
            textFormat: Text.PlainText
            color: root.contract.textSecondaryColor
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        Text {
            Layout.fillWidth: true
            visible: !root.managingQueue && root.selectedId.length > 0
            text: qsTr("Selecione Arquivo / busca atual para salvar a letra carregada.")
            textFormat: Text.PlainText
            color: root.contract.textSecondaryColor
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        ListView {
            id: variantList
            property string cursorId: ""
            objectName: "lyrics-variants-list"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 80
            model: root.rows
            clip: true
            spacing: root.contract.spacingUnit
            boundsBehavior: Flickable.StopAtBounds
            activeFocusOnTab: root.canSelect && count > 0
            keyNavigationEnabled: false
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Versões da letra")
            Accessible.focusable: activeFocusOnTab
            Accessible.focused: activeFocus

            function indexOfId(variantId) {
                for (let index = 0; index < model.length; ++index) {
                    if (model[index].id === variantId)
                        return index;
                }
                return -1;
            }
            function moveCursor(index, reveal) {
                const next = model.length > 0 ? Math.max(0, Math.min(index, model.length - 1)) : -1;
                cursorId = next < 0 ? "" : model[next].id;
                currentIndex = next;
                if (reveal && next >= 0)
                    positionViewAtIndex(next, ListView.Contain);
            }
            function restoreCursor(preferredId) {
                const index = indexOfId(preferredId);
                moveCursor(index >= 0 ? index : indexOfId(root.selectedId), activeFocus);
            }
            function activateRow(variantId, reason) {
                const index = indexOfId(variantId);
                if (!root.canSelect || index < 0)
                    return;
                moveCursor(index, true);
                forceActiveFocus(reason);
                if (root.managingQueue)
                    root.managerSelection = variantId;
                else
                    root.lyrics.selectVariant(variantId);
            }

            onModelChanged: restoreCursor(cursorId)
            onActiveFocusChanged: {
                if (activeFocus)
                    restoreCursor(cursorId);
            }
            Keys.onPressed: event => {
                if (!root.canSelect || count === 0 || (event.modifiers !== Qt.NoModifier && event.modifiers !== Qt.KeypadModifier))
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
            ScrollBar.vertical: AccessibleScrollBar {
                contract: root.contract
            }
            delegate: ItemDelegate {
                id: variantRow
                required property var modelData
                required property int index
                readonly property bool cursorFocused: root.canSelect && variantList.activeFocus && ListView.isCurrentItem
                width: variantList.width
                implicitHeight: 62
                enabled: root.canSelect
                activeFocusOnTab: false
                focusPolicy: Qt.NoFocus
                highlighted: modelData.id === root.selectedId
                Accessible.role: Accessible.ListItem
                Accessible.name: modelData.name + (root.rowDetails(modelData).length ? " · " + root.rowDetails(modelData) : "")
                Accessible.focusable: enabled
                Accessible.focused: cursorFocused
                Accessible.selectable: true
                Accessible.selected: highlighted
                Accessible.onPressAction: variantList.activateRow(modelData.id, Qt.OtherFocusReason)
                onClicked: variantList.activateRow(modelData.id, Qt.MouseFocusReason)
                background: Rectangle {
                    color: variantRow.highlighted ? root.contract.surfaceColor : "transparent"
                    radius: root.contract.radiusSmall
                    border.width: variantRow.cursorFocused ? 3 : (variantRow.highlighted ? 1 : 0)
                    border.color: variantRow.cursorFocused ? root.contract.focusColor : root.contract.accentColor
                }
                contentItem: Column {
                    spacing: root.contract.spacingUnit
                    Text {
                        objectName: "lyrics-variant-name"
                        width: parent.width
                        text: variantRow.modelData.name
                        textFormat: Text.PlainText
                        color: root.contract.textPrimaryColor
                        font.bold: true
                        elide: Text.ElideRight
                        Accessible.ignored: true
                    }
                    Text {
                        width: parent.width
                        text: root.rowDetails(variantRow.modelData)
                        textFormat: Text.PlainText
                        color: root.contract.textSecondaryColor
                        elide: Text.ElideRight
                        Accessible.ignored: true
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            AccessibleTransportButton {
                id: editButton
                objectName: "lyrics-variant-edit"
                contract: root.contract
                text: qsTr("Editar versão")
                visible: root.supported && typeof root.lyrics.prepareVariantEdit === "function"
                enabled: root.canEditSelected
                onClicked: root.requestEdit()
            }
            AccessibleTransportButton {
                objectName: "lyrics-variant-remove"
                contract: root.contract
                text: qsTr("Excluir versão")
                enabled: root.canEdit && root.selectedVariant !== null
                onClicked: root.requestRemoval()
            }
            Item {
                Layout.fillWidth: true
            }
            AccessibleTransportButton {
                id: closeButton
                objectName: "lyrics-variant-close"
                contract: root.contract
                text: qsTr("Fechar")
                onClicked: root.close()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.supported && (root.managingQueue ? !root.canEdit : !root.lyrics.variantsReady || root.lyrics.variantsHasPendingChange)
            AccessibleTransportButton {
                objectName: "lyrics-variant-retry"
                contract: root.contract
                text: qsTr("Tentar salvar")
                visible: root.supported && root.lyrics.variantsCanRetrySave
                enabled: visible && root.visible && root.enabled && !root.busy
                onClicked: {
                    if (enabled)
                        root.lyrics.retryVariantsSave();
                }
            }
            AccessibleTransportButton {
                objectName: "lyrics-variant-reload"
                contract: root.contract
                text: root.noTrackToReload ? qsTr("Abandonar alteração") : qsTr("Recarregar do disco")
                enabled: root.canReload
                onClicked: root.requestReload()
            }
            Item {
                Layout.fillWidth: true
            }
        }
    }
    Dialog {
        id: formDialog
        objectName: "lyrics-variant-form"
        parent: root.parent
        anchors.centerIn: parent
        width: Math.min(600, parent ? parent.width - root.contract.spacingUnit * 8 : 600)
        height: Math.min(620, parent ? parent.height - root.contract.spacingUnit * 8 : 620)
        padding: root.contract.spacingUnit * 3
        modal: true
        focus: true
        popupType: Popup.Item
        closePolicy: Popup.CloseOnEscape
        onOpened: nameField.forceActiveFocus(Qt.TabFocusReason)
        onAboutToHide: root.cancelLrcImport()
        onClosed: {
            const mode = root.formMode;
            const owner = root.formOwner;
            const token = root.queuedToken;
            const edited = root.editToken;
            root.formMode = "";
            root.formOwner = null;
            root.formTrack = "";
            root.formLyricsRevision = "";
            root.editToken = "";
            root.editId = "";
            root.queuedToken = "";
            root.queuedTitle = "";
            if (mode === "queued") {
                if (owner && token.length > 0)
                    owner.cancelQueuedVariant(token);
                root.queuedFormClosed(token.length === 0);
            } else if (mode === "edit") {
                if (owner && edited.length > 0)
                    owner.cancelVariantEdit(edited);
                if (root.visible && root.enabled) {
                    const target = editButton.enabled ? editButton : closeButton;
                    target.forceActiveFocus(Qt.TabFocusReason);
                }
            } else if (mode === "romaji") {
                root.clearRomajiRequest();
                if (root.visible && root.enabled) {
                    const target = generateRomajiButton.enabled ? generateRomajiButton : closeButton;
                    target.forceActiveFocus(Qt.TabFocusReason);
                }
            } else if (root.visible && root.enabled) {
                if (mode === "current")
                    saveCurrentButton.forceActiveFocus(Qt.TabFocusReason);
                else
                    addButton.forceActiveFocus(Qt.TabFocusReason);
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
            Accessible.name: formHeading.text
            RowLayout {
                Layout.fillWidth: true
                Text {
                    id: formHeading
                    Layout.fillWidth: true
                    text: root.editing ? qsTr("Editar versão da letra") : root.formMode === "romaji" ? qsTr("Revisar romaji") : root.formMode === "current" ? qsTr("Salvar letra atual") : qsTr("Adicionar versão da letra")
                    textFormat: Text.PlainText
                    color: root.contract.textPrimaryColor
                    font.bold: true
                    font.pixelSize: 20
                    wrapMode: Text.Wrap
                    Accessible.ignored: true
                }
                AccessibleTransportButton {
                    id: importLrcButton
                    objectName: "lyrics-variant-import-lrc"
                    contract: root.contract
                    text: root.lrcImportToken.length > 0 ? qsTr("Lendo LRC…") : qsTr("Importar .lrc")
                    visible: root.lrcImportAvailable
                    enabled: visible && root.formCurrent && !root.lrcPickerArmed && root.lrcImportToken.length === 0
                    onClicked: root.chooseLrcFile()
                }
            }
            Text {
                objectName: "lyrics-variant-queued-target"
                Layout.fillWidth: true
                visible: root.formMode === "queued"
                text: root.queuedTitle
                textFormat: Text.PlainText
                color: root.contract.textSecondaryColor
                elide: Text.ElideRight
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            ScrollView {
                id: formScroll
                objectName: "lyrics-variant-form-scroll"
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
                        text: qsTr("Nome da versão")
                    }
                    VersionInput {
                        id: nameField
                        objectName: "lyrics-variant-name-input"
                        Accessible.name: qsTr("Nome da versão")
                        placeholderText: qsTr("Ex.: Japonês original ou Romaji")
                        KeyNavigation.tab: root.formMode === "romaji" ? contentField : languageField
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
                        VersionInput {
                            id: languageField
                            objectName: "lyrics-variant-language-input"
                            maximumLength: 64
                            Accessible.name: qsTr("Idioma da letra")
                            placeholderText: qsTr("Ex.: ja")
                            readOnly: root.formMode === "romaji"
                            KeyNavigation.tab: scriptField
                        }
                        VersionInput {
                            id: scriptField
                            objectName: "lyrics-variant-script-input"
                            maximumLength: 64
                            Accessible.name: qsTr("Escrita da letra")
                            placeholderText: qsTr("Ex.: Jpan ou Latn")
                            readOnly: root.formMode === "romaji"
                            KeyNavigation.tab: kindField
                        }
                    }
                    FieldLabel {
                        text: qsTr("Tipo de versão")
                    }
                    ComboBox {
                        id: kindField
                        objectName: "lyrics-variant-kind-input"
                        Layout.fillWidth: true
                        implicitHeight: root.contract.controlTargetPreferred
                        enabled: root.formCurrent && root.formMode !== "romaji"
                        textRole: "label"
                        valueRole: "value"
                        model: root.versionKinds
                        Accessible.name: qsTr("Tipo de versão")
                        KeyNavigation.tab: root.textEntryMode ? sourceField : formSubmit
                        contentItem: Text {
                            text: kindField.displayText
                            textFormat: Text.PlainText
                            color: root.contract.textPrimaryColor
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            color: root.contract.surfaceColor
                            radius: root.contract.radiusSmall
                            border.width: kindField.activeFocus ? 3 : 1
                            border.color: kindField.activeFocus ? root.contract.focusColor : root.contract.borderColor
                        }
                    }
                    FieldLabel {
                        visible: root.textEntryMode
                        text: qsTr("Origem (opcional; não é acessada pelo app)")
                    }
                    VersionInput {
                        id: sourceField
                        objectName: "lyrics-variant-source-input"
                        visible: root.textEntryMode
                        maximumLength: 2048
                        readOnly: root.formMode === "romaji" || root.editing
                        Accessible.name: qsTr("Origem da letra")
                        KeyNavigation.tab: contentField
                    }
                    FieldLabel {
                        visible: root.textEntryMode
                        text: root.editing ? qsTr("Edite a letra. Os tempos ficam protegidos até você permitir alterações.") : root.formMode === "romaji" ? qsTr("Revise a romanização e mantenha os tempos da letra.") : qsTr("Cole a letra ou o conteúdo LRC. Tempos existentes são preservados; texto sem tempos não é sincronizado automaticamente.")
                    }
                    TextArea {
                        id: contentField
                        objectName: "lyrics-variant-content-input"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 180
                        visible: root.textEntryMode
                        enabled: root.formCurrent
                        textFormat: TextEdit.PlainText
                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
                        activeFocusOnTab: true
                        color: root.contract.textPrimaryColor
                        selectionColor: root.contract.accentColor
                        selectedTextColor: root.contract.accentTextColor
                        Accessible.role: Accessible.EditableText
                        Accessible.name: qsTr("Texto da letra ou LRC")
                        KeyNavigation.tab: root.editing ? timingField : (root.formMode === "romaji" ? formSubmit : generatedField)
                        background: Rectangle {
                            color: root.contract.surfaceColor
                            radius: root.contract.radiusSmall
                            border.width: contentField.activeFocus ? 3 : 1
                            border.color: contentField.activeFocus ? root.contract.focusColor : root.contract.borderColor
                        }
                    }
                    CheckBox {
                        id: generatedField
                        objectName: "lyrics-variant-generated-input"
                        Layout.fillWidth: true
                        visible: root.textEntryMode
                        enabled: root.formCurrent && root.formMode !== "romaji" && !root.editing
                        text: qsTr("Texto gerado por ferramenta")
                        Accessible.name: text
                        KeyNavigation.tab: formSubmit
                        contentItem: Text {
                            text: generatedField.text
                            textFormat: Text.PlainText
                            color: root.contract.textPrimaryColor
                            leftPadding: generatedField.indicator.width + generatedField.spacing
                            verticalAlignment: Text.AlignVCenter
                            wrapMode: Text.WordWrap
                        }
                    }
                    CheckBox {
                        id: timingField
                        objectName: "lyrics-variant-timing-input"
                        Layout.fillWidth: true
                        visible: root.editing
                        enabled: root.formCurrent
                        text: qsTr("Permitir alterar os tempos do LRC")
                        Accessible.name: text
                        KeyNavigation.tab: formSubmit
                        contentItem: Text {
                            text: timingField.text
                            textFormat: Text.PlainText
                            color: root.contract.textPrimaryColor
                            leftPadding: timingField.indicator.width + timingField.spacing
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                visible: root.formRejected
                text: root.lrcImportRejected ? qsTr("Não foi possível preencher a versão com o arquivo LRC. Confira o arquivo ou cole a letra.") : root.editing ? qsTr("Confira o texto, os tempos ou reabra a versão se ela mudou.") : root.formMode === "romaji" ? qsTr("Confira o texto e mantenha os tempos da letra original.") : root.formMode === "queued" ? qsTr("Confira o conteúdo e os limites. Se a faixa mudou na fila, abra o formulário novamente.") : qsTr("Não foi possível adicionar a versão. Confira o conteúdo e os limites informados.")
                textFormat: Text.PlainText
                color: root.contract.textSecondaryColor
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.StaticText
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
                    onClicked: formDialog.reject()
                }
                AccessibleTransportButton {
                    objectName: "lyrics-variant-save-copy"
                    contract: root.contract
                    text: qsTr("Salvar como nova versão")
                    visible: root.editing
                    enabled: visible && formSubmit.enabled
                    onClicked: if (enabled)
                        root.submitForm(true)
                }
                AccessibleActionButton {
                    id: formSubmit
                    controlId: "lyrics-variant-submit"
                    contract: root.contract
                    text: root.editing ? qsTr("Salvar alterações") : qsTr("Salvar")
                    enabled: root.formCurrent && !root.lrcPickerArmed && root.lrcImportToken.length === 0 && (root.formMode !== "queued" || root.queuedFormAvailable) && nameField.text.trim().length > 0 && (root.formMode === "current" || contentField.text.trim().length > 0)
                    KeyNavigation.tab: nameField
                    onClicked: root.submitForm(false)
                }
            }
        }
    }
    NativeDialogs.FileDialog {
        id: lrcPicker
        objectName: "lyrics-variant-lrc-picker"
        title: qsTr("Selecionar arquivo de letra LRC")
        modality: Qt.WindowModal
        parentWindow: root.parent ? root.parent.Window.window : null
        popupType: Popup.Item
        fileMode: NativeDialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Letras sincronizadas (*.lrc)")]
        onAccepted: root.startLrcImport(selectedFile)
        onRejected: {
            root.lrcPickerArmed = false;
            root.restoreLrcPickerFocus();
        }
    }
    Dialog {
        id: confirmation
        objectName: "lyrics-variant-confirm"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(500, parent ? parent.width - root.contract.spacingUnit * 8 : 500)
        padding: root.contract.spacingUnit * 3
        modal: true
        focus: true
        popupType: Popup.Item
        closePolicy: Popup.CloseOnEscape
        onClosed: {
            root.confirmOwner = null;
            root.confirmTarget = null;
            root.confirmMode = "";
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
            Accessible.name: root.confirmMode === "reload" ? (root.noTrackToReload ? qsTr("Abandonar alteração pendente") : qsTr("Recarregar letras do disco")) : qsTr("Excluir versão da letra")
            Text {
                objectName: "lyrics-variant-confirm-text"
                Layout.fillWidth: true
                text: {
                    if (root.confirmMode === "reload")
                        return root.noTrackToReload ? qsTr("Abandonar a tentativa em memória? Não há faixa selecionada para recarregar. Uma gravação sem confirmação pode já ter sido efetivada; abandonar a tentativa não desfaz os arquivos gravados.") : qsTr("Abandonar a tentativa em memória e recarregar do disco? Uma gravação sem confirmação pode já ter sido efetivada; recarregar não promete desfazê-la.");
                    if (root.confirmTarget === null)
                        return "";
                    return root.confirmTarget.title.length > 0 ? qsTr("Excluir a versão “%1” de “%2”? O áudio e as outras versões serão preservados.").arg(root.confirmTarget.name).arg(root.confirmTarget.title) : qsTr("Excluir a versão “%1”? O áudio e as outras versões serão preservados.").arg(root.confirmTarget.name);
                }
                textFormat: Text.PlainText
                color: root.contract.textPrimaryColor
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.StaticText
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
                    controlId: "lyrics-variant-confirm-submit"
                    contract: root.contract
                    text: root.confirmMode === "reload" ? (root.noTrackToReload ? qsTr("Abandonar") : qsTr("Recarregar")) : qsTr("Excluir")
                    enabled: root.confirmCurrent
                    onClicked: {
                        if (!enabled || !root.confirmCurrent)
                            return;
                        const owner = root.confirmOwner;
                        let accepted;
                        if (root.confirmMode === "reload")
                            accepted = root.managingQueue ? owner.reloadQueuedManager(root.managerToken) : owner.reloadVariants();
                        else
                            accepted = root.managingQueue ? owner.removeQueuedVariant(root.managerToken, root.confirmTarget.id) : owner.removeVariant(root.confirmTarget.id);
                        if (accepted)
                            confirmation.accept();
                    }
                }
            }
        }
    }
}
