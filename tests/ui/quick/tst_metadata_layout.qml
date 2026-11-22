import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "MetadataLayout"
    when: windowShown

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testLocalPlayerPresenter // qmllint disable unqualified
        }
    }

    function cleanup() {
        testScenarioDriver.resetLocalScenario(); // qmllint disable unqualified
    }

    function geometry(shell) {
        const rectangles = {};
        for (const name of ["playing-state", "cover-track", "embedded-artwork", "current-title", "current-artist", "current-album", "open-another-audio", "track-details-playing", "transport-bar", "progress", "play-pause", "previous", "next", "player-identity", "player-artwork", "player-track-title", "player-time"]) {
            const item = findChild(shell, name);
            verify(item, name);
            const position = item.mapToItem(shell.contentItem, 0, 0);
            // Text width follows content, but its center, line slot and baseline area must not jump.
            rectangles[name] = name.startsWith("current-") ? [position.x + item.width / 2, position.y, item.height] : [position.x, position.y, item.width, item.height];
        }
        return rectangles;
    }

    function verifyGeometry(shell, expected, phase) {
        const actual = geometry(shell);
        for (const name in expected) {
            for (let axis = 0; axis < expected[name].length; ++axis) {
                verify(Math.abs(actual[name][axis] - expected[name][axis]) <= 0.5, phase + ": " + name + " coordinate " + axis + " moved from " + expected[name][axis] + " to " + actual[name][axis]);
            }
        }
    }

    function test_loadingMetadataAndTrackChangesKeepGeometry_data() {
        return [
            {
                tag: "minimum",
                width: 720,
                height: 520,
                artwork: false
            },
            {
                tag: "default",
                width: 960,
                height: 640,
                artwork: false
            },
            {
                tag: "minimum-artwork",
                width: 720,
                height: 520,
                artwork: true
            },
            {
                tag: "default-artwork",
                width: 960,
                height: 640,
                artwork: true
            }
        ];
    }

    function test_loadingMetadataAndTrackChangesKeepGeometry(data) {
        testScenarioDriver.resetLocalScenario(); // qmllint disable unqualified
        const shell = createTemporaryObject(shellComponent, null, {
            width: data.width,
            height: data.height
        });
        verify(shell);
        shell.requestActivate();
        tryVerify(function () {
            return shell.active;
        });
        testScenarioDriver.enqueueLocalSelection(["C:/Synthetic/Track without tags.mp3"]); // qmllint disable unqualified
        mouseClick(findChild(shell, "open-audio"));
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Loading); // qmllint disable unqualified
        compare(findChild(shell, "current-title").text, "Track without tags");
        compare(findChild(shell, "current-artist").text, "");
        compare(findChild(shell, "current-album").text, "");
        const placeholder = findChild(shell, "cover-track");
        verify(placeholder.visible);
        compare(placeholder.source.toString(), "");
        compare(findChild(shell, "embedded-artwork").source.toString(), "");
        verify(!findChild(shell, "embedded-artwork").visible);
        verify(placeholder.width > 0 && placeholder.height > 0, "The native placeholder must have real geometry without a fixture image");
        verify(waitForPolish(shell));
        const initial = geometry(shell);

        testScenarioDriver.publishLocalReady(); // qmllint disable unqualified
        testScenarioDriver.publishLocalPlaying(); // qmllint disable unqualified
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Playing); // qmllint disable unqualified
        verify(waitForPolish(shell));
        verifyGeometry(shell, initial, "Playing");

        if (data.artwork) {
            testScenarioDriver.publishArtworkLocalMetadata(); // qmllint disable unqualified
        } else {
            testScenarioDriver.publishCompleteLocalMetadata(); // qmllint disable unqualified
        }
        tryCompare(findChild(shell, "current-title"), "text", "Título controlado");
        compare(findChild(shell, "current-artist").text, "Artista");
        compare(findChild(shell, "current-album").text, "Álbum");
        if (data.artwork) {
            const artwork = findChild(shell, "embedded-artwork");
            tryCompare(artwork, "status", Image.Ready);
            verify(artwork.visible);
            verify(artwork.source.toString().startsWith("image://"));
        }
        verify(waitForPolish(shell));
        verifyGeometry(shell, initial, "Metadata");

        testScenarioDriver.enqueueLocalSelection(["C:/Synthetic/Another longer title without tags.mp3"]); // qmllint disable unqualified
        mouseClick(findChild(shell, "open-another-audio"));
        tryCompare(testLocalPlayerPresenter, "playbackState", PlayerPresenter.Loading); // qmllint disable unqualified
        compare(findChild(shell, "current-title").text, "Another longer title without tags");
        compare(findChild(shell, "current-artist").text, "");
        compare(findChild(shell, "current-album").text, "");
        compare(findChild(shell, "embedded-artwork").source.toString(), "");
        verify(waitForPolish(shell));
        verifyGeometry(shell, initial, "Next track");
    }
}
