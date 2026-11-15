pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

Dialog {
    id: root
    LyricsText {
        id: lyricsText
    }
    objectName: "lyrics-reader"
    required property ShellUiContract contract
    property var verses: []
    property int activeVerse: -1
    property bool synchronizedLyrics: false
    property string plainText: ""
    property bool copyProtected: false
    property string attribution: ""
    property bool canSeek: false
    property string trackIdentity: ""
    property int textSize: 18
    property int transportInset: 100
    property bool followingPlayback: true
    readonly property int edgeMargin: contract.spacingUnit * 3
    readonly property bool wideWindow: parent !== null && parent.width >= 1040
    signal seekRequested(int timeMs)
    signal attributionRequested
    signal textSizeRequested(int size)

    parent: Overlay.overlay
    width: parent ? Math.min(wideWindow ? 460 : 640, Math.max(0, parent.width - edgeMargin * 2)) : 0
    height: parent ? Math.min(600, Math.max(0, parent.height - transportInset - edgeMargin * 2)) : 0
    x: parent ? (wideWindow ? parent.width - width - edgeMargin : (parent.width - width) / 2) : 0
    y: edgeMargin
    padding: edgeMargin
    modal: false
    focus: true
    popupType: Popup.Item
    closePolicy: Popup.CloseOnEscape

    function revealCurrent() {
        if (opened && synchronizedLyrics && followingPlayback && activeVerse >= 0 && activeVerse < verseList.count)
            verseList.positionViewAtIndex(activeVerse, ListView.Center);
    }

    function resumeFollowing() {
        followingPlayback = true;
        verseList.currentIndex = activeVerse >= 0 ? activeVerse : 0;
        revealCurrent();
    }

    onOpened: {
        resumeFollowing();
        if (synchronizedLyrics)
            verseList.forceActiveFocus(Qt.TabFocusReason);
        else if (copyProtected)
            protectedScroll.forceActiveFocus(Qt.TabFocusReason);
        else
            plainArea.forceActiveFocus(Qt.TabFocusReason);
    }
    onTrackIdentityChanged: {
        if (visible)
            close();
    }
    onVersesChanged: Qt.callLater(revealCurrent)
    onActiveVerseChanged: revealCurrent()
    onTextSizeChanged: Qt.callLater(revealCurrent)

    background: Rectangle {
        color: root.contract.surfaceColor
        radius: root.contract.radiusSmall
        border.width: root.contract.highContrast ? 2 : 1
        border.color: root.contract.borderColor
    }

    contentItem: ColumnLayout {
        spacing: root.contract.spacingUnit
        Accessible.role: Accessible.Dialog
        Accessible.id: root.objectName
        Accessible.name: lyricsText.fullLyrics

        RowLayout {
            Layout.fillWidth: true
            spacing: root.contract.spacingUnit
            Text {
                Layout.fillWidth: true
                text: root.synchronizedLyrics ? lyricsText.fullLyrics : lyricsText.unsynchronized
                textFormat: Text.PlainText
                font.bold: true
                elide: Text.ElideRight
                color: root.contract.textPrimaryColor
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
            AccessibleTransportButton {
                objectName: "lyrics-reader-smaller"
                contract: root.contract
                text: lyricsText.smaller
                accessibleName: lyricsText.smallerName
                enabled: root.textSize > 14
                onClicked: if (enabled)
                    root.textSizeRequested(Math.max(14, root.textSize - 2))
            }
            AccessibleTransportButton {
                objectName: "lyrics-reader-larger"
                contract: root.contract
                text: lyricsText.larger
                accessibleName: lyricsText.largerName
                enabled: root.textSize < 30
                onClicked: if (enabled)
                    root.textSizeRequested(Math.min(30, root.textSize + 2))
            }
            AccessibleTransportButton {
                objectName: "lyrics-reader-close"
                contract: root.contract
                text: lyricsText.close
                accessibleName: lyricsText.closeReader
                onClicked: root.close()
            }
        }

        ListView {
            id: verseList
            objectName: "lyrics-reader-verses"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.synchronizedLyrics
            model: root.verses
            clip: true
            reuseItems: true
            spacing: root.contract.spacingUnit
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: false
            activeFocusOnTab: visible
            Accessible.role: Accessible.List
            Accessible.name: lyricsText.synchronizedVerses
            Accessible.focusable: visible
            Accessible.focused: activeFocus

            function seekToVerse(index) {
                if (!root.opened || !root.canSeek || index < 0 || index >= count)
                    return;
                const time = root.verses[index].timeMs;
                if (Number.isInteger(time) && time >= 0 && time <= 2147483647)
                    root.seekRequested(time);
            }

            function moveCursor(index) {
                root.followingPlayback = false;
                currentIndex = Math.max(0, Math.min(index, count - 1));
                positionViewAtIndex(currentIndex, ListView.Contain);
            }

            onDraggingChanged: if (dragging)
                root.followingPlayback = false
            Keys.onPressed: event => {
                if (count === 0 || (event.modifiers !== Qt.NoModifier && event.modifiers !== Qt.KeypadModifier))
                    return;
                const cursor = root.followingPlayback && root.activeVerse >= 0 ? root.activeVerse : currentIndex;
                switch (event.key) {
                case Qt.Key_Up:
                    moveCursor(cursor - 1);
                    break;
                case Qt.Key_Down:
                    moveCursor(cursor + 1);
                    break;
                case Qt.Key_Home:
                    moveCursor(0);
                    break;
                case Qt.Key_End:
                    moveCursor(count - 1);
                    break;
                case Qt.Key_Return:
                case Qt.Key_Enter:
                    if (!event.isAutoRepeat)
                        seekToVerse(cursor);
                    break;
                default:
                    return;
                }
                event.accepted = true;
            }

            WheelHandler {
                target: null
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: event => {
                    if (verseList.contentHeight > verseList.height)
                        root.followingPlayback = false;
                    event.accepted = false;
                }
            }
            ScrollBar.vertical: AccessibleScrollBar {
                contract: root.contract
                onPressedChanged: if (pressed)
                    root.followingPlayback = false
            }
            delegate: ItemDelegate {
                id: verse
                required property int index
                required property var modelData
                readonly property bool playing: index === root.activeVerse
                readonly property bool cursorFocused: verseList.activeFocus && ListView.isCurrentItem
                objectName: "lyrics-reader-verse-" + index
                width: verseList.width - root.contract.spacingUnit * 2
                padding: root.contract.spacingUnit * 2
                implicitHeight: Math.max(root.contract.controlTargetMinimum, implicitContentHeight + topPadding + bottomPadding)
                focusPolicy: Qt.NoFocus
                activeFocusOnTab: false
                hoverEnabled: root.canSeek
                Accessible.role: Accessible.ListItem
                Accessible.name: modelData.text
                Accessible.focusable: true
                Accessible.focused: cursorFocused
                Accessible.onPressAction: verseList.seekToVerse(verse.index)
                onClicked: {
                    verseList.currentIndex = index;
                    verseList.forceActiveFocus(Qt.MouseFocusReason);
                    verseList.seekToVerse(index);
                }
                contentItem: Text {
                    text: verse.modelData.text
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: root.textSize
                    font.bold: verse.playing
                    color: root.contract.textPrimaryColor
                    Accessible.ignored: true
                }
                background: Rectangle {
                    color: verse.playing || verse.hovered ? root.contract.surfaceRaisedColor : "transparent"
                    radius: root.contract.radiusSmall
                    border.width: verse.cursorFocused ? 3 : (verse.playing ? 2 : 0)
                    border.color: verse.cursorFocused ? root.contract.focusColor : root.contract.accentColor
                }
            }
        }

        ScrollView {
            id: plainScroll
            objectName: "lyrics-reader-plain-scroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.synchronizedLyrics && !root.copyProtected
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical: AccessibleScrollBar {
                contract: root.contract
            }
            TextArea {
                id: plainArea
                objectName: "lyrics-reader-plain"
                width: plainScroll.availableWidth
                text: plainScroll.visible ? root.plainText : ""
                textFormat: Text.PlainText
                readOnly: true
                selectByMouse: true
                activeFocusOnTab: plainScroll.visible
                wrapMode: TextEdit.Wrap
                font.pixelSize: root.textSize
                color: root.contract.textPrimaryColor
                selectionColor: root.contract.accentColor
                selectedTextColor: root.contract.accentTextColor
                Accessible.id: objectName
                Accessible.name: lyricsText.unsynchronized
                background: Rectangle {
                    color: "transparent"
                    radius: root.contract.radiusSmall
                    border.width: plainArea.activeFocus ? 3 : 0
                    border.color: root.contract.focusColor
                }
            }
        }

        ScrollView {
            id: protectedScroll
            objectName: "lyrics-reader-protected-scroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.synchronizedLyrics && root.copyProtected
            activeFocusOnTab: visible
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical: AccessibleScrollBar {
                contract: root.contract
            }
            Accessible.id: objectName
            Accessible.name: lyricsText.unsynchronized
            Accessible.focusable: visible
            Accessible.focused: activeFocus
            Keys.onUpPressed: event => {
                ScrollBar.vertical.decrease();
                event.accepted = true;
            }
            Keys.onDownPressed: event => {
                ScrollBar.vertical.increase();
                event.accepted = true;
            }
            background: Rectangle {
                color: "transparent"
                radius: root.contract.radiusSmall
                border.width: protectedScroll.activeFocus ? 3 : 0
                border.color: root.contract.focusColor
            }
            Text {
                objectName: "lyrics-reader-protected-text"
                width: protectedScroll.availableWidth
                text: protectedScroll.visible ? root.plainText : ""
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                font.pixelSize: root.textSize
                color: root.contract.textPrimaryColor
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }

        AccessibleTransportButton {
            objectName: "lyrics-reader-follow"
            Layout.alignment: Qt.AlignRight
            contract: root.contract
            text: lyricsText.followCurrentVerse
            visible: root.synchronizedLyrics
            enabled: !root.followingPlayback && root.activeVerse >= 0
            onClicked: {
                if (!enabled)
                    return;
                root.resumeFollowing();
                verseList.forceActiveFocus(Qt.TabFocusReason);
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.copyProtected
            ScrollView {
                id: creditScroll
                objectName: "lyrics-reader-attribution"
                Layout.fillWidth: true
                Layout.preferredHeight: root.contract.controlTargetMinimum
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical: AccessibleScrollBar {
                    contract: root.contract
                }
                Text {
                    width: creditScroll.availableWidth
                    text: root.attribution
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: root.contract.textSecondaryColor
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
            }
            AccessibleTransportButton {
                contract: root.contract
                text: lyricsText.lyricFind
                accessibleName: lyricsText.openLyricFind
                onClicked: root.attributionRequested()
            }
        }
    }
}
