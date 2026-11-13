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
    property string queryTrackIdentity: ""
    readonly property bool variantsAvailable: lyrics !== null && typeof lyrics.variants !== "undefined"
    property bool queuedSaveFeedback: false
    readonly property bool variantsPending: variantsAvailable && lyrics.variantsHasPendingChange === true
    readonly property bool reviewSaveAvailable: variantsPending && !lyrics.variantsBusy
    readonly property string queuedFeedbackText: queuedSaveFeedback && variantsAvailable && typeof lyrics.queuedVariantStatusText === "string" ? lyrics.queuedVariantStatusText : ""
    readonly property bool queueVariantActionSupported: variantsAvailable && typeof lyrics.queueVariantAvailable === "boolean"
    readonly property bool queueVariantActionAvailable: queueVariantActionSupported && enabled && lyrics.queueVariantAvailable
    readonly property bool queryOpen: queryDialog.visible || variantsPanel.visible || variantsPanel.childDialogOpen
    readonly property bool synchronizedLyrics: lyrics !== null && lyrics.hasSyncedLyrics
    readonly property bool plainLyrics: lyrics !== null && !lyrics.hasSyncedLyrics && lyrics.plainText.length > 0
    readonly property bool searchAvailable: enabled && lyrics !== null && !lyrics.busy && lyrics.canSearch
    readonly property bool protectedLyrics: lyrics !== null && lyrics.lyricsCopyProtected === true
    readonly property string lyricsAttribution: protectedLyrics ? lyrics.lyricsAttribution : ""
    implicitHeight: protectedLyrics ? 96 + contract.controlTargetMinimum + contract.spacingUnit : 96
    Layout.minimumHeight: implicitHeight
    Layout.maximumHeight: implicitHeight
    signal queuedVariantFormClosed

    function openLyricFind() {
        if (root.enabled && root.protectedLyrics)
            Qt.openUrlExternally("https://www.lyricfind.com/");
    }

    function openQueuedVariant(occurrenceId, title) {
        if (queueVariantActionAvailable && !queryOpen)
            variantsPanel.openQueuedForm(occurrenceId, title);
    }

    onTrackIdentityChanged: {
        if (!variantsPending)
            queuedSaveFeedback = false;
        if (queryDialog)
            queryDialog.close();
        if (variantsPanel)
            variantsPanel.close();
    }
    onLyricsChanged: {
        queuedSaveFeedback = false;
        if (queryDialog)
            queryDialog.close();
        if (variantsPanel)
            variantsPanel.close();
    }
    onVisibleChanged: {
        if (!visible && queryDialog)
            queryDialog.close();
        if (!visible && variantsPanel)
            variantsPanel.close();
    }
    onEnabledChanged: {
        if (!enabled && queryDialog)
            queryDialog.close();
        if (!enabled && variantsPanel)
            variantsPanel.close();
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
                    root.queryTrackIdentity = root.trackIdentity;
                    titleField.text = root.lyrics ? root.lyrics.title : "";
                    artistField.text = root.lyrics ? root.lyrics.artist : "";
                    queryDialog.open();
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

    LyricsVariantsPanel {
        id: variantsPanel
        contract: root.contract
        lyrics: root.variantsAvailable ? root.lyrics : null
        trackIdentity: root.trackIdentity
        enabled: root.enabled
        queuedFeedbackText: root.queuedFeedbackText
        onQueuedFormClosed: submitted => {
            if (submitted)
                root.queuedSaveFeedback = true;
            root.queuedVariantFormClosed();
        }
        onClosed: {
            if (!root.variantsPending)
                root.queuedSaveFeedback = false;
            if (root.visible && root.enabled && versionsButton.visible)
                versionsButton.forceActiveFocus(Qt.TabFocusReason);
        }
    }

    Dialog {
        id: queryDialog
        objectName: "lyrics-query-dialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(480, parent ? parent.width - root.contract.spacingUnit * 8 : 480)
        modal: true
        focus: true
        popupType: Popup.Item
        closePolicy: Popup.CloseOnEscape
        padding: root.contract.spacingUnit * 3
        onOpened: titleField.forceActiveFocus(Qt.TabFocusReason)
        background: Rectangle {
            color: root.contract.surfaceRaisedColor
            radius: root.contract.radiusMedium
            border.width: 1
            border.color: root.contract.borderColor
        }
        contentItem: ColumnLayout {
            spacing: root.contract.spacingUnit * 2
            Accessible.role: Accessible.Dialog
            Accessible.name: qsTr("Buscar letra online")

            Text {
                Layout.fillWidth: true
                text: qsTr("Buscar letra online")
                textFormat: Text.PlainText
                color: root.contract.textPrimaryColor
                font.bold: true
                font.pixelSize: 20
                Accessible.ignored: true
            }
            Text {
                objectName: "lyrics-query-notice"
                Layout.fillWidth: true
                text: root.lyrics !== null && root.lyrics.lyricFindConfigured === true ? qsTr("Busca no LRCLIB e no LyricFind: envia os metadados da faixa aos provedores configurados.") : qsTr("Busca no LRCLIB: envia título, artista, álbum e duração.")
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: root.contract.textSecondaryColor
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Título da música")
                textFormat: Text.PlainText
                color: root.contract.textPrimaryColor
                Accessible.ignored: true
            }
            TextField {
                id: titleField
                objectName: "lyrics-query-title"
                Layout.fillWidth: true
                implicitHeight: root.contract.controlTargetPreferred
                maximumLength: 512
                enabled: root.searchAvailable
                activeFocusOnTab: true
                selectByMouse: true
                color: root.contract.textPrimaryColor
                selectionColor: root.contract.accentColor
                selectedTextColor: root.contract.accentTextColor
                Accessible.role: Accessible.EditableText
                Accessible.id: objectName
                Accessible.name: qsTr("Título da música")
                Accessible.focusable: enabled
                Accessible.focused: activeFocus
                KeyNavigation.tab: artistField
                KeyNavigation.backtab: submitButton
                onAccepted: artistField.forceActiveFocus(Qt.TabFocusReason)
                background: Rectangle {
                    color: root.contract.surfaceColor
                    radius: root.contract.radiusSmall
                    border.width: titleField.activeFocus ? 3 : 1
                    border.color: titleField.activeFocus ? root.contract.focusColor : root.contract.borderColor
                }
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Artista")
                textFormat: Text.PlainText
                color: root.contract.textPrimaryColor
                Accessible.ignored: true
            }
            TextField {
                id: artistField
                objectName: "lyrics-query-artist"
                Layout.fillWidth: true
                implicitHeight: root.contract.controlTargetPreferred
                maximumLength: 512
                enabled: root.searchAvailable
                activeFocusOnTab: true
                selectByMouse: true
                color: root.contract.textPrimaryColor
                selectionColor: root.contract.accentColor
                selectedTextColor: root.contract.accentTextColor
                Accessible.role: Accessible.EditableText
                Accessible.id: objectName
                Accessible.name: qsTr("Artista")
                Accessible.focusable: enabled
                Accessible.focused: activeFocus
                KeyNavigation.tab: cancelButton
                KeyNavigation.backtab: titleField
                onAccepted: submitButton.forceActiveFocus(Qt.TabFocusReason)
                background: Rectangle {
                    color: root.contract.surfaceColor
                    radius: root.contract.radiusSmall
                    border.width: artistField.activeFocus ? 3 : 1
                    border.color: artistField.activeFocus ? root.contract.focusColor : root.contract.borderColor
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                AccessibleTransportButton {
                    id: cancelButton
                    objectName: "lyrics-query-cancel"
                    contract: root.contract
                    text: qsTr("Cancelar")
                    KeyNavigation.tab: submitButton
                    KeyNavigation.backtab: artistField
                    onClicked: queryDialog.reject()
                }
                AccessibleActionButton {
                    id: submitButton
                    controlId: "lyrics-query-submit"
                    contract: root.contract
                    text: qsTr("Buscar")
                    enabled: root.searchAvailable && root.trackIdentity === root.queryTrackIdentity && titleField.text.trim().length > 0 && artistField.text.trim().length > 0
                    KeyNavigation.tab: titleField
                    KeyNavigation.backtab: cancelButton
                    onClicked: {
                        if (submitButton.enabled && root.lyrics && root.lyrics.search(titleField.text.trim(), artistField.text.trim()))
                            queryDialog.accept();
                    }
                }
            }
        }
    }
}
