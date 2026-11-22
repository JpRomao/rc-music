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
}
