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

    ListModel {
        id: records
        ListElement {
            recordId: "3"
            title: "Canção do Mar"
            artist: "Lia Campos"
            album: "Depois da Chuva"
            durationText: "3:37"
            available: true
            fileName: "Canção do Mar.mp3"
        }
        ListElement {
            recordId: "4"
            title: "faixa sem nome"
            artist: ""
            album: ""
            durationText: ""
            available: false
            fileName: "faixa sem nome.flac"
        }
        ListElement {
            recordId: "5"
            title: "Faixa %2 ao vivo"
            artist: "Rui Alves"
            album: "Ao Vivo"
            durationText: "4:01"
            available: true
            fileName: "ao vivo.mp3"
        }
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
        if (records.count > 3)
            records.remove(3, records.count - 3);
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
    function addRecords(extra) {
        for (let i = 0; i < extra; ++i) {
            records.append({
                recordId: String(100 + i),
                title: "Faixa " + i,
                artist: "Artista",
                album: "Disco",
                durationText: "3:00",
                available: true,
                fileName: "faixa" + i + ".mp3"
            });
        }
        catalog.count = records.count;
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
        // The header of a column starts where the row's own column does.
        const album = findChild(first, "track-row-album");
        const header = findChild(columns, "library-column-2");
        compare(Math.round(header.mapToItem(null, 0, 0).x), Math.round(album.mapToItem(null, 0, 0).x));
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
        // The unavailable card can neither play nor be enqueued.
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
}
