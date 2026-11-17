pragma ComponentBehavior: Bound

import QtQuick
import MusicPlayer.M0

// One Tab stop for the bundled avatars: arrows move, Space or Enter chooses,
// and a click chooses directly. The chosen avatar keeps a ring; focus adds an outer ring.
GridView {
    id: root
    required property ShellUiContract contract
    property int selectedIndex: -1
    property int avatarCount: 20
    property string optionObjectName: ""
    property int columns: 10
    readonly property int cellSize: contract.controlTargetPreferred + contract.spacingUnit * 2
    signal avatarChosen(int index)

    function choose(index) {
        if (index < 0 || index >= count)
            return;
        currentIndex = index;
        avatarChosen(index);
    }
    function syncCurrent() {
        if (selectedIndex >= 0 && selectedIndex < count)
            currentIndex = selectedIndex;
    }

    implicitWidth: columns * cellSize
    implicitHeight: Math.ceil(avatarCount / columns) * cellSize
    cellWidth: cellSize
    cellHeight: cellSize
    model: avatarCount
    interactive: false
    clip: false
    keyNavigationEnabled: true
    keyNavigationWraps: false
    activeFocusOnTab: true
    highlightFollowsCurrentItem: false
    Accessible.role: Accessible.List
    Accessible.name: labels.avatar
    Accessible.focusable: true
    Component.onCompleted: syncCurrent()
    onSelectedIndexChanged: syncCurrent()
    onCountChanged: syncCurrent()
    onActiveFocusChanged: {
        if (activeFocus && currentIndex < 0)
            currentIndex = Math.max(0, selectedIndex);
    }
    Keys.onSpacePressed: event => {
        root.choose(root.currentIndex);
        event.accepted = true;
    }
    Keys.onReturnPressed: event => {
        root.choose(root.currentIndex);
        event.accepted = true;
    }
    Keys.onEnterPressed: event => {
        root.choose(root.currentIndex);
        event.accepted = true;
    }

    ProfileText {
        id: labels
    }

    delegate: Item {
        id: cell
        required property int index
        readonly property bool selected: index === root.selectedIndex
        readonly property bool focused: GridView.isCurrentItem && root.activeFocus
        objectName: root.optionObjectName + "avatar-option-" + index
        width: root.cellWidth
        height: root.cellHeight
        Accessible.role: Accessible.RadioButton
        Accessible.name: labels.avatarOption.replace("{number}", index + 1)
        Accessible.checkable: true
        Accessible.checked: selected
        Accessible.focusable: true
        Accessible.focused: focused
        Accessible.onPressAction: root.choose(index)

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: root.contract.radiusMedium
            color: "transparent"
            border.width: cell.focused ? 3 : 0
            border.color: root.contract.focusColor
            Accessible.ignored: true
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: 5
            radius: root.contract.radiusSmall + 2
            color: cell.selected ? root.contract.selectionColor : "transparent"
            border.width: cell.selected ? 2 : 0
            border.color: root.contract.accentColor
            Accessible.ignored: true
        }
        ListenerAvatar {
            anchors.centerIn: parent
            size: root.cellSize - 16
            source: "qrc:/rc_music/avatars/avatar-" + String(cell.index + 1).padStart(2, "0") + ".svg"
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                root.forceActiveFocus(Qt.MouseFocusReason);
                root.choose(cell.index);
            }
        }
    }
}
