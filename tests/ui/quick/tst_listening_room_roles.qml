import QtQuick
import QtQuick.Controls
import QtTest
import MusicPlayer.M0

// S10.1: roles, per-person overrides and moderation on the host's room page, and a guest's controls
// following its permissions, over a fake room: no network, no audio.
TestCase {
    id: testCase
    name: "ListeningRoomRoles"
    when: windowShown
    readonly property PlayerPresenter localPresenter: testLocalPlayerPresenter // qmllint disable unqualified
    property var shell

    QtObject {
        id: fakeRoom
        property bool available: true
        property int role: 1
        property string state: "open"
        property bool busy: false
        property string inviteLink: "https://sala.example/r#convite"
        property ListModel members: ListModel {}
        property ListModel pendingRequests: ListModel {}
        property ListModel queue: ListModel {}
        property ListModel roles: ListModel {}
        property ListModel knownPeople: ListModel {}
        readonly property int memberCount: members.count
        property string hostNickname: "Joao"
        property int maxListeners: 5
        property bool untilRevoked: false
        property string errorText: ""
        property bool privacyNoticeRequired: false
        readonly property int minimumListeners: 1
        readonly property int maximumListeners: 10
        readonly property int defaultListeners: 5
        property int initialRoleId: 1
        readonly property int permissionCount: 7
        property int permissions: 0
        property bool canSkip: false
        property bool canPause: false
        property bool canEditQueue: false
        property bool roomPlaying: false
        property bool roomHasCurrent: false
        property bool rosterUnsaved: false
        property var calls: []
        function record(call) {
            calls = calls.concat([call]);
        }
        function roleRow(roleId) {
            for (let row = 0; row < roles.count; ++row) {
                if (roles.get(row).roleId === roleId)
                    return row;
            }
            return -1;
        }
        function createRole(name, mask) {
            record("create:" + name + ":" + mask);
            const id = roles.count + 1;
            roles.append({
                roleId: id,
                name: name,
                permissions: mask,
                initial: false,
                holders: 0
            });
            return id;
        }
        function renameRole(roleId, name) {
            record("rename:" + roleId + ":" + name);
            roles.setProperty(roleRow(roleId), "name", name);
            return true;
        }
        function setRolePermission(roleId, permission, allowed) {
            record("permission:" + roleId + ":" + permission + ":" + allowed);
            const row = roleRow(roleId);
            const mask = roles.get(row).permissions;
            roles.setProperty(row, "permissions", allowed ? (mask | (1 << permission)) : (mask & ~(1 << permission)));
            return true;
        }
        function removeRole(roleId) {
            record("remove:" + roleId);
            if (roleId === initialRoleId)
                return 1;
            roles.remove(roleRow(roleId));
            return 0;
        }
        function setInitialRole(roleId) {
            record("initial:" + roleId);
            initialRoleId = roleId;
            return true;
        }
        property int personRoleId: 1
        property var personOverrides: [-1, -1, -1, -1, -1, -1, -1]
        function assignRole(fingerprint, roleId) {
            record("assign:" + fingerprint + ":" + roleId);
            personRoleId = roleId;
            return true;
        }
        function setOverride(fingerprint, permission, value) {
            record("override:" + fingerprint + ":" + permission + ":" + value);
            const next = personOverrides.slice();
            next[permission] = value;
            personOverrides = next;
            return true;
        }
        function kick(fingerprint) {
            record("kick:" + fingerprint);
            return true;
        }
        function block(fingerprint) {
            record("block:" + fingerprint);
            return true;
        }
        function unblock(fingerprint) {
            record("unblock:" + fingerprint);
            knownPeople.setProperty(0, "blocked", false);
            return true;
        }
        function person(fingerprint) {
            return {
                known: true,
                roleId: personRoleId,
                blocked: false,
                overrides: personOverrides
            };
        }
        function roomNext() {
            record("next");
            return canSkip;
        }
        function roomPrevious() {
            record("previous");
            return canSkip;
        }
        function roomTogglePlayback() {
            record("toggle");
            return canPause;
        }
        function roomSeekBy(deltaMs) {
            record("seek:" + deltaMs);
            return canSkip;
        }
        function roomRemove(itemId) {
            record("remove-item:" + itemId);
            return canEditQueue;
        }
        function roomMove(itemId, destination) {
            record("move-item:" + itemId + ":" + destination);
            return canEditQueue;
        }
        function openRoom(listeners, revocable) {
            return false;
        }
        function closeRoom() {
        }
        function revoke() {
        }
        function copyLink() {
            return true;
        }
        function approve(fingerprint) {
            return true;
        }
        function reject(fingerprint) {
            return true;
        }
        function join(link) {
            return true;
        }
        function leave() {
        }
        function acknowledgePrivacyNotice() {
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
        fakeRoom.role = 1;
        fakeRoom.state = "open";
        fakeRoom.members.clear();
        fakeRoom.queue.clear();
        fakeRoom.roles.clear();
        fakeRoom.knownPeople.clear();
        fakeRoom.initialRoleId = 1;
        fakeRoom.permissions = 0;
        fakeRoom.canSkip = false;
        fakeRoom.canPause = false;
        fakeRoom.canEditQueue = false;
        fakeRoom.roomPlaying = false;
        fakeRoom.roomHasCurrent = false;
        fakeRoom.rosterUnsaved = false;
        fakeRoom.calls = [];
        fakeRoom.personRoleId = 1;
        fakeRoom.personOverrides = [-1, -1, -1, -1, -1, -1, -1];
        fakeRoom.roles.append({
            roleId: 1,
            name: "Ouvinte",
            permissions: 0,
            initial: true,
            holders: 0
        });
        fakeRoom.roles.append({
            roleId: 2,
            name: "DJ",
            permissions: 127,
            initial: false,
            holders: 0
        });
        fakeRoom.members.append({
            fingerprint: "fp-host",
            nickname: "Joao",
            avatarSource: "qrc:/rc_music/avatars/avatar-04.svg",
            isHost: true,
            isSelf: true,
            hasSyncDrift: false,
            syncDriftMs: 0
        });
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
    function findAll(parent, name, found) {
        const items = parent.children || [];
        for (let index = 0; index < items.length; ++index) {
            if (items[index].objectName === name)
                found.push(items[index]);
            findAll(items[index], name, found);
        }
        return found;
    }
    // The one visible item of that name (Repeater rows repeat their names).
    function shownItem(name) {
        let shown = null;
        tryVerify(() => {
            shown = findAll(testCase.shell.contentItem, name, []).find(item => item.visible) || null;
            return shown !== null;
        }, 1000, name);
        return shown;
    }
    function visibleChild(name) {
        const item = findChild(shell, name) || findItem(shell.contentItem, name) || findItem(shell.overlay, name);
        verify(item, name);
        tryVerify(() => item.visible, 1000, name);
        return item;
    }
    // Reaches an item with Tab alone, as someone without a mouse would.
    function tabTo(name) {
        for (let step = 0; step < 120; ++step) {
            const focused = shell.activeFocusItem;
            if (focused && (focused.objectName === name || (focused.parent && focused.parent.objectName === name)))
                return focused;
            keyClick(Qt.Key_Tab);
        }
        fail("Tab never reached " + name);
        return null;
    }
    function addLia() {
        fakeRoom.members.append({
            fingerprint: "fp-lia",
            nickname: "Lia",
            avatarSource: "qrc:/rc_music/avatars/avatar-02.svg",
            isHost: false,
            isSelf: false,
            hasSyncDrift: false,
            syncDriftMs: 0
        });
    }

    function test_theRoleEditorWorksFromTheKeyboardAndNamesEachPermission() {
        createShell();
        openPage();
        tryVerify(() => findChild(testCase.shell, "room-copy-link").activeFocus);
        tabTo("room-new-role-name");
        keyClick("A");
        for (const letter of "migos")
            keyClick(letter);
        keyClick(Qt.Key_Return);
        compare(fakeRoom.calls, ["create:Amigos:0"]);
        tryVerify(() => findItem(testCase.shell.contentItem, "room-role-3") !== null);

        // Every permission box is reachable by Tab and says which permission of which role it is.
        const names = ["Pular", "Pausar", "Adicionar da sala", "Adicionar playlist", "Editar fila", "Enviar músicas próprias", "Adicionar do YouTube"];
        for (let index = 0; index < 7; ++index) {
            const box = visibleChild("room-role-3-permission-" + index);
            compare(box.Accessible.name, names[index] + " — cargo Amigos");
            compare(box.Accessible.role, Accessible.CheckBox);
            verify(box.activeFocusOnTab);
            compare(visibleChild("room-role-1-permission-" + index).Accessible.name, names[index] + " — cargo Ouvinte");
        }
        findChild(shell, "room-copy-link").forceActiveFocus(Qt.TabFocusReason);
        tabTo("room-role-3-permission-0");
        keyClick(Qt.Key_Space);
        keyClick(Qt.Key_Tab);
        verify(visibleChild("room-role-3-permission-1").activeFocus);
        keyClick(Qt.Key_Space);
        compare(fakeRoom.calls, ["create:Amigos:0", "permission:3:0:true", "permission:3:1:true"]);
        verify(visibleChild("room-role-3-permission-0").checked);
        verify(visibleChild("room-role-3-permission-1").checked);
        verify(!visibleChild("room-role-3-permission-2").checked);

        // Renaming is typing a new name and pressing Enter.
        const name = visibleChild("room-role-name-3");
        compare(name.Accessible.name, "Nome do cargo Amigos");
        name.forceActiveFocus(Qt.TabFocusReason);
        name.selectAll();
        for (const letter of "Turma")
            keyClick(letter);
        keyClick(Qt.Key_Return);
        compare(fakeRoom.calls[fakeRoom.calls.length - 1], "rename:3:Turma");
    }

    function test_theInitialRoleCannotBeRemovedUntilAnotherIsChosen() {
        createShell();
        openPage();
        const remove = visibleChild("room-role-remove-1");
        compare(remove.Accessible.name, "Excluir o cargo Ouvinte");
        remove.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        compare(fakeRoom.calls, ["remove:1"]);
        const message = visibleChild("room-roles-message");
        compare(message.text, "Escolha outro cargo inicial antes");
        compare(message.Accessible.role, Accessible.AlertMessage);
        compare(fakeRoom.roles.count, 2);

        const initial = visibleChild("room-initial-role");
        compare(initial.Accessible.name, "Cargo inicial do convite");
        compare(initial.currentText, "Ouvinte");
        initial.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Down);
        compare(fakeRoom.initialRoleId, 2);
        tryCompare(initial, "currentText", "DJ");
        visibleChild("room-role-remove-1").forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        compare(fakeRoom.roles.count, 1);
        tryVerify(() => !findChild(testCase.shell, "room-roles-message").visible);
    }

    function test_theHostSetsAPersonsRoleAndOverridesAndKicksWithBlock() {
        addLia();
        createShell();
        openPage();
        const actions = shownItem("room-member-actions");
        compare(actions.Accessible.name, "Cargo e permissões de Lia");
        actions.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        const menu = findChild(shell, "room-member-menu");
        verify(menu);
        tryVerify(() => menu.opened);
        const role = findChild(menu.contentItem, "room-member-role");
        tryVerify(() => role.activeFocus);
        compare(role.Accessible.name, "Cargo de Lia");
        compare(role.currentText, "Ouvinte");
        keyClick(Qt.Key_Down);
        compare(fakeRoom.calls, ["assign:fp-lia:2"]);

        // Pausar (permission 1) denied for Lia only.
        keyClick(Qt.Key_Tab);
        const skip = findItem(menu.contentItem, "room-member-override-0");
        tryVerify(() => skip.activeFocus);
        compare(skip.Accessible.name, "Pular para Lia");
        compare(skip.currentText, "Como o cargo");
        keyClick(Qt.Key_Tab);
        const pause = findItem(menu.contentItem, "room-member-override-1");
        tryVerify(() => pause.activeFocus);
        compare(pause.Accessible.name, "Pausar para Lia");
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Down);
        compare(fakeRoom.calls, ["assign:fp-lia:2", "override:fp-lia:1:1", "override:fp-lia:1:0"]);

        const block = findItem(menu.contentItem, "room-member-block");
        compare(block.Accessible.name, "Expulsar e bloquear Lia");
        block.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        compare(fakeRoom.calls[fakeRoom.calls.length - 1], "block:fp-lia");
        tryVerify(() => !menu.opened);
        tryVerify(() => actions.activeFocus);
        // No fingerprint is ever shown.
        verify(findItem(shell.contentItem, "room-member-name").text.indexOf("fp-") < 0);
    }

    function test_aBlockedPersonIsLetBackInFromTheKnownList() {
        fakeRoom.knownPeople.append({
            fingerprint: "fp-caio",
            nickname: "Caio",
            avatarSource: "qrc:/rc_music/avatars/avatar-01.svg",
            roleId: 1,
            roleName: "Ouvinte",
            blocked: true,
            present: false
        });
        createShell();
        openPage();
        compare(visibleChild("room-known-name").text, "Caio · bloqueada");
        const unblock = visibleChild("room-unblock");
        compare(unblock.text, "Desbloquear");
        compare(unblock.Accessible.name, "Desbloquear Caio");
        tryVerify(() => findChild(testCase.shell, "room-copy-link").activeFocus);
        tabTo("room-unblock");
        keyClick(Qt.Key_Space);
        compare(fakeRoom.calls, ["unblock:fp-caio"]);
        tryVerify(() => !findItem(testCase.shell.contentItem, "room-unblock").visible);
        compare(findItem(shell.contentItem, "room-known-name").text, "Caio");
    }

    function joinAsGuest() {
        fakeRoom.role = 2;
        fakeRoom.state = "joined";
        fakeRoom.roomHasCurrent = true;
        fakeRoom.roomPlaying = true;
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
            addedBy: "Joao",
            avatarSource: "qrc:/rc_music/avatars/avatar-04.svg",
            available: true,
            current: false
        });
    }

    function test_aGuestsTransportFollowsItsPermissions() {
        joinAsGuest();
        createShell();
        const pause = findChild(shell, "play-pause");
        const next = findChild(shell, "next");
        const previous = findChild(shell, "previous");
        verify(pause && next && previous);
        for (const button of [pause, next, previous]) {
            verify(!button.enabled, button.objectName);
            compare(button.Accessible.description, "O host não liberou", button.objectName);
            verify(button.toolTipText.indexOf("O host não liberou") >= 0, button.objectName);
        }
        compare(pause.accessibleName, shell.uiContract.actionPause);
        keyClick(Qt.Key_Space);
        compare(fakeRoom.calls, ["toggle"]);

        // The host allows Pausar: the next state enables pause, and it becomes a room intent.
        fakeRoom.canPause = true;
        tryVerify(() => pause.enabled);
        compare(pause.Accessible.description, "Espaço");
        mouseClick(pause);
        compare(fakeRoom.calls, ["toggle", "toggle"]);
        verify(!next.enabled);

        fakeRoom.canSkip = true;
        tryVerify(() => next.enabled && previous.enabled);
        mouseClick(next);
        mouseClick(previous);
        keySequence("Shift+Right");
        compare(fakeRoom.calls, ["toggle", "toggle", "next", "previous", "next"]);

        // An individual override taking Pausar away disables it again.
        fakeRoom.canPause = false;
        tryVerify(() => !pause.enabled);
        compare(pause.disabledReason, "O host não liberou");
    }

    function test_aRosterTheFileDidNotTakeShowsANotice() {
        createShell();
        openPage();
        const notice = findItem(shell.contentItem, "room-roster-unsaved");
        verify(notice);
        verify(!notice.visible);
        fakeRoom.rosterUnsaved = true;
        tryVerify(() => notice.visible);
        compare(notice.text, "Não deu para salvar cargos, ajustes e bloqueios. Eles valem só até a sala fechar.");
        compare(notice.Accessible.role, Accessible.AlertMessage);
        compare(notice.Accessible.name, notice.text);
        fakeRoom.rosterUnsaved = false;
        tryVerify(() => !notice.visible);
    }

    function test_aGuestEditsTheRoomQueueOnlyWithPermission() {
        joinAsGuest();
        createShell();
        shell.setQueueOpen(true);
        tryVerify(() => findChild(testCase.shell, "queue-panel").visible);
        const list = visibleChild("room-queue-list");
        list.currentIndex = 1;
        const remove = findChild(shell, "queue-remove");
        const up = findChild(shell, "queue-move-up");
        verify(!remove.enabled);
        verify(!up.enabled);
        compare(remove.Accessible.description, "O host não liberou");
        compare(list.Accessible.description, "O host não liberou");
        list.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Delete);
        compare(fakeRoom.calls, []);

        fakeRoom.canEditQueue = true;
        tryVerify(() => remove.enabled && up.enabled);
        compare(remove.Accessible.description, "Delete");
        list.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Up, Qt.AltModifier);
        compare(fakeRoom.calls, ["move-item:8:0"]);
        compare(list.currentIndex, 0);
        keyClick(Qt.Key_Delete);
        compare(fakeRoom.calls, ["move-item:8:0", "remove-item:7"]);
    }
}
