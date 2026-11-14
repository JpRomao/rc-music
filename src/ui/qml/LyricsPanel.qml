pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

Item {
    id: root
    objectName: "lyrics-panel"
    required property ShellUiContract contract
    property var lyrics: null
    property string trackIdentity: ""
    property bool readerCanSeek: false
    property real readerBottomInset: 100
    property int readerTextSize: 18
    readonly property bool variantsAvailable: lyrics !== null && typeof lyrics.variants !== "undefined"
    property bool queuedSaveFeedback: false
    readonly property bool variantsPending: variantsAvailable && lyrics.variantsHasPendingChange === true
    readonly property bool reviewSaveAvailable: variantsPending && !lyrics.variantsBusy
    readonly property string queuedFeedbackText: queuedSaveFeedback && variantsAvailable && typeof lyrics.queuedVariantStatusText === "string" ? lyrics.queuedVariantStatusText : ""
    readonly property bool queueVariantActionSupported: variantsAvailable && typeof lyrics.queueVariantAvailable === "boolean"
    readonly property bool queueVariantActionAvailable: queueVariantActionSupported && enabled && lyrics.queueVariantAvailable
    readonly property bool queryOpen: queryDialog.visible || variantsPanel.visible || variantsPanel.childDialogOpen || reader.visible && reader.activeFocus
    readonly property bool synchronizedLyrics: lyrics !== null && lyrics.hasSyncedLyrics
    readonly property bool plainLyrics: lyrics !== null && !lyrics.hasSyncedLyrics && lyrics.plainText.length > 0
    readonly property bool searchAvailable: enabled && lyrics !== null && !lyrics.busy && lyrics.canSearch
    readonly property bool protectedLyrics: lyrics !== null && lyrics.lyricsCopyProtected === true
    readonly property string lyricsAttribution: protectedLyrics ? lyrics.lyricsAttribution : ""
    implicitHeight: protectedLyrics ? 96 + contract.controlTargetMinimum + contract.spacingUnit : 96
    Layout.minimumHeight: implicitHeight
    Layout.maximumHeight: implicitHeight
    signal queuedVariantFormClosed
    signal readerSeekRequested(int timeMs)
    signal readerTextSizeRequested(int size)

    function openLyricFind() {
        if (root.enabled && root.protectedLyrics)
            Qt.openUrlExternally("https://www.lyricfind.com/");
    }

    function openQueuedVariant(occurrenceId, title, artist = "", album = "", durationMs = 0) {
        if (!queueVariantActionAvailable || queryOpen)
            return;
        if (typeof lyrics.openQueuedManager === "function")
            variantsPanel.openQueuedManager(occurrenceId, title, artist, album, durationMs);
        else
            variantsPanel.openQueuedForm(occurrenceId, title);
    }

    onTrackIdentityChanged: {
        if (!variantsPending)
            queuedSaveFeedback = false;
        if (queryDialog && !queryDialog.queuedTarget)
            queryDialog.close();
        if (variantsPanel && !variantsPanel.managingQueue)
            variantsPanel.close();
        if (reader)
            reader.close();
    }
    onLyricsChanged: {
        queuedSaveFeedback = false;
        if (queryDialog)
            queryDialog.close();
        if (variantsPanel)
            variantsPanel.close();
        if (reader)
            reader.close();
    }
    onVisibleChanged: {
        if (!visible && queryDialog && !queryDialog.queuedTarget)
            queryDialog.close();
        if (!visible && variantsPanel && !variantsPanel.managingQueue)
            variantsPanel.close();
        if (!visible && reader)
            reader.close();
    }
    onEnabledChanged: {
        if (!enabled && queryDialog)
            queryDialog.close();
        if (!enabled && variantsPanel)
            variantsPanel.close();
        if (!enabled && reader)
            reader.close();
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: root.contract.spacingUnit
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: root.contract.controlTargetMinimum
            spacing: root.contract.spacingUnit * 2

            Text {
                objectName: "lyrics-status"
                Layout.fillWidth: true
                text: {
                    if (root.queuedFeedbackText.length > 0)
                        return root.queuedFeedbackText;
                    if (root.variantsPending && root.lyrics.variantsStatusText.length > 0)
                        return root.lyrics.variantsStatusText;
                    return root.lyrics && root.lyrics.statusText.length > 0 ? root.lyrics.statusText : qsTr("Letra");
                }
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: root.contract.textSecondaryColor
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            AccessibleTransportButton {
                id: versionsButton
                objectName: "lyrics-versions"
                contract: root.contract
                text: root.reviewSaveAvailable ? qsTr("Revisar salvamento") : qsTr("Versões")
                accessibleName: root.reviewSaveAvailable ? qsTr("Revisar salvamento da letra") : qsTr("Versões da letra")
                Layout.preferredHeight: root.contract.controlTargetMinimum
                visible: root.variantsAvailable
                enabled: visible && root.enabled
                onClicked: {
                    if (!enabled || !root.visible)
                        return;
                    if (!root.reviewSaveAvailable)
                        root.queuedSaveFeedback = false;
                    queryDialog.close();
                    variantsPanel.open();
                }
            }
            AccessibleTransportButton {
                id: openReader
                objectName: "lyrics-open-reader"
                contract: root.contract
                text: qsTr("Abrir letra")
                visible: root.synchronizedLyrics || root.plainLyrics
                enabled: visible && root.enabled
                onClicked: {
                    if (enabled)
                        reader.open();
                }
            }
            AccessibleTransportButton {
                id: searchButton
                objectName: "lyrics-search"
                contract: root.contract
                text: qsTr("Buscar letra")
                accessibleName: qsTr("Buscar letra online")
                Layout.preferredHeight: root.contract.controlTargetMinimum
                enabled: root.searchAvailable
                onClicked: {
                    if (!searchButton.enabled)
                        return;
                    root.queuedSaveFeedback = false;
                    variantsPanel.close();
                    queryDialog.openForTarget(false, "", root.lyrics.title, root.lyrics.artist, root.lyrics.album ?? "", root.lyrics.durationMs ?? 0);
                }
            }
        }
        Text {
            objectName: "lyrics-current-line"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.synchronizedLyrics
            text: root.lyrics ? root.lyrics.currentLine : ""
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.bold: true
            font.pixelSize: 18
            color: root.contract.textPrimaryColor
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        Text {
            Layout.fillWidth: true
            visible: root.synchronizedLyrics && text.length > 0
            text: root.lyrics ? (root.lyrics.nextLine ?? "") : ""
            textFormat: Text.PlainText
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            color: root.contract.textSecondaryColor
            Accessible.ignored: true
        }
        ScrollView {
            id: plainArea
            objectName: "lyrics-plain-scroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.plainLyrics && !root.protectedLyrics
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical: AccessibleScrollBar {
                contract: root.contract
            }

            TextArea {
                objectName: "lyrics-plain-text"
                width: plainArea.availableWidth
                text: root.lyrics && !root.protectedLyrics ? root.lyrics.plainText : ""
                textFormat: Text.PlainText
                readOnly: true
                selectByMouse: true
                activeFocusOnTab: root.plainLyrics
                wrapMode: TextEdit.Wrap
                color: root.contract.textPrimaryColor
                selectionColor: root.contract.accentColor
                selectedTextColor: root.contract.accentTextColor
                Accessible.id: objectName
                Accessible.name: qsTr("Letra sem sincronização")
                background: Rectangle {
                    color: "transparent"
                    radius: root.contract.radiusSmall
                    border.width: parent.activeFocus ? 3 : 0
                    border.color: root.contract.focusColor
                }
            }
        }
        ScrollView {
            id: protectedPlainArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.plainLyrics && root.protectedLyrics
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical: AccessibleScrollBar {
                contract: root.contract
            }
            Text {
                objectName: "lyrics-protected-plain-text"
                width: protectedPlainArea.availableWidth
                text: root.plainLyrics && root.protectedLyrics ? root.lyrics.plainText : ""
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: root.contract.textPrimaryColor
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.synchronizedLyrics && !root.plainLyrics
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.protectedLyrics
            ScrollView {
                id: providerCreditArea
                objectName: "lyrics-provider-credit-scroll"
                Layout.fillWidth: true
                Layout.preferredHeight: root.contract.controlTargetMinimum
                Layout.maximumHeight: root.contract.controlTargetMinimum
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical: AccessibleScrollBar {
                    contract: root.contract
                }
                Text {
                    objectName: "lyrics-provider-credit"
                    width: providerCreditArea.availableWidth
                    text: root.lyricsAttribution
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: root.contract.textSecondaryColor
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
            }
            AccessibleTransportButton {
                objectName: "lyrics-provider-link"
                contract: root.contract
                text: qsTr("LyricFind")
                Layout.preferredHeight: root.contract.controlTargetMinimum
                onClicked: root.openLyricFind()
            }
        }
    }

    LyricsReader {
        id: reader
        contract: root.contract
        verses: root.lyrics ? (root.lyrics.readerVerses ?? []) : []
        activeVerse: root.lyrics ? (root.lyrics.activeReaderVerse ?? -1) : -1
        synchronizedLyrics: root.synchronizedLyrics
        plainText: root.lyrics ? root.lyrics.plainText : ""
        copyProtected: root.protectedLyrics
        attribution: root.lyricsAttribution
        canSeek: root.enabled && root.readerCanSeek
        trackIdentity: root.trackIdentity
        textSize: root.readerTextSize
        transportInset: Math.ceil(root.readerBottomInset)
        onSeekRequested: timeMs => root.readerSeekRequested(timeMs)
        onAttributionRequested: root.openLyricFind()
        onTextSizeRequested: size => root.readerTextSizeRequested(size)
        onClosed: {
            if (openReader.enabled)
                openReader.forceActiveFocus(Qt.TabFocusReason);
        }
    }

    LyricsVariantsPanel {
        id: variantsPanel
        contract: root.contract
        lyrics: root.variantsAvailable ? root.lyrics : null
        trackIdentity: root.trackIdentity
        enabled: root.enabled
        queuedFeedbackText: root.queuedFeedbackText
        onRecordingSearchRequested: (token, title, artist, album, durationMs) => queryDialog.openForTarget(true, token, title, artist, album, durationMs)
        onQueuedFormClosed: submitted => {
            if (submitted)
                root.queuedSaveFeedback = true;
            root.queuedVariantFormClosed();
        }
        onClosed: {
            if (queryDialog.managedTarget)
                queryDialog.close();
            if (!root.variantsPending)
                root.queuedSaveFeedback = false;
            if (root.visible && root.enabled && versionsButton.visible)
                versionsButton.forceActiveFocus(Qt.TabFocusReason);
        }
    }

    Dialog {
        id: queryDialog
        objectName: "lyrics-query-dialog"
        property string track: ""
        property var backend: null
        property bool managedTarget: false
        property string managerToken: ""
        property string targetTitle: ""
        property bool showingResults: false
        property int recordingIndex: -1
        property int formatIndex: -1
        property string choiceRevision: ""
        property bool saveAttempted: false
        property bool saveRequested: false
        property string completedSaveStatus: ""
        readonly property bool queuedTarget: managerToken.length > 0
        readonly property bool targetAvailable: root.enabled && backend !== null && backend === root.lyrics && (!managedTarget || variantsPanel.opened) && (queuedTarget ? variantsPanel.managerToken === managerToken && backend.queuedManagerSearchAvailable === true : root.visible && track === root.trackIdentity)
        readonly property bool current: visible && targetAvailable
        readonly property bool searching: current && backend.recordingSearchBusy === true
        readonly property var recordings: current ? (backend.recordingCandidates ?? []) : []
        readonly property var selected: current && choiceRevision === backend.recordingSearchRevision ? recordings[recordingIndex] ?? null : null
        readonly property bool canApply: selected !== null && (selected.instrumental === true || formatIndex >= 0) && !searching
        readonly property bool configuredLookup: current && !managedTarget && backend.lyricFindConfigured === true
        readonly property bool canSave: current && (queuedTarget ? backend.queuedManagerCanEdit === true : backend.canSaveRecording === true)
        readonly property bool savePending: current && saveRequested && backend.pendingRecordingSaveRevision === choiceRevision
        readonly property bool canRetrySave: savePending && backend.variantsCanRetrySave === true
        readonly property string writerStatus: current ? (queuedTarget ? backend.queuedManagerStatus : backend.variantsStatusText ?? "") : ""
        readonly property string saveStatus: !current || !saveAttempted ? "" : saveRequested ? (completedSaveStatus || writerStatus) : qsTr("Não foi possível salvar a versão. Confira o nome e tente novamente.")
        readonly property string dialogHeading: managedTarget ? qsTr("Buscar no LRCLIB") : qsTr("Buscar letra online")
        readonly property int queryDuration: durationField.text.length ? Number(durationField.text) : 0
        readonly property bool canSubmit: current && (queuedTarget || root.searchAvailable) && !searching && titleField.text.trim().length > 0 && (durationField.text.length === 0 || durationField.acceptableInput && queryDuration <= 86400)

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(632, parent ? parent.width - root.contract.spacingUnit * 6 : 632)
        modal: true
        focus: true
        popupType: Popup.Item
        closePolicy: Popup.CloseOnEscape
        padding: root.contract.spacingUnit * 3
        onOpened: titleField.forceActiveFocus(Qt.TabFocusReason)
        onTargetAvailableChanged: Qt.callLater(closeIfTargetUnavailable)
        onSavePendingChanged: {
            if (current && saveRequested && !savePending)
                Qt.callLater(captureSaveStatus);
        }
        onClosed: {
            const returnToManager = managedTarget && variantsPanel.opened && backend === root.lyrics;
            if (backend && typeof backend.cancelRecordingSearch === "function")
                backend.cancelRecordingSearch();
            backend = null;
            managedTarget = false;
            managerToken = "";
            showingResults = false;
            resetChoice();
            if (returnToManager)
                variantsPanel.focusRecordingSearch();
            else if (root.visible && searchButton.enabled)
                searchButton.forceActiveFocus(Qt.TabFocusReason);
        }

        function openForTarget(managed, token, title, artist, album, durationMs) {
            if (visible)
                return;
            track = root.trackIdentity;
            managedTarget = managed;
            managerToken = token;
            targetTitle = title;
            backend = root.lyrics;
            titleField.text = title;
            artistField.text = artist;
            albumField.text = album;
            const seconds = Math.round(durationMs / 1000);
            durationField.text = seconds > 0 ? String(seconds) : "";
            if (targetAvailable) {
                open();
            } else {
                backend = null;
                managedTarget = false;
                managerToken = "";
            }
        }

        function closeIfTargetUnavailable() {
            if (visible && !targetAvailable)
                close();
        }

        function captureSaveStatus() {
            if (current && saveRequested && !savePending)
                completedSaveStatus = writerStatus;
        }

        function resetChoice() {
            recordingIndex = -1;
            formatIndex = -1;
            choiceRevision = "";
            versionName.text = "";
            completedSaveStatus = "";
            saveAttempted = false;
            saveRequested = false;
        }

        function recordingLabel(recording) {
            const parts = [recording.title, recording.artist, recording.album].filter(value => !!value);
            const seconds = Math.round((recording.durationMs ?? 0) / 1000);
            if (seconds > 0) {
                parts.push(qsTr("%1:%2").arg(Math.floor(seconds / 60)).arg(String(seconds % 60).padStart(2, "0")));
                if (queryDuration > 0 && Math.abs(seconds - queryDuration) > 3)
                    parts.push(qsTr("duração diferente"));
            }
            if (recording.instrumental)
                parts.push(qsTr("Instrumental"));
            return parts.join(" · ");
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
            Accessible.name: queryDialog.dialogHeading

            Text {
                Layout.fillWidth: true
                text: queryDialog.dialogHeading
                textFormat: Text.PlainText
                color: root.contract.textPrimaryColor
                font.bold: true
                font.pixelSize: 20
                Accessible.ignored: true
            }
            Text {
                Layout.fillWidth: true
                visible: queryDialog.managedTarget && queryDialog.targetTitle.length > 0
                text: qsTr("Salvar letra em %1").arg(queryDialog.targetTitle)
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: root.contract.textPrimaryColor
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            Text {
                objectName: "lyrics-query-notice"
                Layout.fillWidth: true
                visible: !queryDialog.showingResults
                text: qsTr("O LRCLIB recebe título, artista e álbum. A duração ajuda a ordenar os resultados no RC Music.")
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: root.contract.textSecondaryColor
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            Text {
                Layout.fillWidth: true
                visible: queryDialog.configuredLookup && !queryDialog.showingResults
                text: qsTr("A consulta exata usa o título e o artista do formulário e o álbum e a duração da faixa atual. Pode consultar o LyricFind configurado.")
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: root.contract.textSecondaryColor
                Accessible.role: Accessible.StaticText
            }
            GridLayout {
                Layout.fillWidth: true
                visible: !queryDialog.showingResults
                columns: 2
                columnSpacing: root.contract.spacingUnit * 2
                rowSpacing: root.contract.spacingUnit

                QueryLabel {
                    text: titleField.Accessible.name
                }
                QueryField {
                    id: titleField
                    objectName: "lyrics-query-title"
                    Accessible.name: qsTr("Título da música")
                    onAccepted: artistField.forceActiveFocus(Qt.TabFocusReason)
                }
                QueryLabel {
                    text: artistField.Accessible.name
                }
                QueryField {
                    id: artistField
                    objectName: "lyrics-query-artist"
                    Accessible.name: qsTr("Artista")
                    onAccepted: albumField.forceActiveFocus(Qt.TabFocusReason)
                }
                QueryLabel {
                    text: albumField.Accessible.name
                }
                QueryField {
                    id: albumField
                    objectName: "lyrics-query-album"
                    Accessible.name: qsTr("Álbum")
                    onAccepted: durationField.forceActiveFocus(Qt.TabFocusReason)
                }
                QueryLabel {
                    text: durationField.Accessible.name
                }
                QueryField {
                    id: durationField
                    objectName: "lyrics-query-duration"
                    Accessible.name: qsTr("Duração em segundos")
                    inputMethodHints: Qt.ImhDigitsOnly
                    maximumLength: 5
                    validator: RegularExpressionValidator {
                        regularExpression: /[0-9]{1,5}/
                    }
                    onAccepted: submitButton.forceActiveFocus(Qt.TabFocusReason)
                }
            }
            Text {
                objectName: "lyrics-recording-status"
                Layout.fillWidth: true
                visible: queryDialog.showingResults && text.length > 0
                text: queryDialog.current ? (queryDialog.backend.recordingSearchStatus ?? "") : ""
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: root.contract.textSecondaryColor
                Accessible.role: Accessible.StaticText
            }
            RecordingChoice {
                id: recordingChoice
                objectName: "lyrics-recording-choice"
                visible: queryDialog.showingResults && queryDialog.recordings.length > 0
                enabled: !queryDialog.savePending
                model: [qsTr("Escolher gravação")].concat(queryDialog.recordings.map(recording => queryDialog.recordingLabel(recording)))
                currentIndex: queryDialog.recordingIndex + 1
                Accessible.name: qsTr("Gravações do LRCLIB")
                onActivated: index => {
                    if (!enabled)
                        return;
                    queryDialog.resetChoice();
                    if (index <= 0)
                        return;
                    queryDialog.choiceRevision = queryDialog.backend.recordingSearchRevision;
                    queryDialog.recordingIndex = index - 1;
                    const recording = queryDialog.selected;
                    if (recording && recording.formats.length === 1)
                        queryDialog.formatIndex = 0;
                    if (recording)
                        versionName.text = recording.title;
                }
            }
            RecordingChoice {
                objectName: "lyrics-recording-format"
                visible: queryDialog.showingResults && model.length > 2
                enabled: !queryDialog.savePending
                model: [qsTr("Escolher formato")].concat(queryDialog.selected?.formats.map(format => format.label) ?? [])
                currentIndex: queryDialog.formatIndex + 1
                Accessible.name: qsTr("Formato da letra")
                onActivated: index => {
                    if (!enabled || queryDialog.formatIndex === index - 1)
                        return;
                    queryDialog.formatIndex = index - 1;
                    queryDialog.completedSaveStatus = "";
                    queryDialog.saveAttempted = false;
                    queryDialog.saveRequested = false;
                }
            }
            ScrollView {
                id: previewScroll
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(root.contract.controlTargetPreferred * 2, Math.min(144, queryDialog.parent ? queryDialog.parent.height - 350 : 144))
                visible: queryDialog.showingResults && preview.text.length > 0
                contentWidth: availableWidth
                clip: true
                TextArea {
                    id: preview
                    objectName: "lyrics-recording-preview"
                    readOnly: true
                    selectByMouse: true
                    textFormat: Text.PlainText
                    wrapMode: TextEdit.Wrap
                    text: queryDialog.canApply && queryDialog.formatIndex >= 0 ? queryDialog.backend.previewRecording(queryDialog.recordingIndex, queryDialog.formatIndex, queryDialog.choiceRevision) : ""
                    color: root.contract.textPrimaryColor
                    selectionColor: root.contract.accentColor
                    selectedTextColor: root.contract.accentTextColor
                    Accessible.name: qsTr("Prévia da letra do LRCLIB")
                    Accessible.focusable: true
                }
            }
            QueryField {
                id: versionName
                visible: queryDialog.showingResults && queryDialog.canApply && queryDialog.formatIndex >= 0 && (queryDialog.canSave || queryDialog.saveRequested)
                readOnly: queryDialog.saveRequested
                Accessible.name: qsTr("Nome da versão")
                placeholderText: Accessible.name
                maximumLength: 128
            }
            Text {
                Layout.fillWidth: true
                visible: queryDialog.showingResults && text.length > 0
                text: queryDialog.saveStatus
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: root.contract.textSecondaryColor
                Accessible.role: Accessible.StaticText
            }
            RowLayout {
                Layout.fillWidth: true
                AccessibleTransportButton {
                    contract: root.contract
                    visible: !queryDialog.showingResults && queryDialog.configuredLookup
                    enabled: visible && queryDialog.canSubmit && artistField.text.trim().length > 0
                    text: qsTr("Consulta por metadados exatos")
                    onClicked: {
                        if (enabled && queryDialog.backend.search(titleField.text.trim(), artistField.text.trim()))
                            queryDialog.accept();
                    }
                }
                AccessibleTransportButton {
                    contract: root.contract
                    visible: queryDialog.showingResults
                    enabled: queryDialog.current && !queryDialog.savePending
                    text: qsTr("Editar busca")
                    onClicked: {
                        if (!enabled)
                            return;
                        queryDialog.backend.cancelRecordingSearch();
                        queryDialog.resetChoice();
                        queryDialog.showingResults = false;
                        titleField.forceActiveFocus(Qt.TabFocusReason);
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                AccessibleTransportButton {
                    id: cancelButton
                    objectName: "lyrics-query-cancel"
                    contract: root.contract
                    text: queryDialog.showingResults && !queryDialog.searching ? qsTr("Fechar") : qsTr("Cancelar")
                    onClicked: queryDialog.reject()
                }
                AccessibleActionButton {
                    id: submitButton
                    controlId: "lyrics-query-submit"
                    contract: root.contract
                    text: qsTr("Buscar")
                    visible: !queryDialog.showingResults
                    enabled: visible && queryDialog.canSubmit
                    onClicked: {
                        if (!enabled)
                            return;
                        queryDialog.resetChoice();
                        queryDialog.showingResults = true;
                        const title = titleField.text.trim();
                        const artist = artistField.text.trim();
                        const album = albumField.text.trim();
                        const durationMs = queryDialog.queryDuration * 1000;
                        if (queryDialog.queuedTarget)
                            queryDialog.backend.searchQueuedRecordings(queryDialog.managerToken, title, artist, album, durationMs);
                        else
                            queryDialog.backend.searchRecordings(title, artist, album, durationMs);
                        cancelButton.forceActiveFocus(Qt.TabFocusReason);
                    }
                }
                AccessibleTransportButton {
                    objectName: "lyrics-recording-save"
                    contract: root.contract
                    text: qsTr("Salvar versão")
                    visible: versionName.visible
                    enabled: visible && queryDialog.canSave && versionName.text.trim().length > 0 && !queryDialog.saveRequested
                    onClicked: {
                        if (!enabled)
                            return;
                        const index = queryDialog.recordingIndex;
                        const format = queryDialog.formatIndex;
                        const name = versionName.text.trim();
                        const revision = queryDialog.choiceRevision;
                        queryDialog.saveAttempted = true;
                        if (queryDialog.queuedTarget)
                            queryDialog.saveRequested = queryDialog.backend.saveQueuedRecording(queryDialog.managerToken, index, format, name, revision);
                        else if (queryDialog.managedTarget)
                            queryDialog.saveRequested = queryDialog.backend.saveManagedRecording(index, format, name, revision);
                        else
                            queryDialog.saveRequested = queryDialog.backend.saveRecording(index, format, name, revision);
                    }
                }
                AccessibleTransportButton {
                    objectName: "lyrics-recording-retry-save"
                    contract: root.contract
                    text: qsTr("Tentar salvar novamente")
                    visible: queryDialog.canRetrySave
                    enabled: visible
                    onClicked: {
                        if (enabled)
                            queryDialog.backend.retryRecordingSave(queryDialog.choiceRevision);
                    }
                }
                AccessibleActionButton {
                    controlId: "lyrics-recording-use"
                    contract: root.contract
                    text: queryDialog.selected?.instrumental ? qsTr("Marcar como instrumental") : qsTr("Usar nesta sessão")
                    visible: !queryDialog.managedTarget && queryDialog.showingResults && queryDialog.recordings.length > 0
                    enabled: visible && queryDialog.canApply && queryDialog.backend.variantsBusy !== true && queryDialog.backend.variantsHasPendingChange !== true
                    onClicked: {
                        if (enabled && queryDialog.backend.useRecording(queryDialog.recordingIndex, queryDialog.formatIndex, queryDialog.choiceRevision))
                            queryDialog.accept();
                    }
                }
            }
        }
    }

    component QueryLabel: Text {
        color: root.contract.textPrimaryColor
        textFormat: Text.PlainText
        Accessible.ignored: true
    }

    component QueryField: TextField {
        id: field
        Layout.fillWidth: true
        implicitHeight: root.contract.controlTargetPreferred
        maximumLength: 512
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
            border.width: field.activeFocus ? 3 : 1
            border.color: field.activeFocus ? root.contract.focusColor : root.contract.borderColor
        }
    }

    component RecordingChoice: ComboBox {
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
            id: entry
            required property int index
            width: choice.width
            text: choice.textAt(entry.index)
            highlighted: choice.highlightedIndex === entry.index
            contentItem: Text {
                text: entry.text
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
}
