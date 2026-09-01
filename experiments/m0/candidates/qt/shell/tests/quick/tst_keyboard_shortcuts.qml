import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "ShellKeyboardShortcuts"
    when: windowShown
    readonly property PlayerPresenter presenter: testPlayerPresenter // qmllint disable unqualified
    readonly property var driver: testScenarioDriver // qmllint disable unqualified
    property var shell

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testCase.presenter
        }
    }
    Component {
        id: editorComponent
        TextInput {
            width: 180
            height: 32
            text: "alpha beta"
        }
    }
    SignalSpy {
        id: openSpy
        target: testCase.shell
        signalName: "openAudioRequested"
    }

    function focusItem(item) {
        verify(item);
        item.forceActiveFocus(Qt.TabFocusReason);
        tryVerify(function () {
            return item.activeFocus;
        });
    }
    function verifySeek(position) {
        tryVerify(function () {
            return !testCase.presenter.seekPending;
        });
        compare(presenter.confirmedPositionMs, position);
        compare(presenter.playbackState, PlayerPresenter.Paused);
    }

    function test_windowShortcutsRespectFocusAndState() {
        driver.setSystemAppearance(false, true);
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.requestActivate();
        tryVerify(function () {
            return testCase.shell.active;
        });
        focusItem(findChild(shell, "empty-title"));
        keyClick(Qt.Key_Space);
        keyClick(Qt.Key_M);
        keyClick(Qt.Key_Left);
        keyClick(Qt.Key_Right);
        keyClick(Qt.Key_Up);
        keyClick(Qt.Key_Down);
        compare(presenter.playbackState, PlayerPresenter.Unloaded);
        compare(presenter.volume, 0.8);
        verify(!presenter.muted);

        keyClick(Qt.Key_Comma, Qt.ControlModifier);
        const emptyPreferences = findChild(shell, "preferences-panel");
        tryVerify(function () {
            return emptyPreferences.opened;
        });
        keyClick(Qt.Key_O, Qt.ControlModifier);
        verify(!presenter.queueLoaded);
        compare(openSpy.count, 0);
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !emptyPreferences.visible;
        });
        focusItem(findChild(shell, "empty-title"));

        keyClick(Qt.Key_O, Qt.ControlModifier);
        verify(presenter.queueLoaded, "Ctrl+O must invoke the existing fixture-open action");
        compare(openSpy.count, 1);
        compare(presenter.playbackState, PlayerPresenter.Playing);
        const queueModel = presenter.queueModel;
        const title = findChild(shell, "current-title");
        focusItem(title);
        keyClick(Qt.Key_Space);
        compare(presenter.playbackState, PlayerPresenter.Paused, "Space must pause when the track title owns focus");
        verify(title.activeFocus);
        for (let index = 0; index < 3; ++index)
            verify(driver.repeatKey(Qt.Key_Space, Qt.NoModifier));
        compare(presenter.playbackState, PlayerPresenter.Paused);
        keyClick(Qt.Key_O, Qt.ControlModifier);
        compare(openSpy.count, 1);
        compare(presenter.queueModel, queueModel);

        keyClick(Qt.Key_M);
        verify(presenter.muted);
        for (let index = 0; index < 3; ++index)
            verify(driver.repeatKey(Qt.Key_M, Qt.NoModifier));
        verify(presenter.muted);
        keyClick(Qt.Key_M);
        verify(!presenter.muted);
        compare(presenter.volume, 0.8);

        keyClick(Qt.Key_Up);
        compare(presenter.volume, 0.81);
        verify(driver.repeatKey(Qt.Key_Up, Qt.NoModifier));
        compare(presenter.volume, 0.82);
        keyClick(Qt.Key_Down);
        compare(presenter.volume, 0.81);
        verify(presenter.setVolume(0.805));
        keyClick(Qt.Key_Up);
        compare(presenter.volume, 0.815);
        keyClick(Qt.Key_Down);
        compare(presenter.volume, 0.805);
        verify(presenter.setVolume(1));
        keyClick(Qt.Key_Up);
        compare(presenter.volume, 1);
        verify(presenter.setVolume(0));
        keyClick(Qt.Key_Down);
        compare(presenter.volume, 0);
        verify(presenter.setVolume(0.8));

        verify(presenter.setSkipBackwardSeconds(7));
        verify(presenter.setSkipForwardSeconds(20));
        keyClick(Qt.Key_Right);
        verifySeek(104000);
        verify(driver.repeatKey(Qt.Key_Right, Qt.NoModifier));
        verifySeek(124000);
        keyClick(Qt.Key_Left);
        verifySeek(117000);
        verify(title.activeFocus);
        verify(presenter.beginSeekPreview(118000));
        const occurrenceBeforeSeek = presenter.currentOccurrenceId;
        keyClick(Qt.Key_Left);
        keyClick(Qt.Key_Right);
        keyClick(Qt.Key_Right, Qt.ControlModifier);
        compare(presenter.positionMs, 118000);
        compare(presenter.confirmedPositionMs, 117000);
        compare(presenter.currentOccurrenceId, occurrenceBeforeSeek);
        verify(presenter.commitSeekPreview());
        keyClick(Qt.Key_Right);
        compare(presenter.positionMs, 118000);
        verifySeek(118000);

        focusItem(shell.contentItem);
        keyClick(Qt.Key_Up);
        compare(presenter.volume, 0.81);
        verify(presenter.setVolume(0.8));
        const playPause = findChild(shell, "play-pause");
        focusItem(playPause);
        keyClick(Qt.Key_Space);
        compare(presenter.playbackState, PlayerPresenter.Playing);
        keyClick(Qt.Key_Space);
        compare(presenter.playbackState, PlayerPresenter.Paused);
        verify(playPause.activeFocus);
        for (const button of [playPause, findChild(shell, "mute"), findChild(shell, "previous"), findChild(shell, "skip-forward")]) {
            focusItem(button);
            const buttonPosition = presenter.confirmedPositionMs;
            keyClick(Qt.Key_Right);
            verifySeek(buttonPosition + 20000);
            keyClick(Qt.Key_Left);
            verifySeek(buttonPosition + 13000);
            keyClick(Qt.Key_Up);
            compare(presenter.volume, 0.81);
            keyClick(Qt.Key_Down);
            compare(presenter.volume, 0.8);
            verify(button.activeFocus, "Focus left " + button.objectName + ": " + String(shell.activeFocusItem) + "/" + (shell.activeFocusItem ? shell.activeFocusItem.objectName : "none"));
        }
        focusItem(findChild(shell, "mute"));
        verify(!presenter.muted);
        keyClick(Qt.Key_Space);
        verify(presenter.muted);
        compare(presenter.playbackState, PlayerPresenter.Paused);
        keyClick(Qt.Key_Space);
        verify(!presenter.muted);

        const queueButton = findChild(shell, "queue");
        focusItem(queueButton);
        keyClick(Qt.Key_Space);
        const queueList = findChild(shell, "queue-list");
        tryVerify(function () {
            return testCase.shell.queueOpen && queueList.activeFocus;
        });
        const queueIndex = queueList.currentIndex;
        const queuePosition = presenter.confirmedPositionMs;
        keyClick(Qt.Key_Down);
        compare(queueList.currentIndex, queueIndex + 1);
        compare(presenter.volume, 0.8);
        keyClick(Qt.Key_Up);
        compare(queueList.currentIndex, queueIndex);
        keyClick(Qt.Key_Left);
        keyClick(Qt.Key_Right);
        keyClick(Qt.Key_Space);
        compare(presenter.confirmedPositionMs, queuePosition);
        compare(presenter.playbackState, PlayerPresenter.Paused);
        keyClick(Qt.Key_L, Qt.ControlModifier);
        tryVerify(function () {
            return !testCase.shell.queueOpen && queueButton.activeFocus;
        });
        verify(driver.repeatKey(Qt.Key_L, Qt.ControlModifier));
        verify(!shell.queueOpen);

        const progress = findChild(shell, "progress");
        focusItem(progress);
        const sliderPosition = presenter.confirmedPositionMs;
        const seekCommits = driver.seekCommitAttemptCount();
        keyClick(Qt.Key_Right);
        compare(driver.seekCommitAttemptCount(), seekCommits + 1);
        verifySeek(sliderPosition + 1000);
        keyClick(Qt.Key_Space);
        compare(presenter.playbackState, PlayerPresenter.Paused);
        const volume = findChild(shell, "volume");
        focusItem(volume);
        keyClick(Qt.Key_Left);
        compare(presenter.volume, 0.79);
        compare(presenter.confirmedPositionMs, sliderPosition + 1000);
        keyClick(Qt.Key_Space);
        compare(presenter.playbackState, PlayerPresenter.Paused);

        const editor = createTemporaryObject(editorComponent, shell.contentItem);
        verify(editor);
        focusItem(editor);
        editor.cursorPosition = editor.text.length;
        keyClick(Qt.Key_M);
        keyClick(Qt.Key_Space);
        compare(editor.text, "alpha betam ");
        compare(presenter.playbackState, PlayerPresenter.Paused);
        verify(!presenter.muted);
        editor.text = "alpha beta";
        editor.cursorPosition = editor.text.length;
        keyClick(Qt.Key_Left, Qt.ControlModifier);
        compare(editor.cursorPosition, 6);
        keyClick(Qt.Key_Right, Qt.ControlModifier);
        compare(editor.cursorPosition, 10);
        keyClick(Qt.Key_Left);
        compare(editor.cursorPosition, 9);
        keyClick(Qt.Key_Up);
        keyClick(Qt.Key_Down);
        compare(presenter.volume, 0.79);
        compare(presenter.confirmedPositionMs, sliderPosition + 1000);
        compare(presenter.currentOccurrenceId, occurrenceBeforeSeek);
        editor.destroy();

        focusItem(title);
        keyClick(Qt.Key_Comma, Qt.ControlModifier);
        const preferences = findChild(shell, "preferences-panel");
        tryVerify(function () {
            return preferences.opened;
        });
        mouseClick(findChild(shell, "preferences-playback"));
        const field = findChild(shell, "skip-forward-seconds");
        focusItem(field);
        keyClick(Qt.Key_A, Qt.ControlModifier);
        keyClick(Qt.Key_M);
        keyClick(Qt.Key_Space);
        compare(field.text, "m ");
        compare(presenter.skipForwardSeconds, 20);
        keyClick(Qt.Key_O, Qt.ControlModifier);
        keyClick(Qt.Key_L, Qt.ControlModifier);
        keyClick(Qt.Key_Right, Qt.ControlModifier);
        keyClick(Qt.Key_Left, Qt.ControlModifier);
        keyClick(Qt.Key_Up);
        keyClick(Qt.Key_Down);
        verify(!shell.queueOpen);
        compare(openSpy.count, 1);
        compare(presenter.volume, 0.79);
        verify(!presenter.muted);
        compare(presenter.playbackState, PlayerPresenter.Paused);
        compare(presenter.currentOccurrenceId, occurrenceBeforeSeek);
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !preferences.visible;
        });
        verify(findChild(shell, "preferences").activeFocus);

        verify(driver.enterUnavailable());
        focusItem(findChild(shell, "state-layer"));
        const errorPosition = presenter.positionMs;
        keyClick(Qt.Key_Space);
        keyClick(Qt.Key_M);
        keyClick(Qt.Key_Left);
        keyClick(Qt.Key_Right);
        keyClick(Qt.Key_Up);
        keyClick(Qt.Key_Down);
        compare(presenter.playbackState, PlayerPresenter.Error);
        compare(presenter.positionMs, errorPosition);
        compare(presenter.volume, 0.79);
        verify(!presenter.muted);
        compare(presenter.queueModel, queueModel);
    }
}
