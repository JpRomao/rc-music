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
    property bool overlayMode: false
    property alias listView: queueList
    property string selectedOccurrenceId: ""
    property string selectedTitle: ""
    property bool selectedAvailable: false
    property string accessibilityAnnouncement: ""
    readonly property string observedCurrentOccurrenceId: presenter.currentOccurrenceId
    readonly property bool clearConfirmationOpen: clearConfirmation.opened
    signal closeRequested
    signal playRequested(string occurrenceId)

    QueueText {
        id: queueText
    }

    function formatDuration(milliseconds) {
        if (typeof milliseconds !== "number" || !isFinite(milliseconds))
            return root.contract.notInformed;
        const totalSeconds = Math.floor(milliseconds / 1000);
        const minutes = Math.floor(totalSeconds / 60);
        const seconds = totalSeconds % 60;
        return String(minutes).padStart(2, "0") + ":" + String(seconds).padStart(2, "0");
    }
    function rowAccessibleName(occurrenceId, title, fileName, artist, durationMs, available) {
        const displayTitle = title.length > 0 ? title : fileName;
        const parts = [occurrenceId, displayTitle];
        if (artist.length > 0)
            parts.push(artist);
        parts.push(root.formatDuration(durationMs));
        if (!available)
            parts.push(root.contract.errorTitle);
        return parts.join(". ");
    }
    function syncCurrent() {
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
        syncCurrent();
        queueList.forceActiveFocus(Qt.TabFocusReason);
    }
    function focusIndex(index) {
        if (queueList.count === 0 || index < 0) {
            queueList.currentIndex = -1;
            root.selectedOccurrenceId = "";
            root.selectedTitle = "";
            root.selectedAvailable = false;
            queueList.forceActiveFocus(Qt.TabFocusReason);
            return;
        }
        const targetIndex = Math.min(index, queueList.count - 1);
        queueList.currentIndex = targetIndex;
        queueList.positionViewAtIndex(targetIndex, ListView.Contain);
        queueList.forceLayout();
        queueList.forceActiveFocus(Qt.TabFocusReason);
    }
    function playRow(row, focusReason) {
        if (!row || !row.available)
            return;
        queueList.currentIndex = row.index;
        queueList.forceActiveFocus(focusReason);
        root.playRequested(row.occurrenceId);
    }
    function addFiles() {
        const selectedIndex = queueList.currentIndex;
        const hadCurrentTrack = root.presenter.hasCurrentTrack;
        if (!root.presenter.addAudio())
            return;
        Qt.callLater(function () {
            root.focusIndex(hadCurrentTrack && selectedIndex >= 0 ? selectedIndex : root.presenter.currentQueueIndex);
        });
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
    function openRowMenu(row) {
        if (!row)
            return;
        queueList.currentIndex = row.index;
        rowMenu.popup(row, 0, row.height);
    }
    function requestClear() {
        if (queueList.count > 0)
            clearConfirmation.open();
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
        if (visible)
            Qt.callLater(root.syncCurrent);
    }

    Rectangle {
        anchors.fill: parent
        color: root.contract.surfaceColor
        radius: root.contract.radiusMedium
        border.width: 1
        border.color: root.contract.borderColor
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.contract.spacingUnit * 4
        spacing: root.contract.spacingUnit * 3

        Text {
            id: queueTitle
            objectName: "queue-title"
            text: root.contract.queueTitle
            color: root.contract.textPrimaryColor
            font.bold: true
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
                accessibleName: queueText.add
                text: "+"
                contract: root.contract
                enabled: !root.presenter.exiting && !root.presenter.usesFrozenFixture
                onClicked: root.addFiles()
            }
            QueueActionButton {
                id: moveUpButton
                controlId: "queue-move-up"
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
                accessibleName: queueText.clear
                text: "×"
                contract: root.contract
                enabled: !root.presenter.exiting && !root.presenter.usesFrozenFixture && queueList.count > 0
                onClicked: root.requestClear()
            }
        }

        ListView {
            id: queueList
            objectName: "queue-list"
            readonly property bool focusIndicatorVisible: activeFocus && enabled
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
            Accessible.focusable: enabled
            Accessible.focused: activeFocus
            Keys.priority: Keys.BeforeItem
            Keys.onPressed: event => {
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

            delegate: ItemDelegate {
                id: row
                required property int index
                required property int orderIndex
                required property string occurrenceId
                required property string fileName
                required property string title
                required property string artist
                required property var durationMs
                required property bool available
                required property var errorCode
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
                Accessible.name: root.rowAccessibleName(occurrenceId, title, fileName, artist, durationMs, available)
                Accessible.description: available || errorCode === undefined ? "" : String(errorCode)
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
                    color: root.contract.highContrast ? (row.contrastHighlighted ? root.contract.accentColor : root.contract.surfaceColor) : (row.ListView.isCurrentItem ? Qt.alpha(root.contract.accentColor, 0.12) : (row.hovered ? Qt.alpha(root.contract.focusColor, 0.08) : "transparent"))
                    radius: root.contract.radiusSmall
                    border.width: row.ListView.isCurrentItem ? (queueList.activeFocus ? 3 : (root.contract.highContrast ? 2 : 0)) : (root.contract.highContrast && row.hovered ? 1 : 0)
                    border.color: row.contrastHighlighted ? root.contract.accentTextColor : root.contract.focusColor
                }

                contentItem: RowLayout {
                    spacing: root.contract.spacingUnit * 2

                    Text {
                        text: row.occurrenceId === root.presenter.currentOccurrenceId ? "▶" : ""
                        color: row.primaryColor
                        Layout.preferredWidth: 16
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
                            color: row.primaryColor
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
                            text: root.contract.errorTitle
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: row.secondaryColor
                            visible: !row.available
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

            ScrollBar.vertical: ScrollBar {
                id: scrollBar
                objectName: "queue-scrollbar"
                activeFocusOnTab: false
                contentItem: Rectangle {
                    implicitWidth: 6
                    implicitHeight: 6
                    radius: width / 2
                    color: root.contract.highContrast ? root.contract.textPrimaryColor : (scrollBar.pressed ? root.contract.accentColor : root.contract.borderColor)
                    opacity: root.contract.highContrast ? 1 : (scrollBar.policy === ScrollBar.AlwaysOn || (scrollBar.active && scrollBar.size < 1) ? 0.75 : 0)
                    Behavior on opacity {
                        enabled: !root.contract.highContrast
                        NumberAnimation {
                            duration: root.contract.feedbackTransitionMs
                        }
                    }
                }
                background: Rectangle {
                    visible: root.contract.highContrast
                    color: root.contract.surfaceColor
                    border.width: 1
                    border.color: root.contract.textPrimaryColor
                }
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
    }

    Menu {
        id: rowMenu
        objectName: "queue-row-menu"
        MenuItem {
            objectName: "queue-menu-play-now"
            text: queueText.playNow
            enabled: root.selectedOccurrenceId.length > 0 && root.selectedAvailable
            onTriggered: root.playRow(queueList.currentItem, Qt.TabFocusReason)
        }
        MenuSeparator {}
        MenuItem {
            objectName: "queue-menu-move-up"
            text: queueText.moveUp
            enabled: !root.presenter.usesFrozenFixture && queueList.currentIndex > 0
            onTriggered: root.moveSelected(-1)
        }
        MenuItem {
            objectName: "queue-menu-move-down"
            text: queueText.moveDown
            enabled: !root.presenter.usesFrozenFixture && queueList.currentIndex >= 0 && queueList.currentIndex < queueList.count - 1
            onTriggered: root.moveSelected(1)
        }
        MenuItem {
            objectName: "queue-menu-remove"
            text: queueText.remove
            enabled: !root.presenter.usesFrozenFixture && queueList.currentIndex >= 0
            onTriggered: root.removeSelected()
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
        root.closeRequested();
        event.accepted = true;
    }

    Shortcut {
        sequence: "Space"
        context: Qt.WindowShortcut
        autoRepeat: false
        enabled: root.visible && queueList.activeFocus
        onActivated: root.playRow(queueList.currentItem, Qt.TabFocusReason)
    }

    Connections {
        target: root.presenter
        function onQueueFocusRequested(index) {
            if (index < 0) {
                root.closeRequested();
                return;
            }
            if (root.visible)
                Qt.callLater(function () {
                    root.focusIndex(index);
                });
        }
    }
}
