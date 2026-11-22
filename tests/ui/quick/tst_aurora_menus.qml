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

    Component {
        id: wideHostComponent
        Window {
            id: wideHost
            property alias menu: wideMenu
            property alias opener: wideButton
            property string longText: "Informações do download"
            width: 960
            height: 400
            visible: true
            color: testCase.contract.canvasColor
            AuroraButton {
                id: wideButton
                contract: testCase.contract
                controlId: "open-wide-menu"
                text: "Abrir"
                x: 20
                y: 20
            }
            AuroraMenu {
                id: wideMenu
                contract: testCase.contract
                AuroraMenuItem {
                    contract: testCase.contract
                    text: "Tentar reproduzir novamente"
                    iconName: "play"
                }
                AuroraMenuItem {
                    contract: testCase.contract
                    text: wideHost.longText
                    iconName: "info"
                    shortcutText: "Ctrl+Shift+I"
                }
            }
        }
    }
    function openWideMenu(options) {
        const h = createTemporaryObject(wideHostComponent, testCase, options || {});
        verify(h);
        verify(waitForRendering(h.contentItem));
        h.menu.popup(h.opener, 0, h.opener.height);
        tryVerify(() => h.menu.opened);
        return h;
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
    function test_keyboardCurrentItemShowsInnerRing() {
        const h = hostMenuWithOpener();
        h.opener.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        tryVerify(() => h.menu.opened);
        keyClick(Qt.Key_Down);
        const current = h.menu.itemAt(h.menu.currentIndex);
        verify(current.visualFocus);
        const ring = findChild(current, "focus-ring");
        verify(ring, "missing focus-ring");
        verify(ring.visible);
        sameColor(ring.border.color, contract.focusColor, "anel");
        // Anel interno: não invade os itens vizinhos.
        compare(ring.x, 0);
        compare(ring.y, 0);
        compare(ring.width, current.width);
        compare(ring.height, current.height);
        // O hoverColor fica para o mouse.
        sameColor(child(current, "menu-item-background").color, "transparent", "fundo do item do teclado");
    }
    function test_mouseHoverUsesHoverColorWithoutRing() {
        const m = openMenu();
        const item = m.itemAt(1);
        mouseMove(item, item.width / 2, item.height / 2);
        tryVerify(() => item.highlighted);
        sameColor(child(item, "menu-item-background").color, contract.hoverColor, "fundo com o mouse");
        verify(!findChild(item, "focus-ring").visible);
    }
    function test_menuWidensForLongItems() {
        const h = openWideMenu();
        verify(h.menu.width > 200, "menu com " + h.menu.width);
        for (let i = 0; i < 2; ++i) {
            const item = h.menu.itemAt(i);
            verify(!child(item, "menu-item-label").truncated, "rótulo cortado: " + item.text);
            verify(child(item, "menu-item-label").width >= child(item, "menu-item-label").implicitWidth);
        }
        verify(child(h.menu.itemAt(1), "menu-item-shortcut").visible);
    }
    function test_menuWidthStaysInsideWindow() {
        const h = openWideMenu({
            width: 300,
            longText: "Informações do download que não cabem numa janela estreita de jeito nenhum"
        });
        verify(h.menu.width <= 300, "menu com " + h.menu.width);
        verify(child(h.menu.itemAt(1), "menu-item-label").truncated);
    }
    function test_menuMotionFollowsContract() {
        driver.setSystemAppearance(false, true);
        const m = openMenu();
        // As transições do Popup são adiadas: a de saída só existe depois de fechar.
        const fadeIn = m.enter.animations;
        compare(fadeIn[0].duration, 0);
        m.close();
        tryVerify(() => !m.visible);
        const fadeOut = m.exit.animations;
        compare(fadeOut[0].duration, 0);
    }
}
