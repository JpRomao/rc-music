import QtQuick
import QtQuick.Controls
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "PrimaryKeyboard"
    when: windowShown
    readonly property var driver: testScenarioDriver // qmllint disable unqualified
    readonly property var presenter: testLocalPlayerPresenter // qmllint disable unqualified
    property var shell

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testCase.presenter
        }
    }
    // Just enough of a profile for the sidebar chip to show.
    QtObject {
        id: fakeProfile
        property bool needsSetup: false
        property bool available: true
        property string nickname: "Bia"
        property int avatarIndex: 7
        readonly property int avatarCount: 20
        readonly property url avatarSource: "qrc:/rc_music/avatars/avatar-08.svg"
        property var customAvatars: []
        function acceptsNickname(text) {
            return text.length > 0;
        }
        function setNickname(text) {
            return false;
        }
        function chooseBundledAvatar(index) {
            return false;
        }
        function chooseCustomAvatar(file) {
            return false;
        }
    }
    Component {
        id: editorComponent
        TextInput {
            width: 180
            height: 32
            text: "abc"
        }
    }
    SignalSpy {
        id: openSpy
        target: testCase.shell
        signalName: "openAudioRequested"
    }

    function init() {
        driver.resetLocalScenario();
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.requestActivate();
        tryVerify(function () {
            return testCase.shell.active;
        });
        openSpy.clear();
    }
    function cleanup() {
        if (shell)
            shell.destroy();
        shell = null;
        driver.resetLocalScenario();
    }
    function openQueue() {
        driver.enqueueLocalSelection(["C:/Music/A.mp3", "C:/Music/B.mp3", "C:/Music/C.mp3", "C:/Music/D.mp3"]);
        findChild(shell, "open-audio").Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        driver.publishLocalPosition(40000);
        tryCompare(presenter, "confirmedPositionMs", 40000);
        presenter.setVolume(0.5);
        compare(presenter.volume, 0.5);
        if (presenter.muted)
            verify(presenter.toggleMute());
        presenter.setSkipBackwardSeconds(7);
        presenter.setSkipForwardSeconds(20);
        compare(presenter.skipBackwardSeconds, 7);
        compare(presenter.skipForwardSeconds, 20);
    }
    function focusControl(name) {
        if (name.startsWith("queue-") || name === "queue-list") {
            shell.setQueueOpen(true);
            tryVerify(function () {
                return findChild(testCase.shell, "queue-panel").visible;
            });
            verify(waitForPolish(shell));
            wait(0);
        }
        if (name === "profile-chip")
            shell.profile = fakeProfile;
        const control = findChild(shell, name);
        verify(control, name);
        // An invisible control can take focus from code, never from the user.
        verify(control.visible, name + " is reachable");
        control.forceActiveFocus(Qt.TabFocusReason);
        tryVerify(function () {
            return control.activeFocus;
        });
        return control;
    }
    function test_spaceNeverActivatesLastControl_data() {
        return ["mute", "next", "previous", "player-shuffle", "player-repeat", "player-queue", "preferences", "profile-chip", "theme-toggle", "progress", "volume", "queue-list", "queue-remove"].map(function (name) {
            return {
                tag: name,
                control: name
            };
        });
    }
    function test_spaceNeverActivatesLastControl(data) {
        openQueue();
        const control = focusControl(data.control);
        const id = presenter.currentOccurrenceId;
        const count = findChild(shell, "queue-list").count;
        const pauses = driver.localPauseCount();
        const shuffle = presenter.shuffleQueueEnabled;
        const repeat = presenter.repeatQueueEnabled;
        keyClick(Qt.Key_Space);
        compare(driver.localPauseCount(), pauses + 1, "Space must route to playback regardless of the focused main-window control");
        compare(driver.localLoadCount(), 1);
        compare(driver.localSeekCount(), 0);
        compare(presenter.currentOccurrenceId, id);
        compare(findChild(shell, "queue-list").count, count);
        verify(!presenter.muted);
        compare(presenter.volume, 0.5);
        compare(presenter.shuffleQueueEnabled, shuffle);
        compare(presenter.repeatQueueEnabled, repeat);
        verify(!findChild(shell, "preferences-panel").visible);
        verify(control.activeFocus);
        verify(driver.repeatKey(Qt.Key_Space, Qt.NoModifier));
        compare(driver.localPauseCount(), pauses + 1);
    }
    function test_emptySpaceDoesNotOpenPicker() {
        focusControl("open-audio");
        keyClick(Qt.Key_Space);
        compare(openSpy.count, 0);
        verify(!presenter.selectingAudioFiles);
        compare(presenter.playbackState, PlayerPresenter.Unloaded);
        keyClick(Qt.Key_Return);
        compare(openSpy.count, 1, "Enter remains the focused button activation key");
    }
    function test_muteIconShowsTheCurrentSoundState() {
        openQueue();
        const mute = findChild(shell, "mute");
        verify(!presenter.muted);
        // The icon shows the sound state; the accessible name keeps the action.
        compare(mute.text, "🔊");
        if (mute.iconName.length > 0)
            compare(mute.iconName, "volume");
        compare(mute.Accessible.name.startsWith("Silenciar"), true);
        mouseClick(mute, mute.width / 2, mute.height / 2);
        verify(presenter.muted);
        compare(mute.text, "🔇");
        if (mute.iconName.length > 0)
            compare(mute.iconName, "mute");
        compare(mute.Accessible.name.startsWith("Silenciar"), false);
        mouseClick(mute, mute.width / 2, mute.height / 2);
        verify(!presenter.muted);
        compare(mute.text, "🔊");
        // Volume at 0% is silent too, so it shows the same crossed speaker.
        const volume = presenter.volume;
        verify(presenter.setVolume(0));
        tryCompare(mute, "text", "🔇");
        if (mute.iconName.length > 0)
            compare(mute.iconName, "mute");
        verify(presenter.setVolume(volume));
        tryCompare(mute, "text", "🔊");
    }
    function test_spaceAfterClickingMuteControlsPlaybackOnly() {
        openQueue();
        const mute = findChild(shell, "mute");
        mouseClick(mute, mute.width / 2, mute.height / 2);
        verify(presenter.muted);
        verify(mute.activeFocus, "The last mouse-clicked button owns focus");
        const pauses = driver.localPauseCount();
        const plays = driver.localPlayCount();
        keyClick(Qt.Key_Space);
        compare(driver.localPauseCount(), pauses + 1);
        verify(presenter.muted, "Space must not reactivate the last clicked mute button");
        driver.publishLocalPaused();
        tryCompare(presenter, "playbackState", PlayerPresenter.Paused);
        keyClick(Qt.Key_Space);
        compare(driver.localPlayCount(), plays + 1);
        verify(presenter.muted);
        verify(mute.activeFocus);
        compare(driver.localLoadCount(), 1);
    }
    function test_arrowsUseConfiguredSeekAndVolume_data() {
        return ["next", "progress", "volume", "queue-list"].map(function (name) {
            return {
                tag: name,
                control: name
            };
        });
    }
    function test_arrowsUseConfiguredSeekAndVolume(data) {
        openQueue();
        const control = focusControl(data.control);
        const list = findChild(shell, "queue-list");
        const index = list.currentIndex;
        keyClick(Qt.Key_Right);
        compare(driver.localSeekCount(), 1);
        compare(driver.localSeekTarget(), 60000);
        driver.publishLocalPosition(60000);
        tryCompare(presenter, "seekPending", false);
        keyClick(Qt.Key_Left);
        compare(driver.localSeekCount(), 2);
        compare(driver.localSeekTarget(), 53000);
        driver.publishLocalPosition(53000);
        tryCompare(presenter, "seekPending", false);
        keyClick(Qt.Key_Up);
        compare(presenter.volume, 0.51);
        keyClick(Qt.Key_Down);
        compare(presenter.volume, 0.5);
        compare(list.currentIndex, index);
        verify(control.activeFocus);
    }
    function test_shiftArrowsNavigateTracks_data() {
        return ["next", "progress", "volume", "queue-list"].map(function (name) {
            return {
                tag: name,
                control: name
            };
        });
    }
    function test_shiftArrowsNavigateTracks(data) {
        openQueue();
        focusControl(data.control);
        keyClick(Qt.Key_Right, Qt.ShiftModifier);
        compare(presenter.currentQueueIndex, 1);
        compare(driver.localSeekCount(), 0);
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        keyClick(Qt.Key_Left, Qt.ShiftModifier);
        compare(presenter.currentQueueIndex, 0);
        compare(driver.localSeekCount(), 0);
    }
    function test_listSelectionAndEnterRemainAvailable() {
        openQueue();
        const list = focusControl("queue-list");
        keyClick(Qt.Key_Down, Qt.ControlModifier);
        compare(list.currentIndex, 1);
        compare(presenter.currentQueueIndex, 0);
        keyClick(Qt.Key_Up, Qt.ControlModifier);
        compare(list.currentIndex, 0);
        keyClick(Qt.Key_Down, Qt.ControlModifier);
        keyClick(Qt.Key_Return);
        compare(presenter.currentQueueIndex, 1);
        compare(driver.localLoadCount(), 2);
    }
    function test_editorAndPreferencesKeepTheirKeys() {
        openQueue();
        const list = findChild(shell, "queue-list");
        const order = queueOrder(list).join(",");
        const repeat = presenter.repeatQueueEnabled;
        const shuffle = presenter.shuffleQueueEnabled;
        const editor = createTemporaryObject(editorComponent, shell.contentItem);
        verify(editor);
        editor.forceActiveFocus(Qt.TabFocusReason);
        editor.cursorPosition = 3;
        const pauses = driver.localPauseCount();
        keyClick(Qt.Key_M);
        keyClick(Qt.Key_Space);
        compare(editor.text, "abcm ");
        keyClick(Qt.Key_Left, Qt.ShiftModifier);
        compare(editor.selectedText, " ");
        keyClick(Qt.Key_F1);
        keyClick(Qt.Key_F2);
        compare(driver.localPauseCount(), pauses);
        compare(driver.localLoadCount(), 1);
        verify(!presenter.muted);
        compare(presenter.repeatQueueEnabled, repeat);
        compare(presenter.shuffleQueueEnabled, shuffle);
        compare(queueOrder(list).join(","), order);
        editor.destroy();
        focusControl("preferences");
        keyClick(Qt.Key_Return);
        const preferences = findChild(shell, "preferences-panel");
        tryVerify(function () {
            return preferences.opened;
        });
        keyClick(Qt.Key_M);
        keyClick(Qt.Key_Right, Qt.ShiftModifier);
        keyClick(Qt.Key_F1);
        keyClick(Qt.Key_F2);
        compare(driver.localLoadCount(), 1);
        verify(!presenter.muted);
        compare(presenter.repeatQueueEnabled, repeat);
        compare(presenter.shuffleQueueEnabled, shuffle);
        compare(queueOrder(list).join(","), order);
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !preferences.visible;
        });
    }
    function test_queueModesHaveVisibleAccessibleControls() {
        openQueue();
        focusControl("player-repeat");
        const shuffle = findChild(shell, "player-shuffle");
        const repeat = findChild(shell, "player-repeat");
        verify(shuffle && repeat, "F1 and F2 require visible player actions");
        verify(shuffle.visible && repeat.visible);
        compare(shuffle.Accessible.name, "Modo aleatório");
        compare(shuffle.Accessible.description, "F1");
        compare(repeat.Accessible.name, "Repetir fila");
        compare(repeat.Accessible.description, "F2");
        const before = presenter.repeatQueueEnabled;
        keyClick(Qt.Key_F2);
        compare(presenter.repeatQueueEnabled, !before);
        compare(repeat.checked, !before);
        compare(repeat.Accessible.checked, !before);
        verify(driver.repeatKey(Qt.Key_F2, Qt.NoModifier));
        compare(presenter.repeatQueueEnabled, !before);
        repeat.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        compare(presenter.repeatQueueEnabled, before);
        compare(repeat.checked, before);
        compare(repeat.Accessible.checked, before);
        shell.width = 720;
        shell.height = 520;
        verify(waitForPolish(shell));
        verify(shuffle.width >= 40 && repeat.width >= 40);
        const bar = findChild(shell, "transport-bar");
        const previous = findChild(shell, "previous");
        const next = findChild(shell, "next");
        const inBar = item => {
            const origin = item.mapToItem(bar, 0, 0);
            return origin.x >= 0 && origin.x + item.width <= bar.width && origin.y >= 0 && origin.y + item.height <= bar.height;
        };
        verify(inBar(shuffle) && inBar(repeat), "shuffle and repeat stay inside the player at 720x520");
        verify(shuffle.mapToItem(previous, shuffle.width, 0).x <= 0, "shuffle does not overlap previous");
        verify(repeat.mapToItem(next, repeat.width, 0).x <= 0 || next.mapToItem(repeat, next.width, 0).x <= 0, "repeat does not overlap next");
    }
    function test_f1F2WorkWithTheQueueClosed() {
        openQueue();
        verify(!findChild(shell, "queue-panel").visible);
        const shuffle = findChild(shell, "player-shuffle");
        const repeat = findChild(shell, "player-repeat");
        const shuffled = presenter.shuffleQueueEnabled;
        keyClick(Qt.Key_F1);
        compare(presenter.shuffleQueueEnabled, !shuffled);
        compare(shuffle.checked, !shuffled);
        keyClick(Qt.Key_F1);
        compare(presenter.shuffleQueueEnabled, shuffled);
        compare(shuffle.checked, shuffled);
        const repeated = presenter.repeatQueueEnabled;
        keyClick(Qt.Key_F2);
        compare(presenter.repeatQueueEnabled, !repeated);
        compare(repeat.checked, !repeated);
        keyClick(Qt.Key_F2);
        compare(presenter.repeatQueueEnabled, repeated);
        compare(repeat.checked, repeated);
        verify(!findChild(shell, "queue-panel").visible);
    }
    function isInside(item, ancestor) {
        for (let current = item; current; current = current.parent) {
            if (current === ancestor)
                return true;
        }
        return false;
    }
    // Presses the key from start until the focus comes back to start; every stop stays inside the drawer.
    function walkDrawerCycle(start, modifiers) {
        const panel = findChild(shell, "queue-panel");
        const walked = [];
        for (let step = 0; step < 40; ++step) {
            keyClick(Qt.Key_Tab, modifiers);
            const item = shell.activeFocusItem;
            verify(item, "focus is never lost while tabbing the drawer");
            verify(isInside(item, panel), "the focus left the drawer for " + item.objectName + " after " + walked.join(","));
            if (item === start)
                return walked;
            walked.push(item.objectName);
        }
        fail("the focus never came back to " + start.objectName + ": " + walked.join(","));
        return walked;
    }
    function test_queueDrawerHasNoShuffleOrRepeat() {
        openQueue();
        const close = focusControl("queue-header-close");
        verify(findChild(shell, "queue-panel").visible);
        const shuffle = findChild(shell, "queue-shuffle");
        const repeat = findChild(shell, "queue-repeat");
        verify(!shuffle || !shuffle.visible, "queue-shuffle is gone from the drawer");
        verify(!repeat || !repeat.visible, "queue-repeat is gone from the drawer");
        // Tab walks the whole drawer (actions, then the rows) and comes back to the close button.
        const walked = walkDrawerCycle(close, Qt.NoModifier);
        verify(walked.indexOf("queue-shuffle") < 0 && walked.indexOf("queue-repeat") < 0, "Tab never stops on a removed mode button: " + walked.join(","));
        verify(walked.indexOf("queue-clear") >= 0, walked.join(","));
        verify(walked.some(name => name.startsWith("queue-row-")), "the rows were reached: " + walked.join(","));
    }
    // The Aurora drawer is modal: Tab and Shift+Tab go around it and never reach the page, the sidebar or the player.
    function test_queueDrawerKeepsTabInside() {
        openQueue();
        const close = focusControl("queue-header-close");
        const forward = walkDrawerCycle(close, Qt.NoModifier);
        compare(forward[0], "queue-add", forward.join(","));
        verify(forward.indexOf("queue-clear") > forward.indexOf("queue-remove"), forward.join(","));
        verify(forward[forward.length - 1].startsWith("queue-row-"), "the rows close the cycle: " + forward.join(","));
        const backward = walkDrawerCycle(close, Qt.ShiftModifier);
        compare(backward.join(","), forward.slice().reverse().join(","), "Shift+Tab walks the same cycle backwards");
    }
    // With the queue emptied under the open drawer, Tab still goes around the drawer: close, the toolbar, the empty action.
    function test_emptyQueueDrawerKeepsTabInside() {
        openQueue();
        const close = focusControl("queue-header-close");
        verify(presenter.clearQueue());
        tryVerify(() => !testCase.presenter.queueLoaded);
        const panel = findChild(shell, "queue-panel");
        verify(panel.visible);
        tryVerify(() => findChild(panel, "queue-empty").visible);
        verify(!findChild(panel, "queue-list").visible);
        close.forceActiveFocus(Qt.TabFocusReason);
        tryVerify(() => close.activeFocus);
        const forward = walkDrawerCycle(close, Qt.NoModifier);
        verify(forward.indexOf("queue-empty-primary") >= 0, forward.join(","));
        verify(forward.indexOf("queue-list") < 0, forward.join(","));
        const backward = walkDrawerCycle(close, Qt.ShiftModifier);
        compare(backward.join(","), forward.slice().reverse().join(","), "Shift+Tab walks the same cycle backwards");
    }
    function queueOrder(list) {
        list.forceLayout();
        const ids = [];
        for (let index = 0; index < list.count; ++index) {
            list.positionViewAtIndex(index, ListView.Contain);
            list.forceLayout();
            ids.push(list.itemAtIndex(index).occurrenceId);
        }
        return ids;
    }
    function test_shuffleModeAndButtonToggleWithoutChangingCurrent() {
        openQueue();
        verify(presenter.nextTrack());
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        const list = focusControl("queue-list");
        const before = presenter.shuffleQueueEnabled;
        const current = presenter.currentOccurrenceId;
        const loads = driver.localLoadCount();
        const pauses = driver.localPauseCount();
        keyClick(Qt.Key_F1);
        compare(presenter.shuffleQueueEnabled, !before);
        verify(list.activeFocus);
        const displayedOrder = queueOrder(list).join(",");
        const shuffle = findChild(shell, "player-shuffle");
        verify(shuffle && shuffle.enabled);
        compare(shuffle.checked, !before);
        compare(shuffle.Accessible.checked, !before);
        verify(driver.repeatKey(Qt.Key_F1, Qt.NoModifier));
        compare(presenter.shuffleQueueEnabled, !before);
        compare(queueOrder(list).join(","), displayedOrder);
        shuffle.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        compare(presenter.shuffleQueueEnabled, before);
        compare(shuffle.checked, before);
        verify(shuffle.activeFocus);
        compare(queueOrder(list).join(","), displayedOrder, "Disabling shuffle must not restore an old queue order");
        compare(presenter.currentOccurrenceId, current);
        compare(driver.localLoadCount(), loads);
        compare(driver.localPauseCount(), pauses);
        compare(driver.localSeekCount(), 0);
    }
    function test_queueModePreferencesCanToggleWithoutAQueue() {
        const before = presenter.repeatQueueEnabled;
        focusControl("open-audio");
        keyClick(Qt.Key_F2);
        compare(presenter.repeatQueueEnabled, !before);
        verify(driver.repeatKey(Qt.Key_F2, Qt.NoModifier));
        compare(presenter.repeatQueueEnabled, !before);
        keyClick(Qt.Key_F2);
        compare(presenter.repeatQueueEnabled, before);
        const shuffle = presenter.shuffleQueueEnabled;
        keyClick(Qt.Key_F1);
        compare(presenter.shuffleQueueEnabled, !shuffle);
        verify(driver.repeatKey(Qt.Key_F1, Qt.NoModifier));
        compare(presenter.shuffleQueueEnabled, !shuffle);
        keyClick(Qt.Key_F1);
        compare(presenter.shuffleQueueEnabled, shuffle);
        compare(driver.localLoadCount(), 0);
        compare(openSpy.count, 0);
    }
    function test_contextMenuAndConfirmationOwnTheirKeys() {
        openQueue();
        const list = focusControl("queue-list");
        const before = queueOrder(list).join(",");
        const repeat = presenter.repeatQueueEnabled;
        const shuffle = presenter.shuffleQueueEnabled;
        keyClick(Qt.Key_Down, Qt.ControlModifier);
        keyClick(Qt.Key_Menu);
        const menu = findChild(shell, "queue-row-menu");
        tryVerify(function () {
            return menu.visible;
        });
        const pauses = driver.localPauseCount();
        for (const key of [Qt.Key_M, Qt.Key_F1, Qt.Key_F2])
            keyClick(key);
        keyClick(Qt.Key_Right, Qt.ShiftModifier);
        compare(presenter.currentQueueIndex, 0);
        compare(presenter.repeatQueueEnabled, repeat);
        compare(presenter.shuffleQueueEnabled, shuffle);
        compare(queueOrder(list).join(","), before);
        verify(!presenter.muted);
        findChild(shell, "queue-menu-play-now").forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        tryCompare(presenter, "currentQueueIndex", 1);
        compare(driver.localPauseCount(), pauses);
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        focusControl("queue-clear");
        keyClick(Qt.Key_Return);
        const confirmation = findChild(shell, "queue-clear-confirmation");
        tryVerify(function () {
            return confirmation.opened;
        });
        keyClick(Qt.Key_F2);
        compare(presenter.repeatQueueEnabled, repeat);
        keyClick(Qt.Key_F1);
        compare(presenter.shuffleQueueEnabled, shuffle);
        findChild(shell, "queue-clear-cancel").forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        tryVerify(function () {
            return !confirmation.visible;
        });
        compare(driver.localPauseCount(), pauses);
        compare(list.count, 4);
    }
}
