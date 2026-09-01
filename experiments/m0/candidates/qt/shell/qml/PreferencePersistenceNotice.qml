import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0 as M0

FocusScope {
    id: root
    objectName: "preference-persistence-notice"
    required property M0.ShellUiContract contract
    required property int warning
    required property int warningScope
    required property Item fallbackFocusItem
    required property bool interactionAllowed
    property Item replacementRestoreFocusItem: null
    property int lastAnnouncedWarning: -1
    property int lastAnnouncedWarningScope: -1
    property bool actionOwnedFocus: false
    property bool focusRestorePending: false
    property int focusTransferRevision: 0
    readonly property bool replacementConfirmationOpen: replacementConfirmation.opened
    readonly property bool dismissAvailable: warning === M0.PlayerPresenter.RecoveredFromBackup || warning === M0.PlayerPresenter.UsingDefaults
    readonly property bool retryAvailable: warning === M0.PlayerPresenter.SaveFailed
    readonly property bool replacementAvailable: warning === M0.PlayerPresenter.FutureVersion
    readonly property bool reviewAvailable: warningScope !== M0.PlayerPresenter.SessionPersistence
    readonly property string warningTitle: titleForWarning()
    readonly property string warningMessage: messageForWarning()
    readonly property string replacementActionText: replacementActionForScope()
    readonly property string replacementConfirmationTitle: replacementTitleForScope()
    readonly property string replacementConfirmationMessage: replacementMessageForScope()
    signal reviewRequested
    signal dismissRequested
    signal retryRequested
    signal replacementConfirmed
    visible: warning !== M0.PlayerPresenter.NoPreferenceWarning
    enabled: visible && interactionAllowed
    implicitHeight: visible ? content.implicitHeight + contract.spacingUnit * 4 : 0
    Accessible.role: Accessible.AlertMessage
    Accessible.name: warningTitle + ". " + warningMessage

    PreferencesText {
        id: labels
    }

    function titleForWarning() {
        if (warningScope === M0.PlayerPresenter.SessionPersistence) {
            switch (warning) {
            case M0.PlayerPresenter.RecoveredFromBackup:
                return labels.sessionRecoveredTitle;
            case M0.PlayerPresenter.UsingDefaults:
                return labels.sessionDefaultsTitle;
            case M0.PlayerPresenter.FutureVersion:
                return labels.sessionFutureVersionTitle;
            case M0.PlayerPresenter.SaveFailed:
                return labels.sessionSaveFailedTitle;
            default:
                return "";
            }
        }
        if (warningScope === M0.PlayerPresenter.CombinedPersistence)
            return labels.combinedWarningTitle;
        switch (warning) {
        case M0.PlayerPresenter.RecoveredFromBackup:
            return labels.recoveredTitle;
        case M0.PlayerPresenter.UsingDefaults:
            return labels.defaultsTitle;
        case M0.PlayerPresenter.FutureVersion:
            return labels.futureVersionTitle;
        case M0.PlayerPresenter.SaveFailed:
            return labels.saveFailedTitle;
        default:
            return "";
        }
    }

    function messageForWarning() {
        if (warningScope === M0.PlayerPresenter.SessionPersistence) {
            switch (warning) {
            case M0.PlayerPresenter.RecoveredFromBackup:
                return labels.sessionRecoveredMessage;
            case M0.PlayerPresenter.UsingDefaults:
                return labels.sessionDefaultsMessage;
            case M0.PlayerPresenter.FutureVersion:
                return labels.sessionFutureVersionMessage;
            case M0.PlayerPresenter.SaveFailed:
                return labels.sessionSaveFailedMessage;
            default:
                return "";
            }
        }
        if (warningScope === M0.PlayerPresenter.CombinedPersistence) {
            switch (warning) {
            case M0.PlayerPresenter.RecoveredFromBackup:
                return labels.combinedRecoveredMessage;
            case M0.PlayerPresenter.UsingDefaults:
                return labels.combinedDefaultsMessage;
            case M0.PlayerPresenter.FutureVersion:
                return labels.combinedFutureVersionMessage;
            case M0.PlayerPresenter.SaveFailed:
                return labels.combinedSaveFailedMessage;
            default:
                return "";
            }
        }
        switch (warning) {
        case M0.PlayerPresenter.RecoveredFromBackup:
            return labels.recoveredMessage;
        case M0.PlayerPresenter.UsingDefaults:
            return labels.defaultsMessage;
        case M0.PlayerPresenter.FutureVersion:
            return labels.futureVersionMessage;
        case M0.PlayerPresenter.SaveFailed:
            return labels.saveFailedMessage;
        default:
            return "";
        }
    }

    function replacementActionForScope() {
        if (warningScope === M0.PlayerPresenter.SessionPersistence)
            return labels.replaceSession;
        if (warningScope === M0.PlayerPresenter.CombinedPersistence)
            return labels.replaceLocalData;
        return labels.replacePreferences;
    }

    function replacementTitleForScope() {
        if (warningScope === M0.PlayerPresenter.SessionPersistence)
            return labels.replaceSessionConfirmationTitle;
        if (warningScope === M0.PlayerPresenter.CombinedPersistence)
            return labels.replaceLocalDataConfirmationTitle;
        return labels.replaceConfirmationTitle;
    }

    function replacementMessageForScope() {
        if (warningScope === M0.PlayerPresenter.SessionPersistence)
            return labels.replaceSessionConfirmationMessage;
        if (warningScope === M0.PlayerPresenter.CombinedPersistence)
            return labels.replaceLocalDataConfirmationMessage;
        return labels.replaceConfirmationMessage;
    }

    function isUsableFocusTarget(item) {
        return item && item.enabled && item.visible;
    }

    function containsFocusItem(item) {
        let current = item;
        while (current) {
            if (current === root)
                return true;
            current = current.parent;
        }
        return false;
    }

    function restoreFallbackAfterWarningChange() {
        const window = root.Window.window;
        const ownedFocus = actionOwnedFocus || (window && containsFocusItem(window.activeFocusItem));
        ++focusTransferRevision;
        focusRestorePending = ownedFocus;
        if (!ownedFocus)
            return;
        schedulePendingFocusRestore();
    }

    function schedulePendingFocusRestore() {
        const revision = focusTransferRevision;
        Qt.callLater(function () {
            if (revision !== root.focusTransferRevision || !root.focusRestorePending)
                return;
            if (!root.interactionAllowed || replacementConfirmation.opened)
                return;
            root.focusRestorePending = false;
            root.actionOwnedFocus = false;
            if (root.isUsableFocusTarget(root.fallbackFocusItem))
                root.fallbackFocusItem.forceActiveFocus(Qt.TabFocusReason);
        });
    }

    function contextualAction() {
        if (dismissAvailable)
            return dismissButton;
        if (retryAvailable)
            return retryButton;
        if (replacementAvailable)
            return replaceButton;
        return fallbackFocusItem;
    }

    function announceWarning() {
        if (warning === M0.PlayerPresenter.NoPreferenceWarning) {
            lastAnnouncedWarning = -1;
            lastAnnouncedWarningScope = -1;
            return;
        }
        if (lastAnnouncedWarning === warning && lastAnnouncedWarningScope === warningScope)
            return;
        lastAnnouncedWarning = warning;
        lastAnnouncedWarningScope = warningScope;
        const politeness = warning === M0.PlayerPresenter.FutureVersion || warning === M0.PlayerPresenter.SaveFailed ? Accessible.Assertive : Accessible.Polite;
        root.Accessible.announce(root.Accessible.name, politeness);
    }

    function scheduleAnnouncement() {
        const observedWarning = warning;
        const observedWarningScope = warningScope;
        Qt.callLater(function () {
            if (root.warning === observedWarning && root.warningScope === observedWarningScope)
                root.announceWarning();
        });
    }

    function openReplacementConfirmation(opener) {
        if (!interactionAllowed || warning !== M0.PlayerPresenter.FutureVersion)
            return;
        replacementRestoreFocusItem = opener;
        replacementConfirmation.open();
    }

    onWarningChanged: {
        restoreFallbackAfterWarningChange();
        if (warning !== M0.PlayerPresenter.FutureVersion && replacementConfirmation.opened)
            replacementConfirmation.close();
        if (warning === M0.PlayerPresenter.NoPreferenceWarning) {
            lastAnnouncedWarning = -1;
            lastAnnouncedWarningScope = -1;
        } else {
            scheduleAnnouncement();
        }
    }
    onWarningScopeChanged: {
        if (warning === M0.PlayerPresenter.NoPreferenceWarning) {
            lastAnnouncedWarningScope = -1;
        } else {
            scheduleAnnouncement();
        }
    }
    onInteractionAllowedChanged: {
        if (!interactionAllowed && replacementConfirmation.opened)
            replacementConfirmation.close();
        else if (interactionAllowed && focusRestorePending)
            schedulePendingFocusRestore();
    }
    Component.onCompleted: scheduleAnnouncement()

    Connections {
        target: root.Window.window
        function onActiveFocusItemChanged() {
            const item = root.Window.window.activeFocusItem;
            if (root.visible && root.interactionAllowed && !root.focusRestorePending && item && !root.containsFocusItem(item))
                root.actionOwnedFocus = false;
        }
    }

    Rectangle {
        id: background
        objectName: "preference-persistence-background"
        anchors.fill: parent
        color: root.contract.surfaceRaisedColor
        radius: root.contract.radiusMedium
        border.width: root.contract.highContrast ? 2 : 1
        border.color: root.contract.highContrast ? root.contract.focusColor : root.contract.warningColor
        Accessible.ignored: true
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: root.contract.spacingUnit * 2
        spacing: root.contract.spacingUnit

        Text {
            objectName: "preference-persistence-title"
            Layout.fillWidth: true
            text: root.warningTitle
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: root.contract.textPrimaryColor
            font.bold: true
            Accessible.ignored: true
        }
        Text {
            objectName: "preference-persistence-message"
            Layout.fillWidth: true
            text: root.warningMessage
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: root.contract.textSecondaryColor
            Accessible.ignored: true
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: root.contract.spacingUnit

            AccessibleTransportButton {
                id: reviewButton
                objectName: "preference-persistence-review"
                contract: root.contract
                text: labels.reviewPreferences
                accessibleName: text
                visible: root.visible && root.reviewAvailable
                enabled: visible && root.enabled
                activeFocusOnTab: visible && root.enabled
                KeyNavigation.tab: root.visible && root.enabled ? root.contextualAction() : null
                KeyNavigation.backtab: root.visible && root.enabled ? root.fallbackFocusItem : null
                onClicked: root.reviewRequested()
                onActiveFocusChanged: {
                    if (activeFocus)
                        root.actionOwnedFocus = true;
                }
            }
            Item {
                Layout.fillWidth: true
            }
            AccessibleTransportButton {
                id: dismissButton
                objectName: "preference-persistence-dismiss"
                contract: root.contract
                text: labels.dismissWarning
                accessibleName: text
                visible: root.visible && root.dismissAvailable
                enabled: visible && root.enabled
                activeFocusOnTab: root.visible && root.enabled && visible
                KeyNavigation.tab: root.visible && root.enabled && visible ? root.fallbackFocusItem : null
                KeyNavigation.backtab: root.visible && root.enabled && visible ? (root.reviewAvailable ? reviewButton : root.fallbackFocusItem) : null
                onClicked: root.dismissRequested()
                onActiveFocusChanged: {
                    if (activeFocus)
                        root.actionOwnedFocus = true;
                }
            }
            AccessibleActionButton {
                id: retryButton
                objectName: "preference-persistence-retry"
                controlId: objectName
                contract: root.contract
                text: labels.retrySave
                visible: root.visible && root.retryAvailable
                enabled: visible && root.enabled
                activeFocusOnTab: root.visible && root.enabled && visible
                KeyNavigation.tab: root.visible && root.enabled && visible ? root.fallbackFocusItem : null
                KeyNavigation.backtab: root.visible && root.enabled && visible ? (root.reviewAvailable ? reviewButton : root.fallbackFocusItem) : null
                onClicked: root.retryRequested()
                onActiveFocusChanged: {
                    if (activeFocus)
                        root.actionOwnedFocus = true;
                }
            }
            AccessibleTransportButton {
                id: replaceButton
                objectName: "preference-persistence-replace"
                contract: root.contract
                text: root.replacementActionText
                accessibleName: text
                visible: root.visible && root.replacementAvailable
                enabled: visible && root.enabled
                activeFocusOnTab: root.visible && root.enabled && visible
                KeyNavigation.tab: root.visible && root.enabled && visible ? root.fallbackFocusItem : null
                KeyNavigation.backtab: root.visible && root.enabled && visible ? (root.reviewAvailable ? reviewButton : root.fallbackFocusItem) : null
                onClicked: root.openReplacementConfirmation(replaceButton)
                onActiveFocusChanged: {
                    if (activeFocus)
                        root.actionOwnedFocus = true;
                }
            }
        }
    }

    Popup {
        id: replacementConfirmation
        objectName: "preference-replacement-confirmation"
        parent: root.Window.window ? root.Window.window.contentItem : root
        width: Math.min(parent.width - root.contract.spacingUnit * 12, 560)
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        modal: true
        dim: false
        focus: true
        popupType: Popup.Item
        closePolicy: Popup.CloseOnEscape
        padding: root.contract.spacingUnit * 4

        onOpened: Qt.callLater(function () {
            cancelButton.forceActiveFocus(Qt.TabFocusReason);
        })
        onClosed: {
            const requested = root.replacementRestoreFocusItem;
            root.replacementRestoreFocusItem = null;
            if (root.interactionAllowed)
                Qt.callLater(function () {
                    if (replacementConfirmation.opened || !root.interactionAllowed)
                        return;
                    const destination = root.isUsableFocusTarget(requested) ? requested : root.fallbackFocusItem;
                    if (root.isUsableFocusTarget(destination))
                        destination.forceActiveFocus(Qt.TabFocusReason);
                });
        }

        background: Rectangle {
            objectName: "preference-replacement-background"
            color: root.contract.surfaceRaisedColor
            radius: root.contract.radiusMedium
            border.width: root.contract.highContrast ? 2 : 1
            border.color: root.contract.highContrast ? root.contract.focusColor : root.contract.borderColor
        }

        contentItem: ColumnLayout {
            spacing: root.contract.spacingUnit * 3
            Accessible.role: Accessible.Dialog
            Accessible.name: root.replacementConfirmationTitle

            Text {
                Layout.fillWidth: true
                text: root.replacementConfirmationTitle
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: root.contract.textPrimaryColor
                font.pixelSize: 20
                font.bold: true
                Accessible.ignored: true
            }
            Text {
                objectName: "preference-replacement-message"
                Layout.fillWidth: true
                text: root.replacementConfirmationMessage
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: root.contract.textPrimaryColor
                Accessible.id: objectName
                Accessible.name: text
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                AccessibleTransportButton {
                    id: cancelButton
                    objectName: "preference-replacement-cancel"
                    contract: root.contract
                    text: labels.cancelReplacement
                    accessibleName: text
                    activeFocusOnTab: replacementConfirmation.opened
                    KeyNavigation.tab: confirmButton
                    KeyNavigation.backtab: confirmButton
                    onClicked: replacementConfirmation.close()
                }
                AccessibleActionButton {
                    id: confirmButton
                    objectName: "preference-replacement-confirm"
                    controlId: objectName
                    contract: root.contract
                    text: labels.confirmReplacement
                    activeFocusOnTab: replacementConfirmation.opened
                    KeyNavigation.tab: cancelButton
                    KeyNavigation.backtab: cancelButton
                    onClicked: {
                        replacementConfirmation.close();
                        root.replacementConfirmed();
                    }
                }
            }
        }
    }
}
