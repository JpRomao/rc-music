import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "AuroraFields"
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
        id: fieldHostComponent
        Window {
            property var props: ({})
            property alias field: hosted
            width: 480
            height: 320
            visible: true
            color: testCase.contract.canvasColor
            AuroraTextField {
                id: hosted
                contract: testCase.contract
                controlId: "nickname"
                label: "Apelido"
                x: 20
                y: 20
                width: 300
            }
            Component.onCompleted: {
                for (const key in props)
                    hosted[key] = props[key];
            }
        }
    }
    Component {
        id: selectHostComponent
        Window {
            property var props: ({})
            property alias select: hosted
            width: 480
            height: 320
            visible: true
            color: testCase.contract.canvasColor
            AuroraSelect {
                id: hosted
                contract: testCase.contract
                controlId: "output"
                label: "Saída de áudio"
                x: 20
                y: 20
                width: 300
            }
            Component.onCompleted: {
                for (const key in props)
                    hosted[key] = props[key];
            }
        }
    }

    Component {
        id: fieldAfterButtonHostComponent
        Window {
            property alias before: beforeButton
            property alias field: hosted
            width: 480
            height: 320
            visible: true
            color: testCase.contract.canvasColor
            AuroraButton {
                id: beforeButton
                contract: testCase.contract
                controlId: "before"
                text: "Antes"
                x: 20
                y: 20
            }
            AuroraTextField {
                id: hosted
                contract: testCase.contract
                controlId: "nickname"
                label: "Apelido"
                x: 20
                y: 100
                width: 300
            }
        }
    }

    function hostField(props) {
        const window = createTemporaryObject(fieldHostComponent, testCase, {
            props: props || {}
        });
        verify(window);
        verify(waitForRendering(window.contentItem));
        return window.field;
    }
    function hostSelect(props) {
        const window = createTemporaryObject(selectHostComponent, testCase, {
            props: props || {}
        });
        verify(window);
        verify(waitForRendering(window.contentItem));
        return window.select;
    }

    function test_fieldHasLineNotBorder_data() {
        return themes();
    }
    function test_fieldHasLineNotBorder(data) {
        driver.setSystemAppearance(data.dark, false);
        const f = hostField({
            label: "Apelido"
        });
        compare(f.input.background.border.width, 0);
        const line = child(f, "field-line");
        compare(line.height, 1);
        sameColor(line.color, contract.textSecondaryColor, "linha");
        f.input.forceActiveFocus(Qt.TabFocusReason);
        compare(line.height, 2);
        sameColor(line.color, contract.focusColor, "linha com foco");
    }
    function test_errorShowsBelowAndColorsLine() {
        const f = hostField({
            label: "Apelido",
            errorText: "Use de 1 a 32 caracteres."
        });
        const error = child(f, "nickname-error");
        verify(error.visible);
        sameColor(error.color, contract.errorColor, "erro");
        sameColor(child(f, "field-line").color, contract.errorColor, "linha");
        compare(f.input.Accessible.description, "Use de 1 a 32 caracteres.");
    }
    function test_leadingIconMakesRoom() {
        const f = hostField({
            label: "Filtrar",
            leadingIconName: "search"
        });
        verify(child(f, "field-icon").visible);
        compare(f.input.leftPadding, 40);
    }
    function test_fieldInHighContrastHasOutline() {
        driver.setHighContrast(true, true);
        const f = hostField({
            label: "Apelido"
        });
        verify(!child(f, "field-line").visible);
        compare(f.input.background.border.width, 2);
    }
    function test_selectOpensAndChoosesWithKeyboard() {
        const s = hostSelect({
            label: "Saída de áudio",
            model: ["Padrão do sistema", "Fones"]
        });
        s.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        tryVerify(() => s.popup.opened);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Return);
        compare(s.currentIndex, 1);
        // O menu sai em fade; a linha normal vale com ele fechado.
        tryVerify(() => !s.popup.visible);
        // O foco de teclado continua no campo e mantém a linha de foco; a linha normal vale sem foco.
        s.focus = false;
        verify(!s.activeFocus);
        sameColor(child(s, "field-line").color, contract.textSecondaryColor, "linha");
    }
    function test_selectMarksCurrentOptionAndFocusLine() {
        const s = hostSelect({
            label: "Saída de áudio",
            model: ["Padrão do sistema", "Fones"],
            currentIndex: 1
        });
        compare(child(s, "select-indicator").iconName, "caret-down");
        s.forceActiveFocus(Qt.TabFocusReason);
        compare(child(s, "field-line").height, 2);
        sameColor(child(s, "field-line").color, contract.focusColor, "linha com foco");
        keyClick(Qt.Key_Space);
        tryVerify(() => s.popup.opened);
        const mark = findChild(s.popup.contentItem, "select-current-mark");
        verify(mark, "missing current mark");
        verify(mark.visible);
        sameColor(mark.color, contract.accentColor, "marca da opção atual");
    }

    function test_fieldInputCarriesControlId() {
        const h = createTemporaryObject(fieldHostComponent, testCase);
        verify(h);
        const input = child(h, "nickname");
        compare(input, h.field.input);
        compare(input.Accessible.id, "nickname");
    }
    function test_fieldShowsFocusRing() {
        const f = hostField({
            label: "Apelido"
        });
        const ring = child(f, "focus-ring");
        verify(!ring.visible);
        f.input.forceActiveFocus(Qt.MouseFocusReason);
        verify(ring.visible);
        compare(ring.border.width, contract.focusRingWidth);
        sameColor(ring.border.color, contract.focusColor, "anel");
        const bg = f.input.background;
        const box = ring.mapToItem(bg, 0, 0);
        compare(box.x, -5);
        compare(box.y, -5);
        compare(ring.width, bg.width + 10);
        compare(ring.height, bg.height + 10);
        compare(ring.radius, contract.radiusControl + 5);
        // A linha continua como marca de estado.
        compare(child(f, "field-line").height, 2);
    }
    function test_tabLandsInTextField() {
        const w = createTemporaryObject(fieldAfterButtonHostComponent, testCase);
        verify(w);
        verify(waitForRendering(w.contentItem));
        w.before.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Tab);
        verify(w.field.input.activeFocus);
        verify(child(w.field, "focus-ring").visible);
    }
    function test_selectShowsFocusRingWithAnyFocus() {
        const s = hostSelect({
            label: "Saída de áudio",
            model: ["Padrão do sistema", "Fones"]
        });
        const ring = child(s, "focus-ring");
        verify(!ring.visible);
        // O mesmo sinal do campo: foco ativo, venha do teclado ou do mouse.
        s.forceActiveFocus(Qt.MouseFocusReason);
        verify(ring.visible);
        compare(child(s, "field-line").height, 2);
        sameColor(child(s, "field-line").color, contract.focusColor, "linha com foco");
        compare(ring.x, -5);
        compare(ring.width, s.width + 10);
        compare(ring.radius, contract.radiusControl + 5);
    }
    function test_selectKeyboardOptionShowsInnerRing() {
        const s = hostSelect({
            label: "Saída de áudio",
            model: ["Padrão do sistema", "Fones", "Caixa"]
        });
        s.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        tryVerify(() => s.popup.opened);
        keyClick(Qt.Key_Down);
        compare(s.highlightedIndex, 1);
        const list = s.popup.contentItem;
        tryVerify(() => list.itemAtIndex(1) !== null, 1000, "opção 1 de " + list.count + " em " + list);
        const option = list.itemAtIndex(1);
        const ring = findChild(option, "focus-ring");
        verify(ring, "missing focus-ring");
        verify(ring.visible);
        compare(ring.x, 0);
        compare(ring.width, option.width);
        sameColor(option.background.color, "transparent", "fundo da opção do teclado");
        // Com o mouse, a opção ganha o hoverColor e o anel some.
        const other = list.itemAtIndex(2);
        mouseMove(other, other.width / 2, other.height / 2);
        tryCompare(s, "highlightedIndex", 2);
        sameColor(other.background.color, contract.hoverColor, "fundo com o mouse");
        verify(!findChild(other, "focus-ring").visible);
    }
    function test_selectPopupFadesLikeMenu_data() {
        return [
            {
                tag: "normal",
                reduced: false
            },
            {
                tag: "movimento-reduzido",
                reduced: true
            }
        ];
    }
    function test_selectPopupFadesLikeMenu(data) {
        driver.setSystemAppearance(false, data.reduced);
        const s = hostSelect({
            label: "Saída de áudio",
            model: ["Padrão do sistema", "Fones"]
        });
        s.popup.open();
        tryVerify(() => s.popup.opened);
        verify(s.popup.enter !== null, "sem transição de entrada");
        // As transições do Popup são adiadas: a de saída só existe depois de fechar.
        const fadeIn = s.popup.enter.animations;
        compare(fadeIn[0].property, "opacity");
        compare(fadeIn[0].duration, data.reduced ? 0 : contract.panelTransitionMs);
        s.popup.close();
        tryVerify(() => !s.popup.visible);
        const fadeOut = s.popup.exit.animations;
        compare(fadeOut[0].duration, data.reduced ? 0 : contract.panelTransitionMs);
    }
    function test_selectDescriptionIsAccessible() {
        const s = hostSelect({
            label: "Saída de áudio",
            description: "Vale para a próxima faixa",
            model: ["Padrão do sistema", "Fones"]
        });
        compare(s.Accessible.description, "Vale para a próxima faixa");
        compare(s.Accessible.name, "Saída de áudio");
    }
}
