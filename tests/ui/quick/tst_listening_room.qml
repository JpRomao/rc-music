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
            isSelf: false
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
            isSelf: false
        });
        fakeRoom.members.append({
            fingerprint: "fp-eu",
            nickname: "Eu",
            avatarSource: "qrc:/rc_music/avatars/avatar-04.svg",
            isHost: false,
            isSelf: true
        });
        tryVerify(() => badge.visible);
        compare(badge.text, "Na sala de Bia · 2 pessoas");
        openPage();
        compare(visibleChild("room-leave").text, "Sair da sala");
        fakeRoom.role = 0;
        fakeRoom.state = "idle";
        tryVerify(() => !badge.visible);
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

    function test_anErrorIsShownAndCanBeDismissed() {
        fakeRoom.errorText = "A sala está cheia";
        createShell();
        openPage();
        compare(visibleChild("room-error").text, "A sala está cheia");
        mouseClick(visibleChild("room-error-dismiss"));
        tryVerify(() => !findChild(shell, "room-error").visible);
    }
}
