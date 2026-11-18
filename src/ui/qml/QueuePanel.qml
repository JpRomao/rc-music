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
    signal closeRequested
    signal playRequested(string occurrenceId)
    signal addLyricVariantRequested(string occurrenceId, string displayTitle)

    QueueText {
        id: queueText
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
        if (!addButton.enabled)
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

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.contract.spacingUnit * 4
        spacing: root.contract.spacingUnit * 3

        Text {
            objectName: "queue-eyebrow"
            visible: root.overlayMode
            text: queueText.drawerEyebrow
            textFormat: Text.PlainText
            color: root.contract.textSecondaryColor
            font.family: root.contract.bodyFontFamily
            font.pixelSize: root.contract.typeEyebrow
            font.letterSpacing: 1.2
            Layout.bottomMargin: -root.contract.spacingUnit * 2
        }
        Text {
            id: queueTitle
            objectName: "queue-title"
            text: root.contract.queueTitle
            color: root.contract.textPrimaryColor
            font: root.overlayMode ? Qt.font({
                family: root.contract.headingFontFamily,
                pixelSize: root.contract.typeHeading
            }) : Qt.font({
                bold: true
            })
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }

        RowLayout {
            id: queueActions
            objectName: "queue-actions"
            Layout.fillWidth: true
            spacing: root.contract.spacingUnit

            QueueActionButton {
                id: addButton
                controlId: "queue-add"
                iconName: root.overlayMode ? "plus" : ""
                quiet: root.overlayMode
                accessibleName: queueText.add
                text: "+"
                contract: root.contract
                enabled: !root.presenter.exiting && !root.presenter.usesFrozenFixture && !root.presenter.selectingAudioFiles && root.presenter.locatingOccurrenceId.length === 0
                onClicked: root.addFiles()
            }
            QueueActionButton {
                id: moveUpButton
                controlId: "queue-move-up"
                iconName: root.overlayMode ? "arrow-up" : ""
                quiet: root.overlayMode
                accessibleName: queueText.moveUp
                shortcutHint: queueText.shortcutMoveUp
                text: "↑"
                contract: root.contract
                enabled: !root.presenter.exiting && !root.presenter.usesFrozenFixture && queueList.currentIndex > 0
                onClicked: root.moveSelected(-1)
            }
            QueueActionButton {
                id: moveDownButton
                controlId: "queue-move-down"
                iconName: root.overlayMode ? "arrow-down" : ""
                quiet: root.overlayMode
                accessibleName: queueText.moveDown
                shortcutHint: queueText.shortcutMoveDown
                text: "↓"
                contract: root.contract
                enabled: !root.presenter.exiting && !root.presenter.usesFrozenFixture && queueList.currentIndex >= 0 && queueList.currentIndex < queueList.count - 1
                onClicked: root.moveSelected(1)
            }
            QueueActionButton {
                id: removeButton
                controlId: "queue-remove"
                iconName: root.overlayMode ? "minus" : ""
                quiet: root.overlayMode
                accessibleName: queueText.remove
                shortcutHint: queueText.shortcutRemove
                text: "−"
                contract: root.contract
                enabled: !root.presenter.exiting && !root.presenter.usesFrozenFixture && queueList.currentIndex >= 0
                onClicked: root.removeSelected()
            }
            Item {
                Layout.fillWidth: true
            }
            QueueActionButton {
                id: clearButton
                controlId: "queue-clear"
                iconName: root.overlayMode ? "close" : ""
                quiet: root.overlayMode
                accessibleName: queueText.clear
                text: "×"
                contract: root.contract
                enabled: !root.roomGuest && !root.presenter.exiting && !root.presenter.usesFrozenFixture && queueList.count > 0
                onClicked: root.requestClear()
            }
        }

        RowLayout {
            objectName: "queue-modes"
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

        ListView {
            id: queueList
            objectName: "queue-list"
            readonly property bool focusIndicatorVisible: activeFocus && enabled
            visible: !root.roomGuest
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
                height: 64
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
                    Rectangle {
                        objectName: "queue-row-divider"
                        visible: root.overlayMode
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: root.contract.highContrast ? root.contract.borderColor : root.contract.dividerColor
                        Accessible.ignored: true
                    }
                    radius: root.contract.radiusSmall
                    border.width: row.ListView.isCurrentItem ? (queueList.activeFocus ? 3 : (root.contract.highContrast ? 2 : 0)) : (root.contract.highContrast && row.hovered ? 1 : 0)
                    border.color: row.contrastHighlighted ? root.contract.accentTextColor : root.contract.focusColor
                }

                contentItem: RowLayout {
                    spacing: root.contract.spacingUnit * 2

                    Text {
                        // Aurora numbers the queue; the legacy page marks the playing row.
                        objectName: "queue-row-number"
                        text: root.overlayMode ? String(row.orderIndex + 1) : (row.occurrenceId === root.presenter.currentOccurrenceId ? "▶" : "")
                        color: root.overlayMode ? row.secondaryColor : row.primaryColor
                        font: root.overlayMode ? Qt.font({
                            family: root.contract.bodyFontFamily,
                            pixelSize: root.contract.typeCaption
                        }) : root.contract.applicationFont
                        Layout.preferredWidth: root.overlayMode ? 24 : 16
                        Accessible.ignored: true
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            objectName: "queue-row-title"
                            text: row.title.length > 0 ? row.title : row.fileName
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: root.overlayMode && !root.contract.highContrast && row.occurrenceId === root.presenter.currentOccurrenceId ? root.contract.accentColor : row.primaryColor
                            Layout.fillWidth: true
                            Accessible.ignored: true
                        }
                        Text {
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
                            color: row.secondaryColor
                            visible: row.locating || row.locationFailed || row.retrying || !row.available
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
            Accessible.focusable: true
            Accessible.focused: activeFocus

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
        fallbackFocusItem: clearButton
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
                if (addButton.visible && addButton.enabled)
                    addButton.forceActiveFocus(Qt.TabFocusReason);
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
