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
    TransportText {
        id: transportText
    }
    ControlsText {
        id: controlsText
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
    // Aurora: the drawer with its own header, toolbar and empty state. Without it this is the M0 reader.
    property bool editorial: false
    property int drawerWidth: 400
    property string eyebrow
    // Not Dialog.title: the Basic style draws that one as a second, unstyled band above the drawer header.
    property string drawerTitle
    property string subtitle
    property string statusText
    property bool versionsAvailable: false
    property bool reviewAvailable: false
    property bool searchAvailable: false
    property bool romajiAvailable: false
    property bool addVersionAvailable: false
    property string romajiDescription
    // True while the drawer closes because another track took over: its opener is not coming back.
    property bool trackChanged: false
    readonly property bool hasText: synchronizedLyrics || plainText.length > 0
    readonly property Item closeControl: headerLoader.item ? (headerLoader.item as AuroraDrawerHeader).closeButton : null
    readonly property Item versionsControl: versionsButton
    readonly property Item searchControl: searchButton
    readonly property Item moreControl: moreButton
    signal seekRequested(int timeMs)
    signal attributionRequested
    signal textSizeRequested(int size)
    signal versionsRequested(Item opener)
    signal searchRequested(Item opener)
    signal romajiRequested(Item opener)
    signal addVersionRequested(Item opener)
    // Tab left the last control (true) or Shift+Tab the first (false).
    signal focusLeft(bool forward)

    parent: Overlay.overlay
    // Aurora side drawer: flush to the right edge, from the top of the window to the player.
    width: parent ? (editorial ? Math.min(drawerWidth, parent.width) : Math.min(420, Math.max(0, parent.width - 64))) : 0
    height: parent ? Math.max(0, parent.height - transportInset) : 0
    x: parent ? parent.width - width : 0
    y: 0
    padding: contract.spacingUnit * 6
    leftPadding: editorial ? 22 : padding
    rightPadding: editorial ? 22 : padding
    topPadding: editorial ? 20 : padding
    modal: false
    focus: true
    popupType: Popup.Item
    // A popup that closes on Escape swallows the window shortcuts, and the Aurora drawer must let Ctrl+L through:
    // there it handles Escape itself.
    closePolicy: editorial ? Popup.NoAutoClose : Popup.CloseOnEscape

    function revealCurrent() {
        if (opened && synchronizedLyrics && followingPlayback && activeVerse >= 0 && activeVerse < verseList.count)
            verseList.positionViewAtIndex(activeVerse, ListView.Center);
    }

    function resumeFollowing() {
        followingPlayback = true;
        verseList.currentIndex = activeVerse >= 0 ? activeVerse : 0;
        revealCurrent();
    }

    // The Aurora drawer does not trap Tab. A zero-size relay on each end passes the focus out when Tab leaves the
    // last control or Shift+Tab the first; reached from the other side it hands the focus on into the drawer.
    // KeyNavigation is reciprocal, so the way out is a relay, never a link to an item outside.
    function relayFocus(atEnd, reason) {
        const backward = reason === Qt.BacktabFocusReason;
        if (atEnd === backward) {
            const inner = atEnd ? focusRelayAfter.nextItemInFocusChain(false) : focusRelayBefore.nextItemInFocusChain(true);
            const target = (!inner || inner === focusRelayBefore || inner === focusRelayAfter) ? closeControl : inner;
            if (target)
                target.forceActiveFocus(reason);
        } else {
            focusLeft(atEnd);
        }
    }

    // The first control worth focusing: the close button, so Escape and Tab start from the drawer's top.
    function focusInside() {
        if (closeControl)
            closeControl.forceActiveFocus(Qt.TabFocusReason);
    }

    onOpened: {
        trackChanged = false;
        resumeFollowing();
        if (synchronizedLyrics)
            verseList.forceActiveFocus(Qt.TabFocusReason);
        else if (copyProtected)
            protectedScroll.forceActiveFocus(Qt.TabFocusReason);
        else if (!editorial || plainText.length > 0)
            plainArea.forceActiveFocus(Qt.TabFocusReason);
        else {
            const entry = [emptyState.primaryButton, emptyState.secondaryButton, closeControl].find(item => item && item.visible && item.enabled);
            if (entry)
                entry.forceActiveFocus(Qt.TabFocusReason);
        }
    }
    onTrackIdentityChanged: {
        if (visible) {
            trackChanged = true;
            close();
        }
    }
    onVersesChanged: Qt.callLater(revealCurrent)
    onActiveVerseChanged: revealCurrent()
    onTextSizeChanged: Qt.callLater(revealCurrent)

    background: Rectangle {
        color: root.contract.surfaceColor
        radius: 0
        border.width: 0
        Rectangle {
            // Only the inner edge is drawn, like the queue drawer.
            width: root.contract.highContrast ? 2 : 1
            height: parent.height
            color: root.contract.highContrast ? root.contract.borderColor : root.contract.dividerColor
            Accessible.ignored: true
        }
    }

    contentItem: Item {
        Keys.onEscapePressed: event => {
            // The M0 reader leaves Escape to its close policy.
            event.accepted = root.editorial;
            if (root.editorial)
                root.close();
        }
        Accessible.role: Accessible.Dialog
        Accessible.id: root.objectName
        Accessible.name: lyricsText.fullLyrics

        // The M0 verses keep the default text font: this hidden text is where to read it.
        Text {
            id: defaultFont
            visible: false
            Accessible.ignored: true
        }

        Control {
            id: focusRelayBefore
            objectName: "lyrics-drawer-focus-relay-before"
            visible: root.editorial
            width: 0
            height: 0
            focusPolicy: Qt.TabFocus
            Accessible.ignored: true
            onActiveFocusChanged: {
                if (activeFocus)
                    root.relayFocus(false, focusReason);
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: root.contract.spacingUnit

            Loader {
                id: headerLoader
                active: root.editorial
                visible: active
                Layout.fillWidth: true
                sourceComponent: AuroraDrawerHeader {
                    contract: root.contract
                    controlId: "lyrics-drawer"
                    closeText: lyricsText.closeDrawer
                    eyebrow: root.eyebrow
                    title: root.drawerTitle
                    subtitle: root.subtitle
                    onCloseRequested: root.close()
                }
            }

            RowLayout {
                visible: root.editorial
                spacing: 6
                Layout.fillWidth: true

                AuroraButton {
                    id: versionsButton
                    contract: root.contract
                    controlId: "lyrics-drawer-versions"
                    kind: AuroraButton.Tonal
                    compact: true
                    text: root.reviewAvailable ? lyricsText.reviewChanges : lyricsText.versions
                    Accessible.name: root.reviewAvailable ? lyricsText.reviewChangesName : lyricsText.versionsTitle
                    visible: root.versionsAvailable
                    onClicked: root.versionsRequested(versionsButton)
                }
                AuroraButton {
                    id: searchButton
                    contract: root.contract
                    controlId: "lyrics-drawer-search"
                    kind: AuroraButton.Tonal
                    compact: true
                    iconName: "search"
                    text: lyricsText.search
                    enabled: root.searchAvailable
                    onClicked: root.searchRequested(searchButton)
                }
                AuroraIconButton {
                    id: moreButton
                    contract: root.contract
                    controlId: "lyrics-drawer-more"
                    iconName: "more"
                    tonal: true
                    text: controlsText.moreActions
                    visible: root.romajiAvailable
                    onClicked: moreMenu.popup(moreButton, 0, moreButton.height)
                }
                Item {
                    Layout.fillWidth: true
                }
                RowLayout {
                    spacing: 2
                    AuroraIconButton {
                        contract: root.contract
                        controlId: "lyrics-drawer-smaller"
                        iconName: "minus"
                        text: lyricsText.smallerName
                        enabled: root.textSize > 14
                        onClicked: root.textSizeRequested(Math.max(14, root.textSize - 2))
                    }
                    Text {
                        objectName: "lyrics-drawer-size-label"
                        text: lyricsText.textSizeLabel
                        color: root.contract.textSecondaryColor
                        font.family: root.contract.bodyFontFamily
                        font.pixelSize: root.contract.typeBody
                        Layout.alignment: Qt.AlignVCenter
                        Accessible.ignored: true
                    }
                    AuroraIconButton {
                        contract: root.contract
                        controlId: "lyrics-drawer-larger"
                        iconName: "plus"
                        text: lyricsText.largerName
                        enabled: root.textSize < 30
                        onClicked: root.textSizeRequested(Math.min(30, root.textSize + 2))
                    }
                }
            }

            Text {
                id: drawerStatus
                objectName: "lyrics-drawer-status"
                visible: root.editorial && text.length > 0
                text: root.statusText
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                color: root.contract.textSecondaryColor
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeBody
                Layout.fillWidth: true
                Accessible.role: Accessible.StaticText
                Accessible.name: text
                property string lastAnnouncement: ""
                function updateAnnouncement() {
                    const message = visible ? text : "";
                    if (message === lastAnnouncement)
                        return;
                    lastAnnouncement = message;
                    if (message.length > 0)
                        Accessible.announce(message, Accessible.Polite);
                }
                onTextChanged: Qt.callLater(updateAnnouncement)
                onVisibleChanged: Qt.callLater(updateAnnouncement)
            }

            RowLayout {
                visible: !root.editorial
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
                activeFocusOnTab: true
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

                // The opening reveals the current verse before the list has its final height (the drawer's first
                // layout, a resize): while following playback, the Aurora drawer reveals it again once the height is
                // known. The M0 reader keeps its frozen behavior.
                onHeightChanged: if (root.editorial && root.followingPlayback)
                    root.revealCurrent()
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
                    readonly property bool past: root.activeVerse >= 0 && index < root.activeVerse
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
                        font.family: root.editorial ? root.contract.headingFontFamily : defaultFont.font.family
                        font.pixelSize: root.editorial && verse.playing ? root.textSize + 4 : root.textSize
                        font.weight: verse.playing ? (root.editorial ? Font.DemiBold : Font.Bold) : Font.Normal
                        color: root.editorial ? (verse.playing ? (root.contract.highContrast ? root.contract.textPrimaryColor : root.contract.accentColor) : (verse.past ? root.contract.textSecondaryColor : root.contract.textPrimaryColor)) : root.contract.textPrimaryColor
                        Accessible.ignored: true
                    }
                    background: Rectangle {
                        color: !root.editorial && (verse.playing || verse.hovered) ? root.contract.surfaceRaisedColor : "transparent"
                        radius: root.contract.radiusSmall
                        border.width: verse.cursorFocused ? 3 : ((!root.editorial || root.contract.highContrast) && verse.playing ? 2 : 0)
                        border.color: verse.cursorFocused ? root.contract.focusColor : root.contract.accentColor
                    }
                }
            }

            ScrollView {
                id: plainScroll
                objectName: "lyrics-reader-plain-scroll"
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: !root.synchronizedLyrics && !root.copyProtected && (!root.editorial || root.plainText.length > 0)
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
                    activeFocusOnTab: true
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
                activeFocusOnTab: true
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

            Item {
                visible: root.editorial && !root.hasText
                Layout.fillWidth: true
                Layout.fillHeight: true

                AuroraEmptyState {
                    id: emptyState
                    contract: root.contract
                    controlId: "lyrics-drawer-empty"
                    iconName: "lyrics"
                    title: transportText.lyricsUnavailable
                    hint: lyricsText.emptyHint
                    primaryActionText: root.searchAvailable ? lyricsText.search : ""
                    secondaryActionText: root.addVersionAvailable ? lyricsText.addVersion : ""
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    onPrimaryTriggered: root.searchRequested(emptyState.primaryButton)
                    onSecondaryTriggered: root.addVersionRequested(emptyState.secondaryButton)
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
                        objectName: "lyrics-reader-credit-text"
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
                    objectName: "lyrics-reader-lyricfind"
                    contract: root.contract
                    text: lyricsText.lyricFind
                    accessibleName: lyricsText.openLyricFind
                    onClicked: root.attributionRequested()
                }
            }
        }

        Control {
            id: focusRelayAfter
            objectName: "lyrics-drawer-focus-relay-after"
            visible: root.editorial
            width: 0
            height: 0
            focusPolicy: Qt.TabFocus
            Accessible.ignored: true
            onActiveFocusChanged: {
                if (activeFocus)
                    root.relayFocus(true, focusReason);
            }
        }

        AuroraMenu {
            id: moreMenu
            objectName: "lyrics-drawer-more-menu"
            contract: root.contract
            AuroraMenuItem {
                contract: root.contract
                controlId: "lyrics-drawer-romaji"
                text: lyricsText.generateRomaji
                Accessible.description: root.romajiDescription
                onTriggered: root.romajiRequested(moreButton)
            }
        }
    }
}
