import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "TypedShellStates"
    when: windowShown

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testPlayerPresenter // qmllint disable unqualified
        }
    }
    Component {
        id: actionProbeComponent
        AccessibleActionButton {
            controlId: "action-probe"
            contract: testShellUiContract // qmllint disable unqualified
            text: testShellUiContract.openAudioText // qmllint disable unqualified
            onClicked: testCase.actionProbeCount += 1
        }
    }

    property var shell
    property int actionProbeCount: 0
    SignalSpy {
        id: openSpy
        target: testCase.shell
        signalName: "openAudioRequested"
    }
    function verifyInsideWindow(item) {
        const position = item.mapToItem(shell.contentItem, 0, 0);
        verify(item.visible);
        verify(item.width > 0);
        verify(item.height > 0);
        verify(position.x >= 0);
        verify(position.y >= 0);
        verify(position.x + item.width <= shell.contentItem.width + 0.5);
        verify(position.y + item.height <= shell.contentItem.height + 0.5);
    }
    function verifyHorizontalNonOverlap(items) {
        for (let index = 1; index < items.length; ++index) {
            const previous = items[index - 1];
            const current = items[index];
            const previousPosition = previous.mapToItem(shell.contentItem, 0, 0);
            const currentPosition = current.mapToItem(shell.contentItem, 0, 0);
            verify(previousPosition.x + previous.width <= currentPosition.x + 0.5, previous.objectName + " [" + previousPosition.x + ", " + previous.width + "] overlaps " + current.objectName + " [" + currentPosition.x + ", " + current.width + "]");
        }
    }

    function test_emptyPlayingErrorJourney() {
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        compare(shell.width, 960);
        compare(shell.height, 640);
        compare(shell.minimumWidth, 720);
        compare(shell.minimumHeight, 520);
        shell.width = shell.minimumWidth;
        shell.height = shell.minimumHeight;
        tryCompare(shell, "width", 720);
        tryCompare(shell, "height", 520);
        const title = findChild(shell, "empty-title");
        const hint = findChild(shell, "empty-hint");
        const button = findChild(shell, "open-audio");
        const image = findChild(shell, "cover-placeholder");
        const emptyState = findChild(shell, "emptyState");
        compare(title.text, testShellUiContract.emptyTitle); // qmllint disable unqualified
        compare(hint.text, testShellUiContract.emptyHint); // qmllint disable unqualified
        compare(button.text, testShellUiContract.openAudioText); // qmllint disable unqualified
        compare(button.Accessible.role, Accessible.Button);
        compare(button.Accessible.name, testShellUiContract.openAudioText); // qmllint disable unqualified
        verify(button.width >= testShellUiContract.controlTargetPreferred); // qmllint disable unqualified
        verify(button.height >= testShellUiContract.controlTargetPreferred); // qmllint disable unqualified
        verifyInsideWindow(emptyState);
        verifyInsideWindow(title);
        verifyInsideWindow(hint);
        verifyInsideWindow(button);
        compare(image.source.toString(), testShellUiContract.placeholderSource.toString()); // qmllint disable unqualified
        verify(image.Accessible.ignored);
        tryCompare(image, "status", Image.Ready, 500);
        shell.requestActivate();
        tryVerify(function () {
            return shell.active;
        }, 500);
        const actionProbe = createTemporaryObject(actionProbeComponent, shell.contentItem, {
            "x": 0,
            "y": 0,
            "z": 10
        });
        verify(actionProbe);
        actionProbe.forceActiveFocus();
        tryVerify(function () {
            return actionProbe.activeFocus;
        }, 500);
        keyClick(Qt.Key_Space);
        compare(actionProbeCount, 1);
        keyClick(Qt.Key_Return);
        compare(actionProbeCount, 2);
        verify(actionProbe.background.border.width > 0);
        mousePress(actionProbe, actionProbe.width / 2, actionProbe.height / 2, Qt.LeftButton);
        verify(actionProbe.down);
        compare(actionProbe.contentItem.topPadding, 2);
        mouseRelease(actionProbe, actionProbe.width / 2, actionProbe.height / 2, Qt.LeftButton);
        compare(actionProbeCount, 3);
        actionProbe.Accessible.pressAction();
        compare(actionProbeCount, 4);
        actionProbe.destroy();

        const transport = findChild(shell.contentItem, "transport-bar");
        verify(transport);
        verify(!transport.enabled);
        const progress = findChild(shell, "progress");
        const playPause = findChild(shell, "play-pause");
        const skipBackward = findChild(shell, "skip-backward");
        const skipForward = findChild(shell, "skip-forward");
        const volume = findChild(shell, "volume");
        const mute = findChild(shell, "mute");
        verify(emptyState.visible);
        verify(!progress.enabled);
        verify(!playPause.enabled);
        verify(!skipBackward.enabled);
        verify(!skipForward.enabled);
        verify(!volume.enabled);
        verify(!mute.enabled);
        verifyInsideWindow(transport);
        verifyInsideWindow(progress);
        verifyInsideWindow(findChild(shell, "elapsed-time"));
        verifyInsideWindow(findChild(shell, "duration-time"));
        const emptyTransportControls = [findChild(shell, "previous"), skipBackward, playPause, skipForward, findChild(shell, "next"), mute, volume];
        for (const emptyControl of emptyTransportControls) {
            verifyInsideWindow(emptyControl);
            compare(emptyControl.Accessible.focusable, false);
            verify(!emptyControl.activeFocus);
        }
        verifyHorizontalNonOverlap(emptyTransportControls);
        button.forceActiveFocus();
        compare(button.Accessible.focusable, true);
        compare(button.Accessible.focused, true);
        compare(button.background.border.width, 3);
        keyClick(Qt.Key_Space);
        compare(openSpy.count, 1);
        verify(testPlayerPresenter.queueLoaded); // qmllint disable unqualified
        verify(testPlayerPresenter.togglePlayback()); // qmllint disable unqualified
        tryVerify(function () {
            return transport.enabled;
        });
        const playingState = findChild(shell, "playing-state");
        verify(playingState.visible);
        verifyInsideWindow(playingState);
        verify(!emptyState.visible);
        const currentTitle = findChild(shell, "current-title");
        const currentArtist = findChild(shell, "current-artist");
        const currentAlbum = findChild(shell, "current-album");
        compare(currentTitle.text, "Canção nº 17 — São João, ação e café ☕");
        compare(currentArtist.text, "Artista Sintética Ç");
        compare(currentAlbum.text, "Coleção Ação e Emoção");
        compare(currentTitle.textFormat, Text.PlainText);
        compare(currentArtist.textFormat, Text.PlainText);
        compare(currentAlbum.textFormat, Text.PlainText);
        compare(currentTitle.Accessible.name, currentTitle.text);
        const cover = findChild(shell, "cover-track");
        compare(cover.source.toString(), testShellUiContract.trackSource.toString()); // qmllint disable unqualified
        verify(cover.Accessible.ignored);
        tryCompare(cover, "status", Image.Ready, 500);
        verifyInsideWindow(cover);
        verifyInsideWindow(currentTitle);
        verifyInsideWindow(currentArtist);
        verifyInsideWindow(currentAlbum);
        compare(findChild(shell, "elapsed-time").text, "01:24");
        compare(findChild(shell, "duration-time").text, "04:08");
        compare(progress.Accessible.role, Accessible.Slider);
        compare(progress.Accessible.name, testShellUiContract.progressName); // qmllint disable unqualified
        compare(progress.stepSize, 1000);
        compare(progress.from, 0);
        compare(progress.to, 248000);
        compare(progress.value, 84000);
        verify(progress.height >= testShellUiContract.controlTargetMinimum); // qmllint disable unqualified
        verifyInsideWindow(progress);
        const expectedNames = {
            "previous": "Faixa anterior",
            "skip-backward": "Voltar 5 segundos",
            "play-pause": "Reproduzir",
            "skip-forward": "Avançar 5 segundos",
            "next": "Próxima faixa",
            "mute": "Silenciar"
        };
        for (const objectName in expectedNames) {
            const transportButton = findChild(shell, objectName);
            verify(transportButton);
            compare(transportButton.Accessible.role, Accessible.Button);
            compare(transportButton.Accessible.name, expectedNames[objectName]);
            verify(transportButton.width >= 44);
            verify(transportButton.height >= 44);
        }
        compare(skipBackward.text, "−5");
        compare(skipForward.text, "+5");
        verify(!findChild(shell, "previous").enabled);
        verify(!findChild(shell, "next").enabled);
        compare(volume.from, 0);
        compare(volume.to, 100);
        compare(volume.value, 80);
        compare(volume.Accessible.name, "Volume");
        compare(volume.stepSize, 1);
        verify(volume.height >= testShellUiContract.controlTargetMinimum); // qmllint disable unqualified
        verifyInsideWindow(volume);
        const focusOrder = [progress, skipBackward, playPause, skipForward, mute, volume];
        for (const focusableControl of focusOrder)
            compare(focusableControl.Accessible.focusable, true);
        compare(findChild(shell, "previous").Accessible.focusable, false);
        compare(findChild(shell, "next").Accessible.focusable, false);
        progress.forceActiveFocus(Qt.TabFocusReason);
        tryVerify(function () {
            return progress.activeFocus;
        });
        verify(progress.focusIndicatorVisible);
        compare(progress.Accessible.focused, true);
        for (let focusIndex = 1; focusIndex < focusOrder.length; ++focusIndex) {
            keyClick(Qt.Key_Tab);
            const focusedControl = focusOrder[focusIndex];
            tryVerify(function () {
                return focusedControl.activeFocus;
            });
            if (focusedControl === volume)
                verify(volume.focusIndicatorVisible);
            else
                compare(focusedControl.background.border.width, 3);
            compare(focusedControl.Accessible.focused, true);
        }
        for (let reverseIndex = focusOrder.length - 2; reverseIndex >= 0; --reverseIndex) {
            keyClick(Qt.Key_Tab, Qt.ShiftModifier);
            tryVerify(function () {
                return focusOrder[reverseIndex].activeFocus;
            });
        }
        verify(progress.focusIndicatorVisible);
        volume.forceActiveFocus();
        keyClick(Qt.Key_Left);
        compare(testPlayerPresenter.volume, 0.79); // qmllint disable unqualified
        verify(Math.abs(volume.value - testPlayerPresenter.volume * 100) < 0.01); // qmllint disable unqualified
        mouseClick(mute);
        verify(testPlayerPresenter.muted); // qmllint disable unqualified
        compare(mute.Accessible.name, "Ativar som");
        mouseClick(mute);
        verify(!testPlayerPresenter.muted); // qmllint disable unqualified
        compare(mute.Accessible.name, "Silenciar");
        playPause.forceActiveFocus();
        keyClick(Qt.Key_Space);
        compare(testPlayerPresenter.playbackState, PlayerPresenter.Playing); // qmllint disable unqualified
        compare(playPause.Accessible.name, "Pausar");
        keyClick(Qt.Key_Return);
        compare(testPlayerPresenter.playbackState, PlayerPresenter.Paused); // qmllint disable unqualified
        compare(playPause.Accessible.name, "Reproduzir");
        keyClick(Qt.Key_Space);
        compare(testPlayerPresenter.playbackState, PlayerPresenter.Playing); // qmllint disable unqualified
        playPause.Accessible.pressAction();
        compare(testPlayerPresenter.playbackState, PlayerPresenter.Paused); // qmllint disable unqualified
        playPause.Accessible.pressAction();
        compare(testPlayerPresenter.playbackState, PlayerPresenter.Playing); // qmllint disable unqualified
        progress.forceActiveFocus();
        let seekCommitCount = testScenarioDriver.seekCommitAttemptCount(); // qmllint disable unqualified
        keyClick(Qt.Key_Right);
        compare(testScenarioDriver.seekCommitAttemptCount(), seekCommitCount + 1); // qmllint disable unqualified
        verify(testPlayerPresenter.seekPending); // qmllint disable unqualified
        verify(!progress.enabled);
        verify(!skipBackward.enabled);
        verify(!skipForward.enabled);
        compare(testPlayerPresenter.positionMs, 85000); // qmllint disable unqualified
        tryVerify(function () {
            return !testPlayerPresenter.seekPending;
        }, 500); // qmllint disable unqualified
        tryVerify(function () {
            return progress.enabled;
        }, 500);
        compare(testPlayerPresenter.confirmedPositionMs, 85000); // qmllint disable unqualified
        seekCommitCount = testScenarioDriver.seekCommitAttemptCount(); // qmllint disable unqualified
        mouseClick(progress, progress.width * 0.45, progress.height / 2, Qt.LeftButton);
        compare(testScenarioDriver.seekCommitAttemptCount(), seekCommitCount + 1); // qmllint disable unqualified
        verify(testPlayerPresenter.seekPending); // qmllint disable unqualified
        tryVerify(function () {
            return !testPlayerPresenter.seekPending;
        }, 500); // qmllint disable unqualified
        seekCommitCount = testScenarioDriver.seekCommitAttemptCount(); // qmllint disable unqualified
        mousePress(progress, progress.width * 0.4, progress.height / 2, Qt.LeftButton);
        mouseMove(progress, progress.width * 0.5, progress.height / 2, 20);
        compare(testScenarioDriver.seekCommitAttemptCount(), seekCommitCount); // qmllint disable unqualified
        verify(testPlayerPresenter.seekPreviewActive); // qmllint disable unqualified
        verify(!testPlayerPresenter.seekPending); // qmllint disable unqualified
        verify(progress.enabled);
        verify(!skipBackward.enabled);
        verify(!skipForward.enabled);
        mouseRelease(progress, progress.width * 0.5, progress.height / 2, Qt.LeftButton);
        compare(testScenarioDriver.seekCommitAttemptCount(), seekCommitCount + 1); // qmllint disable unqualified
        verify(testPlayerPresenter.seekPending); // qmllint disable unqualified
        tryVerify(function () {
            return !testPlayerPresenter.seekPending;
        }, 500); // qmllint disable unqualified
        verify(testScenarioDriver.enterUnavailable()); // qmllint disable unqualified
        wait(0);
        compare(findChild(shell.contentItem, "transport-bar"), transport);
        const errorState = findChild(shell, "error-state");
        verify(errorState.visible);
        verifyInsideWindow(errorState);
        verify(!playingState.visible);
        compare(errorState.Accessible.role, Accessible.AlertMessage);
        compare(errorState.Accessible.name, "Não foi possível abrir esta faixa. A faixa de teste está indisponível.");
        verify(!transport.enabled);
        verify(!progress.enabled);
        verify(!playPause.enabled);
        compare(findChild(shell, "error-title").text, "Não foi possível abrir esta faixa");
        compare(findChild(shell, "error-body").text, "A faixa de teste está indisponível.");
        compare(findChild(shell, "error-context-title").text, "Canção nº 17 — São João, ação e café ☕");
        compare(progress.from, 0);
        compare(progress.to, 0);
        compare(progress.value, 0);
        const errorSkip = findChild(shell, "error-skip");
        compare(errorSkip.Accessible.role, Accessible.Button);
        compare(errorSkip.Accessible.name, "Pular");
        verify(errorSkip.width >= 44);
        verify(errorSkip.height >= 44);
        verifyInsideWindow(findChild(shell, "error-title"));
        verifyInsideWindow(findChild(shell, "error-body"));
        verifyInsideWindow(findChild(shell, "error-context-title"));
        verifyInsideWindow(errorSkip);
        verifyInsideWindow(transport);
        for (const errorControl of emptyTransportControls) {
            verifyInsideWindow(errorControl);
            compare(errorControl.Accessible.focusable, false);
            verify(!errorControl.activeFocus);
        }
        verifyHorizontalNonOverlap(emptyTransportControls);
        errorSkip.forceActiveFocus(Qt.TabFocusReason);
        tryVerify(function () {
            return errorSkip.activeFocus;
        });
        compare(errorSkip.Accessible.focused, true);
        compare(errorSkip.background.border.width, 3);
        errorSkip.Accessible.pressAction();
        wait(0);
        compare(findChild(shell.contentItem, "transport-bar"), transport);
        verify(transport.enabled);
        verify(playingState.visible);
        verify(!errorState.visible);
        verify(progress.enabled);
        compare(findChild(shell, "current-title").text, "Canção nº 17 — São João, ação e café ☕");
    }
}
