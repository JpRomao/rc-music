import QtQuick
import QtQuick.Controls
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "AuroraMenus"
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
    // QtTest nao desce em um Menu ou Popup: a busca parte do item visual.
    function child(item, name) {
        const root = item && item.contentItem && item.contentItem.parent ? item.contentItem.parent : item;
        const found = findChild(root, name);
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
            property alias menu: hosted
            property alias opener: button
            width: 480
            height: 320
            visible: true
            color: testCase.contract.canvasColor
            AuroraButton {
                id: button
                contract: testCase.contract
                controlId: "open-menu"
                text: "Abrir"
                x: 20
                y: 20
                onClicked: hosted.popup(button, 0, button.height)
            }
            AuroraMenu {
                id: hosted
                contract: testCase.contract
                AuroraMenuItem {
                    contract: testCase.contract
                    text: "Tocar"
                    iconName: "play"
                }
                AuroraMenuItem {
                    contract: testCase.contract
                    text: "Subir"
                    shortcutText: "Alt+\u2191"
                }
                AuroraMenuSeparator {
                    contract: testCase.contract
                }
                AuroraMenuItem {
                    contract: testCase.contract
                    text: "Remover"
                    danger: true
                }
            }
        }
    }

    function openMenu() {
        const h = createTemporaryObject(hostComponent, testCase);
        verify(h);
        verify(waitForRendering(h.contentItem));
        h.menu.popup(h.opener, 0, h.opener.height);
        tryVerify(() => h.menu.opened);
        return h.menu;
    }
    function hostMenuWithOpener() {
        const h = createTemporaryObject(hostComponent, testCase);
        verify(h);
        verify(waitForRendering(h.contentItem));
        return h;
    }

    function test_menuSurface_data() {
        return themes();
    }
    function test_menuSurface(data) {
        driver.setSystemAppearance(data.dark, false);
        const m = openMenu();
        sameColor(m.background.color, contract.surfaceRaisedColor, "fundo");
        compare(m.background.radius, contract.radiusMedium);
        verify(child(m.background, "surface-shadow").visible);
    }
    function test_itemsAre40AndNavigable() {
        const m = openMenu();
        const spy = createTemporaryObject(spyComponent, testCase, {
            target: m.itemAt(0),
            signalName: "triggered"
        });
        compare(m.itemAt(0).height, 40);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Return);
        compare(spy.count, 1);
    }
    function test_dangerItemUsesError() {
        const m = openMenu();
        sameColor(child(m.itemAt(3), "menu-item-label").color, contract.errorColor, "perigo");
    }
    function test_shortcutTextStaysReadable() {
        const m = openMenu();
        sameColor(child(m.itemAt(1), "menu-item-shortcut").color, contract.textSecondaryColor, "repouso");
        m.currentIndex = 1;
        sameColor(child(m.itemAt(1), "menu-item-shortcut").color, contract.textPrimaryColor, "destacado");
    }
    function test_noShadowInHighContrast() {
        driver.setHighContrast(true, true);
        const m = openMenu();
        verify(!child(m.background, "surface-shadow").visible);
    }
    function test_escReturnsFocusToOpener() {
        const h = hostMenuWithOpener();
        h.opener.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        tryVerify(() => h.menu.opened);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !h.menu.visible);
        verify(h.opener.activeFocus);
    }
}
