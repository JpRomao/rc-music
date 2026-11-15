pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// The form is one contentItem. Production keeps it in the workspace;
// the optional legacy host moves that same item, never instantiates a copy.
// A Control would reclaim contentItem from the Popup during polish.
FocusScope {
    id: root
    property Item contentItem: null
    property Item background: null
    property real padding: 0
    property Item previousFocusItem: null
    property Item nextFocusItem: null
    property Item lastFocusItem: null
    property bool embedded: false
    property bool modal: !embedded
    property bool dim: true
    property int popupType: Popup.Item
    property int closePolicy: Popup.CloseOnEscape
    property Transition enter: Transition {}
    property Transition exit: Transition {}
    property bool blockingUiOpen: false
    readonly property Dialog legacyDialog: legacyLoader.item as Dialog
    readonly property bool opened: embedded ? visible : legacyDialog !== null && legacyDialog.opened
    signal aboutToShow
    signal aboutToHide
    signal surfaceOpened
    signal closed
    visible: false

    function layoutSurface() {
        if (!embedded)
            return;
        if (background !== null) {
            background.parent = root;
            background.x = 0;
            background.y = 0;
            background.width = width;
            background.height = height;
            background.z = -1;
        }
        if (contentItem !== null) {
            contentItem.parent = inlineViewport.contentItem;
            contentItem.x = 0;
            contentItem.y = 0;
            contentItem.width = inlineViewport.width;
            contentItem.height = inlineViewport.contentHeight;
        }
    }
    function revealFocusedItem(item) {
        if (!embedded || !visible || !item)
            return;
        let ancestor = item;
        while (ancestor && ancestor !== contentItem)
            ancestor = ancestor.parent;
        if (!ancestor)
            return;
        if (item.activeFocus)
            lastFocusItem = item;
        const point = item.mapToItem(inlineViewport.contentItem, 0, 0);
        if (point.y < inlineViewport.contentY)
            inlineViewport.contentY = Math.max(0, point.y);
        else if (point.y + item.height > inlineViewport.contentY + inlineViewport.height)
            inlineViewport.contentY = Math.max(0, Math.min(inlineViewport.contentHeight - inlineViewport.height, point.y + item.height - inlineViewport.height));
    }
    onWidthChanged: layoutSurface()
    onHeightChanged: layoutSurface()
    onPaddingChanged: layoutSurface()
    onContentItemChanged: layoutSurface()
    onBackgroundChanged: layoutSurface()
    onEmbeddedChanged: layoutSurface()
    Component.onCompleted: layoutSurface()

    Flickable {
        id: inlineViewport
        objectName: "page-viewport"
        visible: root.embedded
        anchors.fill: parent
        anchors.margins: root.padding
        clip: true
        contentWidth: width
        contentHeight: root.contentItem !== null ? Math.max(height, root.contentItem.implicitHeight) : height
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        onWidthChanged: root.layoutSurface()
        onContentHeightChanged: root.layoutSurface()
        ScrollBar.vertical: ScrollBar {}
    }
    Connections {
        target: root.Window.window
        function onActiveFocusItemChanged() {
            root.revealFocusedItem(root.Window.window.activeFocusItem);
        }
    }

    function open() {
        if (!enabled || opened)
            return;
        if (embedded) {
            aboutToShow();
            visible = true;
            surfaceOpened();
        } else if (legacyDialog !== null) {
            visible = true;
            legacyDialog.open();
        }
    }
    function close() {
        if (embedded) {
            if (!visible)
                return;
            aboutToHide();
            visible = false;
            closed();
        } else if (legacyDialog !== null) {
            legacyDialog.close();
        }
    }
    Keys.onEscapePressed: event => {
        if (root.embedded && !root.blockingUiOpen && (root.closePolicy & Popup.CloseOnEscape)) {
            root.close();
            event.accepted = true;
        }
    }
    Loader {
        id: legacyLoader
        active: !root.embedded
        sourceComponent: Component {
            Dialog {
                objectName: "legacy-page-dialog"
                parent: root.parent
                x: root.x
                y: root.y
                width: root.width
                height: root.height
                padding: root.padding
                modal: root.modal
                dim: root.dim
                focus: true
                popupType: root.popupType
                closePolicy: root.closePolicy
                contentItem: root.contentItem
                background: root.background
                enter: root.enter
                exit: root.exit
                onAboutToShow: root.aboutToShow()
                onAboutToHide: root.aboutToHide()
                onOpened: root.surfaceOpened()
                onClosed: {
                    root.visible = false;
                    root.closed();
                }
            }
        }
    }
}
