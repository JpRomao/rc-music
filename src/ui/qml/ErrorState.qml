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
    property string bodyText: presenter.usesFrozenFixture ? contract.errorBody : labels.messageFor(presenter.errorMessageKey, presenter.audioOutputSelectionSupported)
    property string actionText: presenter.usesFrozenFixture ? contract.actionSkip : labels.actionFor(presenter.errorActionId)
    readonly property bool outputRecovery: !presenter.usesFrozenFixture && presenter.errorActionId === "recover-audio-output"
    // The Aurora look: the empty-state piece, with the recovery actions in a row.
    readonly property bool editorial: !presenter.usesFrozenFixture
    // The recovery button's id; named controlId so the text scanner reads it as an id.
    readonly property string controlId: presenter.usesFrozenFixture ? "error-skip" : (outputRecovery ? "error-recover-audio-output" : "error-open-audio")
    readonly property string recoveryDescription: outputRecovery ? labels.recoverOutputDescription : ""
    readonly property bool idleForActions: visible && !presenter.exiting && !presenter.selectingAudioFiles && presenter.locatingOccurrenceId.length === 0
    readonly property bool recoveryEnabled: idleForActions
    readonly property bool openAnotherVisible: outputRecovery
    readonly property bool openAnotherEnabled: openAnotherVisible && idleForActions
    readonly property bool retryVisible: !presenter.usesFrozenFixture && presenter.canRetryCurrentOccurrence
    readonly property bool retryEnabled: retryVisible && idleForActions && presenter.retryingOccurrenceId.length === 0
    readonly property bool locateVisible: !presenter.usesFrozenFixture && presenter.canLocateCurrentOccurrence
    readonly property bool locateEnabled: locateVisible && presenter.fileLocationAvailable && idleForActions && presenter.retryingOccurrenceId.length === 0
    property bool locationFocusRestoreAllowed: true
    property string requestedLocationId: ""
    signal audioFilesOpening(Item opener)
    signal detailsRequested(Item opener)
    Accessible.role: Accessible.AlertMessage
    Accessible.name: titleText + (bodyText.length > 0 ? ". " + bodyText : "")

    function focusPrimary() {
        if (actions.item)
            actions.item.recovery.forceActiveFocus(Qt.TabFocusReason);
    }

    // What the recovery button does, shared by the fixture and the Aurora buttons.
    function activateRecovery(opener) {
        if (!recoveryEnabled)
            return;
        if (!presenter.usesFrozenFixture && !outputRecovery)
            audioFilesOpening(opener);
        presenter.executeErrorAction();
    }

    function openAnotherAudio(opener) {
        if (!openAnotherEnabled)
            return;
        audioFilesOpening(opener);
        presenter.openAudio();
    }

    function retryCurrent() {
        if (retryEnabled)
            presenter.retryQueueOccurrence(presenter.currentOccurrenceId);
    }

    function locateCurrent() {
        if (!locateEnabled)
            return;
        const occurrenceId = root.presenter.currentOccurrenceId;
        root.requestedLocationId = occurrenceId;
        if (!root.presenter.locateQueueOccurrence(occurrenceId))
            root.requestedLocationId = "";
    }

    AuroraIconCircle {
        objectName: "error-icon-circle"
        visible: root.editorial
        contract: root.contract
        iconName: "warning-circle"
        iconColor: root.contract.highContrast ? root.contract.textPrimaryColor : root.contract.errorColor
        Layout.alignment: Qt.AlignHCenter
        Layout.bottomMargin: 8
    }
    Text {
        objectName: "error-title"
        text: root.titleText
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        font: root.editorial ? Qt.font({
            family: root.contract.headingFontFamily,
            pixelSize: root.contract.typeTitle
        }) : root.contract.applicationFont
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
        font: root.editorial ? Qt.font({
            family: root.contract.bodyFontFamily,
            pixelSize: root.contract.typeBody
        }) : root.contract.applicationFont
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
        horizontalAlignment: root.editorial ? Text.AlignHCenter : Text.AlignLeft
        font: root.editorial ? Qt.font({
            family: root.contract.bodyFontFamily,
            pixelSize: root.contract.typeCaption
        }) : root.contract.applicationFont
        Accessible.name: text
        color: root.contract.textSecondaryColor
        Layout.fillWidth: true
        Layout.maximumWidth: 560
        Layout.alignment: Qt.AlignHCenter
    }
    // One Loader picks the buttons: the two of the fixture, or the Aurora row.
    // Both items expose the buttons the code here needs: recovery and locate.
    Loader {
        id: actions
        sourceComponent: root.editorial ? auroraActions : fixtureActions
        Layout.fillWidth: true
        Layout.maximumWidth: 560
        Layout.alignment: Qt.AlignHCenter
    }
    Text {
        id: locationFeedback
        objectName: "error-location-feedback"
        text: root.labels.locationFailed
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        font: root.editorial ? Qt.font({
            family: root.contract.bodyFontFamily,
            pixelSize: root.contract.typeBody
        }) : root.contract.applicationFont
        color: root.contract.textSecondaryColor
        visible: root.presenter.fileLocationErrorOccurrenceId.length > 0 && root.presenter.fileLocationErrorOccurrenceId === root.presenter.currentOccurrenceId
        Accessible.role: Accessible.StaticText
        Accessible.name: text
        Layout.fillWidth: true
        Layout.maximumWidth: 560
        Layout.alignment: Qt.AlignHCenter
    }

    // The fixture: the skip button and the details button, as they have always been.
    Component {
        id: fixtureActions
        ColumnLayout {
            readonly property Item recovery: recoveryButton
            readonly property Item locate: null
            AccessibleActionButton {
                id: recoveryButton
                controlId: root.controlId
                contract: root.contract
                text: root.actionText
                enabled: root.recoveryEnabled
                Layout.fillWidth: true
                Layout.maximumWidth: 560
                Layout.alignment: Qt.AlignHCenter
                onClicked: root.activateRecovery(recoveryButton)
            }
            AccessibleTransportButton {
                id: fixtureDetailsButton
                objectName: "track-details-error"
                contract: root.contract
                text: root.contract.detailsTitle
                accessibleName: root.contract.detailsTitle
                enabled: root.presenter.detailsAvailable
                Layout.alignment: Qt.AlignHCenter
                onClicked: root.detailsRequested(fixtureDetailsButton)
            }
        }
    }

    // The Aurora look: every recovery action in one centered row that wraps.
    Component {
        id: auroraActions
        Item {
            id: auroraRow
            readonly property Item recovery: recoveryAction
            readonly property Item locate: locateAction
            readonly property real naturalWidth: {
                let total = 0;
                let count = 0;
                for (const button of [recoveryAction, openAnotherAction, retryAction, locateAction, detailsAction]) {
                    if (!button.visible)
                        continue;
                    total += button.implicitWidth;
                    count += 1;
                }
                return total + Math.max(0, count - 1) * flow.spacing;
            }
            implicitHeight: flow.implicitHeight
            Flow {
                id: flow
                width: Math.min(auroraRow.width, auroraRow.naturalWidth)
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 8
                AuroraButton {
                    id: recoveryAction
                    contract: root.contract
                    controlId: root.controlId
                    kind: AuroraButton.Primary
                    text: root.actionText
                    description: root.recoveryDescription
                    enabled: root.recoveryEnabled
                    onClicked: root.activateRecovery(recoveryAction)
                }
                AuroraButton {
                    id: openAnotherAction
                    contract: root.contract
                    controlId: "error-open-another-audio"
                    kind: AuroraButton.Tonal
                    text: root.labels.openAudioAction
                    visible: root.openAnotherVisible
                    enabled: root.openAnotherEnabled
                    onClicked: root.openAnotherAudio(openAnotherAction)
                }
                AuroraButton {
                    id: retryAction
                    contract: root.contract
                    controlId: "error-retry-occurrence"
                    kind: AuroraButton.Tonal
                    text: root.labels.retryAction
                    description: root.labels.retryDescription
                    visible: root.retryVisible
                    enabled: root.retryEnabled
                    onClicked: root.retryCurrent()
                }
                AuroraButton {
                    id: locateAction
                    contract: root.contract
                    controlId: "error-locate-occurrence"
                    kind: AuroraButton.Tonal
                    text: root.labels.locateAction
                    description: root.labels.locateDescription
                    visible: root.locateVisible
                    enabled: root.locateEnabled
                    onClicked: root.locateCurrent()
                }
                AuroraButton {
                    id: detailsAction
                    contract: root.contract
                    controlId: "track-details-error"
                    kind: AuroraButton.Tonal
                    iconName: "info"
                    text: root.contract.detailsTitle
                    Accessible.name: root.contract.detailsTitle
                    enabled: root.presenter.detailsAvailable
                    onClicked: root.detailsRequested(detailsAction)
                }
            }
        }
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
                const locate = actions.item ? actions.item.locate : null;
                if (locate && locate.visible && locate.enabled)
                    locate.forceActiveFocus(Qt.TabFocusReason);
                if (locationFeedback.visible)
                    locationFeedback.Accessible.announce(locationFeedback.text, Accessible.Polite);
            });
        }
    }
}
