import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

Popup {
    id: root

    property var closeConsentOwner: null
    property string closeConsentWarning: ""
    readonly property bool closeWithoutSaveAvailable: downloads !== null && downloads.canCloseAfterFinalSaveFailure
    required property ShellUiContract contract
    property var downloads: null
    property Item fallbackFocusItem: null
    property bool focusRestoreAllowed: true
    property Item restoreFocusItem: null
    readonly property bool retryAdmissionAvailable: downloads !== null && downloads.canRetryAdmission
    readonly property bool retryFinalSaveAvailable: downloads !== null && downloads.canRetryFinalSave
    readonly property bool retryStartupAvailable: downloads !== null && downloads.canRetryStartup
    readonly property string warning: downloads !== null ? downloads.warningText : ""

    signal exitRequested

    function confirmCloseWithoutSave() {
        if (!exitConfirmation.opened || !closeWithoutSaveAvailable || closeConsentOwner !== downloads || closeConsentWarning !== warning)
            return;
        const owner = closeConsentOwner;
        invalidateCloseConsent();
        if (owner.closeAfterFinalSaveFailure() === true)
            root.exitRequested();
    }
    function firstRowControl() {
        const row = jobsList.currentItem;
        if (row && row.cancelControl.visible && row.cancelControl.enabled)
            return row.cancelControl;
        if (row && row.retryControl.visible && row.retryControl.enabled)
            return row.retryControl;
        return closeButton;
    }
    function invalidateCloseConsent() {
        closeConsentOwner = null;
        closeConsentWarning = "";
        if (exitConfirmation.visible)
            exitConfirmation.close();
    }
    function lastRowControl() {
        const row = jobsList.currentItem;
        if (row && row.retryControl.visible && row.retryControl.enabled)
            return row.retryControl;
        if (row && row.cancelControl.visible && row.cancelControl.enabled)
            return row.cancelControl;
        return jobsList;
    }
    function nextTopControl(item, backwards) {
        const controls = topControls();
        const index = controls.indexOf(item);
        if (backwards)
            return index > 0 ? controls[index - 1] : lastRowControl();
        return index >= 0 && index + 1 < controls.length ? controls[index + 1] : jobsList;
    }
    function lastTopControl() {
        const controls = topControls();
        return controls.length > 0 ? controls[controls.length - 1] : closeButton;
    }
    function openFrom(opener) {
        if (downloads === null)
            return;
        restoreFocusItem = opener;
        open();
    }
    function requestCloseWithoutSave() {
        if (!closeWithoutSaveAvailable || exitConfirmation.visible)
            return;
        closeConsentOwner = downloads;
        closeConsentWarning = warning;
        exitConfirmation.open();
    }
    function showRow(index) {
        jobsList.currentIndex = index;
        jobsList.positionViewAtIndex(index, ListView.Contain);
    }
    function topControls() {
        return [closeButton, retryStartupButton, retryAdmissionButton, retryFinalSaveButton, closeWithoutSaveButton].filter(function (item) {
            return item && item.visible && item.enabled;
        });
    }

    clip: true
    closePolicy: Popup.CloseOnEscape
    dim: false
    focus: true
    modal: true
    objectName: "downloads-panel"
    padding: contract.spacingUnit * 3
    popupType: Popup.Item

    background: Rectangle {
        border.color: root.contract.borderColor
        border.width: root.contract.highContrast ? 2 : 1
        color: root.contract.surfaceRaisedColor
        radius: root.contract.radiusMedium
    }
    contentItem: ColumnLayout {
        Accessible.name: labels.title
        Accessible.role: Accessible.Dialog
        spacing: root.contract.spacingUnit * 2

        RowLayout {
            Layout.fillWidth: true

            Text {
                Layout.fillWidth: true
                color: root.contract.textPrimaryColor
                font.bold: true
                font.pixelSize: 20
                text: labels.title
                textFormat: Text.PlainText
            }
            AccessibleTransportButton {
                id: closeButton

                KeyNavigation.backtab: root.nextTopControl(closeButton, true)
                KeyNavigation.tab: root.nextTopControl(closeButton, false)
                accessibleName: labels.close
                contract: root.contract
                objectName: "downloads-close"
                text: "✕"

                onClicked: root.close()
            }
        }
        Text {
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            Layout.fillWidth: true
            color: root.contract.textSecondaryColor
            objectName: "downloads-status"
            text: root.downloads !== null ? root.downloads.statusText : ""
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
        }
        Text {
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            Layout.fillWidth: true
            color: root.contract.highContrast ? root.contract.textPrimaryColor : root.contract.warningColor
            objectName: "downloads-warning"
            text: root.warning
            textFormat: Text.PlainText
            visible: text.length > 0
            wrapMode: Text.Wrap
        }
        AccessibleTransportButton {
            id: retryStartupButton

            KeyNavigation.backtab: root.nextTopControl(retryStartupButton, true)
            KeyNavigation.tab: root.nextTopControl(retryStartupButton, false)
            Layout.fillWidth: true
            contract: root.contract
            enabled: root.retryStartupAvailable
            objectName: "downloads-retry-startup"
            text: labels.retryStartup
            visible: root.retryStartupAvailable

            onClicked: {
                if (root.retryStartupAvailable)
                    root.downloads.retryStartup();
            }
        }
        AccessibleTransportButton {
            id: retryAdmissionButton

            KeyNavigation.backtab: root.nextTopControl(retryAdmissionButton, true)
            KeyNavigation.tab: root.nextTopControl(retryAdmissionButton, false)
            Layout.fillWidth: true
            contract: root.contract
            enabled: root.retryAdmissionAvailable
            objectName: "downloads-retry-admission"
            text: labels.retryAdmission
            visible: root.retryAdmissionAvailable

            onClicked: {
                if (root.retryAdmissionAvailable)
                    root.downloads.retryAdmission();
            }
        }
        AccessibleTransportButton {
            id: retryFinalSaveButton

            KeyNavigation.backtab: root.nextTopControl(retryFinalSaveButton, true)
            KeyNavigation.tab: root.nextTopControl(retryFinalSaveButton, false)
            Layout.fillWidth: true
            contract: root.contract
            enabled: root.retryFinalSaveAvailable
            objectName: "downloads-retry-final-save"
            text: labels.retryFinalSave
            visible: root.retryFinalSaveAvailable

            onClicked: {
                if (root.retryFinalSaveAvailable)
                    root.downloads.retryFinalSave();
            }
        }
        AccessibleTransportButton {
            id: closeWithoutSaveButton

            KeyNavigation.backtab: root.nextTopControl(closeWithoutSaveButton, true)
            KeyNavigation.tab: root.nextTopControl(closeWithoutSaveButton, false)
            Layout.fillWidth: true
            contract: root.contract
            enabled: root.closeWithoutSaveAvailable
            objectName: "downloads-close-without-save"
            text: labels.closeWithoutSave
            visible: root.closeWithoutSaveAvailable

            onClicked: root.requestCloseWithoutSave()
        }
        ListView {
            id: jobsList

            Accessible.focusable: true
            Accessible.focused: activeFocus
            Accessible.name: labels.title
            Accessible.role: Accessible.List
            KeyNavigation.backtab: root.lastTopControl()
            KeyNavigation.tab: root.firstRowControl()
            Layout.fillHeight: true
            Layout.fillWidth: true
            Layout.minimumHeight: 0
            activeFocusOnTab: true
            clip: true
            keyNavigationWraps: true
            model: root.downloads !== null ? root.downloads.jobsModel : null
            objectName: "downloads-list"
            spacing: root.contract.spacingUnit * 2

            ScrollBar.vertical: ScrollBar {}
            delegate: Item {
                id: row

                required property bool canCancel
                required property bool canRetryImport
                property alias cancelControl: cancelButton
                required property string errorText
                required property bool indeterminate
                required property int index
                required property string jobId
                required property real progressRatio
                property alias retryControl: retryButton
                required property string statusText
                required property string title

                height: rowLayout.implicitHeight + root.contract.spacingUnit * 4
                width: jobsList.width

                Rectangle {
                    anchors.fill: parent
                    border.color: root.contract.borderColor
                    border.width: 1
                    color: root.contract.surfaceColor
                    radius: root.contract.radiusSmall
                }
                ColumnLayout {
                    id: rowLayout

                    anchors.left: parent.left
                    anchors.margins: root.contract.spacingUnit * 2
                    anchors.right: parent.right
                    anchors.top: parent.top
                    spacing: root.contract.spacingUnit

                    Text {
                        Accessible.name: text
                        Accessible.role: Accessible.StaticText
                        Layout.fillWidth: true
                        color: root.contract.textPrimaryColor
                        font.bold: true
                        objectName: "download-title"
                        text: row.title
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Accessible.name: text
                        Accessible.role: Accessible.StaticText
                        Layout.fillWidth: true
                        color: root.contract.textSecondaryColor
                        objectName: "download-status"
                        text: row.statusText
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                    }
                    ProgressBar {
                        id: progress

                        Accessible.description: row.statusText
                        Accessible.name: labels.progress
                        Layout.fillWidth: true
                        Layout.preferredHeight: 12
                        from: 0
                        indeterminate: row.indeterminate
                        objectName: "download-progress"
                        to: 1
                        value: row.progressRatio

                        background: Rectangle {
                            border.color: root.contract.borderColor
                            border.width: 1
                            color: root.contract.surfaceRaisedColor
                            implicitHeight: 12
                            radius: 3
                        }
                        contentItem: Item {
                            property real sweep: 0

                            clip: true

                            NumberAnimation on sweep {
                                duration: 1000
                                from: -progress.width / 3
                                loops: Animation.Infinite
                                running: progress.indeterminate && progress.visible && root.visible && !root.contract.highContrast && root.contract.panelTransitionMs > 0
                                to: progress.width
                            }

                            Rectangle {
                                color: root.contract.accentColor
                                height: parent.height
                                radius: 3
                                width: progress.indeterminate ? parent.width / 3 : parent.width * progress.visualPosition
                                x: progress.indeterminate ? parent.sweep : 0
                            }
                        }
                    }
                    Text {
                        Accessible.name: text
                        Accessible.role: Accessible.StaticText
                        Layout.fillWidth: true
                        color: root.contract.highContrast ? root.contract.textPrimaryColor : root.contract.errorColor
                        objectName: "download-error"
                        text: row.errorText
                        textFormat: Text.PlainText
                        visible: text.length > 0
                        wrapMode: Text.Wrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: row.canCancel || row.canRetryImport

                        AccessibleTransportButton {
                            id: cancelButton

                            KeyNavigation.backtab: jobsList
                            KeyNavigation.tab: retryButton.visible && retryButton.enabled ? retryButton : closeButton
                            contract: root.contract
                            enabled: row.canCancel
                            objectName: "download-cancel"
                            text: labels.cancel
                            visible: row.canCancel

                            onActiveFocusChanged: {
                                if (activeFocus)
                                    root.showRow(row.index);
                            }
                            onClicked: {
                                if (row.canCancel && root.downloads !== null)
                                    root.downloads.jobsModel.cancel(row.jobId);
                            }
                        }
                        AccessibleTransportButton {
                            id: retryButton

                            KeyNavigation.backtab: cancelButton.visible && cancelButton.enabled ? cancelButton : jobsList
                            KeyNavigation.tab: closeButton
                            contract: root.contract
                            enabled: row.canRetryImport
                            objectName: "download-retry-import"
                            text: labels.retryImport
                            visible: row.canRetryImport

                            onActiveFocusChanged: {
                                if (activeFocus)
                                    root.showRow(row.index);
                            }
                            onClicked: {
                                if (row.canRetryImport && root.downloads !== null)
                                    root.downloads.jobsModel.retryImport(row.jobId);
                            }
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                    }
                }
            }

            Keys.onPressed: event => {
                if (event.key === Qt.Key_Space || event.key === Qt.Key_M || event.key === Qt.Key_F1 || event.key === Qt.Key_F2)
                    event.accepted = true;
            }

            Rectangle {
                Accessible.ignored: true
                anchors.fill: parent
                border.color: root.contract.focusColor
                border.width: jobsList.activeFocus ? 3 : 0
                color: "transparent"
                radius: root.contract.radiusSmall
                z: 2
            }
            Text {
                Accessible.name: text
                Accessible.role: Accessible.StaticText
                anchors.centerIn: parent
                color: root.contract.textSecondaryColor
                horizontalAlignment: Text.AlignHCenter
                text: labels.empty
                textFormat: Text.PlainText
                visible: jobsList.count === 0
                width: parent.width
                wrapMode: Text.Wrap
            }
        }
    }

    onAboutToHide: invalidateCloseConsent()
    onCloseWithoutSaveAvailableChanged: {
        if (!closeWithoutSaveAvailable)
            invalidateCloseConsent();
    }
    onClosed: {
        const target = restoreFocusItem;
        restoreFocusItem = null;
        Qt.callLater(function () {
            if (root.visible || !root.focusRestoreAllowed)
                return;
            const destination = target && target.visible && target.enabled ? target : root.fallbackFocusItem;
            if (destination && destination.visible && destination.enabled)
                destination.forceActiveFocus(Qt.TabFocusReason);
        });
    }
    onDownloadsChanged: {
        invalidateCloseConsent();
        if (downloads === null)
            close();
    }
    onOpened: closeButton.forceActiveFocus(Qt.TabFocusReason)
    onWarningChanged: invalidateCloseConsent()

    DownloadText {
        id: labels
    }
    Popup {
        id: exitConfirmation

        closePolicy: Popup.CloseOnEscape
        dim: false
        focus: true
        height: confirmationContent.implicitHeight + padding * 2
        modal: true
        objectName: "downloads-exit-confirmation"
        padding: root.contract.spacingUnit * 3
        parent: root.parent
        popupType: Popup.Item
        width: Math.min(root.width, 560)
        x: root.x + (root.width - width) / 2
        y: root.y + (root.height - height) / 2

        background: Rectangle {
            border.color: root.contract.borderColor
            border.width: root.contract.highContrast ? 2 : 1
            color: root.contract.surfaceRaisedColor
            radius: root.contract.radiusMedium
        }
        contentItem: ColumnLayout {
            id: confirmationContent

            Accessible.name: labels.closeWithoutSave
            Accessible.role: Accessible.Dialog
            spacing: root.contract.spacingUnit * 3

            Text {
                Layout.fillWidth: true
                color: root.contract.textPrimaryColor
                font.bold: true
                text: labels.closeWithoutSave
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }
            Text {
                Layout.fillWidth: true
                color: root.contract.textPrimaryColor
                text: labels.closeWithoutSaveAdvice
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true

                AccessibleTransportButton {
                    id: keepOpenButton

                    KeyNavigation.backtab: confirmCloseButton
                    KeyNavigation.tab: confirmCloseButton
                    contract: root.contract
                    objectName: "downloads-cancel-close-without-save"
                    text: labels.keepOpen

                    onClicked: root.invalidateCloseConsent()
                }
                Item {
                    Layout.fillWidth: true
                }
                AccessibleTransportButton {
                    id: confirmCloseButton

                    KeyNavigation.backtab: keepOpenButton
                    KeyNavigation.tab: keepOpenButton
                    contract: root.contract
                    enabled: root.closeWithoutSaveAvailable && root.closeConsentOwner === root.downloads && root.closeConsentWarning === root.warning
                    objectName: "downloads-confirm-close-without-save"
                    text: labels.closeWithoutSave

                    onClicked: root.confirmCloseWithoutSave()
                }
            }
        }

        onClosed: {
            root.closeConsentOwner = null;
            root.closeConsentWarning = "";
            Qt.callLater(function () {
                if (root.opened && closeWithoutSaveButton.visible && closeWithoutSaveButton.enabled)
                    closeWithoutSaveButton.forceActiveFocus(Qt.TabFocusReason);
            });
        }
        onOpened: keepOpenButton.forceActiveFocus(Qt.TabFocusReason)
    }
}
