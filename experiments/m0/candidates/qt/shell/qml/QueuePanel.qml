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
    signal closeRequested

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
    function focusCurrent() {
        if (queueList.count === 0)
            return;
        const targetIndex = root.presenter.currentQueueIndex >= 0 ? root.presenter.currentQueueIndex : 0;
        queueList.currentIndex = targetIndex;
        queueList.positionViewAtIndex(targetIndex, ListView.Center);
        queueList.forceLayout();
        queueList.forceActiveFocus(Qt.TabFocusReason);
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
                Accessible.onPressAction: {
                    queueList.currentIndex = row.index;
                    queueList.forceActiveFocus(Qt.TabFocusReason);
                }
                onClicked: {
                    queueList.currentIndex = row.index;
                    queueList.forceActiveFocus(Qt.MouseFocusReason);
                }

                background: Rectangle {
                    color: row.ListView.isCurrentItem ? Qt.alpha(root.contract.accentColor, 0.12) : (row.hovered ? Qt.alpha(root.contract.focusColor, 0.08) : "transparent")
                    radius: root.contract.radiusSmall
                    border.width: row.ListView.isCurrentItem && queueList.activeFocus ? 3 : 0
                    border.color: root.contract.focusColor
                }

                contentItem: RowLayout {
                    spacing: root.contract.spacingUnit * 2

                    Text {
                        text: row.occurrenceId === root.presenter.currentOccurrenceId ? "▶" : ""
                        color: root.contract.textPrimaryColor
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
                            color: root.contract.textPrimaryColor
                            Layout.fillWidth: true
                            Accessible.ignored: true
                        }
                        Text {
                            text: row.artist
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: root.contract.textSecondaryColor
                            Layout.fillWidth: true
                            Accessible.ignored: true
                        }
                        Text {
                            objectName: "queue-row-status"
                            text: root.contract.errorTitle
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: root.contract.textSecondaryColor
                            visible: !row.available
                            Layout.fillWidth: true
                            Accessible.ignored: true
                        }
                    }
                    Text {
                        text: root.formatDuration(row.durationMs)
                        color: root.contract.textSecondaryColor
                        Accessible.ignored: true
                    }
                }
            }

            ScrollBar.vertical: ScrollBar {
                activeFocusOnTab: false
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

    Keys.onEscapePressed: event => {
        root.closeRequested();
        event.accepted = true;
    }

    Connections {
        target: root.presenter
        function onStateChanged() {
            if (root.visible)
                Qt.callLater(root.focusCurrent);
        }
    }
}
