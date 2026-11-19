import QtQuick
import QtQuick.Controls
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "PlaylistsPanel"
    when: windowShown
    readonly property var scenarioFactory: testScenarioDriver // qmllint disable unqualified
    property var scenario: null
    readonly property var driver: scenario ? scenario.driver : null
    readonly property PlayerPresenter presenter: scenario ? scenario.presenter : null
    readonly property var backend: playlistsBackend
    property var shell
    property bool exitObserved: false
    readonly property string saveFailure: "Não foi possível confirmar o salvamento. Tente salvar novamente ou recarregue as listas do disco."
    Connections {
        target: testCase.presenter
        function onLifecycleChanged() {
            if (testCase.presenter.exiting)
                testCase.exitObserved = true;
        }
    }
    QtObject {
        id: playlistsBackend
        property bool ready: true
        property bool busy: false
        property bool interactionAllowed: true
        property string statusText: "Playlists salvas"
        property string selectedPlaylistId: "1"
        property var playlists: []
        property var entries: []
        property bool canAddFiles: true
        property bool canReplaceFile: true
        property bool hasPendingChange: false
        readonly property bool canRetrySave: hasPendingChange && !busy && interactionAllowed && testCase.presenter !== null && !testCase.presenter.exiting
        property var calls: []
        function record(action, args) {
            calls = calls.concat([
                {
                    action: action,
                    args: args
                }
            ]);
            return true;
        }
        function selectPlaylist(id) {
            selectedPlaylistId = id;
            return record("select", [id]);
        }
        function createPlaylist(name) {
            return record("create", [name]);
        }
        function renamePlaylist(id, name) {
            return record("rename", [id, name]);
        }
        function deletePlaylist(id) {
            return record("delete", [id]);
        }
        function setPlaylistVisibleToRoom(id, visible) {
            return record("visibility", [id, visible]);
        }
        function saveQueueAsPlaylist(name) {
            return record("saveQueue", [name]);
        }
        function addQueueToPlaylist(id) {
            return record("addQueue", [id]);
        }
        function requestAddFiles(id) {
            return record("addFiles", [id]);
        }
        function requestReplaceFile(id, entry) {
            return record("replaceFile", [id, entry]);
        }
        function removeEntry(id, entry) {
            return record("removeEntry", [id, entry]);
        }
        function moveEntry(id, entry, destination) {
            return record("moveEntry", [id, entry, destination]);
        }
        function enqueuePlaylist(id) {
            return record("enqueue", [id]);
        }
        function retrySave() {
            return record("retrySave", []);
        }
        function discardPending() {
            return record("discardPending", []);
        }
        function discardUnsavedChange() {
            record("discardUnsavedChange", []);
            if (!canRetrySave)
                return false;
            hasPendingChange = false;
            ready = false;
            return true;
        }
        function retryLoad() {
            return record("retryLoad", []);
        }
    }
    QtObject {
        id: optionalDownloads
        property bool canCloseAfterFinalSaveFailure: false
        property bool canRetryAdmission: false
        property bool canRetryFinalSave: false
        property bool canRetryStartup: false
        property bool canRenewNamespace: false
        property string warningText: ""
        property string statusText: "Downloads"
        property string importConsentJobId: ""
        property string importConsentToken: ""
        property string discardConsentJobId: ""
        property string discardConsentToken: ""
        property var jobsModel: null
    }
    QtObject {
        id: optionalBrowser
        property string errorText: ""
        signal errorChanged
        function open() {
        }
    }
    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testCase.presenter
            playlists: testCase.backend
        }
    }
    function init() {
        scenario = scenarioFactory.createDownloadsScenario();
        verify(scenario && driver && presenter);
        scenario.bindPlaylistExitState(backend);
        exitObserved = false;
        driver.resetLocalScenario();
        backend.ready = true;
        backend.busy = false;
        backend.interactionAllowed = true;
        backend.canAddFiles = true;
        backend.canReplaceFile = true;
        backend.hasPendingChange = false;
        backend.statusText = "Playlists salvas";
        backend.selectedPlaylistId = "1";
        backend.playlists = [
            {
                id: "1",
                name: "<b>Favoritas 日本</b>",
                count: 3
            },
            {
                id: "2",
                name: "Segunda",
                count: 0
            }
        ];
        backend.entries = [
            {
                id: "11",
                title: "<b>Faixa original</b>",
                path: "C:/Music/duplicate.mp3"
            },
            {
                id: "12",
                title: "<b>Faixa original</b>",
                path: "C:/Music/duplicate.mp3"
            },
            {
                id: "13",
                title: "Outra faixa",
                path: "C:/Music/other.mp3"
            }
        ];
        backend.calls = [];
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.width = 720;
        shell.height = 520;
        shell.requestActivate();
        tryVerify(function () {
            return testCase.shell.active;
        });
    }
    function cleanup() {
        if (shell)
            shell.destroy();
        shell = null;
        wait(0);
        if (scenario)
            verify(scenarioFactory.releaseDownloadsScenario(scenario));
        scenario = null;
    }
    function clickVisible(item) {
        const page = findChild(shell, "playlists-panel");
        if (page && page.embedded) {
            page.revealFocusedItem(item);
            verify(waitForPolish(shell));
            const point = item.mapToItem(shell.contentItem, 0, 0);
            verify(point.x >= 0 && point.y >= 0);
            verify(point.x + item.width <= shell.width + 1);
            verify(point.y + item.height <= findChild(shell, "player-footer").y + 1 || !shell.isInside(item, page.contentItem));
        }
        mouseClick(item);
    }
    function openPanel() {
        const opener = findChild(shell, "playlists");
        verify(opener && opener.visible);
        clickVisible(opener);
        tryCompare(findChild(shell, "playlists-panel"), "visible", true);
        verify(waitForPolish(shell));
    }
    function rowAt(listName, index) {
        const list = findChild(shell, listName);
        verify(list);
        list.positionViewAtIndex(index, ListView.Contain);
        verify(waitForPolish(shell));
        tryVerify(function () {
            return list.itemAtIndex(index) !== null;
        });
        return list.itemAtIndex(index);
    }
    function submitName(value) {
        const field = findChild(shell, "playlist-name");
        verify(field);
        field.text = value;
        clickVisible(findChild(shell, "playlist-name-submit"));
    }
    function lastCall(action) {
        verify(backend.calls.length > 0);
        const call = backend.calls[backend.calls.length - 1];
        compare(call.action, action);
        return call;
    }
    function test_openingIsInertAndExternalTextIsPlain() {
        const state = presenter.playbackState;
        openPanel();
        compare(backend.calls.length, 0);
        const name = findChild(rowAt("playlists-list", 0), "playlist-row-name");
        const title = findChild(rowAt("playlist-entries", 0), "playlist-entry-title");
        compare(name.text, "<b>Favoritas 日本</b>");
        compare(name.textFormat, Text.PlainText);
        compare(title.text, "<b>Faixa original</b>");
        compare(title.textFormat, Text.PlainText);
        compare(presenter.playbackState, state);
    }
    function test_createAndSaveQueueHaveSeparateExplicitActions() {
        openPanel();
        clickVisible(findChild(shell, "playlist-create"));
        compare(backend.calls.length, 0);
        submitName("Nova 日本");
        compare(lastCall("create").args[0], "Nova 日本");
        clickVisible(findChild(shell, "playlist-save-queue"));
        compare(backend.calls.length, 1);
        submitName("Fila favorita");
        compare(lastCall("saveQueue").args[0], "Fila favorita");
    }
    function test_renameCapturesPlaylistAndDoesNotRetargetChangedSelection() {
        openPanel();
        clickVisible(findChild(shell, "playlist-rename"));
        findChild(shell, "playlist-name").text = "Renomeada";
        backend.selectedPlaylistId = "2";
        wait(0);
        const submit = findChild(shell, "playlist-name-submit");
        const dialog = findChild(shell, "playlist-name-dialog");
        verify(!dialog.visible || !submit.enabled);
        compare(backend.calls.length, 0);
    }
    function test_roomVisibilityToggleIsNamedAndSetsTheSelectedList() {
        openPanel();
        const toggle = findChild(shell, "playlist-room-visibility");
        verify(toggle !== null);
        compare(toggle.text, "Visível para a sala");
        compare(toggle.Accessible.name, "Visível para a sala");
        compare(toggle.checked, false);
        compare(backend.calls.length, 0);
        clickVisible(toggle);
        const call = lastCall("visibility");
        compare(call.args[0], "1");
        compare(call.args[1], true);
        backend.playlists = [
            {
                id: "1",
                name: "<b>Favoritas 日本</b>",
                count: 3,
                visibleToRoom: true
            },
            backend.playlists[1]];
        tryCompare(toggle, "checked", true);
        clickVisible(toggle);
        compare(lastCall("visibility").args[1], false);
        backend.canAddFiles = true;
        backend.hasPendingChange = true;
        tryCompare(toggle, "enabled", false);
    }
    function test_deleteRequiresConfirmationForTheCapturedList() {
        openPanel();
        clickVisible(findChild(shell, "playlist-delete"));
        compare(backend.calls.length, 0);
        const dialog = findChild(shell, "playlist-delete-dialog");
        tryCompare(dialog, "visible", true);
        const label = findChild(shell, "playlist-delete-name");
        compare(label.text, "<b>Favoritas 日本</b>");
        compare(label.textFormat, Text.PlainText);
        clickVisible(findChild(shell, "playlist-delete-submit"));
        compare(lastCall("delete").args[0], "1");
    }
    function test_deletedOrChangedPlaylistCannotReuseAConfirmation() {
        openPanel();
        clickVisible(findChild(shell, "playlist-delete"));
        backend.playlists = [
            {
                id: "2",
                name: "Replacement",
                count: 0
            }
        ];
        backend.selectedPlaylistId = "2";
        wait(0);
        const dialog = findChild(shell, "playlist-delete-dialog");
        verify(!dialog.visible || !findChild(shell, "playlist-delete-submit").enabled);
        compare(backend.calls.length, 0);
    }
    function test_duplicateEntriesUseStableIdentityForMoveAndRemove() {
        openPanel();
        clickVisible(rowAt("playlist-entries", 1));
        clickVisible(findChild(shell, "playlist-move-up"));
        const move = lastCall("moveEntry");
        compare(move.args[0], "1");
        compare(move.args[1], "12");
        compare(move.args[2], 0);
        clickVisible(findChild(shell, "playlist-remove-entry"));
        const remove = lastCall("removeEntry");
        compare(remove.args[0], "1");
        compare(remove.args[1], "12");
    }
    function test_queueIngressRequiresItsOwnAction() {
        openPanel();
        const addQueue = findChild(shell, "playlist-add-queue");
        verify(addQueue && addQueue.visible);
        verify(!presenter.queueLoaded);
        verify(!addQueue.enabled);
        clickVisible(rowAt("playlists-list", 1));
        compare(lastCall("select").args[0], "2");
        compare(backend.calls.filter(function (call) {
            return call.action === "enqueue";
        }).length, 0);
        backend.selectedPlaylistId = "1";

        driver.enqueueLocalSelection(["C:/Music/queued.mp3"]);
        verify(presenter.openAudio());
        tryCompare(presenter, "queueLoaded", true);
        tryCompare(addQueue, "enabled", true);
        clickVisible(addQueue);
        compare(lastCall("addQueue").args[0], "1");

        verify(presenter.clearQueue());
        tryCompare(addQueue, "enabled", false);
        const page = findChild(shell, "playlists-panel");
        tryCompare(page, "opened", true, 5000, "Emptying the queue must not navigate away from playlists");
        clickVisible(findChild(shell, "playlist-enqueue"));
        compare(lastCall("enqueue").args[0], "1");
    }
    function test_statusAnnouncesChangedResultsWithoutTakingFocus() {
        driver.clearAccessibleAnnouncements();
        openPanel();
        wait(0);
        compare(driver.accessibleAnnouncementCount(), 0);

        const enqueue = findChild(shell, "playlist-enqueue");
        enqueue.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        compare(lastCall("enqueue").args[0], "1");
        const enqueued = "Faixas adicionadas à fila. Escolha uma faixa para tocar.";
        backend.statusText = enqueued;
        tryVerify(() => driver.accessibleAnnouncementCount() === 1);
        compare(driver.lastAccessibleAnnouncement(), enqueued);
        compare(driver.lastAccessibleAnnouncementPoliteness(), Accessible.Polite);
        verify(enqueue.activeFocus);

        backend.statusText = enqueued;
        backend.busy = true;
        backend.statusText = "Salvando playlist…";
        wait(0);
        compare(driver.accessibleAnnouncementCount(), 1);
        backend.hasPendingChange = true;
        backend.statusText = saveFailure;
        backend.busy = false;
        tryVerify(() => driver.accessibleAnnouncementCount() === 2);
        compare(driver.lastAccessibleAnnouncement(), saveFailure);

        const retry = findChild(shell, "playlists-retry-save");
        retry.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        lastCall("retrySave");
        backend.busy = true;
        backend.statusText = "Salvando playlist…";
        wait(0);
        compare(driver.accessibleAnnouncementCount(), 2);
        backend.hasPendingChange = false;
        backend.statusText = "Playlist salva.";
        backend.busy = false;
        tryVerify(() => driver.accessibleAnnouncementCount() === 3);
        compare(driver.lastAccessibleAnnouncement(), "Playlist salva.");

        keyClick(Qt.Key_Escape);
        tryCompare(findChild(shell, "playlists-panel"), "visible", false);
        backend.statusText = "Suas playlists estão prontas.";
        wait(0);
        openPanel();
        wait(0);
        compare(driver.accessibleAnnouncementCount(), 3);
    }
    function test_addFilesTargetsSelectedEmptyPlaylistWithoutQueueActions() {
        openPanel();
        clickVisible(rowAt("playlists-list", 1));
        compare(lastCall("select").args[0], "2");
        backend.entries = [];
        const addFiles = findChild(shell, "playlist-add-files");
        verify(addFiles && addFiles.visible && addFiles.enabled);
        compare(addFiles.Accessible.name, "Adicionar arquivos…");
        addFiles.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        compare(lastCall("addFiles").args[0], "2");
        compare(backend.calls.length, 2);
    }
    function test_addFilesRequiresPickerAndSelectedPlaylist() {
        openPanel();
        const addFiles = findChild(shell, "playlist-add-files");
        verify(addFiles && addFiles.enabled);
        backend.canAddFiles = false;
        verify(!addFiles.enabled);
        backend.canAddFiles = true;
        backend.selectedPlaylistId = "";
        verify(!addFiles.enabled);
        compare(backend.calls.length, 0);
    }
    function test_locateFileUsesTheSelectedOccurrenceAndRejectsStaleActions() {
        const playback = presenter.playbackState;
        openPanel();
        const panel = findChild(shell, "playlists-panel");
        const locate = findChild(shell, "playlist-locate-file");
        verify(locate && locate.visible, "The playlist panel exposes Localizar arquivo");
        verify(!locate.enabled);
        clickVisible(rowAt("playlist-entries", 1));
        verify(locate.enabled);
        compare(locate.Accessible.name, "Localizar arquivo…");
        verify(locate.width >= 40 && locate.height >= 40);
        const point = locate.mapToItem(panel.contentItem, 0, 0);
        verify(point.x >= 0 && point.y >= 0);
        verify(point.x + locate.width <= panel.contentItem.width + 1);
        verify(point.y + locate.height <= panel.contentItem.height + 1);
        findChild(shell, "playlist-remove-entry").forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Tab);
        tryCompare(locate, "activeFocus", true);
        keyClick(Qt.Key_Return);
        compare(lastCall("replaceFile").args, ["1", "12"]);
        compare(presenter.playbackState, playback);

        backend.busy = true;
        verify(!locate.enabled);
        locate.Accessible.pressAction();
        backend.busy = false;
        backend.canReplaceFile = false;
        verify(!locate.enabled);
        locate.Accessible.pressAction();
        backend.canReplaceFile = true;
        backend.selectedPlaylistId = "2";
        verify(!locate.enabled);
        locate.Accessible.pressAction();
        compare(backend.calls.length, 1);
    }
    function test_busyAndUnconfirmedSaveKeepEditsClosedButOfferRecovery() {
        openPanel();
        backend.busy = true;
        for (const name of ["playlist-create", "playlist-save-queue", "playlist-rename", "playlist-delete", "playlist-add-files", "playlist-add-queue", "playlist-enqueue"])
            verify(!findChild(shell, name).enabled);
        backend.busy = false;
        backend.hasPendingChange = true;
        const retry = findChild(shell, "playlists-retry-save");
        const reload = findChild(shell, "playlists-discard-pending");
        const panel = findChild(shell, "playlists-panel");
        tryCompare(retry, "visible", true);
        tryCompare(reload, "visible", true);
        // Visibility updates queue RowLayout polish. Route pointer events only
        // after both newly shown actions have distinct, usable geometry.
        verify(waitForPolish(shell));
        tryVerify(function () {
            const retryRight = retry.mapToItem(panel.contentItem, retry.width, 0).x;
            const reloadLeft = reload.mapToItem(panel.contentItem, 0, 0).x;
            return retry.width > 0 && reload.width > 0 && reloadLeft >= retryRight;
        });
        compare(backend.calls.length, 0);
        clickVisible(retry);
        lastCall("retrySave");
        clickVisible(reload);
        lastCall("discardPending");
    }
    function openAbandonDialog() {
        const opener = findChild(shell, "playlists-abandon-pending");
        verify(opener);
        tryVerify(function () {
            return opener.visible && opener.enabled;
        });
        verify(waitForPolish(shell));
        clickVisible(opener);
        const dialog = findChild(shell, "playlist-abandon-dialog");
        tryCompare(dialog, "opened", true);
        return dialog;
    }
    function test_pendingPlaylistDiscardRequiresExplicitConfirmation() {
        openPanel();
        const opener = findChild(shell, "playlists-abandon-pending");
        verify(opener);
        verify(!opener.visible || !opener.enabled);
        backend.busy = true;
        backend.hasPendingChange = true;
        verify(!opener.visible || !opener.enabled);
        backend.busy = false;

        const confirmedPlaylists = backend.playlists;
        const confirmedEntries = backend.entries;
        const selection = backend.selectedPlaylistId;
        const dialog = openAbandonDialog();
        const cancel = findChild(shell, "playlist-abandon-cancel");
        const confirm = findChild(shell, "playlist-abandon-confirm");
        tryCompare(cancel, "activeFocus", true);
        compare(backend.calls.length, 0);
        keyClick(Qt.Key_Escape);
        tryCompare(dialog, "visible", false);
        verify(backend.hasPendingChange);
        compare(backend.calls.length, 0);

        openAbandonDialog();
        clickVisible(cancel);
        tryCompare(dialog, "visible", false);
        verify(backend.hasPendingChange);
        compare(backend.calls.length, 0);

        openAbandonDialog();
        clickVisible(confirm);
        tryCompare(dialog, "visible", false);
        lastCall("discardUnsavedChange");
        compare(backend.calls.length, 1);
        verify(!backend.hasPendingChange);
        compare(backend.playlists, confirmedPlaylists);
        compare(backend.entries, confirmedEntries);
        compare(backend.selectedPlaylistId, selection);
        verify(!findChild(shell, "playlist-create").enabled);
        verify(findChild(shell, "playlists-retry-load").visible);
        verify(!presenter.exiting && !presenter.exitCommitted && !exitObserved);
        compare(scenario.localExitCalls(), 0);
        verify(presenter.requestExit());
        tryCompare(presenter, "exitCommitted", true);
        compare(scenario.localExitCalls(), 1);
    }
    function test_pendingPlaylistDiscardRevokesUnavailableConfirmation_data() {
        return [
            {
                tag: "busy",
                field: "busy",
                value: true
            },
            {
                tag: "resolved",
                field: "hasPendingChange",
                value: false
            }
        ];
    }
    function test_pendingPlaylistDiscardRevokesUnavailableConfirmation(data) {
        openPanel();
        backend.hasPendingChange = true;
        const dialog = openAbandonDialog();
        const confirm = findChild(shell, "playlist-abandon-confirm");
        verify(confirm.enabled);
        backend[data.field] = data.value;
        tryVerify(function () {
            return !dialog.visible || !confirm.enabled;
        });
        compare(backend.calls.length, 0);
    }
    function playTrackForExit() {
        driver.enqueueLocalSelection(["C:/Music/playing.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        return presenter.currentOccurrenceId;
    }
    function verifyExitRecovery(occurrence) {
        tryCompare(findChild(shell, "playlists-panel"), "opened", true, 1000);
        compare(presenter.exiting, false);
        compare(presenter.exitCommitted, false);
        compare(scenario.localExitCalls(), 0);
        compare(presenter.playbackState, PlayerPresenter.Playing);
        compare(presenter.currentOccurrenceId, occurrence);
        compare(backend.hasPendingChange, true);
        compare(findChild(shell, "playlists-status").text, saveFailure);
        const retry = findChild(shell, "playlists-retry-save");
        const reload = findChild(shell, "playlists-discard-pending");
        verify(retry.visible && retry.enabled && reload.visible && reload.enabled);
        tryCompare(retry, "activeFocus", true);
        compare(backend.calls.length, 0);
    }
    function test_failedPlaylistSaveBlocksExitAndOpensRecovery() {
        const occurrence = playTrackForExit();
        backend.hasPendingChange = true;
        backend.statusText = saveFailure;
        verify(!findChild(shell, "playlists-panel").visible);

        verify(!presenter.requestExit());

        verifyExitRecovery(occurrence);
        compare(exitObserved, false);
        keyClick(Qt.Key_Space);
        lastCall("retrySave");
        compare(backend.calls.length, 1);
    }
    function test_playlistSaveFailureDuringExitRestoresRecovery() {
        const occurrence = playTrackForExit();
        backend.hasPendingChange = true;
        backend.busy = true;

        verify(presenter.requestExit());
        verify(!presenter.exiting && !presenter.exitCommitted && !exitObserved);
        compare(scenario.localExitCalls(), 0);
        verify(!findChild(shell, "playlists-panel").visible);
        backend.statusText = saveFailure;
        backend.busy = false;

        verifyExitRecovery(occurrence);
        compare(exitObserved, false);
    }
    function test_confirmedPlaylistSaveAllowsExit() {
        backend.hasPendingChange = true;
        backend.busy = true;

        verify(presenter.requestExit());
        verify(!presenter.exiting && !presenter.exitCommitted && !exitObserved);
        compare(scenario.localExitCalls(), 0);
        backend.hasPendingChange = false;
        backend.busy = false;

        tryCompare(presenter, "exitCommitted", true, 1000);
        compare(exitObserved, true);
        compare(scenario.localExitCalls(), 1);
        verify(!findChild(shell, "playlists-panel").visible);
        compare(backend.calls.length, 0);
    }
    function test_unreadableLibraryOffersReloadWithoutCreatingOrDiscardingLists() {
        openPanel();
        backend.ready = false;
        backend.statusText = "Não foi possível abrir as playlists.";
        verify(!findChild(shell, "playlist-create").enabled);
        clickVisible(findChild(shell, "playlists-retry-load"));
        lastCall("retryLoad");
        compare(backend.calls.length, 1);
    }
    function test_modalBlocksPlayerKeysAndRestoresFocus() {
        openPanel();
        compare(shell.windowShortcutsEnabled, false);
        const muted = presenter.muted;
        clickVisible(findChild(shell, "playlist-create"));
        findChild(shell, "playlist-name").forceActiveFocus();
        keyClick(Qt.Key_M);
        compare(presenter.muted, muted);
        keyClick(Qt.Key_Escape);
        keyClick(Qt.Key_Escape);
        tryCompare(findChild(shell, "playlists-panel"), "visible", false);
        tryCompare(findChild(shell, "playlists"), "activeFocus", true);
        compare(shell.windowShortcutsEnabled, true);
    }
    function test_keyboardSelectsOffscreenPlaylistAndEntryBeforeInvokingRowAction() {
        const count = 40;
        backend.playlists = Array.from({
            length: count
        }, (_, i) => ({
                    id: String(i + 1),
                    name: "Playlist " + (i + 1),
                    count: count
                }));
        backend.entries = Array.from({
            length: count
        }, (_, i) => ({
                    id: String(101 + i),
                    title: "Faixa repetida",
                    path: "C:/Music/duplicate.mp3"
                }));
        driver.enqueueLocalSelection(["C:/Music/playing.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        const volume = presenter.volume;
        const occurrence = presenter.currentOccurrenceId;
        const loads = driver.localLoadCount();
        const plays = driver.localPlayCount();
        const seeks = driver.localSeekCount();

        openPanel();
        const panel = findChild(shell, "playlists-panel");
        const lists = findChild(shell, "playlists-list");
        const entries = findChild(shell, "playlist-entries");
        tryCompare(lists, "activeFocus", true);
        compare(shell.windowShortcutsEnabled, false);
        compare(backend.calls.length, 0);
        keyClick(Qt.Key_Down);
        tryCompare(lists, "currentIndex", 1);
        compare(backend.selectedPlaylistId, "1");
        keyClick(Qt.Key_End);
        tryCompare(lists, "currentIndex", count - 1);
        tryVerify(function () {
            const row = lists.itemAtIndex(count - 1);
            return row && row.mapToItem(lists, 0, row.height).y <= lists.height + 1;
        });
        keyClick(Qt.Key_Return);
        tryCompare(backend, "selectedPlaylistId", "40");
        compare(lastCall("select").args[0], "40");

        keyClick(Qt.Key_Tab);
        tryCompare(findChild(shell, "playlist-rename"), "activeFocus", true);
        keyClick(Qt.Key_Tab);
        tryCompare(findChild(shell, "playlist-delete"), "activeFocus", true);
        keyClick(Qt.Key_Tab);
        tryCompare(findChild(shell, "playlist-room-visibility"), "activeFocus", true);
        keyClick(Qt.Key_Tab);
        tryCompare(entries, "activeFocus", true);
        keyClick(Qt.Key_End);
        tryCompare(entries, "currentIndex", count - 1);
        compare(panel.selectedEntryId, "");
        const reordered = backend.entries.slice();
        reordered.splice(2, 0, reordered.pop());
        backend.entries = reordered;
        tryCompare(entries, "currentIndex", 2);
        tryCompare(entries, "activeFocus", true);
        keyClick(Qt.Key_Space);
        tryCompare(panel, "selectedEntryId", "140");
        keyClick(Qt.Key_Tab);
        tryCompare(findChild(shell, "playlist-move-up"), "activeFocus", true);
        keyClick(Qt.Key_Return);
        compare(lastCall("moveEntry").args, ["40", "140", 1]);
        compare(backend.calls.length, 2);
        compare(presenter.playbackState, PlayerPresenter.Playing);
        compare(presenter.volume, volume);
        compare(presenter.currentOccurrenceId, occurrence);
        compare(driver.localLoadCount(), loads);
        compare(driver.localPlayCount(), plays);
        compare(driver.localSeekCount(), seeks);
    }
    function test_nameDialogExposesAccessibleModeAndKeyboardSubmission() {
        openPanel();
        const cases = [
            {
                button: "playlist-create",
                title: "Nova playlist",
                action: "create"
            },
            {
                button: "playlist-save-queue",
                title: "Salvar fila como playlist",
                action: "saveQueue"
            },
            {
                button: "playlist-rename",
                title: "Renomear playlist",
                action: "rename"
            }
        ];
        for (const scenario of cases) {
            clickVisible(findChild(shell, scenario.button));
            const dialog = findChild(shell, "playlist-name-dialog");
            tryCompare(dialog, "visible", true);
            compare(dialog.contentItem.Accessible.role, Accessible.Dialog);
            compare(dialog.contentItem.Accessible.name, scenario.title);
            const field = findChild(shell, "playlist-name");
            tryCompare(field, "activeFocus", true);
            field.text = "Playlist acessível";
            keyClick(Qt.Key_Return);
            const call = lastCall(scenario.action);
            compare(call.args[call.args.length - 1], "Playlist acessível");
            tryCompare(dialog, "visible", false);
        }
    }
    function test_deleteDialogHasAccessibleNameAndEscapeCancelsBeforeExplicitMouseConfirmation() {
        openPanel();
        clickVisible(findChild(shell, "playlist-delete"));
        const dialog = findChild(shell, "playlist-delete-dialog");
        tryCompare(dialog, "visible", true);
        compare(dialog.contentItem.Accessible.role, Accessible.Dialog);
        compare(dialog.contentItem.Accessible.name, "Excluir playlist");
        keyClick(Qt.Key_Escape);
        tryCompare(dialog, "visible", false);
        compare(backend.calls.length, 0);
        clickVisible(findChild(shell, "playlist-delete"));
        tryCompare(dialog, "visible", true);
        clickVisible(findChild(shell, "playlist-delete-submit"));
        compare(lastCall("delete").args[0], "1");
        tryCompare(dialog, "visible", false);
    }
    function test_minimumWindowKeepsMainActionsVisible() {
        openPanel();
        const panel = findChild(shell, "playlists-panel");
        verify(panel.width <= shell.width && panel.height <= shell.height);
        for (const name of ["playlist-create", "playlist-save-queue", "playlist-add-files", "playlist-enqueue", "playlists-close"]) {
            const item = findChild(shell, name);
            const point = item.mapToItem(panel.contentItem, 0, 0);
            verify(point.x >= 0 && point.y >= 0);
            verify(point.x + item.width <= panel.contentItem.width + 1);
            verify(point.y + item.height <= panel.contentItem.height + 1);
        }
    }
    function test_remainingOptionalNavigationActionsFitTogether() {
        shell.downloads = optionalDownloads;
        shell.embeddedBrowser = optionalBrowser;
        wait(0);
        verify(waitForPolish(shell));
        const viewport = findChild(shell, "navigation-viewport");
        verify(viewport);
        let previousBottom = -1;
        for (const name of ["now", "queue", "playlists", "downloads", "embedded-browser", "theme-toggle", "preferences"]) {
            const button = findChild(shell, name);
            const boundary = name === "theme-toggle" && shell.inlinePages ? findChild(shell, "workspace-header") : viewport;
            verify(button && button.visible);
            if (button.enabled) {
                button.forceActiveFocus(Qt.TabFocusReason);
                tryVerify(() => button.activeFocus);
            }
            tryVerify(() => {
                const point = button.mapToItem(boundary, 0, 0);
                return point.x >= 0 && point.y >= 0 && point.x + button.width <= boundary.width && point.y + button.height <= boundary.height;
            }, 5000, name + " must be reachable inside its actual clipped viewport or header");
            if (boundary === viewport) {
                const y = button.mapToItem(viewport.contentItem, 0, 0).y;
                verify(y >= previousBottom, name + " y=" + y + " previousBottom=" + previousBottom);
                previousBottom = y + button.height;
            }
        }
    }
}
