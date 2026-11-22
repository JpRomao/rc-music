import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "AuroraChoices"
    when: windowShown
    readonly property var contract: testProductUiContract // qmllint disable unqualified
    readonly property var driver: testScenarioDriver // qmllint disable unqualified

    function cleanup() {
        driver.setHighContrast(false, false);
        driver.setSystemAppearance(false, false);
    }
    function themes() {
        return [
            {
                tag: "claro",
                dark: false
            },
            {
                tag: "escuro",
                dark: true
            }
        ];
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
        id: segmentedHostComponent
        Window {
            property var props: ({})
            property alias segmented: hosted
            property alias before: beforeButton
            property alias after: afterButton
            width: 480
            height: 320
            visible: true
            color: testCase.contract.canvasColor
            Row {
                x: 20
                y: 20
                spacing: 10
                AuroraButton {
                    id: beforeButton
                    contract: testCase.contract
                    controlId: "before"
                    text: "Antes"
                }
                AuroraSegmented {
                    id: hosted
                    contract: testCase.contract
                    controlId: "view"
                    label: "Exibição"
                    options: []
                }
                AuroraButton {
                    id: afterButton
                    contract: testCase.contract
                    controlId: "after"
                    text: "Depois"
                }
            }
            Component.onCompleted: {
                for (const key in props)
                    hosted[key] = props[key];
            }
        }
    }
    Component {
        id: bindingHostComponent
        Window {
            property var props: ({})
            property int external: 0
            property alias segmented: hosted
            width: 480
            height: 320
            visible: true
            color: testCase.contract.canvasColor
            AuroraSegmented {
                id: hosted
                contract: testCase.contract
                controlId: "view"
                label: "Exibição"
                options: ["Lista", "Capas"]
                onActivated: index => external = index
            }
            Binding {
                target: hosted
                property: "currentIndex"
                value: external
            }
        }
    }
    Component {
        id: switchHostComponent
        Window {
            property var props: ({})
            property alias control: hosted
            width: 480
            height: 320
            visible: true
            color: testCase.contract.canvasColor
            AuroraSwitch {
                id: hosted
                contract: testCase.contract
                controlId: "minimized"
                label: "Opção"
                x: 20
                y: 20
            }
            Component.onCompleted: {
                for (const key in props)
                    hosted[key] = props[key];
            }
        }
    }
    Component {
        id: stepperHostComponent
        Window {
            property var props: ({})
            property alias control: hosted
            width: 480
            height: 320
            visible: true
            color: testCase.contract.canvasColor
            AuroraStepper {
                id: hosted
                contract: testCase.contract
                controlId: "seek-step"
                label: "Valor"
                x: 20
                y: 20
            }
            Component.onCompleted: {
                for (const key in props)
                    hosted[key] = props[key];
            }
        }
    }
    Component {
        id: checkHostComponent
        Window {
            property var props: ({})
            property alias control: hosted
            width: 480
            height: 320
            visible: true
            color: testCase.contract.canvasColor
            AuroraCheckBox {
                id: hosted
                contract: testCase.contract
                controlId: "tray-notice"
                x: 20
                y: 20
                width: 240
            }
            Component.onCompleted: {
                for (const key in props)
                    hosted[key] = props[key];
            }
        }
    }

    function hostWindow(component, props) {
        const window = createTemporaryObject(component, testCase, {
            props: props || {}
        });
        verify(window);
        verify(waitForRendering(window.contentItem));
        return window;
    }
    function hostSegmented(props) {
        const w = hostWindow(segmentedHostComponent, props);
        return {
            segmented: w.segmented,
            before: w.before,
            after: w.after
        };
    }
    function hostSwitch(props) {
        return hostWindow(switchHostComponent, props).control;
    }
    function hostStepper(props) {
        return hostWindow(stepperHostComponent, props).control;
    }
    function hostCheck(props) {
        return hostWindow(checkHostComponent, props).control;
    }

    function test_segmentedIsOneTabStop() {
        const h = hostSegmented({
            options: ["Lista", "Capas"],
            currentIndex: 1
        });
        h.before.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Tab);
        verify(child(h.segmented, "view-1").activeFocus);
        keyClick(Qt.Key_Tab);
        verify(h.after.activeFocus);
    }
    function test_segmentedArrowsSelect() {
        const h = hostSegmented({
            options: ["Sistema", "Claro", "Escuro"],
            currentIndex: 0
        });
        const spy = createTemporaryObject(spyComponent, testCase, {
            target: h.segmented,
            signalName: "activated"
        });
        child(h.segmented, "view-0").forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Right);
        compare(h.segmented.currentIndex, 1);
        compare(spy.count, 1);
        verify(child(h.segmented, "view-1").activeFocus);
        keyClick(Qt.Key_End);
        compare(h.segmented.currentIndex, 2);
        keyClick(Qt.Key_Right);
        compare(h.segmented.currentIndex, 2);
        keyClick(Qt.Key_Home);
        compare(h.segmented.currentIndex, 0);
    }
    function test_segmentedMarksTheChoice_data() {
        return themes();
    }
    function test_segmentedMarksTheChoice(data) {
        driver.setSystemAppearance(data.dark, false);
        const h = hostSegmented({
            options: ["Lista", "Capas"],
            currentIndex: 0
        });
        const mark = child(child(h.segmented, "view-0"), "state-mark");
        verify(mark.visible);
        compare(mark.height, 3);
        sameColor(mark.color, contract.accentColor, "traço");
        verify(!child(child(h.segmented, "view-1"), "state-mark").visible);
        sameColor(child(child(h.segmented, "view-1"), "segment-label").color, contract.textPrimaryColor, "não escolhida");
        compare(child(h.segmented, "view-0").height, 40);
        compare(child(h.segmented, "view-0").Accessible.checked, true);
    }
    function test_switchStates_data() {
        return themes();
    }
    function test_switchStates(data) {
        driver.setSystemAppearance(data.dark, false);
        const s = hostSwitch({
            label: "Iniciar minimizado"
        });
        compare(child(s, "switch-track").border.width, 2);
        sameColor(child(s, "switch-track").border.color, contract.textSecondaryColor, "trilho desligado");
        sameColor(child(s, "switch-thumb").color, contract.textSecondaryColor, "polegar desligado");
        s.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        verify(s.checked);
        compare(s.Accessible.name, "Iniciar minimizado");
        sameColor(child(s, "switch-track").color, contract.accentColor, "trilho ligado");
        sameColor(child(s, "switch-thumb").color, contract.accentTextColor, "polegar ligado");
        verify(s.height >= 40);
    }
    function test_switchMotionFollowsContract() {
        driver.setSystemAppearance(false, true);
        const s = hostSwitch({
            label: "Restaurar última sessão"
        });
        s.toggle();
        compare(child(s, "switch-thumb").x, child(s, "switch-thumb").checkedX);
    }
    function test_stepperStepsAndEdits() {
        const st = hostStepper({
            label: "Segundos para avançar",
            from: 1,
            to: 60,
            value: 5,
            suffix: "s"
        });
        compare(st.displayText, "5 s");
        st.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Up);
        compare(st.value, 6);
        st.contentItem.selectAll();
        keyClick(Qt.Key_1);
        keyClick(Qt.Key_2);
        keyClick(Qt.Key_Return);
        compare(st.value, 12);
        st.value = 60;
        keyClick(Qt.Key_Up);
        compare(st.value, 60);
        verify(st.up.indicator.width >= 40 && st.down.indicator.height >= 40);
    }
    function test_stepperCenterRestsOnSurface_data() {
        return themes();
    }
    function test_stepperCenterRestsOnSurface(data) {
        driver.setSystemAppearance(data.dark, false);
        const st = hostStepper({
            label: "Valor",
            from: 1,
            to: 60,
            value: 5
        });
        sameColor(child(st, "stepper-center").color, contract.surfaceColor, "centro em repouso");
    }
    function test_checkBoxStates() {
        const c = hostCheck({
            text: "Avisar ao fechar para a bandeja nesta sessão"
        });
        sameColor(c.indicator.border.color, contract.textSecondaryColor, "desmarcada");
        c.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        verify(c.checked);
        sameColor(c.indicator.color, contract.accentColor, "marcada");
        verify(c.height >= 40);
    }

    function test_focusRingHasGapOnCheckedControls_data() {
        const rows = [];
        for (const theme of themes()) {
            rows.push({
                tag: "interruptor-" + theme.tag,
                dark: theme.dark,
                kind: "switch"
            });
            rows.push({
                tag: "caixinha-" + theme.tag,
                dark: theme.dark,
                kind: "check"
            });
        }
        return rows;
    }
    function test_focusRingHasGapOnCheckedControls(data) {
        driver.setSystemAppearance(data.dark, false);
        const control = data.kind === "switch" ? hostSwitch({
            label: "Opção",
            checked: true
        }) : hostCheck({
            text: "Opção",
            checked: true
        });
        const mark = data.kind === "switch" ? child(control, "switch-track") : control.indicator;
        control.forceActiveFocus(Qt.TabFocusReason);
        const ring = child(control, "focus-ring");
        verify(ring.visible);
        const box = mark.mapToItem(control.Window.contentItem, 0, 0);
        const image = grabImage(control.Window.contentItem);
        const middle = Math.round(box.y + mark.height / 2);
        sameColor(image.pixel(box.x - 1, middle), contract.canvasColor, "1 px fora é o fundo");
        sameColor(image.pixel(box.x - 3, middle), contract.focusColor, "3 px fora é o anel");
        sameColor(image.pixel(box.x - 4, middle), contract.focusColor, "4 px fora é o anel");
    }

    function test_segmentedFollowsExternalStateThroughBinding() {
        const w = hostWindow(bindingHostComponent, {});
        const seg = w.segmented;
        child(seg, "view-0").forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Right);
        compare(seg.currentIndex, 1);
        compare(w.external, 1);
        w.external = 0;
        compare(seg.currentIndex, 0);
    }
    function test_segmentedDisabledInHighContrastMarksInDisabledColor() {
        driver.setHighContrast(true, true);
        const h = hostSegmented({
            options: ["Lista", "Capas"],
            currentIndex: 0,
            enabled: false
        });
        sameColor(child(child(h.segmented, "view-0"), "state-mark").color, contract.disabledTextColor, "traço desabilitado");
    }
    function test_switchDisabledCheckedInHighContrastHasNoFill() {
        driver.setHighContrast(true, true);
        const s = hostSwitch({
            label: "Opção",
            checked: true,
            enabled: false
        });
        sameColor(child(s, "switch-track").color, "transparent", "trilho sem preenchimento");
        sameColor(child(s, "switch-track").border.color, contract.disabledTextColor, "contorno");
        sameColor(child(s, "switch-thumb").color, contract.disabledTextColor, "polegar");
        compare(s.opacity, 1);
    }
    function test_checkBoxDisabledCheckedInHighContrastHasNoFill() {
        driver.setHighContrast(true, true);
        const c = hostCheck({
            text: "Opção",
            checked: true,
            enabled: false
        });
        sameColor(c.indicator.color, "transparent", "indicador sem preenchimento");
        sameColor(c.indicator.border.color, contract.disabledTextColor, "contorno");
        sameColor(child(c, "check-icon").color, contract.disabledTextColor, "marca");
    }
    function test_stepperIndicatorsClickAndTextIsInset() {
        const st = hostStepper({
            label: "Valor",
            from: 1,
            to: 60,
            value: 5
        });
        mouseClick(st.up.indicator);
        compare(st.value, 6);
        mouseClick(st.down.indicator);
        mouseClick(st.down.indicator);
        compare(st.value, 4);
        const input = st.contentItem;
        verify(input.x >= st.down.indicator.x + st.down.indicator.width);
        verify(input.x + input.width <= st.up.indicator.x);
    }
}
