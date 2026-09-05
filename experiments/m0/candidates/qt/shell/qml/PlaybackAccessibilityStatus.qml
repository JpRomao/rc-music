import QtQuick
import MusicPlayer.M0

Item {
    id: root
    objectName: "playback-accessibility-status"
    required property ShellUiContract contract
    required property PlayerPresenter presenter
    required property ErrorText errorLabels
    property int observedState: -1
    property string observedLoadingOccurrence: ""
    property string announcedTrackOccurrence: ""
    property string announcedError: ""
    property bool deferredErrorPending: false
    property bool deferredSkippedFilesPending: false
    width: 0
    height: 0
    Accessible.ignored: true

    PlaybackAccessibilityText {
        id: labels
    }

    function announce(message, politeness) {
        if (message.length > 0)
            root.Accessible.announce(message, politeness);
    }

    function errorFingerprint() {
        return presenter.currentOccurrenceId.length + ":" + presenter.currentOccurrenceId + presenter.errorMessageKey.length + ":" + presenter.errorMessageKey + presenter.errorActionId.length + ":" + presenter.errorActionId;
    }

    function errorTitle() {
        return presenter.usesFrozenFixture ? contract.errorTitle : errorLabels.titleFor(presenter.errorMessageKey);
    }

    function errorBody() {
        return presenter.usesFrozenFixture ? contract.errorBody : errorLabels.messageFor(presenter.errorMessageKey);
    }

    function errorAction() {
        return presenter.usesFrozenFixture ? contract.actionSkip : errorLabels.actionFor(presenter.errorActionId);
    }

    function handlePlaybackChanged() {
        const state = presenter.playbackState;
        const occurrence = presenter.currentOccurrenceId;
        // The import status owns speech while discovery is active. This keeps
        // progress batches from competing with its single terminal summary.
        if (presenter.sourceImportActive) {
            observedState = state;
            deferredErrorPending = state === PlayerPresenter.Error;
            if (state === PlayerPresenter.Loading)
                observedLoadingOccurrence = occurrence;
            else if (state === PlayerPresenter.Ready || state === PlayerPresenter.Paused || state === PlayerPresenter.Playing)
                announcedTrackOccurrence = occurrence;
            return;
        }
        deferredErrorPending = false;
        if (state === PlayerPresenter.Loading) {
            if (observedState !== PlayerPresenter.Loading || observedLoadingOccurrence !== occurrence) {
                observedLoadingOccurrence = occurrence;
                announce(labels.loadingAudio, Accessible.Polite);
            }
        } else if (state === PlayerPresenter.Ready || state === PlayerPresenter.Paused || state === PlayerPresenter.Playing) {
            if (presenter.hasCurrentTrack && occurrence.length > 0 && announcedTrackOccurrence !== occurrence) {
                announcedTrackOccurrence = occurrence;
                announce(labels.trackLoaded(presenter.currentTitle), Accessible.Polite);
            }
        } else if (state === PlayerPresenter.Error) {
            const fingerprint = errorFingerprint();
            if (announcedError !== fingerprint) {
                announcedError = fingerprint;
                announce(labels.terminalError(errorTitle(), errorBody(), errorAction()), Accessible.Assertive);
            }
        }
        observedState = state;
    }

    function handleSourceImportChanged() {
        if (presenter.sourceImportActive || (!deferredErrorPending && !deferredSkippedFilesPending))
            return;
        Qt.callLater(function () {
            if (root.presenter.sourceImportActive || (!root.deferredErrorPending && !root.deferredSkippedFilesPending))
                return;
            const shouldAnnounce = root.deferredErrorPending && root.presenter.playbackState === PlayerPresenter.Error;
            root.deferredErrorPending = false;
            if (root.deferredSkippedFilesPending) {
                root.deferredSkippedFilesPending = false;
                root.announce(labels.skippedDuringImport, Accessible.Assertive);
            }
            if (shouldAnnounce)
                root.handlePlaybackChanged();
        });
    }

    function handleOccurrenceAutomaticallySkipped(title, errorCode) {
        // The signal precedes playbackChanged and describes the old occurrence.
        // Observe the accepted successor now so its loading notice does not repeat it.
        observedState = presenter.playbackState;
        observedLoadingOccurrence = presenter.currentOccurrenceId;
        deferredErrorPending = false;
        if (presenter.sourceImportActive) {
            // A skipped occurrence may predate this import and be absent from
            // its issue count. Keep one bounded summary, not a speech backlog.
            deferredSkippedFilesPending = true;
            return;
        }
        announce(labels.automaticallySkipped(title, errorLabels.messageFor(errorCode)), Accessible.Assertive);
    }

    function handleSeekRejected() {
        if (!presenter.sourceImportActive)
            announce(labels.seekRejected, Accessible.Assertive);
    }

    Component.onCompleted: {
        observedState = presenter.playbackState;
        observedLoadingOccurrence = presenter.playbackState === PlayerPresenter.Loading ? presenter.currentOccurrenceId : "";
        announcedTrackOccurrence = presenter.playbackState === PlayerPresenter.Ready || presenter.playbackState === PlayerPresenter.Paused || presenter.playbackState === PlayerPresenter.Playing ? presenter.currentOccurrenceId : "";
        deferredErrorPending = presenter.sourceImportActive && presenter.playbackState === PlayerPresenter.Error;
        if (!presenter.sourceImportActive && presenter.playbackState === PlayerPresenter.Error)
            announcedError = errorFingerprint();
    }

    Connections {
        target: root.presenter

        function onPlaybackChanged() {
            root.handlePlaybackChanged();
        }

        function onSourceImportChanged() {
            root.handleSourceImportChanged();
        }

        function onSeekRejected() {
            root.handleSeekRejected();
        }

        function onOccurrenceAutomaticallySkipped(title, errorCode) {
            root.handleOccurrenceAutomaticallySkipped(title, errorCode);
        }
    }
}
