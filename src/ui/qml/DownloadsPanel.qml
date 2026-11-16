import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

PageSurface {
    id: root

    readonly property bool archiveAvailable: downloads !== null && downloads.canArchiveSettled === true
    readonly property bool archiveRetryAvailable: downloads !== null && downloads.canRetryArchive === true
    property var archiveConsentOwner: null
    property string archiveConsentToken: ""
    readonly property bool archiveConsentCurrent: archiveAvailable && archiveConsentOwner === downloads && archiveConsentToken.length > 0 && downloads.archiveConsentToken === archiveConsentToken
    property var closeConsentOwner: null
    property string closeConsentWarning: ""
    property var informationOwner: null
    property string informationJobId: ""
    property string informationText: ""
    property string copyFeedback: ""
    property Item informationRestoreFocusItem: null
    property var discardConsentOwner: null
    property string discardConsentJobId: ""
    property string discardConsentToken: ""
    property bool discardConsentWithoutFiles: false
    property Item discardRestoreFocusItem: null
    readonly property bool discardConsentCurrent: opened && !lyricsDialog.visible && discardConsentOwner !== null && discardConsentOwner === downloads && discardConsentToken.length > 0 && downloads.discardConsentJobId === discardConsentJobId && downloads.discardConsentToken === discardConsentToken && discardConsentWithoutFiles === (downloads.discardConsentWithoutFiles === true)
    property var importConsentOwner: null
    property string importConsentJobId: ""
    property string importConsentToken: ""
    property var importConsentRow: null
    property Item importRestoreFocusItem: null
    readonly property bool importConsentCurrent: importConsentOwner !== null && importConsentOwner === downloads && importConsentToken.length > 0 && downloads.importConsentJobId === importConsentJobId && downloads.importConsentToken === importConsentToken && importConsentRow !== null && importConsentRow.jobId === importConsentJobId && importConsentRow.canImportRecovered && !importConsentRow.validationBusy && importConsentRow.validationIdentityCurrent() && jobsList.itemAtIndex(importConsentRow.index) === importConsentRow
    readonly property bool closeWithoutSaveAvailable: downloads !== null && downloads.canCloseAfterFinalSaveFailure
    required property ShellUiContract contract
    property var downloads: null
    readonly property bool openFolderAvailable: downloads !== null && typeof downloads.openDownloadDirectory === "function" && typeof downloads.downloadDirectoryText === "string" && downloads.downloadDirectoryText.length > 0
    readonly property bool directoryActionPending: openFolderAvailable && downloads.downloadDirectoryPending === true
    readonly property var lyricsBackend: downloads !== null && typeof downloads.lyrics !== "undefined" ? downloads.lyrics : null
    readonly property bool lyricsAvailable: lyricsBackend !== null && lyricsBackend.hasRequest
    property Item fallbackFocusItem: null
    property bool focusRestoreAllowed: true
    property Item restoreFocusItem: null
    readonly property bool retryAdmissionAvailable: downloads !== null && downloads.canRetryAdmission
    readonly property bool retryFinalSaveAvailable: downloads !== null && downloads.canRetryFinalSave
    readonly property bool retryStartupAvailable: downloads !== null && downloads.canRetryStartup
    readonly property bool renewNamespaceAvailable: downloads !== null && downloads.canRenewNamespace
    readonly property string warning: downloads !== null ? downloads.warningText : ""

    signal exitRequested

    function rowIdentityCurrent(row) {
        return opened && downloads !== null && row !== null && row.validationIdentityCurrent() && jobsList.itemAtIndex(row.index) === row;
    }
    function rowActionAllowed(row) {
        return rowIdentityCurrent(row) && !archiveConfirmation.visible && !discardConfirmation.visible && !importConfirmation.visible && !exitConfirmation.visible && !informationDialog.visible && !lyricsDialog.visible;
    }
    function showInformation(row, opener) {
        if (!rowActionAllowed(row) || typeof downloads.downloadInformation !== "function")
            return;
        const owner = downloads;
        const jobId = row.jobId;
        const text = owner.downloadInformation(jobId);
        if (!text || owner !== downloads || !rowIdentityCurrent(row) || row.jobId !== jobId)
            return;
        informationOwner = owner;
        informationJobId = jobId;
        informationText = text;
        copyFeedback = "";
        informationRestoreFocusItem = opener;
        informationDialog.open();
    }
    function invalidateInformation() {
        informationOwner = null;
        informationJobId = "";
        informationText = "";
        copyFeedback = "";
        if (informationDialog.visible)
            informationDialog.close();
    }
    function refreshInformation() {
        if (!informationDialog.visible)
            return;
        if (!opened || informationOwner === null || informationOwner !== downloads) {
            invalidateInformation();
            return;
        }
        const text = informationOwner.downloadInformation(informationJobId);
        if (!text) {
            invalidateInformation();
            return;
        }
        if (informationText !== text) {
            informationText = text;
            copyFeedback = "";
        }
    }
    function copyInformation() {
        refreshInformation();
        if (!informationDialog.opened || informationOwner === null || informationOwner !== downloads || typeof informationOwner.copyDownloadInformation !== "function")
            return;
        const owner = informationOwner;
        const jobId = informationJobId;
        const copied = owner.copyDownloadInformation(jobId) === true;
        if (informationDialog.opened && informationOwner === owner && downloads === owner && informationJobId === jobId)
            copyFeedback = copied ? labels.copied : labels.copyFailed;
    }

    function canPromptArchive() {
        return opened && archiveAvailable && !archiveConfirmation.visible && !discardConfirmation.visible && !importConfirmation.visible && !exitConfirmation.visible && !lyricsDialog.visible;
    }
    function requestArchiveConsent() {
        if (!canPromptArchive())
            return;
        const owner = downloads;
        if (owner.requestArchiveConfirmation() !== true || !owner)
            return;
        const token = owner.archiveConsentToken;
        if (owner !== downloads || !canPromptArchive() || !token) {
            if (token)
                owner.cancelArchiveConfirmation(token);
            return;
        }
        archiveConsentOwner = owner;
        archiveConsentToken = token;
        archiveConfirmation.open();
    }
    function invalidateArchiveConsent() {
        const owner = archiveConsentOwner;
        const token = archiveConsentToken;
        archiveConsentOwner = null;
        archiveConsentToken = "";
        if (owner !== null && token.length > 0)
            owner.cancelArchiveConfirmation(token);
        if (archiveConfirmation.visible)
            archiveConfirmation.close();
    }
    function confirmArchiveConsent() {
        if (!opened || !archiveConfirmation.opened || !archiveConsentCurrent)
            return;
        const owner = archiveConsentOwner;
        const token = archiveConsentToken;
        archiveConsentOwner = null;
        archiveConsentToken = "";
        owner.confirmArchive(token);
        if (archiveConfirmation.visible)
            archiveConfirmation.close();
    }
    function canPromptDiscard() {
        return opened && downloads !== null && !archiveConfirmation.visible && !discardConfirmation.visible && !exitConfirmation.visible && !importConfirmation.visible && !lyricsDialog.visible;
    }
    function requestDiscardConsent(jobId, opener) {
        if (!canPromptDiscard())
            return;
        const owner = downloads;
        if (owner.requestDiscardPartials(jobId) !== true || !owner)
            return;
        const token = owner.discardConsentToken;
        const jobMatches = owner.discardConsentJobId === jobId;
        if (owner !== downloads || !canPromptDiscard() || !jobMatches || !token) {
            if (jobMatches && token)
                owner.cancelDiscardPartialsConfirmation(jobId, token);
            return;
        }
        discardConsentOwner = owner;
        discardConsentJobId = jobId;
        discardConsentWithoutFiles = owner.discardConsentWithoutFiles === true;
        discardConsentToken = token;
        discardRestoreFocusItem = opener;
        if (discardConsentCurrent)
            discardConfirmation.open();
        else
            invalidateDiscardConsent();
    }
    function invalidateDiscardConsent() {
        const owner = discardConsentOwner;
        const jobId = discardConsentJobId;
        const token = discardConsentToken;
        discardConsentOwner = null;
        discardConsentJobId = "";
        discardConsentToken = "";
        discardConsentWithoutFiles = false;
        if (owner !== null && token.length > 0)
            owner.cancelDiscardPartialsConfirmation(jobId, token);
        if (discardConfirmation.visible)
            discardConfirmation.close();
    }
    function confirmDiscardConsent() {
        if (!discardConfirmation.opened || !discardConsentCurrent)
            return;
        const owner = discardConsentOwner;
        const jobId = discardConsentJobId;
        const token = discardConsentToken;
        owner.confirmDiscardPartials(jobId, token);
        invalidateDiscardConsent();
    }
    function requestRecoveredImportConsent(jobId, opener, row) {
        if (!opened || downloads === null || archiveConfirmation.visible || importConfirmation.visible || discardConfirmation.visible || exitConfirmation.visible || !row || !row.canImportRecovered || row.validationBusy || !row.validationIdentityCurrent() || row.jobId !== jobId || jobsList.itemAtIndex(row.index) !== row)
            return;
        const owner = downloads;
        if (owner.requestRecoveredImport(jobId) !== true)
            return;
        importConsentOwner = owner;
        importConsentJobId = owner.importConsentJobId;
        importConsentToken = owner.importConsentToken;
        importConsentRow = row;
        importRestoreFocusItem = opener;
        if (importConsentCurrent)
            importConfirmation.open();
        else
            invalidateImportConsent();
    }
    function invalidateImportConsent() {
        const owner = importConsentOwner;
        const jobId = importConsentJobId;
        const token = importConsentToken;
        importConsentOwner = null;
        importConsentJobId = "";
        importConsentToken = "";
        importConsentRow = null;
        if (owner !== null && token.length > 0)
            owner.cancelRecoveredImportConfirmation(jobId, token);
        if (importConfirmation.visible)
            importConfirmation.close();
    }
    function confirmImportConsent() {
        if (!importConfirmation.opened || !importConsentCurrent)
            return;
        const owner = importConsentOwner;
        const jobId = importConsentJobId;
        const token = importConsentToken;
        owner.confirmRecoveredImport(jobId, token);
        invalidateImportConsent();
    }

    function confirmCloseWithoutSave() {
        if (!exitConfirmation.opened || !closeWithoutSaveAvailable || closeConsentOwner !== downloads || closeConsentWarning !== warning)
            return;
        const owner = closeConsentOwner;
        invalidateCloseConsent();
        if (owner.closeAfterFinalSaveFailure() === true)
            root.exitRequested();
    }
    function firstRowControl() {
        const controls = rowControls(jobsList.currentItem);
        return controls.length > 0 ? controls[0] : (embedded ? nextFocusItem : closeButton);
    }
    function invalidateCloseConsent() {
        closeConsentOwner = null;
        closeConsentWarning = "";
        if (exitConfirmation.visible)
            exitConfirmation.close();
    }
    function lastRowControl() {
        const controls = rowControls(jobsList.currentItem);
        return controls.length > 0 ? controls[controls.length - 1] : (jobsList.currentItem || jobsList);
    }
    function rowControls(row) {
        if (!row)
            return [];
        return [row.cancelControl, row.retryControl, row.downloadRetryControl, row.reviewControl, row.discardControl, row.validateControl, row.importRecoveredControl, row.releaseValidationControl, row.informationControl].filter(function (item) {
            return item && item.visible && item.enabled;
        });
    }
    function nextRowControl(row, item, backwards) {
        const controls = rowControls(row);
        const index = controls.indexOf(item);
        if (backwards)
            return index > 0 ? controls[index - 1] : row;
        return index >= 0 && index + 1 < controls.length ? controls[index + 1] : (embedded ? nextFocusItem : closeButton);
    }
    function nextTopControl(item, backwards) {
        const controls = topControls();
        const index = controls.indexOf(item);
        if (backwards)
            return index > 0 ? controls[index - 1] : (embedded ? previousFocusItem : lastRowControl());
        return index >= 0 && index + 1 < controls.length ? controls[index + 1] : (jobsList.currentItem || jobsList);
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
        if (!closeWithoutSaveAvailable || archiveConfirmation.visible || exitConfirmation.visible || discardConfirmation.visible || importConfirmation.visible)
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
        return [closeButton, openFolderButton, lyricsButton, archiveButton, retryStartupButton, renewNamespaceButton, retryAdmissionButton, retryFinalSaveButton, closeWithoutSaveButton].filter(function (item) {
            return item && item.visible && item.enabled;
        });
    }

    clip: true
    closePolicy: Popup.CloseOnEscape
    dim: false
    focus: true
    modal: !embedded
    objectName: "downloads-panel"
    padding: embedded ? 0 : contract.spacingUnit * 3
    popupType: Popup.Item

    background: Rectangle {
        // Inline Aurora pages sit on the canvas; the legacy dialog keeps its card.
        border.color: root.contract.borderColor
        border.width: root.embedded ? 0 : (root.contract.highContrast ? 2 : 1)
        color: root.embedded ? root.contract.canvasColor : root.contract.surfaceRaisedColor
        radius: root.embedded ? 0 : root.contract.radiusMedium
    }
    contentItem: ColumnLayout {
        Accessible.name: root.embedded ? navigationLabels.activity : labels.title
        Accessible.role: root.embedded ? Accessible.Pane : Accessible.Dialog
        spacing: root.contract.spacingUnit * 2

        PageHeader {
            Layout.fillWidth: true
            contract: root.contract
            editorial: root.embedded
            eyebrow: root.embedded ? navigationLabels.activityEyebrow : ""
            title: root.embedded ? navigationLabels.activity : labels.title
            description: root.embedded ? navigationLabels.activityDescription : ""

            AccessibleTransportButton {
                id: openFolderButton

                KeyNavigation.backtab: root.nextTopControl(openFolderButton, true)
                KeyNavigation.tab: root.nextTopControl(openFolderButton, false)
                contract: root.contract
                enabled: root.openFolderAvailable && !root.directoryActionPending
                objectName: "downloads-open-folder"
                text: labels.openFolder
                visible: root.openFolderAvailable

                onClicked: {
                    if (root.opened && openFolderButton.enabled)
                        root.downloads.openDownloadDirectory();
                }
                onEnabledChanged: {
                    if (!enabled && activeFocus)
                        closeButton.forceActiveFocus(Qt.TabFocusReason);
                }
            }
            AccessibleTransportButton {
                id: closeButton

                KeyNavigation.backtab: root.nextTopControl(closeButton, true)
                KeyNavigation.tab: root.nextTopControl(closeButton, false)
                accessibleName: root.embedded ? navigationLabels.closeActivity : labels.close
                contract: root.contract
                flat: root.embedded
                iconName: root.embedded ? "close" : ""
                objectName: "downloads-close"
                text: "✕"

                onClicked: root.close()
            }
        }
        AccessibleTransportButton {
            id: lyricsButton
            Layout.fillWidth: true
            contract: root.contract
            objectName: "downloads-lyrics"
            text: root.lyricsBackend !== null && root.lyricsBackend.busy ? labels.lyricsBusy : labels.reviewLyrics
            accessibleName: text
            visible: root.lyricsAvailable
            enabled: root.lyricsAvailable
            KeyNavigation.backtab: root.nextTopControl(lyricsButton, true)
            KeyNavigation.tab: root.nextTopControl(lyricsButton, false)
            onClicked: {
                if (!root.lyricsAvailable)
                    return;
                lyricsDialog.restoreFocusItem = lyricsButton;
                lyricsDialog.open();
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
            color: root.contract.textSecondaryColor
            objectName: "downloads-folder-feedback"
            text: root.openFolderAvailable && typeof root.downloads.downloadDirectoryStatus === "string" ? root.downloads.downloadDirectoryStatus : ""
            textFormat: Text.PlainText
            visible: text.length > 0
            wrapMode: Text.Wrap
        }
        Text {
            Accessible.name: text
            Accessible.role: Accessible.StaticText
            Layout.fillWidth: true
            color: root.contract.textSecondaryColor
            objectName: "downloads-capacity"
            text: root.downloads !== null && typeof root.downloads.historyCapacityText !== "undefined" ? root.downloads.historyCapacityText : ""
            textFormat: Text.PlainText
            visible: text.length > 0
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
            id: archiveButton

            KeyNavigation.backtab: root.nextTopControl(archiveButton, true)
            KeyNavigation.tab: root.nextTopControl(archiveButton, false)
            Layout.fillWidth: true
            contract: root.contract
            enabled: visible
            objectName: "downloads-archive"
            text: root.archiveRetryAvailable ? labels.retryArchive : labels.archive
            visible: root.archiveAvailable || root.archiveRetryAvailable

            onClicked: {
                if (root.archiveRetryAvailable) {
                    if (root.downloads.retryArchive() === true)
                        closeButton.forceActiveFocus(Qt.TabFocusReason);
                } else {
                    root.requestArchiveConsent();
                }
            }
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
            id: renewNamespaceButton

            KeyNavigation.backtab: root.nextTopControl(renewNamespaceButton, true)
            KeyNavigation.tab: root.nextTopControl(renewNamespaceButton, false)
            Layout.fillWidth: true
            contract: root.contract
            enabled: root.renewNamespaceAvailable
            objectName: "downloads-renew-namespace"
            text: labels.recoverHistory
            visible: root.renewNamespaceAvailable

            onClicked: {
                if (root.renewNamespaceAvailable)
                    root.downloads.renewNamespace();
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

            readonly property bool navigationFocused: activeFocus && (Window.activeFocusItem === jobsList || (currentItem !== null && Window.activeFocusItem === currentItem))

            Accessible.focusable: true
            Accessible.focused: navigationFocused && currentItem === null
            Accessible.name: labels.title
            Accessible.role: Accessible.List
            KeyNavigation.backtab: root.lastTopControl()
            KeyNavigation.tab: root.firstRowControl()
            Layout.fillHeight: true
            Layout.fillWidth: true
            Layout.minimumHeight: root.embedded ? 120 : 0
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
                required property bool canRetryDownload
                required property string retryDownloadReason
                required property var model
                readonly property string jobState: model.state
                required property bool canReviewFiles
                required property bool canDiscardPartials
                required property bool canValidateMedia
                required property bool canImportRecovered
                required property bool canReleaseValidation
                property alias cancelControl: cancelButton
                required property string errorText
                required property bool indeterminate
                required property int index
                required property string jobId
                required property real progressRatio
                property alias retryControl: retryButton
                property alias downloadRetryControl: downloadRetryButton
                property alias informationControl: informationButton
                property alias reviewControl: reviewButton
                property alias discardControl: discardButton
                property alias validateControl: validateButton
                property alias importRecoveredControl: importRecoveredButton
                property alias releaseValidationControl: releaseValidationButton
                required property bool reviewBusy
                required property string reviewText
                required property bool validationBusy
                required property string validationText
                required property string statusText
                required property string title
                readonly property bool cursorFocused: jobsList.navigationFocused && ListView.isCurrentItem
                property var validationOwner: null
                property string validationJobId: ""

                Accessible.focusable: true
                Accessible.focused: cursorFocused
                Accessible.name: row.title
                Accessible.role: Accessible.ListItem
                KeyNavigation.backtab: root.lastTopControl()
                KeyNavigation.tab: root.firstRowControl()

                function validationIdentityCurrent() {
                    return root.downloads !== null && validationOwner === root.downloads && validationJobId.length > 0 && validationJobId === jobId;
                }

                Component.onCompleted: {
                    validationOwner = root.downloads;
                    validationJobId = jobId;
                }

                height: rowLayout.implicitHeight + root.contract.spacingUnit * 4
                width: jobsList.width

                Rectangle {
                    anchors.fill: parent
                    border.color: row.cursorFocused ? root.contract.focusColor : root.contract.borderColor
                    border.width: row.cursorFocused ? 3 : 1
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
                    Text {
                        Accessible.name: text
                        Accessible.role: Accessible.StaticText
                        Layout.fillWidth: true
                        color: root.contract.textSecondaryColor
                        objectName: "download-review-status"
                        text: row.reviewText
                        textFormat: Text.PlainText
                        visible: text.length > 0 && row.validationText.length === 0
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Accessible.name: text
                        Accessible.role: Accessible.StaticText
                        Layout.fillWidth: true
                        color: root.contract.textSecondaryColor
                        objectName: "download-validation-status"
                        text: row.validationText
                        textFormat: Text.PlainText
                        visible: text.length > 0
                        wrapMode: Text.Wrap
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: root.contract.spacingUnit
                        visible: true

                        AccessibleTransportButton {
                            id: cancelButton

                            KeyNavigation.backtab: root.nextRowControl(row, cancelButton, true)
                            KeyNavigation.tab: root.nextRowControl(row, cancelButton, false)
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

                            KeyNavigation.backtab: root.nextRowControl(row, retryButton, true)
                            KeyNavigation.tab: root.nextRowControl(row, retryButton, false)
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
                        AccessibleTransportButton {
                            id: downloadRetryButton

                            KeyNavigation.backtab: root.nextRowControl(row, downloadRetryButton, true)
                            KeyNavigation.tab: root.nextRowControl(row, downloadRetryButton, false)
                            contract: root.contract
                            enabled: row.canRetryDownload && root.opened && row.validationIdentityCurrent()
                            objectName: "download-retry"
                            text: labels.retryDownload
                            visible: row.jobState === "failed"
                            onActiveFocusChanged: {
                                if (activeFocus)
                                    root.showRow(row.index);
                            }
                            onClicked: {
                                if (row.canRetryDownload && root.rowActionAllowed(row) && typeof root.downloads.retryDownload === "function")
                                    root.downloads.retryDownload(row.jobId);
                            }
                        }
                        AccessibleTransportButton {
                            id: reviewButton

                            KeyNavigation.backtab: root.nextRowControl(row, reviewButton, true)
                            KeyNavigation.tab: root.nextRowControl(row, reviewButton, false)
                            contract: root.contract
                            enabled: row.canReviewFiles && !row.reviewBusy
                            objectName: "download-review-files"
                            text: row.reviewBusy ? labels.reviewingFiles : row.reviewText.length > 0 ? labels.reviewFilesAgain : labels.reviewFiles
                            visible: row.canReviewFiles || row.reviewBusy

                            onActiveFocusChanged: {
                                if (activeFocus)
                                    root.showRow(row.index);
                            }
                            onClicked: {
                                if (row.canReviewFiles && !row.reviewBusy && root.downloads !== null)
                                    root.downloads.reviewFiles(row.jobId);
                            }
                        }
                        AccessibleTransportButton {
                            id: discardButton

                            KeyNavigation.backtab: root.nextRowControl(row, discardButton, true)
                            KeyNavigation.tab: root.nextRowControl(row, discardButton, false)
                            contract: root.contract
                            enabled: row.canDiscardPartials && !row.reviewBusy
                            objectName: "download-discard-partials"
                            text: labels.discardPartials
                            visible: row.canDiscardPartials

                            onActiveFocusChanged: {
                                if (activeFocus)
                                    root.showRow(row.index);
                            }
                            onClicked: {
                                if (row.canDiscardPartials && !row.reviewBusy)
                                    root.requestDiscardConsent(row.jobId, discardButton);
                            }
                        }
                        AccessibleTransportButton {
                            id: validateButton

                            KeyNavigation.backtab: root.nextRowControl(row, validateButton, true)
                            KeyNavigation.tab: root.nextRowControl(row, validateButton, false)
                            contract: root.contract
                            enabled: row.canValidateMedia && !row.validationBusy && row.validationIdentityCurrent()
                            objectName: "download-validate-media"
                            text: labels.validateMedia
                            visible: row.canValidateMedia

                            onActiveFocusChanged: {
                                if (activeFocus)
                                    root.showRow(row.index);
                            }
                            onClicked: {
                                if (root.opened && row.canValidateMedia && !row.validationBusy && row.validationIdentityCurrent() && jobsList.itemAtIndex(row.index) === row)
                                    root.downloads.validateMedia(row.validationJobId);
                            }
                        }
                        AccessibleTransportButton {
                            id: importRecoveredButton

                            KeyNavigation.backtab: root.nextRowControl(row, importRecoveredButton, true)
                            KeyNavigation.tab: root.nextRowControl(row, importRecoveredButton, false)
                            contract: root.contract
                            enabled: row.canImportRecovered && !row.validationBusy && row.validationIdentityCurrent()
                            objectName: "download-import-recovered"
                            text: labels.importRecovered
                            visible: row.canImportRecovered

                            onActiveFocusChanged: {
                                if (activeFocus)
                                    root.showRow(row.index);
                            }
                            onClicked: {
                                if (root.opened && row.canImportRecovered && !row.validationBusy && row.validationIdentityCurrent() && jobsList.itemAtIndex(row.index) === row)
                                    root.requestRecoveredImportConsent(row.validationJobId, importRecoveredButton, row);
                            }
                        }
                        AccessibleTransportButton {
                            id: releaseValidationButton

                            KeyNavigation.backtab: root.nextRowControl(row, releaseValidationButton, true)
                            KeyNavigation.tab: root.nextRowControl(row, releaseValidationButton, false)
                            contract: root.contract
                            enabled: row.canReleaseValidation && row.validationIdentityCurrent()
                            objectName: "download-release-validation"
                            text: row.validationBusy ? labels.cancelValidation : labels.releaseValidation
                            visible: row.canReleaseValidation

                            onActiveFocusChanged: {
                                if (activeFocus)
                                    root.showRow(row.index);
                            }
                            onClicked: {
                                if (root.opened && row.canReleaseValidation && row.validationIdentityCurrent() && jobsList.itemAtIndex(row.index) === row)
                                    root.downloads.releaseValidation(row.validationJobId);
                            }
                        }
                        AccessibleTransportButton {
                            id: informationButton

                            KeyNavigation.backtab: root.nextRowControl(row, informationButton, true)
                            KeyNavigation.tab: root.nextRowControl(row, informationButton, false)
                            contract: root.contract
                            enabled: root.opened && row.validationIdentityCurrent() && typeof root.downloads.downloadInformation === "function"
                            objectName: "download-information"
                            text: labels.information
                            onActiveFocusChanged: {
                                if (activeFocus)
                                    root.showRow(row.index);
                            }
                            onClicked: root.showInformation(row, informationButton)
                        }
                    }
                    Text {
                        Accessible.name: text
                        Accessible.role: Accessible.StaticText
                        Layout.fillWidth: true
                        color: root.contract.textSecondaryColor
                        objectName: "download-retry-reason"
                        text: row.retryDownloadReason
                        textFormat: Text.PlainText
                        visible: (row.jobState === "failed" || row.jobState === "interrupted") && text.length > 0
                        wrapMode: Text.Wrap
                    }
                }
            }

            Keys.onPressed: event => {
                if (navigationFocused && count > 0 && (event.modifiers === Qt.NoModifier || event.modifiers === Qt.KeypadModifier) && (event.key === Qt.Key_Home || event.key === Qt.Key_End)) {
                    root.showRow(event.key === Qt.Key_Home ? 0 : count - 1);
                    event.accepted = true;
                } else if (event.key === Qt.Key_Space || event.key === Qt.Key_M || event.key === Qt.Key_F1 || event.key === Qt.Key_F2) {
                    event.accepted = true;
                }
            }

            Rectangle {
                Accessible.ignored: true
                anchors.fill: parent
                border.color: root.contract.focusColor
                border.width: jobsList.navigationFocused && jobsList.currentItem === null ? 3 : 0
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

    onAboutToHide: {
        invalidateInformation();
        invalidateArchiveConsent();
        invalidateCloseConsent();
        invalidateDiscardConsent();
        invalidateImportConsent();
    }
    onArchiveConsentCurrentChanged: {
        if (!archiveConsentCurrent)
            Qt.callLater(function () {
                if (!root.archiveConsentCurrent)
                    root.invalidateArchiveConsent();
            });
    }
    onDiscardConsentCurrentChanged: {
        if (!discardConsentCurrent)
            Qt.callLater(function () {
                if (!root.discardConsentCurrent)
                    root.invalidateDiscardConsent();
            });
    }
    onImportConsentCurrentChanged: {
        if (!importConsentCurrent)
            Qt.callLater(function () {
                if (!root.importConsentCurrent)
                    root.invalidateImportConsent();
            });
    }
    onCloseWithoutSaveAvailableChanged: {
        if (!closeWithoutSaveAvailable)
            invalidateCloseConsent();
    }
    onClosed: {
        lyricsDialog.restoreFocusItem = null;
        lyricsDialog.close();
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
        invalidateInformation();
        invalidateArchiveConsent();
        invalidateCloseConsent();
        invalidateDiscardConsent();
        invalidateImportConsent();
        if (downloads === null)
            close();
    }
    blockingUiOpen: archiveConfirmation.visible || discardConfirmation.visible || importConfirmation.visible || exitConfirmation.visible || informationDialog.visible || lyricsDialog.visible
    onSurfaceOpened: closeButton.forceActiveFocus(Qt.TabFocusReason)
    onWarningChanged: invalidateCloseConsent()

    NavigationText {
        id: navigationLabels
    }
    DownloadText {
        id: labels
    }
    Connections {
        target: root.downloads !== null ? root.downloads.jobsModel : null
        function onDataChanged() {
            root.refreshInformation();
        }
        function onRowsRemoved() {
            root.refreshInformation();
        }
        function onModelReset() {
            root.refreshInformation();
        }
    }
    Popup {
        id: informationDialog
        closePolicy: Popup.CloseOnEscape
        dim: false
        focus: true
        modal: true
        objectName: "download-information-dialog"
        padding: root.contract.spacingUnit * 3
        parent: root.parent
        popupType: Popup.Item
        width: Math.min(root.width, 600)
        height: Math.min(root.height, 480)
        x: root.x + (root.width - width) / 2
        y: root.y + (root.height - height) / 2
        background: Rectangle {
            color: root.contract.surfaceRaisedColor
            border.color: root.contract.borderColor
            border.width: root.contract.highContrast ? 2 : 1
            radius: root.contract.radiusMedium
        }
        contentItem: ColumnLayout {
            Accessible.name: labels.information
            Accessible.role: Accessible.Dialog
            spacing: root.contract.spacingUnit * 2
            ScrollView {
                id: informationScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                TextArea {
                    id: informationDetails
                    width: informationScroll.availableWidth
                    objectName: "download-information-text"
                    text: root.informationText
                    textFormat: TextEdit.PlainText
                    readOnly: true
                    selectByMouse: true
                    activeFocusOnTab: true
                    wrapMode: TextEdit.Wrap
                    color: root.contract.textPrimaryColor
                    selectionColor: root.contract.accentColor
                    selectedTextColor: root.contract.accentTextColor
                    Accessible.name: labels.information
                    KeyNavigation.tab: copyInformationButton
                    KeyNavigation.backtab: closeInformationButton
                    background: Rectangle {
                        color: root.contract.surfaceColor
                        border.width: informationDetails.activeFocus ? 3 : 1
                        border.color: informationDetails.activeFocus ? root.contract.focusColor : root.contract.borderColor
                        radius: root.contract.radiusSmall
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                objectName: "download-copy-feedback"
                text: root.copyFeedback
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: root.contract.textPrimaryColor
                Accessible.name: text
                Accessible.role: Accessible.StaticText
            }
            Flow {
                Layout.fillWidth: true
                spacing: root.contract.spacingUnit
                AccessibleTransportButton {
                    id: copyInformationButton
                    contract: root.contract
                    objectName: "download-copy-information"
                    text: labels.copyInformation
                    KeyNavigation.tab: closeInformationButton
                    KeyNavigation.backtab: informationDetails
                    onClicked: root.copyInformation()
                }
                AccessibleTransportButton {
                    id: closeInformationButton
                    contract: root.contract
                    objectName: "download-information-close"
                    text: labels.closeInformation
                    KeyNavigation.tab: informationDetails
                    KeyNavigation.backtab: copyInformationButton
                    onClicked: root.invalidateInformation()
                }
            }
        }
        onOpened: informationDetails.forceActiveFocus(Qt.TabFocusReason)
        onClosed: {
            root.invalidateInformation();
            const target = root.informationRestoreFocusItem;
            root.informationRestoreFocusItem = null;
            Qt.callLater(function () {
                if (root.opened && !informationDialog.visible) {
                    const destination = target && target.visible && target.enabled ? target : jobsList;
                    destination.forceActiveFocus(Qt.TabFocusReason);
                }
            });
        }
    }
    DownloadLyricsPanel {
        id: lyricsDialog
        contract: root.contract
        backend: root.lyricsBackend
    }
    Popup {
        id: archiveConfirmation

        closePolicy: Popup.CloseOnEscape
        dim: false
        focus: true
        height: archiveContent.implicitHeight + padding * 2
        modal: true
        objectName: "downloads-archive-confirmation"
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
            id: archiveContent

            Accessible.name: labels.archive
            Accessible.role: Accessible.Dialog
            spacing: root.contract.spacingUnit * 3

            Text {
                Layout.fillWidth: true
                color: root.contract.textPrimaryColor
                font.bold: true
                text: labels.archive
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }
            Text {
                Layout.fillWidth: true
                color: root.contract.textPrimaryColor
                text: labels.archiveAdvice
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true

                AccessibleTransportButton {
                    id: cancelArchiveButton

                    KeyNavigation.backtab: confirmArchiveButton
                    KeyNavigation.tab: confirmArchiveButton
                    contract: root.contract
                    objectName: "downloads-cancel-archive"
                    text: labels.cancel

                    onClicked: root.invalidateArchiveConsent()
                }
                Item {
                    Layout.fillWidth: true
                }
                AccessibleTransportButton {
                    id: confirmArchiveButton

                    KeyNavigation.backtab: cancelArchiveButton
                    KeyNavigation.tab: cancelArchiveButton
                    contract: root.contract
                    enabled: root.archiveConsentCurrent
                    objectName: "downloads-confirm-archive"
                    text: labels.confirmArchive

                    onClicked: root.confirmArchiveConsent()
                }
            }
        }

        onClosed: {
            root.invalidateArchiveConsent();
            Qt.callLater(function () {
                if (!root.opened || archiveConfirmation.visible)
                    return;
                const target = archiveButton.visible && archiveButton.enabled ? archiveButton : closeButton;
                target.forceActiveFocus(Qt.TabFocusReason);
            });
        }
        onOpened: cancelArchiveButton.forceActiveFocus(Qt.TabFocusReason)
    }
    Popup {
        id: importConfirmation

        closePolicy: Popup.CloseOnEscape
        dim: false
        focus: true
        height: importContent.implicitHeight + padding * 2
        modal: true
        objectName: "download-import-confirmation"
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
            id: importContent

            Accessible.name: labels.importRecovered
            Accessible.role: Accessible.Dialog
            spacing: root.contract.spacingUnit * 3

            Text {
                Layout.fillWidth: true
                color: root.contract.textPrimaryColor
                font.bold: true
                text: labels.importRecovered
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }
            Text {
                Accessible.name: text
                Accessible.role: Accessible.StaticText
                Layout.fillWidth: true
                color: root.contract.textPrimaryColor
                objectName: "download-import-advice"
                text: labels.importRecoveredAdvice
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true

                AccessibleTransportButton {
                    id: cancelImportButton

                    KeyNavigation.backtab: confirmImportButton
                    KeyNavigation.tab: confirmImportButton
                    contract: root.contract
                    objectName: "download-import-cancel"
                    text: labels.cancel

                    onClicked: root.invalidateImportConsent()
                }
                Item {
                    Layout.fillWidth: true
                }
                AccessibleTransportButton {
                    id: confirmImportButton

                    KeyNavigation.backtab: cancelImportButton
                    KeyNavigation.tab: cancelImportButton
                    contract: root.contract
                    enabled: root.importConsentCurrent
                    objectName: "download-import-confirm"
                    text: labels.importRecovered

                    onClicked: root.confirmImportConsent()
                }
            }
        }
        onClosed: {
            root.invalidateImportConsent();
            const target = root.importRestoreFocusItem;
            root.importRestoreFocusItem = null;
            Qt.callLater(function () {
                if (root.opened) {
                    const destination = target && target.visible && target.enabled ? target : jobsList;
                    destination.forceActiveFocus(Qt.TabFocusReason);
                }
            });
        }
        onOpened: cancelImportButton.forceActiveFocus(Qt.TabFocusReason)
    }
    Popup {
        id: discardConfirmation

        closePolicy: Popup.CloseOnEscape
        dim: false
        focus: true
        height: discardContent.implicitHeight + padding * 2
        modal: true
        objectName: "downloads-discard-confirmation"
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
            id: discardContent

            Accessible.name: labels.confirmDiscard
            Accessible.role: Accessible.Dialog
            spacing: root.contract.spacingUnit * 3

            Text {
                Layout.fillWidth: true
                color: root.contract.textPrimaryColor
                font.bold: true
                text: labels.confirmDiscard
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }
            Text {
                Accessible.name: text
                Accessible.role: Accessible.StaticText
                Layout.fillWidth: true
                color: root.contract.textPrimaryColor
                objectName: "download-discard-advice"
                text: root.discardConsentWithoutFiles ? labels.discardWithoutFilesAdvice : labels.discardPartialsAdvice
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true

                AccessibleTransportButton {
                    id: cancelDiscardButton

                    KeyNavigation.backtab: confirmDiscardButton
                    KeyNavigation.tab: confirmDiscardButton
                    contract: root.contract
                    objectName: "download-discard-cancel"
                    text: labels.cancelDiscard

                    onClicked: root.invalidateDiscardConsent()
                }
                Item {
                    Layout.fillWidth: true
                }
                AccessibleTransportButton {
                    id: confirmDiscardButton

                    KeyNavigation.backtab: cancelDiscardButton
                    KeyNavigation.tab: cancelDiscardButton
                    contract: root.contract
                    enabled: root.discardConsentCurrent
                    objectName: "download-discard-confirm"
                    text: labels.confirmDiscard

                    onClicked: root.confirmDiscardConsent()
                }
            }
        }
        onClosed: {
            root.invalidateDiscardConsent();
            const target = root.discardRestoreFocusItem;
            root.discardRestoreFocusItem = null;
            Qt.callLater(function () {
                if (root.opened) {
                    const destination = target && target.visible && target.enabled ? target : jobsList;
                    destination.forceActiveFocus(Qt.TabFocusReason);
                }
            });
        }
        onOpened: cancelDiscardButton.forceActiveFocus(Qt.TabFocusReason)
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
