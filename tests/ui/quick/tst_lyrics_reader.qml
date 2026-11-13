pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "LyricsReader"
    when: windowShown
    property var host
    property var reader
    property var seekRequests: []

    Component {
        id: hostComponent
        ApplicationWindow {
            width: 720
            height: 520
            visible: true

            LyricsReader {
                objectName: "tested-lyrics-reader"
                contract: testShellUiContract // qmllint disable unqualified
                onSeekRequested: timeMs => testCase.seekRequests.push(timeMs)
                onTextSizeRequested: size => textSize = size
            }
        }
    }

    function init() {
        seekRequests = [];
        host = createTemporaryObject(hostComponent, null);
        verify(host);
        host.requestActivate();
        tryCompare(host, "active", true);
        reader = findChild(host, "tested-lyrics-reader");
        verify(reader);
    }

    function openSynchronized() {
        reader.synchronizedLyrics = true;
        reader.canSeek = true;
        reader.verses = Array.from({
            length: 70
        }, (_, index) => ({
                    text: index === 3 ? "<b>日本語</b>" : "Verse " + index,
                    timeMs: index * 1000
                }));
        reader.activeVerse = 3;
        reader.open();
        tryCompare(reader, "opened", true);
        const list = findChild(reader, "lyrics-reader-verses");
        verify(list);
        tryCompare(list, "activeFocus", true);
        tryVerify(() => list.width > 0 && list.height > 0 && list.itemAtIndex(reader.activeVerse) !== null);
        return list;
    }

    function test_confirmedVerseAndKeyboardNavigationAreIndependent() {
        const list = openSynchronized();
        const confirmed = list.itemAtIndex(3);
        verify(confirmed);
        compare(confirmed.contentItem.text, "<b>日本語</b>");
        compare(confirmed.contentItem.textFormat, Text.PlainText);
        verify(confirmed.playing);

        keyClick(Qt.Key_Down);
        compare(list.currentIndex, 4);
        compare(reader.followingPlayback, false);
        verify(confirmed.playing);
        keyClick(Qt.Key_Return);
        compare(seekRequests.length, 1);
        compare(seekRequests[0], 4000);
        verify(confirmed.playing);

        reader.activeVerse = 5;
        compare(list.currentIndex, 4);
        verify(!confirmed.playing);
        const follow = findChild(reader, "lyrics-reader-follow");
        verify(follow && follow.enabled);
        mouseClick(follow);
        compare(reader.followingPlayback, true);
        compare(list.currentIndex, 5);
        tryVerify(() => list.itemAtIndex(5) && list.itemAtIndex(5).playing);

        reader.canSeek = false;
        keyClick(Qt.Key_Return);
        compare(seekRequests.length, 1);
        keyClick(Qt.Key_Escape);
        tryCompare(reader, "opened", false);
    }

    function test_mouseWheelPausesFollowingAndNewTrackClosesReader() {
        const list = openSynchronized();
        mouseWheel(list, Math.round(list.width / 2), Math.round(list.height / 2), 0, -120);
        tryCompare(reader, "followingPlayback", false);
        reader.activeVerse = 40;
        const follow = findChild(reader, "lyrics-reader-follow");
        verify(follow && follow.enabled);
        mouseClick(follow);
        tryCompare(list, "currentIndex", 40);
        tryVerify(() => list.itemAtIndex(40) && list.itemAtIndex(40).playing);
        reader.trackIdentity = "different track";
        tryCompare(reader, "opened", false);
    }

    function test_plainTextResizesAndProtectedTextStaysNonSelectable() {
        reader.plainText = "<i>日本語</i>\n\nעברית";
        reader.open();
        tryCompare(reader, "opened", true);
        const plain = findChild(reader, "lyrics-reader-plain");
        verify(plain && plain.visible);
        compare(plain.text, reader.plainText);
        compare(plain.textFormat, Text.PlainText);
        verify(plain.selectByMouse);
        verify(!findChild(reader, "lyrics-reader-follow").visible);
        mouseClick(findChild(reader, "lyrics-reader-larger"));
        compare(plain.font.pixelSize, 20);
        verify(reader.y + reader.height <= host.height - reader.transportInset);

        reader.copyProtected = true;
        reader.attribution = "Powered by LyricFind";
        const protectedText = findChild(reader, "lyrics-reader-protected-text");
        verify(protectedText && protectedText.visible);
        compare(protectedText.text, reader.plainText);
        compare(protectedText.textFormat, Text.PlainText);
        compare(typeof protectedText.selectByMouse, "undefined");
        verify(!plain.visible);
        compare(plain.text, "");
        verify(findChild(reader, "lyrics-reader-attribution").visible);
    }
}
