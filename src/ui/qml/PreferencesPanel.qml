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
    property var profile: null
    property int section: 0
    readonly property int profileSection: 4
    // Section for the next opening only; read and cleared in onAboutToShow.
    property int requestedSection: 0
    property bool profileNicknameRejected: false
    property bool profileNicknameSaved: false
    property bool customAvatarRejected: false
    property var directoryChoiceOwner: null
    property string announcedDefaultAppsSettingsFeedback: ""
    property bool audioOutputSelectionRejected: false
    readonly property bool downloadFolderAvailable: downloads !== null && typeof downloads.setDownloadDirectory === "function" && typeof downloads.downloadDirectoryText === "string" && downloads.downloadDirectoryText.length > 0
    readonly property bool profileSectionAvailable: profile !== null
    readonly property bool profileEditable: profileSectionAvailable && profile.available === true
    readonly property bool profileDraftValid: profileEditable && profile.acceptsNickname(profileNicknameField.text)
    readonly property bool profileDraftInvalid: profileEditable && profileNicknameField.text.length > 0 && !profileDraftValid
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
    padding: embedded ? 0 : contract.spacingUnit * 3

    NavigationText {
        id: navigationLabels
    }
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
    function resetProfileDraft() {
        profileNicknameField.text = profileEditable ? profile.nickname : "";
        profileNicknameRejected = false;
        profileNicknameSaved = false;
        customAvatarRejected = false;
    }
    function saveProfileNickname() {
        if (!opened || section !== 4 || !profileDraftValid || presenter.exiting)
            return;
        profileNicknameRejected = !profile.setNickname(profileNicknameField.text);
        profileNicknameSaved = !profileNicknameRejected;
        if (profileNicknameSaved)
            profileNicknameField.text = profile.nickname;
    }
    function chooseCustomAvatar() {
        if (!opened || section !== 4 || !profileEditable || avatarImagePicker.visible || presenter.exiting)
            return;
        customAvatarRejected = false;
        avatarImagePicker.open();
    }
    function finishCustomAvatarChoice(file) {
        if (file !== null && opened && section === 4 && profileEditable && !presenter.exiting)
            customAvatarRejected = !profile.chooseCustomAvatar(file);
        Qt.callLater(function () {
            if (root.opened && root.section === 4 && !avatarImagePicker.visible)
                customAvatarButton.forceActiveFocus(Qt.TabFocusReason);
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
        section = requestedSection;
        requestedSection = 0;
        audioOutputSelectionRejected = false;
        backwardField.resetDraft();
        forwardField.resetDraft();
        resetProfileDraft();
    }
    blockingUiOpen: downloadDirectoryPicker.visible || avatarImagePicker.visible
    onSurfaceOpened: focusFirst()
    onAboutToHide: {
        cancelDownloadDirectoryChoice();
        if (avatarImagePicker.visible)
            avatarImagePicker.close();
    }
    onProfileEditableChanged: {
        if (!profileSectionAvailable && section === 4) {
            section = 0;
            if (opened)
                focusFirst();
        }
        resetProfileDraft();
    }
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
    NativeDialogs.FileDialog {
        id: avatarImagePicker

        objectName: "preferences-avatar-image-picker"
        modality: Qt.WindowModal
        parentWindow: root.parent ? root.parent.Window.window : null
        popupType: Popup.Item
        title: profileLabels.chooseCustomAvatar
        nameFilters: [profileLabels.customAvatarFilter]
        fileMode: NativeDialogs.FileDialog.OpenFile

        onAccepted: root.finishCustomAvatarChoice(selectedFile)
        onRejected: root.finishCustomAvatarChoice(null)
    }
    ProfileText {
        id: profileLabels
    }
    background: Rectangle {
        // Inline Aurora pages sit on the canvas; the legacy dialog keeps its card.
        color: root.embedded ? root.contract.canvasColor : root.contract.surfaceRaisedColor
        radius: root.embedded ? 0 : root.contract.radiusMedium
        border.width: root.embedded ? 0 : 1
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
        Accessible.name: root.embedded ? navigationLabels.settings : root.contract.preferencesTitle
        PageHeader {
            Layout.fillWidth: true
            contract: root.contract
            editorial: root.embedded
            eyebrow: root.embedded ? navigationLabels.settingsEyebrow : ""
            title: root.embedded ? navigationLabels.settings : root.contract.preferencesTitle
            description: root.embedded ? navigationLabels.settingsDescription : ""
            AccessibleTransportButton {
                objectName: "preferences-close"
                contract: root.contract
                flat: root.embedded
                iconName: root.embedded ? "close" : ""
                text: "✕"
                accessibleName: root.embedded ? navigationLabels.closeSettings : labels.close
                onClicked: root.close()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            AccessibleTransportButton {
                id: generalButton
                objectName: "preferences-general"
                flat: root.embedded
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
                flat: root.embedded
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
                flat: root.embedded
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
                flat: root.embedded
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
            AccessibleTransportButton {
                objectName: "preferences-profile"
                flat: root.embedded
                Layout.fillWidth: true
                contract: root.contract
                text: profileLabels.section
                visible: root.profileSectionAvailable
                checkable: true
                autoExclusive: true
                checked: root.section === 4
                Accessible.checkable: true
                Accessible.checked: checked
                onClicked: root.section = 4
            }
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            currentIndex: root.section
            ColumnLayout {
                Text {
                    objectName: "preferences-section-title-general"
                    visible: root.embedded
                    Layout.fillWidth: true
                    Layout.topMargin: root.contract.spacingUnit * 2
                    text: root.contract.sectionGeneral
                    textFormat: Text.PlainText
                    font.family: root.contract.headingFontFamily
                    font.pixelSize: root.contract.typeSection
                    color: root.contract.textPrimaryColor
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                Rectangle {
                    visible: root.embedded
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    Layout.bottomMargin: root.contract.spacingUnit * 2
                    color: root.contract.highContrast ? root.contract.borderColor : root.contract.dividerColor
                    Accessible.ignored: true
                }
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
                    font.pixelSize: root.contract.typeCaption
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
                Text {
                    objectName: "preferences-section-title-playback"
                    visible: root.embedded
                    Layout.fillWidth: true
                    Layout.topMargin: root.contract.spacingUnit * 2
                    text: root.contract.sectionPlayback
                    textFormat: Text.PlainText
                    font.family: root.contract.headingFontFamily
                    font.pixelSize: root.contract.typeSection
                    color: root.contract.textPrimaryColor
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                Rectangle {
                    visible: root.embedded
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    Layout.bottomMargin: root.contract.spacingUnit * 2
                    color: root.contract.highContrast ? root.contract.borderColor : root.contract.dividerColor
                    Accessible.ignored: true
                }
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
                RoomOutputOffsetField {
                    Layout.fillWidth: true
                    contract: root.contract
                    controlId: "room-output-offset"
                    label: labels.roomOutputOffset
                    hint: labels.roomOutputOffsetHint
                    valueFormat: labels.roomOutputOffsetValue
                    value: root.presenter.roomOutputOffsetMs
                    minimum: root.presenter.roomOutputOffsetMinimumMs
                    maximum: root.presenter.roomOutputOffsetMaximumMs
                    enabled: !root.presenter.exiting
                    onValueEdited: value => root.presenter.setRoomOutputOffsetMs(value)
                }
                Text {
                    id: audioOutputFeedback
                    objectName: "audio-output-device-feedback"
                    Layout.fillWidth: true
                    text: root.audioOutputSelectionRejected ? labels.audioOutputSelectionFailed : root.selectedAudioOutputIndex < 0 ? labels.missingAudioOutputMessage : root.audioOutputDevices.length === 0 ? labels.noAudioOutputs : ""
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    font.pixelSize: root.contract.typeCaption
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
                Text {
                    objectName: "preferences-section-title-appearance"
                    visible: root.embedded
                    Layout.fillWidth: true
                    Layout.topMargin: root.contract.spacingUnit * 2
                    text: root.contract.sectionAppearance
                    textFormat: Text.PlainText
                    font.family: root.contract.headingFontFamily
                    font.pixelSize: root.contract.typeSection
                    color: root.contract.textPrimaryColor
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                Rectangle {
                    visible: root.embedded
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    Layout.bottomMargin: root.contract.spacingUnit * 2
                    color: root.contract.highContrast ? root.contract.borderColor : root.contract.dividerColor
                    Accessible.ignored: true
                }
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
                Text {
                    objectName: "preferences-section-title-downloads"
                    visible: root.embedded
                    Layout.fillWidth: true
                    Layout.topMargin: root.contract.spacingUnit * 2
                    text: labels.downloads
                    textFormat: Text.PlainText
                    font.family: root.contract.headingFontFamily
                    font.pixelSize: root.contract.typeSection
                    color: root.contract.textPrimaryColor
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                Rectangle {
                    visible: root.embedded
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    Layout.bottomMargin: root.contract.spacingUnit * 2
                    color: root.contract.highContrast ? root.contract.borderColor : root.contract.dividerColor
                    Accessible.ignored: true
                }
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
            ColumnLayout {
                spacing: root.contract.spacingUnit
                Text {
                    objectName: "preferences-section-title-profile"
                    visible: root.embedded
                    Layout.fillWidth: true
                    Layout.topMargin: root.contract.spacingUnit * 2
                    text: profileLabels.section
                    textFormat: Text.PlainText
                    font.family: root.contract.headingFontFamily
                    font.pixelSize: root.contract.typeSection
                    color: root.contract.textPrimaryColor
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                Rectangle {
                    visible: root.embedded
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    Layout.bottomMargin: root.contract.spacingUnit * 2
                    color: root.contract.highContrast ? root.contract.borderColor : root.contract.dividerColor
                    Accessible.ignored: true
                }
                Text {
                    objectName: "preferences-profile-unavailable"
                    Layout.fillWidth: true
                    visible: root.profileSectionAvailable && !root.profileEditable
                    text: profileLabels.unavailable
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    color: root.contract.textSecondaryColor
                    Accessible.role: Accessible.StaticText
                    Accessible.id: objectName
                    Accessible.name: text
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.profileEditable
                    spacing: root.contract.spacingUnit * 2
                    ListenerAvatar {
                        objectName: "preferences-profile-avatar"
                        size: 56
                        source: root.profileEditable ? root.profile.avatarSource : ""
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: root.contract.spacingUnit
                        Text {
                            Layout.fillWidth: true
                            text: profileLabels.nickname
                            textFormat: Text.PlainText
                            color: root.contract.textPrimaryColor
                            Accessible.ignored: true
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField {
                                id: profileNicknameField
                                objectName: "preferences-profile-nickname"
                                Layout.fillWidth: true
                                implicitHeight: root.contract.controlTargetPreferred
                                maximumLength: 128
                                selectByMouse: true
                                activeFocusOnTab: true
                                color: root.contract.textPrimaryColor
                                selectionColor: root.contract.accentColor
                                selectedTextColor: root.contract.accentTextColor
                                Accessible.role: Accessible.EditableText
                                Accessible.id: objectName
                                Accessible.name: profileLabels.nickname
                                Accessible.description: profileFeedback.visible ? profileFeedback.text : ""
                                Accessible.focusable: enabled
                                Accessible.focused: activeFocus
                                onTextEdited: {
                                    root.profileNicknameRejected = false;
                                    root.profileNicknameSaved = false;
                                }
                                onAccepted: root.saveProfileNickname()
                                background: Rectangle {
                                    color: root.contract.surfaceColor
                                    radius: root.contract.radiusSmall
                                    border.width: profileNicknameField.activeFocus ? 3 : 1
                                    border.color: profileNicknameField.activeFocus ? root.contract.focusColor : (root.profileDraftInvalid ? root.contract.errorColor : root.contract.borderColor)
                                }
                            }
                            AccessibleTransportButton {
                                objectName: "preferences-profile-save"
                                contract: root.contract
                                text: profileLabels.saveNickname
                                accessibleName: profileLabels.saveNickname
                                enabled: root.profileDraftValid && profileNicknameField.text !== root.profile.nickname && !root.presenter.exiting
                                onClicked: root.saveProfileNickname()
                            }
                        }
                    }
                }
                Text {
                    id: profileFeedback
                    objectName: "preferences-profile-feedback"
                    Layout.fillWidth: true
                    visible: root.profileEditable && text.length > 0
                    text: root.profileNicknameRejected ? profileLabels.saveFailed : (root.profileDraftInvalid ? profileLabels.nicknameInvalid : (root.profileNicknameSaved ? profileLabels.nicknameSaved : ""))
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    font.pixelSize: root.contract.typeCaption
                    color: root.profileNicknameRejected || root.profileDraftInvalid ? root.contract.errorColor : root.contract.textSecondaryColor
                    Accessible.role: Accessible.AlertMessage
                    Accessible.id: objectName
                    Accessible.name: text
                }
                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: root.contract.spacingUnit
                    visible: root.profileEditable
                    text: profileLabels.avatar
                    textFormat: Text.PlainText
                    color: root.contract.textPrimaryColor
                    Accessible.ignored: true
                }
                Text {
                    objectName: "preferences-profile-custom-avatar-note"
                    Layout.fillWidth: true
                    visible: root.profileEditable && root.profile.avatarIndex < 0
                    text: profileLabels.customAvatarActive
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    font.pixelSize: root.contract.typeCaption
                    color: root.contract.textSecondaryColor
                    Accessible.role: Accessible.StaticText
                    Accessible.id: objectName
                    Accessible.name: text
                }
                AvatarPicker {
                    objectName: "preferences-profile-avatar-picker"
                    visible: root.profileEditable
                    Layout.preferredWidth: implicitWidth
                    Layout.preferredHeight: implicitHeight
                    contract: root.contract
                    optionObjectName: "preferences-"
                    avatarCount: root.profileEditable ? root.profile.avatarCount : 0
                    selectedIndex: root.profileEditable ? root.profile.avatarIndex : -1
                    onAvatarChosen: index => {
                        if (root.opened && root.section === 4 && root.profileEditable && !root.presenter.exiting)
                            root.profile.chooseBundledAvatar(index);
                    }
                }
                AccessibleTransportButton {
                    id: customAvatarButton
                    objectName: "preferences-profile-custom-avatar"
                    visible: root.profileEditable
                    contract: root.contract
                    text: profileLabels.customAvatar
                    accessibleName: profileLabels.customAvatar
                    enabled: root.profileEditable && !avatarImagePicker.visible && !root.presenter.exiting
                    onClicked: root.chooseCustomAvatar()
                }
                Text {
                    objectName: "preferences-profile-custom-avatar-feedback"
                    Layout.fillWidth: true
                    visible: root.profileEditable && root.customAvatarRejected
                    text: profileLabels.customAvatarRejected
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    font.pixelSize: root.contract.typeCaption
                    color: root.contract.errorColor
                    Accessible.role: Accessible.AlertMessage
                    Accessible.id: objectName
                    Accessible.name: text
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
            font.pixelSize: root.contract.typeCaption
            color: root.contract.textSecondaryColor
        }
    }
}
