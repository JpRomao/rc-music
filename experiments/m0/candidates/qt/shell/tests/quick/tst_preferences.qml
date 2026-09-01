import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "ShellPreferences"
    when: windowShown

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testPlayerPresenter // qmllint disable unqualified
        }
    }

    property var shell

    function enterText(field, text) {
        field.forceActiveFocus(Qt.TabFocusReason);
        tryVerify(function () {
            return field.activeFocus;
        });
        keyClick(Qt.Key_A, Qt.ControlModifier);
        keyClick(Qt.Key_Backspace);
        for (let index = 0; index < text.length; ++index)
            keyClick(text.charCodeAt(index));
        compare(field.text, text);
    }
    function luminance(color) {
        function linear(value) {
            return value <= 0.04045 ? value / 12.92 : Math.pow((value + 0.055) / 1.055, 2.4);
        }
        return 0.2126 * linear(color.r) + 0.7152 * linear(color.g) + 0.0722 * linear(color.b);
    }
    function verifyChoiceContrast(control) {
        const list = control.popup.contentItem;
        for (let index = 0; index < control.count; ++index) {
            const row = list.itemAtIndex(index);
            verify(row);
            verify(row.height >= 40, control.objectName + " option " + index + " height: " + row.height);
            const foreground = luminance(row.contentItem.color);
            const background = luminance(row.background.visible ? row.background.color : control.popup.background.color);
            const ratio = (Math.max(foreground, background) + 0.05) / (Math.min(foreground, background) + 0.05);
            verify(ratio >= 4.5, control.objectName + " option " + index + " contrast: " + ratio);
        }
    }
    function selectChoice(control, index) {
        control.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        tryVerify(function () {
            return control.popup.opened;
        });
        keyClick(Qt.Key_Home);
        for (let step = 0; step < index; ++step)
            keyClick(Qt.Key_Down);
        verifyChoiceContrast(control);
        keyClick(Qt.Key_Return);
        tryVerify(function () {
            return !control.popup.visible;
        });
        compare(control.currentIndex, index);
    }
    function verifyControl(control, name) {
        verify(control);
        compare(control.Accessible.name, name);
        compare(control.Accessible.id, control.objectName);
        verify(control.width >= 40);
        verify(control.height >= 40);
        const point = control.mapToItem(shell.contentItem, 0, 0);
        verify(point.x >= 0 && point.y >= 0);
        verify(point.x + control.width <= shell.width + 0.5);
        verify(point.y + control.height <= shell.height + 0.5);
    }
    function verifyFocusCycle(items) {
        items[0].forceActiveFocus(Qt.TabFocusReason);
        for (let index = 1; index <= items.length; ++index) {
            keyClick(Qt.Key_Tab);
            const expected = items[index % items.length];
            tryVerify(function () {
                return expected.activeFocus;
            });
        }
        for (let index = items.length - 1; index >= 0; --index) {
            keyClick(Qt.Key_Tab, Qt.ShiftModifier);
            const expected = items[index];
            tryVerify(function () {
                return expected.activeFocus;
            });
        }
    }

    function test_preferencesJourney() {
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.width = 720;
        shell.height = 520;
        shell.requestActivate();
        tryVerify(function () {
            return shell.active;
        });
        verify(!testPlayerPresenter.queueLoaded); // qmllint disable unqualified
        const preferences = findChild(shell, "preferences");
        verifyControl(preferences, "Preferências");
        compare(preferences.Accessible.role, Accessible.Button);
        verify(preferences.enabled);
        const panel = findChild(shell, "preferences-panel");
        verify(panel);
        verify(!panel.visible);
        mouseClick(preferences);
        tryVerify(function () {
            return panel.opened;
        });
        const general = findChild(shell, "preferences-general");
        const playback = findChild(shell, "preferences-playback");
        const appearance = findChild(shell, "preferences-appearance");
        const close = findChild(shell, "preferences-close");
        verifyControl(general, "Geral");
        verifyControl(playback, "Reprodução");
        verifyControl(appearance, "Aparência");
        verifyControl(close, "Fechar preferências");
        verify(general.activeFocus);
        compare(panel.section, 0);
        mouseClick(general);
        verify(general.checked);
        keyClick(Qt.Key_Space);
        verify(general.checked);
        verify(!playback.checked && !appearance.checked);
        const feedback = findChild(close.background, "hover-feedback");
        verify(feedback);
        mouseMove(general);
        tryCompare(feedback, "opacity", 0);
        mouseMove(close);
        tryCompare(feedback, "opacity", 0.08, 300);
        const closeBehavior = findChild(shell, "close-behavior");
        const restoreSession = findChild(shell, "restore-session");
        const startMinimized = findChild(shell, "start-minimized");
        testScenarioDriver.resetDefaultAppsSettings(); // qmllint disable unqualified
        const defaultApps = findChild(shell, "open-default-apps-settings");
        const defaultAppsFeedback = findChild(shell, "default-apps-settings-feedback");
        verifyControl(closeBehavior, "Ao fechar");
        verifyControl(restoreSession, "Restaurar última sessão");
        verifyControl(startMinimized, "Iniciar minimizado");
        verifyControl(defaultApps, "Abrir Aplicativos Padrão");
        verify(defaultAppsFeedback);
        compare(defaultApps.Accessible.role, Accessible.Button);
        compare(defaultApps.Accessible.description, "Abre as configurações oficiais do Windows em outra janela.");
        const defaultAppsAccessible = testScenarioDriver.accessibleSnapshot(defaultApps); // qmllint disable unqualified
        verify(defaultAppsAccessible.valid);
        compare(defaultAppsAccessible.role, Accessible.Button);
        compare(defaultAppsAccessible.name, "Abrir Aplicativos Padrão");
        compare(defaultAppsAccessible.description, "Abre as configurações oficiais do Windows em outra janela.");
        compare(closeBehavior.Accessible.role, Accessible.ComboBox);
        compare(restoreSession.Accessible.role, Accessible.CheckBox);
        compare(closeBehavior.currentIndex, 0);
        verify(restoreSession.checked);
        verify(!startMinimized.checked);
        verify(!startMinimized.enabled);
        testScenarioDriver.setTrayAvailable(true); // qmllint disable unqualified
        verify(startMinimized.enabled);
        startMinimized.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        verify(startMinimized.checked);
        verify(testPlayerPresenter.startMinimized); // qmllint disable unqualified
        testScenarioDriver.setTrayAvailable(false); // qmllint disable unqualified
        verify(startMinimized.enabled);
        keyClick(Qt.Key_Space);
        verify(!startMinimized.checked);
        verify(!testPlayerPresenter.startMinimized); // qmllint disable unqualified
        verify(!startMinimized.enabled);
        verifyFocusCycle([general, playback, appearance, closeBehavior, restoreSession, defaultApps, close]);
        testScenarioDriver.setDefaultAppsSettingsResult(2); // qmllint disable unqualified
        testScenarioDriver.clearAccessibleAnnouncements(); // qmllint disable unqualified
        defaultApps.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        tryVerify(function () {
            return testScenarioDriver.defaultAppsSettingsCalls() === 1; // qmllint disable unqualified
        });
        tryVerify(function () {
            return defaultAppsFeedback.visible;
        });
        compare(defaultAppsFeedback.text, "Não foi possível abrir Aplicativos Padrão. Abra Configurações > Aplicativos > Aplicativos Padrão.");
        compare(defaultAppsFeedback.Accessible.role, Accessible.AlertMessage);
        compare(defaultAppsFeedback.Accessible.name, defaultAppsFeedback.text);
        const defaultAppsFeedbackPoint = defaultAppsFeedback.mapToItem(shell.contentItem, 0, 0);
        verify(defaultAppsFeedbackPoint.x >= 0 && defaultAppsFeedbackPoint.y >= 0);
        verify(defaultAppsFeedbackPoint.x + defaultAppsFeedback.width <= shell.width + 0.5);
        verify(defaultAppsFeedbackPoint.y + defaultAppsFeedback.height <= shell.height + 0.5);
        tryVerify(function () {
            return testScenarioDriver.accessibleAnnouncementCount() === 1; // qmllint disable unqualified
        });
        compare(testScenarioDriver.lastAccessibleAnnouncement(), defaultAppsFeedback.text); // qmllint disable unqualified
        compare(testScenarioDriver.lastAccessibleAnnouncementPoliteness(), Accessible.Assertive); // qmllint disable unqualified
        testScenarioDriver.setDefaultAppsSettingsResult(0); // qmllint disable unqualified
        keyClick(Qt.Key_Return);
        tryVerify(function () {
            return testScenarioDriver.defaultAppsSettingsCalls() === 2; // qmllint disable unqualified
        });
        tryVerify(function () {
            return !defaultAppsFeedback.visible;
        });
        testScenarioDriver.setDefaultAppsSettingsAvailable(false); // qmllint disable unqualified
        verify(!defaultApps.enabled);
        compare(defaultApps.Accessible.description, "As configurações de Aplicativos Padrão do Windows não estão disponíveis.");
        verify(defaultAppsFeedback.visible);
        compare(defaultAppsFeedback.text, "As configurações de Aplicativos Padrão do Windows não estão disponíveis.");
        tryVerify(function () {
            return testScenarioDriver.accessibleAnnouncementCount() === 2; // qmllint disable unqualified
        });
        compare(testScenarioDriver.lastAccessibleAnnouncement(), defaultAppsFeedback.text); // qmllint disable unqualified
        testScenarioDriver.setTrayAvailable(true); // qmllint disable unqualified
        testScenarioDriver.setTrayAvailable(false); // qmllint disable unqualified
        wait(1);
        compare(testScenarioDriver.accessibleAnnouncementCount(), 2); // qmllint disable unqualified
        verifyFocusCycle([general, playback, appearance, closeBehavior, restoreSession, close]);
        testScenarioDriver.setDefaultAppsSettingsAvailable(true); // qmllint disable unqualified
        verify(defaultApps.enabled);
        verify(!defaultAppsFeedback.visible);
        testScenarioDriver.setDefaultAppsSettingsSupported(false); // qmllint disable unqualified
        verify(!defaultApps.visible);
        verify(!defaultAppsFeedback.visible);
        verifyFocusCycle([general, playback, appearance, closeBehavior, restoreSession, close]);
        testScenarioDriver.setDefaultAppsSettingsSupported(true); // qmllint disable unqualified
        verify(defaultApps.visible);
        verify(defaultApps.enabled);
        closeBehavior.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        tryVerify(function () {
            return closeBehavior.popup.opened;
        });
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !closeBehavior.popup.visible;
        });
        verify(panel.opened);
        verify(closeBehavior.activeFocus);
        const memoryNote = findChild(shell, "preferences-memory-note");
        verify(memoryNote.visible);
        compare(memoryNote.text, "As preferências são salvas localmente. Quando a restauração da última sessão está ativa, ela é retomada pausada; iniciar minimizado mantém o aplicativo na bandeja quando ela está disponível.");
        restoreSession.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        verify(!testPlayerPresenter.restoreLastSession); // qmllint disable unqualified
        selectChoice(closeBehavior, 1);
        compare(testPlayerPresenter.closePreference, PlayerPresenter.Exit); // qmllint disable unqualified
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !panel.visible && preferences.activeFocus;
        });
        testScenarioDriver.setDefaultAppsSettingsAvailable(false); // qmllint disable unqualified
        wait(1);
        compare(testScenarioDriver.accessibleAnnouncementCount(), 2); // qmllint disable unqualified
        testScenarioDriver.setDefaultAppsSettingsAvailable(true); // qmllint disable unqualified
        mouseClick(findChild(shell, "open-audio"));
        mouseClick(findChild(shell, "play-pause"));
        compare(testPlayerPresenter.playbackState, PlayerPresenter.Paused); // qmllint disable unqualified
        keyClick(Qt.Key_L, Qt.ControlModifier);
        verify(findChild(shell, "queue-panel").visible);
        keyClick(Qt.Key_Comma, Qt.ControlModifier);
        tryVerify(function () {
            return panel.opened && general.activeFocus;
        });
        verify(!findChild(shell, "queue-panel").visible);
        keyClick(Qt.Key_Comma, Qt.ControlModifier);
        verify(panel.visible);
        mouseClick(playback);
        compare(panel.section, 1);
        const backward = findChild(shell, "skip-backward-seconds");
        const forward = findChild(shell, "skip-forward-seconds");
        const queueEnd = findChild(shell, "queue-end-behavior");
        verifyControl(backward, "Segundos para retroceder");
        verifyControl(forward, "Segundos para avançar");
        verifyControl(queueEnd, "Ao terminar a fila");
        compare(backward.Accessible.role, Accessible.EditableText);
        compare(forward.Accessible.role, Accessible.EditableText);
        verifyFocusCycle([general, playback, appearance, backward, forward, queueEnd, close]);
        compare(backward.text, "5");
        compare(forward.text, "5");
        enterText(backward, "7");
        compare(testPlayerPresenter.skipBackwardSeconds, 7); // qmllint disable unqualified
        compare(findChild(shell, "skip-backward").text, "−7");
        compare(findChild(shell, "skip-backward").Accessible.name, "Voltar 7 segundos");
        enterText(forward, "20");
        compare(testPlayerPresenter.skipForwardSeconds, 20); // qmllint disable unqualified
        compare(findChild(shell, "skip-forward").text, "+20");
        compare(findChild(shell, "skip-forward").Accessible.name, "Avançar 20 segundos");
        enterText(backward, "1");
        enterText(forward, "1");
        compare(findChild(shell, "skip-backward").Accessible.name, "Voltar 1 segundo");
        compare(findChild(shell, "skip-forward").Accessible.name, "Avançar 1 segundo");
        enterText(backward, "7");
        enterText(forward, "20");
        const invalid = findChild(shell, "skip-backward-seconds-error");
        for (const draft of ["", "0", "301", "1.5"]) {
            enterText(backward, draft);
            verify(invalid.visible);
            compare(invalid.text, "Use um número inteiro entre 1 e 300.");
            compare(testPlayerPresenter.skipBackwardSeconds, draft === "301" ? 30 : (draft === "1.5" ? 1 : 7)); // qmllint disable unqualified
        }
        enterText(backward, "7");
        verify(!invalid.visible);
        keyClick(Qt.Key_Space);
        verify(invalid.visible);
        compare(testPlayerPresenter.playbackState, PlayerPresenter.Paused); // qmllint disable unqualified
        const positionBefore = testPlayerPresenter.positionMs; // qmllint disable unqualified
        keyClick(Qt.Key_Right, Qt.ControlModifier);
        keyClick(Qt.Key_Left, Qt.ControlModifier);
        keyClick(Qt.Key_L, Qt.ControlModifier);
        compare(testPlayerPresenter.currentOccurrenceId, "queue-0017"); // qmllint disable unqualified
        compare(testPlayerPresenter.positionMs, positionBefore); // qmllint disable unqualified
        verify(!findChild(shell, "queue-panel").visible);
        selectChoice(queueEnd, 1);
        compare(testPlayerPresenter.queueEndPreference, PlayerPresenter.StopClearSelection); // qmllint disable unqualified
        mouseClick(appearance);
        compare(panel.section, 2);
        const theme = findChild(shell, "theme-mode");
        const motion = findChild(shell, "motion-mode");
        verifyControl(theme, "Tema");
        verifyControl(motion, "Movimento");
        verifyFocusCycle([general, playback, appearance, theme, motion, close]);
        selectChoice(theme, 2);
        compare(testPlayerPresenter.themePreference, PlayerPresenter.DarkTheme); // qmllint disable unqualified
        compare(shell.color.toString(), "#111318");
        compare(panel.background.color.toString(), "#21252d");
        compare(findChild(shell, "progress").palette.highlight.toString(), "#8b83ff");
        compare(findChild(shell, "volume").palette.text.toString(), "#f5f7fa");
        compare(findChild(shell, "progress").background.color.toString(), "#69717d");
        compare(findChild(shell, "progress").background.children[0].color.toString(), "#8b83ff");
        compare(findChild(shell, "progress").handle.color.toString(), "#181b21");
        selectChoice(theme, 1);
        compare(shell.color.toString(), "#f6f7f9");
        testScenarioDriver.setSystemAppearance(true, false); // qmllint disable unqualified
        compare(shell.color.toString(), "#f6f7f9");
        selectChoice(theme, 0);
        compare(shell.color.toString(), "#111318");
        testScenarioDriver.setSystemAppearance(false, true); // qmllint disable unqualified
        compare(shell.color.toString(), "#f6f7f9");
        verify(testShellUiContract.reducedMotion); // qmllint disable unqualified
        compare(testShellUiContract.panelTransitionMs, 0); // qmllint disable unqualified
        testScenarioDriver.setSystemAppearance(false, false); // qmllint disable unqualified
        verify(!testShellUiContract.reducedMotion); // qmllint disable unqualified
        compare(testShellUiContract.panelTransitionMs, 160); // qmllint disable unqualified
        selectChoice(motion, 1);
        verify(testShellUiContract.reducedMotion); // qmllint disable unqualified
        compare(testShellUiContract.feedbackTransitionMs, 0); // qmllint disable unqualified
        mouseMove(motion);
        compare(feedback.opacity, 0);
        mouseMove(close);
        compare(feedback.opacity, 0.08);
        compare(testPlayerPresenter.currentOccurrenceId, "queue-0017"); // qmllint disable unqualified
        compare(testPlayerPresenter.positionMs, positionBefore); // qmllint disable unqualified
        mouseClick(close);
        tryVerify(function () {
            return !panel.visible && preferences.activeFocus;
        });
        keyClick(Qt.Key_Comma, Qt.ControlModifier);
        tryVerify(function () {
            return panel.opened && general.activeFocus;
        });
        compare(panel.section, 0);
        mouseClick(playback);
        compare(backward.text, "7");
        compare(forward.text, "20");
        verify(!invalid.visible);
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !panel.visible && preferences.activeFocus;
        });
    }

    function test_initialSystemDarkIsAppliedBeforeCreatingTheWindow() {
        testScenarioDriver.setSystemAppearance(true, false); // qmllint disable unqualified
        const initialShell = createTemporaryObject(shellComponent, null);
        verify(initialShell);
        compare(initialShell.color.toString(), "#111318");
        compare(findChild(initialShell, "empty-title").color.toString(), "#f5f7fa");
        testScenarioDriver.setSystemAppearance(false, false); // qmllint disable unqualified
        compare(initialShell.color.toString(), "#f6f7f9");
    }
}
