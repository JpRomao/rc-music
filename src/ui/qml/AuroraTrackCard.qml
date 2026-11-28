import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

// A card of a home shelf: the cover, a title of up to two lines and one subtitle line. An interactive card
// is a button (click, Enter and the screen reader play it); a read-only one is static text and takes no focus.
// The shelf walks the cards with Tab; the card only keeps the list's current index on itself.
ItemDelegate {
    id: root
    required property ShellUiContract contract
    property string title
    property string subtitle
    property bool subtitleWarning: false
    property url artworkSource
    property bool playing: false
    property bool interactive: true
    property string accessibleName
    // Shows the play, enqueue and more actions over the cover: on hover or highlight, or when the host says so.
    property bool actionsShown: cardHover.hovered || highlighted
    property bool unavailableBadge: false
    // False while the player cannot open anything: the play button and the activation do nothing.
    property bool playEnabled: true
    // A click plays at once (shelves); otherwise it only selects and the double click plays (grid).
    property bool activateOnClick: true
    signal activated
    signal selected
    signal playRequested
    signal enqueueRequested
    signal moreRequested(Item opener)

    readonly property int coverSize: 150
    readonly property int titleGap: 10
    readonly property int subtitleGap: 4
    // Room for two title lines, so every card of a shelf has the same height.
    readonly property real titleSlot: Math.ceil(titleMetrics.height * 2)

    width: coverSize
    implicitWidth: coverSize
    implicitHeight: coverSize + titleGap + titleSlot + subtitleGap + Math.ceil(captionMetrics.height)
    padding: 0
    hoverEnabled: interactive
    focusPolicy: interactive ? Qt.ClickFocus : Qt.NoFocus
    Accessible.role: interactive ? Accessible.Button : Accessible.StaticText
    Accessible.name: accessibleName
    Accessible.focusable: interactive && enabled
    Accessible.focused: activeFocus
    Accessible.onPressAction: root.activate()

    function activate() {
        if (root.interactive && root.enabled && !root.unavailableBadge && root.playEnabled)
            root.activated();
    }

    HoverHandler {
        id: cardHover
        enabled: root.interactive
    }

    onClicked: {
        if (root.activateOnClick)
            activate();
        else if (root.interactive && root.enabled)
            root.selected();
    }
    onDoubleClicked: {
        if (!root.activateOnClick)
            activate();
    }
    Keys.onReturnPressed: event => {
        root.activate();
        event.accepted = true;
    }
    Keys.onEnterPressed: event => {
        root.activate();
        event.accepted = true;
    }
    onActiveFocusChanged: {
        const view = root.ListView.view;
        if (!activeFocus || !view)
            return;
        const row = view.indexAt(root.x + root.width / 2, root.y + root.height / 2);
        if (row >= 0) {
            view.currentIndex = row;
            view.positionViewAtIndex(row, ListView.Contain);
        }
    }

    FontMetrics {
        id: titleMetrics
        font.family: root.contract.bodyFontFamily
        font.pixelSize: root.contract.typeBody
        font.weight: Font.DemiBold
    }
    FontMetrics {
        id: captionMetrics
        font.family: root.contract.bodyFontFamily
        font.pixelSize: root.contract.typeCaption
    }

    background: null
    contentItem: Item {
        TrackArtwork {
            id: artwork
            objectName: "shelf-card-artwork"
            width: root.coverSize
            height: root.coverSize
            radius: root.contract.radiusMedium
            contract: root.contract
            source: root.artworkSource
        }
        AuroraChip {
            objectName: "shelf-card-playing"
            x: 8
            y: artwork.height - height - 8
            visible: root.playing
            contract: root.contract
            tone: AuroraChip.Accent
            iconName: "waveform"
            text: labels.playingBadge
            Accessible.ignored: true
        }
        AuroraChip {
            objectName: "shelf-card-unavailable"
            x: 8
            y: artwork.height - height - 8
            visible: root.unavailableBadge
            contract: root.contract
            tone: AuroraChip.Warning
            iconName: "warning"
            text: libraryLabels.unavailable
            Accessible.ignored: true
        }
        Rectangle {
            objectName: "shelf-card-highlight"
            width: artwork.width
            height: artwork.height
            radius: root.contract.radiusMedium
            color: "transparent"
            border.width: 2
            border.color: root.contract.accentColor
            visible: root.highlighted
        }
        AuroraIconButton {
            id: playButton
            objectName: "shelf-card-play"
            x: artwork.width - 8 - drawnSize - (width - drawnSize) / 2
            y: artwork.height - 8 - drawnSize - (height - drawnSize) / 2
            visible: root.actionsShown
            enabled: !root.unavailableBadge && root.playEnabled
            contract: root.contract
            controlId: "shelf-card-play"
            iconName: "play"
            accent: true
            round: true
            activeFocusOnTab: false
            focusPolicy: Qt.NoFocus
            text: libraryLabels.playNowName.arg(root.title)
            onClicked: root.playRequested()
        }
        AuroraIconButton {
            objectName: "shelf-card-enqueue"
            x: artwork.width - 54 - drawnSize - (width - drawnSize) / 2
            y: playButton.y
            visible: root.actionsShown && !root.unavailableBadge
            contract: root.contract
            controlId: "shelf-card-enqueue"
            iconName: "plus"
            raised: true
            round: true
            activeFocusOnTab: false
            focusPolicy: Qt.NoFocus
            text: libraryLabels.enqueue.arg(root.title)
            onClicked: root.enqueueRequested()
        }
        AuroraIconButton {
            id: moreButton
            objectName: "shelf-card-more"
            x: artwork.width - 8 - drawnSize - (width - drawnSize) / 2
            y: 8 - (height - drawnSize) / 2
            visible: root.actionsShown && !root.unavailableBadge
            contract: root.contract
            controlId: "shelf-card-more"
            iconName: "more"
            raised: true
            round: true
            drawnSize: 32
            activeFocusOnTab: false
            focusPolicy: Qt.NoFocus
            text: controlsLabels.moreActionsFor.arg(root.title)
            onClicked: root.moreRequested(moreButton)
        }
        AuroraFocusRing {
            target: root
            contract: root.contract
            areaWidth: artwork.width
            areaHeight: artwork.height
            targetRadius: root.contract.radiusMedium
        }
        Text {
            id: titleText
            objectName: "shelf-card-title"
            y: artwork.height + root.titleGap
            width: parent.width
            text: root.title
            font.family: root.contract.bodyFontFamily
            font.pixelSize: root.contract.typeBody
            font.weight: Font.DemiBold
            color: root.contract.textPrimaryColor
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            textFormat: Text.PlainText
            Accessible.ignored: true
        }
        Text {
            objectName: "shelf-card-subtitle"
            y: titleText.y + root.titleSlot + root.subtitleGap
            width: parent.width
            text: root.subtitle
            font.family: root.contract.bodyFontFamily
            font.pixelSize: root.contract.typeCaption
            color: root.subtitleWarning ? root.contract.warningColor : root.contract.textSecondaryColor
            maximumLineCount: 1
            elide: Text.ElideRight
            textFormat: Text.PlainText
            Accessible.ignored: true
        }
    }

    NavigationText {
        id: labels
    }
    LibraryText {
        id: libraryLabels
    }
    ControlsText {
        id: controlsLabels
    }
}
