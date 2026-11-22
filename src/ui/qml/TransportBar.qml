pragma ComponentBehavior: Bound

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
    property alias queueControl: queueAction
    // The player's first control in the Tab order, where a page hands the focus on; null when none is.
    readonly property Item firstControl: [progress, shuffleAction, previousTrack, skipBackward, playPause, skipForward, nextTrack, repeatAction, lyricsAction, queueAction, muteAction, volume].find(item => item.visible && item.enabled) ?? null
    readonly property bool editorial: !presenter.usesFrozenFixture
    // Set by the window: the same compact rule as the side navigation.
    property bool compact: false
    readonly property bool queueModesAvailable: editorial && !roomGuest && !presenter.exiting && !presenter.selectingAudioFiles && presenter.locatingOccurrenceId.length === 0
    property int queueCount: 0
    property bool queueAvailable: false
    property bool lyricsAvailable: false
    // ADR-0022: the listening room binding; the badge shows only while in a room.
    property var room: null
    readonly property bool inRoom: room !== null && room.available && room.role !== 0
    // S8.2: shuffle and repeat belong to the host while this app is a guest.
    readonly property bool roomGuest: inRoom && room.role === 2
    // S10.1: a guest's transport sends room intents, each one only with the permission the host gave.
    readonly property bool roomSkip: roomGuest && room.canSkip
    readonly property bool roomPause: roomGuest && room.canPause && room.roomHasCurrent
    readonly property bool showsPlaying: roomGuest ? room.roomPlaying : presenter.playbackState === PlayerPresenter.Playing
    signal queueRequested
    signal lyricsRequested
    // Player C: the progress spans the top edge (row 0); identity, controls and side actions share row 1.
    columns: editorial ? 3 : 1
    columnSpacing: editorial ? 10 : 0
    rowSpacing: editorial ? 0 : 5
    QueueText {
        id: queueLabels
    }
    TransportText {
        id: labels
    }
    NavigationText {
        id: navigationLabels
    }
    RoomText {
        id: roomLabels
    }
    readonly property bool playbackEnabled: presenter.playbackState === PlayerPresenter.Playing || presenter.playbackState === PlayerPresenter.Paused || presenter.playbackState === PlayerPresenter.Ready
    enabled: roomGuest || playbackEnabled || presenter.canGoPrevious || presenter.canGoNext
    // A guest still entering has no host or count yet. The host nickname is filled last: arg() never reads
    // placeholders inside text it inserted, so a nickname cannot change the badge.
    function roomBadgeText() {
        if (!inRoom)
            return "";
        const count = room.memberCount;
        if (room.role === 1)
            return count === 1 ? roomLabels.badgeSelfOne : roomLabels.badgeSelf.arg(count);
        if (room.state === "connecting" || room.state === "waiting")
            return roomLabels.badgeJoining;
        if (room.hostNickname.length === 0)
            return count === 1 ? roomLabels.badgeUnnamedOne : roomLabels.badgeUnnamed.arg(count);
        return count === 1 ? roomLabels.badgeOne.arg(room.hostNickname) : roomLabels.badge.arg(count).arg(room.hostNickname);
    }
    function roomDisabledReason(allowed) {
        return roomGuest && !allowed ? queueLabels.hostNotAllowed : "";
    }
    function formattedTime(milliseconds) {
        const totalSeconds = Math.floor(milliseconds / 1000);
        const minutes = Math.floor(totalSeconds / 60);
        const seconds = totalSeconds % 60;
        return String(minutes).padStart(2, "0") + ":" + String(seconds).padStart(2, "0");
    }
    // Aurora: "1:19", no leading zero on the minutes, and "1:02:03" past one hour.
    function formattedCompactTime(milliseconds) {
        const totalSeconds = Math.max(0, Math.floor(milliseconds / 1000));
        const hours = Math.floor(totalSeconds / 3600);
        const minutes = Math.floor(totalSeconds / 60);
        const seconds = String(totalSeconds % 60).padStart(2, "0");
        if (hours === 0)
            return minutes + ":" + seconds;
        return hours + ":" + String(minutes % 60).padStart(2, "0") + ":" + seconds;
    }
    // One pass over the template: a translation may reorder the placeholders, and a "%1" or "%2"
    // inside an artist or album stays text.
    function artistLine() {
        const artist = presenter.currentArtist.length > 0 ? presenter.currentArtist : navigationLabels.localAudio;
        if (presenter.currentAlbum.length === 0)
            return artist;
        const album = presenter.currentAlbum;
        return labels.artistAlbum.replace(/%([12])/g, (placeholder, index) => index === "1" ? artist : album);
    }
    // A guest never seeks the shared player locally: only the room moves the music.
    function commitImmediateSeek(target) {
        if (roomGuest)
            return;
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
        if (roomGuest)
            return queueLabels.hostControls;
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
        Layout.row: 0
        Layout.column: 0
        Layout.columnSpan: root.editorial ? 3 : 1
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
            edge: root.editorial
            handleSize: root.editorial ? 14 : 28
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
            enabled: !root.roomGuest && root.playbackEnabled && root.presenter.seekable && !root.presenter.seekPending
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
    // Fixture only: Aurora shows the time beside the track identity.
    Item {
        visible: !root.editorial
        implicitWidth: timeLabels.implicitWidth
        implicitHeight: timeLabels.implicitHeight
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
    // Aurora only. A Loader, so the fixture's own title in the window keeps these object names unique.
    Loader {
        active: root.editorial
        visible: root.editorial
        Layout.row: 1
        Layout.column: 0
        Layout.preferredWidth: root.compact ? 200 : 260
        Layout.leftMargin: 20
        Layout.bottomMargin: 14
        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
        sourceComponent: Item {
            objectName: "player-identity"
            implicitHeight: Math.max(artwork.height, details.implicitHeight)
            TrackArtwork {
                id: artwork
                objectName: "player-artwork"
                anchors.verticalCenter: parent.verticalCenter
                width: root.compact ? 48 : 64
                height: width
                radius: root.contract.radiusControl
                contract: root.contract
                source: root.presenter.currentArtworkSource
            }
            Column {
                id: details
                anchors.left: artwork.right
                anchors.leftMargin: 14
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    objectName: "player-track-title"
                    width: parent.width
                    text: root.presenter.hasCurrentTrack ? root.presenter.currentTitle : root.contract.emptyTitle
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    font.family: root.contract.headingFontFamily
                    font.pixelSize: root.contract.typeTitle
                    color: root.contract.textPrimaryColor
                    Accessible.name: text
                }
                Text {
                    objectName: "player-track-artist"
                    // Without a track, only the empty title shows.
                    visible: root.presenter.hasCurrentTrack
                    width: parent.width
                    text: root.artistLine()
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    font.family: root.contract.bodyFontFamily
                    font.pixelSize: root.contract.typeCaption
                    color: root.contract.textSecondaryColor
                    Accessible.name: text
                }
                Text {
                    objectName: "player-time"
                    visible: root.presenter.hasCurrentTrack
                    topPadding: 2
                    text: labels.elapsedOfDuration.arg(root.formattedCompactTime(root.presenter.positionMs)).arg(root.formattedCompactTime(root.presenter.durationMs))
                    textFormat: Text.PlainText
                    font.family: root.contract.bodyFontFamily
                    font.pixelSize: root.contract.typeCaption
                    color: root.contract.textSecondaryColor
                    Accessible.name: text
                }
            }
        }
    }
    RowLayout {
        id: controlsRow
        Layout.row: root.editorial ? 1 : 2
        Layout.column: root.editorial ? 1 : 0
        Layout.fillWidth: root.editorial
        Layout.bottomMargin: root.editorial ? 14 : 0
        Layout.alignment: root.editorial ? Qt.AlignVCenter : Qt.AlignHCenter
        spacing: root.editorial ? 6 : 5
        // Aurora centers the controls between the identity and the side actions.
        Item {
            visible: root.editorial
            Layout.fillWidth: true
        }
        AccessibleTransportButton {
            id: shuffleAction
            objectName: "player-shuffle"
            flat: root.editorial
            visible: root.editorial
            // A 40 px target that draws a 36 px button.
            Layout.preferredWidth: 40
            Layout.preferredHeight: 40
            topInset: 2
            bottomInset: 2
            leftInset: 2
            rightInset: 2
            iconSize: 18
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
            Layout.preferredWidth: root.editorial ? 40 : -1
            Layout.preferredHeight: root.editorial ? 40 : -1
            iconSize: 22
            font.family: root.presenter.usesFrozenFixture ? root.contract.applicationFont.family : root.contract.symbolFontFamily
            enabled: root.roomGuest ? root.roomSkip : root.presenter.canGoPrevious
            contract: root.contract
            text: "⏮"
            iconName: root.editorial ? "previous" : ""
            accessibleName: root.contract.actionPrevious
            shortcutHint: labels.previousShortcut
            disabledReason: root.roomDisabledReason(root.roomSkip)
            onClicked: root.roomGuest ? root.room.roomPrevious() : root.presenter.previousTrack()
        }
        AccessibleTransportButton {
            id: skipBackward
            flat: root.editorial
            objectName: "skip-backward"
            // Player C drops the ±5 s buttons; ←/→ and the progress bar keep the jump.
            visible: !root.editorial
            enabled: root.roomGuest ? root.roomSkip && root.room.roomHasCurrent : root.playbackEnabled && root.presenter.seekable && !root.presenter.seekPreviewActive && !root.presenter.seekPending
            contract: root.contract
            text: root.contract.skipBackwardShort.replace("{seconds}", root.presenter.skipBackwardSeconds)
            accessibleName: labels.backwardName(root.presenter.skipBackwardSeconds)
            shortcutHint: labels.backwardShortcut
            disabledReason: root.roomGuest ? root.roomDisabledReason(root.roomSkip) : root.seekDisabledReason()
            onClicked: root.roomGuest ? root.room.roomSeekBy(-root.presenter.skipBackwardSeconds * 1000) : root.presenter.skipBackward()
        }
        AccessibleTransportButton {
            id: playPause
            objectName: "play-pause"
            primary: root.editorial
            Layout.preferredWidth: root.editorial ? 52 : -1
            Layout.preferredHeight: root.editorial ? 52 : -1
            iconSize: 22
            font.family: root.presenter.usesFrozenFixture ? root.contract.applicationFont.family : root.contract.symbolFontFamily
            enabled: root.roomGuest ? root.roomPause : root.playbackEnabled
            contract: root.contract
            text: root.showsPlaying ? "⏸" : "▶"
            iconName: root.editorial ? (root.showsPlaying ? "pause" : "play") : ""
            accessibleName: root.showsPlaying ? root.contract.actionPause : root.contract.actionPlay
            shortcutHint: labels.playPauseShortcut
            disabledReason: root.roomDisabledReason(root.roomGuest && root.room.canPause)
            onClicked: root.roomGuest ? root.room.roomTogglePlayback() : root.presenter.togglePlayback()
        }
        AccessibleTransportButton {
            id: skipForward
            flat: root.editorial
            objectName: "skip-forward"
            visible: !root.editorial
            enabled: root.roomGuest ? root.roomSkip && root.room.roomHasCurrent : root.playbackEnabled && root.presenter.seekable && !root.presenter.seekPreviewActive && !root.presenter.seekPending
            contract: root.contract
            text: root.contract.skipForwardShort.replace("{seconds}", root.presenter.skipForwardSeconds)
            accessibleName: labels.forwardName(root.presenter.skipForwardSeconds)
            shortcutHint: labels.forwardShortcut
            disabledReason: root.roomGuest ? root.roomDisabledReason(root.roomSkip) : root.seekDisabledReason()
            onClicked: root.roomGuest ? root.room.roomSeekBy(root.presenter.skipForwardSeconds * 1000) : root.presenter.skipForward()
        }
        AccessibleTransportButton {
            id: nextTrack
            flat: root.editorial
            objectName: "next"
            Layout.preferredWidth: root.editorial ? 40 : -1
            Layout.preferredHeight: root.editorial ? 40 : -1
            iconSize: 22
            font.family: root.presenter.usesFrozenFixture ? root.contract.applicationFont.family : root.contract.symbolFontFamily
            enabled: root.roomGuest ? root.roomSkip : root.presenter.canGoNext
            contract: root.contract
            text: "⏭"
            iconName: root.editorial ? "next" : ""
            accessibleName: root.contract.actionNext
            shortcutHint: labels.nextShortcut
            disabledReason: root.roomDisabledReason(root.roomSkip)
            onClicked: root.roomGuest ? root.room.roomNext() : root.presenter.nextTrack()
        }
        AccessibleTransportButton {
            id: repeatAction
            objectName: "player-repeat"
            flat: root.editorial
            visible: root.editorial
            // A 40 px target that draws a 36 px button.
            Layout.preferredWidth: 40
            Layout.preferredHeight: 40
            topInset: 2
            bottomInset: 2
            leftInset: 2
            rightInset: 2
            iconSize: 18
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
        Item {
            visible: root.editorial
            Layout.fillWidth: true
        }
    }
    // Player C: the room badge above icon-only Lyrics, Queue and volume.
    ColumnLayout {
        id: rightCluster
        objectName: "player-side-actions"
        visible: root.editorial
        Layout.row: 1
        Layout.column: 2
        // Mirrors the identity's 260 px so the controls stay centered; natural width on a compact window.
        Layout.preferredWidth: root.compact ? -1 : 260
        Layout.rightMargin: 20
        Layout.bottomMargin: 14
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
        spacing: 0
        // A status chip, not a control: the side navigation opens the room page.
        Rectangle {
            id: roomBadge
            objectName: "player-room-badge"
            readonly property string text: root.roomBadgeText()
            visible: root.inRoom
            Layout.alignment: Qt.AlignRight
            Layout.maximumWidth: 200
            implicitWidth: badgeRow.implicitWidth + 16
            implicitHeight: 20
            radius: height / 2
            color: root.contract.selectionColor
            border.width: root.contract.highContrast ? 1 : 0
            border.color: root.contract.borderColor
            Accessible.role: Accessible.StaticText
            Accessible.name: text
            RowLayout {
                id: badgeRow
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 4
                ShellIcon {
                    iconName: "people"
                    size: 14
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
        RowLayout {
            id: sideIcons
            Layout.alignment: Qt.AlignRight
            spacing: 4
            AccessibleTransportButton {
                id: lyricsAction
                objectName: "player-lyrics"
                flat: true
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
                contract: root.contract
                iconName: "lyrics"
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
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
                contract: root.contract
                iconName: "queue"
                text: root.contract.queueTitle
                accessibleName: labels.queueWithCount.arg(root.queueCount)
                enabled: root.queueAvailable
                onClicked: {
                    if (enabled)
                        root.queueRequested();
                }
                // A small badge on the icon's top-right corner; high contrast outlines it instead.
                Rectangle {
                    objectName: "player-queue-count"
                    readonly property string text: String(root.queueCount)
                    anchors.top: parent.top
                    anchors.topMargin: 4
                    anchors.right: parent.right
                    anchors.rightMargin: 2
                    width: Math.max(16, queueCountText.implicitWidth + 6)
                    height: 16
                    radius: 8
                    color: root.contract.highContrast ? root.contract.canvasColor : root.contract.selectionColor
                    border.width: root.contract.highContrast ? 2 : 0
                    border.color: root.contract.textPrimaryColor
                    Accessible.ignored: true
                    Text {
                        id: queueCountText
                        objectName: "player-queue-count-text"
                        anchors.centerIn: parent
                        text: parent.text
                        textFormat: Text.PlainText
                        font.family: root.contract.bodyFontFamily
                        font.pixelSize: root.contract.typeEyebrow
                        font.weight: Font.DemiBold
                        color: root.contract.textPrimaryColor
                        Accessible.ignored: true
                    }
                }
            }
        }
    }
    RowLayout {
        id: volumeControls
        parent: root.editorial ? sideIcons : controlsRow
        Layout.alignment: root.editorial ? Qt.AlignVCenter : Qt.AlignHCenter | Qt.AlignVCenter
        spacing: root.editorial ? 4 : 5
        AccessibleTransportButton {
            id: muteAction
            objectName: "mute"
            flat: root.editorial
            Layout.preferredWidth: root.editorial ? 40 : -1
            Layout.preferredHeight: root.editorial ? 40 : -1
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
            // A compact Aurora window keeps only the mute button.
            visible: !root.editorial || !root.compact
            enabled: root.playbackEnabled
            contract: root.contract
            handleSize: root.editorial ? 16 : 28
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
            implicitWidth: root.editorial ? 90 : 120
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
            visible: root.editorial && !root.compact
            Layout.preferredWidth: 34
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
