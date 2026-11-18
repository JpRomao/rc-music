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

    function test_anErrorIsShownAndCanBeDismissed() {
        fakeRoom.errorText = "A sala está cheia";
        createShell();
        openPage();
        compare(visibleChild("room-error").text, "A sala está cheia");
        mouseClick(visibleChild("room-error-dismiss"));
        tryVerify(() => !findChild(shell, "room-error").visible);
    }
}
