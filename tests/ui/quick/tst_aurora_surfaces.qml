import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "AuroraSurfaces"
    when: windowShown
    readonly property var contract: testProductUiContract // qmllint disable unqualified
    readonly property var driver: testScenarioDriver // qmllint disable unqualified

    function cleanup() {
        driver.setHighContrast(false, false);
        driver.setSystemAppearance(false, false);
    }
    function sameColor(actual, expected, context) {
        verify(Qt.colorEqual(actual, expected), context + ": " + actual + " != " + expected);
    }
    function child(item, name) {
        const found = findChild(item, name);
        verify(found, "missing child " + name);
        return found;
    }

    Component {
        id: spyComponent
        SignalSpy {}
    }
    Component {
        id: drawerHeaderHost
        Window {
            property alias piece: hosted
            width: 480
            height: 320
            visible: true
            color: testCase.contract.canvasColor
            AuroraDrawerHeader {
                id: hosted
                contract: testCase.contract
                controlId: "queue-drawer"
                closeText: "Fechar fila"
                width: 400
            }
        }
    }
    Component {
        id: noticeHost
        Window {
            property alias piece: hosted
            width: 480
            height: 320
            visible: true
            color: testCase.contract.canvasColor
            AuroraNotice {
                id: hosted
                contract: testCase.contract
                controlId: "settings-notice"
                width: 440
            }
        }
    }
    Component {
        id: floatingCardHost
        Window {
            property alias piece: hosted
            width: 480
            height: 320
            visible: true
            color: testCase.contract.canvasColor
            AuroraFloatingCard {
                id: hosted
                contract: testCase.contract
                x: 40
                y: 40
                width: 240
                height: 120
            }
        }
    }
    Component {
        id: feedbackHost
        Window {
            property alias piece: hosted
            width: 480
            height: 320
            visible: true
            color: testCase.contract.canvasColor
            AuroraFeedback {
                id: hosted
                contract: testCase.contract
            }
        }
    }
    Component {
        id: emptyHost
        Window {
            property alias piece: hosted
            width: 480
            height: 320
            visible: true
            color: testCase.contract.canvasColor
            AuroraEmptyState {
                id: hosted
                contract: testCase.contract
                controlId: "queue-empty"
                width: 400
            }
        }
    }
    function open(component, props) {
        const w = createTemporaryObject(component, testCase);
        verify(w);
        waitForRendering(w.contentItem);
        for (const key in props)
            w.piece[key] = props[key];
        // Deixa o layout assentar com as propriedades novas antes de usar o mouse.
        wait(20);
        return w.piece;
    }

    function test_drawerHeaderCloses() {
        const d = open(drawerHeaderHost, {
            eyebrow: "Reprodução",
            title: "Fila",
            subtitle: "12 faixas · 48 min"
        });
        d.closeText = "Fechar fila";
        const spy = createTemporaryObject(spyComponent, testCase, {
            target: d,
            signalName: "closeRequested"
        });
        const close = child(d, "queue-drawer-close");
        compare(close.Accessible.name, "Fechar fila");
        mouseClick(close);
        compare(spy.count, 1);
    }
    function test_noticeTones_data() {
        return [["Warning", AuroraNotice.Warning, "warning", "warningColor"], ["Error", AuroraNotice.Error, "warning-circle", "errorColor"], ["Info", AuroraNotice.Info, "info", "accentColor"]].map(r => ({
                    tag: r[0],
                    tone: r[1],
                    icon: r[2],
                    color: r[3]
                }));
    }
    function test_noticeTones(data) {
        const n = open(noticeHost, {
            tone: data.tone,
            title: "Configurações recuperadas",
            text: "Algumas opções voltaram ao padrão.",
            primaryActionText: "Abrir configurações"
        });
        compare(child(n, "notice-icon").iconName, data.icon);
        sameColor(child(n, "notice-icon").color, contract[data.color], "ícone");
        sameColor(n.background.color, contract.selectionColor, "fundo");
        sameColor(child(n, "notice-text").color, contract.textPrimaryColor, "texto");
        compare(n.Accessible.role, Accessible.AlertMessage);
        verify(child(n, "settings-notice-primary").visible);
        verify(!child(n, "settings-notice-secondary").visible);
    }
    function test_noticeHighContrast() {
        const n = open(noticeHost, {
            title: "Aviso",
            text: "Texto."
        });
        driver.setHighContrast(true, false);
        sameColor(n.background.color, contract.canvasColor, "fundo");
        compare(n.background.border.width, 2);
    }
    function test_noticeActionsEmit() {
        const n = open(noticeHost, {
            title: "Aviso",
            text: "Texto.",
            primaryActionText: "Um",
            secondaryActionText: "Dois"
        });
        const first = createTemporaryObject(spyComponent, testCase, {
            target: n,
            signalName: "primaryTriggered"
        });
        const second = createTemporaryObject(spyComponent, testCase, {
            target: n,
            signalName: "secondaryTriggered"
        });
        mouseClick(child(n, "settings-notice-primary"));
        mouseClick(child(n, "settings-notice-secondary"));
        compare(first.count, 1);
        compare(second.count, 1);
    }
    function test_floatingCardShadowFollowsContrast() {
        const c = open(floatingCardHost, {});
        verify(child(c, "surface-shadow").visible);
        driver.setHighContrast(true, true);
        verify(!child(c, "surface-shadow").visible);
        sameColor(c.background.color, contract.canvasColor, "fundo");
        compare(c.background.border.width, 2);
    }
    function test_feedbackHidesWhenEmpty() {
        const f = open(feedbackHost, {
            text: ""
        });
        verify(!f.visible);
        f.text = "Caminho copiado.";
        verify(f.visible);
        sameColor(child(f, "feedback-icon").color, contract.successColor, "ícone");
        compare(child(f, "feedback-icon").iconName, "check-circle");
    }
    function test_emptyStateActions() {
        const e = open(emptyHost, {
            iconName: "queue",
            title: "A fila está vazia",
            hint: "Adicione arquivos para começar.",
            primaryActionText: "Adicionar arquivos"
        });
        const spy = createTemporaryObject(spyComponent, testCase, {
            target: e,
            signalName: "primaryTriggered"
        });
        mouseClick(child(e, "queue-empty-primary"));
        compare(spy.count, 1);
        verify(!child(e, "queue-empty-secondary").visible);
    }
    function test_emptyStateHighContrast() {
        const e = open(emptyHost, {
            iconName: "queue",
            title: "A fila está vazia"
        });
        sameColor(child(e, "empty-icon-circle").color, contract.selectionColor, "círculo normal");
        sameColor(child(e, "empty-icon").color, contract.accentColor, "ícone normal");
        driver.setHighContrast(true, true);
        const circle = child(e, "empty-icon-circle");
        sameColor(circle.color, contract.canvasColor, "círculo");
        compare(circle.border.width, 2);
        sameColor(circle.border.color, contract.textPrimaryColor, "contorno");
        sameColor(child(e, "empty-icon").color, contract.textPrimaryColor, "ícone");
    }
    function test_noticeAccessibleName() {
        const n = open(noticeHost, {
            title: "Configurações recuperadas",
            text: "Algumas opções voltaram ao padrão."
        });
        compare(n.Accessible.name, "Configurações recuperadas. Algumas opções voltaram ao padrão.");
        n.text = "";
        compare(n.Accessible.name, "Configurações recuperadas");
        n.title = "";
        n.text = "Algumas opções voltaram ao padrão.";
        compare(n.Accessible.name, "Algumas opções voltaram ao padrão.");
    }
}
