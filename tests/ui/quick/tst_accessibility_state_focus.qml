pragma ComponentBehavior: Bound

import QtQuick
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "AccessibilityStateFocus"
    when: windowShown

    readonly property PlayerPresenter presenter: testLocalPlayerPresenter // qmllint disable unqualified
    readonly property var driver: testScenarioDriver // qmllint disable unqualified
    property var shell

    Component {
        id: shellComponent
        Main {
            uiContract: testShellUiContract // qmllint disable unqualified
            presenter: testCase.presenter
        }
    }
    QtObject {
        id: fakeLyrics
        property string title: "Cantiga"
        property string artist: "Artista"
        property string album: "Disco"
        property int durationMs: 180000
        property string statusText: ""
        property string currentLine: "Primeiro verso"
        property string nextLine: "Segundo verso"
        property string plainText: ""
        property bool hasSyncedLyrics: true
        property bool busy: false
        property bool canSearch: true
        property bool lyricsCopyProtected: false
    }
    Component {
        id: externalFocusComponent
        Item {
            objectName: "external-focus-owner"
            activeFocusOnTab: true
            width: 32
            height: 32
        }
    }

    function focusItem(item) {
        verify(item);
        item.forceActiveFocus(Qt.TabFocusReason);
        tryVerify(function () {
            return item.activeFocus;
        });
    }

    function openLocalTrack(path) {
        driver.enqueueLocalSelection([path]);
        const openAudio = findChild(shell, "open-audio");
        verify(openAudio && openAudio.visible && openAudio.enabled);
        openAudio.Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
    }

    function init() {
        driver.resetLocalScenario();
        shell = createTemporaryObject(shellComponent, null);
        verify(shell);
        shell.requestActivate();
        tryVerify(function () {
            return testCase.shell.active;
        });
    }

    function cleanup() {
        if (shell)
            shell.destroy();
        shell = null;
        driver.resetLocalScenario();
    }

    function test_focusMovesToThePrimaryActionWhenItsPlaybackStateDisappears() {
        const emptyAction = findChild(shell, "open-audio");
        focusItem(emptyAction);

        openLocalTrack("C:/Música/Foco entre estados.mp3");
        const playingAction = findChild(shell, "open-audio");
        tryVerify(function () {
            return playingAction.activeFocus;
        });

        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        verify(playingAction.activeFocus, "Loading and Playing share one focus scope");
        // Abrir áudio sits in the header, outside every state scope, so it keeps the focus through the error;
        // the proof of the focus leaving a disappearing state starts from a button of the playing state.
        focusItem(findChild(shell, "track-details-playing"));

        driver.publishLocalError();
        tryCompare(presenter, "playbackState", PlayerPresenter.Error);
        const errorAction = findChild(shell, "error-open-audio");
        wait(0);
        verify(errorAction.activeFocus, "Focus after disabling transport: active=" + String(shell.activeFocusItem) + ", last=" + String(shell.lastActiveFocusItem));

        driver.enqueueLocalSelection(["C:/Música/Recuperação com foco.flac"]);
        errorAction.Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        tryVerify(function () {
            return playingAction.activeFocus;
        });

        verify(presenter.clearQueue());
        tryCompare(presenter, "playbackState", PlayerPresenter.Unloaded);
        tryVerify(function () {
            return emptyAction.activeFocus;
        });
    }

    function test_heroFocusMovesToOpenAudioWhenTheTrackGoes() {
        openLocalTrack("C:/Música/Foco no destaque.mp3");
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        focusItem(findChild(shell, "track-details-playing"));
        verify(presenter.clearQueue());
        tryCompare(presenter, "playbackState", PlayerPresenter.Unloaded);
        const openAudio = findChild(shell, "open-audio");
        tryVerify(function () {
            return openAudio.activeFocus;
        });

        openLocalTrack("C:/Música/Foco na letra.mp3");
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        shell.lyrics = fakeLyrics;
        const lyricsButton = findChild(shell, "home-lyrics");
        tryVerify(function () {
            return lyricsButton.enabled;
        });
        focusItem(lyricsButton);
        driver.publishLocalError();
        tryCompare(presenter, "playbackState", PlayerPresenter.Error);
        const errorAction = findChild(shell, "error-open-audio");
        tryVerify(function () {
            return errorAction.activeFocus;
        });
    }

    function test_stateChangesNeverStealFocusFromOutsideTheDisappearingState() {
        const external = createTemporaryObject(externalFocusComponent, shell.contentItem);
        verify(external);
        focusItem(external);

        openLocalTrack("C:/Música/Foco externo.mp3");
        verify(external.activeFocus);

        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        verify(external.activeFocus);

        driver.publishLocalError();
        tryCompare(presenter, "playbackState", PlayerPresenter.Error);
        verify(external.activeFocus);

        verify(presenter.clearQueue());
        tryCompare(presenter, "playbackState", PlayerPresenter.Unloaded);
        verify(external.activeFocus);
    }

    function test_disablingFocusedTransportMovesFocusToTheNewPrimaryAction() {
        openLocalTrack("C:/Música/Foco no transporte.mp3");
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);

        const playPause = findChild(shell, "play-pause");
        focusItem(playPause);
        driver.publishLocalError();
        tryCompare(presenter, "playbackState", PlayerPresenter.Error);
        const errorAction = findChild(shell, "error-open-audio");
        wait(0);
        verify(errorAction.activeFocus, "Focus after disabling transport: active=" + String(shell.activeFocusItem) + ", last=" + String(shell.lastActiveFocusItem));

        driver.enqueueLocalSelection(["C:/Música/Foco no progresso.flac"]);
        errorAction.Accessible.pressAction();
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);
        const progress = findChild(shell, "progress");
        focusItem(progress);
        driver.publishLocalUnseekablePlaying();
        tryCompare(presenter, "seekable", false);
        tryVerify(function () {
            return playPause.activeFocus;
        });

        driver.publishLocalPlaying();
        tryCompare(presenter, "seekable", true);
        focusItem(progress);

        verify(presenter.clearQueue());
        tryCompare(presenter, "playbackState", PlayerPresenter.Unloaded);
        const emptyAction = findChild(shell, "open-audio");
        tryVerify(function () {
            return emptyAction.activeFocus;
        });
    }

    function test_queueAndPreferencesKeepFocusAcrossPlaybackStateChanges() {
        openLocalTrack("C:/Música/Foco em superfícies.mp3");
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);

        const queueButton = findChild(shell, "player-queue");
        focusItem(queueButton);
        shell.setQueueOpen(true);
        const queueList = findChild(shell, "queue-list");
        tryVerify(function () {
            return queueList.activeFocus;
        });
        driver.publishLocalError();
        tryCompare(presenter, "playbackState", PlayerPresenter.Error);
        verify(queueList.activeFocus);

        shell.setQueueOpen(false);
        const preferencesButton = findChild(shell, "preferences");
        mouseClick(preferencesButton);
        const preferences = findChild(shell, "preferences-panel");
        tryVerify(function () {
            return preferences.opened;
        });
        const preferencesControl = findChild(shell, "preferences-general");
        tryVerify(function () {
            return preferencesControl.activeFocus;
        });

        verify(presenter.clearQueue());
        tryCompare(presenter, "playbackState", PlayerPresenter.Unloaded);
        verify(preferencesControl.activeFocus);
    }

    function test_detailsAndStatusBannerKeepFocusAcrossPlaybackStateChanges() {
        openLocalTrack("C:/Música/Foco em detalhes e aviso.mp3");
        const detailsOpener = findChild(shell, "track-details-playing");
        verify(detailsOpener.enabled);
        detailsOpener.Accessible.pressAction();
        const detailsPanel = findChild(shell, "track-details-panel");
        tryVerify(function () {
            return detailsPanel.opened;
        });
        const detailsControl = findChild(shell, "details-copy-path");
        focusItem(detailsControl);

        driver.publishLocalError();
        tryCompare(presenter, "playbackState", PlayerPresenter.Error);
        verify(detailsControl.activeFocus);

        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !detailsPanel.visible;
        });
        driver.enqueueLocalSelection(["C:/Música/Foco antes do aviso.flac"]);
        verify(presenter.openAudio());
        tryCompare(presenter, "playbackState", PlayerPresenter.Loading);
        driver.publishLocalPlaying();
        tryCompare(presenter, "playbackState", PlayerPresenter.Playing);

        const centerX = shell.width / 2;
        const centerY = shell.height / 2;
        compare(driver.beginUrlDrag(shell, centerX, centerY, ["C:/Música/Pasta"]), Qt.CopyAction);
        compare(driver.dropDrag(shell, centerX, centerY), Qt.CopyAction);
        tryCompare(presenter, "sourceImportState", PlayerPresenter.Discovering);
        const bannerAction = findChild(shell, "source-import-cancel");
        focusItem(bannerAction);

        driver.publishLocalError();
        tryCompare(presenter, "playbackState", PlayerPresenter.Error);
        verify(bannerAction.activeFocus);
    }
}
