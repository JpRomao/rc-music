import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

GridLayout {
    id: root
    objectName: "transport-bar"
    required property ShellUiContract contract
    required property PlayerPresenter presenter
    property alias primaryControl: playPause
    property Item pageReturnFocusItem: null
    readonly property bool editorial: !presenter.usesFrozenFixture
    readonly property bool spread: editorial && width >= 680
    readonly property bool queueModesAvailable: editorial && !roomGuest && !presenter.exiting && !presenter.selectingAudioFiles && presenter.locatingOccurrenceId.length === 0
    // Narrow rails drop the ±5 s buttons; ←/→ and the progress bar keep that function.
    readonly property bool roomy: !editorial || width >= 580
    property int queueCount: 0
    property bool queueAvailable: false
    property bool lyricsAvailable: false
    // ADR-0022: the listening room binding; the badge shows only while in a room.
    property var room: null
    readonly property bool inRoom: room !== null && room.available && room.role !== 0
    // S8.2: shuffle and repeat belong to the host while this app is a guest.
    readonly property bool roomGuest: inRoom && room.role === 2
    signal queueRequested
    signal lyricsRequested
    columns: editorial ? 2 : 1
    columnSpacing: editorial ? 24 : 0
    rowSpacing: editorial ? 4 : 5
    QueueText {
        id: queueLabels
    }
    TransportText {
        id: labels
    }
    RoomText {
        id: roomLabels
    }
    readonly property bool playbackEnabled: presenter.playbackState === PlayerPresenter.Playing || presenter.playbackState === PlayerPresenter.Paused || presenter.playbackState === PlayerPresenter.Ready
    enabled: playbackEnabled || presenter.canGoPrevious || presenter.canGoNext
    function formattedTime(milliseconds) {
        const totalSeconds = Math.floor(milliseconds / 1000);
        const minutes = Math.floor(totalSeconds / 60);
        const seconds = totalSeconds % 60;
        return String(minutes).padStart(2, "0") + ":" + String(seconds).padStart(2, "0");
    }
    function commitImmediateSeek(target) {
        if (presenter.beginSeekPreview(target))
            presenter.commitSeekPreview();
    }
    function focusPrimary() {
        playPause.forceActiveFocus(Qt.TabFocusReason);
    }
    function isSeekControl(item) {
        return item === progress || item === skipBackward || item === skipForward;
    }
    function isAvailableNavigationControl(item) {
        return (item === previousTrack && presenter.canGoPrevious) || (item === nextTrack && presenter.canGoNext);
    }
    function seekDisabledReason() {
        if (!presenter.hasCurrentTrack)
            return labels.seekRequiresTrack;
        if (presenter.playbackState === PlayerPresenter.Loading)
            return labels.seekLoading;
        if (!root.playbackEnabled)
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
        Layout.row: root.editorial ? 1 : 0
        Layout.column: 0
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
            anchors.leftMargin: root.editorial ? 52 : 0
            anchors.rightMargin: root.editorial ? 52 : 0
            contract: root.contract
            property bool dragStarted: false
            property bool synchronizingValue: false
            property bool initialized: false
            from: 0
            function synchronizeValue() {
                synchronizingValue = true;
                to = root.presenter.hasDuration ? root.presenter.durationMs : 0;
                value = root.presenter.positionMs;
                synchronizingValue = false;
            }
            Component.onCompleted: {
                synchronizeValue();
                initialized = true;
            }
            Connections {
                target: root.presenter
                function onPlaybackChanged() {
                    progress.synchronizeValue();
                }
            }
            stepSize: 1000
            enabled: root.playbackEnabled && root.presenter.seekable && !root.presenter.seekPending
            Accessible.role: Accessible.Slider
            Accessible.id: "progress"
            Accessible.name: root.contract.progressName
            Accessible.description: enabled ? labels.seekShortcutHint : root.seekDisabledReason()
            Accessible.focusable: enabled
            Accessible.focused: activeFocus
            activeFocusOnTab: true
            // Qt accessibility writes value directly, without moved or pressed.
            // Presenter updates (including duration clamping) are not commands.
            onValueChanged: {
                if (!initialized || synchronizingValue || pressed)
                    return;
                if (enabled && !dragStarted && !root.presenter.seekPreviewActive && Number.isFinite(value) && value !== root.presenter.positionMs)
                    root.commitImmediateSeek(value);
                synchronizeValue();
            }
            onMoved: {
                if (!pressed)
                    return;
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
                if (!pressed)
                    synchronizeValue();
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
                root.commitImmediateSeek(target);
                event.accepted = true;
            }
        }
    }
    Item {
        parent: root.editorial ? progressArea : root
        implicitWidth: root.editorial ? progressArea.width : timeLabels.implicitWidth
        implicitHeight: root.editorial ? progressArea.height : timeLabels.implicitHeight
        Layout.row: 1
        Layout.column: 0
        Layout.fillWidth: true
        RowLayout {
            id: timeLabels
            anchors.fill: parent
            Text {
                objectName: "elapsed-time"
                font.family: root.presenter.usesFrozenFixture ? root.contract.applicationFont.family : root.contract.bodyFontFamily
                text: root.formattedTime(root.presenter.positionMs)
                color: root.contract.textSecondaryColor
            }
            Item {
                Layout.fillWidth: true
            }
            Text {
                objectName: "duration-time"
                font.family: root.presenter.usesFrozenFixture ? root.contract.applicationFont.family : root.contract.bodyFontFamily
                text: root.formattedTime(root.presenter.durationMs)
                color: root.contract.textSecondaryColor
            }
        }
    }
    RowLayout {
        id: controlsRow
        Layout.row: root.editorial ? 0 : 2
        Layout.column: 0
        Layout.alignment: Qt.AlignHCenter
        AccessibleTransportButton {
            objectName: "player-shuffle"
            flat: root.editorial
            visible: root.editorial
            enabled: root.queueModesAvailable
            checkable: true
            checked: root.presenter.shuffleQueueEnabled
            contract: root.contract
            text: "⇄"
            iconName: root.editorial ? "shuffle" : ""
            font.family: root.contract.symbolFontFamily
            accessibleName: queueLabels.shuffle
            shortcutHint: queueLabels.shortcutShuffle
            disabledReason: root.roomGuest ? queueLabels.hostControls : ""
            onClicked: {
                if (enabled)
                    root.presenter.toggleShuffleQueue();
            }
        }
        AccessibleTransportButton {
            id: previousTrack
            flat: root.editorial
            objectName: "previous"
            font.family: root.presenter.usesFrozenFixture ? root.contract.applicationFont.family : root.contract.symbolFontFamily
            enabled: root.presenter.canGoPrevious
            contract: root.contract
            text: "⏮"
            iconName: root.editorial ? "previous" : ""
            accessibleName: root.contract.actionPrevious
            shortcutHint: labels.previousShortcut
            onClicked: root.presenter.previousTrack()
        }
        AccessibleTransportButton {
            id: skipBackward
            flat: root.editorial
            objectName: "skip-backward"
            visible: root.roomy
            enabled: root.playbackEnabled && root.presenter.seekable && !root.presenter.seekPreviewActive && !root.presenter.seekPending
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
            primary: root.editorial
            KeyNavigation.backtab: null
            Keys.onBacktabPressed: event => {
                const target = root.pageReturnFocusItem;
                if (target && target.visible && target.enabled) {
                    target.forceActiveFocus(Qt.BacktabFocusReason);
                    event.accepted = true;
                } else {
                    event.accepted = false;
                }
            }
            font.family: root.presenter.usesFrozenFixture ? root.contract.applicationFont.family : root.contract.symbolFontFamily
            enabled: root.playbackEnabled
            contract: root.contract
            text: root.presenter.playbackState === PlayerPresenter.Playing ? "⏸" : "▶"
            iconName: root.editorial ? (root.presenter.playbackState === PlayerPresenter.Playing ? "pause" : "play") : ""
            accessibleName: root.presenter.playbackState === PlayerPresenter.Playing ? root.contract.actionPause : root.contract.actionPlay
            shortcutHint: labels.playPauseShortcut
            onClicked: root.presenter.togglePlayback()
        }
        AccessibleTransportButton {
            id: skipForward
            flat: root.editorial
            objectName: "skip-forward"
            visible: root.roomy
            enabled: root.playbackEnabled && root.presenter.seekable && !root.presenter.seekPreviewActive && !root.presenter.seekPending
            contract: root.contract
            text: root.contract.skipForwardShort.replace("{seconds}", root.presenter.skipForwardSeconds)
            accessibleName: labels.forwardName(root.presenter.skipForwardSeconds)
            shortcutHint: labels.forwardShortcut
            disabledReason: root.seekDisabledReason()
            onClicked: root.presenter.skipForward()
        }
        AccessibleTransportButton {
            id: nextTrack
            flat: root.editorial
            objectName: "next"
            font.family: root.presenter.usesFrozenFixture ? root.contract.applicationFont.family : root.contract.symbolFontFamily
            enabled: root.presenter.canGoNext
            contract: root.contract
            text: "⏭"
            iconName: root.editorial ? "next" : ""
            accessibleName: root.contract.actionNext
            shortcutHint: labels.nextShortcut
            onClicked: root.presenter.nextTrack()
        }
        AccessibleTransportButton {
            objectName: "player-repeat"
            flat: root.editorial
            visible: root.editorial
            enabled: root.queueModesAvailable
            checkable: true
            checked: root.presenter.repeatQueueEnabled
            contract: root.contract
            text: "↻"
            iconName: root.editorial ? "repeat" : ""
            font.family: root.contract.symbolFontFamily
            accessibleName: queueLabels.repeat
            shortcutHint: queueLabels.shortcutRepeat
            disabledReason: root.roomGuest ? queueLabels.hostControls : ""
            onClicked: {
                if (enabled)
                    root.presenter.toggleRepeatQueue();
            }
        }
    }
    GridLayout {
        id: rightCluster
        objectName: "player-side-actions"
        visible: root.editorial
        Layout.row: 0
        Layout.column: 1
        Layout.rowSpan: 2
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
        columns: root.spread ? 1 : 2
        rowSpacing: 4
        columnSpacing: 4
        RowLayout {
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
            spacing: 4
            // A status chip, not a control: the side navigation opens the room page.
            Rectangle {
                id: roomBadge
                objectName: "player-room-badge"
                readonly property string text: !root.inRoom ? "" : (root.room.role === 1 ? roomLabels.badgeSelf.arg(root.room.memberCount) : roomLabels.badge.arg(root.room.hostNickname).arg(root.room.memberCount))
                visible: root.inRoom
                Layout.alignment: Qt.AlignVCenter
                Layout.maximumWidth: 260
                implicitWidth: badgeRow.implicitWidth + 20
                implicitHeight: 28
                radius: height / 2
                color: root.contract.selectionColor
                border.width: root.contract.highContrast ? 1 : 0
                border.color: root.contract.borderColor
                Accessible.role: Accessible.StaticText
                Accessible.name: text
                RowLayout {
                    id: badgeRow
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 6
                    ShellIcon {
                        iconName: "people"
                        size: 16
                        color: root.contract.textPrimaryColor
                    }
                    Text {
                        Layout.fillWidth: true
                        text: roomBadge.text
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        font.family: root.contract.bodyFontFamily
                        font.pixelSize: root.contract.typeCaption
                        color: root.contract.textPrimaryColor
                        Accessible.ignored: true
                    }
                }
            }
            AccessibleTransportButton {
                objectName: "player-lyrics"
                flat: true
                contract: root.contract
                iconName: "lyrics"
                iconWithText: root.spread
                text: labels.lyricsAction
                enabled: root.lyricsAvailable
                disabledReason: labels.lyricsUnavailable
                onClicked: {
                    if (enabled)
                        root.lyricsRequested();
                }
            }
            AccessibleTransportButton {
                id: queueAction
                objectName: "player-queue"
                flat: true
                contract: root.contract
                iconName: "queue"
                iconWithText: root.spread
                text: root.contract.queueTitle
                accessibleName: labels.queueWithCount.arg(root.queueCount)
                enabled: root.queueAvailable
                rightPadding: root.spread ? queueCountLabel.implicitWidth + 18 : 6
                implicitWidth: Math.max(root.contract.controlTargetPreferred, contentItem.implicitWidth + 16 + (root.spread ? queueCountLabel.implicitWidth + 6 : 0))
                onClicked: {
                    if (enabled)
                        root.queueRequested();
                }
                Text {
                    id: queueCountLabel
                    objectName: "player-queue-count"
                    // Beside the label when wide; a small badge on the icon when narrow.
                    anchors.right: parent.right
                    anchors.rightMargin: root.spread ? 12 : 4
                    anchors.top: root.spread ? undefined : parent.top
                    anchors.topMargin: 4
                    anchors.verticalCenter: root.spread ? parent.verticalCenter : undefined
                    text: String(root.queueCount)
                    textFormat: Text.PlainText
                    font.family: root.contract.bodyFontFamily
                    font.pixelSize: root.spread ? root.contract.typeCaption : root.contract.typeEyebrow
                    color: root.contract.textSecondaryColor
                    Accessible.ignored: true
                }
            }
        }
    }
    RowLayout {
        id: volumeControls
        parent: root.editorial ? rightCluster : controlsRow
        Layout.alignment: root.editorial ? Qt.AlignRight | Qt.AlignVCenter : Qt.AlignHCenter | Qt.AlignVCenter
        AccessibleTransportButton {
            objectName: "mute"
            flat: root.editorial
            enabled: root.playbackEnabled
            contract: root.contract
            // The icon shows the sound state (muted or at 0% → crossed speaker);
            // the accessible name keeps the action.
            readonly property bool silent: root.presenter.muted || root.presenter.volume <= 0
            text: silent ? "🔇" : "🔊"
            iconName: root.editorial ? (silent ? "mute" : "volume") : ""
            accessibleName: root.presenter.muted ? root.contract.actionUnmute : root.contract.actionMute
            shortcutHint: labels.muteShortcut
            onClicked: root.presenter.toggleMute()
        }
        TransportSlider {
            id: volume
            objectName: "volume"
            enabled: root.playbackEnabled
            contract: root.contract
            hoverEnabled: true
            ToolTip.text: Math.round(root.presenter.volume * 100) + "%"
            ToolTip.visible: enabled && (hovered || pressed)
            ToolTip.delay: 0
            ToolTip.timeout: -1
            from: 0
            to: 100
            property bool synchronizingValue: false
            property bool initialized: false
            function synchronizeValue() {
                synchronizingValue = true;
                value = root.presenter.volume * 100;
                synchronizingValue = false;
            }
            Component.onCompleted: {
                synchronizeValue();
                initialized = true;
            }
            Connections {
                target: root.presenter
                function onPreferencesChanged() {
                    volume.synchronizeValue();
                }
            }
            stepSize: 1
            implicitWidth: root.editorial && !root.spread ? 64 : 120
            activeFocusOnTab: true
            Accessible.role: Accessible.Slider
            Accessible.id: "volume"
            Accessible.name: root.contract.volumeName
            Accessible.focusable: enabled
            Accessible.focused: activeFocus
            onValueChanged: {
                if (!initialized || synchronizingValue || pressed)
                    return;
                if (enabled && Number.isFinite(value))
                    root.presenter.setVolume(value / 100);
                synchronizeValue();
            }
            onMoved: {
                if (pressed)
                    root.presenter.setVolume(value / 100);
            }
            onPressedChanged: {
                if (!pressed)
                    synchronizeValue();
            }
        }
        Text {
            objectName: "player-volume-percent"
            visible: root.editorial && root.spread
            Layout.preferredWidth: 36
            horizontalAlignment: Text.AlignRight
            text: labels.volumePercent.arg(Math.round(root.presenter.volume * 100))
            textFormat: Text.PlainText
            font.family: root.contract.bodyFontFamily
            font.pixelSize: root.contract.typeCaption
            color: root.contract.textSecondaryColor
            Accessible.ignored: true
        }
    }
}
