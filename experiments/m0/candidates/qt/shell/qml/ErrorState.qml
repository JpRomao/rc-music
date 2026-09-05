import QtQuick
import QtQuick.Layouts
import MusicPlayer.M0

ColumnLayout {
    id: root
    objectName: "error-state"
    width: Math.min(parent ? parent.width : 560, 560)
    required property ShellUiContract contract
    required property PlayerPresenter presenter
    required property ErrorText labels
    property string titleText: presenter.usesFrozenFixture ? contract.errorTitle : labels.titleFor(presenter.errorMessageKey)
    property string bodyText: presenter.usesFrozenFixture ? contract.errorBody : labels.messageFor(presenter.errorMessageKey)
    property string actionText: presenter.usesFrozenFixture ? contract.actionSkip : labels.actionFor(presenter.errorActionId)
    readonly property bool outputRecovery: !presenter.usesFrozenFixture && presenter.errorActionId === "recover-audio-output"
    property bool locationFocusRestoreAllowed: true
    property string requestedLocationId: ""
    signal audioFilesOpening(Item opener)
    signal detailsRequested(Item opener)
    Accessible.role: Accessible.AlertMessage
    Accessible.name: titleText + (bodyText.length > 0 ? ". " + bodyText : "")

    function focusPrimary() {
        recoveryButton.forceActiveFocus(Qt.TabFocusReason);
    }

    function locateCurrent() {
        if (!locateButton.visible || !locateButton.enabled)
            return;
        const occurrenceId = root.presenter.currentOccurrenceId;
        root.requestedLocationId = occurrenceId;
        if (!root.presenter.locateQueueOccurrence(occurrenceId))
            root.requestedLocationId = "";
    }

    Text {
        objectName: "error-title"
        text: root.titleText
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        color: root.contract.textPrimaryColor
        Layout.fillWidth: true
        Layout.maximumWidth: 560
        Layout.alignment: Qt.AlignHCenter
    }
    Text {
        objectName: "error-body"
        visible: root.bodyText.length > 0
        text: root.bodyText
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        color: root.contract.textSecondaryColor
        Layout.fillWidth: true
        Layout.maximumWidth: 560
        Layout.alignment: Qt.AlignHCenter
    }
    Text {
        objectName: "error-context-title"
        text: root.presenter.contextTitle
        textFormat: Text.PlainText
        elide: Text.ElideRight
        Accessible.name: text
        color: root.contract.textSecondaryColor
        Layout.fillWidth: true
        Layout.maximumWidth: 560
        Layout.alignment: Qt.AlignHCenter
    }
    AccessibleActionButton {
        id: recoveryButton
        controlId: root.presenter.usesFrozenFixture ? "error-skip" : (root.outputRecovery ? "error-recover-audio-output" : "error-open-audio")
        contract: root.contract
        text: root.actionText
        Accessible.description: root.outputRecovery ? root.labels.recoverOutputDescription : ""
        enabled: root.visible && !root.presenter.exiting && !root.presenter.selectingAudioFiles && root.presenter.locatingOccurrenceId.length === 0
        Layout.fillWidth: true
        Layout.maximumWidth: 560
        Layout.alignment: Qt.AlignHCenter
        onClicked: {
            if (!enabled || !visible)
                return;
            if (!root.presenter.usesFrozenFixture && !root.outputRecovery)
                root.audioFilesOpening(recoveryButton);
            root.presenter.executeErrorAction();
        }
    }
    AccessibleActionButton {
        id: openAnotherButton
        controlId: "error-open-another-audio"
        contract: root.contract
        text: root.labels.openAudioAction
        visible: root.outputRecovery
        enabled: visible && !root.presenter.exiting && !root.presenter.selectingAudioFiles && root.presenter.locatingOccurrenceId.length === 0
        Layout.fillWidth: true
        Layout.maximumWidth: 560
        Layout.alignment: Qt.AlignHCenter
        onClicked: {
            if (!enabled || !visible)
                return;
            root.audioFilesOpening(openAnotherButton);
            root.presenter.openAudio();
        }
    }
    AccessibleActionButton {
        id: retryButton
        controlId: "error-retry-occurrence"
        contract: root.contract
        text: root.labels.retryAction
        Accessible.description: root.labels.retryDescription
        visible: !root.presenter.usesFrozenFixture && root.presenter.canRetryCurrentOccurrence
        enabled: visible && !root.presenter.exiting && !root.presenter.selectingAudioFiles && root.presenter.retryingOccurrenceId.length === 0 && root.presenter.locatingOccurrenceId.length === 0
        Layout.fillWidth: true
        Layout.maximumWidth: 560
        Layout.alignment: Qt.AlignHCenter
        onClicked: {
            if (enabled && visible)
                root.presenter.retryQueueOccurrence(root.presenter.currentOccurrenceId);
        }
    }
    AccessibleActionButton {
        id: locateButton
        controlId: "error-locate-occurrence"
        contract: root.contract
        text: root.labels.locateAction
        Accessible.description: root.labels.locateDescription
        visible: !root.presenter.usesFrozenFixture && root.presenter.canLocateCurrentOccurrence
        enabled: visible && root.presenter.fileLocationAvailable && !root.presenter.exiting && !root.presenter.selectingAudioFiles && root.presenter.retryingOccurrenceId.length === 0 && root.presenter.locatingOccurrenceId.length === 0
        Layout.fillWidth: true
        Layout.maximumWidth: 560
        Layout.alignment: Qt.AlignHCenter
        onClicked: root.locateCurrent()
    }
    Text {
        id: locationFeedback
        objectName: "error-location-feedback"
        text: root.labels.locationFailed
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        color: root.contract.textSecondaryColor
        visible: root.presenter.fileLocationErrorOccurrenceId.length > 0 && root.presenter.fileLocationErrorOccurrenceId === root.presenter.currentOccurrenceId
        Accessible.role: Accessible.StaticText
        Accessible.name: text
        Layout.fillWidth: true
        Layout.maximumWidth: 560
        Layout.alignment: Qt.AlignHCenter
    }
    AccessibleTransportButton {
        id: detailsButton
        objectName: "track-details-error"
        contract: root.contract
        text: root.contract.detailsTitle
        accessibleName: root.contract.detailsTitle
        enabled: root.presenter.detailsAvailable
        Layout.alignment: Qt.AlignHCenter
        onClicked: root.detailsRequested(detailsButton)
    }

    Connections {
        target: root.presenter
        function onFileLocationFinished(occurrenceId, changed) {
            if (occurrenceId !== root.requestedLocationId)
                return;
            root.requestedLocationId = "";
            Qt.callLater(function () {
                if (!root.visible || !root.locationFocusRestoreAllowed || root.presenter.exiting || root.presenter.locatingOccurrenceId.length > 0 || root.presenter.currentOccurrenceId !== occurrenceId)
                    return;
                if (locateButton.visible && locateButton.enabled)
                    locateButton.forceActiveFocus(Qt.TabFocusReason);
                if (locationFeedback.visible)
                    locationFeedback.Accessible.announce(locationFeedback.text, Accessible.Polite);
            });
        }
    }
}
