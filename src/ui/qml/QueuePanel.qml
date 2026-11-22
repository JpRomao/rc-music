pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models
import MusicPlayer.M0

FocusScope {
    id: root
    objectName: "queue-panel"
    required property ShellUiContract contract
    required property PlayerPresenter presenter
    // True when shown as the Aurora side drawer over the home; false as a full legacy page.
    property bool overlayMode: false
    // The listening room (ListeningRoomBinding); as a guest the panel shows the room queue instead.
    property var room: null
    readonly property bool roomGuest: room !== null && room.available && room.role === 2
    // S10.1: a guest edits the room queue only with the host's permission; the edits are room intents.
    readonly property bool roomEdit: roomGuest && room.canEditQueue
    property alias listView: queueList
    property string selectedOccurrenceId: ""
    property string selectedTitle: ""
    property bool selectedAvailable: false
    property string menuOccurrenceId: ""
    property bool lyricVariantActionSupported: false
    property bool lyricVariantActionAvailable: false
    readonly property bool menuTargetSelected: {
        const row = queueList.currentItem as QueueOccurrenceDelegate;
        return root.visible && root.enabled && !root.presenter.exiting && menuOccurrenceId.length > 0 && selectedOccurrenceId === menuOccurrenceId && row !== null && row.occurrenceId === menuOccurrenceId;
    }
    readonly property bool selectedRetryAvailable: !presenter.usesFrozenFixture && !presenter.exiting && !presenter.selectingAudioFiles && selectedOccurrenceId.length > 0 && !selectedAvailable && presenter.retryingOccurrenceId.length === 0 && presenter.locatingOccurrenceId.length === 0 && presenter.playbackState !== PlayerPresenter.Loading
    readonly property bool selectedLocationAvailable: !presenter.usesFrozenFixture && presenter.fileLocationAvailable && !presenter.exiting && !presenter.selectingAudioFiles && selectedOccurrenceId.length > 0 && !selectedAvailable && presenter.retryingOccurrenceId.length === 0 && presenter.locatingOccurrenceId.length === 0
    property bool locationFocusRestoreAllowed: true
    property string requestedLocationId: ""
    property bool addingFilesPending: false
    property bool addTerminalObserved: false
    property int addFocusRevision: 0
    property int queueFocusRevision: 0
    property string accessibilityAnnouncement: ""
    readonly property string observedCurrentOccurrenceId: presenter.currentOccurrenceId
    readonly property bool clearConfirmationOpen: clearConfirmation.opened
    readonly property bool contextMenuOpen: rowMenu.visible
    readonly property bool queueModesAvailable: !roomGuest && !presenter.usesFrozenFixture && !presenter.exiting && !presenter.selectingAudioFiles && presenter.locatingOccurrenceId.length === 0
    readonly property bool queueFocusRestoreAllowed: visible && enabled && locationFocusRestoreAllowed && !presenter.exiting && !presenter.selectingAudioFiles && !addingFilesPending && presenter.locatingOccurrenceId.length === 0 && !clearConfirmation.visible
    // The toolbar actions, shared by the fixture buttons and the Aurora drawer buttons.
    readonly property bool addAvailable: !roomGuest && !presenter.exiting && !presenter.usesFrozenFixture && !presenter.selectingAudioFiles && presenter.locatingOccurrenceId.length === 0
    readonly property bool moveUpAvailable: roomGuest ? roomEdit && roomQueueList.currentIndex > 0 : !presenter.exiting && !presenter.usesFrozenFixture && queueList.currentIndex > 0
    readonly property bool moveDownAvailable: roomGuest ? roomEdit && roomQueueList.currentIndex >= 0 && roomQueueList.currentIndex < roomQueueList.count - 1 : !presenter.exiting && !presenter.usesFrozenFixture && queueList.currentIndex >= 0 && queueList.currentIndex < queueList.count - 1
    readonly property bool removeAvailable: roomGuest ? roomEdit && roomQueueList.currentIndex >= 0 : !presenter.exiting && !presenter.usesFrozenFixture && queueList.currentIndex >= 0
    readonly property bool clearAvailable: !roomGuest && !presenter.exiting && !presenter.usesFrozenFixture && queueList.count > 0
    // S9.1: a guest's player belongs to the room until it leaves.
    readonly property string addHint: roomGuest ? queueText.guestLocalQueueHeld : ""
    // The toolbar of the current mode; its buttons live in a Loader because the ids repeat.
    readonly property QueueActionsRow actionsRow: (overlayMode ? auroraActions.item : fixtureActions.item) as QueueActionsRow
    // Aurora drawer header: the local count and, when every track has one, the total duration.
    // A guest sees the room queue, so the local count does not apply.
    readonly property string subtitleText: {
        if (roomGuest)
            return "";
        const count = queueText.trackCount(queueList.count);
        const total = presenter.queueTotalDurationMs;
        return typeof total === "number" ? queueText.countAndDuration.arg(count).arg(durationText(total)) : count;
    }
    // Aurora: an empty local queue offers to open audio instead of an empty list.
    readonly property bool emptyShown: overlayMode && !roomGuest && queueList.count === 0
    signal closeRequested
    signal openAudioRequested
    signal playRequested(string occurrenceId)
    signal addLyricVariantRequested(string occurrenceId, string displayTitle)

    RoomText {
        id: roomText
    }
    QueueText {
        id: queueText
    }
    component QueueActionsRow: RowLayout {
        required property Item addControl
        required property Item clearControl
    }
    component QueueOccurrenceDelegate: ItemDelegate {
        required property string occurrenceId
        required property string fileName
        required property string title
        required property string artist
        required property string album
        required property var durationMs
    }

    function formatDuration(milliseconds) {
        if (typeof milliseconds !== "number" || !isFinite(milliseconds))
            return root.contract.notInformed;
        const totalSeconds = Math.floor(milliseconds / 1000);
        const minutes = Math.floor(totalSeconds / 60);
        const seconds = totalSeconds % 60;
        return String(minutes).padStart(2, "0") + ":" + String(seconds).padStart(2, "0");
    }
    // The minutes rounded up (at least 1); from 60 rounded minutes on, hours and minutes.
    function durationText(milliseconds) {
        const minutes = Math.max(1, Math.ceil(milliseconds / 60000));
        if (minutes < 60)
            return queueText.durationMinutes.arg(minutes);
        return queueText.durationHours.arg(Math.floor(minutes / 60)).arg(minutes % 60);
    }
    function editHint(shortcut) {
        return roomGuest && !roomEdit ? queueText.hostNotAllowed : shortcut;
    }
    // The Aurora drawer is modal: a relay reached by Tab hands the focus to the first control, by Shift+Tab
    // to the last. KeyNavigation is reciprocal, so the relays keep the wrap inside the drawer.
    function wrapFocus(reason) {
        const forward = reason !== Qt.BacktabFocusReason;
        let target = forward ? focusWrapStart.nextItemInFocusChain(true) : focusWrapEnd.nextItemInFocusChain(false);
        // Never rest on an invisible relay: fall back to the close button.
        if (!target || target === focusWrapStart || target === focusWrapEnd) {
            const header = headerLoader.item as AuroraDrawerHeader;
            target = header ? header.closeButton : null;
        }
        if (target)
            target.forceActiveFocus(forward ? Qt.TabFocusReason : Qt.BacktabFocusReason);
    }
    function rowAccessibleName(title, fileName, artist, durationMs, available, errorCode, position, total, retrying, locating, locationFailed) {
        const displayTitle = title.length > 0 ? title : fileName;
        const parts = [displayTitle];
        if (artist.length > 0)
            parts.push(artist);
        parts.push(root.formatDuration(durationMs));
        parts.push(queueText.positionOfTotal.replace("{position}", position).replace("{total}", total));
        if (locating)
            parts.push(queueText.locating);
        else if (locationFailed)
            parts.push(queueText.locationFailed);
        else if (retrying)
            parts.push(queueText.retrying);
        else if (!available)
            parts.push(queueText.unavailableReason(errorCode));
        return parts.join(". ") + (parts[parts.length - 1].endsWith(".") ? "" : ".");
    }
    function syncCurrent() {
        // The open menu owns its occurrence, not the changing playback cursor.
        if (rowMenu.visible)
            return;
        if (queueList.count === 0) {
            root.selectedOccurrenceId = "";
            root.selectedTitle = "";
            root.selectedAvailable = false;
            return;
        }
        const targetIndex = root.presenter.currentQueueIndex >= 0 ? root.presenter.currentQueueIndex : 0;
        queueList.currentIndex = targetIndex;
        queueList.positionViewAtIndex(targetIndex, ListView.Center);
        queueList.forceLayout();
    }
    function focusCurrent() {
        if (rowMenu.visible)
            return;
        syncCurrent();
        queueList.forceActiveFocus(Qt.TabFocusReason);
    }
    function selectFocusOccurrence(index) {
        if (index < 0 || index >= queueList.count)
            return "";
        queueList.currentIndex = index;
        queueList.positionViewAtIndex(index, ListView.Contain);
        queueList.forceLayout();
        const selectedRow = queueList.currentItem as QueueOccurrenceDelegate;
        return selectedRow ? selectedRow.occurrenceId : "";
    }
    function playRow(row, focusReason) {
        if (!row || !row.available)
            return;
        queueList.currentIndex = row.index;
        queueList.forceActiveFocus(focusReason);
        root.playRequested(row.occurrenceId);
    }
    function addFiles() {
        if (!root.addAvailable)
            return;
        root.addingFilesPending = true;
        root.addTerminalObserved = false;
        ++root.addFocusRevision;
        if (!root.presenter.addAudio())
            root.addingFilesPending = false;
    }
    function forgetPendingAdd() {
        addingFilesPending = false;
        addTerminalObserved = false;
        ++addFocusRevision;
    }
    function moveSelected(offset) {
        if (root.selectedOccurrenceId.length === 0)
            return;
        const destination = queueList.currentIndex + offset;
        const displayTitle = root.selectedTitle.length > 0 ? root.selectedTitle : root.selectedOccurrenceId;
        if (!root.presenter.moveQueueOccurrence(root.selectedOccurrenceId, destination))
            return;
        const message = queueText.movedAnnouncement.replace("{position}", destination + 1).replace("{total}", queueList.count).split("{title}").join(displayTitle);
        root.accessibilityAnnouncement = message;
        queueList.Accessible.announce(message, Accessible.Polite);
    }
    function roomItemAt(index) {
        const row = roomQueueList.itemAtIndex(index);
        return row !== null ? row.itemId : "";
    }
    function moveRoomSelected(offset) {
        const destination = roomQueueList.currentIndex + offset;
        if (!root.roomEdit || destination < 0 || destination >= roomQueueList.count)
            return;
        if (root.room.roomMove(root.roomItemAt(roomQueueList.currentIndex), destination))
            roomQueueList.currentIndex = destination;
    }
    function removeRoomSelected() {
        if (root.roomEdit && roomQueueList.currentIndex >= 0)
            root.room.roomRemove(root.roomItemAt(roomQueueList.currentIndex));
    }
    function removeSelected() {
        if (root.selectedOccurrenceId.length > 0)
            root.presenter.removeQueueOccurrence(root.selectedOccurrenceId);
    }
    function retrySelected() {
        if (!root.selectedRetryAvailable)
            return;
        const occurrenceId = root.selectedOccurrenceId;
        rowMenu.close();
        queueList.forceActiveFocus(Qt.TabFocusReason);
        root.presenter.retryQueueOccurrence(occurrenceId);
    }
    function locateSelected() {
        if (!root.selectedLocationAvailable)
            return;
        const occurrenceId = root.selectedOccurrenceId;
        root.requestedLocationId = occurrenceId;
        rowMenu.close();
        queueList.forceActiveFocus(Qt.TabFocusReason);
        if (!root.presenter.locateQueueOccurrence(occurrenceId))
            root.requestedLocationId = "";
    }
    function openRowMenu(row) {
        if (!row)
            return;
        ++root.queueFocusRevision;
        queueList.currentIndex = row.index;
        root.menuOccurrenceId = row.occurrenceId;
        rowMenu.popup(row, 0, row.height);
    }
    function requestClear() {
        if (queueList.count > 0)
            clearConfirmation.open();
    }
    function toggleShuffle() {
        if (queueModesAvailable)
            presenter.toggleShuffleQueue();
    }
    function toggleRepeat() {
        if (queueModesAvailable)
            presenter.toggleRepeatQueue();
    }
    function confirmClear() {
        if (!root.presenter.clearQueue())
            return;
        clearConfirmation.accepted = true;
        clearConfirmation.close();
    }
    function dropInsertionIndexAt(panelX, panelY) {
        if (!root.visible)
            return -1;
        const point = queueList.mapFromItem(root, panelX, panelY);
        if (point.x < 0 || point.x > queueList.width || point.y < 0 || point.y > queueList.height)
            return -1;
        if (queueList.count === 0)
            return 0;
        queueList.forceLayout();
        const contentX = queueList.contentX + point.x;
        const contentY = queueList.contentY + point.y;
        const rowIndex = queueList.indexAt(contentX, contentY);
        if (rowIndex < 0)
            return queueList.count;
        const row = queueList.itemAtIndex(rowIndex);
        if (!row)
            return queueList.count;
        return rowIndex + (contentY >= row.y + row.height / 2 ? 1 : 0);
    }
    function dropIndicatorViewportY(index) {
        queueList.forceLayout();
        if (queueList.count === 0 || index <= 0) {
            const first = queueList.itemAtIndex(0);
            return first ? first.mapToItem(queueList, 0, 0).y : 0;
        }
        if (index >= queueList.count) {
            const last = queueList.itemAtIndex(queueList.count - 1);
            return last ? last.mapToItem(queueList, 0, last.height).y : queueList.height;
        }
        const row = queueList.itemAtIndex(index);
        return row ? row.mapToItem(queueList, 0, 0).y : queueList.height;
    }

    onObservedCurrentOccurrenceIdChanged: {
        const revision = queueFocusRevision;
        if (visible && !presenter.selectingAudioFiles && !addingFilesPending)
            Qt.callLater(function () {
                // Keep the keyboard action target while the list owns focus.
                if (revision === root.queueFocusRevision && !root.presenter.selectingAudioFiles && !root.addingFilesPending && (!queueList.activeFocus || !queueList.currentItem))
                    root.syncCurrent();
            });
    }
    onQueueFocusRestoreAllowedChanged: {
        if (!queueFocusRestoreAllowed)
            ++queueFocusRevision;
    }
    onPresenterChanged: ++queueFocusRevision
    onMenuTargetSelectedChanged: {
        if (rowMenu.visible && !menuTargetSelected)
            rowMenu.close();
    }

    Rectangle {
        // The drawer edge is drawn by the shell; the legacy page keeps its card.
        objectName: "queue-panel-background"
        anchors.fill: parent
        color: root.contract.surfaceColor
        radius: root.overlayMode ? 0 : root.contract.radiusMedium
        border.width: root.overlayMode ? 0 : 1
        border.color: root.contract.borderColor
    }

    Control {
        id: focusWrapStart
        objectName: "queue-focus-wrap-start"
        visible: root.overlayMode
        width: 0
        height: 0
        focusPolicy: Qt.TabFocus
        Accessible.ignored: true
        onActiveFocusChanged: {
            if (activeFocus)
                root.wrapFocus(focusReason);
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.contract.spacingUnit * 4
        spacing: root.contract.spacingUnit * 3

        Loader {
            id: headerLoader
            active: root.overlayMode
            visible: active
            Layout.fillWidth: true
            sourceComponent: AuroraDrawerHeader {
                contract: root.contract
                controlId: "queue-header"
                closeText: queueText.closeQueue
                eyebrow: queueText.drawerEyebrow
                title: root.contract.queueTitle
                subtitle: root.subtitleText
                onCloseRequested: root.closeRequested()
            }
        }
        Text {
            id: queueTitle
            objectName: "queue-title"
            visible: !root.overlayMode
            text: root.contract.queueTitle
            color: root.contract.textPrimaryColor
            font: Qt.font({
                bold: true
            })
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }

        Loader {
            id: fixtureActions
            active: !root.overlayMode
            visible: active
            Layout.fillWidth: true
            sourceComponent: QueueActionsRow {
                objectName: "queue-actions"
                addControl: fixtureAddButton
                clearControl: fixtureClearButton
                spacing: root.contract.spacingUnit

                QueueActionButton {
                    id: fixtureAddButton
                    controlId: "queue-add"
                    accessibleName: queueText.add
                    text: "+"
                    contract: root.contract
                    shortcutHint: root.addHint
                    enabled: root.addAvailable
                    onClicked: root.addFiles()
                }
                QueueActionButton {
                    controlId: "queue-move-up"
                    accessibleName: queueText.moveUp
                    shortcutHint: root.editHint(queueText.shortcutMoveUp)
                    text: "↑"
                    contract: root.contract
                    enabled: root.moveUpAvailable
                    onClicked: root.roomGuest ? root.moveRoomSelected(-1) : root.moveSelected(-1)
                }
                QueueActionButton {
                    controlId: "queue-move-down"
                    accessibleName: queueText.moveDown
                    shortcutHint: root.editHint(queueText.shortcutMoveDown)
                    text: "↓"
                    contract: root.contract
                    enabled: root.moveDownAvailable
                    onClicked: root.roomGuest ? root.moveRoomSelected(1) : root.moveSelected(1)
                }
                QueueActionButton {
                    controlId: "queue-remove"
                    accessibleName: queueText.remove
                    shortcutHint: root.editHint(queueText.shortcutRemove)
                    text: "−"
                    contract: root.contract
                    enabled: root.removeAvailable
                    onClicked: root.roomGuest ? root.removeRoomSelected() : root.removeSelected()
                }
                // S12.1: a guest with the permission sends one of its own files to the room's queue.
                QueueActionButton {
                    controlId: "queue-room-upload"
                    visible: root.roomGuest && root.room.canUploadOwnTracks
                    iconName: "plus"
                    accessibleName: roomText.uploadOwnName
                    text: roomText.uploadOwn
                    contract: root.contract
                    onClicked: root.room.roomUploadOwnTrack()
                }
                Item {
                    Layout.fillWidth: true
                }
                QueueActionButton {
                    id: fixtureClearButton
                    controlId: "queue-clear"
                    accessibleName: queueText.clear
                    text: "×"
                    contract: root.contract
                    enabled: root.clearAvailable
                    onClicked: root.requestClear()
                }
            }
        }

        Loader {
            id: auroraActions
            active: root.overlayMode
            visible: active
            Layout.fillWidth: true
            sourceComponent: QueueActionsRow {
                objectName: "queue-actions"
                addControl: auroraAddButton
                clearControl: auroraClearButton
                spacing: root.contract.spacingUnit

                AuroraIconButton {
                    id: auroraAddButton
                    contract: root.contract
                    controlId: "queue-add"
                    iconName: "plus"
                    text: queueText.addFiles
                    description: root.addHint
                    enabled: root.addAvailable
                    onClicked: root.addFiles()
                }
                AuroraIconButton {
                    contract: root.contract
                    controlId: "queue-move-up"
                    iconName: "arrow-up"
                    text: queueText.moveUp
                    description: root.editHint(queueText.shortcutMoveUp)
                    enabled: root.moveUpAvailable
                    onClicked: root.roomGuest ? root.moveRoomSelected(-1) : root.moveSelected(-1)
                }
                AuroraIconButton {
                    contract: root.contract
                    controlId: "queue-move-down"
                    iconName: "arrow-down"
                    text: queueText.moveDown
                    description: root.editHint(queueText.shortcutMoveDown)
                    enabled: root.moveDownAvailable
                    onClicked: root.roomGuest ? root.moveRoomSelected(1) : root.moveSelected(1)
                }
                AuroraIconButton {
                    contract: root.contract
                    controlId: "queue-remove"
                    iconName: "minus"
                    text: queueText.removeFromQueue
                    description: root.editHint(queueText.shortcutRemove)
                    enabled: root.removeAvailable
                    onClicked: root.roomGuest ? root.removeRoomSelected() : root.removeSelected()
                }
                // S12.1: a guest with the permission sends one of its own files to the room's queue.
                AuroraIconButton {
                    contract: root.contract
                    controlId: "queue-room-upload"
                    visible: root.roomGuest && root.room.canUploadOwnTracks
                    iconName: "plus"
                    text: roomText.uploadOwnName
                    onClicked: root.room.roomUploadOwnTrack()
                }
                Item {
                    Layout.fillWidth: true
                }
                AuroraButton {
                    id: auroraClearButton
                    contract: root.contract
                    controlId: "queue-clear"
                    kind: AuroraButton.Quiet
                    errorInk: true
                    iconName: "trash"
                    text: queueText.clearEllipsis
                    Accessible.name: queueText.clearName
                    enabled: root.clearAvailable
                    onClicked: root.requestClear()
                }
            }
        }

        RowLayout {
            objectName: "queue-modes"
            visible: !root.overlayMode
            Layout.fillWidth: true
            spacing: root.contract.spacingUnit

            AccessibleTransportButton {
                objectName: "queue-shuffle"
                flat: root.overlayMode
                iconName: root.overlayMode ? "shuffle" : ""
                iconWithText: true
                Layout.fillWidth: true
                contract: root.contract
                text: root.presenter.shuffleQueueEnabled ? queueText.shuffleOn : queueText.shuffleOff
                accessibleName: queueText.shuffle
                shortcutHint: queueText.shortcutShuffle
                disabledReason: root.roomGuest ? queueText.hostControls : ""
                enabled: root.queueModesAvailable
                checkable: true
                checked: root.presenter.shuffleQueueEnabled
                Accessible.checkable: true
                Accessible.checked: checked
                onClicked: root.toggleShuffle()
            }
            AccessibleTransportButton {
                objectName: "queue-repeat"
                flat: root.overlayMode
                iconName: root.overlayMode ? "repeat" : ""
                iconWithText: true
                Layout.fillWidth: true
                contract: root.contract
                text: root.presenter.repeatQueueEnabled ? queueText.repeatOn : queueText.repeatOff
                accessibleName: queueText.repeat
                shortcutHint: queueText.shortcutRepeat
                disabledReason: root.roomGuest ? queueText.hostControls : ""
                enabled: root.queueModesAvailable
                checkable: true
                checked: root.presenter.repeatQueueEnabled
                Accessible.checkable: true
                Accessible.checked: checked
                onClicked: root.toggleRepeat()
            }
        }

        AuroraEmptyState {
            visible: root.emptyShown
            contract: root.contract
            controlId: "queue-empty"
            iconName: "queue"
            title: queueText.emptyTitle
            primaryActionText: root.contract.openAudioText
            onPrimaryTriggered: root.openAudioRequested()
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        ListView {
            id: queueList
            objectName: "queue-list"
            readonly property bool focusIndicatorVisible: activeFocus && enabled
            visible: !root.roomGuest && !root.emptyShown
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.presenter.queueModel
            clip: true
            reuseItems: true
            cacheBuffer: 0
            delegateModelAccess: DelegateModel.ReadOnly
            keyNavigationEnabled: true
            keyNavigationWraps: false
            activeFocusOnTab: true
            highlightMoveDuration: 0
            highlightResizeDuration: 0
            Accessible.role: Accessible.List
            Accessible.id: "queue-list"
            Accessible.name: root.contract.queueTitle
            Accessible.description: queueText.selectionShortcut
            Accessible.focusable: enabled
            Accessible.focused: activeFocus
            Keys.priority: Keys.BeforeItem
            Keys.onPressed: event => {
                if (event.modifiers === Qt.ControlModifier && (event.key === Qt.Key_Up || event.key === Qt.Key_Down)) {
                    const offset = event.key === Qt.Key_Up ? -1 : 1;
                    queueList.currentIndex = Math.max(0, Math.min(queueList.count - 1, queueList.currentIndex + offset));
                    queueList.positionViewAtIndex(queueList.currentIndex, ListView.Contain);
                    event.accepted = true;
                    return;
                }
                if (queueList.count > 0 && (event.modifiers === Qt.NoModifier || event.modifiers === Qt.KeypadModifier) && (event.key === Qt.Key_Home || event.key === Qt.Key_End)) {
                    root.selectFocusOccurrence(event.key === Qt.Key_Home ? 0 : queueList.count - 1);
                    event.accepted = true;
                    return;
                }
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    root.playRow(queueList.currentItem, Qt.TabFocusReason);
                    event.accepted = true;
                    return;
                }
                if (event.key === Qt.Key_Delete) {
                    root.removeSelected();
                    event.accepted = true;
                    return;
                }
                if ((event.modifiers & Qt.AltModifier) && event.key === Qt.Key_Up) {
                    root.moveSelected(-1);
                    event.accepted = true;
                    return;
                }
                if ((event.modifiers & Qt.AltModifier) && event.key === Qt.Key_Down) {
                    root.moveSelected(1);
                    event.accepted = true;
                    return;
                }
                if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                    root.openRowMenu(queueList.currentItem);
                    event.accepted = true;
                }
            }

            delegate: QueueOccurrenceDelegate {
                id: row
                required property int index
                required property int orderIndex
                required property bool available
                required property var errorCode
                readonly property bool retrying: occurrenceId === root.presenter.retryingOccurrenceId
                readonly property bool locating: occurrenceId === root.presenter.locatingOccurrenceId
                readonly property bool locationFailed: !available && occurrenceId === root.presenter.fileLocationErrorOccurrenceId
                readonly property bool contrastHighlighted: root.contract.highContrast && (ListView.isCurrentItem || hovered)
                readonly property color primaryColor: contrastHighlighted ? root.contract.accentTextColor : root.contract.textPrimaryColor
                readonly property color secondaryColor: contrastHighlighted ? root.contract.accentTextColor : root.contract.textSecondaryColor
                objectName: "queue-row-" + occurrenceId
                width: ListView.view.width
                readonly property bool isPlaying: occurrenceId === root.presenter.currentOccurrenceId
                readonly property bool statusShown: locating || locationFailed || retrying || !available
                height: root.overlayMode ? 56 : 64
                padding: root.contract.spacingUnit * 2
                activeFocusOnTab: false
                hoverEnabled: true
                Accessible.role: Accessible.ListItem
                Accessible.id: occurrenceId
                Accessible.name: root.rowAccessibleName(title, fileName, artist, durationMs, available, errorCode, orderIndex + 1, queueList.count, retrying, locating, locationFailed)
                Accessible.description: ""
                Accessible.focusable: true
                Accessible.focused: activeFocus
                Accessible.selectable: true
                Accessible.selected: ListView.isCurrentItem
                ListView.onIsCurrentItemChanged: {
                    if (ListView.isCurrentItem) {
                        root.selectedOccurrenceId = row.occurrenceId;
                        root.selectedTitle = row.title.length > 0 ? row.title : row.fileName;
                        root.selectedAvailable = row.available;
                    }
                }
                onAvailableChanged: {
                    if (ListView.isCurrentItem)
                        root.selectedAvailable = row.available;
                }
                onTitleChanged: {
                    if (ListView.isCurrentItem)
                        root.selectedTitle = row.title.length > 0 ? row.title : row.fileName;
                }
                Accessible.onPressAction: root.playRow(row, Qt.TabFocusReason)
                onClicked: {
                    queueList.currentIndex = row.index;
                    queueList.forceActiveFocus(Qt.MouseFocusReason);
                }
                onDoubleClicked: root.playRow(row, Qt.MouseFocusReason)

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: root.openRowMenu(row)
                }

                background: Rectangle {
                    color: root.contract.highContrast ? (row.contrastHighlighted ? root.contract.accentColor : root.contract.surfaceColor) : (root.overlayMode ? (row.ListView.isCurrentItem ? root.contract.selectionColor : (row.hovered ? root.contract.hoverColor : "transparent")) : (row.ListView.isCurrentItem ? Qt.alpha(root.contract.accentColor, 0.12) : (row.hovered ? Qt.alpha(root.contract.focusColor, 0.08) : "transparent")))
                    radius: root.contract.radiusSmall
                    border.width: row.ListView.isCurrentItem ? (queueList.activeFocus ? 3 : (root.contract.highContrast ? 2 : 0)) : (root.contract.highContrast && row.hovered ? 1 : 0)
                    border.color: row.contrastHighlighted ? root.contract.accentTextColor : root.contract.focusColor
                }

                contentItem: RowLayout {
                    spacing: root.contract.spacingUnit * 2

                    // Aurora: the playing track shows a waveform where the other rows show their number.
                    Item {
                        visible: root.overlayMode && row.isPlaying
                        Layout.preferredWidth: 24
                        Layout.preferredHeight: 24
                        Accessible.ignored: true

                        ShellIcon {
                            objectName: "queue-row-now-icon"
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "waveform"
                            size: 18
                            visible: parent.visible
                            color: row.contrastHighlighted ? root.contract.accentTextColor : root.contract.accentColor
                        }
                    }
                    Text {
                        // Aurora numbers the queue; the legacy page marks the playing row.
                        objectName: "queue-row-number"
                        visible: !(root.overlayMode && row.isPlaying)
                        text: root.overlayMode ? String(row.orderIndex + 1) : (row.isPlaying ? "▶" : "")
                        color: root.overlayMode ? row.secondaryColor : row.primaryColor
                        font: root.overlayMode ? Qt.font({
                            family: root.contract.bodyFontFamily,
                            pixelSize: root.contract.typeCaption
                        }) : root.contract.applicationFont
                        Layout.preferredWidth: root.overlayMode ? 24 : 16
                        Accessible.ignored: true
                    }
                    TrackArtwork {
                        objectName: "queue-row-artwork"
                        visible: root.overlayMode
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        implicitWidth: 40
                        implicitHeight: 40
                        radius: root.contract.radiusSmall
                        contract: root.contract
                        source: row.isPlaying ? root.presenter.currentArtworkSource : ""
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            objectName: "queue-row-title"
                            text: row.title.length > 0 ? row.title : row.fileName
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: root.overlayMode && !root.contract.highContrast && row.isPlaying ? root.contract.accentColor : row.primaryColor
                            Layout.fillWidth: true
                            Accessible.ignored: true
                        }
                        Text {
                            objectName: "queue-row-artist"
                            // Aurora: the status takes the place of the artist.
                            visible: !(root.overlayMode && row.statusShown)
                            text: row.artist
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: row.secondaryColor
                            Layout.fillWidth: true
                            Accessible.ignored: true
                        }
                        Text {
                            objectName: "queue-row-status"
                            text: row.locating ? queueText.locating : (row.locationFailed ? queueText.locationFailed : (row.retrying ? queueText.retrying : queueText.unavailableReason(row.errorCode)))
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: root.overlayMode && !row.contrastHighlighted ? root.contract.warningColor : row.secondaryColor
                            visible: row.statusShown
                            Layout.fillWidth: true
                            Accessible.ignored: true
                        }
                    }
                    Text {
                        text: root.formatDuration(row.durationMs)
                        color: row.secondaryColor
                        Accessible.ignored: true
                    }
                }
            }

            ScrollBar.vertical: AccessibleScrollBar {
                objectName: "queue-scrollbar"
                contract: root.contract
            }

            Rectangle {
                anchors.fill: parent
                color: "transparent"
                radius: root.contract.radiusSmall
                border.width: 3
                border.color: root.contract.focusColor
                visible: queueList.focusIndicatorVisible && queueList.currentItem === null
                z: 10
            }
        }

        // S8.2: a guest sees the room queue: the host's order, who added each item, and
        // a loading hint until the item's file is in the local room cache.
        ListView {
            id: roomQueueList
            objectName: "room-queue-list"
            visible: root.roomGuest
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.roomGuest ? root.room.queue : null
            clip: true
            keyNavigationEnabled: true
            activeFocusOnTab: true
            highlightMoveDuration: 0
            Accessible.role: Accessible.List
            Accessible.name: queueText.roomQueue
            Accessible.description: root.roomEdit ? "" : queueText.hostNotAllowed
            Accessible.focusable: true
            Accessible.focused: activeFocus
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Delete) {
                    root.removeRoomSelected();
                    event.accepted = true;
                } else if ((event.modifiers & Qt.AltModifier) && (event.key === Qt.Key_Up || event.key === Qt.Key_Down)) {
                    root.moveRoomSelected(event.key === Qt.Key_Up ? -1 : 1);
                    event.accepted = true;
                }
            }

            delegate: ItemDelegate {
                id: roomRow
                required property int index
                required property string itemId
                required property string title
                required property string artist
                required property string addedBy
                required property string avatarSource
                required property bool available
                required property bool current
                objectName: "room-queue-row-" + itemId
                width: ListView.view.width
                padding: root.contract.spacingUnit
                focusPolicy: Qt.NoFocus
                Accessible.role: Accessible.ListItem
                Accessible.name: title
                Accessible.description: available ? queueText.addedBy.arg(addedBy) : queueText.loadingAddedBy.arg(addedBy)
                Accessible.selected: ListView.isCurrentItem
                onClicked: roomQueueList.currentIndex = index

                background: Rectangle {
                    color: roomRow.ListView.isCurrentItem && roomQueueList.activeFocus ? root.contract.selectionColor : "transparent"
                    radius: root.contract.radiusSmall
                    border.width: roomRow.ListView.isCurrentItem && roomQueueList.activeFocus ? 3 : 0
                    border.color: root.contract.focusColor
                }

                contentItem: RowLayout {
                    spacing: root.contract.spacingUnit * 2

                    ListenerAvatar {
                        objectName: "room-queue-avatar"
                        source: roomRow.avatarSource
                        size: 28
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            objectName: "room-queue-title"
                            text: roomRow.title
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: roomRow.current && !root.contract.highContrast ? root.contract.accentColor : root.contract.textPrimaryColor
                            Layout.fillWidth: true
                            Accessible.ignored: true
                        }
                        Text {
                            text: roomRow.artist
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: root.contract.textSecondaryColor
                            visible: text.length > 0
                            Layout.fillWidth: true
                            Accessible.ignored: true
                        }
                        Text {
                            objectName: "room-queue-added-by"
                            text: queueText.addedBy.arg(roomRow.addedBy)
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: root.contract.textSecondaryColor
                            Layout.fillWidth: true
                            Accessible.ignored: true
                        }
                    }
                    Text {
                        objectName: "room-queue-loading"
                        text: queueText.loading
                        color: root.contract.textSecondaryColor
                        visible: !roomRow.available
                        Accessible.ignored: true
                    }
                }
            }

            ScrollBar.vertical: AccessibleScrollBar {
                objectName: "room-queue-scrollbar"
                contract: root.contract
            }
        }
    }

    Control {
        id: focusWrapEnd
        objectName: "queue-focus-wrap-end"
        visible: root.overlayMode
        width: 0
        height: 0
        focusPolicy: Qt.TabFocus
        Accessible.ignored: true
        onActiveFocusChanged: {
            if (activeFocus)
                root.wrapFocus(focusReason);
        }
    }

    Menu {
        id: rowMenu
        objectName: "queue-row-menu"
        MenuItem {
            objectName: "queue-menu-play-now"
            text: queueText.playNow
            enabled: root.menuTargetSelected && root.selectedAvailable
            onTriggered: {
                if (root.menuTargetSelected)
                    root.playRow(queueList.currentItem, Qt.TabFocusReason);
            }
        }
        MenuItem {
            id: queuedVariantMenuItem
            objectName: "queue-menu-add-lyrics-variant"
            text: queueText.manageLyrics
            visible: root.lyricVariantActionSupported && !root.presenter.usesFrozenFixture
            enabled: visible && root.lyricVariantActionAvailable && root.queueModesAvailable && root.menuTargetSelected && root.selectedAvailable
            onTriggered: {
                if (!queuedVariantMenuItem.enabled)
                    return;
                const occurrenceId = root.menuOccurrenceId;
                const row = queueList.currentItem as QueueOccurrenceDelegate;
                if (!row || row.occurrenceId !== occurrenceId)
                    return;
                const displayTitle = row.title.length > 0 ? row.title : row.fileName;
                rowMenu.close();
                root.addLyricVariantRequested(occurrenceId, displayTitle);
            }
        }
        MenuItem {
            id: retryMenuItem
            objectName: "queue-menu-retry"
            text: queueText.retry
            visible: !root.presenter.usesFrozenFixture && root.selectedOccurrenceId.length > 0 && (!root.selectedAvailable || root.selectedOccurrenceId === root.presenter.retryingOccurrenceId)
            enabled: root.menuTargetSelected && root.selectedRetryAvailable
            Accessible.id: "queue-menu-retry"
            Accessible.role: Accessible.MenuItem
            Accessible.name: text
            Accessible.description: root.presenter.retryingOccurrenceId.length > 0 ? queueText.retrying : queueText.retryDescription
            Accessible.onPressAction: {
                if (root.menuTargetSelected)
                    root.retrySelected();
            }
            onTriggered: {
                if (root.menuTargetSelected)
                    root.retrySelected();
            }
        }
        MenuItem {
            objectName: "queue-menu-locate"
            text: queueText.locate
            visible: !root.presenter.usesFrozenFixture && root.presenter.fileLocationAvailable && root.selectedOccurrenceId.length > 0 && !root.selectedAvailable
            enabled: root.menuTargetSelected && root.selectedLocationAvailable
            Accessible.id: "queue-menu-locate"
            Accessible.role: Accessible.MenuItem
            Accessible.name: text
            Accessible.description: root.presenter.locatingOccurrenceId.length > 0 ? queueText.locating : queueText.locateDescription
            Accessible.onPressAction: {
                if (root.menuTargetSelected)
                    root.locateSelected();
            }
            onTriggered: {
                if (root.menuTargetSelected)
                    root.locateSelected();
            }
        }
        MenuSeparator {}
        MenuItem {
            objectName: "queue-menu-move-up"
            text: queueText.moveUp
            enabled: root.menuTargetSelected && !root.presenter.usesFrozenFixture && queueList.currentIndex > 0
            onTriggered: {
                if (root.menuTargetSelected)
                    root.moveSelected(-1);
            }
        }
        MenuItem {
            objectName: "queue-menu-move-down"
            text: queueText.moveDown
            enabled: root.menuTargetSelected && !root.presenter.usesFrozenFixture && queueList.currentIndex >= 0 && queueList.currentIndex < queueList.count - 1
            onTriggered: {
                if (root.menuTargetSelected)
                    root.moveSelected(1);
            }
        }
        MenuItem {
            objectName: "queue-menu-remove"
            text: queueText.remove
            enabled: root.menuTargetSelected && !root.presenter.usesFrozenFixture && queueList.currentIndex >= 0
            onTriggered: {
                if (root.menuTargetSelected)
                    root.removeSelected();
            }
        }
    }

    QueueClearConfirmation {
        id: clearConfirmation
        parent: root
        width: Math.min(root.width - root.contract.spacingUnit * 8, 320)
        x: (root.width - width) / 2
        y: Math.max(root.contract.spacingUnit * 4, (root.height - implicitHeight) / 2)
        contract: root.contract
        queueCount: queueList.count
        fallbackFocusItem: root.actionsRow ? root.actionsRow.clearControl : root
        onConfirmed: root.confirmClear()
    }

    Keys.onEscapePressed: event => {
        if (!root.presenter.selectingAudioFiles)
            root.closeRequested();
        event.accepted = true;
    }

    Connections {
        target: root.presenter
        function onStateChanged() {
            if (!root.addingFilesPending)
                return;
            if (root.presenter.selectingAudioFiles) {
                if (root.addTerminalObserved)
                    root.forgetPendingAdd();
                return;
            }
            root.addTerminalObserved = true;
            const revision = root.addFocusRevision;
            Qt.callLater(function () {
                if (revision === root.addFocusRevision && root.addingFilesPending && !root.presenter.selectingAudioFiles)
                    root.forgetPendingAdd();
            });
        }
        function onAudioFileSelectionFinished(changed) {
            if (!root.addingFilesPending)
                return;
            root.addingFilesPending = false;
            root.addTerminalObserved = false;
            const revision = root.addFocusRevision;
            // ListView follows model insert/move notifications by identity. Do not
            // replace its selection with an index captured before the dialog.
            Qt.callLater(function () {
                if (revision !== root.addFocusRevision || !root.visible || !root.locationFocusRestoreAllowed || root.presenter.exiting || root.presenter.selectingAudioFiles)
                    return;
                const add = root.actionsRow ? root.actionsRow.addControl : null;
                if (add && add.visible && add.enabled)
                    add.forceActiveFocus(Qt.TabFocusReason);
            });
        }
        function onFileLocationFinished(occurrenceId, changed) {
            if (occurrenceId !== root.requestedLocationId)
                return;
            root.requestedLocationId = "";
            Qt.callLater(function () {
                if (!root.visible || !root.locationFocusRestoreAllowed || root.presenter.exiting || root.presenter.locatingOccurrenceId.length > 0 || root.selectedOccurrenceId !== occurrenceId)
                    return;
                queueList.forceActiveFocus(Qt.TabFocusReason);
                const message = root.presenter.fileLocationErrorOccurrenceId === occurrenceId ? queueText.locationFailed : (changed ? queueText.sourceRelocated : "");
                if (message.length > 0) {
                    root.accessibilityAnnouncement = message;
                    queueList.Accessible.announce(message, Accessible.Polite);
                }
            });
        }
        function onQueueFocusRequested(index) {
            const revision = ++root.queueFocusRevision;
            if (rowMenu.visible || root.presenter.selectingAudioFiles || root.addingFilesPending)
                return;
            if (index < 0) {
                root.closeRequested();
                return;
            }
            if (!root.queueFocusRestoreAllowed)
                return;
            // Resolve the notified index now. ListView follows that occurrence
            // through later inserts/moves; deferred focus must not replay an index.
            const occurrenceId = root.selectFocusOccurrence(index);
            if (occurrenceId.length === 0)
                return;
            Qt.callLater(function () {
                if (revision !== root.queueFocusRevision || !root.queueFocusRestoreAllowed || rowMenu.visible)
                    return;
                queueList.forceLayout();
                const selectedRow = queueList.currentItem as QueueOccurrenceDelegate;
                if (!selectedRow || selectedRow.occurrenceId !== occurrenceId)
                    return;
                queueList.positionViewAtIndex(queueList.currentIndex, ListView.Contain);
                queueList.forceActiveFocus(Qt.TabFocusReason);
            });
        }
    }
}
