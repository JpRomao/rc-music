import QtQuick
import QtQuick.Controls
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "AuroraDialogs"
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
    // QtTest nao desce em um Popup: a busca parte do item visual do popup.
    function child(item, name) {
        const isPopup = item && item.contentItem && item.contentItem.parent && item.open !== undefined;
        const root = isPopup ? item.contentItem.parent : item;
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
            id: host
            property alias dialog: hosted
            property alias opener: button
            property string controlId: "dialog"
            property int preferredWidth: 480
            property bool tallContent: false
            width: 960
            height: 640
            visible: true
            color: testCase.contract.canvasColor
            AuroraButton {
                id: button
                contract: testCase.contract
                controlId: "open-dialog"
                text: "Abrir"
                x: 20
                y: 20
                onClicked: hosted.open()
            }
            AuroraDialog {
                id: hosted
                contract: testCase.contract
                controlId: host.controlId
                title: "Detalhes"
                preferredWidth: host.preferredWidth
                Rectangle {
                    implicitWidth: 100
                    implicitHeight: host.tallContent ? 2000 : 40
                    color: testCase.contract.surfaceColor
                }
                actions: [
                    AuroraButton {
                        contract: testCase.contract
                        controlId: "ok"
                        text: "Ok"
                        onClicked: hosted.close()
                    }
                ]
            }
        }
    }
    Component {
        id: confirmHostComponent
        Window {
            id: host
            property alias dialog: hosted
            property string controlId: "confirm"
            property string question: "Pergunta?"
            property string consequence: "Consequencia."
            property string confirmText: "Sim"
            property bool destructive: false
            width: 960
            height: 640
            visible: true
            color: testCase.contract.canvasColor
            AuroraConfirmDialog {
                id: hosted
                contract: testCase.contract
                controlId: host.controlId
                question: host.question
                consequence: host.consequence
                confirmText: host.confirmText
                destructive: host.destructive
            }
        }
    }

    Component {
        id: formHostComponent
        Window {
            id: formHost
            property alias dialog: formDialog
            property alias first: firstControl
            property alias last: lastControl
            property bool tallContent: false
            width: 720
            height: 520
            visible: true
            color: testCase.contract.canvasColor
            AuroraDialog {
                id: formDialog
                contract: testCase.contract
                controlId: "form"
                title: "Formulário"
                AuroraButton {
                    id: firstControl
                    contract: testCase.contract
                    controlId: "form-first"
                    kind: AuroraButton.Primary
                    text: "Primeiro"
                }
                Rectangle {
                    implicitWidth: 100
                    implicitHeight: formHost.tallContent ? 1200 : 40
                    color: testCase.contract.surfaceColor
                }
                AuroraButton {
                    id: lastControl
                    contract: testCase.contract
                    controlId: "form-last"
                    kind: AuroraButton.Tonal
                    text: "Último"
                }
                actions: [
                    AuroraButton {
                        contract: testCase.contract
                        controlId: "form-ok"
                        text: "Ok"
                        onClicked: formDialog.close()
                    }
                ]
            }
        }
    }
    function openForm(options) {
        const h = createTemporaryObject(formHostComponent, testCase, options || {});
        verify(h);
        verify(waitForRendering(h.contentItem));
        h.dialog.open();
        tryVerify(() => h.dialog.opened);
        return h;
    }

    function openDialog(options) {
        const h = createTemporaryObject(hostComponent, testCase, options || {});
        verify(h);
        verify(waitForRendering(h.contentItem));
        h.dialog.open();
        tryVerify(() => h.dialog.opened);
        return h;
    }
    function openDialogFromButton() {
        const h = createTemporaryObject(hostComponent, testCase);
        verify(h);
        verify(waitForRendering(h.contentItem));
        h.opener.forceActiveFocus();
        verify(h.opener.activeFocus);
        h.dialog.open();
        tryVerify(() => h.dialog.opened);
        return h;
    }
    function openConfirm(options) {
        const h = createTemporaryObject(confirmHostComponent, testCase, options || {});
        verify(h);
        verify(waitForRendering(h.contentItem));
        h.dialog.open();
        tryVerify(() => h.dialog.opened);
        return h;
    }

    function test_scrimCoversWholeWindow() {
        const h = openDialog();
        const scrim = child(h.Overlay.overlay, "dialog-scrim");
        compare(scrim.width, h.width);
        compare(scrim.height, h.height);
        sameColor(scrim.color, contract.scrimColor, "veu");
    }
    function test_escClosesAndRestoresFocus() {
        const h = openDialogFromButton();
        keyClick(Qt.Key_Escape);
        tryVerify(() => !h.dialog.visible);
        verify(h.opener.activeFocus);
    }
    function test_closeButtonIsNamed() {
        const h = openDialog({
            controlId: "track-details"
        });
        compare(child(h.dialog.header, "track-details-close").Accessible.name, "Fechar");
    }
    function test_confirmOpensOnCancelAndConfirms() {
        const h = openConfirm({
            controlId: "kick",
            question: "Expulsar Ana?",
            consequence: "Ana sai da sala agora.",
            confirmText: "Expulsar",
            destructive: true
        });
        tryVerify(() => child(h.dialog, "kick-cancel").activeFocus);
        compare(child(h.dialog, "kick-confirm").kind, AuroraButton.DangerFilled);
        const spy = createTemporaryObject(spyComponent, testCase, {
            target: h.dialog,
            signalName: "confirmed"
        });
        keyClick(Qt.Key_Tab);
        keyClick(Qt.Key_Return);
        compare(spy.count, 1);
        tryVerify(() => !h.dialog.visible);
    }
    function test_titleUsesHeadingFont() {
        const h = openDialog();
        compare(child(h.dialog.header, "dialog-title").font.family, contract.headingFontFamily);
    }
    function test_themeChangeWhileOpen() {
        const h = openDialog();
        driver.setSystemAppearance(true, false);
        sameColor(h.dialog.background.color, contract.surfaceRaisedColor, "fundo depois da troca");
    }
    function test_dialogMotionFollowsContract() {
        driver.setSystemAppearance(false, true);
        const h = openDialog();
        compare(h.dialog.enter.animations[0].duration, 0);
    }
    function test_dialogFitsMinimumWindow() {
        const h = createTemporaryObject(hostComponent, testCase, {
            width: 720,
            height: 520,
            preferredWidth: 800,
            tallContent: true
        });
        verify(h);
        verify(waitForRendering(h.contentItem));
        h.dialog.open();
        tryVerify(() => h.dialog.opened);
        verify(h.dialog.width <= 720 - 48);
        verify(h.dialog.height <= 520 - 48);
    }

    function test_shortDialogDoesNotScroll() {
        const h = openDialog();
        const sv = h.dialog.contentItem;
        verify(sv.contentHeight <= sv.availableHeight, "conteudo " + sv.contentHeight + " > area " + sv.availableHeight);
    }
    function test_tallDialogScrollsInsideBounds() {
        const h = createTemporaryObject(hostComponent, testCase, {
            width: 720,
            height: 520,
            tallContent: true
        });
        verify(h);
        verify(waitForRendering(h.contentItem));
        h.dialog.open();
        tryVerify(() => h.dialog.opened);
        const sv = h.dialog.contentItem;
        verify(sv.contentHeight > sv.availableHeight);
        verify(sv.availableHeight > 0);
        verify(h.dialog.height <= 520 - 48);
    }
    function test_highContrastSurface() {
        driver.setHighContrast(true, true);
        const h = openDialog();
        sameColor(h.dialog.background.color, contract.canvasColor, "fundo em alto contraste");
        compare(h.dialog.background.border.width, 2);
        sameColor(h.dialog.background.border.color, contract.textPrimaryColor, "contorno");
        verify(!child(h.dialog.background, "surface-shadow").visible);
    }

    function test_bodyFocusRingIsNotClipped() {
        const h = openForm();
        h.first.forceActiveFocus(Qt.TabFocusReason);
        verify(h.first.activeFocus);
        const box = h.first.mapToItem(h.contentItem, 0, 0);
        const image = grabImage(h.contentItem);
        const middleX = Math.round(box.x + h.first.width / 2);
        const middleY = Math.round(box.y + h.first.height / 2);
        sameColor(image.pixel(middleX, box.y - 4), contract.focusColor, "anel acima");
        sameColor(image.pixel(box.x - 4, middleY), contract.focusColor, "anel à esquerda");
        sameColor(image.pixel(box.x + h.first.width + 3, middleY), contract.focusColor, "anel à direita");
    }
}
