import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "ShellIcons"
    when: windowShown

    // Every icon the shell references. A missing file would only surface as a
    // runtime image warning, so the catalog is exercised here explicitly.
    readonly property var catalog: ["home", "library", "search", "playlist", "activity", "settings", "arrow", "plus", "close", "play", "pause", "next", "previous", "queue", "lyrics", "volume", "mute", "info", "check", "repeat", "shuffle", "sun", "moon", "browser", "arrow-up", "arrow-down", "minus", "people", "more", "caret-down", "caret-right", "warning", "warning-circle", "check-circle"]

    Component {
        id: iconComponent
        ShellIcon {}
    }

    // This harness renders separate windows (like the shell tests), not TestCase children.
    Component {
        id: hostComponent
        Window {
            property alias icon: hostedIcon
            width: 64
            height: 64
            visible: true
            color: "#123456"
            ShellIcon {
                id: hostedIcon
                size: 48
            }
        }
    }

    function hostIcon(name, color) {
        const host = createTemporaryObject(hostComponent, testCase);
        verify(host);
        host.icon.iconName = name;
        host.icon.color = color;
        tryCompare(host.icon, "status", Image.Ready);
        verify(waitForRendering(host.contentItem));
        return host;
    }

    // Grabs are composited over the window color, so antialiased edges are blends.
    // Every pixel must lie between the window color and the requested tint: an
    // untinted (black) stroke or any other color fails. Returns the solid pixel count.
    function verifyTint(image, requested, context) {
        const background = Qt.color("#123456");
        const target = Qt.color(requested);
        const base = [background.r, background.g, background.b];
        const span = [target.r - background.r, target.g - background.g, target.b - background.b];
        const axis = span.reduce((best, value, index) => Math.abs(value) > Math.abs(span[best]) ? index : best, 0);
        let solid = 0;
        for (let x = 0; x < image.width; ++x) {
            for (let y = 0; y < image.height; ++y) {
                const pixel = image.pixel(x, y);
                const channel = [pixel.r, pixel.g, pixel.b];
                const amount = Math.min(1, Math.max(0, (channel[axis] - base[axis]) / span[axis]));
                for (let index = 0; index < 3; ++index)
                    verify(Math.abs(channel[index] - (base[index] + span[index] * amount)) <= 4 / 255, context + " pixel " + x + "," + y + " " + pixel + " is not a tint of " + requested);
                if (amount > 0.98)
                    ++solid;
            }
        }
        return solid;
    }

    function test_catalogIconsLoadAndTakeTheRequestedColor_data() {
        return catalog.map(name => ({
                    tag: name,
                    iconName: name
                }));
    }

    function test_catalogIconsLoadAndTakeTheRequestedColor(data) {
        const icon = hostIcon(data.iconName, "#934B32").icon;
        compare(icon.width, 48);
        compare(icon.height, 48);
        verify(String(icon.source).endsWith("/icons/" + data.iconName + ".svg"), String(icon.source));
        verify(verifyTint(grabImage(icon), "#934B32", data.iconName) > 0, data.iconName + " rendered no solid stroke");
    }

    function test_iconFollowsColorChanges() {
        const host = hostIcon("home", "#934B32");
        host.icon.color = "#F2EADD";
        verify(waitForRendering(host.contentItem));
        verify(verifyTint(grabImage(host.icon), "#F2EADD", "recolored home") > 0);
    }

    function test_emptyNameShowsNothingWithoutWarnings() {
        const icon = createTemporaryObject(iconComponent, testCase, {
            iconName: "",
            size: 20
        });
        verify(icon);
        compare(String(icon.source), "");
        verify(!icon.visible);
        compare(icon.Accessible.ignored, true);
    }
}
