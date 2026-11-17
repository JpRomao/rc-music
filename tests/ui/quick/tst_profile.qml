import QtQuick
import QtQuick.Controls
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "ListenerProfile"
    when: windowShown
    readonly property PlayerPresenter localPresenter: testLocalPlayerPresenter // qmllint disable unqualified
    readonly property string invalidNickname: "O apelido precisa ter de 1 a 32 caracteres, numa linha só, sem espaços repetidos nem caracteres especiais."
    property var shell

    QtObject {
        id: fakeProfile
        property bool needsSetup: true
        property bool available: true
        property string nickname: ""
        property int avatarIndex: 7
        readonly property int avatarCount: 20
        readonly property url avatarSource: avatarIndex >= 0 ? "qrc:/rc_music/avatars/avatar-" + String(avatarIndex + 1).padStart(2, "0") + ".svg" : ""
        property bool saveResult: true
        property var nicknames: []
        property var customAvatars: []
        function trimmed(text) {
            return text.replace(/^ +| +$/g, "");
        }
        function acceptsNickname(text) {
            const value = trimmed(text);
            return value.length > 0 && value.length <= 32 && !/ {2}|[\t\n]/.test(value);
        }
        function setNickname(text) {
            nicknames = nicknames.concat([text]);
            if (!saveResult || !acceptsNickname(text))
                return false;
            nickname = trimmed(text);
            needsSetup = false;
            return true;
        }
        function chooseBundledAvatar(index) {
            if (index < 0 || index >= avatarCount)
                return false;
            avatarIndex = index;
            return true;
        }
        function chooseCustomAvatar(file) {
            customAvatars = customAvatars.concat([file]);
            return false;
        }
    }

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testCase.localPresenter
            profile: fakeProfile
        }
    }
    Component {
        id: shellWithoutProfileComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testCase.localPresenter
        }
    }

    function init() {
        fakeProfile.needsSetup = true;
        fakeProfile.available = true;
        fakeProfile.nickname = "";
        fakeProfile.avatarIndex = 7;
        fakeProfile.saveResult = true;
        fakeProfile.nicknames = [];
        fakeProfile.customAvatars = [];
    }
    function cleanup() {
        if (shell)
            shell.destroy();
        shell = null;
        wait(0);
    }
    function createShell(component) {
        shell = createTemporaryObject(component || shellComponent, null);
        verify(shell);
        shell.width = 900;
        shell.height = 640;
        shell.requestActivate();
        tryVerify(() => testCase.shell.active);
        return shell;
    }
    function setupDialog() {
        return findChild(shell, "profile-setup-dialog");
    }
    function waitForSetup() {
        const dialog = setupDialog();
        verify(dialog);
        tryVerify(() => dialog.opened);
        const field = findChild(shell, "profile-nickname");
        tryVerify(() => field.activeFocus);
        return dialog;
    }
    function typeText(text) {
        for (let index = 0; index < text.length; ++index)
            keyClick(text[index]);
    }

    function test_dialogAppearsWithoutProfileAndContinueStartsDisabled() {
        createShell();
        waitForSetup();
        const field = findChild(shell, "profile-nickname");
        const continueButton = findChild(shell, "profile-continue");
        compare(field.text, "");
        verify(!continueButton.enabled);
        verify(!findChild(shell, "profile-nickname-error").visible);
        const current = findChild(shell, "profile-current-avatar");
        compare(String(current.source), "qrc:/rc_music/avatars/avatar-08.svg");
    }
    function test_dialogStaysOpenOnEscape() {
        createShell();
        const dialog = waitForSetup();
        keyClick(Qt.Key_Escape);
        wait(50);
        verify(dialog.opened);
    }
    function test_invalidNicknameExplainsWhyAndValidOneContinues() {
        createShell();
        const dialog = waitForSetup();
        const continueButton = findChild(shell, "profile-continue");
        const error = findChild(shell, "profile-nickname-error");
        typeText("Ana  Bia");
        verify(!continueButton.enabled);
        verify(error.visible);
        compare(error.text, invalidNickname);
        keyClick(Qt.Key_A, Qt.ControlModifier);
        typeText("Ana");
        verify(continueButton.enabled);
        verify(!error.visible);
        mouseClick(continueButton);
        compare(fakeProfile.nicknames, ["Ana"]);
        tryVerify(() => !dialog.visible);
    }
    function test_failedSaveKeepsTheDialogWithAMessage() {
        fakeProfile.saveResult = false;
        createShell();
        const dialog = waitForSetup();
        typeText("Ana");
        keyClick(Qt.Key_Return);
        compare(fakeProfile.nicknames, ["Ana"]);
        verify(dialog.opened);
        verify(findChild(shell, "profile-nickname-error").visible);
    }
    function test_tabWalksFieldAvatarGridAndContinue() {
        createShell();
        waitForSetup();
        const field = findChild(shell, "profile-nickname");
        const picker = findChild(shell, "profile-avatar-picker");
        const continueButton = findChild(shell, "profile-continue");
        typeText("Ana");
        keyClick(Qt.Key_Tab);
        tryVerify(() => picker.activeFocus);
        keyClick(Qt.Key_Tab);
        tryVerify(() => continueButton.activeFocus);
        keyClick(Qt.Key_Tab);
        tryVerify(() => field.activeFocus);
        keyClick(Qt.Key_Backtab);
        tryVerify(() => continueButton.activeFocus);
        keyClick(Qt.Key_Backtab);
        tryVerify(() => picker.activeFocus);
    }
    function test_choosingAnAvatarFromTheGridChangesTheIndex() {
        createShell();
        waitForSetup();
        const picker = findChild(shell, "profile-avatar-picker");
        tryVerify(() => picker.itemAtIndex(3) !== null);
        compare(picker.itemAtIndex(3).objectName, "avatar-option-3");
        mouseClick(picker.itemAtIndex(3));
        compare(fakeProfile.avatarIndex, 3);
        verify(picker.activeFocus);
        keyClick(Qt.Key_Right);
        compare(fakeProfile.avatarIndex, 3);
        keyClick(Qt.Key_Space);
        compare(fakeProfile.avatarIndex, 4);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Return);
        compare(fakeProfile.avatarIndex, 14);
        compare(String(findChild(shell, "profile-current-avatar").source), "qrc:/rc_music/avatars/avatar-15.svg");
    }
    function test_noDialogWhenTheProfileExistsOrIsAbsent() {
        fakeProfile.needsSetup = false;
        fakeProfile.nickname = "Bia";
        createShell();
        wait(50);
        verify(!setupDialog() || !setupDialog().visible);
        shell.destroy();
        shell = null;
        wait(0);
        createShell(shellWithoutProfileComponent);
        wait(50);
        verify(!setupDialog() || !setupDialog().visible);
    }
    function test_settingsProfileSectionChangesNicknameAndAvatar() {
        fakeProfile.needsSetup = false;
        fakeProfile.nickname = "Bia";
        createShell();
        shell.openPreferences();
        const tab = findChild(shell, "preferences-profile");
        tryVerify(() => tab && tab.visible);
        mouseClick(tab);
        const field = findChild(shell, "preferences-profile-nickname");
        tryVerify(() => field.visible);
        compare(field.text, "Bia");
        const save = findChild(shell, "preferences-profile-save");
        verify(!save.enabled);
        field.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_A, Qt.ControlModifier);
        typeText("Bia Lima");
        verify(save.enabled);
        mouseClick(save);
        compare(fakeProfile.nicknames, ["Bia Lima"]);
        compare(fakeProfile.nickname, "Bia Lima");
        const picker = findChild(shell, "preferences-profile-avatar-picker");
        tryVerify(() => picker.visible && picker.itemAtIndex(12) !== null);
        compare(picker.itemAtIndex(12).objectName, "preferences-avatar-option-12");
        mouseClick(picker.itemAtIndex(12));
        compare(fakeProfile.avatarIndex, 12);
        verify(findChild(shell, "preferences-profile-custom-avatar").visible);
    }
    function test_settingsShowsAnUnavailableProfileWithoutEditing() {
        fakeProfile.needsSetup = false;
        fakeProfile.available = false;
        fakeProfile.avatarIndex = -1;
        createShell();
        wait(50);
        verify(!setupDialog() || !setupDialog().visible);
        shell.openPreferences();
        const tab = findChild(shell, "preferences-profile");
        tryVerify(() => tab && tab.visible);
        mouseClick(tab);
        tryVerify(() => findChild(shell, "preferences-profile-unavailable").visible);
        verify(!findChild(shell, "preferences-profile-nickname").visible);
    }
}
