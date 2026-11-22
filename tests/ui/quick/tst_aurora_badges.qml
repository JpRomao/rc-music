import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "AuroraBadges"
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
        id: chipHostComponent
        Window {
            property var props: ({})
            property alias chip: hosted
            width: 480
            height: 320
            visible: true
            color: testCase.contract.canvasColor
            AuroraChip {
                id: hosted
                x: 20
                y: 20
                contract: testCase.contract
            }
            Component.onCompleted: {
                for (const key in props)
                    hosted[key] = props[key];
            }
        }
    }
    Component {
        id: avatarHostComponent
        Window {
            property var props: ({})
            property alias avatar: hosted
            width: 160
            height: 160
            visible: true
            color: testCase.contract.surfaceColor
            AuroraAvatar {
                id: hosted
                x: 20
                y: 20
                contract: testCase.contract
            }
            Component.onCompleted: {
                for (const key in props)
                    hosted[key] = props[key];
            }
        }
    }

    function hostChip(props) {
        const host = createTemporaryObject(chipHostComponent, testCase, {
            props: props
        });
        verify(host);
        return host.chip;
    }
    function hostAvatar(props) {
        const host = createTemporaryObject(avatarHostComponent, testCase, {
            props: props
        });
        verify(host);
        return host.avatar;
    }

    function test_chipTones_data() {
        return [["Neutral", AuroraChip.Neutral, "textPrimaryColor"], ["Accent", AuroraChip.Accent, "accentColor"], ["Warning", AuroraChip.Warning, "warningColor"], ["Success", AuroraChip.Success, "successColor"], ["Error", AuroraChip.Error, "errorColor"]].map(r => ({
                    tag: r[0],
                    tone: r[1],
                    fg: r[2]
                }));
    }
    function test_chipTones(data) {
        const c = hostChip({
            text: "Sincronizada",
            tone: data.tone
        });
        compare(c.height, 24);
        sameColor(child(c, "chip-background").color, contract.selectionColor, "fundo");
        sameColor(child(c, "chip-label").color, contract[data.fg], "texto");
        compare(c.Accessible.name, "Sincronizada");
    }
    function test_chipHighContrast() {
        driver.setHighContrast(true, false);
        const c = hostChip({
            text: "Sincronizada"
        });
        const bg = child(c, "chip-background");
        sameColor(bg.color, contract.canvasColor, "fundo");
        compare(bg.border.width, 1);
        sameColor(bg.border.color, contract.textPrimaryColor, "contorno");
    }
    function test_avatarIsRound() {
        const a = hostAvatar({
            source: "qrc:/rc_music/avatars/avatar-01.svg",
            size: 64
        });
        verify(waitForRendering(a));
        const image = grabImage(a);
        const corner = image.pixel(1, 1);
        sameColor(corner, contract.surfaceColor, "canto coberto");
        verify(!Qt.colorEqual(image.pixel(32, 32), contract.surfaceColor), "centro mostra a imagem");
    }
    function test_avatarSelectionRing() {
        const a = hostAvatar({
            source: "qrc:/rc_music/avatars/avatar-02.svg",
            size: 56,
            selected: true
        });
        const ring = child(a, "avatar-selection-ring");
        verify(ring.visible);
        compare(ring.border.width, 2);
        sameColor(ring.border.color, contract.accentColor, "anel");
    }
    function test_avatarWithoutImageShowsFallbackDisc_data() {
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
    function test_avatarWithoutImageShowsFallbackDisc(data) {
        driver.setSystemAppearance(data.dark, false);
        const a = hostAvatar({
            size: 64
        });
        const disc = child(a, "avatar-fallback");
        verify(disc.visible);
        sameColor(disc.color, contract.selectionColor, "disco");
        compare(disc.border.width, 0);
        verify(waitForRendering(a));
        const image = grabImage(a);
        sameColor(image.pixel(32, 32), contract.selectionColor, "centro mostra o disco");
        sameColor(image.pixel(1, 1), contract.surfaceColor, "canto coberto");
    }
    function test_avatarFallbackInHighContrast() {
        driver.setHighContrast(true, true);
        const a = hostAvatar({
            size: 64
        });
        const disc = child(a, "avatar-fallback");
        verify(disc.visible);
        sameColor(disc.color, contract.canvasColor, "disco");
        compare(disc.border.width, 2);
        sameColor(disc.border.color, contract.textPrimaryColor, "contorno");
    }
    function test_avatarWithImageHidesFallback() {
        const a = hostAvatar({
            source: "qrc:/rc_music/avatars/avatar-01.svg",
            size: 64
        });
        verify(!child(a, "avatar-fallback").visible);
    }
}
