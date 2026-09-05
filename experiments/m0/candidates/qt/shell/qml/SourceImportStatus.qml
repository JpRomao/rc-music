import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

FocusScope {
    id: root
    objectName: "source-import-status"
    required property ShellUiContract contract
    required property PlayerPresenter presenter
    property string accessibilityAnnouncement: ""
    property int lastAnnouncedState: -1
    property bool actionOwnedFocus: false
    property bool restoreFocusPending: false
    property int focusTransferRevision: 0
    property bool focusTakeoverAllowed: true
    readonly property bool active: presenter.sourceImportState === PlayerPresenter.Discovering || presenter.sourceImportState === PlayerPresenter.Cancelling
    readonly property bool terminal: presenter.sourceImportState === PlayerPresenter.Completed || presenter.sourceImportState === PlayerPresenter.CompletedWithIssues || presenter.sourceImportState === PlayerPresenter.Cancelled || presenter.sourceImportState === PlayerPresenter.Rejected
    readonly property bool recoveryAvailable: presenter.sourceImportState === PlayerPresenter.CompletedWithIssues || presenter.sourceImportState === PlayerPresenter.Rejected
    visible: presenter.sourceImportState !== PlayerPresenter.Idle
    implicitHeight: visible ? content.implicitHeight + contract.spacingUnit * 4 : 0
    Accessible.role: Accessible.StaticText
    Accessible.name: title.text + ". " + progress.text + (advice.visible ? ". " + advice.text : "")
    signal focusRestoreRequested
    signal audioFilesOpening(Item opener)

    SourceImportText {
        id: sourceText
    }

    function replaceCounts(template) {
        return template.replace("{accepted}", presenter.sourceImportAcceptedCount).replace("{inspected}", presenter.sourceImportInspectedCount).replace("{issues}", presenter.sourceImportIssueCount);
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

    function titleForState() {
        switch (presenter.sourceImportState) {
        case PlayerPresenter.Discovering:
            return sourceText.discovering;
        case PlayerPresenter.Cancelling:
            return sourceText.cancelling;
        case PlayerPresenter.Completed:
            return sourceText.completed;
        case PlayerPresenter.CompletedWithIssues:
            return sourceText.completedWithIssues;
        case PlayerPresenter.Cancelled:
            return sourceText.cancelled;
        case PlayerPresenter.Rejected:
            return sourceText.rejected;
        default:
            return "";
        }
    }

    function terminalAnnouncement() {
        switch (presenter.sourceImportState) {
        case PlayerPresenter.Completed:
        case PlayerPresenter.CompletedWithIssues:
            return replaceCounts(sourceText.completedAnnouncement);
        case PlayerPresenter.Cancelled:
            return replaceCounts(sourceText.cancelledAnnouncement);
        case PlayerPresenter.Rejected:
            return replaceCounts(sourceText.rejectedAnnouncement);
        default:
            return "";
        }
    }

    function adviceForState() {
        switch (presenter.sourceImportState) {
        case PlayerPresenter.CompletedWithIssues:
            return sourceText.completedWithIssuesAdvice;
        case PlayerPresenter.Rejected:
            return sourceText.rejectedAdvice;
        default:
            return "";
        }
    }

    function terminalAction() {
        return recoveryButton.visible ? recoveryButton : dismissButton;
    }

    function updateAnnouncement() {
        const state = presenter.sourceImportState;
        ++focusTransferRevision;
        if (state === PlayerPresenter.Idle) {
            const shouldRestoreFocus = restoreFocusPending;
            restoreFocusPending = false;
            actionOwnedFocus = false;
            if (shouldRestoreFocus) {
                Qt.callLater(function () {
                    if (root.presenter.sourceImportState === PlayerPresenter.Idle)
                        root.focusRestoreRequested();
                });
            }
        } else if (state === PlayerPresenter.Discovering) {
            restoreFocusPending = false;
            actionOwnedFocus = cancelButton.activeFocus;
        } else if (terminal && actionOwnedFocus) {
            const revision = focusTransferRevision;
            Qt.callLater(function () {
                if (revision !== root.focusTransferRevision || !root.terminal || !root.actionOwnedFocus)
                    return;
                if (!root.focusTakeoverAllowed) {
                    root.actionOwnedFocus = false;
                    return;
                }
                const action = root.terminalAction();
                if (root.visible && action.visible && action.enabled)
                    action.forceActiveFocus(Qt.TabFocusReason);
            });
        }
        if (state === PlayerPresenter.Idle || state === PlayerPresenter.Discovering || state === PlayerPresenter.Cancelling) {
            lastAnnouncedState = -1;
            accessibilityAnnouncement = "";
            return;
        }
        if (state === lastAnnouncedState)
            return;
        lastAnnouncedState = state;
        accessibilityAnnouncement = terminalAnnouncement();
        if (accessibilityAnnouncement.length > 0) {
            const politeness = state === PlayerPresenter.CompletedWithIssues || state === PlayerPresenter.Rejected ? Accessible.Assertive : Accessible.Polite;
            root.Accessible.announce(accessibilityAnnouncement, politeness);
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.contract.surfaceRaisedColor
        radius: root.contract.radiusMedium
        border.width: root.contract.highContrast ? 2 : 1
        border.color: root.presenter.sourceImportState === PlayerPresenter.CompletedWithIssues || root.presenter.sourceImportState === PlayerPresenter.Rejected ? root.contract.warningColor : root.contract.borderColor
    }

    RowLayout {
        id: content
        anchors.fill: parent
        anchors.margins: root.contract.spacingUnit * 2
        spacing: root.contract.spacingUnit * 2

        BusyIndicator {
            objectName: "source-import-busy-indicator"
            running: root.active && !root.contract.reducedMotion
            visible: root.active
            implicitWidth: root.contract.controlTargetMinimum
            implicitHeight: root.contract.controlTargetMinimum
            Accessible.ignored: true
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            Text {
                id: title
                text: root.titleForState()
                textFormat: Text.PlainText
                color: root.contract.textPrimaryColor
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
                Accessible.ignored: true
            }

            Text {
                id: progress
                objectName: "source-import-progress"
                text: root.replaceCounts(sourceText.progress)
                textFormat: Text.PlainText
                color: root.contract.textSecondaryColor
                elide: Text.ElideRight
                Layout.fillWidth: true
                Accessible.ignored: true
            }

            Text {
                id: advice
                objectName: "source-import-advice"
                text: root.adviceForState()
                textFormat: Text.PlainText
                color: root.contract.textSecondaryColor
                wrapMode: Text.WordWrap
                visible: text.length > 0
                Layout.fillWidth: true
                Accessible.ignored: true
            }
        }

        QueueActionButton {
            id: cancelButton
            controlId: "source-import-cancel"
            accessibleName: sourceText.cancel
            text: "×"
            contract: root.contract
            visible: root.active
            enabled: root.presenter.sourceImportState === PlayerPresenter.Discovering
            onClicked: root.presenter.cancelSourceImport()
            onActiveFocusChanged: {
                if (activeFocus)
                    root.actionOwnedFocus = true;
                else if (root.presenter.sourceImportState === PlayerPresenter.Discovering)
                    root.actionOwnedFocus = false;
            }
        }

        AccessibleActionButton {
            id: recoveryButton
            controlId: "source-import-recover"
            text: sourceText.recover
            contract: root.contract
            visible: root.recoveryAvailable
            enabled: visible && !root.presenter.exiting && !root.presenter.selectingAudioFiles && root.presenter.locatingOccurrenceId.length === 0
            onClicked: {
                root.restoreFocusPending = false;
                root.audioFilesOpening(recoveryButton);
                if (!root.presenter.dismissSourceImportStatus()) {
                    root.restoreFocusPending = false;
                    return;
                }
                root.presenter.openAudio();
            }
            onActiveFocusChanged: {
                if (activeFocus)
                    root.actionOwnedFocus = true;
                else if (root.terminal)
                    root.actionOwnedFocus = false;
            }
        }

        QueueActionButton {
            id: dismissButton
            controlId: "source-import-dismiss"
            accessibleName: sourceText.dismiss
            text: "✓"
            contract: root.contract
            visible: root.terminal
            enabled: root.terminal
            onClicked: {
                root.restoreFocusPending = activeFocus;
                if (!root.presenter.dismissSourceImportStatus())
                    root.restoreFocusPending = false;
            }
            onActiveFocusChanged: {
                if (activeFocus)
                    root.actionOwnedFocus = true;
                else if (root.terminal)
                    root.actionOwnedFocus = false;
            }
        }
    }

    onFocusTakeoverAllowedChanged: {
        if (!focusTakeoverAllowed)
            actionOwnedFocus = false;
    }

    Connections {
        target: root.presenter
        function onSourceImportChanged() {
            root.updateAnnouncement();
        }
    }

    Connections {
        target: root.Window.window
        enabled: root.actionOwnedFocus && root.active
        function onActiveFocusItemChanged() {
            const item = root.Window.window.activeFocusItem;
            if (item && !root.containsFocusItem(item))
                root.actionOwnedFocus = false;
        }
    }

    Component.onCompleted: updateAnnouncement()
}
