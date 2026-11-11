pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
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
    property string queuedToken: ""
    property string queuedTitle: ""
    property bool formRejected: false
    property string confirmMode: ""
    property var confirmOwner: null
    property string confirmTrack: ""
    property var confirmTarget: null
    readonly property bool supported: lyrics !== null && typeof lyrics.variants !== "undefined"
    readonly property bool busy: supported && lyrics.variantsBusy
    readonly property bool canEdit: visible && enabled && supported && lyrics.variantsCanEdit && !busy
    readonly property bool canSelect: visible && enabled && supported && lyrics.variantsReady && !busy && !lyrics.variantsHasPendingChange
    readonly property bool canReload: visible && enabled && supported && !busy
    readonly property bool noTrackToReload: trackIdentity.length === 0
    readonly property string selectedId: supported ? lyrics.selectedVariantId : ""
    readonly property var onlineCandidates: supported && typeof lyrics.onlineLyricsCandidates !== "undefined" ? lyrics.onlineLyricsCandidates : []
    readonly property int selectedOnlineCandidate: supported && typeof lyrics.selectedOnlineLyricsCandidate !== "undefined" ? lyrics.selectedOnlineLyricsCandidate : -1
    readonly property bool showOnlineCandidates: selectedId.length === 0 && onlineCandidates.length > 1
    readonly property string currentLyricsRevision: supported && typeof lyrics.currentLyricsRevision !== "undefined" ? lyrics.currentLyricsRevision : ""
    readonly property bool canSaveCurrent: canEdit && currentLyricsRevision.length > 0 && lyrics.canSaveCurrentLyrics
    readonly property var selectedVariant: variantForId(selectedId)
    readonly property var rows: variantRows()
    readonly property bool childDialogOpen: formDialog.visible || confirmation.visible
    readonly property bool textEntryMode: formMode === "add" || formMode === "queued"
    readonly property bool queuedFormAvailable: enabled && supported && typeof lyrics.queueVariantAvailable === "boolean" && lyrics.queueVariantAvailable
    readonly property bool formCurrent: formDialog.visible && formOwner === lyrics && (formMode === "queued" ? enabled && queuedToken.length > 0 : formTrack === trackIdentity && canEdit && (formMode === "add" || (formMode === "current" && canSaveCurrent && formLyricsRevision === currentLyricsRevision)))
    readonly property bool confirmCurrent: confirmation.visible && confirmOwner === lyrics && confirmTrack === trackIdentity && (confirmMode === "reload" ? (canReload && lyrics.variantsHasPendingChange) : (canEdit && targetCurrent()))
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
    }
    onLyricsChanged: revokeDialogs()
    onTrackIdentityChanged: {
        if (formMode !== "queued")
            revokeDialogs();
    }
    onCurrentLyricsRevisionChanged: {
        if (formMode === "current" && formLyricsRevision !== currentLyricsRevision && formDialog && formDialog.visible)
            formDialog.close();
    }
    onEnabledChanged: {
        if (!enabled)
            revokeDialogs();
    }
    function revokeDialogs() {
        if (formDialog)
            formDialog.close();
        if (confirmation)
            confirmation.close();
        if (visible)
            close();
    }
    function variantRows() {
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
            const stored = lyrics.variants;
            for (let index = 0; index < stored.length; ++index)
                result.push(stored[index]);
        }
        return result;
    }
    function variantForId(id) {
        if (!supported || id.length === 0)
            return null;
        const stored = lyrics.variants;
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
        if (row.kind.length > 0)
            pieces.push(row.kind === "romanized" ? qsTr("Romanizada") : qsTr("Original"));
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
    function startForm(mode) {
        formMode = mode;
        formOwner = lyrics;
        formTrack = trackIdentity;
        formLyricsRevision = mode === "current" ? currentLyricsRevision : "";
        formRejected = false;
        nameField.text = "";
        languageField.text = "";
        scriptField.text = "";
        sourceField.text = "";
        contentField.text = "";
        kindField.currentIndex = 0;
        generatedField.checked = false;
        formDialog.open();
    }
    function requestRemoval() {
        if (!canEdit || selectedVariant === null)
            return;
        confirmMode = "remove";
        confirmOwner = lyrics;
        confirmTrack = trackIdentity;
        confirmTarget = {
            id: selectedVariant.id,
            name: selectedVariant.name
        };
        confirmation.open();
    }
    function requestReload() {
        if (!canReload)
            return;
        if (!lyrics.variantsHasPendingChange) {
            lyrics.reloadVariants();
            return;
        }
        confirmMode = "reload";
        confirmOwner = lyrics;
        confirmTrack = trackIdentity;
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
            text: qsTr("Versões da letra")
            textFormat: Text.PlainText
            color: root.contract.textPrimaryColor
            font.bold: true
            font.pixelSize: 22
            Accessible.ignored: true
        }
        Text {
            objectName: "lyrics-variants-status"
            Layout.fillWidth: true
            text: root.queuedFeedbackText.length > 0 ? root.queuedFeedbackText : (root.supported ? root.lyrics.variantsStatusText : "")
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
                enabled: root.canSaveCurrent
                onClicked: root.openForm("current")
            }
        }
        Text {
            Layout.fillWidth: true
            visible: root.selectedId.length > 0
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
            visible: root.supported && (!root.lyrics.variantsReady || root.lyrics.variantsHasPendingChange)
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
        onClosed: {
            const mode = root.formMode;
            const owner = root.formOwner;
            const token = root.queuedToken;
            root.formMode = "";
            root.formOwner = null;
            root.formTrack = "";
            root.formLyricsRevision = "";
            root.queuedToken = "";
            root.queuedTitle = "";
            if (mode === "queued") {
                if (owner && token.length > 0)
                    owner.cancelQueuedVariant(token);
                root.queuedFormClosed(token.length === 0);
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
            Accessible.name: root.formMode === "current" ? qsTr("Salvar letra atual") : qsTr("Adicionar versão da letra")
            Text {
                Layout.fillWidth: true
                text: root.formMode === "current" ? qsTr("Salvar letra atual") : qsTr("Adicionar versão da letra")
                textFormat: Text.PlainText
                color: root.contract.textPrimaryColor
                font.bold: true
                font.pixelSize: 20
                Accessible.ignored: true
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
                        VersionInput {
                            id: languageField
                            objectName: "lyrics-variant-language-input"
                            maximumLength: 64
                            Accessible.name: qsTr("Idioma da letra")
                            placeholderText: qsTr("Ex.: ja")
                            KeyNavigation.tab: scriptField
                        }
                        VersionInput {
                            id: scriptField
                            objectName: "lyrics-variant-script-input"
                            maximumLength: 64
                            Accessible.name: qsTr("Escrita da letra")
                            placeholderText: qsTr("Ex.: Jpan ou Latn")
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
                        enabled: root.formCurrent
                        model: [qsTr("Original"), qsTr("Romanizada")]
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
                        Accessible.name: qsTr("Origem da letra")
                        KeyNavigation.tab: contentField
                    }
                    FieldLabel {
                        visible: root.textEntryMode
                        text: qsTr("Cole a letra ou o conteúdo LRC. Tempos existentes são preservados; texto sem tempos não é sincronizado automaticamente.")
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
                        KeyNavigation.tab: generatedField
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
                        enabled: root.formCurrent
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
                }
            }
            Text {
                Layout.fillWidth: true
                visible: root.formRejected
                text: root.formMode === "queued" ? qsTr("Confira o conteúdo e os limites. Se a faixa mudou na fila, abra o formulário novamente.") : qsTr("Não foi possível adicionar a versão. Confira o conteúdo e os limites informados.")
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
                AccessibleActionButton {
                    id: formSubmit
                    controlId: "lyrics-variant-submit"
                    contract: root.contract
                    text: qsTr("Salvar")
                    enabled: root.formCurrent && (root.formMode !== "queued" || root.queuedFormAvailable) && nameField.text.trim().length > 0 && (root.formMode === "current" || contentField.text.trim().length > 0)
                    KeyNavigation.tab: nameField
                    onClicked: {
                        if (!enabled || !root.formCurrent)
                            return;
                        const owner = root.formOwner;
                        const mode = root.formMode;
                        const token = root.queuedToken;
                        const name = nameField.text.trim();
                        const language = languageField.text.trim();
                        const script = scriptField.text.trim();
                        const kind = kindField.currentIndex === 1 ? "romanized" : "original";
                        let accepted;
                        if (mode === "current")
                            accepted = owner.saveCurrentLyrics(name, language, script, kind, root.formLyricsRevision);
                        else if (mode === "queued")
                            accepted = owner.addQueuedVariant(token, name, language, script, sourceField.text.trim(), kind, contentField.text, generatedField.checked);
                        else
                            accepted = owner.addVariant(name, language, script, sourceField.text.trim(), kind, contentField.text, generatedField.checked);
                        if (!formDialog.visible)
                            return;
                        if (accepted) {
                            root.queuedToken = "";
                            formDialog.accept();
                        } else {
                            root.formRejected = true;
                        }
                    }
                }
            }
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
                text: root.confirmMode === "reload" ? (root.noTrackToReload ? qsTr("Abandonar a tentativa em memória? Não há faixa selecionada para recarregar. Uma gravação sem confirmação pode já ter sido efetivada; abandonar a tentativa não desfaz os arquivos gravados.") : qsTr("Abandonar a tentativa em memória e recarregar do disco? Uma gravação sem confirmação pode já ter sido efetivada; recarregar não promete desfazê-la.")) : qsTr("Excluir a versão “%1”? O áudio e as outras versões serão preservados.").arg(root.confirmTarget !== null ? root.confirmTarget.name : "")
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
                        const accepted = root.confirmMode === "reload" ? owner.reloadVariants() : owner.removeVariant(root.confirmTarget.id);
                        if (accepted)
                            confirmation.accept();
                    }
                }
            }
        }
    }
}
