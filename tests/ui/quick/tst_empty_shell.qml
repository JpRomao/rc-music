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
    SignalSpy {
        id: queuePlaySpy
        target: testCase.shell ? findChild(testCase.shell, "queue-panel") : null
        signalName: "playRequested"
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
    function instantiatedQueueRows(listView) {
        let instantiated = 0;
        for (let index = 0; index < listView.count; ++index) {
            if (listView.itemAtIndex(index) !== null)
                ++instantiated;
        }
        return instantiated;
    }
    function revealQueueRow(listView, index) {
        listView.positionViewAtIndex(index, ListView.Center);
        listView.forceLayout();
        tryVerify(function () {
            return listView.itemAtIndex(index) !== null;
        });
        return listView.itemAtIndex(index);
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
        compare(button.Accessible.id, "open-audio");
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
        const queueButton = findChild(shell, "queue");
        verify(queueButton);
        compare(queueButton.Accessible.role, Accessible.Button);
        compare(queueButton.Accessible.name, "Fila");
        compare(queueButton.Accessible.id, "queue");
        verify(!queueButton.enabled);
        verify(!findChild(shell, "queue-panel").visible);
        verify(!transport.enabled);
        const progress = findChild(shell, "progress");
        const playPause = findChild(shell, "play-pause");
        const skipBackward = findChild(shell, "skip-backward");
        const skipForward = findChild(shell, "skip-forward");
        const volume = findChild(shell, "volume");
        const mute = findChild(shell, "mute");
        const previous = findChild(shell, "previous");
        const next = findChild(shell, "next");
        compare(progress.Accessible.id, "progress");
        compare(volume.Accessible.id, "volume");
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
        const embeddedArtwork = findChild(shell, "embedded-artwork");
        compare(embeddedArtwork.source.toString(), "");
        compare(embeddedArtwork.asynchronous, true);
        compare(embeddedArtwork.cache, false);
        compare(embeddedArtwork.retainWhileLoading, false);
        verify(!embeddedArtwork.visible);
        verify(embeddedArtwork.Accessible.ignored);
        verifyInsideWindow(cover);
        verifyInsideWindow(currentTitle);
        verifyInsideWindow(currentArtist);
        verifyInsideWindow(currentAlbum);
        const openAnotherAudio = findChild(shell, "open-another-audio");
        verify(openAnotherAudio);
        compare(openAnotherAudio.text, testShellUiContract.openAudioText); // qmllint disable unqualified
        compare(openAnotherAudio.Accessible.role, Accessible.Button);
        compare(openAnotherAudio.Accessible.name, testShellUiContract.openAudioText); // qmllint disable unqualified
        compare(openAnotherAudio.Accessible.id, "open-another-audio");
        verify(openAnotherAudio.width >= testShellUiContract.controlTargetPreferred); // qmllint disable unqualified
        verify(openAnotherAudio.height >= testShellUiContract.controlTargetPreferred); // qmllint disable unqualified
        verifyInsideWindow(openAnotherAudio);
        const playbackStateBeforeOpenAnother = testPlayerPresenter.playbackState; // qmllint disable unqualified
        openAnotherAudio.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        compare(openSpy.count, 2);
        compare(testPlayerPresenter.playbackState, playbackStateBeforeOpenAnother); // qmllint disable unqualified
        keyClick(Qt.Key_Return);
        compare(openSpy.count, 3);
        compare(testPlayerPresenter.playbackState, playbackStateBeforeOpenAnother); // qmllint disable unqualified
        mouseClick(openAnotherAudio, openAnotherAudio.width / 2, openAnotherAudio.height / 2, Qt.LeftButton);
        compare(openSpy.count, 4);
        compare(testPlayerPresenter.playbackState, playbackStateBeforeOpenAnother); // qmllint disable unqualified
        verify(queueButton.enabled);
        queueButton.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        const queuePanel = findChild(shell, "queue-panel");
        const stateLayer = findChild(shell, "state-layer");
        const queueList = findChild(shell, "queue-list");
        tryVerify(function () {
            return queuePanel.visible;
        });
        verifyInsideWindow(queuePanel);
        compare(queuePanel.width, testShellUiContract.queuePanelMinimumWidth); // qmllint disable unqualified
        compare(queuePanel.overlayMode, true);
        compare(queueList.count, 1000);
        compare(queueList.reuseItems, true);
        compare(queueList.cacheBuffer, 0);
        compare(queueList.currentIndex, 16);
        compare(queueList.Accessible.role, Accessible.List);
        compare(queueList.Accessible.name, "Fila");
        compare(queueList.Accessible.id, "queue-list");
        compare(queueList.Accessible.focused, true);
        verify(queueList.focusIndicatorVisible);
        compare(queueList.itemAtIndex(998), null);
        const currentQueueRow = revealQueueRow(queueList, 16);
        compare(currentQueueRow.occurrenceId, "queue-0017");
        compare(currentQueueRow.Accessible.id, "queue-0017");
        compare(currentQueueRow.Accessible.role, Accessible.ListItem);
        compare(currentQueueRow.Accessible.focusable, true);
        compare(currentQueueRow.Accessible.selectable, true);
        compare(currentQueueRow.Accessible.selected, true);
        compare(currentQueueRow.Accessible.focused, true);
        compare(currentQueueRow.background.border.width, 3);
        verify(currentQueueRow.Accessible.name.indexOf("Canção nº 17 — São João, ação e café ☕") >= 0);
        verify(instantiatedQueueRows(queueList) > 0);
        verify(instantiatedQueueRows(queueList) < 64);
        const queueOpenFocusOrder = [queueButton, findChild(shell, "preferences"), openAnotherAudio, findChild(shell, "track-details-playing"), progress, previous, skipBackward, playPause, skipForward, next, mute, volume, queueList];
        queueButton.forceActiveFocus(Qt.TabFocusReason);
        for (let focusIndex = 1; focusIndex < queueOpenFocusOrder.length; ++focusIndex) {
            keyClick(Qt.Key_Tab);
            tryVerify(function () {
                return queueOpenFocusOrder[focusIndex].activeFocus;
            });
        }
        for (let focusIndex = queueOpenFocusOrder.length - 2; focusIndex >= 0; --focusIndex) {
            keyClick(Qt.Key_Tab, Qt.ShiftModifier);
            tryVerify(function () {
                return queueOpenFocusOrder[focusIndex].activeFocus;
            });
        }
        queueList.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Down);
        compare(queueList.currentIndex, 17);
        const decomposedQueueRow = queueList.itemAtIndex(17);
        compare(decomposedQueueRow.occurrenceId, "queue-0018");
        compare(decomposedQueueRow.Accessible.selected, true);
        keyClick(Qt.Key_Up);
        compare(queueList.currentIndex, 16);
        const markupQueueRow = revealQueueRow(queueList, 41);
        compare(markupQueueRow.occurrenceId, "queue-0042");
        compare(markupQueueRow.title, "<b>Não interpretar</b> & \"aspas\" 'simples'");
        compare(findChild(markupQueueRow, "queue-row-title").textFormat, Text.PlainText);
        compare(findChild(markupQueueRow, "queue-row-title").text, markupQueueRow.title);
        const longQueueRow = revealQueueRow(queueList, 511);
        compare(longQueueRow.occurrenceId, "queue-0512");
        compare(findChild(longQueueRow, "queue-row-title").elide, Text.ElideRight);
        compare(findChild(longQueueRow, "queue-row-title").text, longQueueRow.title);
        const unavailableQueueRow = revealQueueRow(queueList, 998);
        compare(unavailableQueueRow.occurrenceId, "queue-0999");
        verify(!unavailableQueueRow.available);
        verify(unavailableQueueRow.Accessible.name.indexOf("Arquivo não disponível.") >= 0);
        compare(findChild(unavailableQueueRow, "queue-row-status").text, "Arquivo não disponível.");
        verify(findChild(unavailableQueueRow, "queue-row-status").visible);
        const finalQueueRow = revealQueueRow(queueList, 999);
        compare(finalQueueRow.occurrenceId, "queue-1000");
        compare(finalQueueRow.Accessible.id, "queue-1000");
        verify(finalQueueRow.available);
        verify(!findChild(finalQueueRow, "queue-row-status").visible);
        verify(finalQueueRow.Accessible.name.indexOf("Arquivo não disponível.") < 0);
        verify(instantiatedQueueRows(queueList) < 64);
        shell.width = 960;
        tryCompare(shell, "width", 960);
        tryCompare(queuePanel, "width", testShellUiContract.queuePanelMaximumWidth); // qmllint disable unqualified
        compare(queuePanel.overlayMode, false);
        const stateRight = stateLayer.mapToItem(shell.contentItem, stateLayer.width, 0).x;
        const panelLeft = queuePanel.mapToItem(shell.contentItem, 0, 0).x;
        verify(stateRight <= panelLeft + 0.5);
        shell.width = shell.minimumWidth;
        tryCompare(shell, "width", 720);
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !queuePanel.visible;
        });
        tryVerify(function () {
            return queueButton.activeFocus;
        });
        keyClick(Qt.Key_L, Qt.ControlModifier);
        tryVerify(function () {
            return queuePanel.visible && queueList.activeFocus;
        });
        keyClick(Qt.Key_L, Qt.ControlModifier);
        tryVerify(function () {
            return !queuePanel.visible && queueButton.activeFocus;
        });
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
            compare(transportButton.Accessible.id, objectName);
            verify(transportButton.width >= 44);
            verify(transportButton.height >= 44);
        }
        compare(skipBackward.text, "−5");
        compare(skipForward.text, "+5");
        verify(previous.enabled);
        verify(next.enabled);
        compare(volume.from, 0);
        compare(volume.to, 100);
        compare(volume.value, 80);
        compare(volume.Accessible.name, "Volume");
        compare(volume.stepSize, 1);
        verify(volume.height >= testShellUiContract.controlTargetMinimum); // qmllint disable unqualified
        verifyInsideWindow(volume);
        const focusOrder = [progress, previous, skipBackward, playPause, skipForward, next, mute, volume];
        for (const focusableControl of focusOrder)
            compare(focusableControl.Accessible.focusable, true);
        compare(previous.Accessible.focusable, true);
        compare(next.Accessible.focusable, true);
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
        compare(errorSkip.Accessible.id, "error-skip");
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
        queueButton.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        tryVerify(function () {
            return queuePanel.visible && queueList.activeFocus;
        });
        const queueAdd = findChild(shell, "queue-add");
        const queueMoveUp = findChild(shell, "queue-move-up");
        const queueMoveDown = findChild(shell, "queue-move-down");
        const queueRemove = findChild(shell, "queue-remove");
        const queueClear = findChild(shell, "queue-clear");
        const queueActions = [queueAdd, queueMoveUp, queueMoveDown, queueRemove, queueClear];
        const queueActionNames = ["Adicionar arquivos", "Mover para cima", "Mover para baixo", "Remover", "Limpar fila"];
        const queueActionIds = ["queue-add", "queue-move-up", "queue-move-down", "queue-remove", "queue-clear"];
        for (let queueActionIndex = 0; queueActionIndex < queueActions.length; ++queueActionIndex) {
            const queueAction = queueActions[queueActionIndex];
            verify(queueAction);
            compare(queueAction.Accessible.role, Accessible.Button);
            compare(queueAction.Accessible.name, queueActionNames[queueActionIndex]);
            compare(queueAction.Accessible.id, queueActionIds[queueActionIndex]);
            verify(queueAction.width >= testShellUiContract.controlTargetPreferred); // qmllint disable unqualified
            verify(queueAction.height >= testShellUiContract.controlTargetPreferred); // qmllint disable unqualified
            verifyInsideWindow(queueAction);
        }
        verify(!queueAdd.enabled);
        verify(!queueMoveUp.enabled);
        verify(!queueMoveDown.enabled);
        verify(!queueRemove.enabled);
        verify(!queueClear.enabled);
        const queueRowMenu = findChild(shell, "queue-row-menu");
        verify(queueRowMenu);
        keyClick(Qt.Key_Menu);
        tryVerify(function () {
            return queueRowMenu.visible;
        });
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !queueRowMenu.visible;
        });
        verify(queuePanel.visible);
        verify(queueList.activeFocus);
        verify(queuePlaySpy.valid);
        queuePlaySpy.clear();
        queueList.currentIndex = 16;
        const playableQueueRow = revealQueueRow(queueList, 16);
        queueList.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        compare(queuePlaySpy.count, 1);
        compare(queuePlaySpy.signalArguments[0][0], "queue-0017");
        verify(queueList.activeFocus);
        keyClick(Qt.Key_Space);
        compare(queuePlaySpy.count, 2);
        compare(queuePlaySpy.signalArguments[1][0], "queue-0017");
        mouseDoubleClickSequence(playableQueueRow);
        compare(queuePlaySpy.count, 3);
        compare(queuePlaySpy.signalArguments[2][0], "queue-0017");
        playableQueueRow.Accessible.pressAction();
        compare(queuePlaySpy.count, 4);
        compare(queuePlaySpy.signalArguments[3][0], "queue-0017");
        verify(queueList.activeFocus);
        queueList.currentIndex = 998;
        compare(queueList.currentIndex, 998);
        const selectedUnavailableQueueRow = revealQueueRow(queueList, 998);
        compare(selectedUnavailableQueueRow.occurrenceId, "queue-0999");
        compare(selectedUnavailableQueueRow.Accessible.selected, true);
        const errorQueueFocusOrder = [queueButton, findChild(shell, "preferences"), errorSkip, findChild(shell, "track-details-error"), queueList];
        queueButton.forceActiveFocus(Qt.TabFocusReason);
        for (let focusIndex = 1; focusIndex < errorQueueFocusOrder.length; ++focusIndex) {
            keyClick(Qt.Key_Tab);
            tryVerify(function () {
                return errorQueueFocusOrder[focusIndex].activeFocus;
            });
        }
        for (let focusIndex = errorQueueFocusOrder.length - 2; focusIndex >= 0; --focusIndex) {
            keyClick(Qt.Key_Tab, Qt.ShiftModifier);
            tryVerify(function () {
                return errorQueueFocusOrder[focusIndex].activeFocus;
            });
        }
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !queuePanel.visible && queueButton.activeFocus;
        });
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

        mouseClick(playPause);
        compare(testPlayerPresenter.playbackState, PlayerPresenter.Paused); // qmllint disable unqualified
        seekCommitCount = testScenarioDriver.seekCommitAttemptCount(); // qmllint disable unqualified
        mouseClick(previous);
        compare(testScenarioDriver.seekCommitAttemptCount(), seekCommitCount + 1); // qmllint disable unqualified
        compare(testPlayerPresenter.currentOccurrenceId, "queue-0017"); // qmllint disable unqualified
        compare(testPlayerPresenter.positionMs, 0); // qmllint disable unqualified
        verify(!previous.enabled);
        verify(!next.enabled);
        tryVerify(function () {
            return !testPlayerPresenter.seekPending;
        }, 500); // qmllint disable unqualified
        compare(testPlayerPresenter.playbackState, PlayerPresenter.Paused); // qmllint disable unqualified
        previous.Accessible.pressAction();
        compare(testPlayerPresenter.currentOccurrenceId, "queue-0016"); // qmllint disable unqualified
        next.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        compare(testPlayerPresenter.currentOccurrenceId, "queue-0017"); // qmllint disable unqualified
        keyClick(Qt.Key_Left, Qt.ControlModifier);
        compare(testPlayerPresenter.currentOccurrenceId, "queue-0016"); // qmllint disable unqualified
        keyClick(Qt.Key_Right, Qt.ControlModifier);
        compare(testPlayerPresenter.currentOccurrenceId, "queue-0017"); // qmllint disable unqualified

        shell.width = 960;
        tryCompare(shell, "width", 960);
        keyClick(Qt.Key_L, Qt.ControlModifier);
        tryVerify(function () {
            return queuePanel.visible && queueList.activeFocus;
        });
        next.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        compare(testPlayerPresenter.currentOccurrenceId, "queue-0018"); // qmllint disable unqualified
        wait(0);
        verify(next.activeFocus, "Track changes must not steal transport focus for the queue");
        compare(queueList.currentIndex, 17);
        for (let index = 17; index < 511; ++index)
            verify(testPlayerPresenter.nextTrack()); // qmllint disable unqualified
        wait(0);
        compare(testPlayerPresenter.currentOccurrenceId, "queue-0512"); // qmllint disable unqualified
        compare(currentTitle.textFormat, Text.PlainText);
        compare(currentTitle.text, testPlayerPresenter.currentTitle); // qmllint disable unqualified
        const currentTitleLeft = currentTitle.mapToItem(shell.contentItem, 0, 0).x;
        const currentTitleRight = currentTitle.mapToItem(shell.contentItem, currentTitle.width, 0).x;
        const currentStateLeft = stateLayer.mapToItem(shell.contentItem, 0, 0).x;
        const currentStateRight = stateLayer.mapToItem(shell.contentItem, stateLayer.width, 0).x;
        verify(currentTitleLeft >= currentStateLeft - 0.5);
        verify(currentTitleRight <= currentStateRight + 0.5);
        verifyInsideWindow(currentTitle);
        verify(next.activeFocus);
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !queuePanel.visible && queueButton.activeFocus;
        });
    }
}
