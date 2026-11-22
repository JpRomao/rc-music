import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "AuroraButtons"
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
        id: hostComponent
        Window {
            id: hostWindow
            property var props: ({})
            property int kind: AuroraButton.Primary
            property alias button: hosted
            width: 480
            height: 320
            visible: true
            color: testCase.contract.canvasColor
            AuroraButton {
                id: hosted
                contract: testCase.contract
                controlId: "sample-button"
                text: "Abrir áudio"
                kind: hostWindow.kind
                x: 20
                y: 20
            }
            Component.onCompleted: {
                for (const key in props)
                    hosted[key] = props[key];
            }
        }
    }

    function host(kind, props) {
        const window = createTemporaryObject(hostComponent, testCase, {
            kind: kind,
            props: props || {}
        });
        verify(window);
        verify(waitForRendering(window.contentItem));
        return window.button;
    }

    function test_kindsUseTheirColors_data() {
        const kinds = [["Primary", AuroraButton.Primary, "accentColor", "accentTextColor"], ["Tonal", AuroraButton.Tonal, "selectionColor", "textPrimaryColor"], ["Quiet", AuroraButton.Quiet, "transparent", "textPrimaryColor"], ["Danger", AuroraButton.Danger, "selectionColor", "errorColor"], ["DangerFilled", AuroraButton.DangerFilled, "errorColor", "accentTextColor"]];
        const rows = [];
        for (const theme of themes())
            for (const k of kinds)
                rows.push({
                    tag: k[0] + "-" + theme.tag,
                    dark: theme.dark,
                    kind: k[1],
                    bg: k[2],
                    fg: k[3]
                });
        return rows;
    }
    function test_kindsUseTheirColors(data) {
        driver.setSystemAppearance(data.dark, false);
        const button = host(data.kind);
        sameColor(child(button, "button-background").color, data.bg === "transparent" ? "transparent" : contract[data.bg], "fundo");
        sameColor(child(button, "button-label").color, contract[data.fg], "texto");
    }
    function test_targetIsAtLeast40() {
        for (const compact of [false, true]) {
            const button = host(AuroraButton.Tonal, {
                compact: compact
            });
            verify(button.height >= contract.controlTargetMinimum);
            compare(child(button, "button-background").height, compact ? 32 : button.height);
        }
    }
    function test_keyboardActivates() {
        const button = host(AuroraButton.Primary);
        const spy = createTemporaryObject(spyComponent, testCase, {
            target: button,
            signalName: "clicked"
        });
        button.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        keyClick(Qt.Key_Enter);
        keyClick(Qt.Key_Space);
        compare(spy.count, 3);
    }
    function test_focusRingOnlyWithFocus() {
        const button = host(AuroraButton.Primary);
        const ring = child(button, "focus-ring");
        verify(!ring.visible);
        button.forceActiveFocus(Qt.TabFocusReason);
        verify(ring.visible);
        compare(ring.border.width, 3);
        sameColor(ring.border.color, contract.focusColor, "anel");
        // Folga de 2 px entre o botão e o anel de 3 px.
        const bg = child(button, "button-background");
        compare(ring.x, -5);
        compare(ring.y, bg.y - 5);
        compare(ring.width, button.width + 10);
        compare(ring.height, bg.height + 10);
        compare(ring.radius, contract.radiusControl + 5);
    }
    function test_focusRingHasGap_data() {
        const rows = [];
        for (const theme of themes())
            for (const k of [["Primary", AuroraButton.Primary], ["DangerFilled", AuroraButton.DangerFilled]])
                rows.push({
                    tag: k[0] + "-" + theme.tag,
                    dark: theme.dark,
                    kind: k[1]
                });
        return rows;
    }
    function test_focusRingHasGap(data) {
        driver.setSystemAppearance(data.dark, false);
        const button = host(data.kind);
        button.forceActiveFocus(Qt.TabFocusReason);
        const image = grabImage(button.Window.contentItem);
        const middle = button.y + Math.round(button.height / 2);
        sameColor(image.pixel(button.x - 1, middle), contract.canvasColor, "1 px fora é o fundo");
        sameColor(image.pixel(button.x - 3, middle), contract.focusColor, "3 px fora é o anel");
        sameColor(image.pixel(button.x - 4, middle), contract.focusColor, "4 px fora é o anel");
        sameColor(image.pixel(button.x + button.width, middle), contract.canvasColor, "1 px fora à direita é o fundo");
        sameColor(image.pixel(button.x + button.width + 3, middle), contract.focusColor, "4 px fora à direita é o anel");
    }
    function test_disabledIsVisibleAndInert() {
        const button = host(AuroraButton.Primary, {
            enabled: false
        });
        compare(button.opacity, 0.4);
        const spy = createTemporaryObject(spyComponent, testCase, {
            target: button,
            signalName: "clicked"
        });
        mouseClick(button);
        compare(spy.count, 0);
    }
    function test_disabledInHighContrast() {
        driver.setHighContrast(true, true);
        const button = host(AuroraButton.Primary, {
            enabled: false
        });
        compare(button.opacity, 1);
        const bg = child(button, "button-background");
        sameColor(bg.color, "transparent", "sem preenchimento");
        sameColor(bg.border.color, contract.disabledTextColor, "contorno");
        sameColor(child(button, "button-label").color, contract.disabledTextColor, "texto");
    }
    function test_highContrastOutlinesTonal() {
        driver.setHighContrast(true, true);
        const tonal = host(AuroraButton.Tonal);
        sameColor(child(tonal, "button-background").color, contract.canvasColor, "fundo");
        compare(child(tonal, "button-background").border.width, 2);
        sameColor(child(tonal, "button-background").border.color, contract.textPrimaryColor, "contorno");
        sameColor(child(host(AuroraButton.Primary), "button-background").color, contract.accentColor, "principal");
    }
    function test_longLabelElidesButKeepsName() {
        const text = "Abrir pasta de downloads e conferir os arquivos";
        const button = host(AuroraButton.Tonal, {
            text: text,
            width: 160
        });
        verify(child(button, "button-label").truncated);
        compare(button.Accessible.name, text);
    }
    function test_colorsFollowThemeChange() {
        const button = host(AuroraButton.Primary);
        driver.setSystemAppearance(true, false);
        sameColor(child(button, "button-background").color, contract.accentColor, "depois da troca");
    }
    function test_iconShowsWithKindColor() {
        const button = host(AuroraButton.Danger, {
            iconName: "plus"
        });
        const icon = child(button, "button-icon");
        verify(icon.visible);
        compare(icon.iconName, "plus");
        sameColor(icon.color, contract.errorColor, "ícone");
    }
    function test_emptyIconDoesNotTakeSpace() {
        const plain = host(AuroraButton.Tonal);
        const icon = child(plain, "button-icon");
        verify(!icon.visible);
        const label = child(plain, "button-label");
        const left = label.mapToItem(plain, 0, 0).x;
        compare(left, plain.leftPadding);
        compare(plain.implicitWidth, label.implicitWidth + plain.leftPadding + plain.rightPadding);
        const withIcon = host(AuroraButton.Tonal, {
            iconName: "plus"
        });
        verify(withIcon.implicitWidth > plain.implicitWidth);
    }

    Component {
        id: hostIconComponent
        Window {
            id: iconWindow
            property var props: ({})
            property alias button: hostedIcon
            width: 480
            height: 320
            visible: true
            color: testCase.contract.canvasColor
            AuroraIconButton {
                id: hostedIcon
                contract: testCase.contract
                controlId: "sample-icon-button"
                iconName: iconWindow.props.iconName
                x: 20
                y: 20
            }
            Component.onCompleted: {
                for (const key in props)
                    hostedIcon[key] = props[key];
            }
        }
    }
    function hostIcon(props) {
        const window = createTemporaryObject(hostIconComponent, testCase, {
            props: props
        });
        verify(window);
        verify(waitForRendering(window.contentItem));
        return window.button;
    }

    function test_iconButtonIs40AndNamed() {
        const b = hostIcon({
            iconName: "more",
            text: "Mais ações"
        });
        compare(b.width, 40);
        compare(b.height, 40);
        compare(b.Accessible.name, "Mais ações");
        compare(child(b, "button-icon").iconName, "more");
    }
    function test_checkedShowsMark_data() {
        return themes();
    }
    function test_checkedShowsMark(data) {
        driver.setSystemAppearance(data.dark, false);
        const b = hostIcon({
            iconName: "shuffle",
            text: "Aleatório",
            checkable: true
        });
        verify(!child(b, "state-mark").visible);
        sameColor(child(b, "button-icon").color, contract.textPrimaryColor, "desligado");
        b.toggle();
        verify(child(b, "state-mark").visible);
        compare(child(b, "state-mark").width, 4);
        sameColor(child(b, "state-mark").color, contract.accentColor, "ponto");
        sameColor(child(b, "icon-button-background").color, contract.selectionColor, "fundo");
        sameColor(child(b, "button-icon").color, contract.accentColor, "ícone");
        compare(b.Accessible.checked, true);
    }
    function test_checkedInHighContrastUsesHighlight() {
        driver.setHighContrast(true, true);
        const b = hostIcon({
            iconName: "repeat",
            text: "Repetir",
            checkable: true,
            checked: true
        });
        sameColor(child(b, "icon-button-background").color, contract.accentColor, "fundo");
        sameColor(child(b, "state-mark").color, contract.accentTextColor, "ponto");
    }
    function test_tooltipShowsName() {
        const b = hostIcon({
            iconName: "close",
            text: "Fechar fila"
        });
        mouseMove(b);
        tryVerify(() => b.ToolTip.toolTip.visible, 2000);
        compare(b.ToolTip.text, "Fechar fila");
    }

    function test_keysActivateCheckableOnce_data() {
        return [
            {
                tag: "Enter",
                key: Qt.Key_Enter
            },
            {
                tag: "Return",
                key: Qt.Key_Return
            },
            {
                tag: "Espaço",
                key: Qt.Key_Space
            }
        ];
    }
    function test_keysActivateCheckableOnce(data) {
        const b = hostIcon({
            iconName: "shuffle",
            text: "Aleatório",
            checkable: true
        });
        const toggled = createTemporaryObject(spyComponent, testCase, {
            target: b,
            signalName: "toggled"
        });
        const clicked = createTemporaryObject(spyComponent, testCase, {
            target: b,
            signalName: "clicked"
        });
        b.forceActiveFocus();
        keyClick(data.key);
        tryCompare(b, "checked", true);
        compare(toggled.count, 1);
        compare(clicked.count, 1);
        keyClick(data.key);
        tryCompare(b, "checked", false);
        compare(toggled.count, 2);
        compare(clicked.count, 2);
    }
    function test_pressActionTogglesCheckable() {
        const b = hostIcon({
            iconName: "shuffle",
            text: "Aleatório",
            checkable: true
        });
        const toggled = createTemporaryObject(spyComponent, testCase, {
            target: b,
            signalName: "toggled"
        });
        b.Accessible.pressAction();
        compare(b.checked, true);
        compare(toggled.count, 1);
    }
    function test_buttonDescriptionIsAccessible() {
        const button = host(AuroraButton.Tonal, {
            text: "Abrir letras",
            description: "Sem letra para esta faixa"
        });
        compare(button.Accessible.description, "Sem letra para esta faixa");
        compare(button.Accessible.name, "Abrir letras");
    }
    function test_iconButtonDescriptionGoesToTooltip() {
        const b = hostIcon({
            iconName: "shuffle",
            text: "Aleatório",
            description: "Só quem criou a sala muda"
        });
        compare(b.Accessible.description, "Só quem criou a sala muda");
        verify(b.ToolTip.text.indexOf("Aleatório") === 0, "dica: " + b.ToolTip.text);
        verify(b.ToolTip.text.indexOf("Só quem criou a sala muda") > 0, "dica: " + b.ToolTip.text);
        const plain = hostIcon({
            iconName: "close",
            text: "Fechar fila"
        });
        compare(plain.ToolTip.text, "Fechar fila");
    }
    function test_tooltipShowsWithKeyboardFocus() {
        const b = hostIcon({
            iconName: "close",
            text: "Fechar fila"
        });
        verify(!b.ToolTip.visible);
        b.forceActiveFocus(Qt.TabFocusReason);
        tryVerify(() => b.ToolTip.toolTip.visible, 2000);
        compare(b.ToolTip.toolTip.text, "Fechar fila");
    }
    function test_pressActionIgnoredWhenDisabled() {
        const button = host(AuroraButton.Primary, {
            enabled: false
        });
        const clicked = createTemporaryObject(spyComponent, testCase, {
            target: button,
            signalName: "clicked"
        });
        button.Accessible.pressAction();
        compare(clicked.count, 0);
        const b = hostIcon({
            iconName: "shuffle",
            text: "Aleatório",
            checkable: true,
            enabled: false
        });
        const toggled = createTemporaryObject(spyComponent, testCase, {
            target: b,
            signalName: "toggled"
        });
        b.Accessible.pressAction();
        compare(b.checked, false);
        compare(toggled.count, 0);
    }
    function test_disabledCheckedMarkInHighContrast() {
        driver.setHighContrast(true, true);
        const b = hostIcon({
            iconName: "repeat",
            text: "Repetir",
            checkable: true,
            checked: true,
            enabled: false
        });
        const mark = child(b, "state-mark");
        verify(mark.visible);
        sameColor(child(b, "icon-button-background").color, "transparent", "sem preenchimento");
        sameColor(mark.color, contract.disabledTextColor, "ponto desabilitado");
    }
}
