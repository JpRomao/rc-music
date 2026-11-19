import QtQuick
import QtQuick.Controls
import QtTest
import MusicPlayer.M0

// "Ouvir junto" (ADR-0022, S7.3) over a fake room: no network, no audio.
TestCase {
    id: testCase
    name: "ListeningRoomPage"
    when: windowShown
    readonly property PlayerPresenter localPresenter: testLocalPlayerPresenter // qmllint disable unqualified
    property var shell

    QtObject {
        id: fakeRoom
        property bool available: true
        property int role: 0
        property string state: "idle"
        property bool busy: false
        property string inviteLink: ""
        property ListModel members: ListModel {}
        property ListModel pendingRequests: ListModel {}
        property ListModel queue: ListModel {}
        property ListModel roles: ListModel {}
        property ListModel knownPeople: ListModel {}
        property int initialRoleId: 1
        readonly property int permissionCount: 7
        property int permissions: 0
        property bool canSkip: false
        property bool canPause: false
        property bool canEditQueue: false
        property ListModel catalog: ListModel {}
        property bool canAddFromRoom: false
        property bool canAddPlaylist: false
        property bool canUploadOwnTracks: false
        property bool uploading: false
        property bool roomPlaying: false
        property bool roomHasCurrent: false
        property bool rosterUnsaved: false
        readonly property int memberCount: members.count
        property string hostNickname: ""
        property int maxListeners: 5
        property bool untilRevoked: false
        property string errorText: ""
        property bool privacyNoticeRequired: false
        readonly property int minimumListeners: 1
        readonly property int maximumListeners: 10
        readonly property int defaultListeners: 5
        property var calls: []
        function record(call) {
            calls = calls.concat([call]);
        }
        function openRoom(listeners, revocable) {
            record("open:" + listeners + ":" + revocable);
            if (listeners < minimumListeners || listeners > maximumListeners)
                return false;
            role = 1;
            state = "open";
            inviteLink = "https://sala.example/r#convite";
            return true;
        }
        function closeRoom() {
            record("close");
            role = 0;
            state = "idle";
        }
        function revoke() {
            record("revoke");
        }
        function copyLink() {
            record("copy");
            return true;
        }
        function approve(fingerprint) {
            record("approve:" + fingerprint);
            return true;
        }
        function reject(fingerprint) {
            record("reject:" + fingerprint);
            return true;
        }
        function join(link) {
            record("join:" + link);
            return true;
        }
        function leave() {
            record("leave");
            role = 0;
            state = "idle";
        }
        function acknowledgePrivacyNotice() {
            record("privacy");
            privacyNoticeRequired = false;
        }
        function dismissError() {
            errorText = "";
        }
        function roomAddItem(entryId) {
            record("add:" + entryId);
            return true;
        }
        function roomAddPlaylist(playlistId) {
            record("addPlaylist:" + playlistId);
            return true;
        }
        function roomUploadOwnTrack() {
            record("upload");
            uploading = true;
            return true;
        }
    }

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testCase.localPresenter
            room: fakeRoom
        }
    }

    function init() {
        fakeRoom.available = true;
        fakeRoom.role = 0;
        fakeRoom.state = "idle";
        fakeRoom.busy = false;
        fakeRoom.inviteLink = "";
        fakeRoom.members.clear();
        fakeRoom.pendingRequests.clear();
        fakeRoom.queue.clear();
        fakeRoom.hostNickname = "";
        fakeRoom.untilRevoked = false;
        fakeRoom.errorText = "";
        fakeRoom.privacyNoticeRequired = false;
        fakeRoom.catalog.clear();
        fakeRoom.canAddFromRoom = false;
        fakeRoom.canAddPlaylist = false;
        fakeRoom.canUploadOwnTracks = false;
        fakeRoom.uploading = false;
        fakeRoom.calls = [];
    }
    function cleanup() {
        if (shell)
            shell.destroy();
        shell = null;
        wait(0);
    }
    function createShell() {
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.width = 1100;
        shell.height = 720;
        shell.requestActivate();
        tryVerify(() => testCase.shell.active);
        return shell;
    }
    function openPage() {
        const button = findChild(shell, "room");
        verify(button);
        tryVerify(() => button.visible);
        mouseClick(button);
        const panel = findChild(shell, "room-panel");
        tryVerify(() => panel.visible);
        return panel;
    }
    // Repeater delegates are visual children only, so findChild (the object tree) misses them.
    function findItem(parent, name) {
        const items = parent.children || [];
        for (let index = 0; index < items.length; ++index) {
            if (items[index].objectName === name)
                return items[index];
            const found = findItem(items[index], name);
            if (found)
                return found;
        }
        return null;
    }
    function visibleChild(name) {
        const item = findChild(shell, name) || findItem(shell.contentItem, name);
        verify(item, name);
        tryVerify(() => item.visible, 1000, name);
        return item;
    }

    function test_outsideARoomItOffersCreateAndJoin() {
        createShell();
        openPage();
        compare(shell.currentPage, "room");
        compare(visibleChild("room-create").text, "Criar sala");
        compare(visibleChild("room-join-start").text, "Entrar com link");
    }

    function test_withoutAnOriginThePageSaysTheRoomIsUnavailable() {
        fakeRoom.available = false;
        createShell();
        openPage();
        compare(visibleChild("room-unavailable").text, "Sala indisponível neste build");
        verify(!findChild(shell, "room-create").visible);
    }

    function test_theHostFormOffersOneToTenListenersAndTwoDurations() {
        createShell();
        openPage();
        mouseClick(visibleChild("room-create"));
        const capacity = visibleChild("room-capacity");
        compare(capacity.count, 10);
        compare(capacity.currentIndex, 4);
        compare(capacity.textAt(0), "1 pessoa");
        compare(capacity.textAt(9), "10 pessoas");
        const duration = visibleChild("room-duration");
        compare(duration.count, 2);
        compare(duration.currentIndex, 0);
        compare(duration.currentText, "Termina quando eu fechar o app");
        compare(duration.textAt(1), "Vale até eu revogar");
        mouseClick(visibleChild("room-open"));
        compare(fakeRoom.calls, ["open:5:false"]);
        tryVerify(() => visibleChild("room-invite-link").text === "https://sala.example/r#convite");
    }

    function test_theChosenCapacityAndDurationReachTheRoom() {
        createShell();
        openPage();
        mouseClick(visibleChild("room-create"));
        const capacity = visibleChild("room-capacity");
        capacity.activated(9);
        const duration = visibleChild("room-duration");
        duration.activated(1);
        compare(capacity.currentIndex, 9);
        compare(duration.currentIndex, 1);
        mouseClick(visibleChild("room-open"));
        compare(fakeRoom.calls, ["open:10:true"]);
    }

    function test_joiningSendsThePastedLink() {
        createShell();
        openPage();
        mouseClick(visibleChild("room-join-start"));
        const field = visibleChild("room-link-field");
        tryVerify(() => field.activeFocus);
        field.text = "https://sala.example/r#abc";
        mouseClick(visibleChild("room-join"));
        compare(fakeRoom.calls, ["join:https://sala.example/r#abc"]);
    }

    function joinAsGuest() {
        fakeRoom.role = 2;
        fakeRoom.state = "joined";
        fakeRoom.hostNickname = "Joao";
        fakeRoom.members.append({
            fingerprint: "fp-host",
            nickname: "Joao",
            avatarSource: "qrc:/rc_music/avatars/avatar-04.svg",
            isHost: true,
            isSelf: false,
            hasSyncDrift: false,
            syncDriftMs: 0
        });
        fakeRoom.queue.append({
            itemId: "7",
            title: "Primeira",
            artist: "Artista",
            addedBy: "Joao",
            avatarSource: "qrc:/rc_music/avatars/avatar-04.svg",
            available: true,
            current: true
        });
        fakeRoom.queue.append({
            itemId: "8",
            title: "Segunda",
            artist: "Artista",
            addedBy: "Bia",
            avatarSource: "qrc:/rc_music/avatars/avatar-02.svg",
            available: false,
            current: false
        });
    }

    // S8.2: the guest's queue shows the room's items, "Carregando…" until a file arrives, and who added each.
    function test_aGuestQueueShowsLoadingItemsWithTheirAuthor() {
        joinAsGuest();
        createShell();
        shell.setQueueOpen(true);
        const panel = findChild(shell, "queue-panel");
        tryVerify(() => panel.visible);
        const list = visibleChild("room-queue-list");
        compare(list.count, 2);
        tryVerify(() => findItem(list, "room-queue-row-8") !== null);
        const loadingRow = findItem(list, "room-queue-row-8");
        const loading = findItem(loadingRow, "room-queue-loading");
        verify(loading.visible);
        compare(loading.text, "Carregando…");
        const avatar = findItem(loadingRow, "room-queue-avatar");
        verify(avatar.visible);
        compare(String(avatar.source), "qrc:/rc_music/avatars/avatar-02.svg");
        compare(findItem(loadingRow, "room-queue-title").text, "Segunda");
        verify(loadingRow.Accessible.description.indexOf("Carregando…") >= 0);
        verify(loadingRow.Accessible.description.indexOf("Bia") >= 0);
        const readyRow = findItem(list, "room-queue-row-7");
        verify(!findItem(readyRow, "room-queue-loading").visible);
        compare(String(findItem(readyRow, "room-queue-avatar").source), "qrc:/rc_music/avatars/avatar-04.svg");

        fakeRoom.queue.setProperty(1, "available", true);
        tryVerify(() => !findItem(findItem(list, "room-queue-row-8"), "room-queue-loading").visible);
    }

    // S8.2: on a guest, shuffle and repeat belong to the host.
    function test_aGuestCannotChangeShuffleOrRepeat() {
        joinAsGuest();
        createShell();
        shell.setQueueOpen(true);
        tryVerify(() => findChild(testCase.shell, "queue-panel").visible);
        for (const name of ["queue-shuffle", "queue-repeat", "player-shuffle", "player-repeat"]) {
            const button = findChild(shell, name);
            verify(button, name);
            verify(!button.enabled, name);
            compare(button.disabledReason, "O host controla", name);
            verify(button.toolTipText.indexOf("O host controla") >= 0, name);
            compare(button.Accessible.description, "O host controla", name);
        }
        const shuffled = localPresenter.shuffleQueueEnabled;
        const repeated = localPresenter.repeatQueueEnabled;
        keyClick(Qt.Key_F1);
        keyClick(Qt.Key_F2);
        compare(localPresenter.shuffleQueueEnabled, shuffled);
        compare(localPresenter.repeatQueueEnabled, repeated);

        // Back outside the room, they work again.
        fakeRoom.role = 0;
        fakeRoom.state = "idle";
        verify(findChild(shell, "queue-shuffle").enabled);
        verify(findChild(shell, "queue-repeat").enabled);
        compare(findChild(shell, "player-shuffle").disabledReason, "");
        compare(findChild(shell, "player-repeat").disabledReason, "");
    }

    // S9.1: a guest's player belongs to the room; its own queue waits until it leaves.
    function test_aGuestCannotOpenOrAddLocalFiles() {
        joinAsGuest();
        createShell();
        shell.setQueueOpen(true);
        tryVerify(() => findChild(testCase.shell, "queue-panel").visible);
        const add = findChild(shell, "queue-add");
        verify(add);
        verify(!add.enabled);
        const reason = "Durante a sala você ouve a fila do anfitrião. A sua fila volta quando você sair.";
        compare(add.Accessible.description, reason);
        verify(add.ToolTip.text.indexOf(reason) >= 0);
        keySequence("Ctrl+O");
        verify(!shell.audioSelectionPending);
        verify(!localPresenter.selectingAudioFiles);

        fakeRoom.role = 0;
        fakeRoom.state = "idle";
        verify(findChild(shell, "queue-add").enabled);
        compare(findChild(shell, "queue-add").Accessible.description, "");
    }

    function test_aPendingRequestOffersAcceptAndRejectByKeyboard() {
        fakeRoom.role = 1;
        fakeRoom.state = "open";
        fakeRoom.inviteLink = "https://sala.example/r#convite";
        fakeRoom.pendingRequests.append({
            fingerprint: "fp-ana",
            nickname: "Ana",
            avatarSource: "qrc:/rc_music/avatars/avatar-02.svg",
            isHost: false,
            isSelf: false
        });
        createShell();
        openPage();
        compare(visibleChild("room-request-name").text, "Ana quer entrar");
        const accept = visibleChild("room-approve");
        const refuse = visibleChild("room-reject");
        compare(accept.text, "Aceitar");
        compare(refuse.text, "Recusar");
        verify(accept.activeFocusOnTab);
        verify(refuse.activeFocusOnTab);
        // The page first focuses its main action, the invite link.
        tryVerify(() => findChild(testCase.shell, "room-copy-link").activeFocus);
        accept.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        compare(fakeRoom.calls, ["approve:fp-ana"]);
        keyClick(Qt.Key_Tab);
        tryVerify(() => refuse.activeFocus);
        keyClick(Qt.Key_Space);
        compare(fakeRoom.calls, ["approve:fp-ana", "reject:fp-ana"]);
    }

    function test_thePrivacyNoticeAppearsOnceOnFirstUse() {
        fakeRoom.privacyNoticeRequired = true;
        createShell();
        openPage();
        mouseClick(visibleChild("room-create"));
        const notice = visibleChild("room-privacy-notice");
        verify(notice.text.indexOf("endereço IP") >= 0);
        verify(!findChild(shell, "room-capacity").visible);
        const understood = visibleChild("room-privacy-accept");
        tryVerify(() => understood.activeFocus);
        keyClick(Qt.Key_Return);
        compare(fakeRoom.calls, ["privacy"]);
        visibleChild("room-capacity");
        mouseClick(visibleChild("room-back"));
        mouseClick(visibleChild("room-join-start"));
        visibleChild("room-link-field");
        verify(!findChild(shell, "room-privacy-notice").visible);
        compare(fakeRoom.calls, ["privacy"]);
    }

    function test_theTransportBadgeAppearsOnlyDuringARoom() {
        createShell();
        const badge = findChild(shell, "player-room-badge");
        verify(badge);
        verify(!badge.visible);
        fakeRoom.role = 2;
        fakeRoom.state = "joined";
        fakeRoom.hostNickname = "Bia";
        fakeRoom.members.append({
            fingerprint: "fp-bia",
            nickname: "Bia",
            avatarSource: "qrc:/rc_music/avatars/avatar-03.svg",
            isHost: true,
            isSelf: false,
            hasSyncDrift: false,
            syncDriftMs: 0
        });
        fakeRoom.members.append({
            fingerprint: "fp-eu",
            nickname: "Eu",
            avatarSource: "qrc:/rc_music/avatars/avatar-04.svg",
            isHost: false,
            isSelf: true,
            hasSyncDrift: false,
            syncDriftMs: 0
        });
        tryVerify(() => badge.visible);
        compare(badge.text, "Na sala de Bia · 2 pessoas");
        openPage();
        compare(visibleChild("room-leave").text, "Sair da sala");
        fakeRoom.role = 0;
        fakeRoom.state = "idle";
        tryVerify(() => !badge.visible);
    }

    // S9.2: the host's member list shows each person's sync difference, never the fingerprint.
    function test_theHostSeesTheSyncDifferenceOfEachPerson() {
        fakeRoom.role = 1;
        fakeRoom.state = "open";
        fakeRoom.members.append({
            fingerprint: "fp-secreto-do-anfitriao",
            nickname: "Joao",
            avatarSource: "qrc:/rc_music/avatars/avatar-04.svg",
            isHost: true,
            isSelf: true,
            hasSyncDrift: false,
            syncDriftMs: 0
        });
        fakeRoom.members.append({
            fingerprint: "fp-secreto-da-ana",
            nickname: "Ana",
            avatarSource: "qrc:/rc_music/avatars/avatar-02.svg",
            isHost: false,
            isSelf: false,
            hasSyncDrift: true,
            syncDriftMs: 35
        });
        fakeRoom.members.append({
            fingerprint: "fp-secreto-do-caio",
            nickname: "Caio",
            avatarSource: "qrc:/rc_music/avatars/avatar-03.svg",
            isHost: false,
            isSelf: false,
            hasSyncDrift: true,
            syncDriftMs: -120
        });
        createShell();
        openPage();
        const drifts = [];
        const names = [];
        const collect = item => {
            if (item.objectName === "room-member-sync-drift" && item.visible)
                drifts.push(item.text);
            if (item.objectName === "room-member-name")
                names.push(item.text);
            for (let i = 0; item.children && i < item.children.length; ++i)
                collect(item.children[i]);
        };
        collect(testCase.shell.contentItem);
        compare(names.length, 3);
        compare(drifts, ["diferença de sincronia: +35 ms", "diferença de sincronia: -120 ms"]);
        verify(names.concat(drifts).every(text => text.indexOf("fp-") < 0));
    }

    // S9.1: a guest adjusts its output offset on the room page, by keyboard.
    function test_aGuestAdjustsTheOutputOffsetByKeyboard() {
        verify(localPresenter.setRoomOutputOffsetMs(0) || localPresenter.roomOutputOffsetMs === 0);
        joinAsGuest();
        createShell();
        openPage();
        const field = visibleChild("room-guest-output-offset");
        compare(field.Accessible.name, "Ajuste de sincronia");
        verify(field.Accessible.description.indexOf("Bluetooth") >= 0);
        compare(field.displayText, "0 ms");
        // The page focuses "Sair da sala" first; the offset is the next Tab stop.
        tryVerify(() => findChild(testCase.shell, "room-leave").activeFocus);
        keyClick(Qt.Key_Tab);
        tryVerify(() => field.activeFocus);
        keyClick(Qt.Key_Up);
        compare(localPresenter.roomOutputOffsetMs, 10);
        compare(field.displayText, "+10 ms");
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Down);
        compare(localPresenter.roomOutputOffsetMs, -10);
        compare(field.displayText, "−10 ms");
        // A change made elsewhere (the preferences page) shows here too.
        verify(localPresenter.setRoomOutputOffsetMs(250));
        compare(field.value, 250);
        verify(localPresenter.setRoomOutputOffsetMs(0));
    }

    // S11.2: a guest sees the host's visible playlists and adds one track or a whole list, by keyboard too,
    // only with the permissions.
    function catalogRow(kind, playlistId, entryId, name, title, artist, entryCount) {
        return {
            kind: kind,
            playlistId: playlistId,
            entryId: entryId,
            name: name,
            title: title,
            artist: artist,
            entryCount: entryCount
        };
    }

    function test_aGuestAddsFromTheHostsPlaylists() {
        joinAsGuest();
        fakeRoom.catalog.append(catalogRow("playlist", "5", "", "Festa", "", "", 2));
        fakeRoom.catalog.append(catalogRow("entry", "5", "6", "", "Primeira", "Artista", 0));
        fakeRoom.catalog.append(catalogRow("entry", "5", "7", "", "", "", 0));
        createShell();
        openPage();
        // The page first focuses "Sair da sala".
        tryVerify(() => findChild(testCase.shell, "room-leave").activeFocus);
        compare(visibleChild("room-catalog-title").text, "Playlists do host");
        compare(visibleChild("room-catalog-playlist-name").text, "Festa");
        const first = findItem(shell.contentItem, "room-catalog-entry-6");
        verify(first);
        compare(findItem(first, "room-catalog-entry-title").text, "Primeira");
        compare(first.Accessible.name, "Primeira");
        compare(first.Accessible.description, "Artista");
        compare(findItem(findItem(shell.contentItem, "room-catalog-entry-7"), "room-catalog-entry-title").text, "Sem título");

        // Without the permissions the lists show and the buttons do not.
        const addAll = findItem(shell.contentItem, "room-catalog-add-all");
        const addOne = findItem(first, "room-catalog-add");
        verify(addAll);
        verify(addOne);
        verify(!addAll.visible);
        verify(!addOne.visible);
        verify(visibleChild("room-catalog-no-permission").text.indexOf("permissão") >= 0);

        fakeRoom.canAddFromRoom = true;
        fakeRoom.canAddPlaylist = true;
        tryVerify(() => addAll.visible && addOne.visible);
        verify(!findChild(shell, "room-catalog-no-permission").visible);
        compare(addAll.text, "Adicionar todas");
        compare(addAll.Accessible.name, "Adicionar todas as músicas de Festa");
        compare(addOne.text, "Adicionar");
        compare(addOne.Accessible.name, "Adicionar Primeira");
        mouseClick(addOne);
        addAll.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        compare(fakeRoom.calls, ["add:6", "addPlaylist:5"]);
        keyClick(Qt.Key_Tab);
        tryVerify(() => addOne.activeFocus);
        keyClick(Qt.Key_Space);
        compare(fakeRoom.calls, ["add:6", "addPlaylist:5", "add:6"]);

        // A track described later appears without recreating the others or taking the keyboard away.
        fakeRoom.catalog.setProperty(0, "entryCount", 3);
        fakeRoom.catalog.append(catalogRow("entry", "5", "8", "", "Terceira", "", 0));
        tryVerify(() => findItem(testCase.shell.contentItem, "room-catalog-entry-8") !== null);
        verify(findItem(shell.contentItem, "room-catalog-entry-6") === first);
        verify(findItem(shell.contentItem, "room-catalog-add-all") === addAll);
        verify(addOne.activeFocus);
        keyClick(Qt.Key_Tab);
        tryVerify(() => findItem(findItem(testCase.shell.contentItem, "room-catalog-entry-7"), "room-catalog-add").activeFocus);

        // Only "add whole lists": the track buttons go.
        fakeRoom.canAddFromRoom = false;
        tryVerify(() => !addOne.visible);
        verify(addAll.visible);

        // The room ends: the lists go.
        fakeRoom.catalog.clear();
        tryVerify(() => !findChild(testCase.shell, "room-catalog-title").visible);
    }

    // S12.1: "Adicionar música minha" only with the permission, on the room page and in the queue.
    function test_aGuestSendsItsOwnTrackOnlyWithThePermission() {
        joinAsGuest();
        createShell();
        openPage();
        const upload = findChild(shell, "room-upload-own");
        verify(upload);
        verify(!upload.visible);

        fakeRoom.canUploadOwnTracks = true;
        tryVerify(() => upload.visible);
        compare(upload.text, "Adicionar música minha");
        compare(upload.Accessible.name, "Adicionar uma música sua à fila da sala");
        verify(!findChild(shell, "room-uploading").visible);
        mouseClick(upload);
        compare(fakeRoom.calls, ["upload"]);
        compare(visibleChild("room-uploading").text, "Enviando sua música…");
        // A refused file says so.
        fakeRoom.uploading = false;
        fakeRoom.errorText = "Arquivo de áudio inválido";
        compare(visibleChild("room-error").text, "Arquivo de áudio inválido");
        tryVerify(() => !findChild(testCase.shell, "room-uploading").visible);
        fakeRoom.canUploadOwnTracks = false;
        tryVerify(() => !upload.visible);

        // The queue offers it too, with the same gate.
        shell.setQueueOpen(true);
        tryVerify(() => findChild(testCase.shell, "queue-panel").visible);
        const queueUpload = findChild(shell, "queue-room-upload");
        verify(queueUpload);
        verify(!queueUpload.visible);
        fakeRoom.canUploadOwnTracks = true;
        tryVerify(() => queueUpload.visible);
        compare(queueUpload.Accessible.name, "Adicionar uma música sua à fila da sala");
        verify(queueUpload.enabled);
        mouseClick(queueUpload);
        compare(fakeRoom.calls, ["upload", "upload"]);
        fakeRoom.canUploadOwnTracks = false;
        tryVerify(() => !queueUpload.visible);
    }

    function test_anErrorIsShownAndCanBeDismissed() {
        fakeRoom.errorText = "A sala está cheia";
        createShell();
        openPage();
        compare(visibleChild("room-error").text, "A sala está cheia");
        mouseClick(visibleChild("room-error-dismiss"));
        tryVerify(() => !findChild(shell, "room-error").visible);
    }
}
