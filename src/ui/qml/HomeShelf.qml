pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

// A home shelf: a heading with its link, then a horizontal list of cards. With cardsFocusable, Tab enters the
// list at its current card, walks the enabled cards in model order and leaves after the last one; Backtab from
// the first card returns to the link. Without it the cards are read-only and the list stays out of the Tab order.
Item {
    id: root
    required property ShellUiContract contract
    required property string controlId
    property string listObjectName
    property string linkObjectName
    property string title
    property string subtitle
    property string linkText
    property bool linkEnabled: true
    property var model: null
    property Component delegate
    property bool cardsFocusable: false
    readonly property int count: cards.count
    // Room around the list for the focus ring of a card, which is drawn outside the cover.
    readonly property real ringBleed: 2 + contract.focusRingWidth
    // The last item focused outside the list: it tells whether Tab (from the link) or Backtab entered the list.
    property Item focusOrigin: null
    signal linkTriggered

    objectName: controlId
    implicitHeight: header.implicitHeight + 12 + cards.height

    function insideCards(item) {
        while (item && item !== cards)
            item = item.parent;
        return item === cards;
    }
    function focusCard(index, direction) {
        if (!root.visible || !root.cardsFocusable)
            return false;
        for (let row = index; row >= 0 && row < cards.count; row += direction) {
            cards.positionViewAtIndex(row, ListView.Contain);
            cards.forceLayout();
            const item = cards.itemAtIndex(row);
            if (item && item.enabled) {
                cards.currentIndex = row;
                item.forceActiveFocus(direction > 0 ? Qt.TabFocusReason : Qt.BacktabFocusReason);
                return true;
            }
        }
        return false;
    }
    function leaveCardsForward() {
        let target = cards.nextItemInFocusChain(true);
        const visited = [];
        while (target && insideCards(target) && visited.indexOf(target) < 0) {
            visited.push(target);
            target = target.nextItemInFocusChain(true);
        }
        if (target && !insideCards(target))
            target.forceActiveFocus(Qt.TabFocusReason);
    }
    function focusCurrentCard() {
        if (!cards || !cards.activeFocus || !root.visible || !root.cardsFocusable || (cards.currentItem && cards.currentItem.activeFocus))
            return;
        const row = Math.max(0, cards.currentIndex);
        if (focusCard(row, 1) || focusCard(row - 1, -1))
            return;
        // No card can take the focus: pass the list in the direction it was entered.
        if (root.focusOrigin === link || !link.enabled)
            leaveCardsForward();
        else
            link.forceActiveFocus(Qt.BacktabFocusReason);
    }
    // The model row of the focused card (each card makes itself the current item when focused). It is read from
    // the card's place, because a reorder of other rows can leave the current index behind its item.
    function focusedRow() {
        const item = cards.currentItem;
        if (!item || !item.activeFocus)
            return -1;
        cards.forceLayout();
        return cards.indexAt(item.x + item.width / 2, item.y + item.height / 2);
    }

    Connections {
        target: root.Window
        function onActiveFocusItemChanged() {
            const item = root.Window.activeFocusItem;
            if (item && !root.insideCards(item))
                root.focusOrigin = item;
        }
    }

    RowLayout {
        id: header
        width: root.width
        spacing: 16
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 4
            Text {
                objectName: "shelf-title"
                Layout.fillWidth: true
                text: root.title
                font.family: root.contract.headingFontFamily
                font.pixelSize: root.contract.typeSection
                color: root.contract.textPrimaryColor
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
            Text {
                objectName: "shelf-subtitle"
                Layout.fillWidth: true
                text: root.subtitle
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeCaption
                color: root.contract.textSecondaryColor
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
        }
        AuroraButton {
            id: link
            Layout.alignment: Qt.AlignVCenter
            contract: root.contract
            controlId: root.linkObjectName
            kind: AuroraButton.Quiet
            compact: true
            iconName: "arrow"
            text: root.linkText
            enabled: root.linkEnabled
            onClicked: root.linkTriggered()
        }
    }
    // The list does not clip itself, so the focus ring of a card shows; this frame clips it a ring away.
    Item {
        id: frame
        x: -root.ringBleed
        y: header.height + 12 - root.ringBleed
        width: root.width + root.ringBleed * 2
        height: cards.height + root.ringBleed * 2
        clip: true
        ListView {
            id: cards
            objectName: root.listObjectName
            x: root.ringBleed
            y: root.ringBleed
            width: root.width
            height: contentItem.childrenRect.height
            model: root.model
            delegate: root.delegate
            orientation: ListView.Horizontal
            spacing: 18
            boundsBehavior: Flickable.StopAtBounds
            activeFocusOnTab: root.cardsFocusable
            onActiveFocusChanged: {
                if (activeFocus)
                    Qt.callLater(root.focusCurrentCard);
            }
            onCurrentItemChanged: {
                if (activeFocus)
                    Qt.callLater(root.focusCurrentCard);
            }
            Keys.onTabPressed: event => {
                const row = root.focusedRow();
                event.accepted = row >= 0;
                if (row >= 0 && !root.focusCard(row + 1, 1))
                    root.leaveCardsForward();
            }
            Keys.onBacktabPressed: event => {
                const row = root.focusedRow();
                event.accepted = row >= 0;
                if (row >= 0 && !root.focusCard(row - 1, -1))
                    link.forceActiveFocus(Qt.BacktabFocusReason);
            }
            ScrollBar.horizontal: AccessibleScrollBar {
                contract: root.contract
            }
        }
    }
}
