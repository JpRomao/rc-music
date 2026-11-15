import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as NativeDialogs
import QtQuick.Layouts
import MusicPlayer.M0

PageSurface {
    id: root
    objectName: "preferences-panel"
    required property ShellUiContract contract
    required property PlayerPresenter presenter
    property var downloads: null
    property int section: 0
    property var directoryChoiceOwner: null
    property string announcedDefaultAppsSettingsFeedback: ""
    property bool audioOutputSelectionRejected: false
    readonly property bool downloadFolderAvailable: downloads !== null && typeof downloads.setDownloadDirectory === "function" && typeof downloads.downloadDirectoryText === "string" && downloads.downloadDirectoryText.length > 0
    readonly property var audioOutputDevices: presenter.audioOutputSelectionSupported ? presenter.audioOutputDevices : []
    readonly property int selectedAudioOutputIndex: {
        const id = presenter.selectedAudioOutputId;
        if (id.length === 0)
            return 0;
        const index = audioOutputDevices.findIndex(device => device.id === id);
        return index < 0 ? -1 : index + 1;
    }
    modal: !embedded
    dim: false
    focus: true
    clip: true
    popupType: Popup.Item
    closePolicy: Popup.CloseOnEscape
    padding: contract.spacingUnit * 3

    PreferencesText {
        id: labels
    }
    function focusFirst() {
        generalButton.forceActiveFocus(Qt.TabFocusReason);
    }
    function chooseDownloadDirectory() {
        if (!opened || section !== 3 || !changeDownloadDirectoryButton.enabled || downloadDirectoryPicker.visible)
            return;
        directoryChoiceOwner = downloads;
        downloadDirectoryPicker.currentFolder = downloads.downloadDirectory;
        downloadDirectoryPicker.open();
        if (!downloadDirectoryPicker.visible)
            directoryChoiceOwner = null;
    }
    function finishDownloadDirectoryChoice(directory) {
        const owner = directoryChoiceOwner;
        directoryChoiceOwner = null;
        if (opened && section === 3 && downloadFolderAvailable && owner === downloads && !presenter.exiting)
            owner.setDownloadDirectory(directory);
        restoreDownloadDirectoryFocus();
    }
    function cancelDownloadDirectoryChoice() {
        directoryChoiceOwner = null;
        if (downloadDirectoryPicker.visible)
            downloadDirectoryPicker.close();
    }
    function restoreDownloadDirectoryFocus() {
        Qt.callLater(function () {
            if (root.opened && root.section === 3 && !downloadDirectoryPicker.visible) {
                const target = changeDownloadDirectoryButton.enabled ? changeDownloadDirectoryButton : downloadsSectionButton;
                target.forceActiveFocus(Qt.TabFocusReason);
            }
        });
    }
    function selectAudioOutput(index) {
        if (!opened || section !== 1 || !presenter.audioOutputSelectionSupported || presenter.exiting || index < 0 || index > audioOutputDevices.length)
            return;
        const id = index === 0 ? "" : audioOutputDevices[index - 1].id;
        audioOutputSelectionRejected = !presenter.selectAudioOutput(id);
    }
    function announceDefaultAppsSettingsFeedback() {
        const feedback = defaultAppsSettingsFeedback.visible ? defaultAppsSettingsFeedback.text : "";
        if (feedback.length === 0) {
            announcedDefaultAppsSettingsFeedback = "";
            return;
        }
        if (!root.opened || !root.visible || feedback === announcedDefaultAppsSettingsFeedback)
            return;
        announcedDefaultAppsSettingsFeedback = feedback;
        defaultAppsSettingsFeedback.Accessible.announce(feedback, Accessible.Assertive);
    }
    onAboutToShow: {
        section = 0;
        audioOutputSelectionRejected = false;
        backwardField.resetDraft();
        forwardField.resetDraft();
    }
    blockingUiOpen: downloadDirectoryPicker.visible
    onSurfaceOpened: focusFirst()
    onAboutToHide: cancelDownloadDirectoryChoice()
    onDownloadsChanged: cancelDownloadDirectoryChoice()
    onSectionChanged: {
        if (section !== 3)
            cancelDownloadDirectoryChoice();
    }
    onDownloadFolderAvailableChanged: {
        if (!downloadFolderAvailable && section === 3) {
            section = 0;
            if (opened)
                focusFirst();
        }
    }
    onClosed: {
        backwardField.resetDraft();
        forwardField.resetDraft();
        announcedDefaultAppsSettingsFeedback = "";
    }
    NativeDialogs.FolderDialog {
        id: downloadDirectoryPicker

        objectName: "preferences-download-directory-picker"
        modality: Qt.WindowModal
        parentWindow: root.parent ? root.parent.Window.window : null
        popupType: Popup.Item
        title: labels.chooseDownloadDirectory

        onAccepted: root.finishDownloadDirectoryChoice(selectedFolder)
        onRejected: {
            root.directoryChoiceOwner = null;
            root.restoreDownloadDirectoryFocus();
        }
    }
    background: Rectangle {
        color: root.contract.surfaceRaisedColor
        radius: root.contract.radiusMedium
        border.width: 1
        border.color: root.contract.borderColor
    }
    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: root.contract.panelTransitionMs
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1
            to: 0
            duration: root.contract.panelTransitionMs
        }
    }
    contentItem: ColumnLayout {
        spacing: root.contract.spacingUnit * 2
        Accessible.role: root.embedded ? Accessible.Pane : Accessible.Dialog
        Accessible.name: root.contract.preferencesTitle
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: root.contract.preferencesTitle
                textFormat: Text.PlainText
                color: root.contract.textPrimaryColor
                font.pixelSize: 20
                font.bold: true
                Layout.fillWidth: true
            }
            AccessibleTransportButton {
                objectName: "preferences-close"
                contract: root.contract
                text: "✕"
                accessibleName: labels.close
                onClicked: root.close()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            AccessibleTransportButton {
                id: generalButton
                objectName: "preferences-general"
                Layout.fillWidth: true
                contract: root.contract
                text: root.contract.sectionGeneral
                checkable: true
                autoExclusive: true
                checked: root.section === 0
                Accessible.checkable: true
                Accessible.checked: checked
                onClicked: root.section = 0
            }
            AccessibleTransportButton {
                objectName: "preferences-playback"
                Layout.fillWidth: true
                contract: root.contract
                text: root.contract.sectionPlayback
                checkable: true
                autoExclusive: true
                checked: root.section === 1
                Accessible.checkable: true
                Accessible.checked: checked
                onClicked: root.section = 1
            }
            AccessibleTransportButton {
                objectName: "preferences-appearance"
                Layout.fillWidth: true
                contract: root.contract
                text: root.contract.sectionAppearance
                checkable: true
                autoExclusive: true
                checked: root.section === 2
                Accessible.checkable: true
                Accessible.checked: checked
                onClicked: root.section = 2
            }
            AccessibleTransportButton {
                id: downloadsSectionButton

                objectName: "preferences-downloads"
                Layout.fillWidth: true
                contract: root.contract
                text: labels.downloads
                visible: root.downloadFolderAvailable
                checkable: true
                autoExclusive: true
                checked: root.section === 3
                Accessible.checkable: true
                Accessible.checked: checked
                onClicked: root.section = 3
            }
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            currentIndex: root.section
            ColumnLayout {
                spacing: root.contract.spacingUnit
                PreferenceChoice {
                    Layout.fillWidth: true
                    contract: root.contract
                    controlId: "close-behavior"
                    label: labels.closeBehavior
                    options: [labels.minimizeToTray, labels.exitApp]
                    selected: root.presenter.closePreference === PlayerPresenter.Exit ? 1 : 0
                    onValueActivated: index => root.presenter.setClosePreference(index === 1 ? PlayerPresenter.Exit : PlayerPresenter.MinimizeToTray)
                }
                RowLayout {
                    Layout.fillWidth: true
                    PreferenceToggle {
                        Layout.fillWidth: true
                        contract: root.contract
                        controlId: "restore-session"
                        text: labels.restoreSession
                        value: root.presenter.restoreLastSession
                        onValueEdited: value => root.presenter.setRestoreLastSession(value)
                    }
                    PreferenceToggle {
                        Layout.fillWidth: true
                        contract: root.contract
                        controlId: "start-minimized"
                        text: labels.startMinimized
                        value: root.presenter.startMinimized
                        enabled: root.presenter.trayAvailable || value
                        onValueEdited: value => root.presenter.setStartMinimized(value)
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: labels.trayRequired
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    color: root.contract.textSecondaryColor
                    visible: !root.presenter.trayAvailable
                }
                AccessibleTransportButton {
                    id: defaultAppsSettingsButton
                    objectName: "open-default-apps-settings"
                    Layout.fillWidth: true
                    visible: root.presenter.defaultAppsSettingsSupported
                    contract: root.contract
                    text: labels.openDefaultAppsSettings
                    accessibleName: labels.openDefaultAppsSettings
                    shortcutHint: labels.openDefaultAppsSettingsDescription
                    disabledReason: !root.presenter.defaultAppsSettingsAvailable ? labels.defaultAppsSettingsUnavailable : (root.presenter.defaultAppsSettingsState === PlayerPresenter.Opening ? labels.defaultAppsSettingsOpening : "")
                    enabled: root.presenter.defaultAppsSettingsAvailable && root.presenter.defaultAppsSettingsState !== PlayerPresenter.Opening && !root.presenter.exiting
                    onClicked: root.presenter.openDefaultAppsSettings()
                }
                Text {
                    id: defaultAppsSettingsFeedback
                    objectName: "default-apps-settings-feedback"
                    Layout.fillWidth: true
                    text: !root.presenter.defaultAppsSettingsAvailable || root.presenter.defaultAppsSettingsState === PlayerPresenter.Unavailable ? labels.defaultAppsSettingsUnavailable : labels.defaultAppsSettingsFailed
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    color: root.presenter.defaultAppsSettingsState === PlayerPresenter.Failed ? root.contract.errorColor : root.contract.textSecondaryColor
                    visible: root.presenter.defaultAppsSettingsSupported && (!root.presenter.defaultAppsSettingsAvailable || root.presenter.defaultAppsSettingsState === PlayerPresenter.Unavailable || root.presenter.defaultAppsSettingsState === PlayerPresenter.Failed)
                    Accessible.role: Accessible.AlertMessage
                    Accessible.id: objectName
                    Accessible.name: text
                }
                Connections {
                    target: root.presenter
                    function onDefaultAppsSettingsChanged() {
                        Qt.callLater(root.announceDefaultAppsSettingsFeedback);
                    }
                }
                Item {
                    Layout.fillHeight: true
                }
            }
            ColumnLayout {
                spacing: root.contract.spacingUnit
                RowLayout {
                    Layout.fillWidth: true
                    spacing: root.contract.spacingUnit * 3
                    PreferenceSecondsField {
                        id: backwardField
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        contract: root.contract
                        controlId: "skip-backward-seconds"
                        label: labels.backwardSeconds
                        errorText: labels.invalidSeconds
                        value: root.presenter.skipBackwardSeconds
                        onValueEdited: value => root.presenter.setSkipBackwardSeconds(value)
                    }
                    PreferenceSecondsField {
                        id: forwardField
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        contract: root.contract
                        controlId: "skip-forward-seconds"
                        label: labels.forwardSeconds
                        errorText: labels.invalidSeconds
                        value: root.presenter.skipForwardSeconds
                        onValueEdited: value => root.presenter.setSkipForwardSeconds(value)
                    }
                }
                PreferenceChoice {
                    Layout.fillWidth: true
                    contract: root.contract
                    controlId: "queue-end-behavior"
                    label: labels.queueEnd
                    options: [labels.keepSelection, labels.clearSelection]
                    selected: root.presenter.queueEndPreference === PlayerPresenter.StopClearSelection ? 1 : 0
                    onValueActivated: index => root.presenter.setQueueEndPreference(index === 1 ? PlayerPresenter.StopClearSelection : PlayerPresenter.StopKeepSelection)
                }
                PreferenceChoice {
                    Layout.fillWidth: true
                    contract: root.contract
                    controlId: "audio-output-device"
                    label: labels.audioOutput
                    options: [labels.systemDefaultAudioOutput].concat(root.audioOutputDevices.map(device => device.label))
                    selected: root.selectedAudioOutputIndex
                    unselectedText: labels.missingAudioOutput
                    accessibleDescription: audioOutputFeedback.text
                    visible: root.presenter.audioOutputSelectionSupported
                    enabled: !root.presenter.exiting
                    onValueActivated: index => root.selectAudioOutput(index)
                }
                Text {
                    id: audioOutputFeedback
                    objectName: "audio-output-device-feedback"
                    Layout.fillWidth: true
                    text: root.audioOutputSelectionRejected ? labels.audioOutputSelectionFailed : root.selectedAudioOutputIndex < 0 ? labels.missingAudioOutputMessage : root.audioOutputDevices.length === 0 ? labels.noAudioOutputs : ""
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    color: root.audioOutputSelectionRejected ? root.contract.errorColor : root.contract.textSecondaryColor
                    visible: root.presenter.audioOutputSelectionSupported && text.length > 0
                    Accessible.role: Accessible.StaticText
                    Accessible.id: objectName
                    Accessible.name: text
                }
                Item {
                    Layout.fillHeight: true
                }
            }
            ColumnLayout {
                RowLayout {
                    Layout.fillWidth: true
                    spacing: root.contract.spacingUnit * 3
                    PreferenceChoice {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        contract: root.contract
                        controlId: "theme-mode"
                        label: labels.theme
                        options: [root.contract.themeSystem, root.contract.themeLight, root.contract.themeDark]
                        selected: root.presenter.themePreference === PlayerPresenter.DarkTheme ? 2 : (root.presenter.themePreference === PlayerPresenter.LightTheme ? 1 : 0)
                        onValueActivated: index => root.presenter.setThemePreference([PlayerPresenter.SystemTheme, PlayerPresenter.LightTheme, PlayerPresenter.DarkTheme][index])
                    }
                    PreferenceChoice {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        contract: root.contract
                        controlId: "motion-mode"
                        label: labels.motion
                        options: [root.contract.motionSystem, root.contract.motionReduced]
                        selected: root.presenter.motionPreference === PlayerPresenter.ReducedMotion ? 1 : 0
                        onValueActivated: index => root.presenter.setMotionPreference(index === 1 ? PlayerPresenter.ReducedMotion : PlayerPresenter.SystemMotion)
                    }
                }
                Text {
                    objectName: "preferences-high-contrast-note"
                    Layout.fillWidth: true
                    text: labels.highContrastActive
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    color: root.contract.textSecondaryColor
                    visible: root.contract.highContrast
                    Accessible.id: objectName
                    Accessible.name: text
                }
                Item {
                    Layout.fillHeight: true
                }
            }
            ColumnLayout {
                spacing: root.contract.spacingUnit

                Text {
                    Layout.fillWidth: true
                    color: root.contract.textPrimaryColor
                    text: labels.downloadDirectory
                    textFormat: Text.PlainText
                }
                Text {
                    Accessible.name: text
                    Accessible.role: Accessible.StaticText
                    Layout.fillWidth: true
                    color: root.contract.textSecondaryColor
                    maximumLineCount: 3
                    elide: Text.ElideMiddle
                    objectName: "preferences-download-directory"
                    text: root.downloadFolderAvailable ? root.downloads.downloadDirectoryText : ""
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                }
                AccessibleTransportButton {
                    id: changeDownloadDirectoryButton

                    contract: root.contract
                    enabled: root.downloadFolderAvailable && root.downloads.downloadDirectoryPending !== true && !root.presenter.exiting
                    objectName: "preferences-change-download-directory"
                    text: labels.changeDownloadDirectory
                    accessibleName: labels.changeDownloadDirectory

                    onClicked: root.chooseDownloadDirectory()
                }
                Text {
                    Accessible.name: text
                    Accessible.role: Accessible.StaticText
                    Layout.fillWidth: true
                    color: root.contract.textSecondaryColor
                    objectName: "preferences-download-directory-feedback"
                    text: root.downloadFolderAvailable ? root.downloads.downloadDirectoryStatus : ""
                    textFormat: Text.PlainText
                    visible: text.length > 0
                    wrapMode: Text.Wrap
                }
                Item {
                    Layout.fillHeight: true
                }
            }
        }
        Text {
            objectName: "preferences-memory-note"
            Layout.fillWidth: true
            text: labels.memoryNote
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            font.pixelSize: 12
            color: root.contract.textSecondaryColor
        }
    }
}
