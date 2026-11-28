pragma ComponentBehavior: Bound

import QtQuick
import MusicPlayer.M0

// The menu of a library track: play now, enqueue, add to a playlist, locate (only for a missing file) and remove.
AuroraMenu {
    id: root

    property string recordId
    property string recordTitle
    property bool available: true
    property bool canLocate: false
    // False while the player cannot open anything (a file chooser is open).
    property bool playEnabled: true
    // Saved playlists, each with `id` and `name`.
    property var playlists: []

    // Opened from the keyboard: the focus starts on the first usable item.
    property bool keyboardOpened: false

    signal playRequested
    signal enqueueRequested
    signal playlistChosen(string id, string name)
    signal locateRequested
    signal removeRequested

    // The playlists submenu is a popup of its own: the page counts it as open UI too.
    readonly property bool submenuOpen: playlistMenu.visible

    implicitWidth: 248

    onOpened: {
        if (!keyboardOpened)
            return;
        for (let i = 0; i < count; ++i) {
            const entry = itemAt(i);
            if (entry && entry.enabled && entry.visible && entry.height > 0 && entry.focusPolicy !== Qt.NoFocus) {
                currentIndex = i;
                entry.forceActiveFocus(Qt.TabFocusReason);
                break;
            }
        }
    }
    onClosed: keyboardOpened = false

    LibraryText {
        id: labels
    }

    AuroraMenuItem {
        contract: root.contract
        controlId: "library-menu-play"
        iconName: "play"
        text: labels.playNow
        shortcutText: labels.playNowShortcut
        enabled: root.available && root.playEnabled
        Accessible.description: root.available ? labels.playNowDescription : labels.unavailableReason
        onTriggered: root.playRequested()
    }
    AuroraMenuItem {
        contract: root.contract
        controlId: "library-menu-enqueue"
        iconName: "plus"
        text: labels.enqueueAction
        enabled: root.available
        Accessible.description: root.available ? labels.enqueueDescription : labels.unavailableReason
        onTriggered: root.enqueueRequested()
    }
    AuroraMenu {
        id: playlistMenu
        objectName: "library-menu-playlists"
        contract: root.contract
        iconName: "playlist"
        title: labels.addToPlaylistAction
        itemDescription: labels.unavailableDescription(root.available)
        enabled: root.available

        Instantiator {
            model: root.playlists
            delegate: AuroraMenuItem {
                id: choice
                required property var modelData
                contract: root.contract
                controlId: "library-playlist-choice-" + choice.modelData.id
                text: choice.modelData.name
                onTriggered: root.playlistChosen(choice.modelData.id, choice.modelData.name)
            }
            onObjectAdded: (index, object) => playlistMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => playlistMenu.removeItem(object)
        }
        AuroraMenuItem {
            contract: root.contract
            controlId: "library-playlist-none"
            text: labels.noPlaylists
            visible: root.playlists.length === 0
            enabled: false
            height: visible ? implicitHeight : 0
        }
    }
    AuroraMenuItem {
        id: locateItem
        contract: root.contract
        controlId: "library-menu-locate"
        iconName: "folder-open"
        text: labels.locateAction
        visible: !root.available && root.canLocate
        enabled: visible
        height: visible ? implicitHeight : 0
        onTriggered: root.locateRequested()
    }
    AuroraMenuSeparator {
        contract: root.contract
    }
    AuroraMenuItem {
        contract: root.contract
        controlId: "library-menu-remove"
        iconName: "trash"
        text: labels.removeAction
        danger: true
        onTriggered: root.removeRequested()
    }
}
