import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

// A track row of a list: number, cover with title and artist, album, duration and the actions that appear on
// hover or on the current row of a focused list. The list owns the focus and the keyboard (the row takes none),
// so the action buttons stay out of the Tab order and every action is also in the row menu.
ItemDelegate {
    id: root
    required property ShellUiContract contract
    property int number: 0
    property string title
    property string subtitle
    property string album
    property string durationText
    property url artworkSource
    property bool available: true
    // False while the player cannot open anything: the play button and the double click do nothing.
    property bool playEnabled: true
    // The track the player has loaded; `playing` adds the wave in place of the number.
    property bool current: false
    property bool playing: false
    property bool compact: false
    // Whether Localizar can be used right now (false while a file chooser or another locate is open).
    property bool locateEnabled: true
    property string accessibleName
    property string description
    // The current row of the focused list: the view keeps the keyboard focus, so the screen reader follows this.
    property bool cursorFocused: false
    signal activated
    signal playRequested
    signal enqueueRequested
    signal moreRequested(Item opener)
    signal locateRequested
    signal contextMenuRequested(Item opener)

    readonly property int columnSpacing: columns.spacing
    readonly property int sidePadding: columns.sidePadding
    readonly property int numberWidth: columns.numberWidth
    readonly property int durationWidth: columns.durationWidth
    readonly property int actionsWidth: columns.actionsWidth
    readonly property int coverSize: compact ? 36 : 40
    readonly property bool actionsShown: rowHover.hovered || highlighted
    readonly property real titleColumnWidth: columns.titleWidth
    readonly property real albumColumnWidth: columns.albumWidth
    readonly property real titleColumnX: columns.titleX
    readonly property real albumColumnX: columns.albumX
    readonly property real durationColumnX: columns.durationX
    readonly property real actionsColumnX: columns.actionsX
    // In high contrast the current row of a focused list wears the system selection: its ground and its ink.
    // A row whose file is gone keeps the window ground: its disabled play button has no ground of its own.
    readonly property bool selected: contract.highContrast && highlighted && available
    readonly property color secondaryInk: {
        if (selected)
            return contract.accentTextColor;
        return actionsShown ? contract.textPrimaryColor : contract.textSecondaryColor;
    }
    readonly property color titleInk: {
        if (selected)
            return contract.accentTextColor;
        if (current)
            return contract.accentColor;
        return available ? contract.textPrimaryColor : secondaryInk;
    }

    implicitWidth: 900
    implicitHeight: compact ? 52 : 56
    padding: 0
    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    Accessible.role: Accessible.ListItem
    Accessible.name: accessibleName
    Accessible.description: description
    Accessible.focusable: true
    Accessible.focused: cursorFocused
    Accessible.onPressAction: {
        if (root.available && root.playEnabled)
            root.activated();
    }

    onDoubleClicked: {
        if (root.available && root.playEnabled)
            root.activated();
    }

    HoverHandler {
        id: rowHover
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: root.contextMenuRequested(root)
    }

    background: Rectangle {
        objectName: "track-row-background"
        radius: root.contract.radiusControl
        color: root.selected ? root.contract.selectionColor : (root.actionsShown ? root.contract.hoverColor : "transparent")
        border.width: root.contract.highContrast && root.actionsShown ? 2 : 0
        border.color: root.contract.textPrimaryColor
    }

    contentItem: Item {
        Item {
            id: numberCell
            x: root.sidePadding
            width: root.numberWidth
            height: parent.height
            Text {
                objectName: "track-row-number"
                anchors.fill: parent
                visible: !root.actionsShown && !(root.current && root.playing)
                text: root.number
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeBody
                color: root.contract.textSecondaryColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                Accessible.ignored: true
            }
            ShellIcon {
                objectName: "track-row-wave"
                anchors.centerIn: parent
                visible: !root.actionsShown && root.current && root.playing
                iconName: "waveform"
                color: root.contract.accentColor
            }
            AuroraIconButton {
                objectName: "track-row-play"
                anchors.centerIn: parent
                visible: root.actionsShown
                enabled: root.available && root.playEnabled
                contract: root.contract
                controlId: "track-row-play"
                iconName: "play"
                iconSize: 16
                accent: true
                round: true
                drawnSize: 32
                activeFocusOnTab: false
                focusPolicy: Qt.NoFocus
                text: labels.playNowName.arg(root.title)
                onClicked: root.playRequested()
            }
        }

        TrackArtwork {
            id: cover
            objectName: "track-row-artwork"
            x: root.titleColumnX
            y: (parent.height - height) / 2
            width: root.coverSize
            height: root.coverSize
            radius: 6
            contract: root.contract
            source: root.artworkSource
        }
        Item {
            id: textColumn
            readonly property real textX: cover.x + cover.width + root.columnSpacing
            x: textX
            width: root.titleColumnX + root.titleColumnWidth - textX
            height: parent.height
            Text {
                id: titleText
                objectName: "track-row-title"
                y: parent.height / 2 - height
                width: parent.width
                text: root.title
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeBody
                font.weight: Font.DemiBold
                color: root.titleInk
                elide: Text.ElideRight
                maximumLineCount: 1
                textFormat: Text.PlainText
                Accessible.ignored: true
            }
            Row {
                y: parent.height / 2 + 1
                width: parent.width
                spacing: 4
                Text {
                    objectName: "track-row-subtitle"
                    width: Math.min(implicitWidth, Math.max(0, parent.width - (root.available ? 0 : separator.width + unavailableText.width + 2 * parent.spacing)))
                    text: root.subtitle
                    font.family: root.contract.bodyFontFamily
                    font.pixelSize: root.contract.typeCaption
                    color: root.secondaryInk
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    textFormat: Text.PlainText
                    Accessible.ignored: true
                }
                Text {
                    id: separator
                    visible: !root.available
                    text: labels.subtitleSeparator
                    font.family: root.contract.bodyFontFamily
                    font.pixelSize: root.contract.typeCaption
                    color: root.secondaryInk
                    Accessible.ignored: true
                }
                Text {
                    id: unavailableText
                    objectName: "track-row-unavailable"
                    visible: !root.available
                    text: labels.unavailable
                    font.family: root.contract.bodyFontFamily
                    font.pixelSize: root.contract.typeCaption
                    font.weight: Font.DemiBold
                    color: root.contract.warningColor
                    Accessible.ignored: true
                }
            }
        }

        Text {
            objectName: "track-row-album"
            x: root.albumColumnX
            width: root.albumColumnWidth
            height: parent.height
            visible: !root.compact
            text: root.album
            font.family: root.contract.bodyFontFamily
            font.pixelSize: root.contract.typeBody
            color: root.secondaryInk
            elide: Text.ElideRight
            maximumLineCount: 1
            verticalAlignment: Text.AlignVCenter
            textFormat: Text.PlainText
            Accessible.ignored: true
        }
        Text {
            objectName: "track-row-duration"
            x: root.durationColumnX
            width: root.durationWidth
            height: parent.height
            text: root.durationText
            font.family: root.contract.bodyFontFamily
            font.pixelSize: root.contract.typeBody
            color: root.secondaryInk
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            maximumLineCount: 1
            Accessible.ignored: true
        }

        Item {
            id: actionsCell
            x: root.actionsColumnX
            width: root.actionsWidth
            height: parent.height
            AuroraIconButton {
                objectName: "track-row-enqueue"
                x: moreButton.x - width - 4
                anchors.verticalCenter: parent.verticalCenter
                visible: root.actionsShown && root.available
                contract: root.contract
                controlId: "track-row-enqueue"
                iconName: "plus"
                drawnSize: 36
                tonal: root.selected
                activeFocusOnTab: false
                focusPolicy: Qt.NoFocus
                text: labels.enqueue.arg(root.title)
                onClicked: root.enqueueRequested()
            }
            AuroraIconButton {
                id: moreButton
                objectName: "track-row-more"
                x: parent.width - width
                anchors.verticalCenter: parent.verticalCenter
                visible: root.actionsShown && root.available
                contract: root.contract
                controlId: "track-row-more"
                iconName: "more"
                drawnSize: 36
                tonal: true
                activeFocusOnTab: false
                focusPolicy: Qt.NoFocus
                text: controlsLabels.moreActionsFor.arg(root.title)
                onClicked: root.moreRequested(moreButton)
            }
            AuroraButton {
                objectName: "track-row-locate"
                x: parent.width - width
                anchors.verticalCenter: parent.verticalCenter
                visible: !root.available
                enabled: root.locateEnabled
                contract: root.contract
                controlId: "track-row-locate"
                kind: AuroraButton.Quiet
                compact: true
                leftPadding: 8
                iconName: "folder-open"
                activeFocusOnTab: false
                focusPolicy: Qt.NoFocus
                text: labels.locateShort
                onClicked: root.locateRequested()
            }
        }
    }

    AuroraFocusRing {
        target: root
        contract: root.contract
        inside: true
        targetRadius: root.contract.radiusControl
        shown: root.highlighted
    }

    AuroraTrackColumns {
        id: columns
        width: root.width
        compact: root.compact
    }
    LibraryText {
        id: labels
    }
    ControlsText {
        id: controlsLabels
    }
}
