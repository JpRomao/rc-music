import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

// A track card of a home shelf or of the library grid: the cover, a title of up to two lines and one subtitle
// line. The card is a button (click, Enter and the screen reader play it). The shelf walks the cards with Tab;
// the card only keeps the list's current index on itself.
ItemDelegate {
    id: root
    required property ShellUiContract contract
    property string title
    property string subtitle
    property bool subtitleWarning: false
    property url artworkSource
    property bool playing: false
    property string accessibleName
    // Shows the play, enqueue and more actions over the cover: on hover or highlight, or when the host says so.
    property bool actionsShown: cardHover.hovered || highlighted
    property bool unavailableBadge: false
    // False while the player cannot open anything: the play button and the activation do nothing.
    property bool playEnabled: true
    // A click plays at once (shelves); otherwise it only selects and the double click plays (grid).
    property bool activateOnClick: true
    // The current card of a focused view that keeps the keyboard focus itself: the screen reader follows this.
    property bool cursorFocused: false
    signal activated
    signal selected
    signal playRequested
    signal enqueueRequested
    signal moreRequested(Item opener)

    readonly property int coverSize: 150
    readonly property int titleGap: 10
    readonly property int subtitleGap: 4
    // Room for two title lines, so every card of a shelf has the same height; the subtitle follows the rendered title and the free room stays below it.
    readonly property real titleSlot: Math.ceil(titleMetrics.height * 2)

    width: coverSize
    implicitWidth: coverSize
    implicitHeight: coverSize + titleGap + titleSlot + subtitleGap + Math.ceil(captionMetrics.height)
    padding: 0
    // An unavailable card is dimmed as in the mockup (60%): the cover and the texts, not the chip, which keeps its
    // contrast; the reason is in the accessible description.
    readonly property real dimmedOpacity: unavailableBadge ? 0.6 : 1
    hoverEnabled: true
    focusPolicy: Qt.ClickFocus
    Accessible.role: Accessible.Button
    Accessible.name: accessibleName
    Accessible.focusable: enabled
    Accessible.focused: activeFocus || cursorFocused
    Accessible.onPressAction: root.activate()

    function activate() {
        if (root.enabled && !root.unavailableBadge && root.playEnabled)
            root.activated();
    }

    HoverHandler {
        id: cardHover
    }

    onClicked: {
        if (root.activateOnClick)
            activate();
        else if (root.enabled)
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
            opacity: root.dimmedOpacity
            contract: root.contract
            source: root.artworkSource
        }
        AuroraChip {
            objectName: "shelf-card-playing"
            x: 8
            y: artwork.height - height - 8
            visible: root.playing && !root.actionsShown
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
        // The focused card, or the current card of a view that keeps the focus itself (the grid), as the list row.
        AuroraFocusRing {
            target: root
            contract: root.contract
            areaWidth: artwork.width
            areaHeight: artwork.height
            targetRadius: root.contract.radiusMedium
            shown: root.activeFocus || root.highlighted
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
            opacity: root.dimmedOpacity
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            textFormat: Text.PlainText
            Accessible.ignored: true
        }
        Text {
            objectName: "shelf-card-subtitle"
            y: titleText.y + titleText.height + root.subtitleGap
            width: parent.width
            text: root.subtitle
            font.family: root.contract.bodyFontFamily
            font.pixelSize: root.contract.typeCaption
            color: root.subtitleWarning ? root.contract.warningColor : root.contract.textSecondaryColor
            opacity: root.dimmedOpacity
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
