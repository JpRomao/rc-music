import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

Popup {
    id: root
    objectName: "window-notice"
    required property ShellUiContract contract
    required property PlayerPresenter presenter
    required property Item fallbackFocusItem
    property Item restoreFocusItem: null
    property int displayedNotice: PlayerPresenter.NoNotice
    property int focusRestoreRevision: 0
    property int restoredFocusRevision: -1
    readonly property bool closeNotice: displayedNotice === PlayerPresenter.TrayCloseNotice
    modal: true
    dim: false
    focus: true
    popupType: Popup.Item
    closePolicy: Popup.CloseOnEscape
    padding: contract.spacingUnit * 4

    LifecycleText {
        id: labels
    }
    function focusFirst() {
        confirmButton.forceActiveFocus(Qt.TabFocusReason);
    }
    function captureFocus() {
        if (!visible && parent && parent.Window.window)
            restoreFocusItem = parent.Window.window.activeFocusItem;
    }
    function restoreFocus() {
        if (presenter.exiting)
            return;
        const windowContentItem = parent && parent.Window.window ? parent.Window.window.contentItem : null;
        const restoreTargetAvailable = restoreFocusItem && restoreFocusItem.parent && restoreFocusItem !== windowContentItem && restoreFocusItem.enabled && restoreFocusItem.visible;
        const target = restoreTargetAvailable ? restoreFocusItem : fallbackFocusItem;
        target.forceActiveFocus(Qt.TabFocusReason);
    }
    function restoreFocusOnce(revision) {
        if (restoredFocusRevision === revision)
            return;
        restoredFocusRevision = revision;
        restoreFocus();
    }
    function synchronizeNotice() {
        if (presenter.windowNotice === PlayerPresenter.NoNotice)
            close();
        else {
            displayedNotice = presenter.windowNotice;
            if (!visible)
                captureFocus();
            open();
        }
    }
    Component.onCompleted: synchronizeNotice()
    Connections {
        target: root.presenter
        function onLifecycleChanged() {
            root.synchronizeNotice();
        }
    }
    onAboutToShow: ++focusRestoreRevision
    onOpened: focusFirst()
    onAboutToHide: {
        const revision = ++focusRestoreRevision;
        Qt.callLater(function () {
            if (revision !== root.focusRestoreRevision || root.opened || root.presenter.windowNotice !== PlayerPresenter.NoNotice)
                return;
            root.restoreFocusOnce(revision);
        });
    }
    onClosed: {
        if (presenter.windowNotice !== PlayerPresenter.NoNotice)
            presenter.cancelWindowNotice();
        if (!visible && !opened)
            restoreFocusOnce(focusRestoreRevision);
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
        spacing: root.contract.spacingUnit * 3
        Accessible.role: Accessible.Dialog
        Accessible.name: root.closeNotice ? labels.trayTitle : labels.unavailableTitle
        Text {
            Layout.fillWidth: true
            text: root.closeNotice ? labels.trayTitle : labels.unavailableTitle
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: root.contract.textPrimaryColor
            font.pixelSize: root.contract.typeTitle
            font.bold: true
        }
        Text {
            objectName: "window-notice-message"
            Layout.fillWidth: true
            text: root.closeNotice ? labels.trayMessage : labels.unavailableMessage
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: root.contract.textPrimaryColor
            Accessible.id: objectName
            Accessible.name: text
        }
        PreferenceToggle {
            Layout.fillWidth: true
            contract: root.contract
            controlId: "window-notice-enabled"
            text: labels.trayNoticeEnabled
            visible: root.closeNotice
            value: root.presenter.trayNoticeEnabled
            onValueEdited: value => root.presenter.setTrayNoticeEnabled(value)
        }
        RowLayout {
            Layout.fillWidth: true
            Item {
                Layout.fillWidth: true
            }
            AccessibleTransportButton {
                objectName: "window-notice-cancel"
                contract: root.contract
                text: labels.cancelAction
                visible: root.closeNotice
                onClicked: root.presenter.cancelWindowNotice()
            }
            AccessibleActionButton {
                id: confirmButton
                contract: root.contract
                controlId: "window-notice-confirm"
                text: root.closeNotice ? labels.continueAction : labels.dismissAction
                onClicked: root.presenter.confirmWindowNotice()
            }
        }
    }
}
