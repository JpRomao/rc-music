import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

ColumnLayout {
    id: root
    objectName: "transport-bar"
    required property ShellUiContract contract
    required property PlayerPresenter presenter
    TransportText {
        id: labels
    }
    enabled: presenter.playbackState === PlayerPresenter.Playing || presenter.playbackState === PlayerPresenter.Paused || presenter.playbackState === PlayerPresenter.Ready
    function formattedTime(milliseconds) {
        const totalSeconds = Math.floor(milliseconds / 1000);
        const minutes = Math.floor(totalSeconds / 60);
        const seconds = totalSeconds % 60;
        return String(minutes).padStart(2, "0") + ":" + String(seconds).padStart(2, "0");
    }
    function commitKeyboardSeek(target) {
        if (presenter.beginSeekPreview(target))
            presenter.commitSeekPreview();
    }
    function focusPrimary() {
        playPause.forceActiveFocus(Qt.TabFocusReason);
    }
    function isSeekControl(item) {
        return item === progress || item === skipBackward || item === skipForward;
    }
    function seekDisabledReason() {
        if (!presenter.hasCurrentTrack)
            return labels.seekRequiresTrack;
        if (presenter.playbackState === PlayerPresenter.Loading)
            return labels.seekLoading;
        if (!root.enabled)
            return labels.seekInactive;
        if (presenter.seekPending)
            return labels.seekPending;
        if (presenter.seekPreviewActive)
            return labels.seekPreviewActive;
        if (!presenter.seekable)
            return labels.seekUnsupported;
        return "";
    }
    Item {
        id: progressArea
        objectName: "progress-tooltip-area"
        Layout.fillWidth: true
        implicitWidth: progress.implicitWidth
        implicitHeight: progress.implicitHeight
        readonly property string toolTipText: progress.enabled ? root.contract.progressName + " (" + labels.seekShortcutHint + ")" : root.contract.progressName + " — " + root.seekDisabledReason()
        readonly property bool toolTipVisible: progressHover.hovered && toolTipText.length > 0
        ToolTip.text: toolTipText
        ToolTip.visible: toolTipVisible
        ToolTip.delay: 0

        HoverHandler {
            id: progressHover
        }

        TransportSlider {
            id: progress
            objectName: "progress"
            anchors.fill: parent
            contract: root.contract
            property bool dragStarted: false
            from: 0
            to: root.presenter.hasDuration ? root.presenter.durationMs : 0
            value: root.presenter.positionMs
            stepSize: 1000
            enabled: root.enabled && root.presenter.seekable && !root.presenter.seekPending
            Accessible.role: Accessible.Slider
            Accessible.id: "progress"
            Accessible.name: root.contract.progressName
            Accessible.description: enabled ? labels.seekShortcutHint : root.seekDisabledReason()
            Accessible.focusable: enabled
            Accessible.focused: activeFocus
            activeFocusOnTab: true
            onMoved: {
                if (!dragStarted) {
                    dragStarted = root.presenter.beginSeekPreview(value);
                } else {
                    root.presenter.updateSeekPreview(value);
                }
            }
            onPressedChanged: {
                if (!pressed && dragStarted) {
                    root.presenter.commitSeekPreview();
                    dragStarted = false;
                }
            }
            Keys.onPressed: event => {
                let target = root.presenter.positionMs;
                if (event.key === Qt.Key_Left)
                    target = Math.max(from, target - 1000);
                else if (event.key === Qt.Key_Right)
                    target = Math.min(to, target + 1000);
                else if (event.key === Qt.Key_Home)
                    target = from;
                else if (event.key === Qt.Key_End)
                    target = to;
                else if (event.key === Qt.Key_PageUp)
                    target = Math.min(to, target + 10000);
                else if (event.key === Qt.Key_PageDown)
                    target = Math.max(from, target - 10000);
                else
                    return;
                root.commitKeyboardSeek(target);
                event.accepted = true;
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Text {
            objectName: "elapsed-time"
            text: root.formattedTime(root.presenter.positionMs)
            color: root.contract.textSecondaryColor
        }
        Item {
            Layout.fillWidth: true
        }
        Text {
            objectName: "duration-time"
            text: root.formattedTime(root.presenter.durationMs)
            color: root.contract.textSecondaryColor
        }
    }
    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        AccessibleTransportButton {
            objectName: "previous"
            enabled: root.presenter.canGoPrevious
            contract: root.contract
            text: "⏮"
            accessibleName: root.contract.actionPrevious
            shortcutHint: labels.previousShortcut
            onClicked: root.presenter.previousTrack()
        }
        AccessibleTransportButton {
            id: skipBackward
            objectName: "skip-backward"
            enabled: root.presenter.seekable && !root.presenter.seekPreviewActive && !root.presenter.seekPending
            contract: root.contract
            text: root.contract.skipBackwardShort.replace("{seconds}", root.presenter.skipBackwardSeconds)
            accessibleName: labels.backwardName(root.presenter.skipBackwardSeconds)
            shortcutHint: labels.backwardShortcut
            disabledReason: root.seekDisabledReason()
            onClicked: root.presenter.skipBackward()
        }
        AccessibleTransportButton {
            id: playPause
            objectName: "play-pause"
            contract: root.contract
            text: root.presenter.playbackState === PlayerPresenter.Playing ? "⏸" : "▶"
            accessibleName: root.presenter.playbackState === PlayerPresenter.Playing ? root.contract.actionPause : root.contract.actionPlay
            shortcutHint: labels.playPauseShortcut
            onClicked: root.presenter.togglePlayback()
        }
        AccessibleTransportButton {
            id: skipForward
            objectName: "skip-forward"
            enabled: root.presenter.seekable && !root.presenter.seekPreviewActive && !root.presenter.seekPending
            contract: root.contract
            text: root.contract.skipForwardShort.replace("{seconds}", root.presenter.skipForwardSeconds)
            accessibleName: labels.forwardName(root.presenter.skipForwardSeconds)
            shortcutHint: labels.forwardShortcut
            disabledReason: root.seekDisabledReason()
            onClicked: root.presenter.skipForward()
        }
        AccessibleTransportButton {
            objectName: "next"
            enabled: root.presenter.canGoNext
            contract: root.contract
            text: "⏭"
            accessibleName: root.contract.actionNext
            shortcutHint: labels.nextShortcut
            onClicked: root.presenter.nextTrack()
        }
        AccessibleTransportButton {
            objectName: "mute"
            contract: root.contract
            text: root.presenter.muted ? "🔊" : "🔇"
            accessibleName: root.presenter.muted ? root.contract.actionUnmute : root.contract.actionMute
            shortcutHint: labels.muteShortcut
            onClicked: root.presenter.toggleMute()
        }
        TransportSlider {
            objectName: "volume"
            contract: root.contract
            from: 0
            to: 100
            value: root.presenter.volume * 100
            stepSize: 1
            implicitWidth: 120
            activeFocusOnTab: true
            Accessible.role: Accessible.Slider
            Accessible.id: "volume"
            Accessible.name: root.contract.volumeName
            Accessible.focusable: enabled
            Accessible.focused: activeFocus
            onMoved: root.presenter.setVolume(value / 100)
        }
    }
}
