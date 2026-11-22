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
}
