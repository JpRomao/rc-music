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
    readonly property bool queryOpen: queryDialog.visible
    readonly property bool synchronizedLyrics: lyrics !== null && lyrics.hasSyncedLyrics
    readonly property bool plainLyrics: lyrics !== null && !lyrics.hasSyncedLyrics && lyrics.plainText.length > 0
    readonly property bool searchAvailable: enabled && lyrics !== null && !lyrics.busy && lyrics.canSearch
    implicitHeight: 96
    Layout.minimumHeight: 96
    Layout.maximumHeight: 96

    onTrackIdentityChanged: {
        if (queryDialog)
            queryDialog.close();
    }
    onVisibleChanged: {
        if (!visible && queryDialog)
            queryDialog.close();
    }
    onEnabledChanged: {
        if (!enabled && queryDialog)
            queryDialog.close();
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
                text: root.lyrics && root.lyrics.statusText.length > 0 ? root.lyrics.statusText : qsTr("Letra")
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: root.contract.textSecondaryColor
                Accessible.role: Accessible.StaticText
                Accessible.name: text
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
            visible: root.plainLyrics
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical: AccessibleScrollBar {
                contract: root.contract
            }

            TextArea {
                objectName: "lyrics-plain-text"
                width: plainArea.availableWidth
                text: root.lyrics ? root.lyrics.plainText : ""
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
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.synchronizedLyrics && !root.plainLyrics
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
                text: qsTr("Busca no LRCLIB: envia título, artista, álbum e duração.")
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
