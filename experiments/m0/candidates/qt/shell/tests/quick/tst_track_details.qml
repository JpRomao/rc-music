import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "TrackDetails"
    when: windowShown

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testDetailsFixturePresenter // qmllint disable unqualified
        }
    }

    Component {
        id: localShellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testLocalPlayerPresenter // qmllint disable unqualified
        }
    }

    function isInside(item, ancestor) {
        while (item) {
            if (item === ancestor)
                return true;
            item = item.parent;
        }
        return false;
    }

    function isFullyVisibleInViewport(item, viewport) {
        const mapped = item.mapToItem(viewport, 0, 0);
        return mapped.y >= 0 && mapped.y + item.height <= viewport.height;
    }

    function verifyFocusCycle(controls, viewport) {
        controls[0].forceActiveFocus(Qt.TabFocusReason);
        for (let index = 1; index < controls.length; ++index) {
            keyClick(Qt.Key_Tab);
            tryVerify(function () {
                return controls[index].activeFocus;
            }, 5000, "Tab should focus " + controls[index].objectName);
            tryVerify(function () {
                return isFullyVisibleInViewport(controls[index], viewport);
            }, 5000, "Tab focus should be visible for " + controls[index].objectName);
        }
        keyClick(Qt.Key_Tab);
        tryVerify(function () {
            return controls[0].activeFocus;
        }, 5000, "Tab should wrap to " + controls[0].objectName);
        for (let index = controls.length - 1; index >= 0; --index) {
            keyClick(Qt.Key_Backtab);
            tryVerify(function () {
                return controls[index].activeFocus;
            }, 5000, "Backtab should focus " + controls[index].objectName);
            if (index > 0)
                tryVerify(function () {
                    return isFullyVisibleInViewport(controls[index], viewport);
                }, 5000, "Backtab focus should be visible for " + controls[index].objectName);
        }
    }

    function test_fixtureDetailsAreModalCompleteAndFocusSafe() {
        const shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.width = 720;
        shell.height = 520;
        shell.requestActivate();
        tryVerify(function () {
            return shell.active;
        });

        mouseClick(findChild(shell, "open-audio"));
        tryCompare(testDetailsFixturePresenter, "playbackState", PlayerPresenter.Playing); // qmllint disable unqualified
        verify(testDetailsFixturePresenter.togglePlayback()); // qmllint disable unqualified
        tryCompare(testDetailsFixturePresenter, "playbackState", PlayerPresenter.Paused); // qmllint disable unqualified

        const opener = findChild(shell, "track-details-playing");
        verify(opener);
        compare(opener.Accessible.role, Accessible.Button);
        compare(opener.Accessible.name, "Detalhes da faixa");
        testScenarioDriver.clearAccessibleAnnouncements(); // qmllint disable unqualified
        opener.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);

        const panel = findChild(shell, "track-details-panel");
        tryVerify(function () {
            return panel.opened;
        });
        compare(panel.contentItem.Accessible.role, Accessible.Dialog);
        compare(panel.contentItem.Accessible.name, "Detalhes da faixa");
        compare(testScenarioDriver.accessibleAnnouncementCount(), 1); // qmllint disable unqualified
        compare(testScenarioDriver.lastAccessibleAnnouncementPoliteness(), Accessible.Polite); // qmllint disable unqualified
        verify(isInside(shell.activeFocusItem, panel.contentItem));
        compare(panel.width, 672);
        verify(panel.height <= 472);
        verify(!findChild(shell, "source-drop-area").enabled);

        compare(findChild(shell, "details-file-name-value").text, "Canção nº 17 — São João, ação e café ☕.m4a");
        const path = findChild(shell, "details-file-path-value");
        compare(path.text, "Não informado");
        compare(path.textFormat, Text.PlainText);
        compare(findChild(shell, "details-file-size-value").text, "7.483.921 bytes");
        compare(findChild(shell, "details-modified-value").text, "2026-08-23T00:00:00Z");
        compare(findChild(shell, "details-title-value").text, "Canção nº 17 — São João, ação e café ☕");
        compare(findChild(shell, "details-artist-value").text, "Artista Sintética Ç");
        compare(findChild(shell, "details-album-value").text, "Coleção Ação e Emoção");
        compare(findChild(shell, "details-duration-value").text, "04:08");
        compare(findChild(shell, "details-container-value").text, "M4A");
        compare(findChild(shell, "details-codec-value").text, "AAC-LC");
        compare(findChild(shell, "details-bitrate-value").text, "256 kbps");
        compare(findChild(shell, "details-sample-rate-value").text, "48.000 Hz");
        compare(findChild(shell, "details-channels-value").text, "2");
        compare(findChild(shell, "details-bit-depth-value").text, "Não informado");
        compare(findChild(shell, "details-diagnostics-empty").text, "Nenhum diagnóstico.");

        testScenarioDriver.setHighContrast(true, false); // qmllint disable unqualified
        compare(panel.background.color.a, 1);
        compare(panel.background.border.width, 2);
        testScenarioDriver.setHighContrast(false, false); // qmllint disable unqualified

        const occurrence = testDetailsFixturePresenter.currentOccurrenceId; // qmllint disable unqualified
        const position = testDetailsFixturePresenter.positionMs; // qmllint disable unqualified
        keyClick(Qt.Key_L, Qt.ControlModifier);
        keyClick(Qt.Key_Comma, Qt.ControlModifier);
        keyClick(Qt.Key_Right, Qt.ControlModifier);
        verify(panel.opened);
        verify(!findChild(shell, "queue-panel").visible);
        verify(!findChild(shell, "preferences-panel").visible);
        compare(testDetailsFixturePresenter.currentOccurrenceId, occurrence); // qmllint disable unqualified
        compare(testDetailsFixturePresenter.positionMs, position); // qmllint disable unqualified

        for (let index = 0; index < 12; ++index) {
            keyClick(Qt.Key_Tab);
            verify(isInside(shell.activeFocusItem, panel.contentItem));
        }
        for (let index = 0; index < 12; ++index) {
            keyClick(Qt.Key_Tab, Qt.ShiftModifier);
            verify(isInside(shell.activeFocusItem, panel.contentItem));
        }

        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !panel.visible && opener.activeFocus;
        });
    }

    function test_localPartialDetailsUpdateCopyAndRemainActionableOnError() {
        testScenarioDriver.resetLocalScenario(); // qmllint disable unqualified
        const shell = createTemporaryObject(localShellComponent, null);
        verify(shell);
        shell.width = 720;
        shell.height = 520;
        shell.requestActivate();
        tryVerify(function () {
            return shell.active;
        });

        testScenarioDriver.enqueueLocalSelection(["C:/Música/Coleção 日本/Faixa número 07.mp3"]); // qmllint disable unqualified
        mouseClick(findChild(shell, "open-audio"));
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Loading); // qmllint disable unqualified

        const opener = findChild(shell, "track-details-playing");
        opener.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        const panel = findChild(shell, "track-details-panel");
        tryVerify(function () {
            return panel.opened;
        });
        const path = findChild(shell, "details-file-path-value");
        compare(path.text, "C:/Música/Coleção 日本/Faixa número 07.mp3");
        compare(findChild(shell, "details-file-size-value").text, "Não informado");
        compare(findChild(shell, "details-duration-value").text, "Não informado");
        compare(findChild(shell, "details-bitrate-value").text, "Não informado");
        compare(findChild(shell, "details-sample-rate-value").text, "Não informado");
        compare(findChild(shell, "details-channels-value").text, "Não informado");
        compare(findChild(shell, "details-bit-depth-value").text, "Não informado");

        const copyPath = findChild(shell, "details-copy-path");
        copyPath.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        compare(testScenarioDriver.clipboardText(), path.text); // qmllint disable unqualified
        const focusBeforeMetadata = shell.activeFocusItem;
        testScenarioDriver.publishCompleteLocalMetadata(); // qmllint disable unqualified
        tryCompare(findChild(shell, "details-file-size-value"), "text", "4.096 bytes");
        compare(shell.activeFocusItem, focusBeforeMetadata);
        compare(findChild(shell, "details-title-value").text, "Título controlado");
        compare(findChild(shell, "details-track-number-value").text, "7");
        compare(findChild(shell, "details-year-value").text, "2026");
        compare(findChild(shell, "details-duration-value").text, "02:03");
        compare(findChild(shell, "details-bitrate-value").text, "320 kbps");
        compare(findChild(shell, "details-bit-depth-value").text, "24 bits");

        const diagnosticsToggle = findChild(shell, "details-diagnostics-toggle");
        verify(diagnosticsToggle.visible);
        compare(diagnosticsToggle.Accessible.checked, false);
        diagnosticsToggle.Accessible.pressAction();
        compare(diagnosticsToggle.Accessible.checked, true);
        const diagnostics = findChild(shell, "details-diagnostics-value");
        verify(diagnostics.visible);
        verify(diagnostics.text.includes("16.385 bytes"));
        verify(diagnostics.text.includes("16.384 bytes"));
        verify(diagnostics.text.includes("4.097 × 320 px"));
        verify(!diagnostics.text.includes("DO NOT EXPOSE"));
        const copyDiagnostics = findChild(shell, "details-copy-diagnostics");
        const close = findChild(shell, "track-details-close");
        const scroll = findChild(shell, "track-details-scroll");
        verifyFocusCycle([close, path, copyPath, diagnosticsToggle, diagnostics, copyDiagnostics], scroll);
        copyDiagnostics.Accessible.pressAction();
        compare(testScenarioDriver.clipboardText(), diagnostics.text); // qmllint disable unqualified

        copyDiagnostics.forceActiveFocus(Qt.TabFocusReason);
        testScenarioDriver.enqueueLocalSelection(["C:/Música/Coleção 日本/Próxima faixa.mp3"]); // qmllint disable unqualified
        verify(testLocalPlayerPresenter.openAudio()); // qmllint disable unqualified
        tryVerify(function () {
            return close.activeFocus;
        }, 5000, "Focus should move safely when focused diagnostics controls disappear");

        testScenarioDriver.publishLocalError(); // qmllint disable unqualified
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Error); // qmllint disable unqualified

        keyClick(Qt.Key_Escape);
        const fallback = findChild(shell, "preferences");
        tryVerify(function () {
            return !panel.visible && fallback.activeFocus;
        });
        shell.destroy();
        wait(0);

        testScenarioDriver.resetLocalScenario(); // qmllint disable unqualified
        const errorShell = createTemporaryObject(localShellComponent, null);
        verify(errorShell);
        errorShell.requestActivate();
        tryVerify(function () {
            return errorShell.active;
        });
        testScenarioDriver.enqueueLocalSelection(["C:/Música/Falha secreta.flac"]); // qmllint disable unqualified
        mouseClick(findChild(errorShell, "open-audio"));
        testScenarioDriver.publishFailedLocalMetadata(); // qmllint disable unqualified
        tryVerify(function () {
            return testLocalPlayerPresenter.hasDetailsDiagnostics; // qmllint disable unqualified
        });
        testScenarioDriver.publishLocalError(); // qmllint disable unqualified
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Error); // qmllint disable unqualified
        const errorOpener = findChild(errorShell, "track-details-error");
        verify(errorOpener);
        errorOpener.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        const errorPanel = findChild(errorShell, "track-details-panel");
        tryVerify(function () {
            return errorPanel.opened;
        });
        findChild(errorShell, "details-diagnostics-toggle").Accessible.pressAction();
        const errorDiagnostics = findChild(errorShell, "details-diagnostics-value");
        verify(errorDiagnostics.text.includes("Não foi possível ler os metadados deste arquivo."));
        verify(errorDiagnostics.text.includes("A reprodução falhou."));
        verify(!errorDiagnostics.text.includes("DO NOT EXPOSE"));
        compare(findChild(errorShell, "details-file-size-value").text, "Não informado");
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !errorPanel.visible && errorOpener.activeFocus;
        });
        errorShell.destroy();
        testScenarioDriver.resetLocalScenario(); // qmllint disable unqualified
    }

    function test_byteCountsBeyondJavaScriptSafeIntegerRemainExact() {
        testScenarioDriver.resetLocalScenario(); // qmllint disable unqualified
        const shell = createTemporaryObject(localShellComponent, null);
        verify(shell);
        shell.width = 720;
        shell.height = 520;
        shell.requestActivate();
        tryVerify(function () {
            return shell.active;
        });

        testScenarioDriver.enqueueLocalSelection(["C:/Música/Contagens exatas.flac"]); // qmllint disable unqualified
        mouseClick(findChild(shell, "open-audio"));
        testScenarioDriver.publishExactLargeLocalMetadata(); // qmllint disable unqualified
        tryVerify(function () {
            return testLocalPlayerPresenter.hasDetailsDiagnostics; // qmllint disable unqualified
        });

        const opener = findChild(shell, "track-details-playing");
        opener.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        const panel = findChild(shell, "track-details-panel");
        tryVerify(function () {
            return panel.opened;
        });
        compare(findChild(shell, "details-file-size-value").text, "9.007.199.254.740.993 bytes");

        findChild(shell, "details-diagnostics-toggle").Accessible.pressAction();
        const diagnostics = findChild(shell, "details-diagnostics-value").text;
        verify(diagnostics.includes("9.007.199.254.740.993 bytes"));
        verify(diagnostics.includes("9.007.199.254.740.992 bytes"));
        verify(diagnostics.includes("9.007.199.254.740.991 bytes codificados"));

        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !panel.visible && opener.activeFocus;
        });
        shell.destroy();
        testScenarioDriver.resetLocalScenario(); // qmllint disable unqualified
    }

    function test_closingUnderLifecycleNoticeDoesNotStealItsFocus() {
        testScenarioDriver.resetLocalScenario(); // qmllint disable unqualified
        testScenarioDriver.setTrayAvailable(true); // qmllint disable unqualified
        const shell = createTemporaryObject(localShellComponent, null);
        verify(shell);
        shell.width = 720;
        shell.height = 520;
        shell.requestActivate();
        tryVerify(function () {
            return shell.active;
        });

        testScenarioDriver.enqueueLocalSelection(["C:/Música/Faixa modal.mp3"]); // qmllint disable unqualified
        mouseClick(findChild(shell, "open-audio"));
        testScenarioDriver.publishCompleteLocalMetadata(); // qmllint disable unqualified
        const opener = findChild(shell, "track-details-playing");
        opener.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        const panel = findChild(shell, "track-details-panel");
        tryVerify(function () {
            return panel.opened;
        });
        findChild(shell, "details-diagnostics-toggle").Accessible.pressAction();
        const panelFocus = findChild(shell, "details-copy-diagnostics");
        panelFocus.forceActiveFocus(Qt.TabFocusReason);

        shell.close();
        const notice = findChild(shell, "window-notice");
        const confirm = findChild(shell, "window-notice-confirm");
        tryVerify(function () {
            return notice.opened && confirm.activeFocus;
        });
        testScenarioDriver.enqueueLocalSelection(["C:/Música/Faixa modal nova.mp3"]); // qmllint disable unqualified
        verify(testLocalPlayerPresenter.openAudio()); // qmllint disable unqualified
        tryVerify(function () {
            return !panelFocus.visible && confirm.activeFocus;
        }, 5000, "A disappearing details control must not steal focus from the competing modal");
        testLocalPlayerPresenter.cancelWindowNotice(); // qmllint disable unqualified
        const panelFallback = findChild(shell, "track-details-close");
        tryVerify(function () {
            return panel.opened && panelFallback.activeFocus;
        }, 5000, "Focus should return safely after the competing modal closes");

        shell.close();
        tryVerify(function () {
            return notice.opened && confirm.activeFocus;
        });
        panel.close();
        wait(0);
        verify(confirm.activeFocus);

        testLocalPlayerPresenter.cancelWindowNotice(); // qmllint disable unqualified
        shell.destroy();
        testScenarioDriver.resetLocalScenario(); // qmllint disable unqualified
        testScenarioDriver.setTrayAvailable(false); // qmllint disable unqualified
    }
}
