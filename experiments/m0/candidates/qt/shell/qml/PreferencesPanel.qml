import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

Popup {
    id: root
    objectName: "preferences-panel"
    required property ShellUiContract contract
    required property PlayerPresenter presenter
    property int section: 0
    modal: true
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
    onAboutToShow: {
        section = 0;
        backwardField.resetDraft();
        forwardField.resetDraft();
    }
    onOpened: focusFirst()
    onClosed: {
        backwardField.resetDraft();
        forwardField.resetDraft();
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
        Accessible.role: Accessible.Dialog
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
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            currentIndex: root.section
            ColumnLayout {
                spacing: root.contract.spacingUnit * 2
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
