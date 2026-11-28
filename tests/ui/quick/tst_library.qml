import QtQuick
import QtTest
import MusicPlayer.M0

// ADR-0019: the permanent library page in List view.
TestCase {
    id: testCase
    name: "LibraryPage"
    when: windowShown
    readonly property var driver: testScenarioDriver // qmllint disable unqualified
    readonly property PlayerPresenter presenter: testLocalPlayerPresenter // qmllint disable unqualified
    readonly property ShellUiContract contract: testProductUiContract // qmllint disable unqualified
    property var shell

    // The catalog's full list; `records` shows the part that matches the filter, as the binding's model does.
    property var source: []
    readonly property var baseRecords: [
        {
            recordId: "3",
            title: "Canção do Mar",
            artist: "Lia Campos",
            album: "Depois da Chuva",
            durationText: "3:37",
            available: true,
            fileName: "Canção do Mar.mp3"
        },
        {
            recordId: "4",
            title: "faixa sem nome",
            artist: "",
            album: "",
            durationText: "",
            available: false,
            fileName: "faixa sem nome.flac"
        },
        {
            recordId: "5",
            title: "Faixa %2 ao vivo",
            artist: "Rui Alves",
            album: "Ao Vivo",
            durationText: "4:01",
            available: true,
            fileName: "ao vivo.mp3"
        }
    ]
    ListModel {
        id: records
    }
    ListModel {
        id: recentRecords
    }
    QtObject {
        id: catalog
        property bool ready: true
        property bool busy: false
        property int count: 3
        property int visibleCount: 3
        property bool hasPendingChange: false
        property bool canRetrySave: false
        property string statusText: "Sua biblioteca está pronta."
        property bool statusProblem: false
        property string filterText: ""
        onFilterTextChanged: testCase.applyFilter()
        property string playingRecordId: ""
        property var model: records
        property var recentModel: recentRecords
        property var enqueued: []
        property var played: []
        property var removed: []
        property var located: []
        property var playlistAdds: []
        property int loadRetries: 0
        property int saveRetries: 0
        property bool playResult: true
        property bool canLocate: true
        property string artworkBase: ""
        function enqueue(id) {
            enqueued = enqueued.concat([id]);
            return true;
        }
        function playNow(id) {
            played = played.concat([id]);
            return playResult;
        }
        function remove(id) {
            removed = removed.concat([id]);
            testCase.source = testCase.source.filter(entry => entry.recordId !== id);
            for (let i = 0; i < records.count; ++i) {
                if (records.get(i).recordId === id) {
                    records.remove(i);
                    break;
                }
            }
            count = testCase.source.length;
            visibleCount = records.count;
            if (playingRecordId === id)
                playingRecordId = "";
            return true;
        }
        function locate(id) {
            located = located.concat([id]);
            statusText = "Arquivo localizado.";
            return true;
        }
        function cancelLocate() {
        }
        function addToPlaylist(id, playlist) {
            playlistAdds = playlistAdds.concat([id + ":" + playlist]);
            return true;
        }
        function retryLoad() {
            loadRetries++;
            return true;
        }
        function retrySave() {
            saveRetries++;
            return true;
        }
    }
    QtObject {
        id: lists
        property bool ready: true
        property bool busy: false
        property bool interactionAllowed: true
        property bool hasPendingChange: false
        property bool canRetrySave: false
        property bool canAddFiles: true
        property bool canReplaceFile: true
        property string statusText: ""
        property string selectedPlaylistId: ""
        property var playlists: [
            {
                id: "7",
                name: "Favoritas 日本",
                count: 0
            }
        ]
        property var entries: []
        function selectPlaylist(id) {
            return true;
        }
        function retryLoad() {
            return true;
        }
    }
    Component {
        id: shellComponent
        Main {
            uiContract: testCase.contract
            presenter: testCase.presenter
            library: catalog
            playlists: lists
        }
    }
    Component {
        id: spyComponent
        SignalSpy {}
    }
    function init() {
        driver.resetLocalScenario();
        catalog.filterText = "";
        source = baseRecords.slice();
        records.clear();
        for (const entry of source)
            records.append(entry);
        catalog.ready = true;
        catalog.busy = false;
        catalog.count = 3;
        catalog.visibleCount = 3;
        catalog.filterText = "";
        catalog.playingRecordId = "";
        catalog.enqueued = [];
        catalog.played = [];
        catalog.removed = [];
        catalog.located = [];
        catalog.playlistAdds = [];
        catalog.loadRetries = 0;
        catalog.saveRetries = 0;
        catalog.playResult = true;
        catalog.canRetrySave = false;
        catalog.statusProblem = false;
        catalog.statusText = "Sua biblioteca está pronta.";
        lists.playlists = [
            {
                id: "7",
                name: "Favoritas 日本",
                count: 0
            }
        ];
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.width = 1440;
        shell.height = 900;
        shell.requestActivate();
        tryCompare(shell, "active", true);
    }
    function cleanup() {
        if (shell)
            shell.destroy();
        shell = null;
        driver.resetLocalScenario();
    }
    function openLibrary() {
        const button = findChild(shell, "library");
        verify(button.visible);
        mouseClick(button);
        const panel = findChild(shell, "library-panel");
        tryCompare(panel, "opened", true);
        compare(shell.currentPage, "library");
        verify(button.checked);
        return panel;
    }
    // A real queue with the first track playing, as the player reports it: the wave and the playing badge need it.
    function loadPlayingQueue() {
        driver.enqueueLocalSelection(["C:/Fixture/Primeira.mp3"]);
        verify(presenter.addAudio());
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
    }
    // The binding's model follows the filter by removing and inserting rows, never by a reset.
    function applyFilter() {
        const needle = catalog.filterText.trim().toLowerCase();
        let at = 0;
        for (const entry of source) {
            const matches = needle.length === 0 || entry.title.toLowerCase().indexOf(needle) >= 0;
            const shown = at < records.count && records.get(at).recordId === entry.recordId;
            if (matches && shown)
                ++at;
            else if (matches)
                records.insert(at++, entry);
            else if (shown)
                records.remove(at);
        }
        catalog.visibleCount = records.count;
    }
    function addRecords(extra) {
        for (let i = 0; i < extra; ++i) {
            const entry = {
                recordId: String(100 + i),
                title: "Faixa " + i,
                artist: "Artista",
                album: "Disco",
                durationText: "3:00",
                available: true,
                fileName: "faixa" + i + ".mp3"
            };
            source = source.concat([entry]);
            records.append(entry);
        }
        catalog.count = source.length;
        catalog.visibleCount = records.count;
    }
    function row(panel, id) {
        const list = findChild(panel, "library-list");
        tryVerify(() => findChild(list, "library-row-" + id) !== null);
        return findChild(list, "library-row-" + id);
    }
    function card(panel, id) {
        const grid = findChild(panel, "library-grid");
        tryVerify(() => findChild(grid, "library-card-" + id) !== null);
        return findChild(grid, "library-card-" + id);
    }
    function hover(item) {
        mouseMove(item, item.width / 2, item.height / 2);
    }
    function showRowActions(item) {
        hover(item);
        tryVerify(() => findChild(item, "track-row-more") !== null && findChild(item, "track-row-more").visible);
    }
    function openRowMenu(panel, id) {
        const target = row(panel, id);
        showRowActions(target);
        mouseClick(findChild(target, "track-row-more"));
        const menu = findChild(panel, "library-track-menu");
        tryCompare(menu, "opened", true);
        return menu;
    }
    // The header of a column starts where the row's own column does: number, title, album and duration.
    function columnsLineUp(panel) {
        const columns = findChild(panel, "library-columns");
        const first = row(panel, "3");
        const pairs = [["library-column-0", "track-row-number"], ["library-column-1", "track-row-artwork"], ["library-column-2", "track-row-album"], ["library-column-3", "track-row-duration"]];
        for (const pair of pairs) {
            const header = findChild(columns, pair[0]);
            const cell = findChild(first, pair[1]);
            if (Math.round(header.mapToItem(null, 0, 0).x) !== Math.round(cell.mapToItem(null, 0, 0).x))
                return false;
            if (pair[1] !== "track-row-artwork" && Math.round(header.width) !== Math.round(cell.width))
                return false;
        }
        return true;
    }
    function verifyColumnsLineUp(panel) {
        verify(columnsLineUp(panel), "the column header and the rows do not line up");
    }
    function feedbackText(panel) {
        return findChild(findChild(panel, "library-feedback"), "feedback-text").text;
    }

    function test_theLibraryMatchesTheMockup() {
        const panel = openLibrary();
        compare(panel.modal, false);
        const field = findChild(panel, "library-filter");
        verify(field.visible);
        compare(field.placeholderText, "Filtrar a biblioteca…");
        compare(findChild(field, "field-icon").iconName, "search");
        compare(findChild(panel, "library-count").text, "3 faixas");
        catalog.filterText = "mar";
        catalog.visibleCount = 1;
        tryCompare(findChild(panel, "library-count"), "text", "1 de 3 faixas");
        catalog.count = 1;
        tryCompare(findChild(panel, "library-count"), "text", "1 de 1 faixa");
        catalog.count = 3;
        catalog.filterText = "";
        catalog.visibleCount = 3;
        tryCompare(findChild(panel, "library-count"), "text", "3 faixas");
        verify(findChild(panel, "library-view-0").chosen);
        verify(!findChild(panel, "library-view-1").chosen);
        compare(findChild(findChild(panel, "library-view-0"), "segment-label").text, "Lista");
        compare(findChild(findChild(panel, "library-view-1"), "segment-label").text, "Capas");
        const columns = findChild(panel, "library-columns");
        verify(columns.visible);
        compare(findChild(columns, "library-column-0").text, "Nº");
        compare(findChild(columns, "library-column-1").text, "FAIXA / ARTISTA");
        compare(findChild(columns, "library-column-2").text, "ÁLBUM");
        compare(findChild(columns, "library-column-3").text, "DURAÇÃO");
        const first = row(panel, "3");
        compare(first.height, 56);
        compare(findChild(first, "track-row-title").text, "Canção do Mar");
        verify(findChild(first, "track-row-album").visible);
        verify(!findChild(panel, "library-empty").visible);
        verify(!findChild(panel, "library-no-matches").visible);
        verify(!findChild(panel, "library-see-queue").visible);
        verify(!findChild(panel, "library-feedback").visible);
        verifyColumnsLineUp(panel);
        shell.width = 1100;
        tryCompare(shell.contentItem, "width", 1100);
        tryVerify(() => Math.round(first.width) !== 1140 && first.width > 0);
        tryVerify(() => columnsLineUp(panel));
        verifyColumnsLineUp(panel);
    }
    function test_compactLibraryShowsTitleAndListOnly() {
        addRecords(8);
        shell.width = 720;
        shell.height = 520;
        tryCompare(shell.contentItem, "width", 720);
        tryCompare(shell.contentItem, "height", 520);
        const panel = openLibrary();
        verify(findChild(panel, "page-title").visible);
        verify(!findChild(panel, "page-eyebrow").visible);
        verify(!findChild(panel, "page-description").visible);
        verify(!findChild(panel, "library-columns").visible);
        const first = row(panel, "3");
        verify(!findChild(first, "track-row-album").visible);
        compare(first.height, 52);
        const list = findChild(panel, "library-list");
        // The shell leaves 310 px to the page at 520 high: the title and the bar take 91, so four whole rows fit.
        tryVerify(() => list.height >= 4 * 52, 5000, "list height " + list.height);
    }
    function test_bigLibraryKeepsOneScrollerAndFewDelegates() {
        addRecords(5000);
        const panel = openLibrary();
        const list = findChild(panel, "library-list");
        tryVerify(() => list.count === 5003 && list.height > 0);
        const viewport = findChild(panel, "page-viewport");
        verify(viewport.contentHeight <= viewport.height, "the page scrolls: " + viewport.contentHeight + " > " + viewport.height);
        list.positionViewAtEnd();
        tryVerify(() => list.atYEnd);
        verify(list.contentY > 0);
        let delegates = 0;
        for (const child of list.contentItem.children) {
            if (child.objectName.indexOf("library-row-") === 0)
                ++delegates;
        }
        verify(delegates > 0 && delegates < 40, "delegates: " + delegates);
        verify(viewport.contentHeight <= viewport.height);
    }
    function test_doubleClickAndPlayButtonPlayNow() {
        const panel = openLibrary();
        const first = row(panel, "3");
        mouseDoubleClickSequence(first, first.width / 2, first.height / 2);
        compare(catalog.played, ["3"]);
        const other = row(panel, "5");
        showRowActions(other);
        mouseClick(findChild(other, "track-row-play"));
        compare(catalog.played, ["3", "5"]);
        compare(catalog.enqueued, []);
    }
    function test_playNowFailureSaysSo() {
        catalog.playResult = false;
        const panel = openLibrary();
        const first = row(panel, "3");
        mouseDoubleClickSequence(first, first.width / 2, first.height / 2);
        compare(catalog.played, ["3"]);
        const feedback = findChild(panel, "library-feedback");
        tryVerify(() => feedback.visible);
        compare(feedbackText(panel), "Não foi possível tocar agora. Tente novamente.");
        compare(findChild(feedback, "feedback-icon").iconName, "warning-circle");
        verify(!findChild(panel, "library-see-queue").visible);
    }
    function test_enqueueConfirmsAndOffersSeeQueue() {
        const panel = openLibrary();
        const first = row(panel, "3");
        showRowActions(first);
        mouseClick(findChild(first, "track-row-enqueue"));
        compare(catalog.enqueued, ["3"]);
        compare(catalog.played, []);
        const feedback = findChild(panel, "library-feedback");
        tryVerify(() => feedback.visible);
        verify(feedbackText(panel).indexOf("Canção do Mar") >= 0);
        compare(findChild(feedback, "feedback-icon").iconName, "check-circle");
        verify(findChild(panel, "library-see-queue").visible);
        compare(catalog.removed, []);
    }
    function test_seeQueueOpensTheDrawerFromThePlayer() {
        loadPlayingQueue();
        const panel = openLibrary();
        const first = row(panel, "3");
        showRowActions(first);
        mouseClick(findChild(first, "track-row-enqueue"));
        const seeQueue = findChild(panel, "library-see-queue");
        tryVerify(() => seeQueue.visible);
        mouseClick(seeQueue);
        const drawer = findChild(shell, "queue-panel");
        tryCompare(drawer, "visible", true);
        tryCompare(panel, "opened", false);
        keyClick(Qt.Key_Escape);
        tryCompare(drawer, "visible", false);
        const player = findChild(shell, "player-queue");
        tryCompare(player, "activeFocus", true);
    }
    function test_playingRecordShowsTheWave() {
        loadPlayingQueue();
        catalog.playingRecordId = "3";
        const panel = openLibrary();
        const first = row(panel, "3");
        verify(findChild(first, "track-row-wave").visible);
        verify(!findChild(first, "track-row-number").visible);
        verify(!findChild(row(panel, "5"), "track-row-wave").visible);
        driver.publishLocalPaused();
        tryCompare(presenter, "playbackState", PlayerPresenter.Paused);
        tryVerify(() => !findChild(first, "track-row-wave").visible);
        verify(findChild(first, "track-row-number").visible);
        compare(findChild(first, "track-row-number").text, "1");
    }
    function test_menuOffersTheLibraryActions() {
        const panel = openLibrary();
        const menu = openRowMenu(panel, "3");
        compare(menu.width, 248);
        const names = [];
        for (let i = 0; i < menu.count; ++i)
            names.push(menu.itemAt(i).objectName);
        compare(names.filter(name => name.length > 0), ["library-menu-play", "library-menu-enqueue", "library-menu-playlists", "library-menu-locate", "library-menu-remove"]);
        const play = menu.itemAt(0);
        compare(play.text, "Tocar agora");
        compare(play.shortcutText, "Enter");
        compare(play.iconName, "play");
        verify(play.enabled);
        compare(menu.itemAt(1).text, "Adicionar à fila");
        const playlists = menu.itemAt(2);
        compare(playlists.text, "Adicionar à playlist");
        verify(playlists.hasSubMenu);
        const locate = findChild(menu, "library-menu-locate");
        verify(!locate.visible);
        verify(!locate.enabled);
        compare(locate.height, 0);
        const remove = findChild(menu, "library-menu-remove");
        compare(remove.text, "Remover da biblioteca…");
        verify(remove.danger);
        verify(remove.y > playlists.y + playlists.height, "a separator sits before Remover");
        menu.close();
        tryCompare(menu, "visible", false);
        // The unavailable row has no ⋯: its menu opens with the right button.
        const gone = row(panel, "4");
        hover(gone);
        verify(!findChild(gone, "track-row-more").visible);
        mouseClick(gone, gone.width / 2, gone.height / 2, Qt.RightButton);
        tryCompare(menu, "opened", true);
        const blocked = findChild(menu, "library-menu-play");
        verify(!blocked.enabled);
        compare(blocked.Accessible.description, "Arquivo não encontrado. Localize o arquivo para tocar a faixa.");
        const locating = findChild(menu, "library-menu-locate");
        verify(locating.visible);
        verify(locating.enabled);
        compare(locating.text, "Localizar arquivo…");
        mouseClick(locating);
        compare(catalog.located, ["4"]);
    }
    function test_menuActionsReachTheBinding() {
        const panel = openLibrary();
        let menu = openRowMenu(panel, "3");
        mouseClick(findChild(menu, "library-menu-play"));
        compare(catalog.played, ["3"]);
        tryCompare(menu, "visible", false);
        menu = openRowMenu(panel, "5");
        mouseClick(findChild(menu, "library-menu-enqueue"));
        compare(catalog.enqueued, ["5"]);
        verify(findChild(panel, "library-see-queue").visible);
    }
    function test_removingAsksFirstAndTouchesOnlyTheRecord() {
        const panel = openLibrary();
        let menu = openRowMenu(panel, "3");
        mouseClick(findChild(menu, "library-menu-remove"));
        const confirmation = findChild(shell, "library-remove-confirmation");
        tryCompare(confirmation, "visible", true);
        verify(findChild(confirmation, "confirm-consequence").text.indexOf("A faixa sai só da biblioteca.") === 0);
        verify(confirmation.title.indexOf("Canção do Mar") >= 0);
        mouseClick(findChild(confirmation, "library-remove-confirmation-cancel"));
        tryCompare(confirmation, "visible", false);
        compare(catalog.removed, []);
        menu = openRowMenu(panel, "3");
        mouseClick(findChild(menu, "library-menu-remove"));
        tryCompare(confirmation, "visible", true);
        mouseClick(findChild(confirmation, "library-remove-confirmation-confirm"));
        tryCompare(confirmation, "visible", false);
        compare(catalog.removed, ["3"]);
        compare(catalog.enqueued, []);
        compare(catalog.played, []);
    }
    function test_onlyUnavailableRecordsOfferLocating() {
        const panel = openLibrary();
        const available = row(panel, "3");
        verify(!findChild(available, "track-row-locate").visible);
        const gone = row(panel, "4");
        verify(gone.Accessible.name.indexOf("Indisponível") >= 0);
        compare(findChild(panel, "library-list").count, 3);
        hover(gone);
        verify(!findChild(gone, "track-row-enqueue").visible);
        const locate = findChild(gone, "track-row-locate");
        verify(locate.visible);
        mouseClick(locate);
        compare(catalog.located, ["4"]);
        const feedback = findChild(panel, "library-feedback");
        tryVerify(() => feedback.visible);
        compare(feedbackText(panel), "Arquivo localizado.");
        compare(findChild(feedback, "feedback-icon").iconName, "check-circle");
    }
    function test_aTrackCanBeAddedToASavedPlaylist() {
        const panel = openLibrary();
        const menu = openRowMenu(panel, "5");
        const entry = menu.itemAt(2);
        mouseClick(entry);
        tryCompare(entry.subMenu, "opened", true);
        const choice = findChild(shell, "library-playlist-choice-7");
        verify(choice);
        compare(choice.text, "Favoritas 日本");
        mouseClick(choice);
        compare(catalog.playlistAdds, ["5:7"]);
        const feedback = findChild(panel, "library-feedback");
        tryVerify(() => feedback.visible);
        // Both blanks are filled in one pass: a title with a placeholder of its own stays as it is.
        compare(feedbackText(panel), "“Faixa %2 ao vivo” foi para a playlist “Favoritas 日本”.");
    }
    function test_withoutPlaylistsTheSubmenuSaysSo() {
        lists.playlists = [];
        const panel = openLibrary();
        const menu = openRowMenu(panel, "3");
        const entry = menu.itemAt(2);
        mouseClick(entry);
        tryCompare(entry.subMenu, "opened", true);
        const none = findChild(shell, "library-playlist-none");
        verify(none);
        compare(none.text, "Nenhuma playlist ainda");
        verify(!none.enabled);
        verify(!findChild(shell, "library-playlist-choice-7"));
    }
    function test_listIsTheDefaultAndCoversIsTheAlternative() {
        const panel = openLibrary();
        verify(findChild(panel, "library-view-0").chosen);
        verify(!findChild(panel, "library-grid").visible);
        mouseClick(findChild(panel, "library-view-1"));
        tryVerify(() => findChild(panel, "library-grid").visible);
        verify(findChild(panel, "library-view-1").chosen);
        verify(!findChild(panel, "library-list").visible);
        verify(!findChild(panel, "library-columns").visible);
        const grid = findChild(panel, "library-grid");
        tryCompare(grid, "count", 3);
        // Switching views relayouts the page; wait until the grid has its real size.
        tryVerify(() => grid.width > 0 && grid.height > 0);
        const first = card(panel, "3");
        // Without a cover reader the cards keep only the themed placeholder.
        compare(findChild(first, "shelf-card-artwork").source.toString(), "");
        mouseClick(findChild(panel, "library-view-0"));
        tryVerify(() => findChild(panel, "library-list").visible);
        verify(findChild(panel, "library-view-0").chosen);
    }
    function test_coversViewShowsOverlaysAndBadges() {
        loadPlayingQueue();
        catalog.playingRecordId = "3";
        const panel = openLibrary();
        mouseClick(findChild(panel, "library-view-1"));
        tryVerify(() => findChild(panel, "library-grid").visible);
        const first = card(panel, "3");
        const gone = card(panel, "4");
        verify(findChild(first, "shelf-card-playing").visible);
        compare(findChild(first, "shelf-card-playing").text, "Tocando");
        verify(!findChild(first, "shelf-card-unavailable").visible);
        verify(findChild(gone, "shelf-card-unavailable").visible);
        verify(!findChild(gone, "shelf-card-playing").visible);
        verify(!findChild(first, "shelf-card-play").visible);
        hover(first);
        tryVerify(() => findChild(first, "shelf-card-play").visible);
        verify(findChild(first, "shelf-card-enqueue").visible);
        verify(findChild(first, "shelf-card-more").visible);
        mouseClick(findChild(first, "shelf-card-play"));
        compare(catalog.played, ["3"]);
        mouseClick(findChild(first, "shelf-card-enqueue"));
        compare(catalog.enqueued, ["3"]);
        mouseClick(findChild(first, "shelf-card-more"));
        const menu = findChild(panel, "library-track-menu");
        tryCompare(menu, "opened", true);
        compare(menu.recordId, "3");
        menu.close();
        tryCompare(menu, "visible", false);
        // A single click only selects; the double click plays.
        const last = card(panel, "5");
        mouseClick(last, 20, 20);
        compare(catalog.played, ["3"]);
        mouseDoubleClickSequence(last, 20, 20);
        compare(catalog.played, ["3", "5"]);
        // The unavailable card can neither play nor be enqueued: it does not even try, and says nothing.
        mouseDoubleClickSequence(gone, 20, 20);
        compare(catalog.played, ["3", "5"]);
        verify(!findChild(panel, "library-feedback").visible);
        hover(gone);
        tryVerify(() => findChild(gone, "shelf-card-play").visible);
        verify(!findChild(gone, "shelf-card-play").enabled);
        verify(!findChild(gone, "shelf-card-enqueue").visible);
    }
    function test_typingFiltersThroughTheBinding() {
        const panel = openLibrary();
        const field = findChild(panel, "library-filter");
        field.forceActiveFocus();
        keyClick(Qt.Key_M);
        keyClick(Qt.Key_A);
        keyClick(Qt.Key_R);
        compare(catalog.filterText, "mar");
    }
    function test_emptyLibraryUsesTheEmptyStatePiece() {
        catalog.count = 0;
        catalog.visibleCount = 0;
        const panel = openLibrary();
        const empty = findChild(panel, "library-empty");
        verify(empty.visible);
        compare(findChild(empty, "empty-title").text, "Sua biblioteca está vazia");
        compare(findChild(empty, "empty-hint").text, "Abra arquivos do computador ou baixe faixas em Buscar. Tudo fica guardado aqui, mesmo quando a fila muda.");
        const primary = findChild(empty, "library-empty-primary");
        compare(primary.text, "Abrir áudio");
        verify(!findChild(panel, "library-filter").visible);
        verify(!findChild(panel, "library-view-0").visible);
        verify(!findChild(panel, "library-count").visible);
        verify(!findChild(panel, "library-list").visible);
        const spy = createTemporaryObject(spyComponent, testCase, {
            target: panel,
            signalName: "openAudioRequested"
        });
        mouseClick(primary);
        compare(spy.count, 1);
    }
    function test_emptyAndNoMatchStatesAreDistinct() {
        catalog.count = 0;
        catalog.visibleCount = 0;
        const panel = openLibrary();
        verify(findChild(panel, "library-empty").visible);
        verify(!findChild(panel, "library-filter").visible);
        verify(!findChild(panel, "library-no-matches").visible);
        catalog.count = 3;
        catalog.filterText = "rock";
        tryVerify(() => findChild(panel, "library-no-matches").visible);
        verify(!findChild(panel, "library-empty").visible);
        verify(findChild(panel, "library-filter").visible);
        verify(findChild(findChild(panel, "library-no-matches"), "empty-title").text.indexOf("rock") >= 0);
    }
    function test_aFailedLoadOffersARetryAndLoadingSaysSo() {
        catalog.ready = false;
        catalog.busy = true;
        catalog.statusText = "Carregando a biblioteca…";
        const panel = openLibrary();
        const status = findChild(panel, "library-status");
        tryVerify(() => status.visible);
        compare(status.text, "Carregando a biblioteca…");
        verify(!findChild(panel, "library-status-notice").visible);
        verify(!findChild(panel, "library-filter").visible);
        catalog.busy = false;
        catalog.statusProblem = true;
        catalog.statusText = "Não foi possível carregar a biblioteca. Nada foi alterado. Tente novamente.";
        const notice = findChild(panel, "library-status-notice");
        tryVerify(() => notice.visible);
        verify(!status.visible);
        const retry = findChild(notice, "library-status-notice-primary");
        compare(retry.text, "Tentar carregar novamente");
        mouseClick(retry);
        compare(catalog.loadRetries, 1);
    }
    function test_aFailedSaveOffersToSaveAgain() {
        catalog.canRetrySave = true;
        catalog.statusProblem = true;
        catalog.statusText = "Não foi possível salvar a biblioteca.";
        const panel = openLibrary();
        const notice = findChild(panel, "library-status-notice");
        tryVerify(() => notice.visible);
        verify(findChild(panel, "library-filter").visible);
        const retry = findChild(notice, "library-status-notice-primary");
        compare(retry.text, "Tentar salvar novamente");
        mouseClick(retry);
        compare(catalog.saveRetries, 1);
    }
    function test_playIsDisabledWhileAFileChooserIsOpen() {
        const panel = openLibrary();
        // Main drives it from the open file chooser, which the fake driver closes on its own: set it directly.
        panel.openingEnabled = false;
        const first = row(panel, "3");
        showRowActions(first);
        verify(!findChild(first, "track-row-play").enabled);
        mouseDoubleClickSequence(first, first.width / 2, first.height / 2);
        compare(catalog.played, []);
        verify(!findChild(panel, "library-feedback").visible);
        const menu = openRowMenu(panel, "3");
        verify(!findChild(menu, "library-menu-play").enabled);
        verify(findChild(menu, "library-menu-enqueue").enabled);
        menu.close();
        mouseClick(findChild(panel, "library-view-1"));
        const cover = card(panel, "3");
        hover(cover);
        tryVerify(() => findChild(cover, "shelf-card-play").visible);
        verify(!findChild(cover, "shelf-card-play").enabled);
        mouseDoubleClickSequence(cover, 20, 20);
        compare(catalog.played, []);
    }
    function test_openingAfterAFailedLoadFocusesTheRetryButton() {
        catalog.ready = false;
        catalog.statusProblem = true;
        catalog.statusText = "Não foi possível carregar a biblioteca. Nada foi alterado. Tente novamente.";
        const panel = openLibrary();
        const retry = findChild(findChild(panel, "library-status-notice"), "library-status-notice-primary");
        tryCompare(retry, "activeFocus", true);
    }

    // The name of the focused control; a row of the list that holds the focus counts as the list.
    function focusName() {
        for (let item = shell.activeFocusItem; item; item = item.parent) {
            if (item.objectName === "library-list" || item.objectName === "library-grid")
                return String(item.objectName);
        }
        const focused = shell.activeFocusItem;
        return focused ? String(focused.objectName) : "";
    }
    function tabNames(key, count) {
        const names = [];
        for (let i = 0; i < count; ++i) {
            keyClick(key);
            names.push(focusName());
        }
        return names;
    }
    function focusList(panel) {
        const list = findChild(panel, "library-list");
        list.forceActiveFocus(Qt.TabFocusReason);
        tryCompare(list, "activeFocus", true);
        return list;
    }
    // The current row is wholly inside the list's window.
    function wholeRowShown(list) {
        const item = list.currentItem;
        return item !== null && item.y >= list.contentY && item.y + item.height <= list.contentY + list.height;
    }
    function confirmRemoval() {
        const confirmation = findChild(shell, "library-remove-confirmation");
        tryCompare(confirmation, "visible", true);
        mouseClick(findChild(confirmation, "library-remove-confirmation-confirm"));
        tryCompare(confirmation, "visible", false);
    }
    function removeCurrentFromTheKeyboard(panel) {
        const menu = findChild(panel, "library-track-menu");
        keyClick(Qt.Key_Menu);
        tryCompare(menu, "opened", true);
        mouseClick(findChild(menu, "library-menu-remove"));
        confirmRemoval();
    }

    function test_tabOrderThroughTheLibrary() {
        loadPlayingQueue();
        const panel = openLibrary();
        const theme = findChild(shell, "theme-toggle");
        theme.forceActiveFocus(Qt.TabFocusReason);
        compare(focusName(), "theme-toggle");
        compare(tabNames(Qt.Key_Tab, 3), ["library-filter", "library-view-0", "library-list"]);
        const list = findChild(panel, "library-list");
        // The current row wears the focus outline and shows its actions.
        verify(row(panel, "3").highlighted);
        compare(tabNames(Qt.Key_Tab, 1), ["progress"]);
        verify(!row(panel, "3").highlighted);
        compare(tabNames(Qt.Key_Backtab, 4), ["library-list", "library-view-0", "library-filter", "theme-toggle"]);
        // The confirmation line adds one stop between the view choice and the list.
        const first = row(panel, "3");
        showRowActions(first);
        mouseClick(findChild(first, "track-row-enqueue"));
        const seeQueue = findChild(panel, "library-see-queue");
        tryVerify(() => seeQueue.visible);
        theme.forceActiveFocus(Qt.TabFocusReason);
        compare(tabNames(Qt.Key_Tab, 5), ["library-filter", "library-view-0", "library-see-queue", "library-list", "progress"]);
        compare(tabNames(Qt.Key_Backtab, 5), ["library-list", "library-see-queue", "library-view-0", "library-filter", "theme-toggle"]);
        // The list is one stop: none of the row's buttons takes the Tab.
        compare(list.activeFocusOnTab, true);
        for (const name of ["track-row-play", "track-row-enqueue", "track-row-more", "track-row-locate"])
            compare(findChild(first, name).activeFocusOnTab, false);
    }
    function test_arrowsMoveTheCurrentRowAndEnterPlaysIt() {
        addRecords(30);
        const panel = openLibrary();
        const list = focusList(panel);
        compare(list.currentIndex, 0);
        verify(row(panel, "3").highlighted);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Down);
        compare(list.currentIndex, 2);
        keyClick(Qt.Key_Return);
        compare(catalog.played, ["5"]);
        keyClick(Qt.Key_End);
        compare(list.currentIndex, list.count - 1);
        tryVerify(() => wholeRowShown(list), 5000, "the last row is not wholly shown");
        keyClick(Qt.Key_Home);
        compare(list.currentIndex, 0);
        tryVerify(() => wholeRowShown(list) && list.contentY === 0);
        keyClick(Qt.Key_PageDown);
        verify(list.currentIndex > 3, "PageDown moved to " + list.currentIndex);
        keyClick(Qt.Key_PageUp);
        compare(list.currentIndex, 0);
        keyClick(Qt.Key_Enter);
        compare(catalog.played, ["5", "3"]);
    }
    function test_menuKeysOpenTheRowMenu() {
        const panel = openLibrary();
        const list = focusList(panel);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Down);
        const menu = findChild(panel, "library-track-menu");
        for (const press of [[Qt.Key_Menu, Qt.NoModifier], [Qt.Key_F10, Qt.ShiftModifier]]) {
            keyClick(press[0], press[1]);
            tryCompare(menu, "opened", true);
            compare(menu.recordId, "5");
            compare(list.currentIndex, 2);
            tryCompare(findChild(menu, "library-menu-play"), "activeFocus", true);
            keyClick(Qt.Key_Escape);
            tryCompare(menu, "visible", false);
            tryCompare(list, "activeFocus", true);
            compare(panel.opened, true);
            compare(shell.currentPage, "library");
        }
        // Enter on the first item plays the row and sends the focus back to the list.
        keyClick(Qt.Key_Menu);
        tryCompare(menu, "opened", true);
        tryCompare(findChild(menu, "library-menu-play"), "activeFocus", true);
        keyClick(Qt.Key_Return);
        compare(catalog.played, ["5"]);
        tryCompare(menu, "visible", false);
        tryCompare(list, "activeFocus", true);
        // On a missing file the first item is disabled: the focus starts on the first usable one.
        keyClick(Qt.Key_Up);
        compare(list.currentIndex, 1);
        keyClick(Qt.Key_Menu);
        tryCompare(menu, "opened", true);
        compare(menu.recordId, "4");
        verify(!findChild(menu, "library-menu-play").enabled);
        tryCompare(findChild(menu, "library-menu-locate"), "activeFocus", true);
        keyClick(Qt.Key_Escape);
        tryCompare(menu, "visible", false);
        tryCompare(list, "activeFocus", true);
    }
    function test_rightClickOpensTheMenuOnTheClickedRow() {
        addRecords(8);
        const panel = openLibrary();
        const list = findChild(panel, "library-list");
        const target = row(panel, "5");
        mouseClick(target, target.width / 2, target.height / 2, Qt.RightButton);
        const menu = findChild(panel, "library-track-menu");
        tryCompare(menu, "opened", true);
        compare(menu.recordId, "5");
        compare(list.currentIndex, 2);
        compare(list.currentItem.objectName, "library-row-5");
        keyClick(Qt.Key_Escape);
        tryCompare(menu, "visible", false);
        tryCompare(list, "activeFocus", true);
    }
    function test_unavailableRowEnterDoesNothingButSaysWhy() {
        const panel = openLibrary();
        const list = focusList(panel);
        keyClick(Qt.Key_Down);
        compare(list.currentItem.objectName, "library-row-4");
        keyClick(Qt.Key_Return);
        keyClick(Qt.Key_Enter);
        compare(catalog.played, []);
        verify(!findChild(panel, "library-feedback").visible);
        compare(list.currentItem.Accessible.description, "Arquivo não encontrado. Localize o arquivo para tocar a faixa.");
        verify(list.currentItem.Accessible.name.indexOf("Indisponível") >= 0);
    }
    function test_removingTheFocusedRowKeepsTheFocusInTheList() {
        addRecords(2);
        const panel = openLibrary();
        const list = focusList(panel);
        // The middle: the next row takes the place and the focus stays in the list.
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Down);
        removeCurrentFromTheKeyboard(panel);
        compare(catalog.removed, ["5"]);
        tryCompare(list, "activeFocus", true);
        compare(list.currentIndex, 2);
        compare(list.currentItem.objectName, "library-row-100");
        // The last: the previous row becomes the current one.
        keyClick(Qt.Key_End);
        compare(list.currentItem.objectName, "library-row-101");
        removeCurrentFromTheKeyboard(panel);
        compare(catalog.removed, ["5", "101"]);
        tryCompare(list, "activeFocus", true);
        compare(list.currentItem.objectName, "library-row-100");
        // The filtered list empties while the catalog still has tracks: the focus goes to the filter.
        const field = findChild(panel, "library-filter");
        field.forceActiveFocus();
        for (const key of [Qt.Key_M, Qt.Key_A, Qt.Key_R])
            keyClick(key);
        compare(catalog.filterText, "mar");
        tryCompare(list, "count", 1);
        tryVerify(() => list.currentItem !== null && list.currentItem.objectName === "library-row-3");
        list.forceActiveFocus(Qt.TabFocusReason);
        removeCurrentFromTheKeyboard(panel);
        compare(catalog.removed, ["5", "101", "3"]);
        tryCompare(catalog, "visibleCount", 0);
        verify(catalog.count > 0);
        tryCompare(field, "activeFocus", true);
        // The catalog empties: the toolbar is hidden, so the focus goes to the empty state's action.
        catalog.filterText = "";
        tryCompare(list, "count", 2);
        catalog.remove("100");
        tryCompare(list, "count", 1);
        tryVerify(() => list.currentItem !== null && list.currentItem.objectName === "library-row-4");
        list.forceActiveFocus(Qt.TabFocusReason);
        removeCurrentFromTheKeyboard(panel);
        compare(catalog.count, 0);
        verify(!field.visible);
        tryCompare(findChild(panel, "library-empty-primary"), "activeFocus", true);
    }
    function test_removingThePlayingRecordKeepsPlaying() {
        loadPlayingQueue();
        catalog.playingRecordId = "3";
        const panel = openLibrary();
        const first = row(panel, "3");
        verify(findChild(first, "track-row-wave").visible);
        const commands = () => [driver.localLoadCount(), driver.localUnloadCount(), driver.localPlayCount(), driver.localPauseCount(), driver.localSeekCount()];
        const before = commands();
        const playing = presenter.currentTitle;
        const queued = presenter.queueModel.count;
        const playingIndex = presenter.currentQueueIndex;
        const menu = openRowMenu(panel, "3");
        mouseClick(findChild(menu, "library-menu-remove"));
        confirmRemoval();
        compare(catalog.removed, ["3"]);
        compare(catalog.playingRecordId, "");
        compare(presenter.playbackState, PlayerPresenter.Playing);
        compare(presenter.currentTitle, playing);
        compare(presenter.queueModel.count, queued);
        compare(presenter.currentQueueIndex, playingIndex);
        compare(commands(), before);
        compare(catalog.played, []);
        tryCompare(findChild(panel, "library-list"), "count", 2);
        verify(!findChild(row(panel, "5"), "track-row-wave").visible);
    }
    function test_gridKeyboard() {
        addRecords(16);
        const panel = openLibrary();
        mouseClick(findChild(panel, "library-view-1"));
        const grid = findChild(panel, "library-grid");
        tryVerify(() => grid.visible && grid.width > 0 && grid.height > 0);
        card(panel, "3");
        grid.forceActiveFocus(Qt.TabFocusReason);
        tryCompare(grid, "activeFocus", true);
        let columns = 0;
        tryVerify(() => {
            columns = 0;
            const top = grid.itemAtIndex(0);
            for (let i = 0; top && i < 12; ++i) {
                const item = grid.itemAtIndex(i);
                if (item && item.y === top.y)
                    ++columns;
            }
            return columns > 1;
        });
        keyClick(Qt.Key_Right);
        keyClick(Qt.Key_Right);
        compare(grid.currentIndex, 2);
        keyClick(Qt.Key_Down);
        compare(grid.currentIndex, 2 + columns);
        keyClick(Qt.Key_Up);
        compare(grid.currentIndex, 2);
        compare(grid.currentItem.model.recordId, "5");
        keyClick(Qt.Key_Return);
        compare(catalog.played, ["5"]);
        keyClick(Qt.Key_Menu);
        const menu = findChild(panel, "library-track-menu");
        tryCompare(menu, "opened", true);
        compare(menu.recordId, "5");
        tryCompare(findChild(menu, "library-menu-play"), "activeFocus", true);
        keyClick(Qt.Key_Escape);
        tryCompare(menu, "visible", false);
        tryCompare(grid, "activeFocus", true);
        compare(panel.opened, true);
        keyClick(Qt.Key_End);
        compare(grid.currentIndex, grid.count - 1);
        keyClick(Qt.Key_Home);
        compare(grid.currentIndex, 0);
    }
    function test_filterKeepsTheCurrentRowWhenItStillMatches() {
        const panel = openLibrary();
        const list = focusList(panel);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Down);
        compare(list.currentItem.objectName, "library-row-5");
        const field = findChild(panel, "library-filter");
        field.forceActiveFocus();
        for (const key of [Qt.Key_F, Qt.Key_A, Qt.Key_I, Qt.Key_X, Qt.Key_A])
            keyClick(key);
        compare(catalog.filterText, "faixa");
        tryCompare(list, "count", 2);
        tryCompare(list, "currentIndex", 1);
        compare(list.currentItem.objectName, "library-row-5");
    }
}
