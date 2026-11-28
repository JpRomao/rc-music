pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

// ADR-0019: the permanent library page, opening in List view. Only confirmed records appear; removing touches
// the catalog alone. The header, the bar and the feedback line stay fixed: the list (or the grid) fills the rest
// of the page and is the only thing that scrolls.
PageSurface {
    id: root
    NavigationText {
        id: navigationLabels
    }
    LibraryText {
        id: libraryText
    }
    objectName: "library-panel"
    required property ShellUiContract contract
    property var library: null
    property var playlists: null
    // The narrow window: a title-only header, no columns header, no album column and rows of 52.
    property bool compact: false
    // Whether the player is playing now: the wave and the badge of the current track need it.
    property bool playbackPlaying: false
    // False while a file chooser or a locate is open, or the app is leaving.
    property bool openingEnabled: true
    signal queueRequested(Item opener)
    signal openAudioRequested(Item opener)
    readonly property var playlistChoices: playlists !== null && playlists.ready ? playlists.playlists : []
    property string removeId: ""
    property string removeTitle: ""
    // The last confirmation or failure of an action, and whether it was a failure.
    property string notice: ""
    property bool noticeProblem: false
    // The see-queue button shows only after a track was added to the queue.
    property bool seeQueueShown: false
    // After a locate request, the binding's own outcome is shown.
    property bool showStatus: false
    // ADR-0019: the library opens in List; Covers is the alternative.
    property bool showCovers: false
    // The menu closes before the removal dialog opens, so the focus goes back to the page first.
    property bool removePending: false
    // After a confirmed removal the focus goes where the row's place went; it waits for the catalog's own change.
    property bool removeFocusArmed: false
    property int removeFocusTotal: 0
    readonly property bool ready: library !== null && library.ready
    readonly property int total: library !== null ? library.count : 0
    readonly property int visibleTotal: library !== null ? library.visibleCount : 0
    readonly property bool filtered: library !== null && library.filterText.trim().length > 0
    readonly property string artworkBase: library !== null ? library.artworkBase : ""
    readonly property string playingId: library !== null ? library.playingRecordId : ""
    readonly property bool retryNeeded: library !== null && library.statusProblem && library.statusText.length > 0 && (!ready || library.canRetrySave)
    readonly property bool loadingShown: library !== null && !ready && !retryNeeded && library.statusText.length > 0
    readonly property bool statusFeedback: showStatus && !retryNeeded && library !== null && library.statusText.length > 0
    readonly property string feedbackText: statusFeedback ? library.statusText : notice
    readonly property bool feedbackProblem: statusFeedback ? library.statusProblem : noticeProblem
    readonly property Item activeView: showCovers ? grid : list
    embedded: true
    padding: 0
    focus: true
    onTotalChanged: {
        if (removeFocusArmed && !removeConfirmation.visible)
            Qt.callLater(root.settleFocusAfterRemove);
    }
    blockingUiOpen: removeConfirmation.visible || trackMenu.visible || trackMenu.submenuOpen
    onSurfaceOpened: {
        if (ready && total > 0)
            filterField.input.forceActiveFocus(Qt.TabFocusReason);
        else if (retryNotice.visible)
            retryNotice.primaryButton.forceActiveFocus(Qt.TabFocusReason);
    }
    onClosed: {
        removeConfirmation.close();
        trackMenu.close();
        removePending = false;
        removeFocusArmed = false;
        notice = "";
        seeQueueShown = false;
        if (library !== null)
            library.cancelLocate();
    }

    function selectItem(view, index) {
        view.currentIndex = index;
        view.forceActiveFocus(Qt.MouseFocusReason);
    }
    // A missing file or a busy file chooser cannot play: nothing happens and no failure is shown for it.
    function playNow(recordId, available) {
        if (!openingEnabled || !available)
            return;
        showStatus = false;
        seeQueueShown = false;
        const played = library.playNow(recordId);
        notice = played ? "" : libraryText.playNowFailed;
        noticeProblem = !played;
    }
    function enqueue(recordId, title) {
        showStatus = false;
        const added = library.enqueue(recordId);
        notice = added ? libraryText.enqueued.arg(title) : libraryText.enqueueFailed;
        noticeProblem = !added;
        seeQueueShown = added;
    }
    function addToPlaylist(recordId, title, playlistId, playlistName) {
        showStatus = false;
        seeQueueShown = false;
        const added = library.addToPlaylist(recordId, playlistId);
        notice = added ? navigationLabels.fillTwo(libraryText.addedToPlaylist, title, playlistName) : libraryText.addToPlaylistFailed;
        noticeProblem = !added;
    }
    function locate(recordId) {
        notice = "";
        seeQueueShown = false;
        showStatus = library.locate(recordId);
    }
    function showTrackMenu(view, index, recordId, title, available, opener, x, y) {
        view.currentIndex = index;
        trackMenu.recordId = recordId;
        trackMenu.recordTitle = title;
        trackMenu.available = available;
        trackMenu.popup(opener, x, y);
    }
    // A menu opened from a button hangs from the button's right edge, so it stays inside the window.
    function showMenuFromButton(view, index, recordId, title, available, opener) {
        showTrackMenu(view, index, recordId, title, available, opener, opener.width - trackMenu.implicitWidth, opener.height);
    }
    // The current row of the list or the grid: the keyboard acts on it.
    function currentEntry() {
        const item = showCovers ? grid.currentItem : list.currentItem;
        // The delegates declare `model` themselves; the view only knows them as items.
        // qmllint disable missing-property
        return item !== null && item.model !== undefined && item.model.recordId !== undefined ? item : null;
        // qmllint enable missing-property
    }
    function playCurrent(event) {
        event.accepted = true;
        const item = currentEntry();
        if (item !== null)
            playNow(item.model.recordId, item.model.available);
    }
    function openMenuForCurrent(event) {
        event.accepted = true;
        const item = currentEntry();
        if (item === null)
            return;
        trackMenu.keyboardOpened = true;
        const entry = item.model;
        if (activeView === list)
            showTrackMenu(list, list.currentIndex, entry.recordId, entry.title, entry.available, item, item.actionsColumnX + item.actionsWidth - trackMenu.implicitWidth, item.height);
        else
            showTrackMenu(grid, grid.currentIndex, entry.recordId, entry.title, entry.available, item, item.width / 2, item.height / 2);
    }
    function moveCurrent(view, index) {
        view.currentIndex = Math.max(0, Math.min(view.count - 1, index));
        view.positionViewAtIndex(view.currentIndex, view === list ? ListView.Contain : GridView.Contain);
    }
    // What the views do not do by themselves: the ends, whole screens and the keyboard form of the context menu.
    function viewKeyPressed(view, event) {
        const entry = view.currentItem;
        const rowHeight = entry !== null ? entry.height : 1;
        const columns = view === grid ? Math.max(1, Math.floor((grid.width - grid.leftMargin) / grid.cellWidth)) : 1;
        const screen = Math.max(1, Math.floor(view.height / (view === grid ? grid.cellHeight : rowHeight))) * columns;
        event.accepted = true;
        if (event.key === Qt.Key_Home)
            moveCurrent(view, 0);
        else if (event.key === Qt.Key_End)
            moveCurrent(view, view.count - 1);
        else if (event.key === Qt.Key_PageDown)
            moveCurrent(view, view.currentIndex + screen);
        else if (event.key === Qt.Key_PageUp)
            moveCurrent(view, view.currentIndex - screen);
        else if (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))
            openMenuForCurrent(event);
        else
            event.accepted = false;
    }
    // Where the focus goes once the confirmation closes: the row that took the removed row's place, the filter when
    // the filtered list emptied, or the empty state's action when the catalog did.
    function settleFocusAfterRemove() {
        if (removeFocusArmed && total !== removeFocusTotal)
            removeFocusArmed = false;
        if (!visible)
            return;
        const emptyState = libraryEmpty.item as AuroraEmptyState;
        if (ready && total === 0 && emptyState !== null)
            emptyState.primaryButton.forceActiveFocus(Qt.OtherFocusReason);
        else if (ready && total > 0 && visibleTotal === 0)
            filterField.input.forceActiveFocus(Qt.OtherFocusReason);
        else
            activeView.forceActiveFocus(Qt.OtherFocusReason);
    }
    function requestRemove() {
        removeId = trackMenu.recordId;
        removeTitle = trackMenu.recordTitle;
        removePending = true;
    }
    function retryFromNotice() {
        if (library.canRetrySave)
            library.retrySave();
        else
            library.retryLoad();
    }

    background: Rectangle {
        color: root.contract.canvasColor
    }
    contentItem: ColumnLayout {
        spacing: root.contract.spacingUnit * 2
        Accessible.role: Accessible.Pane
        Accessible.name: navigationLabels.library

        PageHeader {
            Layout.fillWidth: true
            contract: root.contract
            editorial: true
            titleOnly: root.compact
            eyebrow: navigationLabels.libraryEyebrow
            title: navigationLabels.library
            description: navigationLabels.libraryDescription
            legacyTitleSize: root.contract.typeSection
        }
        Text {
            objectName: "library-status"
            Layout.fillWidth: true
            visible: root.loadingShown
            text: root.library !== null ? root.library.statusText : ""
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: root.contract.textSecondaryColor
            font.family: root.contract.bodyFontFamily
            font.pixelSize: root.contract.typeBody
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.ready && root.total > 0
            spacing: root.contract.spacingUnit * 2
            AuroraTextField {
                id: filterField
                Layout.fillWidth: true
                Layout.maximumWidth: 420
                Layout.alignment: Qt.AlignVCenter
                contract: root.contract
                controlId: "library-filter"
                label: libraryText.filterName
                leadingIconName: "search"
                placeholderText: libraryText.filterPlaceholder
                text: root.library !== null ? root.library.filterText : ""
                Connections {
                    target: filterField.input
                    function onTextEdited() {
                        root.library.filterText = filterField.text;
                    }
                }
            }
            Text {
                objectName: "library-count"
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                text: libraryText.countLine(root.visibleTotal, root.total, root.filtered)
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: root.contract.textSecondaryColor
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeCaption
            }
            AuroraSegmented {
                id: viewChoice
                Layout.alignment: Qt.AlignVCenter
                contract: root.contract
                controlId: "library-view"
                label: libraryText.viewChoice
                options: [libraryText.listView, libraryText.coversView]
                onActivated: index => root.showCovers = index === 1
                Binding {
                    target: viewChoice
                    property: "currentIndex"
                    value: root.showCovers ? 1 : 0
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.feedbackText.length > 0 || root.seeQueueShown
            spacing: root.contract.spacingUnit * 2
            AuroraFeedback {
                objectName: "library-feedback"
                Layout.alignment: Qt.AlignVCenter
                contract: root.contract
                text: root.feedbackText
                tone: root.feedbackProblem ? AuroraFeedback.Error : AuroraFeedback.Success
            }
            AuroraButton {
                id: seeQueue
                Layout.alignment: Qt.AlignVCenter
                visible: root.seeQueueShown
                contract: root.contract
                controlId: "library-see-queue"
                kind: AuroraButton.Quiet
                compact: true
                text: navigationLabels.seeQueue
                onClicked: root.queueRequested(seeQueue)
            }
            Item {
                Layout.fillWidth: true
            }
        }
        AuroraNotice {
            id: retryNotice
            Layout.fillWidth: true
            visible: root.retryNeeded
            contract: root.contract
            controlId: "library-status-notice"
            tone: AuroraNotice.Error
            text: root.library !== null ? root.library.statusText : ""
            primaryActionText: root.library !== null && root.library.canRetrySave ? libraryText.retrySave : (root.library !== null && !root.library.busy ? libraryText.retryLoad : "")
            onPrimaryTriggered: root.retryFromNotice()
        }
        // The empty states exist only with a library: the frozen fixture shows its own and shares the ids inside.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: libraryEmpty.implicitHeight
            visible: root.ready && root.total === 0
            Loader {
                id: libraryEmpty
                anchors.centerIn: parent
                width: Math.min(parent.width, 480)
                active: root.library !== null
                sourceComponent: AuroraEmptyState {
                    contract: root.contract
                    controlId: "library-empty"
                    iconName: "library"
                    title: libraryText.emptyTitle
                    hint: libraryText.emptyAdvice
                    primaryActionText: root.contract.openAudioText
                    primaryButton.enabled: root.openingEnabled
                    onPrimaryTriggered: root.openAudioRequested(primaryButton)
                }
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: noMatches.implicitHeight
            visible: root.ready && root.total > 0 && root.visibleTotal === 0
            Loader {
                id: noMatches
                anchors.centerIn: parent
                width: Math.min(parent.width, 480)
                active: root.library !== null
                sourceComponent: AuroraEmptyState {
                    contract: root.contract
                    controlId: "library-no-matches"
                    iconName: "search"
                    title: libraryText.noMatches.arg(root.library !== null ? root.library.filterText.trim() : "")
                }
            }
        }
        // The header of the columns reads the same measures as the rows (AuroraTrackColumns).
        Item {
            id: columns
            objectName: "library-columns"
            AuroraTrackColumns {
                id: measures
                width: columns.width
                compact: root.compact
            }
            Layout.fillWidth: true
            Layout.preferredHeight: numberHeader.implicitHeight
            visible: root.ready && root.visibleTotal > 0 && !root.showCovers && !root.compact
            HeaderText {
                id: numberHeader
                objectName: "library-column-0"
                x: measures.sidePadding
                width: measures.numberWidth
                horizontalAlignment: Text.AlignHCenter
                text: libraryText.numberHeader
            }
            HeaderText {
                objectName: "library-column-1"
                x: measures.titleX
                width: measures.titleWidth
                text: libraryText.trackHeader
            }
            HeaderText {
                objectName: "library-column-2"
                x: measures.albumX
                width: measures.albumWidth
                visible: !root.compact
                text: libraryText.albumHeader
            }
            HeaderText {
                objectName: "library-column-3"
                x: measures.durationX
                width: measures.durationWidth
                horizontalAlignment: Text.AlignRight
                text: libraryText.timeHeader
            }
        }
        ListView {
            id: list
            objectName: "library-list"
            visible: root.ready && root.visibleTotal > 0 && !root.showCovers
            Layout.fillWidth: true
            Layout.fillHeight: true
            // One row is all the page asks for: the list takes the rest of the height and is the only scroller.
            Layout.preferredHeight: root.compact ? 52 : 56
            clip: true
            reuseItems: true
            cacheBuffer: 0
            boundsBehavior: Flickable.StopAtBounds
            activeFocusOnTab: true
            keyNavigationEnabled: true
            model: root.library !== null ? root.library.model : null
            ScrollBar.vertical: AccessibleScrollBar {
                contract: root.contract
            }
            Keys.onReturnPressed: event => root.playCurrent(event)
            Keys.onEnterPressed: event => root.playCurrent(event)
            Keys.onMenuPressed: event => root.openMenuForCurrent(event)
            Keys.onPressed: event => root.viewKeyPressed(list, event)
            Accessible.role: Accessible.List
            Accessible.name: navigationLabels.library
            delegate: AuroraTrackRow {
                id: row
                required property int index
                required property var model
                readonly property bool isPlayingRecord: root.playingId.length > 0 && model.recordId === root.playingId
                objectName: "library-row-" + model.recordId
                width: list.width
                contract: root.contract
                number: index + 1
                title: model.title
                subtitle: model.artist.length > 0 ? model.artist : libraryText.unknownArtist
                album: model.album
                durationText: model.durationText
                artworkSource: root.artworkBase.length > 0 ? root.artworkBase + model.recordId : ""
                available: model.available
                compact: root.compact
                current: isPlayingRecord
                playing: isPlayingRecord && root.playbackPlaying
                highlighted: ListView.isCurrentItem && list.activeFocus
                locateEnabled: root.library !== null && root.library.canLocate
                accessibleName: libraryText.rowName(model.title, model.artist, model.available)
                description: libraryText.rowDescription(model.available)
                onClicked: root.selectItem(list, index)
                playEnabled: root.openingEnabled
                onActivated: root.playNow(model.recordId, model.available)
                onPlayRequested: root.playNow(model.recordId, model.available)
                onEnqueueRequested: root.enqueue(model.recordId, model.title)
                onLocateRequested: root.locate(model.recordId)
                onMoreRequested: opener => root.showMenuFromButton(list, index, model.recordId, model.title, model.available, opener)
                onContextMenuRequested: opener => root.showTrackMenu(list, index, model.recordId, model.title, model.available, opener, row.actionsColumnX + row.actionsWidth - trackMenu.implicitWidth, row.height)
            }
        }
        GridView {
            id: grid
            objectName: "library-grid"
            readonly property real ringBleed: 2 + root.contract.focusRingWidth
            readonly property int gap: 24
            visible: root.ready && root.visibleTotal > 0 && root.showCovers
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: cellHeight
            clip: true
            reuseItems: true
            cacheBuffer: 0
            boundsBehavior: Flickable.StopAtBounds
            activeFocusOnTab: true
            keyNavigationEnabled: true
            leftMargin: ringBleed
            topMargin: ringBleed
            cellWidth: 150 + gap
            // Room for the cover, two title lines and the subtitle, as the card itself measures them.
            cellHeight: 150 + 10 + Math.ceil(titleMetrics.height * 2) + 4 + Math.ceil(captionMetrics.height) + gap
            model: root.library !== null ? root.library.model : null
            ScrollBar.vertical: AccessibleScrollBar {
                contract: root.contract
            }
            Keys.onReturnPressed: event => root.playCurrent(event)
            Keys.onEnterPressed: event => root.playCurrent(event)
            Keys.onMenuPressed: event => root.openMenuForCurrent(event)
            Keys.onPressed: event => root.viewKeyPressed(grid, event)
            Accessible.role: Accessible.List
            Accessible.name: navigationLabels.library
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
            delegate: Item {
                id: cell
                required property int index
                required property var model
                readonly property bool isPlayingRecord: root.playingId.length > 0 && model.recordId === root.playingId
                width: grid.cellWidth
                height: grid.cellHeight
                AuroraTrackCard {
                    id: card
                    objectName: "library-card-" + cell.model.recordId
                    contract: root.contract
                    title: cell.model.title
                    subtitle: cell.model.available ? (cell.model.artist.length > 0 ? cell.model.artist : libraryText.unknownArtist) : libraryText.unavailable
                    subtitleWarning: !cell.model.available
                    artworkSource: root.artworkBase.length > 0 ? root.artworkBase + cell.model.recordId : ""
                    playing: cell.isPlayingRecord && root.playbackPlaying
                    unavailableBadge: !cell.model.available
                    activateOnClick: false
                    // The grid keeps the focus: a click only makes the card the current one.
                    focusPolicy: Qt.NoFocus
                    highlighted: cell.GridView.isCurrentItem && grid.activeFocus
                    accessibleName: libraryText.rowName(cell.model.title, cell.model.artist, cell.model.available)
                    onSelected: root.selectItem(grid, cell.index)
                    playEnabled: root.openingEnabled
                    onActivated: root.playNow(cell.model.recordId, cell.model.available)
                    onPlayRequested: root.playNow(cell.model.recordId, cell.model.available)
                    onEnqueueRequested: root.enqueue(cell.model.recordId, cell.model.title)
                    onMoreRequested: opener => root.showMenuFromButton(grid, cell.index, cell.model.recordId, cell.model.title, cell.model.available, opener)
                }
            }
        }
    }
    component HeaderText: Text {
        textFormat: Text.PlainText
        elide: Text.ElideRight
        color: root.contract.textSecondaryColor
        font.family: root.contract.bodyFontFamily
        font.pixelSize: root.contract.typeEyebrow
        font.letterSpacing: 1.2
        Accessible.ignored: true
    }
    LibraryTrackMenu {
        id: trackMenu
        objectName: "library-track-menu"
        contract: root.contract
        playlists: root.playlistChoices
        playEnabled: root.openingEnabled
        canLocate: root.library !== null && root.library.canLocate
        onPlayRequested: root.playNow(recordId, available)
        onEnqueueRequested: root.enqueue(recordId, recordTitle)
        onPlaylistChosen: (id, name) => root.addToPlaylist(recordId, recordTitle, id, name)
        onLocateRequested: root.locate(recordId)
        onRemoveRequested: root.requestRemove()
        onClosed: {
            if (root.visible)
                root.activeView.forceActiveFocus(Qt.OtherFocusReason);
            if (root.removePending) {
                root.removePending = false;
                removeConfirmation.open();
            }
        }
    }
    AuroraConfirmDialog {
        id: removeConfirmation
        contract: root.contract
        controlId: "library-remove-confirmation"
        destructive: true
        question: libraryText.removeTitle.arg(root.removeTitle)
        consequence: libraryText.removeAdvice
        confirmText: libraryText.removeConfirm
        onConfirmed: {
            root.removeFocusTotal = root.total;
            root.removeFocusArmed = root.library.remove(root.removeId);
        }
        onClosed: root.settleFocusAfterRemove()
    }
}
