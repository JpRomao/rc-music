#include "../../support/FakeRoomHost.hpp"
#include "../../support/FakeRoomTransport.hpp"

#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "m0/application/ListeningRoomGuest.hpp"
#include "m0/application/ListeningRoomHost.hpp"
#include "m0/application/PreferencesAutosave.hpp"
#include "m0/application/SessionAutosave.hpp"
#include "m0/domain/ArtworkLimits.hpp"
#include "m0/ports/AudioFileLocator.hpp"
#include "m0/ports/AudioFilesPicker.hpp"
#include "m0/ports/AudioOutputSelection.hpp"
#include "m0/ports/LocalSourceDiscovery.hpp"
#include "m0/ports/MetadataReader.hpp"
#include "m0/ports/PlaybackBackend.hpp"
#include "m0/ports/SystemPowerEvents.hpp"
#include "presentation/ArtworkImageProvider.hpp"
#include "presentation/PlayerPresenterScenarioControl.hpp"
#include "presentation/PlayerPresenterTestAccess.hpp"
#include "presentation/PresenterRoomHostPlayer.hpp"

#include <QAbstractItemModelTester>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QImage>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QPersistentModelIndex>
#include <QSet>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace music_player::m0::test {
namespace {

class PresenterFilesPicker final : public AudioFilesPicker {
  public:
    bool pick(AudioFilesSelectionCompletion completion) override {
        ++requests;
        if (!accept)
            return false;
        pending = std::move(completion);
        if (immediate.has_value())
            complete(*immediate);
        if (throw_after_completion)
            throw std::runtime_error{"picker launch failed"};
        if (reject_after_completion)
            return false;
        return true;
    }
    void cancel() noexcept override {
        ++cancels;
        pending = {};
        if (on_cancel)
            on_cancel();
    }
    void complete(AudioFilesSelectionResult result) {
        auto callback = std::exchange(pending, {});
        if (callback)
            callback(std::move(result));
    }
    bool accept{true};
    bool throw_after_completion{false};
    bool reject_after_completion{false};
    int requests{0};
    int cancels{0};
    AudioFilesSelectionCompletion pending;
    std::optional<AudioFilesSelectionResult> immediate;
    std::function<void()> on_cancel;
};

class PresenterFileLocator final : public AudioFileLocator {
  public:
    bool locate(const std::filesystem::path& source, AudioFileLocationCompletion completion) override {
        requests.push_back(source);
        if (!accept)
            return false;
        pending = std::move(completion);
        if (immediate.has_value())
            complete(*immediate);
        return true;
    }
    void cancel() noexcept override {
        ++cancels;
        pending = {};
    }
    void complete(AudioFileLocationResult result) {
        auto callback = std::exchange(pending, {});
        if (callback)
            callback(std::move(result));
    }
    bool accept{true};
    int cancels{0};
    std::optional<AudioFileLocationResult> immediate;
    std::vector<std::filesystem::path> requests;
    AudioFileLocationCompletion pending;
};

class AvailableTrayPlatform final : public PlatformIntegration {
  public:
    bool available{true};
    [[nodiscard]] bool trayAvailable() const noexcept override { return available; }
    [[nodiscard]] bool defaultAppsSettingsSupported() const noexcept override { return default_apps_supported; }
    [[nodiscard]] bool defaultAppsSettingsAvailable() const noexcept override { return default_apps_available; }
    void openDefaultAppsSettings(DefaultAppsSettingsCompletion completion) override {
        ++default_apps_settings_calls;
        if (throw_on_default_apps_settings_launch)
            throw std::runtime_error{"default apps settings launch failed"};
        if (default_apps_settings_immediate_result.has_value()) {
            completion(*default_apps_settings_immediate_result);
            return;
        }
        pending_default_apps_settings_completion = std::move(completion);
    }
    void completeDefaultAppsSettings(DefaultAppsSettingsLaunchResult result) {
        auto completion = std::exchange(pending_default_apps_settings_completion, {});
        if (completion)
            completion(result);
    }
    void hideWindow() override { ++hide_calls; }
    void minimizeWindow() override { ++minimize_calls; }
    void showAndActivateWindow() override { ++restore_calls; }
    void requestExit() override {
        ++exit_calls;
        if (on_exit)
            on_exit();
    }
    int hide_calls{0};
    int minimize_calls{0};
    int restore_calls{0};
    int exit_calls{0};
    bool default_apps_supported{true};
    bool default_apps_available{true};
    bool throw_on_default_apps_settings_launch{false};
    std::optional<DefaultAppsSettingsLaunchResult> default_apps_settings_immediate_result{};
    DefaultAppsSettingsCompletion pending_default_apps_settings_completion{};
    int default_apps_settings_calls{0};
    std::function<void()> on_exit;
};

class PresenterPlaybackBackend : public PlaybackBackend {
  public:
    [[nodiscard]] const PlaybackSnapshot& snapshot() const noexcept override { return snapshot_; }
    [[nodiscard]] bool sourceVolumeRemovalObserved(const std::uint64_t generation) const noexcept override {
        return observed_source_volume_removal == std::optional{generation};
    }
    void setEventCallback(PlaybackEventCallback callback) override {
        event_callback_ = std::move(callback);
        ++callback_assignments;
    }
    PlaybackCommandResult load(const std::filesystem::path& source_path) override {
        loaded_paths.push_back(source_path);
        const PlaybackCommandResult result = [this] {
            if (load_results.empty())
                return load_result;
            const PlaybackCommandResult next = load_results.front();
            load_results.pop_front();
            return next;
        }();
        if (result == PlaybackCommandResult::Accepted) {
            snapshot_ = {.status = accepted_load_status.value_or(PlaybackStatus::Loading),
                         .generation = ++last_generation_};
            if (snapshot_.status == PlaybackStatus::Error)
                snapshot_.error = accepted_load_error;
        }
        return result;
    }
    PlaybackCommandResult play() override {
        ++play_calls;
        return play_result;
    }
    PlaybackCommandResult pause() override {
        ++pause_calls;
        if (on_pause)
            on_pause();
        return pause_result;
    }
    PlaybackCommandResult seekTo(const std::int64_t position_ms) override {
        seek_positions.push_back(position_ms);
        if (on_seek)
            on_seek();
        return seek_result;
    }
    PlaybackCommandResult setVolume(const double volume) override {
        volumes.push_back(volume);
        return volume_result;
    }
    PlaybackCommandResult setMuted(const bool muted) override {
        muted_values.push_back(muted);
        return muted_result;
    }
    PlaybackCommandResult unload() override {
        ++unload_calls;
        if (unload_result == PlaybackCommandResult::Accepted) {
            snapshot_ = {.status = PlaybackStatus::Empty, .generation = ++last_generation_};
        }
        return unload_result;
    }
    PlaybackCommandResult shutdown() override {
        ++shutdown_calls;
        if (on_shutdown)
            on_shutdown();
        return shutdown_result;
    }

    void publish(PlaybackSnapshot snapshot) {
        snapshot_ = snapshot;
        if (event_callback_)
            event_callback_({PlaybackEventType::SnapshotChanged, snapshot_});
    }

    void publishEvent(const PlaybackEventType type, PlaybackSnapshot snapshot, const bool update_snapshot = true) {
        if (update_snapshot)
            snapshot_ = snapshot;
        if (event_callback_)
            event_callback_({type, snapshot});
    }

    [[nodiscard]] bool callbackInstalled() const noexcept { return static_cast<bool>(event_callback_); }

    PlaybackSnapshot snapshot_{};
    std::optional<std::uint64_t> observed_source_volume_removal;
    PlaybackEventCallback event_callback_{};
    PlaybackCommandResult load_result{PlaybackCommandResult::Accepted};
    std::optional<PlaybackStatus> accepted_load_status{};
    PlaybackErrorCode accepted_load_error{PlaybackErrorCode::SourceUnavailable};
    std::deque<PlaybackCommandResult> load_results;
    PlaybackCommandResult play_result{PlaybackCommandResult::Accepted};
    PlaybackCommandResult pause_result{PlaybackCommandResult::Accepted};
    PlaybackCommandResult seek_result{PlaybackCommandResult::Accepted};
    PlaybackCommandResult volume_result{PlaybackCommandResult::Accepted};
    PlaybackCommandResult muted_result{PlaybackCommandResult::Accepted};
    PlaybackCommandResult unload_result{PlaybackCommandResult::Accepted};
    PlaybackCommandResult shutdown_result{PlaybackCommandResult::Accepted};
    std::vector<std::filesystem::path> loaded_paths;
    std::vector<std::int64_t> seek_positions;
    std::vector<double> volumes;
    std::vector<bool> muted_values;
    int callback_assignments{0};
    int play_calls{0};
    int pause_calls{0};
    int unload_calls{0};
    int shutdown_calls{0};
    std::function<void()> on_shutdown;

    std::function<void()> on_pause;
    std::function<void()> on_seek;

  private:
    std::uint64_t last_generation_{0};
};

class PresenterAudioOutputBackend final : public PresenterPlaybackBackend, public AudioOutputSelection {
  public:
    [[nodiscard]] std::vector<AudioOutputDevice> audioOutputs() const override { return outputs_; }
    [[nodiscard]] std::string selectedAudioOutputId() const override { return selected_; }
    bool selectAudioOutput(const std::string_view id) override {
        if (!id.empty() && std::ranges::none_of(outputs_, [id](const auto& device) { return device.id == id; }))
            return false;
        if (selected_ == id)
            return true;
        selected_ = id;
        if (snapshot().status == PlaybackStatus::Playing)
            interruptOutput(PlaybackErrorCode::AudioOutputChanged);
        if (changed_)
            changed_();
        return true;
    }
    void setAudioOutputsChangedCallback(std::function<void()> callback) override { changed_ = std::move(callback); }
    void setOutputs(std::vector<AudioOutputDevice> outputs) {
        outputs_ = std::move(outputs);
        if (changed_)
            changed_();
    }
    void interruptOutput(const PlaybackErrorCode code) {
        auto interrupted = snapshot();
        interrupted.status = PlaybackStatus::Error;
        interrupted.error = code;
        interrupted.seekable = false;
        publish(interrupted);
    }

  private:
    std::vector<AudioOutputDevice> outputs_;
    std::string selected_;
    std::function<void()> changed_;
};

class PresenterMetadataReader final : public MetadataReader {
  public:
    MetadataCommandResult startRead(MetadataReadRequest request) override {
        requests.push_back(std::move(request));
        return start_result;
    }
    MetadataCommandResult cancelRead(const MetadataGeneration generation) override {
        cancelled_generations.push_back(generation);
        return cancel_result;
    }
    [[nodiscard]] MetadataPollResult poll(const MetadataGeneration generation) override {
        polled_generations.push_back(generation);
        if (poll_results.empty())
            return {MetadataPollStatus::Pending, std::nullopt};
        auto result = std::move(poll_results.front());
        poll_results.pop_front();
        return result;
    }
    [[nodiscard]] std::optional<MetadataReadCompletion> tryTakeCompletion() override { return std::nullopt; }
    MetadataCommandResult shutdown() override { return MetadataCommandResult::Accepted; }

    MetadataCommandResult start_result{MetadataCommandResult::Accepted};
    MetadataCommandResult cancel_result{MetadataCommandResult::Accepted};
    std::vector<MetadataReadRequest> requests;
    std::vector<MetadataGeneration> cancelled_generations;
    std::vector<MetadataGeneration> polled_generations;
    std::deque<MetadataPollResult> poll_results;
};

class FakeLocalSourceDiscovery final : public LocalSourceDiscovery {
  public:
    DiscoveryCommandResult start(LocalSourceDiscoveryRequest request) override {
        requests.push_back(std::move(request));
        return start_result;
    }

    DiscoveryCommandResult cancel(const DiscoveryGeneration generation) override {
        cancelled_generations.push_back(generation);
        return cancel_result;
    }

    [[nodiscard]] DiscoveryPollResult poll(const DiscoveryGeneration generation) override {
        polled_generations.push_back(generation);
        if (poll_results.empty())
            return {.status = DiscoveryPollStatus::Pending};
        DiscoveryPollResult result = std::move(poll_results.front());
        poll_results.pop_front();
        return result;
    }

    DiscoveryCommandResult shutdown() override {
        ++shutdown_calls;
        return shutdown_result;
    }

    void publishBatch(const DiscoveryGeneration generation, const std::uint64_t sequence,
                      std::vector<std::filesystem::path> accepted_paths, std::vector<LocalSourceDiscoveryIssue> issues,
                      const std::uint64_t inspected_count) {
        poll_results.push_back({.status = DiscoveryPollStatus::BatchReady,
                                .batch = LocalSourceDiscoveryBatch{.generation = generation,
                                                                   .sequence = sequence,
                                                                   .inspected_count = inspected_count,
                                                                   .accepted_paths = std::move(accepted_paths),
                                                                   .issues = std::move(issues)}});
    }

    void publishCompleted(const LocalSourceDiscoverySummary summary) {
        poll_results.push_back({.status = DiscoveryPollStatus::Completed, .summary = summary});
    }

    void publishCancelled() { poll_results.push_back({.status = DiscoveryPollStatus::Cancelled}); }

    DiscoveryCommandResult start_result{DiscoveryCommandResult::Accepted};
    DiscoveryCommandResult cancel_result{DiscoveryCommandResult::Accepted};
    DiscoveryCommandResult shutdown_result{DiscoveryCommandResult::Accepted};
    std::vector<LocalSourceDiscoveryRequest> requests;
    std::vector<DiscoveryGeneration> cancelled_generations;
    std::vector<DiscoveryGeneration> polled_generations;
    std::deque<DiscoveryPollResult> poll_results;
    int shutdown_calls{0};
};

class PresenterPreferencesWriter final : public PreferencesWriter {
  public:
    PreferencesWriterCommand start(PreferencesWriteRequest request) override {
        if (active.has_value())
            return PreferencesWriterCommand::Busy;
        active = request;
        starts.push_back(std::move(request));
        return PreferencesWriterCommand::Accepted;
    }

    std::optional<PreferencesWriteCompletion> poll() override {
        if (completions.empty())
            return std::nullopt;
        auto completion = completions.front();
        completions.pop_front();
        if (active.has_value() && active->revision == completion.revision)
            active.reset();
        return completion;
    }

    std::optional<PreferencesWriteCompletion> flush(PreferencesWriteRequest request) override {
        flushes.push_back(request);
        if (on_flush)
            on_flush(request);
        active.reset();
        return PreferencesWriteCompletion{.revision = request.revision, .status = flush_status};
    }

    bool shutdown() override { return true; }

    void complete(const PreferencesSaveStatus status) {
        QVERIFY(active.has_value());
        completions.push_back({.revision = active->revision, .status = status});
    }

    std::optional<PreferencesWriteRequest> active;
    std::deque<PreferencesWriteCompletion> completions;
    std::vector<PreferencesWriteRequest> starts;
    std::vector<PreferencesWriteRequest> flushes;
    PreferencesSaveStatus flush_status{PreferencesSaveStatus::Saved};
    std::function<void(const PreferencesWriteRequest&)> on_flush;
};

class PresenterSessionWriter final : public SessionWriter {
  public:
    SessionWriterCommand start(SessionWriteRequest request) override {
        if (active.has_value())
            return SessionWriterCommand::Busy;
        active = request;
        starts.push_back(std::move(request));
        return SessionWriterCommand::Accepted;
    }

    std::optional<SessionWriteCompletion> poll() override {
        if (completions.empty())
            return std::nullopt;
        auto completion = completions.front();
        completions.pop_front();
        if (active.has_value() && active->revision == completion.revision)
            active.reset();
        return completion;
    }

    std::optional<SessionWriteCompletion> flush(SessionWriteRequest request) override {
        flushes.push_back(request);
        active.reset();
        return SessionWriteCompletion{.revision = request.revision, .status = flush_status};
    }

    bool shutdown() override { return true; }

    void complete(const SessionSaveStatus status) {
        QVERIFY(active.has_value());
        completions.push_back({.revision = active->revision, .status = status});
    }

    std::optional<SessionWriteRequest> active;
    std::deque<SessionWriteCompletion> completions;
    std::vector<SessionWriteRequest> starts;
    std::vector<SessionWriteRequest> flushes;
    SessionSaveStatus flush_status{SessionSaveStatus::Saved};
};

struct PresenterPreferencesClock final {
    PreferencesAutosave::TimePoint current{};
    [[nodiscard]] PreferencesAutosave::TimePoint now() const noexcept { return current; }
    void advance(const std::chrono::milliseconds duration) noexcept { current += duration; }
};

Preferences nonDefaultPreferences() {
    return {.theme = ThemeMode::Dark,
            .motion = MotionMode::Reduced,
            .volume = 0.35,
            .muted = true,
            .skip_backward_seconds = 7,
            .skip_forward_seconds = 20,
            .close_behavior = CloseBehavior::Exit,
            .restore_last_session = false,
            .start_minimized = true,
            .queue_end_behavior = QueueEndBehavior::StopClearSelection};
}

[[nodiscard]] FixtureBundle frozenBundle() {
    return FixtureRepository{}.load(QString::fromUtf8(M0_TEST_FIXTURE_FILE), QString::fromUtf8(M0_TEST_QUEUE_FILE));
}

[[nodiscard]] std::filesystem::path localTestPath(const std::u8string& file_name) {
    return std::filesystem::absolute(std::filesystem::path{u8"local-test"} / std::filesystem::path{file_name});
}

[[nodiscard]] SessionSnapshot persistedSessionSnapshot() {
    const auto duplicate = localTestPath(u8"Abertura 日本語.mp3");
    return {
        .queue =
            {
                .items = {{{41}, duplicate}, {{42}, duplicate}},
                .current_occurrence_id = LocalQueueOccurrenceId{42},
            },
        .position_ms = 42'000,
    };
}

[[nodiscard]] QUrl localTestUrl(const std::filesystem::path& path) {
#ifdef _WIN32
    return QUrl::fromLocalFile(QString::fromStdWString(path.native()));
#else
    const auto& native = path.native();
    return QUrl::fromLocalFile(QString::fromUtf8(native.data(), static_cast<qsizetype>(native.size())));
#endif
}

[[nodiscard]] QString displayPath(const std::filesystem::path& path) {
#ifdef _WIN32
    return QString::fromStdWString(path.native());
#else
    const auto& native = path.native();
    return QString::fromUtf8(native.data(), static_cast<qsizetype>(native.size()));
#endif
}

[[nodiscard]] QStringList queueFileNames(const QAbstractItemModel& model) {
    QStringList names;
    names.reserve(model.rowCount());
    for (int row = 0; row < model.rowCount(); ++row)
        names.push_back(model.data(model.index(row, 0), QueueListModel::FileNameRole).toString());
    return names;
}

[[nodiscard]] QStringList queueOccurrenceIds(const QAbstractItemModel& model) {
    QStringList ids;
    for (int row = 0; row < model.rowCount(); ++row)
        ids.push_back(model.data(model.index(row, 0), LocalQueueListModel::OccurrenceIdRole).toString());
    return ids;
}

} // namespace

class PlayerPresenterTests final : public QObject {
    Q_OBJECT

  private slots:
    void fixtureOpenAudioPreservesTheFrozenModeContract();
    void productDefaultsAndEmptyStateNeedNoFixture();
    void productDefaultsRejectUnavailableAudioPreferences();
    void localOpenCancellationIsInert();
    void localOpenPublishesUnicodeFallbackAndStartsMetadata();
    void localOpenPublishesOrderedQueueWithDistinctDuplicateOccurrences();
    void playlistBatchAppendsWithoutSelectionOrPlaybackAndCopiesTheQueue();
    void acceptedLocalIngressIsReportedToTheLibraryObserver();
    void playSourcesNowInsertsAfterTheCurrentTrackAndStartsIt();
    void playSourcesNowWithoutACurrentTrackAppendsAndStarts();
    void playSourcesNowInsertsAfterAFailedOrLoadingCurrentTrack();
    void playSourcesNowWithShuffleStartsTheInsertedTrack();
    void playSourcesNowMarksARejectedFileUnavailable();
    void playSourcesNowRefusesInvalidInputAndGuestRooms();
    void playSourcesNowFollowsTheRoomPlayOrderForTheHost();
    void playSourcesNowAdmitsTheFilesToTheLibrary();
    void currentLocalSourceFollowsTheCurrentOccurrence();
    void playlistAppendPreservesActivePlaybackAndRejectsAnInvalidBatch();
    void playlistAppendAcknowledgesItsCommitWhenALaterObserverThrows();
    void openingAdditionalFilesPreservesTheQueueAndPlaysTheFirstNewOccurrence();
    void localQueueNavigationAvoidsAutoplayAndPreservesDuplicateOccurrence();
    void playNowUsesTheStableLocalOccurrenceIdAndAutoplays();
    void playNowPromotesTheCurrentPendingLoadWithoutReloading();
    void localQueueAppendMoveAndNonCurrentRemovalUseFineGrainedChanges();
    void shuffleQueuePreservesCurrentPrefixAndMetadata_data();
    void shuffleQueuePreservesCurrentPrefixAndMetadata();
    void shuffleQueueChangesTwoUpcomingItemsForEveryTestSeed();
    void queueTotalDurationSumsKnownDurations();
    void queueArtworkSinkReceivesTheAvailableOccurrences();
    void queueArtworkFollowsAvailabilityAndRelocation();
    void queueModelExposesArtworkSourcePerOccurrence();
    void queueTotalDurationIsUnknownWhenAnyTrackHasNoDuration();
    void queueTotalDurationIsUnknownForAnEmptyQueue();
    void nextQueueTitleNamesTheFollowingOccurrence();
    void nextQueueTitleSkipsUnavailableOccurrences();
    void nextQueueTitleIsEmptyWithShuffleOrAtTheEnd();
    void shuffleQueueWithNoCurrentShufflesAllRemainingOccurrences();
    void repeatQueueTogglePublishesAndPersistsWithoutTransport();
    void repeatQueueRotatesCompletedOccurrence_data();
    void repeatQueueRotatesCompletedOccurrence();
    void repeatQueueRejectedLoadPreservesOrder_data();
    void repeatQueueRejectedLoadPreservesOrder();
    void repeatQueueSkipsUnavailableAndNeverResurrectsRemovedOccurrences();
    void queueShuffleAndRepeatPersistTheNewOrder();
    void queueOrderObserversMayReplaceTheOperation_data();
    void queueOrderObserversMayReplaceTheOperation();
    void shuffleQueueModePublishesPersistsAndStopsWithoutRestoringOrder();
    void shuffleQueueModeVisitsEveryRemainingOccurrence_data();
    void shuffleQueueModeVisitsEveryRemainingOccurrence();
    void shuffleQueueModeRejectedNextPreservesOrderAndEligibility();
    void roomPlayOrderAdvancesInQueueOrderWhileShuffled();
    void openingARoomWhileShuffledReordersThePendingQueueOnce();
    void turningShuffleOnInARoomReordersWhatGuestsSee();
    void roomHostPlayerDelegatesQueueAndTransportToThePresenter();
    void roomSessionGateFiltersTheSavedSessionAndHoldsItsPosition();
    void roomSessionGateLeavesTheRoomBeforeTheFinalExitSave();
    void restoredRoomSessionPutsBackTheQueuePaused();
    void anEmptyRoomSessionPutsTheLocalQueueAway();
    void aGuestFollowingTheRoomShowsTheRoomTrackAndItsTime();
    void aGuestFollowingTheRoomAddsNoLocalSources();
    void hostRoomSessionExcludesGuestItems();
    void roomHostLeadsEachStartOnceOverQueuedConfirmations();
    void roomHostHoldsALoadingTrackUntilTheAnchor();
    void roomHostHoldsAResumeFromItsOwnControlsUntilTheAnchor();
    void roomHostLeadsTheSuccessorOfACurrentItemRemovedDuringTheWait();
    void pausingAReleasedRoomStartShowsPausedAtOnce();
    void guestRoomSessionKeepsThePreRoomSession();
    void roomPrivacyNoticeIsMarkedOnceAndPersisted();
    void roomOutputOffsetIsBoundedAndPersisted();
    void disablingShuffleFromAnOrderObserverStopsThePendingPermutation();
    void removingCurrentUsesAForwardSuccessorOrLeavesPlaybackEmpty();
    void removingPausedCurrentKeepsTheSuccessorReady();
    void addingAfterTheQueueBecomesEmptyStartsTheFirstNewOccurrence();
    void typedSourceImportPreservesOrderDuplicatesAndNativeMetacharacters();
    void typedSourceImportRejectsBusyWithoutMutatingTheActiveBatch();
    void droppedSourceImportValidatesBoundaryAndKeepsStableAnchor();
    void droppedSourceImportRejectsMalformedPathsWithoutLosingValidItems_data();
    void droppedSourceImportRejectsMalformedPathsWithoutLosingValidItems();
    void droppedSourceImportPreservesAbsoluteWindowsPaths_data();
    void droppedSourceImportPreservesAbsoluteWindowsPaths();
    void sourceImportTracksMovedAnchorAndFallsBackAfterItsRemoval();
    void sourceImportOnEmptyQueuePreservesTransientFirstLoadAndAutoplaysTheNext_data();
    void sourceImportOnEmptyQueuePreservesTransientFirstLoadAndAutoplaysTheNext();
    void sourceImportMarksInvalidArgumentLoadUnavailableAndContinues();
    void sourceImportCountsAnAsynchronousFailureOnceAfterCompletion();
    void dismissingSourceImportStopsTrackingCompletedOccurrences();
    void startingANewSourceImportStopsTrackingPreviousOccurrences();
    void sourceImportResumesAutoplayWhenALaterBatchFollowsAnAsynchronousFailure();
    void sourceImportDoesNotArmAutoplayAfterItsInitialTrackIsRemoved();
    void sourceImportIssuesBetweenBatchesDoNotCancelValidFiles();
    void cancellingSourceImportKeepsConsumedBatchesAndDiscardsLateOnes();
    void exitDrainsActiveAndAcceptedSourceImportsBeforeCommit();
    void exitCancelsANonIpcSourceImportWhenAcceptedIpcWorkIsDrained();
    void sourceDrainTimeoutCancelsExitWithoutDiscardingAcceptedWork();
    void playlistSaveFailureStopsExitBeforeOtherPreparation();
    void playlistSaveSuccessContinuesAnAcceptedExit();
    void playlistSaveExitTimeoutCannotCommitAfterLateSuccess();
    void discardingPlaylistEditRequiresANewExitRequest();
    void exitPreparationPublishesFreezeBeforeBeginAndWaitsForReady();
    void exitPreparationDoesNotReplaceAcceptedSourceDrain();
    void exitPreparationFailureIsReversibleWithoutDetachingTheGate_data();
    void exitPreparationFailureIsReversibleWithoutDetachingTheGate();
    void exitPreparationTimeoutAllowsANewIntentButKeepsTheGate();
    void exitPreparationSetterRejectsIncompleteReplacementAndWrongThread();
    void exitPreparationCanLoseItsPresenterDuringBeginOrPoll_data();
    void exitPreparationCanLoseItsPresenterDuringBeginOrPoll();
    void rejectedQueuePlaybackTransitionsLeaveTheQueueAtomic();
    void transientNavigationRejectionKeepsTheTargetAvailable();
    void rejectedLocalOpenDoesNotStartMetadataOrPublishTrackState();
    void rejectedAdditionalOpenPreservesTheCurrentLocalQueue();
    void rejectedAddPreservesTheCurrentLocalQueue();
    void localPlaybackErrorKeepsContextAndOffersReopen();
    void typedLocalPlaybackErrorsExposeAnExecutableRecovery_data();
    void typedLocalPlaybackErrorsExposeAnExecutableRecovery();
    void localEndedAdvancesToNextOccurrenceWithAutoplay();
    void localEndedAtQueueTailKeepsSelection();
    void localEndedAtQueueTailClearsSelectionWhenConfigured();
    void typedLocalErrorMarksOnlyFailedOccurrenceAndContinues_data();
    void typedLocalErrorMarksOnlyFailedOccurrenceAndContinues();
    void automaticSourceSkipPublishesContextBeforePlayback_data();
    void automaticSourceSkipPublishesContextBeforePlayback();
    void nonAutomaticSourceFailuresDoNotPublishSkippedContext_data();
    void nonAutomaticSourceFailuresDoNotPublishSkippedContext();
    void automaticSourceSkipObserversMayDestroyThePresenter_data();
    void automaticSourceSkipObserversMayDestroyThePresenter();
    void automaticSourceSkipDoesNotPublishObsoleteNotificationsAfterSelection();
    void automaticSourceSkipKeepsImportNotificationsIndependent_data();
    void automaticSourceSkipKeepsImportNotificationsIndependent();
    void retriesTheOriginalUnavailableOccurrenceWithoutDuplicatingOrResettingTheQueue();
    void locatesCurrentMissingFileAndRecoversPausedAtTheConfirmedPosition();
    void locatesOnlyTheChosenOccurrenceWithoutInterruptingAnotherTrack();
    void fileLocationCancellationRemovalAndDuplicateCompletionAreInert();
    void failedFileLocationPreservesThePreviousSourceAndRemainsActionable();
    void fileLocationCompletionCannotOutliveExitOrThePresenter();
    void synchronousFileLocationCompletionIsDeferredAndSingleShot();
    void relocatedSourceIsSavedWithItsOccurrenceAndPendingRestorePosition_data();
    void relocatedSourceIsSavedWithItsOccurrenceAndPendingRestorePosition();
    void cancellingFileLocationForExitNotifiesTheBusyProperty();
    void asynchronousOpenKeepsPlaybackEventsLiveAndAppendsToTheLatestQueue();
    void asynchronousAddPreservesTheCurrentTrackOrStartsAnEmptyQueue_data();
    void asynchronousAddPreservesTheCurrentTrackOrStartsAnEmptyQueue();
    void asynchronousSelectionCancellationFailureAndDuplicateCompletionAreSafe();
    void asynchronousSelectionRejectsInvalidPathsWithoutChangingPlayback_data();
    void asynchronousSelectionRejectsInvalidPathsWithoutChangingPlayback();
    void asynchronousSelectionIsCancelledOnExitAndCannotOutliveThePresenter();
    void asynchronousSelectionRebindingAndInlineCompletionAreGenerationSafe();
    void mainSelectionAndFileLocationCannotOverlap();
    void selectionObserverCanCancelOrDestroyBeforeTheNativeRequest_data();
    void selectionObserverCanCancelOrDestroyBeforeTheNativeRequest();
    void inlineSelectionCannotWinAfterLaunchFailure_data();
    void inlineSelectionCannotWinAfterLaunchFailure();
    void obsoleteSelectionFinishedCannotAffectAReentrantNewRequest();
    void failedAudioSelectionPreservesAnActiveImport_data();
    void failedAudioSelectionPreservesAnActiveImport();
    void selectionCompletionMayDestroyThePresenter_data();
    void selectionCompletionMayDestroyThePresenter();
    void pickerCancellationCannotStartAnotherDialog_data();
    void pickerCancellationCannotStartAnotherDialog();
    void pickerCancellationMayDestroyThePresenter_data();
    void pickerCancellationMayDestroyThePresenter();
    void audioOutputRecoveryPreservesOccurrenceAndPositionWithoutAutoplay_data();
    void audioOutputRecoveryPreservesOccurrenceAndPositionWithoutAutoplay();
    void manualAudioOutputSwitchRestoresPausedAndTopologyAloneNeverResumes();
    void sourceVolumeRemovalKeepsTheOccurrenceAndRecoversPaused_data();
    void sourceVolumeRemovalKeepsTheOccurrenceAndRecoversPaused();
    void observedSourceVolumeRemovalSupersedesQueuedTerminalDecoderEvents();
    void sourceVolumeRemovalDoesNotAutoplayLaterDiscoveryBatches();
    void repeatedAudioOutputRecoveryRetainsPositionAndRejectsBusyLoads();
    void supersededAudioOutputRecoveryCannotSeekTheNewSelection();
    void outputLossDuringRecoveryPreservesThePersistedTarget_data();
    void outputLossDuringRecoveryPreservesThePersistedTarget();
    void rejectedRetryPreservesTheCurrentTrackAndRepeatedFailureRemainsActionable();
    void retryCompletionCannotReviveAnOccurrenceAfterAnotherExplicitSelection();
    void retryReconcilesASynchronousLoadCompletion_data();
    void retryReconcilesASynchronousLoadCompletion();
    void explicitRetryPreventsLaterImportBatchesFromTakingOver_data();
    void explicitRetryPreventsLaterImportBatchesFromTakingOver();
    void localErrorContinuationPreservesStayReadyIntent();
    void localErrorContinuationPreservesPausedPlayback();
    void rejectedAutomaticContinuationRemainsManuallyRecoverable();
    void rejectedContinuationPreservesConfirmedPlayingIntent();
    void staleLocalTerminalEventsAreInertAfterAdvance();
    void currentReadyEventAutoplaysAndStaleEventsAreIgnored();
    void localTransportAcceptsAsynchronousPlayAndSeekWithALocalQueue();
    void rejectedLocalSeekNotifiesRestoredStateBeforeFeedback_data();
    void rejectedLocalSeekNotifiesRestoredStateBeforeFeedback();
    void rejectedLocalSeekNotificationMayDestroyThePresenter();
    void rejectedLocalSeekDoesNotAnnounceAfterAReentrantTrackChange();
    void inertLocalSeekCommandsDoNotAnnounceRejection();
    void currentMetadataCompletionReplacesFallbackAndStopsPolling();
    void projectsCompleteFixtureDetailsWithExplicitUnknowns();
    void projectsStructuredLocalDetailsWithoutLeakingReaderMessages();
    void projectsSanitizedMetadataDiagnosticsAsClosedIdentifiers_data();
    void projectsSanitizedMetadataDiagnosticsAsClosedIdentifiers();
    void projectsExactDecimalByteCountsBeyondJavascriptSafeInteger();
    void artworkCacheServesOnlyTheCurrentGenerationWithoutCopyingPixels();
    void currentArtworkUsesAnOpaqueUrlAndAcceptedOpenClearsIt();
    void terminalMetadataStatusStopsPollingWithoutReplacingFallback();
    void destructionClearsPlaybackCallbackAndCancelsPendingMetadata();
    void startsFromNullableEmptyStateAndAppliesEveryDefault();
    void appliesDefaultsThatDifferFromTheCoreDefaults();
    void loadsFrozenQueueOnceWithoutReplacingTheModel();
    void derivesBackendSnapshotFromTheTypedPlayingState();
    void rejectsUnknownMappingsAndExposesNoInfrastructureApi();
    void explicitPlayPauseShareIdempotentTransportSemantics();
    void asynchronousExplicitTransportUsesTheLatestUnacknowledgedIntent();
    void navigationUsesTheLatestTransportIntentBeforeAcknowledgement();
    void drivesTransportPreferencesAndManualTicksDeterministically();
    void keepsUnavailableAndTerminalTransportInert();
    void schedulesTicksOnlyWhilePlaybackOrSeekNeedsThem();
    void projectsTypedMetadataAndRecoversFrozenUnavailableScenario();
    void navigatesDistinctOccurrencesWithoutResettingTheModel();
    void previousUsesConfirmedThresholdAndPreservesPause();
    void navigationSkipsUnavailableItemsAndStopsAtBounds();
    void navigationIsInertDuringSeekAndInactiveStates();
    void exposesTypedPreferenceDefaults();
    void editsPreferencesWithoutChangingPlaybackOrTheQueue();
    void rejectsInvalidAndUnchangedPreferencesWithoutNotifications();
    void refreshesTrayAvailabilityAndGuardsStartMinimized();
    void opensDefaultAppsSettingsOnceUntilTheTypedCompletionArrives();
    void defaultAppsSettingsUnavailableAndFailureStatesAreRecoverable();
    void defaultAppsSettingsCompletionIsInertAfterExitOrDestruction();
    void rejectsPersistedWindowPlacementWithNonPositiveSize();
    void loadsPersistedPreferencesBeforePresentationWithoutAutosavingAValidDocument();
    void restoresPersistedSessionPausedBeforePresentationWithoutAutoplay();
    void retryRestoresThePausedSessionAfterTheSavedSourceReturns();
    void acceptsAnApproximateConfirmedPositionWhenRestoring();
    void waitsForRestorableDurationBeforeApplyingThePersistedPosition();
    void userSelectionCancelsPendingRestoreWithoutApplyingItsPosition();
    void explicitPlayOfTheRestoredLoadingOccurrenceCancelsItsPendingPosition();
    void explicitTransportCancelsRestoreAfterReadyBeforeSeekConfirmation_data();
    void explicitTransportCancelsRestoreAfterReadyBeforeSeekConfirmation();
    void restoredEndOfFileRemainsPausedWhenEndedArrives();
    void explicitPlayAfterRestoredEndOfFileAllowsANewEndedEventToAdvance();
    void restoreDisabledIgnoresAProvidedPersistedSession();
    void reenablingRestoreForcesTheCurrentSessionToReplaceStaleDiskState();
    void exitWaitsForBothPreferenceAndSessionPersistence();
    void pendingSessionExitFreezesLocalPlaybackAndMetadataUntilCancellation();
    void coalescesStructuralSessionMutationsIntoTheLatestSnapshot();
    void persistsOnlyConfirmedPositionsOnPeriodicAndUrgentCheckpoints();
    void systemSuspendStartsAnImmediateConfirmedCheckpointWithoutChangingPlayback();
    void systemSuspendQuiescesApplicationPollingUntilResume();
    void resumeDuringReversibleExitReactivatesPollingWhenTheExitFails();
    void systemResumeReconcilesUnreportedBackendStateAndDeduplicatesTransitions();
    void sessionSaveFailureUsesTheExistingWarningAndRetryAction();
    void sessionFutureBlockUsesTheExistingWarningAndReplacementAction();
    void initialSessionFutureVersionUsesTheExistingReplacementAction();
    void replacementConsentRejectsScopeExpansion_data();
    void replacementConsentRejectsScopeExpansion();
    void replacementConsentRejectsMissingAndUnknownRevision_data();
    void replacementConsentRejectsMissingAndUnknownRevision();
    void replacementConsentCannotBeReusedAfterWarningReturns();
    void replacementConsentRejectsReentrantActivation_data();
    void replacementConsentRejectsReentrantActivation();
    void replacementConsentDoesNotExpandDuringScheduling();
    void replacementConsentMayDestroyPresenterAtNotification();
    void replacementConsentStopsWhenSchedulingDestroysPresenter_data();
    void replacementConsentStopsWhenSchedulingDestroysPresenter();
    void initialSessionRecoveryWarningsIdentifySessionPersistence_data();
    void initialSessionRecoveryWarningsIdentifySessionPersistence();
    void mixedRecoveryWarningsRevealTheResidualWarning();
    void mixedFutureVersionAndDefaultsRevealTheResidualWarning_data();
    void mixedFutureVersionAndDefaultsRevealTheResidualWarning();
    void sessionOnlyExitTimeoutUsesTheExistingFailureWarning();
    void recordsWindowPlacementAsASilentPreferenceMutation();
    void flushesPendingWindowPlacementBeforeRequestingPreferenceSaveOnExit();
    void coalescesPreferenceChangesAndSavesTheFullSnapshotBeforeExit();
    void pendingPreferenceExitKeepsBackgroundTicksReversible();
    void preferenceSaveTimeoutKeepsTheApplicationResponsiveAndOpen();
    void preferencePollingSleepsAfterTerminalNoticeUntilUserAction_data();
    void preferencePollingSleepsAfterTerminalNoticeUntilUserAction();
    void projectsTypedPreferenceRecoveryWarningsAndActions();
    void initialPreferenceLoadWarningsRequireExplicitActions_data();
    void initialPreferenceLoadWarningsRequireExplicitActions();
    void exposesReadOnlyLifecycleDefaultsAndOnlyIntentMethods();
    void firstCloseWaitsForAcknowledgementAndDoesNotRepeatTheNotice();
    void unavailableTrayMinimizesAndKeepsTheExplanationUntilRestored();
    void cancellingTheFirstNoticeDoesNotConsumeIt();
    void noticeOptOutIsInMemoryAndDoesNotActOnThePendingWindow();
    void closeRefreshesAvailabilityBeforeDecidingTheWindowAction();
    void confirmationRechecksAvailabilityWithoutPublishingATransientNotice();
    void explicitHideIgnoresClosePreferenceAndBypassesTheNotice_data();
    void explicitHideIgnoresClosePreferenceAndBypassesTheNotice();
    void explicitHideClearsAPendingFirstNoticeWithoutAcknowledgingIt();
    void closeExitPreferenceOverridesNoticesAndTrayAvailability_data();
    void closeExitPreferenceOverridesNoticesAndTrayAvailability();
    void reportingTrayLossOnlyProjectsAnIdempotentExplanation();
    void lifecyclePreservesPlaybackQueueModelAndPendingBackendWork_data();
    void lifecyclePreservesPlaybackQueueModelAndPendingBackendWork();
    void exitPublishesItsGuardAndStopsTicksBeforeRequestingThePlatform_data();
    void exitPublishesItsGuardAndStopsTicksBeforeRequestingThePlatform();
};

void PlayerPresenterTests::fixtureOpenAudioPreservesTheFrozenModeContract() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);

    QVERIFY(presenter.usesFrozenFixture());
    QVERIFY(presenter.openAudio());

    QVERIFY(presenter.queueLoaded());
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QCOMPARE(presenter.currentTitle(), QStringLiteral("Canção nº 17 — São João, ação e café ☕"));
}

void PlayerPresenterTests::localOpenCancellationIsInert() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    int picker_calls = 0;
    PlayerPresenter presenter(platform, backend, metadata, [&]() -> std::vector<std::filesystem::path> {
        ++picker_calls;
        return {};
    });
    QSignalSpy states(&presenter, &PlayerPresenter::stateChanged);
    QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);
    QSignalSpy source_imports(&presenter, &PlayerPresenter::sourceImportChanged);

    QVERIFY(!presenter.usesFrozenFixture());
    QVERIFY(!presenter.openAudio());
    QVERIFY(!presenter.addAudio());

    QCOMPARE(picker_calls, 2);
    QVERIFY(backend.loaded_paths.empty());
    QVERIFY(metadata.requests.empty());
    QVERIFY(!presenter.hasCurrentTrack());
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Unloaded);
    QCOMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::Idle);
    QCOMPARE(presenter.sourceImportIssueCount(), 0ULL);
    QCOMPARE(states.size(), 0);
    QCOMPARE(playback.size(), 0);
    QCOMPARE(source_imports.size(), 0);
}

void PlayerPresenterTests::productDefaultsAndEmptyStateNeedNoFixture() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    FakeLocalSourceDiscovery discovery;
    const auto source = localTestPath(u8"Produto 日本語.wav");
    PlayerPresenter presenter(platform, backend, metadata, [source] { return std::vector{source}; }, discovery);

    QVERIFY(!presenter.usesFrozenFixture());
    QCOMPARE(presenter.preferences(), Preferences{});
    QCOMPARE(presenter.stateId(), QStringLiteral("empty"));
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Unloaded);
    QCOMPARE(presenter.assetId(), QStringLiteral("cover-placeholder"));
    QVERIFY(presenter.hasMessage());
    QCOMPARE(presenter.messageKey(), QStringLiteral("empty_title"));
    QCOMPARE(presenter.primaryActionId(), QStringLiteral("open-audio"));
    QCOMPARE(presenter.queueModelObject()->rowCount(), 0);
    QVERIFY(!presenter.queueLoaded());
    QVERIFY(!presenter.hasContextTrack());
    QVERIFY(!backend.volumes.empty());
    QVERIFY(!backend.muted_values.empty());
    QCOMPARE(backend.volumes.back(), Preferences{}.volume);
    QCOMPARE(backend.muted_values.back(), Preferences{}.muted);
    QVERIFY(backend.loaded_paths.empty());
    QVERIFY(metadata.requests.empty());
    QCOMPARE(backend.play_calls, 0);

    QVERIFY(presenter.openAudio());
    QCOMPARE(presenter.stateId(), QStringLiteral("playing"));
    QCOMPARE(presenter.assetId(), QStringLiteral("cover-track"));
    QVERIFY(!presenter.hasMessage());
    QVERIFY(presenter.messageKey().isEmpty());
    QCOMPARE(presenter.primaryActionId(), QStringLiteral("play-pause"));
    QCOMPARE(backend.loaded_paths, std::vector{source});
    QCOMPARE(metadata.requests.front().source_path, source);
    QCOMPARE(presenter.currentTitle(), QStringLiteral("Produto 日本語"));
}

void PlayerPresenterTests::productDefaultsRejectUnavailableAudioPreferences() {
    for (const bool reject_volume : {false, true}) {
        AvailableTrayPlatform platform;
        PresenterPlaybackBackend backend;
        PresenterMetadataReader metadata;
        if (reject_volume)
            backend.volume_result = PlaybackCommandResult::Shutdown;
        else
            backend.muted_result = PlaybackCommandResult::Shutdown;
        const auto construct = [&] {
            PlayerPresenter presenter(platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; });
        };
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, construct());
        QVERIFY(!backend.callbackInstalled());
    }
}

void PlayerPresenterTests::localOpenPublishesUnicodeFallbackAndStartsMetadata() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::filesystem::path source = localTestPath(u8"Abertura 日本語.wav");
    PlayerPresenter presenter(platform, backend, metadata, [source] { return std::vector{source}; });
    QSignalSpy states(&presenter, &PlayerPresenter::stateChanged);
    QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);

    QVERIFY(presenter.openAudio());

    QCOMPARE(backend.loaded_paths, std::vector<std::filesystem::path>{source});
    QCOMPARE(metadata.requests.size(), std::size_t{1});
    QCOMPARE(metadata.requests.front().source_path, source);
    QCOMPARE(metadata.requests.front().generation, MetadataGeneration{1});
    QVERIFY(presenter.hasCurrentTrack());
    QCOMPARE(presenter.currentTitle(), QStringLiteral("Abertura 日本語"));
    QCOMPARE(presenter.currentArtist(), QString{});
    QCOMPARE(presenter.currentAlbum(), QString{});
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
    QCOMPARE(presenter.queueLoaded(), true);
    QCOMPARE(presenter.canGoPrevious(), false);
    QCOMPARE(presenter.canGoNext(), false);
    QCOMPARE(states.size(), 1);
    QCOMPARE(playback.size(), 1);
    const QList<QTimer*> timers = presenter.findChildren<QTimer*>(QString{}, Qt::FindDirectChildrenOnly);
    QCOMPARE(timers.size(), 1);
    QVERIFY(timers.front()->interval() < 200);
    QVERIFY(timers.front()->isActive());
}

void PlayerPresenterTests::playlistBatchAppendsWithoutSelectionOrPlaybackAndCopiesTheQueue() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PlayerPresenter presenter(platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; });
    const auto a = localTestPath(u8"First 日本.mp3"), b = localTestPath(u8"Second.flac");
    QVERIFY(presenter.appendPlaylistSources({a, b, a}));
    const auto snapshot = presenter.playlistQueueSnapshot();
    QCOMPARE(snapshot.items.size(), std::size_t{3});
    QCOMPARE(snapshot.items[0].source_path, a);
    QCOMPARE(snapshot.items[1].source_path, b);
    QCOMPARE(snapshot.items[2].source_path, a);
    QVERIFY(snapshot.items[0].occurrence_id != snapshot.items[2].occurrence_id);
    QVERIFY(!snapshot.current_occurrence_id);
    QVERIFY(backend.loaded_paths.empty());
    QCOMPARE(backend.play_calls, 0);
    QVERIFY(metadata.requests.empty());
    QVERIFY(presenter.removeQueueOccurrence(LocalQueueListModel::occurrenceId(snapshot.items[1].occurrence_id)));
    QCOMPARE(snapshot.items.size(), std::size_t{3});
    QCOMPARE(presenter.playlistQueueSnapshot().items.size(), std::size_t{2});
}

void PlayerPresenterTests::acceptedLocalIngressIsReportedToTheLibraryObserver() {
    // ADR-0019: every accepted local ingress feeds the permanent library.
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    FakeLocalSourceDiscovery discovery;
    const auto a = localTestPath(u8"Aberta 日本.mp3"), b = localTestPath(u8"Da playlist.flac"),
               c = localTestPath(u8"Solta.wav");
    PlayerPresenter presenter(
        platform, backend, metadata, [a] { return std::vector<std::filesystem::path>{a}; }, discovery);
    std::vector<std::vector<std::filesystem::path>> admitted;
    presenter.setLocalAdmissionObserver([&](const std::vector<std::filesystem::path>& paths) {
        admitted.push_back(paths);
        throw std::runtime_error("observer failures never undo the queue change");
    });
    QVERIFY(presenter.openAudio());
    QVERIFY(presenter.addAudio());
    QVERIFY(presenter.appendPlaylistSources({b}));
    QVERIFY(!presenter.appendPlaylistSources({std::filesystem::path("relative.mp3")}));
    QVERIFY(presenter.openSourcePaths({c}, 1));
    const DiscoveryGeneration generation = discovery.requests.front().generation;
    discovery.publishBatch(generation, 1, {c}, {}, 1);
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QCOMPARE(admitted.size(), std::size_t{4});
    QCOMPARE(admitted[0], std::vector<std::filesystem::path>{a});
    QCOMPARE(admitted[1], std::vector<std::filesystem::path>{a});
    QCOMPARE(admitted[2], std::vector<std::filesystem::path>{b});
    QCOMPARE(admitted[3], std::vector<std::filesystem::path>{c});
    QCOMPARE(presenter.playlistQueueSnapshot().items.size(), std::size_t{4});
}

namespace {
[[nodiscard]] std::vector<std::filesystem::path> queuePaths(const PlayerPresenter& presenter) {
    std::vector<std::filesystem::path> paths;
    for (const auto& item : presenter.playlistQueueSnapshot().items)
        paths.push_back(item.source_path);
    return paths;
}
} // namespace

void PlayerPresenterTests::playSourcesNowInsertsAfterTheCurrentTrackAndStartsIt() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const auto a = localTestPath(u8"A.mp3"), b = localTestPath(u8"B.mp3"), c = localTestPath(u8"C.mp3"),
               x = localTestPath(u8"X.mp3");
    PlayerPresenter presenter(platform, backend, metadata, [=] { return std::vector<std::filesystem::path>{a, b, c}; });
    QAbstractItemModel* const model = presenter.queueModelObject();
    QVERIFY(presenter.openAudio());
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    QSignalSpy inserted(model, &QAbstractItemModel::rowsInserted);

    QVERIFY(presenter.playSourcesNow({x}));

    QCOMPARE(queuePaths(presenter), (std::vector<std::filesystem::path>{a, x, b, c}));
    QCOMPARE(backend.loaded_paths.back(), x);
    QCOMPARE(resets.size(), 0);
    QCOMPARE(inserted.size(), 1);
    QCOMPARE(inserted.front().at(1).toInt(), 1);
    QCOMPARE(inserted.front().at(2).toInt(), 1);
    QCOMPARE(presenter.currentQueueIndex(), 1);
    QCOMPARE(presenter.currentOccurrenceId(),
             model->data(model->index(1, 0), QueueListModel::OccurrenceIdRole).toString());
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 2});
    QTRY_COMPARE(backend.play_calls, 2);
}

void PlayerPresenterTests::playSourcesNowWithoutACurrentTrackAppendsAndStarts() {
    AvailableTrayPlatform platform;
    PresenterMetadataReader metadata;
    const auto a = localTestPath(u8"A.mp3"), b = localTestPath(u8"B.mp3"), x = localTestPath(u8"X.mp3");
    {
        PresenterPlaybackBackend backend;
        PlayerPresenter presenter(platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; });
        QVERIFY(!presenter.currentLocalSource().has_value());
        QVERIFY(presenter.playSourcesNow({x}));
        QCOMPARE(queuePaths(presenter), std::vector<std::filesystem::path>{x});
        QCOMPARE(backend.loaded_paths.back(), x);
        QCOMPARE(presenter.currentQueueIndex(), 0);
    }
    PresenterPlaybackBackend backend;
    PlayerPresenter presenter(platform, backend, metadata, [=] { return std::vector<std::filesystem::path>{a, b}; });
    QVERIFY(presenter.setQueueEndPreference(PlayerPresenter::QueueEndPreference::StopClearSelection));
    QVERIFY(presenter.openAudio());
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    QVERIFY(presenter.nextTrack());
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 2});
    backend.publishEvent(PlaybackEventType::Ended, {.status = PlaybackStatus::Paused,
                                                    .position_ms = 2'000,
                                                    .duration_ms = 2'000,
                                                    .seekable = true,
                                                    .generation = 2});
    QTRY_VERIFY(!presenter.hasCurrentOccurrence());
    const auto loads = backend.loaded_paths.size();

    QVERIFY(presenter.playSourcesNow({x}));

    QCOMPARE(queuePaths(presenter), (std::vector<std::filesystem::path>{a, b, x}));
    QCOMPARE(backend.loaded_paths.size(), loads + 1);
    QCOMPARE(backend.loaded_paths.back(), x);
    QCOMPARE(presenter.currentQueueIndex(), 2);
}

void PlayerPresenterTests::playSourcesNowInsertsAfterAFailedOrLoadingCurrentTrack() {
    AvailableTrayPlatform platform;
    PresenterMetadataReader metadata;
    const auto a = localTestPath(u8"A.mp3"), b = localTestPath(u8"B.mp3"), x = localTestPath(u8"X.mp3");
    for (const bool failed : {true, false}) {
        PresenterPlaybackBackend backend;
        PlayerPresenter presenter(platform, backend, metadata,
                                  [=] { return std::vector<std::filesystem::path>{a, b}; });
        if (failed) {
            backend.accepted_load_status = PlaybackStatus::Error;
            backend.accepted_load_error = PlaybackErrorCode::SourceUnavailable;
        }
        QVERIFY(presenter.openAudio());
        if (failed)
            QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Error);
        else
            QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
        backend.accepted_load_status.reset();

        QVERIFY(presenter.playSourcesNow({x}));

        QCOMPARE(queuePaths(presenter), (std::vector<std::filesystem::path>{a, x, b}));
        QCOMPARE(backend.loaded_paths.back(), x);
        QCOMPARE(presenter.currentQueueIndex(), 1);
    }
}

void PlayerPresenterTests::playSourcesNowWithShuffleStartsTheInsertedTrack() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const auto a = localTestPath(u8"A.mp3"), b = localTestPath(u8"B.mp3"), c = localTestPath(u8"C.mp3"),
               x = localTestPath(u8"X.mp3");
    PlayerPresenter presenter(platform, backend, metadata, [=] { return std::vector<std::filesystem::path>{a, b, c}; });
    QVERIFY(presenter.openAudio());
    QVERIFY(presenter.toggleShuffleQueue());
    QVERIFY(presenter.shuffleQueueEnabled());

    QVERIFY(presenter.playSourcesNow({x}));

    QCOMPARE(backend.loaded_paths.back(), x);
    QCOMPARE(presenter.currentLocalSource(), std::optional{x});
    QCOMPARE(queuePaths(presenter).size(), std::size_t{4});
    backend.publish({.status = PlaybackStatus::Ready,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = backend.snapshot().generation});
    QTRY_COMPARE(backend.play_calls, 1);
}

void PlayerPresenterTests::playSourcesNowMarksARejectedFileUnavailable() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const auto a = localTestPath(u8"A.mp3"), x = localTestPath(u8"X.mp3");
    PlayerPresenter presenter(platform, backend, metadata, [=] { return std::vector<std::filesystem::path>{a}; });
    QAbstractItemModel* const model = presenter.queueModelObject();
    QVERIFY(presenter.openAudio());
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    const auto before = presenter.currentOccurrenceId();
    backend.load_results = {PlaybackCommandResult::InvalidArgument};

    QVERIFY(!presenter.playSourcesNow({x}));

    QCOMPARE(queuePaths(presenter), (std::vector<std::filesystem::path>{a, x}));
    QVERIFY(!model->data(model->index(1, 0), QueueListModel::AvailableRole).toBool());
    QVERIFY(model->data(model->index(0, 0), QueueListModel::AvailableRole).toBool());
    QCOMPARE(presenter.currentOccurrenceId(), before);
    QCOMPARE(presenter.currentLocalSource(), std::optional{a});
    QCOMPARE(backend.play_calls, 1);
}

void PlayerPresenterTests::playSourcesNowRefusesInvalidInputAndGuestRooms() {
    AvailableTrayPlatform platform;
    PresenterMetadataReader metadata;
    const auto a = localTestPath(u8"A.mp3"), x = localTestPath(u8"X.mp3");
    {
        PresenterPlaybackBackend backend;
        PlayerPresenter presenter(platform, backend, metadata, [=] { return std::vector<std::filesystem::path>{a}; });
        QVERIFY(presenter.openAudio());
        const auto queue = queuePaths(presenter);
        const auto loads = backend.loaded_paths.size();
        QVERIFY(!presenter.playSourcesNow({}));
        QVERIFY(!presenter.playSourcesNow({std::filesystem::path{"relative.mp3"}}));
        QVERIFY(!presenter.playSourcesNow({x, std::filesystem::path{"relative.mp3"}}));
        presenter.setRoomSessionGate({.blocks_local_sources = [] { return true; }});
        QVERIFY(!presenter.playSourcesNow({x}));
        QCOMPARE(queuePaths(presenter), queue);
        QCOMPARE(backend.loaded_paths.size(), loads);
        QCOMPARE(backend.play_calls, 0);
    }
    {
        PresenterPlaybackBackend backend;
        PlayerPresenter presenter(platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; });
        QVERIFY(presenter.requestExit());
        QVERIFY(presenter.exiting());
        QVERIFY(!presenter.playSourcesNow({x}));
        QVERIFY(presenter.playlistQueueSnapshot().items.empty());
        QVERIFY(backend.loaded_paths.empty());
    }
    {
        PlayerPresenter presenter(frozenBundle(), platform);
        QVERIFY(presenter.usesFrozenFixture());
        QVERIFY(!presenter.playSourcesNow({x}));
        QVERIFY(!presenter.currentLocalSource().has_value());
    }
}

void PlayerPresenterTests::playSourcesNowFollowsTheRoomPlayOrderForTheHost() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector<std::filesystem::path> sources{localTestPath(u8"Sala A.wav"), localTestPath(u8"Sala B.wav"),
                                                     localTestPath(u8"Sala C.wav")};
    const auto x = localTestPath(u8"X.mp3");
    PlayerPresenter presenter(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(presenter.openAudio());
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    presenter.setRoomPlayOrder(true);

    QVERIFY(presenter.playSourcesNow({x}));

    QCOMPARE(queuePaths(presenter), (std::vector<std::filesystem::path>{sources[0], x, sources[1], sources[2]}));
    QCOMPARE(backend.loaded_paths.back(), x);
    QCOMPARE(presenter.currentQueueIndex(), 1);
}

void PlayerPresenterTests::playSourcesNowAdmitsTheFilesToTheLibrary() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const auto x = localTestPath(u8"X.mp3"), y = localTestPath(u8"Y.mp3");
    PlayerPresenter presenter(platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; });
    std::vector<std::vector<std::filesystem::path>> admitted;
    presenter.setLocalAdmissionObserver(
        [&](const std::vector<std::filesystem::path>& paths) { admitted.push_back(paths); });

    QVERIFY(presenter.playSourcesNow({x, y}));

    QCOMPARE(admitted.size(), std::size_t{1});
    QCOMPARE(admitted.front(), (std::vector<std::filesystem::path>{x, y}));
    QCOMPARE(queuePaths(presenter), (std::vector<std::filesystem::path>{x, y}));
    QCOMPARE(backend.loaded_paths.back(), x);
}

void PlayerPresenterTests::currentLocalSourceFollowsTheCurrentOccurrence() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const auto a = localTestPath(u8"A.mp3"), x = localTestPath(u8"X.mp3");
    PlayerPresenter presenter(platform, backend, metadata, [=] { return std::vector<std::filesystem::path>{a}; });
    QVERIFY(!presenter.currentLocalSource().has_value());
    QVERIFY(presenter.openAudio());
    QCOMPARE(presenter.currentLocalSource(), std::optional{a});
    QVERIFY(presenter.playSourcesNow({x}));
    QCOMPARE(presenter.currentLocalSource(), std::optional{x});

    PlayerPresenter fixture(frozenBundle(), platform);
    QVERIFY(!fixture.currentLocalSource().has_value());
}

void PlayerPresenterTests::playlistAppendPreservesActivePlaybackAndRejectsAnInvalidBatch() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const auto a = localTestPath(u8"Current.mp3"), b = localTestPath(u8"New.flac");
    PlayerPresenter presenter(platform, backend, metadata, [a] { return std::vector<std::filesystem::path>{a}; });
    QVERIFY(presenter.openAudio());
    const auto current = presenter.currentOccurrenceId();
    const auto state = presenter.playbackState();
    const auto loads = backend.loaded_paths.size();
    QVERIFY(presenter.appendPlaylistSources({b, b}));
    QCOMPARE(presenter.currentOccurrenceId(), current);
    QCOMPARE(presenter.playbackState(), state);
    QCOMPARE(backend.loaded_paths.size(), loads);
    const auto snapshot = presenter.playlistQueueSnapshot();
    QVERIFY(!presenter.appendPlaylistSources({b, std::filesystem::path("relative.mp3")}));
    QVERIFY(!presenter.appendPlaylistSources({}));
    QVERIFY(presenter.playlistQueueSnapshot() == snapshot);
}

void PlayerPresenterTests::playlistAppendAcknowledgesItsCommitWhenALaterObserverThrows() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PlayerPresenter presenter(platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; });
    const auto path = localTestPath(u8"Committed.mp3");
    const auto connection = QObject::connect(
        &presenter, &PlayerPresenter::stateChanged, &presenter,
        [] { throw std::runtime_error("observer failed after commit"); }, Qt::DirectConnection);
    bool accepted{}, threw{};
    try {
        accepted = presenter.appendPlaylistSources({path, path});
    } catch (...) {
        threw = true;
    }
    QObject::disconnect(connection);
    QVERIFY(!threw);
    QVERIFY(accepted);
    QCOMPARE(presenter.playlistQueueSnapshot().items.size(), std::size_t{2});
    QVERIFY(backend.loaded_paths.empty());
    QCOMPARE(backend.play_calls, 0);
    QVERIFY(presenter.appendPlaylistSources({path}));
    QCOMPARE(presenter.playlistQueueSnapshot().items.size(), std::size_t{3});
}

void PlayerPresenterTests::localOpenPublishesOrderedQueueWithDistinctDuplicateOccurrences() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector<std::filesystem::path> selections{
        localTestPath(u8"Primeira 日本語.mp3"),
        localTestPath(u8"Segunda ☕.m4a"),
        localTestPath(u8"Primeira 日本語.mp3"),
    };
    PlayerPresenter presenter(platform, backend, metadata, [selections] { return selections; });
    QAbstractItemModel* const model = presenter.queueModelObject();
    QAbstractItemModelTester model_tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);

    QVERIFY(presenter.openAudio());

    QCOMPARE(backend.loaded_paths, std::vector<std::filesystem::path>{selections.front()});
    QVERIFY(presenter.queueLoaded());
    QVERIFY(presenter.hasCurrentOccurrence());
    QCOMPARE(presenter.currentQueueIndex(), 0);
    QCOMPARE(model, presenter.queueModelObject());
    QCOMPARE(model->rowCount(), 3);
    // The local model adds only the cover role over the fixture's.
    auto local_roles = model->roleNames();
    QVERIFY(local_roles.values().contains("artworkSource"));
    local_roles.remove(LocalQueueListModel::ArtworkSourceRole);
    QCOMPARE(local_roles, presenter.queueModel()->roleNames());
    QCOMPARE(resets.size(), 1);
    QCOMPARE(model->data(model->index(0, 0), QueueListModel::FileNameRole).toString(),
             QStringLiteral("Primeira 日本語.mp3"));
    QCOMPARE(model->data(model->index(1, 0), QueueListModel::TitleRole).toString(), QStringLiteral("Segunda ☕"));
    QCOMPARE(model->data(model->index(2, 0), QueueListModel::FileNameRole).toString(),
             QStringLiteral("Primeira 日本語.mp3"));
    const QString first_id = model->data(model->index(0, 0), QueueListModel::OccurrenceIdRole).toString();
    const QString duplicate_id = model->data(model->index(2, 0), QueueListModel::OccurrenceIdRole).toString();
    QVERIFY(!first_id.isEmpty());
    QVERIFY(first_id != duplicate_id);
    QCOMPARE(presenter.currentOccurrenceId(), first_id);
}

void PlayerPresenterTests::openingAdditionalFilesPreservesTheQueueAndPlaysTheFirstNewOccurrence() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::filesystem::path first = localTestPath(u8"Primeira.mp3");
    const std::filesystem::path queued = localTestPath(u8"Na fila.flac");
    const std::filesystem::path newly_opened = localTestPath(u8"Nova 日本語.ogg");
    const std::vector<std::vector<std::filesystem::path>> selections{
        {first, queued},
        {newly_opened, first},
    };
    std::size_t selection_index = 0;
    PlayerPresenter presenter(platform, backend, metadata, [&] { return selections.at(selection_index++); });
    QAbstractItemModel* const model = presenter.queueModelObject();
    QVERIFY(presenter.openAudio());
    const QString first_id = model->data(model->index(0, 0), QueueListModel::OccurrenceIdRole).toString();
    const QPersistentModelIndex queued_index{model->index(1, 0)};
    const QString queued_id = queued_index.data(QueueListModel::OccurrenceIdRole).toString();
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    QSignalSpy inserted(model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(model, &QAbstractItemModel::rowsRemoved);

    QVERIFY(presenter.openAudio());

    QCOMPARE(model->rowCount(), 4);
    QCOMPARE(resets.size(), 0);
    QCOMPARE(inserted.size(), 1);
    QCOMPARE(model->data(model->index(0, 0), QueueListModel::OccurrenceIdRole).toString(), first_id);
    QCOMPARE(model->data(model->index(1, 0), QueueListModel::OccurrenceIdRole).toString(), queued_id);
    QVERIFY(queued_index.isValid());
    QCOMPARE(queued_index.row(), 1);
    QCOMPARE(presenter.currentQueueIndex(), 2);
    const QString newly_opened_id = model->data(model->index(2, 0), QueueListModel::OccurrenceIdRole).toString();
    QCOMPARE(presenter.currentOccurrenceId(), newly_opened_id);
    QCOMPARE(model->data(model->index(2, 0), QueueListModel::FileNameRole).toString(),
             QStringLiteral("Nova 日本語.ogg"));
    QCOMPARE(model->data(model->index(3, 0), QueueListModel::FileNameRole).toString(), QStringLiteral("Primeira.mp3"));
    QVERIFY(model->data(model->index(3, 0), QueueListModel::OccurrenceIdRole).toString() != first_id);
    QCOMPARE(backend.loaded_paths, std::vector<std::filesystem::path>({first, newly_opened}));
    QCOMPARE(metadata.requests.size(), std::size_t{2});
    QCOMPARE(metadata.requests.back().source_path, newly_opened);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
    QCOMPARE(removed.size(), 0);

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 2});
    QTRY_COMPARE(backend.play_calls, 1);
}

void PlayerPresenterTests::localQueueNavigationAvoidsAutoplayAndPreservesDuplicateOccurrence() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::filesystem::path duplicate = localTestPath(u8"Duplicada 日本語.mp3");
    const std::filesystem::path final_source = localTestPath(u8"Final ☕.m4a");
    const std::vector<std::filesystem::path> selections{duplicate, duplicate, final_source};
    PlayerPresenter presenter(platform, backend, metadata, [selections] { return selections; });
    QAbstractItemModel* const model = presenter.queueModelObject();
    QVERIFY(presenter.openAudio());
    const QString first_id = presenter.currentOccurrenceId();
    const QString second_id = model->data(model->index(1, 0), QueueListModel::OccurrenceIdRole).toString();
    QVERIFY(first_id != second_id);
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    backend.publish({.status = PlaybackStatus::Paused,
                     .position_ms = 500,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);

    QVERIFY(presenter.nextTrack());
    const std::vector<std::filesystem::path> expected_loaded_paths{duplicate, duplicate};
    QCOMPARE(backend.loaded_paths, expected_loaded_paths);
    QCOMPARE(presenter.currentQueueIndex(), 1);
    QCOMPARE(presenter.currentOccurrenceId(), second_id);
    QCOMPARE(resets.size(), 0);

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 2});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Ready);
    QCOMPARE(backend.play_calls, 1);
    QCOMPARE(backend.pause_calls, 0);

    QVERIFY(presenter.previousTrack());
    QCOMPARE(presenter.currentQueueIndex(), 0);
    QCOMPARE(presenter.currentOccurrenceId(), first_id);
    QCOMPARE(resets.size(), 0);
}

void PlayerPresenterTests::playNowUsesTheStableLocalOccurrenceIdAndAutoplays() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::filesystem::path duplicate = localTestPath(u8"Duplicada 日本語.mp3");
    const std::vector<std::filesystem::path> selections{duplicate, duplicate};
    PlayerPresenter presenter(platform, backend, metadata, [selections] { return selections; });
    QAbstractItemModel* const model = presenter.queueModelObject();
    QVERIFY(presenter.openAudio());
    const QString first_id = presenter.currentOccurrenceId();
    const QPersistentModelIndex second_index{model->index(1, 0)};
    const QString second_id = second_index.data(QueueListModel::OccurrenceIdRole).toString();
    QVERIFY(first_id != second_id);
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    backend.publish({.status = PlaybackStatus::Paused,
                     .position_ms = 500,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);

    QVERIFY(!presenter.playQueueOccurrence(QStringLiteral("local-999999")));
    QVERIFY(presenter.playQueueOccurrence(second_id));

    QCOMPARE(presenter.currentQueueIndex(), 1);
    QCOMPARE(presenter.currentOccurrenceId(), second_id);
    QCOMPARE(backend.loaded_paths, selections);
    QCOMPARE(resets.size(), 0);
    QVERIFY(second_index.isValid());
    QCOMPARE(second_index.data(QueueListModel::OccurrenceIdRole).toString(), second_id);

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 2});
    QTRY_COMPARE(backend.play_calls, 2);
    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 100,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = 2});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QVERIFY(presenter.playQueueOccurrence(second_id));
    QCOMPARE(backend.loaded_paths, selections);
    QCOMPARE(backend.play_calls, 2);
}

void PlayerPresenterTests::playNowPromotesTheCurrentPendingLoadWithoutReloading() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector<std::filesystem::path> selections{
        localTestPath(u8"Primeira.mp3"),
        localTestPath(u8"Segunda.mp3"),
    };
    PlayerPresenter presenter(platform, backend, metadata, [selections] { return selections; });
    QAbstractItemModel* const model = presenter.queueModelObject();
    QVERIFY(presenter.openAudio());
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    backend.publish({.status = PlaybackStatus::Paused,
                     .position_ms = 500,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QVERIFY(presenter.nextTrack());
    const QString second_id = model->data(model->index(1, 0), QueueListModel::OccurrenceIdRole).toString();
    QCOMPARE(presenter.currentOccurrenceId(), second_id);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);

    QVERIFY(presenter.playQueueOccurrence(second_id));
    QCOMPARE(backend.loaded_paths, selections);
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 2});

    QTRY_COMPARE(backend.play_calls, 2);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Ready);
}

void PlayerPresenterTests::localQueueAppendMoveAndNonCurrentRemovalUseFineGrainedChanges() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::filesystem::path first = localTestPath(u8"Primeira 日本語.mp3");
    const std::filesystem::path second = localTestPath(u8"Segunda.flac");
    const std::filesystem::path final_source = localTestPath(u8"Final.ogg");
    const std::vector<std::vector<std::filesystem::path>> selections{{first, second}, {first, final_source}};
    std::size_t selection_index = 0;
    PlayerPresenter presenter(platform, backend, metadata, [&] { return selections.at(selection_index++); });
    QAbstractItemModel* const model = presenter.queueModelObject();
    QVERIFY(presenter.openAudio());
    const QString current_id = presenter.currentOccurrenceId();
    const QPersistentModelIndex second_index{model->index(1, 0)};
    const QString second_id = second_index.data(QueueListModel::OccurrenceIdRole).toString();
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 250,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    QSignalSpy inserted(model, &QAbstractItemModel::rowsInserted);
    QSignalSpy moved(model, &QAbstractItemModel::rowsMoved);
    QSignalSpy removed(model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy focus_requested(&presenter, &PlayerPresenter::queueFocusRequested);

    QVERIFY(presenter.addAudio());

    QCOMPARE(model->rowCount(), 4);
    QCOMPARE(inserted.size(), 1);
    QCOMPARE(resets.size(), 0);
    QCOMPARE(presenter.currentOccurrenceId(), current_id);
    QCOMPARE(presenter.currentQueueIndex(), 0);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QCOMPARE(backend.loaded_paths, std::vector<std::filesystem::path>{first});
    QCOMPARE(metadata.requests.size(), std::size_t{1});
    const QString appended_duplicate_id = model->data(model->index(2, 0), QueueListModel::OccurrenceIdRole).toString();
    const QString final_id = model->data(model->index(3, 0), QueueListModel::OccurrenceIdRole).toString();
    QVERIFY(appended_duplicate_id != current_id);
    QCOMPARE(focus_requested.size(), 0);

    QVERIFY(presenter.moveQueueOccurrence(appended_duplicate_id, 0));
    QCOMPARE(moved.size(), 1);
    QCOMPARE(presenter.currentOccurrenceId(), current_id);
    QCOMPARE(presenter.currentQueueIndex(), 1);
    QVERIFY(second_index.isValid());
    QCOMPARE(second_index.row(), 2);
    QCOMPARE(second_index.data(QueueListModel::OccurrenceIdRole).toString(), second_id);
    QCOMPARE(focus_requested.last().at(0).toInt(), 0);

    QVERIFY(presenter.removeQueueOccurrence(final_id));
    QCOMPARE(removed.size(), 1);
    QCOMPARE(presenter.currentOccurrenceId(), current_id);
    QCOMPARE(presenter.currentQueueIndex(), 1);
    QCOMPARE(backend.loaded_paths, std::vector<std::filesystem::path>{first});
    QCOMPARE(focus_requested.last().at(0).toInt(), 2);

    QVERIFY(presenter.removeQueueOccurrence(appended_duplicate_id));
    QCOMPARE(removed.size(), 2);
    QCOMPARE(presenter.currentOccurrenceId(), current_id);
    QCOMPARE(presenter.currentQueueIndex(), 0);
    QVERIFY(second_index.isValid());
    QCOMPARE(second_index.row(), 1);
    QCOMPARE(focus_requested.last().at(0).toInt(), 0);

    QVERIFY(presenter.moveQueueOccurrence(current_id, 1));
    QCOMPARE(moved.size(), 2);
    QCOMPARE(presenter.currentOccurrenceId(), current_id);
    QCOMPARE(presenter.currentQueueIndex(), 1);
    QCOMPARE(second_index.row(), 0);
    QCOMPARE(focus_requested.last().at(0).toInt(), 1);
    QCOMPARE(backend.loaded_paths, std::vector<std::filesystem::path>{first});
    QCOMPARE(backend.play_calls, 1);
    QVERIFY(!presenter.moveQueueOccurrence(current_id, 1));
    QVERIFY(!presenter.moveQueueOccurrence(QStringLiteral("local-999999"), 0));
    QVERIFY(!presenter.moveQueueOccurrence(current_id, -1));
    QVERIFY(!presenter.moveQueueOccurrence(current_id, 2));
    QCOMPARE(resets.size(), 0);
}

void PlayerPresenterTests::shuffleQueuePreservesCurrentPrefixAndMetadata_data() {
    QTest::addColumn<int>("status");
    QTest::newRow("ready") << static_cast<int>(PlaybackStatus::Ready);
    QTest::newRow("paused") << static_cast<int>(PlaybackStatus::Paused);
    QTest::newRow("playing") << static_cast<int>(PlaybackStatus::Playing);
}

void PlayerPresenterTests::shuffleQueuePreservesCurrentPrefixAndMetadata() {
    QFETCH(int, status);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const auto duplicate = localTestPath(u8"Duplicada 日本語.mp3");
    const std::vector sources{localTestPath(u8"Histórico.wav"), localTestPath(u8"Atual.wav"), duplicate, duplicate,
                              localTestPath(u8"Última.wav")};
    PlayerPresenter presenter(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(presenter.openAudio());
    auto* const model = qobject_cast<LocalQueueListModel*>(presenter.queueModelObject());
    QVERIFY(model);
    const auto before = queueOccurrenceIds(*model);
    QVERIFY(presenter.playQueueOccurrence(before[1]));
    const auto generation = backend.snapshot().generation;
    backend.publish({.status = static_cast<PlaybackStatus>(status),
                     .position_ms = 600,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = generation});
    QTRY_COMPARE(presenter.confirmedPositionMs(), 600);
    QVERIFY(model->updateMetadata({3}, QStringLiteral("Título preservado"), QStringLiteral("Artista"),
                                  QStringLiteral("Álbum"), 8'000));
    const QPersistentModelIndex duplicate_index{model->index(2, 0)};
    const auto playback = backend.snapshot();
    const auto paths = backend.loaded_paths;
    const auto reads = metadata.requests.size();
    const auto plays = backend.play_calls;
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    QSignalSpy focus(&presenter, &PlayerPresenter::queueFocusRequested);
    presenter.setQueueShuffleSeedForTest(19);

    QVERIFY(presenter.shuffleQueue());

    const auto after = queueOccurrenceIds(*model);
    QCOMPARE(after.mid(0, 2), before.mid(0, 2));
    QVERIFY(after.mid(2) != before.mid(2));
    auto before_suffix = before.mid(2);
    auto after_suffix = after.mid(2);
    before_suffix.sort();
    after_suffix.sort();
    QCOMPARE(after_suffix, before_suffix);
    QCOMPARE(presenter.currentOccurrenceId(), before[1]);
    QCOMPARE(presenter.currentQueueIndex(), 1);
    QCOMPARE(backend.snapshot(), playback);
    QCOMPARE(backend.loaded_paths, paths);
    QCOMPARE(metadata.requests.size(), reads);
    QCOMPARE(backend.play_calls, plays);
    QCOMPARE(backend.pause_calls, 0);
    QVERIFY(backend.seek_positions.empty());
    QVERIFY(duplicate_index.isValid());
    QCOMPARE(duplicate_index.data(LocalQueueListModel::OccurrenceIdRole).toString(), before[2]);
    QCOMPARE(duplicate_index.data(LocalQueueListModel::TitleRole).toString(), QStringLiteral("Título preservado"));
    QCOMPARE(duplicate_index.data(LocalQueueListModel::ArtistRole).toString(), QStringLiteral("Artista"));
    QCOMPARE(duplicate_index.data(LocalQueueListModel::AlbumRole).toString(), QStringLiteral("Álbum"));
    QCOMPARE(duplicate_index.data(LocalQueueListModel::DurationMsRole).toLongLong(), 8'000);
    QCOMPARE(resets.size(), 0);
    QCOMPARE(focus.size(), 0);
}

void PlayerPresenterTests::shuffleQueueChangesTwoUpcomingItemsForEveryTestSeed() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector sources{localTestPath(u8"A.wav"), localTestPath(u8"B.wav"), localTestPath(u8"C.wav")};
    PlayerPresenter presenter(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(!presenter.shuffleQueue());
    QVERIFY(presenter.openAudio());
    auto* const model = presenter.queueModelObject();
    for (std::uint32_t seed = 0; seed < 8; ++seed) {
        presenter.setQueueShuffleSeedForTest(seed);
        const auto before = queueOccurrenceIds(*model);
        QVERIFY(presenter.shuffleQueue());
        QCOMPARE(queueOccurrenceIds(*model), (QStringList{before[0], before[2], before[1]}));
    }
    QVERIFY(presenter.playQueueOccurrence(queueOccurrenceIds(*model)[1]));
    QVERIFY(!presenter.shuffleQueue());
    QVERIFY(presenter.playQueueOccurrence(queueOccurrenceIds(*model)[2]));
    QVERIFY(!presenter.shuffleQueue());
    QVERIFY(presenter.requestExit());
    QVERIFY(!presenter.shuffleQueue());
}

namespace {
using QueueArtworkEntries = std::vector<std::pair<std::uint64_t, std::filesystem::path>>;
std::uint64_t occurrenceValue(const QString& occurrence_id) {
    return occurrence_id.mid(QStringLiteral("local-").size()).toULongLong();
}
} // namespace

void PlayerPresenterTests::queueArtworkSinkReceivesTheAvailableOccurrences() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector<std::filesystem::path> selections{localTestPath(u8"A.mp3"), localTestPath(u8"B.mp3"),
                                                        localTestPath(u8"A.mp3")};
    PlayerPresenter presenter(platform, backend, metadata, [selections] { return selections; });
    std::vector<QueueArtworkEntries> deliveries;
    presenter.setQueueArtwork([&](QueueArtworkEntries entries) { deliveries.push_back(std::move(entries)); },
                              QStringLiteral("image://queueart/"));
    QVERIFY(!deliveries.empty());
    QVERIFY(deliveries.back().empty());
    QVERIFY(presenter.openAudio());
    auto* model = presenter.queueModelObject();
    QCOMPARE(model->rowCount(), 3);
    QueueArtworkEntries expected;
    for (int row = 0; row < 3; ++row)
        expected.emplace_back(occurrenceValue(model->index(row, 0).data(QueueListModel::OccurrenceIdRole).toString()),
                              selections[row]);
    QCOMPARE(deliveries.back(), expected);
    const QString removed = model->index(1, 0).data(QueueListModel::OccurrenceIdRole).toString();
    QVERIFY(presenter.removeQueueOccurrence(removed));
    expected.erase(expected.begin() + 1);
    QCOMPARE(deliveries.back(), expected);
    // An empty sink is accepted.
    presenter.setQueueArtwork({}, {});
    QVERIFY(presenter.removeQueueOccurrence(model->index(0, 0).data(QueueListModel::OccurrenceIdRole).toString()));
}

void PlayerPresenterTests::queueArtworkFollowsAvailabilityAndRelocation() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterFileLocator locator;
    const auto source = localTestPath(u8"Duplicada.wav");
    const auto replacement = std::filesystem::absolute(localTestPath(u8"Outra pasta/Duplicada.wav"));
    PlayerPresenter presenter(platform, backend, metadata, [source] { return std::vector{source, source, source}; });
    presenter.setAudioFileLocator(&locator);
    QueueArtworkEntries last;
    presenter.setQueueArtwork([&](QueueArtworkEntries entries) { last = std::move(entries); },
                              QStringLiteral("image://queueart/"));
    QVERIFY(presenter.openAudio());
    const QString missing = presenter.currentOccurrenceId();
    const auto missing_value = occurrenceValue(missing);
    QCOMPARE(last.size(), std::size_t{3});
    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::SourceUnavailable});
    QTRY_COMPARE(presenter.currentQueueIndex(), 1);
    auto* model = presenter.queueModelObject();
    const auto row = model->index(0, 0);
    QVERIFY(!row.data(QueueListModel::AvailableRole).toBool());
    QVERIFY(row.data(LocalQueueListModel::ArtworkSourceRole).toString().isEmpty());
    QCOMPARE(last.size(), std::size_t{2});
    for (const auto& entry : last)
        QVERIFY(entry.first != missing_value);
    QVERIFY(presenter.locateQueueOccurrence(missing));
    locator.complete({AudioFileLocationStatus::Selected, replacement});
    QTRY_VERIFY(presenter.locatingOccurrenceId().isEmpty());
    QVERIFY(presenter.retryQueueOccurrence(missing));
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 3});
    QTRY_VERIFY(row.data(QueueListModel::AvailableRole).toBool());
    QCOMPARE(row.data(LocalQueueListModel::ArtworkSourceRole).toString(),
             QStringLiteral("image://queueart/%1").arg(missing_value));
    QCOMPARE(last.size(), std::size_t{3});
    const auto found = std::ranges::find(last, missing_value, &QueueArtworkEntries::value_type::first);
    QVERIFY(found != last.end());
    QCOMPARE(found->second, replacement);
}

void PlayerPresenterTests::queueModelExposesArtworkSourcePerOccurrence() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector<std::filesystem::path> selections{localTestPath(u8"A.mp3"), localTestPath(u8"B.mp3")};
    PlayerPresenter presenter(platform, backend, metadata, [selections] { return selections; });
    QVERIFY(presenter.openAudio());
    auto* model = presenter.queueModelObject();
    QVERIFY(model->roleNames().values().contains("artworkSource"));
    QVERIFY(model->index(0, 0).data(LocalQueueListModel::ArtworkSourceRole).toString().isEmpty());
    presenter.setQueueArtwork({}, QStringLiteral("image://queueart/"));
    const auto value = occurrenceValue(model->index(0, 0).data(QueueListModel::OccurrenceIdRole).toString());
    QCOMPARE(model->index(0, 0).data(LocalQueueListModel::ArtworkSourceRole).toString(),
             QStringLiteral("image://queueart/") + QString::number(value));
    presenter.setQueueArtwork({}, {});
    QVERIFY(model->index(0, 0).data(LocalQueueListModel::ArtworkSourceRole).toString().isEmpty());
}

void PlayerPresenterTests::queueTotalDurationSumsKnownDurations() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector sources{localTestPath(u8"A.wav"), localTestPath(u8"B.wav")};
    PlayerPresenter presenter(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(presenter.openAudio());
    auto* const model = qobject_cast<LocalQueueListModel*>(presenter.queueModelObject());
    QVERIFY(model);
    QVERIFY(model->updateMetadata({1}, QStringLiteral("A"), {}, {}, 90'000));
    QVERIFY(model->updateMetadata({2}, QStringLiteral("B"), {}, {}, 120'000));

    const QVariant total = presenter.queueTotalDurationMs();

    QVERIFY(total.isValid());
    QCOMPARE(total.toLongLong(), 210'000);
}

void PlayerPresenterTests::queueTotalDurationIsUnknownWhenAnyTrackHasNoDuration() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector sources{localTestPath(u8"A.wav"), localTestPath(u8"B.wav")};
    PlayerPresenter presenter(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(presenter.openAudio());
    auto* const model = qobject_cast<LocalQueueListModel*>(presenter.queueModelObject());
    QVERIFY(model);
    QVERIFY(model->updateMetadata({1}, QStringLiteral("A"), {}, {}, 90'000));

    QVERIFY(!presenter.queueTotalDurationMs().isValid());
}

void PlayerPresenterTests::queueTotalDurationIsUnknownForAnEmptyQueue() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PlayerPresenter presenter(platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; });

    QVERIFY(!presenter.queueTotalDurationMs().isValid());
}

void PlayerPresenterTests::nextQueueTitleNamesTheFollowingOccurrence() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector sources{localTestPath(u8"Primeira.wav"), localTestPath(u8"Segunda.wav"),
                              localTestPath(u8"Terceira.wav")};
    PlayerPresenter presenter(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(presenter.nextQueueTitle().isEmpty());
    QVERIFY(presenter.openAudio());
    QCOMPARE(presenter.currentQueueIndex(), 0);
    QCOMPARE(presenter.nextQueueTitle(), QStringLiteral("Segunda"));
    auto* const model = qobject_cast<LocalQueueListModel*>(presenter.queueModelObject());
    QVERIFY(model);
    QVERIFY(model->updateMetadata({2}, QStringLiteral("Título da segunda"), {}, {}, {}));
    QCOMPARE(presenter.nextQueueTitle(), QStringLiteral("Título da segunda"));
    QVERIFY(presenter.playQueueOccurrence(queueOccurrenceIds(*model)[1]));
    QCOMPARE(presenter.currentQueueIndex(), 1);
    QCOMPARE(presenter.nextQueueTitle(), QStringLiteral("Terceira"));
}

void PlayerPresenterTests::nextQueueTitleSkipsUnavailableOccurrences() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector sources{localTestPath(u8"A.wav"), localTestPath(u8"B.wav"), localTestPath(u8"C.wav")};
    PlayerPresenter presenter(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(presenter.openAudio());
    auto* const model = presenter.queueModelObject();
    const auto ids = queueOccurrenceIds(*model);

    // B fails to load, so playback moves on to C: from A, the next track to play is C, not B.
    QVERIFY(presenter.playQueueOccurrence(ids[1]));
    backend.publish({.status = PlaybackStatus::Error,
                     .generation = backend.snapshot().generation,
                     .error = PlaybackErrorCode::SourceUnavailable});
    QTRY_COMPARE(presenter.currentOccurrenceId(), ids[2]);
    QVERIFY(!model->data(model->index(1, 0), LocalQueueListModel::AvailableRole).toBool());
    QVERIFY(presenter.playQueueOccurrence(ids[0]));
    QCOMPARE(presenter.currentQueueIndex(), 0);
    QCOMPARE(presenter.nextQueueTitle(), QStringLiteral("C"));

    // C fails too: nothing after A will play, so no title is promised.
    QVERIFY(presenter.playQueueOccurrence(ids[2]));
    backend.publish({.status = PlaybackStatus::Error,
                     .generation = backend.snapshot().generation,
                     .error = PlaybackErrorCode::SourceUnavailable});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Error);
    QVERIFY(!model->data(model->index(2, 0), LocalQueueListModel::AvailableRole).toBool());
    QVERIFY(presenter.playQueueOccurrence(ids[0]));
    QCOMPARE(presenter.currentQueueIndex(), 0);
    QVERIFY(presenter.nextQueueTitle().isEmpty());
}

void PlayerPresenterTests::nextQueueTitleIsEmptyWithShuffleOrAtTheEnd() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterPreferencesWriter writer;
    PreferencesAutosave autosave{writer, {}};
    const std::vector sources{localTestPath(u8"A.wav"), localTestPath(u8"B.wav"), localTestPath(u8"C.wav")};
    PlayerPresenter presenter(
        platform, backend, metadata, [sources] { return sources; }, PlayerPresenterPreferences{.autosave = &autosave});
    QVERIFY(presenter.openAudio());
    QCOMPARE(presenter.nextQueueTitle(), QStringLiteral("B"));
    const auto property = presenter.metaObject()->property(presenter.metaObject()->indexOfProperty("nextQueueTitle"));
    QVERIFY(property.isValid());
    QVERIFY(property.hasNotifySignal());
    QSignalSpy notified(&presenter, property.notifySignal());

    // Shuffle decides the next occurrence later, so no title is promised.
    QVERIFY(presenter.toggleShuffleQueue());
    QVERIFY(presenter.shuffleQueueEnabled());
    QVERIFY(presenter.nextQueueTitle().isEmpty());
    // Turning shuffle off keeps the order and only changes the preference: the title must still be notified.
    notified.clear();
    QVERIFY(presenter.toggleShuffleQueue());
    QVERIFY(!presenter.shuffleQueueEnabled());
    QVERIFY(!notified.isEmpty());
    QVERIFY(!presenter.nextQueueTitle().isEmpty());

    const auto ids = queueOccurrenceIds(*presenter.queueModelObject());
    QVERIFY(presenter.playQueueOccurrence(ids.back()));
    QCOMPARE(presenter.currentQueueIndex(), 2);
    QVERIFY(presenter.nextQueueTitle().isEmpty());
    QVERIFY(presenter.toggleRepeatQueue());
    QVERIFY(presenter.repeatQueueEnabled());
    QVERIFY(presenter.nextQueueTitle().isEmpty());

    QVERIFY(presenter.clearQueue());
    QVERIFY(!presenter.queueLoaded());
    QVERIFY(presenter.nextQueueTitle().isEmpty());
}

void PlayerPresenterTests::shuffleQueueWithNoCurrentShufflesAllRemainingOccurrences() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector sources{localTestPath(u8"A.wav"), localTestPath(u8"B.wav"), localTestPath(u8"C.wav")};
    PlayerPresenter presenter(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(presenter.openAudio());
    auto* const model = presenter.queueModelObject();
    const auto ids = queueOccurrenceIds(*model);
    QVERIFY(presenter.playQueueOccurrence(ids[2]));
    QVERIFY(presenter.removeQueueOccurrence(ids[2]));
    QVERIFY(!presenter.hasCurrentOccurrence());
    const auto loads = backend.loaded_paths.size();
    presenter.setQueueShuffleSeedForTest(2);
    QVERIFY(presenter.shuffleQueue());
    QCOMPARE(queueOccurrenceIds(*model), (QStringList{ids[1], ids[0]}));
    QVERIFY(!presenter.hasCurrentOccurrence());
    QCOMPARE(backend.loaded_paths.size(), loads);
}

void PlayerPresenterTests::repeatQueueTogglePublishesAndPersistsWithoutTransport() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterPreferencesClock clock;
    PresenterPreferencesWriter writer;
    PreferencesAutosave autosave{writer, {}, {.now = [&clock] { return clock.now(); }}};
    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
        PlayerPresenterPreferences{.autosave = &autosave, .now = [&clock] { return clock.now(); }});
    QVERIFY(!presenter.repeatQueueEnabled());
    const auto property =
        presenter.metaObject()->property(presenter.metaObject()->indexOfProperty("repeatQueueEnabled"));
    QVERIFY(property.isValid());
    QCOMPARE(property.notifySignal().name(), QByteArray{"preferencesChanged"});
    QSignalSpy changes(&presenter, &PlayerPresenter::preferencesChanged);
    QVERIFY(presenter.toggleRepeatQueue());
    QVERIFY(presenter.repeatQueueEnabled());
    QCOMPARE(changes.size(), 1);
    QCOMPARE(autosave.flush(), PreferencesSaveStatus::Saved);
    QVERIFY(!writer.flushes.empty());
    QVERIFY(writer.flushes.back().preferences.repeat_queue_enabled);
    QVERIFY(presenter.toggleRepeatQueue());
    QVERIFY(!presenter.repeatQueueEnabled());
    QCOMPARE(changes.size(), 2);
    QVERIFY(backend.loaded_paths.empty());
    QCOMPARE(backend.play_calls, 0);
    QCOMPARE(backend.pause_calls, 0);
    QVERIFY(presenter.requestExit());
    QVERIFY(!presenter.toggleRepeatQueue());
}

void PlayerPresenterTests::repeatQueueRotatesCompletedOccurrence_data() {
    QTest::addColumn<int>("current");
    QTest::addColumn<int>("count");
    QTest::newRow("first") << 0 << 4;
    QTest::newRow("middle") << 1 << 4;
    QTest::newRow("last-wraps") << 3 << 4;
    QTest::newRow("single-restarts") << 0 << 1;
}

void PlayerPresenterTests::repeatQueueRotatesCompletedOccurrence() {
    QFETCH(int, current);
    QFETCH(int, count);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    std::vector sources{localTestPath(u8"Duplicada.wav"), localTestPath(u8"B.wav"), localTestPath(u8"Duplicada.wav"),
                        localTestPath(u8"D.wav")};
    sources.resize(static_cast<std::size_t>(count));
    PlayerPresenter presenter(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(presenter.openAudio());
    auto* const model = presenter.queueModelObject();
    auto expected = queueOccurrenceIds(*model);
    const auto completed = expected[current];
    const auto next = expected[(current + 1) % count];
    if (current != 0)
        QVERIFY(presenter.playQueueOccurrence(completed));
    expected.removeAt(current);
    expected.push_back(completed);
    QVERIFY(presenter.toggleRepeatQueue());
    const auto generation = backend.snapshot().generation;
    backend.publish(
        {.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = generation});
    QTRY_COMPARE(backend.play_calls, 1);
    const auto loads = backend.loaded_paths.size();
    const QPersistentModelIndex completed_index{model->index(current, 0)};
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    const PlaybackSnapshot ended{.status = PlaybackStatus::Paused,
                                 .position_ms = 2'000,
                                 .duration_ms = 2'000,
                                 .seekable = true,
                                 .generation = generation};
    backend.publishEvent(PlaybackEventType::Ended, ended);
    QTRY_COMPARE(backend.loaded_paths.size(), loads + 1);
    QCOMPARE(queueOccurrenceIds(*model), expected);
    QCOMPARE(presenter.currentOccurrenceId(), next);
    QCOMPARE(backend.loaded_paths.back(), sources[static_cast<std::size_t>((current + 1) % count)]);
    QVERIFY(completed_index.isValid());
    QCOMPARE(completed_index.row(), count - 1);
    QCOMPARE(resets.size(), 0);
    const auto new_generation = backend.snapshot().generation;
    backend.publishEvent(PlaybackEventType::Ended, ended, false);
    QCoreApplication::processEvents();
    QCOMPARE(backend.loaded_paths.size(), loads + 1);
    QCOMPARE(queueOccurrenceIds(*model), expected);
    backend.publish(
        {.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = new_generation});
    QTRY_COMPARE(backend.play_calls, 2);
}

void PlayerPresenterTests::repeatQueueRejectedLoadPreservesOrder_data() {
    QTest::addColumn<int>("rejection");
    QTest::newRow("busy") << static_cast<int>(PlaybackCommandResult::Busy);
    QTest::newRow("invalid") << static_cast<int>(PlaybackCommandResult::InvalidArgument);
    QTest::newRow("not-ready") << static_cast<int>(PlaybackCommandResult::NotReady);
    QTest::newRow("shutdown") << static_cast<int>(PlaybackCommandResult::Shutdown);
}

void PlayerPresenterTests::repeatQueueRejectedLoadPreservesOrder() {
    QFETCH(int, rejection);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector sources{localTestPath(u8"A.wav"), localTestPath(u8"B.wav")};
    PlayerPresenter presenter(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(presenter.openAudio());
    const auto before = queueOccurrenceIds(*presenter.queueModelObject());
    const auto reads = metadata.requests.size();
    QVERIFY(presenter.toggleRepeatQueue());
    backend.load_result = static_cast<PlaybackCommandResult>(rejection);
    backend.publishEvent(PlaybackEventType::Ended, {.status = PlaybackStatus::Paused,
                                                    .position_ms = 2'000,
                                                    .duration_ms = 2'000,
                                                    .seekable = true,
                                                    .generation = 1});
    QTRY_COMPARE(backend.loaded_paths.size(), std::size_t{2});
    QCOMPARE(queueOccurrenceIds(*presenter.queueModelObject()), before);
    QCOMPARE(presenter.currentOccurrenceId(), before[0]);
    QCOMPARE(metadata.requests.size(), reads);
    QCOMPARE(backend.unload_calls, 0);
}

void PlayerPresenterTests::repeatQueueSkipsUnavailableAndNeverResurrectsRemovedOccurrences() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector sources{localTestPath(u8"A.wav"), localTestPath(u8"B.wav"), localTestPath(u8"Removida.wav"),
                              localTestPath(u8"D.wav")};
    PlayerPresenter presenter(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(presenter.openAudio());
    auto* const model = presenter.queueModelObject();
    const auto ids = queueOccurrenceIds(*model);
    QVERIFY(presenter.toggleRepeatQueue());
    QVERIFY(presenter.removeQueueOccurrence(ids[2]));
    QVERIFY(presenter.playQueueOccurrence(ids[1]));
    backend.publish({.status = PlaybackStatus::Error,
                     .generation = backend.snapshot().generation,
                     .error = PlaybackErrorCode::SourceUnavailable});
    QTRY_COMPARE(presenter.currentOccurrenceId(), ids[3]);
    backend.publishEvent(PlaybackEventType::Ended, {.status = PlaybackStatus::Paused,
                                                    .position_ms = 2'000,
                                                    .duration_ms = 2'000,
                                                    .seekable = true,
                                                    .generation = backend.snapshot().generation});
    QTRY_COMPARE(presenter.currentOccurrenceId(), ids[0]);
    backend.publishEvent(PlaybackEventType::Ended, {.status = PlaybackStatus::Paused,
                                                    .position_ms = 2'000,
                                                    .duration_ms = 2'000,
                                                    .seekable = true,
                                                    .generation = backend.snapshot().generation});
    QTRY_COMPARE(presenter.currentOccurrenceId(), ids[3]);
    QCOMPARE(queueOccurrenceIds(*model), (QStringList{ids[1], ids[3], ids[0]}));
    backend.publish({.status = PlaybackStatus::Error,
                     .generation = backend.snapshot().generation,
                     .error = PlaybackErrorCode::InvalidMedia});
    QTRY_COMPARE(presenter.currentOccurrenceId(), ids[0]);
    const auto loads = backend.loaded_paths.size();
    backend.publish({.status = PlaybackStatus::Error,
                     .generation = backend.snapshot().generation,
                     .error = PlaybackErrorCode::AccessDenied});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Error);
    QCoreApplication::processEvents();
    QCOMPARE(backend.loaded_paths.size(), loads);
    QCOMPARE(model->rowCount(), 3);
    QVERIFY(!queueOccurrenceIds(*model).contains(ids[2]));
    for (int row = 0; row < model->rowCount(); ++row)
        QVERIFY(!model->data(model->index(row, 0), LocalQueueListModel::AvailableRole).toBool());
}

void PlayerPresenterTests::queueShuffleAndRepeatPersistTheNewOrder() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterPreferencesClock clock;
    PresenterPreferencesWriter preferences_writer;
    PreferencesAutosave preferences_autosave{preferences_writer, {}, {.now = [&clock] { return clock.now(); }}};
    PresenterSessionWriter session_writer;
    SessionAutosave session_autosave{session_writer, {}, {.now = [&clock] { return clock.now(); }}};
    const std::vector sources{localTestPath(u8"A.wav"), localTestPath(u8"B.wav"), localTestPath(u8"C.wav")};
    PlayerPresenter presenter(
        platform, backend, metadata, [sources] { return sources; },
        PlayerPresenterPreferences{
            .autosave = &preferences_autosave,
            .session = {.autosave = &session_autosave, .now = [&clock] { return clock.now(); }}});
    QVERIFY(presenter.openAudio());
    presenter.setQueueShuffleSeedForTest(5);
    QVERIFY(presenter.shuffleQueue());
    QCOMPARE(session_autosave.flush(), SessionSaveStatus::Saved);
    QVERIFY(!session_writer.flushes.empty());
    const auto shuffled = session_writer.flushes.back().snapshot;
    QCOMPARE(shuffled.queue.items[0].source_path, sources[0]);
    QCOMPARE(shuffled.queue.items[1].source_path, sources[2]);
    QCOMPARE(shuffled.queue.items[2].source_path, sources[1]);
    QVERIFY(presenter.toggleRepeatQueue());
    backend.publishEvent(PlaybackEventType::Ended, {.status = PlaybackStatus::Paused,
                                                    .position_ms = 2'000,
                                                    .duration_ms = 2'000,
                                                    .seekable = true,
                                                    .generation = 1});
    QTRY_COMPARE(presenter.currentOccurrenceId(), QStringLiteral("local-3"));
    QCOMPARE(session_autosave.flush(), SessionSaveStatus::Saved);
    const auto repeated = session_writer.flushes.back().snapshot;
    QCOMPARE(repeated.queue.items[0], shuffled.queue.items[1]);
    QCOMPARE(repeated.queue.items[1], shuffled.queue.items[2]);
    QCOMPARE(repeated.queue.items[2], shuffled.queue.items[0]);
    QCOMPARE(repeated.queue.current_occurrence_id, std::optional{shuffled.queue.items[1].occurrence_id});
    QCOMPARE(repeated.position_ms, 0);
}

void PlayerPresenterTests::queueOrderObserversMayReplaceTheOperation_data() {
    QTest::addColumn<bool>("repeating");
    QTest::addColumn<QString>("action");
    for (const bool repeating : {false, true}) {
        for (const auto* action : {"select", "exit", "destroy"}) {
            const auto name = QByteArray(repeating ? "repeat-" : "shuffle-") + action;
            QTest::newRow(name.constData()) << repeating << QString::fromLatin1(action);
        }
    }
}

void PlayerPresenterTests::queueOrderObserversMayReplaceTheOperation() {
    QFETCH(bool, repeating);
    QFETCH(QString, action);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector sources{localTestPath(u8"A.wav"), localTestPath(u8"B.wav"), localTestPath(u8"C.wav"),
                              localTestPath(u8"D.wav")};
    auto presenter = std::make_unique<PlayerPresenter>(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(presenter->openAudio());
    auto* const model = presenter->queueModelObject();
    const auto before = queueOccurrenceIds(*model);
    if (repeating)
        QVERIFY(presenter->toggleRepeatQueue());
    presenter->setQueueShuffleSeedForTest(19);
    int moves = 0;
    QMetaObject::Connection connection;
    connection = connect(model, &QAbstractItemModel::rowsMoved, model, [&] {
        ++moves;
        QObject::disconnect(connection);
        if (action == QStringLiteral("select")) {
            QVERIFY(presenter->playQueueOccurrence(before[3]));
        } else if (action == QStringLiteral("exit")) {
            QVERIFY(presenter->requestExit());
        } else {
            presenter.reset();
        }
    });
    QSignalSpy state(presenter.get(), &PlayerPresenter::stateChanged);
    QSignalSpy playback(presenter.get(), &PlayerPresenter::playbackChanged);
    if (repeating) {
        backend.publishEvent(PlaybackEventType::Ended, {.status = PlaybackStatus::Paused,
                                                        .position_ms = 2'000,
                                                        .duration_ms = 2'000,
                                                        .seekable = true,
                                                        .generation = 1});
        QCoreApplication::processEvents();
    } else {
        QVERIFY(presenter->shuffleQueue());
    }
    QCOMPARE(moves, 1);
    if (action == QStringLiteral("destroy")) {
        QVERIFY(!presenter);
        QVERIFY(!backend.callbackInstalled());
    } else if (action == QStringLiteral("select")) {
        QCOMPARE(presenter->currentOccurrenceId(), before[3]);
        QCOMPARE(backend.loaded_paths.back(), sources[3]);
        QCOMPARE(state.size(), 1);
        QCOMPARE(playback.size(), 1);
        const auto loads = backend.loaded_paths.size();
        backend.publishEvent(PlaybackEventType::Ended, {.status = PlaybackStatus::Paused, .generation = 1}, false);
        QCoreApplication::processEvents();
        QCOMPARE(backend.loaded_paths.size(), loads);
        QCOMPARE(presenter->currentOccurrenceId(), before[3]);
    } else {
        QVERIFY(presenter->exiting());
        QCOMPARE(backend.shutdown_calls, 1);
        QCOMPARE(platform.exit_calls, 1);
    }
}

void PlayerPresenterTests::shuffleQueueModePublishesPersistsAndStopsWithoutRestoringOrder() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    backend.accepted_load_status = PlaybackStatus::Ready;
    PresenterMetadataReader metadata;
    PresenterPreferencesWriter writer;
    PreferencesAutosave autosave{writer, {}};
    const std::vector sources{localTestPath(u8"A.wav"), localTestPath(u8"B.wav"), localTestPath(u8"C.wav")};
    PlayerPresenter presenter(
        platform, backend, metadata, [sources] { return sources; }, PlayerPresenterPreferences{.autosave = &autosave});
    QVERIFY(!presenter.shuffleQueueEnabled());
    const auto property =
        presenter.metaObject()->property(presenter.metaObject()->indexOfProperty("shuffleQueueEnabled"));
    QVERIFY(property.isValid());
    QCOMPARE(property.notifySignal().name(), QByteArray{"preferencesChanged"});
    QCOMPARE(presenter.metaObject()->indexOfMethod("shuffleQueue()"), -1);
    QSignalSpy changes(&presenter, &PlayerPresenter::preferencesChanged);
    QVERIFY(presenter.toggleShuffleQueue());
    QVERIFY(presenter.shuffleQueueEnabled());
    QVERIFY(presenter.openAudio());
    QVERIFY(presenter.toggleShuffleQueue());
    QVERIFY(!presenter.shuffleQueueEnabled());
    const auto before = queueOccurrenceIds(*presenter.queueModelObject());
    presenter.setQueueShuffleSeedForTest(3);
    QVERIFY(presenter.toggleShuffleQueue());
    const auto enabled_order = queueOccurrenceIds(*presenter.queueModelObject());
    QCOMPARE(enabled_order, (QStringList{before[0], before[2], before[1]}));
    QCOMPARE(autosave.flush(), PreferencesSaveStatus::Saved);
    QVERIFY(writer.flushes.back().preferences.shuffle_queue_enabled);
    QVERIFY(presenter.toggleShuffleQueue());
    QVERIFY(!presenter.shuffleQueueEnabled());
    QCOMPARE(queueOccurrenceIds(*presenter.queueModelObject()), enabled_order);
    QCOMPARE(presenter.currentOccurrenceId(), before[0]);
    QCOMPARE(backend.loaded_paths.size(), std::size_t{1});
    QCOMPARE(changes.size(), 4);
    QVERIFY(presenter.nextTrack());
    QCOMPARE(presenter.currentOccurrenceId(), enabled_order[1]);
    QCOMPARE(backend.loaded_paths.back(), sources[2]);
    QVERIFY(presenter.requestExit());
    QVERIFY(!presenter.toggleShuffleQueue());
}

void PlayerPresenterTests::shuffleQueueModeVisitsEveryRemainingOccurrence_data() {
    QTest::addColumn<bool>("repeat");
    QTest::newRow("without-repeat") << false;
    QTest::newRow("repeat-round") << true;
}

void PlayerPresenterTests::shuffleQueueModeVisitsEveryRemainingOccurrence() {
    QFETCH(bool, repeat);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    backend.accepted_load_status = PlaybackStatus::Ready;
    PresenterMetadataReader metadata;
    const std::vector sources{localTestPath(u8"A.wav"), localTestPath(u8"Duplicada.wav"),
                              localTestPath(u8"Duplicada.wav"), localTestPath(u8"D.wav"), localTestPath(u8"E.wav")};
    PlayerPresenter presenter(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(presenter.openAudio());
    const auto ids = queueOccurrenceIds(*presenter.queueModelObject());
    presenter.setQueueShuffleSeedForTest(47);
    QVERIFY(presenter.toggleShuffleQueue());
    if (repeat)
        QVERIFY(presenter.toggleRepeatQueue());
    QSet<QString> visited{presenter.currentOccurrenceId()};
    for (int step = 1; step < ids.size(); ++step) {
        const auto previous = presenter.currentOccurrenceId();
        if (step % 2 == 1) {
            QVERIFY(presenter.nextTrack());
        } else {
            backend.publishEvent(PlaybackEventType::Ended, {.status = PlaybackStatus::Paused,
                                                            .position_ms = 2'000,
                                                            .duration_ms = 2'000,
                                                            .seekable = true,
                                                            .generation = backend.snapshot().generation});
        }
        QTRY_VERIFY(presenter.currentOccurrenceId() != previous);
        QVERIFY(ids.contains(presenter.currentOccurrenceId()));
        QVERIFY(!visited.contains(presenter.currentOccurrenceId()));
        visited.insert(presenter.currentOccurrenceId());
    }
    QCOMPARE(visited.size(), ids.size());
    QCOMPARE(presenter.queueModelObject()->rowCount(), ids.size());
    if (repeat) {
        QVERIFY(presenter.canGoNext());
        QVERIFY(presenter.nextTrack());
        QVERIFY(visited.contains(presenter.currentOccurrenceId()));
    } else {
        QVERIFY(!presenter.canGoNext());
        QVERIFY(!presenter.nextTrack());
    }
}

void PlayerPresenterTests::shuffleQueueModeRejectedNextPreservesOrderAndEligibility() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    backend.accepted_load_status = PlaybackStatus::Ready;
    PresenterMetadataReader metadata;
    const std::vector sources{localTestPath(u8"A.wav"), localTestPath(u8"B.wav"), localTestPath(u8"C.wav")};
    PlayerPresenter presenter(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(presenter.openAudio());
    presenter.setQueueShuffleSeedForTest(10);
    QVERIFY(presenter.toggleShuffleQueue());
    QVERIFY(presenter.toggleRepeatQueue());
    const auto before = queueOccurrenceIds(*presenter.queueModelObject());
    backend.load_result = PlaybackCommandResult::Busy;
    QVERIFY(!presenter.nextTrack());
    QCOMPARE(queueOccurrenceIds(*presenter.queueModelObject()), before);
    QCOMPARE(presenter.currentOccurrenceId(), before[0]);
    backend.load_result = PlaybackCommandResult::Accepted;
    QSet<QString> visited{presenter.currentOccurrenceId()};
    for (int step = 0; step < 2; ++step) {
        QVERIFY(presenter.nextTrack());
        QVERIFY(!visited.contains(presenter.currentOccurrenceId()));
        visited.insert(presenter.currentOccurrenceId());
    }
    QCOMPARE(visited.size(), 3);
}

void PlayerPresenterTests::disablingShuffleFromAnOrderObserverStopsThePendingPermutation() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector sources{localTestPath(u8"A.wav"), localTestPath(u8"B.wav"), localTestPath(u8"C.wav"),
                              localTestPath(u8"D.wav"), localTestPath(u8"E.wav"), localTestPath(u8"F.wav")};
    PlayerPresenter presenter(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(presenter.openAudio());
    presenter.setQueueShuffleSeedForTest(19);
    QStringList order_when_disabled;
    QMetaObject::Connection connection;
    connection = connect(presenter.queueModelObject(), &QAbstractItemModel::rowsMoved, &presenter, [&] {
        QObject::disconnect(connection);
        order_when_disabled = queueOccurrenceIds(*presenter.queueModelObject());
        QVERIFY(presenter.toggleShuffleQueue());
        QVERIFY(!presenter.shuffleQueueEnabled());
    });
    QVERIFY(presenter.toggleShuffleQueue());
    QVERIFY(!order_when_disabled.empty());
    QVERIFY(!presenter.shuffleQueueEnabled());
    QCOMPARE(queueOccurrenceIds(*presenter.queueModelObject()), order_when_disabled);
}

void PlayerPresenterTests::removingCurrentUsesAForwardSuccessorOrLeavesPlaybackEmpty() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::filesystem::path first = localTestPath(u8"Primeira.mp3");
    const std::filesystem::path second = localTestPath(u8"Segunda.mp3");
    const std::filesystem::path third = localTestPath(u8"Terceira.mp3");
    const std::vector<std::filesystem::path> selections{first, second, third};
    PlayerPresenter presenter(platform, backend, metadata, [selections] { return selections; });
    QAbstractItemModel* const model = presenter.queueModelObject();
    QVERIFY(presenter.openAudio());
    const QString first_id = presenter.currentOccurrenceId();
    const QString second_id = model->data(model->index(1, 0), QueueListModel::OccurrenceIdRole).toString();
    const QString third_id = model->data(model->index(2, 0), QueueListModel::OccurrenceIdRole).toString();
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    QSignalSpy removed(model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy focus_requested(&presenter, &PlayerPresenter::queueFocusRequested);
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 250,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);

    QVERIFY(presenter.removeQueueOccurrence(first_id));

    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(presenter.currentOccurrenceId(), second_id);
    QCOMPARE(presenter.currentQueueIndex(), 0);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
    QCOMPARE(backend.loaded_paths, std::vector<std::filesystem::path>({first, second}));
    QCOMPARE(removed.size(), 1);
    QCOMPARE(focus_requested.last().at(0).toInt(), 0);
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 2});
    QTRY_COMPARE(backend.play_calls, 2);
    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 100,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = 2});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);

    QVERIFY(presenter.playQueueOccurrence(third_id));
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 3});
    QTRY_COMPARE(backend.play_calls, 3);
    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 100,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = 3});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);

    QVERIFY(presenter.removeQueueOccurrence(third_id));

    QCOMPARE(model->rowCount(), 1);
    QVERIFY(presenter.queueLoaded());
    QVERIFY(!presenter.hasCurrentOccurrence());
    QVERIFY(!presenter.hasCurrentTrack());
    QCOMPARE(presenter.currentQueueIndex(), -1);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Unloaded);
    QCOMPARE(backend.unload_calls, 1);
    QCOMPARE(removed.size(), 2);
    QCOMPARE(focus_requested.last().at(0).toInt(), 0);

    QVERIFY(presenter.clearQueue());
    QCOMPARE(model->rowCount(), 0);
    QVERIFY(!presenter.queueLoaded());
    QCOMPARE(backend.unload_calls, 1);
    QCOMPARE(removed.size(), 3);
    QCOMPARE(focus_requested.last().at(0).toInt(), -1);
    QCOMPARE(resets.size(), 0);
    QVERIFY(!presenter.clearQueue());
}

void PlayerPresenterTests::removingPausedCurrentKeepsTheSuccessorReady() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::filesystem::path first = localTestPath(u8"Primeira.mp3");
    const std::filesystem::path second = localTestPath(u8"Segunda.mp3");
    const std::vector<std::filesystem::path> selections{first, second};
    PlayerPresenter presenter(platform, backend, metadata, [selections] { return selections; });
    QAbstractItemModel* const model = presenter.queueModelObject();
    QVERIFY(presenter.openAudio());
    const QString first_id = presenter.currentOccurrenceId();
    const QString second_id = model->data(model->index(1, 0), QueueListModel::OccurrenceIdRole).toString();
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    backend.publish({.status = PlaybackStatus::Paused,
                     .position_ms = 500,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);

    QVERIFY(presenter.removeQueueOccurrence(first_id));
    QCOMPARE(presenter.currentOccurrenceId(), second_id);
    QCOMPARE(presenter.currentQueueIndex(), 0);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
    QCOMPARE(backend.loaded_paths, std::vector<std::filesystem::path>({first, second}));

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 2});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Ready);
    QCOMPARE(backend.play_calls, 1);
    QCOMPARE(backend.pause_calls, 0);
    QCOMPARE(resets.size(), 0);
}

void PlayerPresenterTests::addingAfterTheQueueBecomesEmptyStartsTheFirstNewOccurrence() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::filesystem::path first = localTestPath(u8"Primeira.mp3");
    const std::filesystem::path duplicate = localTestPath(u8"Duplicada 日本語.mp3");
    const std::vector<std::vector<std::filesystem::path>> selections{{first}, {duplicate, duplicate}};
    std::size_t selection_index = 0;
    PlayerPresenter presenter(platform, backend, metadata, [&] { return selections.at(selection_index++); });
    QAbstractItemModel* const model = presenter.queueModelObject();
    QVERIFY(presenter.openAudio());
    const QString removed_id = presenter.currentOccurrenceId();
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    QSignalSpy inserted(model, &QAbstractItemModel::rowsInserted);
    QSignalSpy focus_requested(&presenter, &PlayerPresenter::queueFocusRequested);

    QVERIFY(presenter.removeQueueOccurrence(removed_id));
    QCOMPARE(model->rowCount(), 0);
    QVERIFY(!presenter.queueLoaded());
    QCOMPARE(backend.unload_calls, 1);
    QCOMPARE(focus_requested.size(), 1);
    QCOMPARE(focus_requested.last().at(0).toInt(), -1);

    QVERIFY(presenter.addAudio());

    QCOMPARE(model->rowCount(), 2);
    QVERIFY(presenter.queueLoaded());
    QCOMPARE(presenter.currentQueueIndex(), 0);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
    const QString first_duplicate_id = model->data(model->index(0, 0), QueueListModel::OccurrenceIdRole).toString();
    const QString second_duplicate_id = model->data(model->index(1, 0), QueueListModel::OccurrenceIdRole).toString();
    QVERIFY(first_duplicate_id != second_duplicate_id);
    QVERIFY(first_duplicate_id != removed_id);
    QCOMPARE(presenter.currentOccurrenceId(), first_duplicate_id);
    QCOMPARE(inserted.size(), 1);
    QCOMPARE(focus_requested.size(), 2);
    QCOMPARE(focus_requested.last().at(0).toInt(), 0);
    QCOMPARE(backend.loaded_paths, std::vector<std::filesystem::path>({first, duplicate}));
    QCOMPARE(metadata.requests.size(), std::size_t{2});
    QCOMPARE(resets.size(), 0);
}

void PlayerPresenterTests::typedSourceImportPreservesOrderDuplicatesAndNativeMetacharacters() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    FakeLocalSourceDiscovery discovery;
    const auto current = localTestPath(u8"Atual.mp3");
    const auto anchor = localTestPath(u8"Ancora.flac");
    const auto duplicate = localTestPath(u8"Duplicada 日本語.mp3");
    const auto metacharacters = localTestPath(u8R"(Abertura "aspas" & ^ % ! [x].ogg)");
    const std::vector<std::filesystem::path> selection{current, anchor};
    const std::vector<std::filesystem::path> sources{duplicate, metacharacters, duplicate};
    PlayerPresenter presenter(platform, backend, metadata, [selection] { return selection; }, discovery);
    QAbstractItemModel* const model = presenter.queueModelObject();
    QVERIFY(presenter.openAudio());
    const QString current_id = presenter.currentOccurrenceId();
    QSignalSpy inserted(model, &QAbstractItemModel::rowsInserted);

    QVERIFY(presenter.openSourcePaths(sources, 1));
    QCOMPARE(discovery.requests.size(), std::size_t{1});
    QCOMPARE(discovery.requests.front().roots, sources);
    const DiscoveryGeneration generation = discovery.requests.front().generation;
    discovery.publishBatch(generation, 1, sources, {}, 3);
    PlayerPresenterTestAccess::pollSourceImport(presenter);

    QCOMPARE(queueFileNames(*model), QStringList({displayPath(current.filename()), displayPath(duplicate.filename()),
                                                  displayPath(metacharacters.filename()),
                                                  displayPath(duplicate.filename()), displayPath(anchor.filename())}));
    QCOMPARE(presenter.currentOccurrenceId(), current_id);
    QCOMPARE(presenter.currentQueueIndex(), 0);
    QCOMPARE(backend.loaded_paths, std::vector<std::filesystem::path>{current});
    QCOMPARE(inserted.size(), 1);
    QCOMPARE(inserted.front().at(1).toInt(), 1);
    QCOMPARE(inserted.front().at(2).toInt(), 3);
    QCOMPARE(presenter.sourceImportAcceptedCount(), 3ULL);
    QCOMPARE(presenter.sourceImportIssueCount(), 0ULL);
    QCOMPARE(presenter.sourceImportInspectedCount(), 3ULL);
}

void PlayerPresenterTests::typedSourceImportRejectsBusyWithoutMutatingTheActiveBatch() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    FakeLocalSourceDiscovery discovery;
    const auto first = localTestPath(u8"Primeira.mp3");
    const auto ignored = localTestPath(u8"Ignorada 日本語 & ^.flac");
    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; }, discovery);
    QAbstractItemModel* const model = presenter.queueModelObject();
    QSignalSpy import_changed(&presenter, &PlayerPresenter::sourceImportChanged);

    QVERIFY(presenter.openSourcePaths({first}, 0));
    QCOMPARE(import_changed.size(), 1);
    QCOMPARE(discovery.requests.size(), std::size_t{1});
    const DiscoveryGeneration generation = discovery.requests.front().generation;

    QVERIFY(!presenter.openSourcePaths({ignored}, 0));
    QCOMPARE(import_changed.size(), 1);
    QCOMPARE(discovery.requests.size(), std::size_t{1});
    QCOMPARE(discovery.requests.front().roots, std::vector<std::filesystem::path>{first});
    QCOMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::Discovering);
    QCOMPARE(presenter.sourceImportAcceptedCount(), 0ULL);
    QCOMPARE(presenter.sourceImportIssueCount(), 0ULL);
    QCOMPARE(presenter.sourceImportInspectedCount(), 0ULL);
    QCOMPARE(model->rowCount(), 0);
    QVERIFY(backend.loaded_paths.empty());

    discovery.publishBatch(generation, 1, {first}, {}, 1);
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QCOMPARE(queueFileNames(*model), QStringList({displayPath(first.filename())}));
    QCOMPARE(backend.loaded_paths, std::vector<std::filesystem::path>{first});
}

void PlayerPresenterTests::exitDrainsActiveAndAcceptedSourceImportsBeforeCommit() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    FakeLocalSourceDiscovery discovery;
    const auto first = localTestPath(u8"Primeira ACK.wav");
    const auto second = localTestPath(u8"Segunda ACK 日本 & 'tema'.flac");
    bool accepted_work_drained = false;
    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; }, discovery);
    presenter.setSourceExitDrainedProbe([&] { return accepted_work_drained; });

    QVERIFY(presenter.openSourcePaths({first}, 0));
    QCOMPARE(discovery.requests.size(), std::size_t{1});
    QVERIFY(presenter.requestExit());
    QVERIFY(presenter.exiting());
    QVERIFY(!presenter.exitCommitted());
    QCOMPARE(platform.exit_calls, 0);
    QVERIFY(discovery.cancelled_generations.empty());

    // Activation requests from commands ACKed before the exit intent are harmless
    // while the window remains unavailable to new user interaction.
    QVERIFY(presenter.requestRestore());
    QCOMPARE(platform.restore_calls, 0);

    const DiscoveryGeneration first_generation = discovery.requests.front().generation;
    discovery.publishBatch(first_generation, 1, {first}, {}, 1);
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    discovery.publishCompleted({.inspected_count = 1, .accepted_count = 1});
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QCOMPARE(queueFileNames(*presenter.queueModelObject()), QStringList({displayPath(first.filename())}));
    QCOMPARE(platform.exit_calls, 0);

    QVERIFY(presenter.openSourcePaths({second}, 1));
    QCOMPARE(discovery.requests.size(), std::size_t{2});
    const DiscoveryGeneration second_generation = discovery.requests.back().generation;
    discovery.publishBatch(second_generation, 1, {second}, {}, 1);
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    discovery.publishCompleted({.inspected_count = 1, .accepted_count = 1});
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QCOMPARE(queueFileNames(*presenter.queueModelObject()),
             QStringList({displayPath(first.filename()), displayPath(second.filename())}));
    QVERIFY(presenter.exiting());
    QVERIFY(!presenter.exitCommitted());
    QCOMPARE(platform.exit_calls, 0);
    QVERIFY(discovery.cancelled_generations.empty());

    accepted_work_drained = true;
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QVERIFY(presenter.exitCommitted());
    QCOMPARE(platform.exit_calls, 1);
    QVERIFY(discovery.cancelled_generations.empty());
}

void PlayerPresenterTests::sourceDrainTimeoutCancelsExitWithoutDiscardingAcceptedWork() {
    AvailableTrayPlatform platform;
    PresenterPreferencesWriter writer;
    PresenterPreferencesClock clock;
    const Preferences initial{};
    PreferencesAutosave autosave{writer, initial, {.now = [&clock] { return clock.now(); }}};
    bool accepted_work_drained = false;
    PlayerPresenter presenter(frozenBundle(), platform,
                              PlayerPresenterPreferences{.initial = initial,
                                                         .load_status = PreferencesLoadStatus::Loaded,
                                                         .autosave = &autosave,
                                                         .exit_flush_timeout = std::chrono::milliseconds{100},
                                                         .now = [&clock] { return clock.now(); }});
    presenter.setSourceExitDrainedProbe([&] { return accepted_work_drained; });
    QSignalSpy lifecycle(&presenter, &PlayerPresenter::lifecycleChanged);

    QVERIFY(presenter.requestExit());
    QVERIFY(presenter.exiting());
    QVERIFY(!presenter.exitCommitted());
    QCOMPARE(platform.exit_calls, 0);
    QCOMPARE(lifecycle.size(), 1);

    clock.advance(std::chrono::milliseconds{100});
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QVERIFY(!presenter.exiting());
    QVERIFY(!presenter.exitCommitted());
    QVERIFY(!accepted_work_drained);
    QCOMPARE(platform.exit_calls, 0);
    QCOMPARE(platform.restore_calls, 1);
    QCOMPARE(lifecycle.size(), 2);

    accepted_work_drained = true;
    QVERIFY(presenter.requestExit());
    QVERIFY(presenter.exitCommitted());
    QCOMPARE(platform.exit_calls, 1);
}

void PlayerPresenterTests::playlistSaveFailureStopsExitBeforeOtherPreparation() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    auto playlist = ExitPreparationState::Pending;
    int begins = 0;
    int source_polls = 0;
    presenter.setPlaylistExitPreparationProbe([&] { return playlist; });
    presenter.setSourceExitDrainedProbe([&] {
        ++source_polls;
        return true;
    });
    QVERIFY(presenter.setExitPreparationCallbacks(
        {.begin = [&] { ++begins; }, .poll = [] { return ExitPreparationState::Ready; }}));
    QSignalSpy recovery(&presenter, &PlayerPresenter::playlistExitRecoveryRequested);

    QVERIFY(presenter.requestExit());
    QVERIFY(!presenter.exiting());
    QVERIFY(!presenter.exitCommitted());
    QCOMPARE(recovery.size(), 0);
    QCOMPARE(begins, 0);
    QCOMPARE(source_polls, 0);

    playlist = ExitPreparationState::Failed;
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(recovery.size(), 1);
    QCOMPARE(platform.restore_calls, 1);
    QVERIFY(!presenter.exiting());
    QVERIFY(!presenter.exitCommitted());
    QVERIFY(presenter.setVolume(0.35));

    QVERIFY(!presenter.requestExit());
    QCOMPARE(recovery.size(), 2);
    QCOMPARE(begins, 0);
    QCOMPARE(source_polls, 0);
    QCOMPARE(platform.exit_calls, 0);
}

void PlayerPresenterTests::playlistSaveSuccessContinuesAnAcceptedExit() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    auto playlist = ExitPreparationState::Pending;
    int begins = 0;
    presenter.setPlaylistExitPreparationProbe([&] { return playlist; });
    QVERIFY(presenter.setExitPreparationCallbacks(
        {.begin = [&] { ++begins; }, .poll = [] { return ExitPreparationState::Ready; }}));
    QSignalSpy recovery(&presenter, &PlayerPresenter::playlistExitRecoveryRequested);

    QVERIFY(presenter.requestExit());
    QVERIFY(!presenter.exiting());
    QCOMPARE(begins, 0);
    QCOMPARE(platform.exit_calls, 0);

    playlist = ExitPreparationState::Ready;
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QVERIFY(presenter.exitCommitted());
    QCOMPARE(begins, 1);
    QCOMPARE(platform.exit_calls, 1);
    QCOMPARE(recovery.size(), 0);
}

void PlayerPresenterTests::playlistSaveExitTimeoutCannotCommitAfterLateSuccess() {
    AvailableTrayPlatform platform;
    PresenterPreferencesClock clock;
    PresenterPreferencesWriter writer;
    PreferencesAutosave autosave(writer, {}, {.now = [&clock] { return clock.now(); }});
    const auto timeout = std::chrono::milliseconds{100};
    PlayerPresenter presenter(
        frozenBundle(), platform,
        {.autosave = &autosave, .exit_flush_timeout = timeout, .now = [&clock] { return clock.now(); }});
    auto playlist = ExitPreparationState::Pending;
    int begins = 0;
    presenter.setPlaylistExitPreparationProbe([&] { return playlist; });
    QVERIFY(presenter.setExitPreparationCallbacks(
        {.begin = [&] { ++begins; }, .poll = [] { return ExitPreparationState::Ready; }}));
    QSignalSpy recovery(&presenter, &PlayerPresenter::playlistExitRecoveryRequested);

    QVERIFY(presenter.requestExit());
    QVERIFY(!presenter.exiting());
    clock.advance(timeout);
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(recovery.size(), 1);
    QCOMPARE(platform.restore_calls, 1);
    QVERIFY(!presenter.exiting());

    playlist = ExitPreparationState::Ready;
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QVERIFY(!presenter.exitCommitted());
    QCOMPARE(begins, 0);
    QCOMPARE(platform.exit_calls, 0);

    QVERIFY(presenter.requestExit());
    QVERIFY(presenter.exitCommitted());
    QCOMPARE(begins, 1);
    QCOMPARE(platform.exit_calls, 1);
    QCOMPARE(recovery.size(), 1);
}

void PlayerPresenterTests::discardingPlaylistEditRequiresANewExitRequest() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    auto playlist = ExitPreparationState::Pending;
    int begins = 0;
    presenter.setPlaylistExitPreparationProbe([&] { return playlist; });
    QVERIFY(presenter.setExitPreparationCallbacks(
        {.begin = [&] { ++begins; }, .poll = [] { return ExitPreparationState::Ready; }}));
    QSignalSpy recovery(&presenter, &PlayerPresenter::playlistExitRecoveryRequested);

    QVERIFY(presenter.requestExit());
    QVERIFY(!presenter.exiting());
    QCOMPARE(platform.exit_calls, 0);

    // The write failed and the user abandoned the edit before the next poll.
    playlist = ExitPreparationState::Failed;
    presenter.cancelPendingPlaylistExit();
    playlist = ExitPreparationState::Ready;
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QVERIFY(!presenter.exiting());
    QVERIFY(!presenter.exitCommitted());
    QCOMPARE(platform.exit_calls, 0);
    QCOMPARE(begins, 0);
    QCOMPARE(recovery.size(), 0);

    QVERIFY(presenter.requestExit());
    QVERIFY(presenter.exitCommitted());
    QCOMPARE(platform.exit_calls, 1);
    QCOMPARE(begins, 1);
}

void PlayerPresenterTests::exitPreparationPublishesFreezeBeforeBeginAndWaitsForReady() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    auto preparation = ExitPreparationState::Pending;
    int begins = 0;
    int polls = 0;
    bool nested_exit = true;
    connect(&presenter, &PlayerPresenter::lifecycleChanged, &presenter, [&] {
        if (presenter.exiting() && !presenter.exitCommitted() && begins == 0) {
            PlayerPresenterTestAccess::pollPersistence(presenter);
            QCOMPARE(polls, 0);
        }
    });
    QVERIFY(presenter.setExitPreparationCallbacks({.begin =
                                                       [&] {
                                                           ++begins;
                                                           QVERIFY(presenter.exiting());
                                                           QVERIFY(!presenter.exitCommitted());
                                                           nested_exit = presenter.requestExit();
                                                           QVERIFY(!presenter.setExitPreparationCallbacks({}));
                                                           PlayerPresenterTestAccess::pollPersistence(presenter);
                                                       },
                                                   .poll =
                                                       [&] {
                                                           ++polls;
                                                           return preparation;
                                                       }}));

    QVERIFY(presenter.requestExit());
    QCOMPARE(begins, 1);
    QVERIFY(!nested_exit);
    QVERIFY(presenter.exiting());
    QVERIFY(!presenter.exitCommitted());
    QCOMPARE(platform.exit_calls, 0);
    QVERIFY(polls > 0);
    preparation = ExitPreparationState::Ready;
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QVERIFY(presenter.exitCommitted());
    QCOMPARE(platform.exit_calls, 1);
    QCOMPARE(begins, 1);
}

void PlayerPresenterTests::exitPreparationDoesNotReplaceAcceptedSourceDrain() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    bool sources_drained = false;
    auto preparation = ExitPreparationState::Ready;
    presenter.setSourceExitDrainedProbe([&] { return sources_drained; });
    QVERIFY(presenter.setExitPreparationCallbacks({.begin = [] {}, .poll = [&] { return preparation; }}));
    QVERIFY(presenter.requestExit());
    QCOMPARE(platform.exit_calls, 0);
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(platform.exit_calls, 0);
    sources_drained = true;
    preparation = ExitPreparationState::Pending;
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(platform.exit_calls, 0);
    preparation = ExitPreparationState::Ready;
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(platform.exit_calls, 1);
}

void PlayerPresenterTests::exitPreparationFailureIsReversibleWithoutDetachingTheGate_data() {
    QTest::addColumn<QString>("failure");
    for (const auto* failure : {"failed", "begin-exception", "poll-exception", "unknown", "lost-ready"})
        QTest::newRow(failure) << QString::fromLatin1(failure);
}

void PlayerPresenterTests::exitPreparationFailureIsReversibleWithoutDetachingTheGate() {
    QFETCH(QString, failure);
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    int polls = 0;
    QVERIFY(presenter.setExitPreparationCallbacks({.begin =
                                                       [&] {
                                                           if (failure == QStringLiteral("begin-exception"))
                                                               throw std::runtime_error{"begin failed"};
                                                       },
                                                   .poll =
                                                       [&] {
                                                           ++polls;
                                                           if (failure == QStringLiteral("poll-exception"))
                                                               throw std::runtime_error{"poll failed"};
                                                           if (failure == QStringLiteral("unknown"))
                                                               return static_cast<ExitPreparationState>(99);
                                                           if (failure == QStringLiteral("lost-ready") && polls == 1)
                                                               return ExitPreparationState::Ready;
                                                           return ExitPreparationState::Failed;
                                                       }}));
    static_cast<void>(presenter.requestExit());
    QVERIFY(!presenter.exiting());
    QVERIFY(!presenter.exitCommitted());
    QCOMPARE(platform.exit_calls, 0);
    QVERIFY(!presenter.setExitPreparationCallbacks({}));
    QVERIFY(
        !presenter.setExitPreparationCallbacks({.begin = [] {}, .poll = [] { return ExitPreparationState::Ready; }}));
    QVERIFY(presenter.setVolume(0.35));
    static_cast<void>(presenter.requestExit());
    QCOMPARE(platform.exit_calls, 0);
}

void PlayerPresenterTests::exitPreparationTimeoutAllowsANewIntentButKeepsTheGate() {
    AvailableTrayPlatform platform;
    PresenterPreferencesWriter writer;
    auto now = PlayerPresenterPreferences::TimePoint{};
    PreferencesAutosave autosave(writer, {}, {.now = [&] { return now; }});
    PlayerPresenter presenter(
        frozenBundle(), platform,
        {.autosave = &autosave, .exit_flush_timeout = std::chrono::milliseconds{100}, .now = [&] { return now; }});
    auto preparation = ExitPreparationState::Pending;
    int begins = 0;
    QVERIFY(presenter.setExitPreparationCallbacks({.begin = [&] { ++begins; }, .poll = [&] { return preparation; }}));
    QVERIFY(presenter.requestExit());
    now += std::chrono::milliseconds{100};
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QVERIFY(!presenter.exiting());
    QVERIFY(!presenter.exitCommitted());
    QVERIFY(!presenter.setExitPreparationCallbacks({}));
    QCOMPARE(platform.exit_calls, 0);
    preparation = ExitPreparationState::Ready;
    QVERIFY(presenter.requestExit());
    QCOMPARE(begins, 2);
    QCOMPARE(platform.exit_calls, 1);
}

void PlayerPresenterTests::exitPreparationSetterRejectsIncompleteReplacementAndWrongThread() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    const ExitPreparationCallbacks callbacks{.begin = [] {}, .poll = [] { return ExitPreparationState::Pending; }};
    QVERIFY(!presenter.setExitPreparationCallbacks({.begin = [] {}}));
    QVERIFY(!presenter.setExitPreparationCallbacks({.poll = [] { return ExitPreparationState::Ready; }}));
    bool cross_thread_result = true;
    std::thread worker([&] { cross_thread_result = presenter.setExitPreparationCallbacks(callbacks); });
    worker.join();
    QVERIFY(!cross_thread_result);
    QVERIFY(presenter.setExitPreparationCallbacks(callbacks));
    QVERIFY(!presenter.setExitPreparationCallbacks(callbacks));
    QVERIFY(presenter.setExitPreparationCallbacks({}));
    QVERIFY(presenter.requestExit());
    QCOMPARE(platform.exit_calls, 1);
}

void PlayerPresenterTests::exitPreparationCanLoseItsPresenterDuringBeginOrPoll_data() {
    QTest::addColumn<bool>("destroy_in_begin");
    QTest::newRow("begin") << true;
    QTest::newRow("poll") << false;
}

void PlayerPresenterTests::exitPreparationCanLoseItsPresenterDuringBeginOrPoll() {
    QFETCH(bool, destroy_in_begin);
    AvailableTrayPlatform platform;
    auto presenter = std::make_unique<PlayerPresenter>(frozenBundle(), platform);
    QPointer<PlayerPresenter> guard{presenter.get()};
    QVERIFY(presenter->setExitPreparationCallbacks({.begin =
                                                        [&] {
                                                            if (destroy_in_begin)
                                                                presenter.reset();
                                                        },
                                                    .poll =
                                                        [&] {
                                                            presenter.reset();
                                                            return ExitPreparationState::Ready;
                                                        }}));
    static_cast<void>(presenter->requestExit());
    QVERIFY(guard.isNull());
    QCOMPARE(platform.exit_calls, 0);
}

void PlayerPresenterTests::exitCancelsANonIpcSourceImportWhenAcceptedIpcWorkIsDrained() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    FakeLocalSourceDiscovery discovery;
    const auto source = localTestPath(u8"Importação local em andamento.wav");
    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; }, discovery);
    presenter.setSourceExitDrainedProbe([] { return true; });

    QVERIFY(presenter.importDroppedUrls({QUrl::fromLocalFile(displayPath(source))}, 0));
    QVERIFY(presenter.sourceImportActive());
    const DiscoveryGeneration generation = discovery.requests.front().generation;

    QVERIFY(presenter.requestExit());
    QVERIFY(presenter.exitCommitted());
    QCOMPARE(platform.exit_calls, 1);
    QCOMPARE(discovery.cancelled_generations, std::vector<DiscoveryGeneration>{generation});
    QCOMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::Cancelled);
}

void PlayerPresenterTests::droppedSourceImportValidatesBoundaryAndKeepsStableAnchor() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    FakeLocalSourceDiscovery discovery;
    const auto first = localTestPath(u8"Primeira.mp3");
    const auto anchor = localTestPath(u8"Ancora.flac");
    const auto inserted_first = localTestPath(u8"Inserida 1.ogg");
    const auto inserted_second = localTestPath(u8"Inserida 2.wav");
    const std::vector<std::filesystem::path> selection{first, anchor};
    PlayerPresenter presenter(platform, backend, metadata, [selection] { return selection; }, discovery);
    QAbstractItemModel* const model = presenter.queueModelObject();
    QVERIFY(presenter.openAudio());
    const QString current_id = presenter.currentOccurrenceId();
    QSignalSpy inserted(model, &QAbstractItemModel::rowsInserted);

    QVERIFY(!presenter.importDroppedUrls({QUrl{QStringLiteral("https://example.invalid/audio.mp3")}}, 1));
    QCOMPARE(discovery.requests.size(), std::size_t{0});
    QCOMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::Rejected);
    QCOMPARE(presenter.sourceImportIssueCount(), 1ULL);
    QVERIFY(presenter.dismissSourceImportStatus());

    const QList<QUrl> dropped{localTestUrl(inserted_first),
                              QUrl{QStringLiteral("https://example.invalid/ignored.wav")}};
    QVERIFY(presenter.importDroppedUrls(dropped, 1));
    QCOMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::Discovering);
    QCOMPARE(discovery.requests.size(), std::size_t{1});
    QCOMPARE(discovery.requests.front().roots, std::vector<std::filesystem::path>{inserted_first});
    const DiscoveryGeneration generation = discovery.requests.front().generation;
    discovery.publishBatch(generation, 1, {inserted_first, inserted_second}, {}, 2);
    PlayerPresenterTestAccess::pollSourceImport(presenter);

    QCOMPARE(queueFileNames(*model), QStringList({QStringLiteral("Primeira.mp3"), QStringLiteral("Inserida 1.ogg"),
                                                  QStringLiteral("Inserida 2.wav"), QStringLiteral("Ancora.flac")}));
    QCOMPARE(presenter.currentOccurrenceId(), current_id);
    QCOMPARE(presenter.currentQueueIndex(), 0);
    QCOMPARE(inserted.size(), 1);
    QCOMPARE(backend.loaded_paths, std::vector<std::filesystem::path>{first});
    QCOMPARE(inserted.front().at(1).toInt(), 1);
    QCOMPARE(inserted.front().at(2).toInt(), 2);
    QCOMPARE(presenter.sourceImportAcceptedCount(), 2ULL);
    QCOMPARE(presenter.sourceImportIssueCount(), 1ULL);
    QCOMPARE(presenter.sourceImportInspectedCount(), 2ULL);

    discovery.publishCompleted({.inspected_count = 2, .accepted_count = 2, .issue_count = 0});
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QCOMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::CompletedWithIssues);
}

void PlayerPresenterTests::droppedSourceImportRejectsMalformedPathsWithoutLosingValidItems_data() {
    QTest::addColumn<QUrl>("invalidUrl");
    QTest::newRow("decoded-nul") << QUrl{localTestUrl(localTestPath(u8"A.wav")).toString() +
                                         QStringLiteral("%00outro")};
    QTest::newRow("relative") << QUrl{QStringLiteral("file:relative.wav")};
    QTest::newRow("empty") << QUrl{QStringLiteral("file:")};
#ifdef Q_OS_WIN
    QTest::newRow("drive-relative") << QUrl{QStringLiteral("file:///C:relative.wav")};
    QTest::newRow("root-without-drive") << QUrl{QStringLiteral("file:///relative.wav")};
    QTest::newRow("device") << QUrl::fromLocalFile(QStringLiteral(R"(\\.\PhysicalDrive0)"));
    QTest::newRow("pipe") << QUrl::fromLocalFile(QStringLiteral(R"(\\.\pipe\audio.wav)"));
    QTest::newRow("object-manager") << QUrl::fromLocalFile(QStringLiteral(R"(\??\C:\audio.wav)"));
#endif
}

void PlayerPresenterTests::droppedSourceImportRejectsMalformedPathsWithoutLosingValidItems() {
    QFETCH(QUrl, invalidUrl);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    FakeLocalSourceDiscovery discovery;
    const auto valid = localTestPath(u8"Válida 日本語 %00.wav");
    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; }, discovery);

    QVERIFY(!presenter.importDroppedUrls({invalidUrl}, 0));
    QVERIFY(discovery.requests.empty());
    QCOMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::Rejected);
    QCOMPARE(presenter.sourceImportIssueCount(), 1ULL);
    QVERIFY(backend.loaded_paths.empty());
    QCOMPARE(presenter.queueModelObject()->rowCount(), 0);
    QVERIFY(presenter.dismissSourceImportStatus());

    QVERIFY(presenter.importDroppedUrls({invalidUrl, localTestUrl(valid), localTestUrl(valid)}, 0));
    QCOMPARE(discovery.requests.size(), std::size_t{1});
    QCOMPARE(discovery.requests.front().roots, std::vector<std::filesystem::path>({valid, valid}));
    QCOMPARE(presenter.sourceImportIssueCount(), 1ULL);
    const DiscoveryGeneration generation = discovery.requests.front().generation;
    discovery.publishBatch(generation, 1, {valid, valid}, {}, 2);
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    discovery.publishCompleted({.inspected_count = 2, .accepted_count = 2, .issue_count = 0});
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QCOMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::CompletedWithIssues);
    QCOMPARE(presenter.sourceImportAcceptedCount(), 2ULL);
    QCOMPARE(presenter.sourceImportIssueCount(), 1ULL);
    QCOMPARE(presenter.queueModelObject()->rowCount(), 2);
    QCOMPARE(backend.loaded_paths, std::vector<std::filesystem::path>{valid});
}

void PlayerPresenterTests::droppedSourceImportPreservesAbsoluteWindowsPaths_data() {
    QTest::addColumn<QString>("path");
    QTest::newRow("unicode-percent") << displayPath(localTestPath(u8"Pasta com espaço\\日本語 %00.wav"));
#ifdef Q_OS_WIN
    QTest::newRow("unc") << QStringLiteral(R"(\\servidor\Música\日本語.wav)");
    QTest::newRow("extended-drive") << QStringLiteral(R"(\\?\C:\Música\日本語.wav)");
    QTest::newRow("extended-unc") << QStringLiteral(R"(\\?\UNC\servidor\Música\日本語.wav)");
    QTest::newRow("long-path") << QStringLiteral("C:/Music/") + QString(280, QLatin1Char('a')) + QStringLiteral(".wav");
#endif
}

void PlayerPresenterTests::droppedSourceImportPreservesAbsoluteWindowsPaths() {
    QFETCH(QString, path);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    FakeLocalSourceDiscovery discovery;
    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; }, discovery);
    const auto url = QUrl::fromLocalFile(path);
    QVERIFY(url.isValid());
    QVERIFY(presenter.importDroppedUrls({url}, 0));
    QCOMPARE(presenter.sourceImportIssueCount(), 0ULL);
    QCOMPARE(discovery.requests.size(), std::size_t{1});
#ifdef Q_OS_WIN
    QCOMPARE(discovery.requests.front().roots, std::vector{std::filesystem::path{path.toStdWString()}});
#else
    QCOMPARE(discovery.requests.front().roots, std::vector{std::filesystem::path{path.toStdString()}});
#endif
}

void PlayerPresenterTests::sourceImportTracksMovedAnchorAndFallsBackAfterItsRemoval() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    FakeLocalSourceDiscovery discovery;
    const auto first = localTestPath(u8"A.mp3");
    const auto predecessor = localTestPath(u8"B.mp3");
    const auto anchor = localTestPath(u8"C.mp3");
    const auto tail = localTestPath(u8"D.mp3");
    const auto batch_one = localTestPath(u8"X.mp3");
    const auto batch_two = localTestPath(u8"Y.mp3");
    const auto batch_three = localTestPath(u8"Z.mp3");
    const std::vector<std::filesystem::path> selection{first, predecessor, anchor, tail};
    PlayerPresenter presenter(platform, backend, metadata, [selection] { return selection; }, discovery);
    QAbstractItemModel* const model = presenter.queueModelObject();
    QVERIFY(presenter.openAudio());
    const QString anchor_id = model->data(model->index(2, 0), QueueListModel::OccurrenceIdRole).toString();

    QVERIFY(presenter.importDroppedUrls({localTestUrl(localTestPath(u8"pasta"))}, 2));
    const DiscoveryGeneration generation = discovery.requests.front().generation;
    discovery.publishBatch(generation, 1, {batch_one}, {}, 1);
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QCOMPARE(queueFileNames(*model),
             QStringList({QStringLiteral("A.mp3"), QStringLiteral("B.mp3"), QStringLiteral("X.mp3"),
                          QStringLiteral("C.mp3"), QStringLiteral("D.mp3")}));

    QVERIFY(presenter.moveQueueOccurrence(anchor_id, 1));
    discovery.publishBatch(generation, 2, {batch_two}, {}, 1);
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QCOMPARE(queueFileNames(*model),
             QStringList({QStringLiteral("A.mp3"), QStringLiteral("Y.mp3"), QStringLiteral("C.mp3"),
                          QStringLiteral("B.mp3"), QStringLiteral("X.mp3"), QStringLiteral("D.mp3")}));

    QVERIFY(presenter.removeQueueOccurrence(anchor_id));
    discovery.publishBatch(generation, 3, {batch_three}, {}, 1);
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QCOMPARE(queueFileNames(*model),
             QStringList({QStringLiteral("A.mp3"), QStringLiteral("Y.mp3"), QStringLiteral("Z.mp3"),
                          QStringLiteral("B.mp3"), QStringLiteral("X.mp3"), QStringLiteral("D.mp3")}));
}

void PlayerPresenterTests::sourceImportOnEmptyQueuePreservesTransientFirstLoadAndAutoplaysTheNext_data() {
    QTest::addColumn<int>("transient_result");
    QTest::newRow("busy") << static_cast<int>(PlaybackCommandResult::Busy);
    QTest::newRow("not-ready") << static_cast<int>(PlaybackCommandResult::NotReady);
}

void PlayerPresenterTests::sourceImportOnEmptyQueuePreservesTransientFirstLoadAndAutoplaysTheNext() {
    QFETCH(int, transient_result);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    FakeLocalSourceDiscovery discovery;
    const auto rejected = localTestPath(u8"Corrompida.bin");
    const auto playable = localTestPath(u8"Valida.mp3");
    backend.load_results = {static_cast<PlaybackCommandResult>(transient_result), PlaybackCommandResult::Accepted};
    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; }, discovery);
    QAbstractItemModel* const model = presenter.queueModelObject();

    QVERIFY(presenter.importDroppedUrls({localTestUrl(rejected), localTestUrl(playable)}, 0));
    const DiscoveryGeneration generation = discovery.requests.front().generation;
    discovery.publishBatch(generation, 1, {rejected, playable}, {}, 2);
    PlayerPresenterTestAccess::pollSourceImport(presenter);

    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(backend.loaded_paths, std::vector<std::filesystem::path>({rejected, playable}));
    QCOMPARE(model->data(model->index(0, 0), QueueListModel::AvailableRole).toBool(), true);
    QVERIFY(!model->data(model->index(0, 0), QueueListModel::ErrorCodeRole).isValid());
    QCOMPARE(presenter.currentQueueIndex(), 1);
    QCOMPARE(presenter.currentTitle(), QStringLiteral("Valida"));
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
    QCOMPARE(metadata.requests.size(), std::size_t{1});
    QCOMPARE(metadata.requests.front().source_path, playable);
    QCOMPARE(presenter.sourceImportAcceptedCount(), 2ULL);
    QCOMPARE(presenter.sourceImportIssueCount(), 0ULL);

    discovery.publishCompleted({.inspected_count = 2, .accepted_count = 2, .issue_count = 0});
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QCOMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::Completed);
}

void PlayerPresenterTests::sourceImportMarksInvalidArgumentLoadUnavailableAndContinues() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    FakeLocalSourceDiscovery discovery;
    const auto invalid = localTestPath(u8"Inválida.bin");
    const auto playable = localTestPath(u8"Válida.mp3");
    backend.load_results = {PlaybackCommandResult::InvalidArgument, PlaybackCommandResult::Accepted};
    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; }, discovery);
    QAbstractItemModel* const model = presenter.queueModelObject();

    QVERIFY(presenter.importDroppedUrls({localTestUrl(invalid), localTestUrl(playable)}, 0));
    const DiscoveryGeneration generation = discovery.requests.front().generation;
    discovery.publishBatch(generation, 1, {invalid, playable}, {}, 2);
    PlayerPresenterTestAccess::pollSourceImport(presenter);

    QCOMPARE(backend.loaded_paths, std::vector<std::filesystem::path>({invalid, playable}));
    QCOMPARE(model->data(model->index(0, 0), QueueListModel::AvailableRole).toBool(), false);
    QCOMPARE(model->data(model->index(0, 0), QueueListModel::ErrorCodeRole).toString(),
             QStringLiteral("open_rejected"));
    QCOMPARE(model->data(model->index(1, 0), QueueListModel::AvailableRole).toBool(), true);
    QCOMPARE(presenter.currentQueueIndex(), 1);
    QCOMPARE(presenter.sourceImportIssueCount(), 1ULL);

    discovery.publishCompleted({.inspected_count = 2, .accepted_count = 2, .issue_count = 0});
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QCOMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::CompletedWithIssues);
}

void PlayerPresenterTests::sourceImportCountsAnAsynchronousFailureOnceAfterCompletion() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    FakeLocalSourceDiscovery discovery;
    const auto invalid = localTestPath(u8"Corrompida.bin");
    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; }, discovery);
    QAbstractItemModel* const model = presenter.queueModelObject();
    QSignalSpy source_import_changed(&presenter, &PlayerPresenter::sourceImportChanged);

    QVERIFY(presenter.importDroppedUrls({localTestUrl(invalid)}, 0));
    const DiscoveryGeneration generation = discovery.requests.front().generation;
    discovery.publishBatch(generation, 1, {invalid}, {}, 1);
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    discovery.publishCompleted({.inspected_count = 1, .accepted_count = 1, .issue_count = 0});
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QCOMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::Completed);
    QCOMPARE(presenter.sourceImportIssueCount(), 0ULL);

    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::InvalidMedia});

    QTRY_COMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::CompletedWithIssues);
    QCOMPARE(presenter.sourceImportIssueCount(), 1ULL);
    QCOMPARE(model->data(model->index(0, 0), QueueListModel::AvailableRole).toBool(), false);
    const int notifications_after_first_error = source_import_changed.size();

    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::InvalidMedia});
    QCoreApplication::processEvents();

    QCOMPARE(presenter.sourceImportIssueCount(), 1ULL);
    QCOMPARE(source_import_changed.size(), notifications_after_first_error);
    QVERIFY(presenter.retryQueueOccurrence(presenter.currentOccurrenceId()));
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 2});
    QTRY_VERIFY(model->data(model->index(0, 0), QueueListModel::AvailableRole).toBool());
    backend.publish({.status = PlaybackStatus::Error, .generation = 2, .error = PlaybackErrorCode::SourceUnavailable});
    QTRY_VERIFY(!model->data(model->index(0, 0), QueueListModel::AvailableRole).toBool());
    QCOMPARE(presenter.sourceImportIssueCount(), 1ULL);
    QCOMPARE(source_import_changed.size(), notifications_after_first_error);
}

void PlayerPresenterTests::dismissingSourceImportStopsTrackingCompletedOccurrences() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    FakeLocalSourceDiscovery discovery;
    const auto imported = localTestPath(u8"Importada.mp3");
    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; }, discovery);
    QAbstractItemModel* const model = presenter.queueModelObject();

    QVERIFY(presenter.importDroppedUrls({localTestUrl(imported)}, 0));
    const DiscoveryGeneration generation = discovery.requests.front().generation;
    discovery.publishBatch(generation, 1, {imported}, {}, 1);
    discovery.publishCompleted({.inspected_count = 1, .accepted_count = 1, .issue_count = 0});
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QVERIFY(presenter.dismissSourceImportStatus());

    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::InvalidMedia});

    QTRY_COMPARE(model->data(model->index(0, 0), QueueListModel::AvailableRole).toBool(), false);
    QCOMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::Idle);
    QCOMPARE(presenter.sourceImportIssueCount(), 0ULL);
}

void PlayerPresenterTests::startingANewSourceImportStopsTrackingPreviousOccurrences() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    FakeLocalSourceDiscovery discovery;
    const auto previous = localTestPath(u8"Anterior.mp3");
    const auto next = localTestPath(u8"Nova.mp3");
    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; }, discovery);
    QAbstractItemModel* const model = presenter.queueModelObject();

    QVERIFY(presenter.importDroppedUrls({localTestUrl(previous)}, 0));
    const DiscoveryGeneration previous_generation = discovery.requests.back().generation;
    discovery.publishBatch(previous_generation, 1, {previous}, {}, 1);
    discovery.publishCompleted({.inspected_count = 1, .accepted_count = 1, .issue_count = 0});
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QCOMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::Completed);

    QVERIFY(presenter.importDroppedUrls({localTestUrl(next)}, 1));
    const DiscoveryGeneration next_generation = discovery.requests.back().generation;
    QCOMPARE(presenter.sourceImportIssueCount(), 0ULL);
    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::InvalidMedia});
    QTRY_COMPARE(model->data(model->index(0, 0), QueueListModel::AvailableRole).toBool(), false);
    QCOMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::Discovering);
    QCOMPARE(presenter.sourceImportIssueCount(), 0ULL);

    discovery.publishBatch(next_generation, 1, {next}, {}, 1);
    discovery.publishCompleted({.inspected_count = 1, .accepted_count = 1, .issue_count = 0});
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QCOMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::Completed);
    QCOMPARE(presenter.sourceImportIssueCount(), 0ULL);
}

void PlayerPresenterTests::sourceImportResumesAutoplayWhenALaterBatchFollowsAnAsynchronousFailure() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    FakeLocalSourceDiscovery discovery;
    const auto invalid = localTestPath(u8"Primeira corrompida.bin");
    const auto playable = localTestPath(u8"Segunda válida.mp3");
    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; }, discovery);
    QAbstractItemModel* const model = presenter.queueModelObject();

    QVERIFY(presenter.importDroppedUrls({localTestUrl(localTestPath(u8"pasta"))}, 0));
    const DiscoveryGeneration generation = discovery.requests.front().generation;
    discovery.publishBatch(generation, 1, {invalid}, {}, 1);
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QCOMPARE(backend.loaded_paths, std::vector<std::filesystem::path>{invalid});

    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::InvalidMedia});
    QTRY_COMPARE(model->data(model->index(0, 0), QueueListModel::AvailableRole).toBool(), false);

    discovery.publishBatch(generation, 2, {playable}, {}, 1);
    PlayerPresenterTestAccess::pollSourceImport(presenter);

    QCOMPARE(backend.loaded_paths, std::vector<std::filesystem::path>({invalid, playable}));
    QCOMPARE(presenter.currentQueueIndex(), 1);
    QCOMPARE(presenter.currentTitle(), QStringLiteral("Segunda válida"));
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
    QCOMPARE(presenter.sourceImportIssueCount(), 1ULL);

    discovery.publishCompleted({.inspected_count = 2, .accepted_count = 2, .issue_count = 0});
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QCOMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::CompletedWithIssues);
    QCOMPARE(presenter.sourceImportIssueCount(), 1ULL);
}

void PlayerPresenterTests::sourceImportDoesNotArmAutoplayAfterItsInitialTrackIsRemoved() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    FakeLocalSourceDiscovery discovery;
    const auto existing = localTestPath(u8"Existente.mp3");
    const auto imported = localTestPath(u8"Importada.mp3");
    const std::vector<std::filesystem::path> selection{existing};
    PlayerPresenter presenter(platform, backend, metadata, [selection] { return selection; }, discovery);
    QAbstractItemModel* const model = presenter.queueModelObject();
    QVERIFY(presenter.openAudio());
    const QString existing_id = presenter.currentOccurrenceId();

    QVERIFY(presenter.importDroppedUrls({localTestUrl(imported)}, 1));
    const DiscoveryGeneration generation = discovery.requests.back().generation;
    QVERIFY(presenter.removeQueueOccurrence(existing_id));
    QCOMPARE(model->rowCount(), 0);

    discovery.publishBatch(generation, 1, {imported}, {}, 1);
    PlayerPresenterTestAccess::pollSourceImport(presenter);

    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(backend.loaded_paths, std::vector<std::filesystem::path>{existing});
    QCOMPARE(presenter.currentQueueIndex(), -1);
    QCOMPARE(model->data(model->index(0, 0), QueueListModel::AvailableRole).toBool(), true);
}

void PlayerPresenterTests::sourceImportIssuesBetweenBatchesDoNotCancelValidFiles() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    FakeLocalSourceDiscovery discovery;
    const auto first = localTestPath(u8"Primeira.flac");
    const auto ignored = localTestPath(u8"ignorada.txt");
    const auto second = localTestPath(u8"Segunda.ogg");
    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; }, discovery);
    QAbstractItemModel* const model = presenter.queueModelObject();

    QVERIFY(presenter.importDroppedUrls({localTestUrl(localTestPath(u8"pasta"))}, 0));
    const DiscoveryGeneration generation = discovery.requests.front().generation;
    discovery.publishBatch(generation, 1, {first}, {}, 1);
    discovery.publishBatch(generation, 2, {}, {{LocalSourceDiscoveryIssueCode::UnsupportedFile, ignored}}, 1);
    discovery.publishBatch(generation, 3, {second}, {}, 1);
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    PlayerPresenterTestAccess::pollSourceImport(presenter);

    QCOMPARE(queueFileNames(*model), QStringList({QStringLiteral("Primeira.flac"), QStringLiteral("Segunda.ogg")}));
    QCOMPARE(backend.loaded_paths, std::vector<std::filesystem::path>{first});
    QCOMPARE(presenter.sourceImportAcceptedCount(), 2ULL);
    QCOMPARE(presenter.sourceImportIssueCount(), 1ULL);
    QCOMPARE(presenter.sourceImportInspectedCount(), 3ULL);
    QCOMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::Discovering);

    discovery.publishCompleted({.inspected_count = 3, .accepted_count = 2, .issue_count = 1});
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QCOMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::CompletedWithIssues);
}

void PlayerPresenterTests::cancellingSourceImportKeepsConsumedBatchesAndDiscardsLateOnes() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    FakeLocalSourceDiscovery discovery;
    const auto consumed = localTestPath(u8"Consumida.wav");
    const auto late = localTestPath(u8"Tardia.wav");
    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; }, discovery);
    QAbstractItemModel* const model = presenter.queueModelObject();

    QVERIFY(presenter.importDroppedUrls({localTestUrl(localTestPath(u8"pasta"))}, 0));
    const DiscoveryGeneration generation = discovery.requests.front().generation;
    discovery.publishBatch(generation, 1, {consumed}, {}, 1);
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QCOMPARE(model->rowCount(), 1);

    QVERIFY(presenter.cancelSourceImport());
    QVERIFY(presenter.cancelSourceImport());
    QCOMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::Cancelling);
    QCOMPARE(discovery.cancelled_generations, std::vector<DiscoveryGeneration>{generation});
    discovery.publishCancelled();
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QCOMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::Cancelled);
    QCOMPARE(model->rowCount(), 1);

    discovery.publishBatch(generation, 2, {late}, {}, 1);
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QCOMPARE(queueFileNames(*model), QStringList{QStringLiteral("Consumida.wav")});
    QCOMPARE(presenter.sourceImportAcceptedCount(), 1ULL);
    QVERIFY(presenter.dismissSourceImportStatus());
    QCOMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::Idle);
    QCOMPARE(presenter.sourceImportAcceptedCount(), 0ULL);
}

void PlayerPresenterTests::rejectedQueuePlaybackTransitionsLeaveTheQueueAtomic() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::filesystem::path first = localTestPath(u8"Primeira.mp3");
    const std::filesystem::path second = localTestPath(u8"Segunda.mp3");
    const std::vector<std::filesystem::path> selections{first, second};
    PlayerPresenter presenter(platform, backend, metadata, [selections] { return selections; });
    QAbstractItemModel* const model = presenter.queueModelObject();
    QVERIFY(presenter.openAudio());
    const QString current_id = presenter.currentOccurrenceId();
    const QString second_id = model->data(model->index(1, 0), QueueListModel::OccurrenceIdRole).toString();
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    QSignalSpy removed(model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy focus_requested(&presenter, &PlayerPresenter::queueFocusRequested);
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 250,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);

    backend.load_result = PlaybackCommandResult::Busy;
    QVERIFY(!presenter.removeQueueOccurrence(current_id));
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(presenter.currentOccurrenceId(), current_id);
    QCOMPARE(presenter.currentQueueIndex(), 0);
    QCOMPARE(model->data(model->index(1, 0), QueueListModel::OccurrenceIdRole).toString(), second_id);
    QCOMPARE(removed.size(), 0);
    QCOMPARE(focus_requested.size(), 0);
    QCOMPARE(resets.size(), 0);

    backend.load_result = PlaybackCommandResult::Accepted;
    backend.unload_result = PlaybackCommandResult::Busy;
    QVERIFY(!presenter.clearQueue());
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(presenter.currentOccurrenceId(), current_id);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QCOMPARE(removed.size(), 0);
    QCOMPARE(focus_requested.size(), 0);
    QCOMPARE(resets.size(), 0);
}

void PlayerPresenterTests::transientNavigationRejectionKeepsTheTargetAvailable() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector<std::filesystem::path> selections{
        localTestPath(u8"Atual.mp3"),
        localTestPath(u8"Próxima.mp3"),
    };
    PlayerPresenter presenter(platform, backend, metadata, [selections] { return selections; });
    QAbstractItemModel* const model = presenter.queueModelObject();
    QVERIFY(presenter.openAudio());
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Ready);
    QVERIFY(presenter.canGoNext());
    const QString current_id = presenter.currentOccurrenceId();
    QSignalSpy data_changed(model, &QAbstractItemModel::dataChanged);
    QSignalSpy state_changed(&presenter, &PlayerPresenter::stateChanged);
    QSignalSpy playback_changed(&presenter, &PlayerPresenter::playbackChanged);
    backend.load_result = PlaybackCommandResult::Busy;

    QVERIFY(!presenter.nextTrack());

    QCOMPARE(presenter.currentOccurrenceId(), current_id);
    QCOMPARE(presenter.currentQueueIndex(), 0);
    QVERIFY(model->data(model->index(1, 0), QueueListModel::AvailableRole).toBool());
    QCOMPARE(model->data(model->index(1, 0), QueueListModel::ErrorCodeRole).toString(), QString{});
    QCOMPARE(data_changed.size(), 0);
    QCOMPARE(state_changed.size(), 0);
    QCOMPARE(playback_changed.size(), 0);
    QVERIFY(presenter.canGoNext());
}

void PlayerPresenterTests::rejectedLocalOpenDoesNotStartMetadataOrPublishTrackState() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    backend.load_result = PlaybackCommandResult::NotReady;
    PresenterMetadataReader metadata;
    const std::filesystem::path source{LR"(C:\audio\rejected.wav)"};
    PlayerPresenter presenter(platform, backend, metadata, [source] { return std::vector{source}; });
    QSignalSpy states(&presenter, &PlayerPresenter::stateChanged);
    QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);
    QSignalSpy source_imports(&presenter, &PlayerPresenter::sourceImportChanged);

    QVERIFY(!presenter.openAudio());

    QCOMPARE(backend.loaded_paths, std::vector<std::filesystem::path>{source});
    QVERIFY(metadata.requests.empty());
    QVERIFY(!presenter.hasCurrentTrack());
    QCOMPARE(presenter.currentTitle(), QString{});
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Unloaded);
    QCOMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::Rejected);
    QCOMPARE(presenter.sourceImportIssueCount(), 1ULL);
    QCOMPARE(states.size(), 0);
    QCOMPARE(playback.size(), 0);
    QCOMPARE(source_imports.size(), 1);
}

void PlayerPresenterTests::rejectedAdditionalOpenPreservesTheCurrentLocalQueue() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector<std::vector<std::filesystem::path>> selections{
        {localTestPath(u8"current.wav"), localTestPath(u8"queued.wav")},
        {localTestPath(u8"rejected.wav")},
    };
    std::size_t selection_index = 0;
    PlayerPresenter presenter(platform, backend, metadata, [&] { return selections.at(selection_index++); });
    QAbstractItemModel* const model = presenter.queueModelObject();
    QVERIFY(presenter.openAudio());
    const QString current_id = presenter.currentOccurrenceId();
    const QString queued_id = model->data(model->index(1, 0), QueueListModel::OccurrenceIdRole).toString();
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    QSignalSpy inserted(model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy source_imports(&presenter, &PlayerPresenter::sourceImportChanged);
    backend.load_result = PlaybackCommandResult::Busy;

    QVERIFY(!presenter.openAudio());

    QCOMPARE(backend.loaded_paths.back(), selections.back().front());
    QCOMPARE(model, presenter.queueModelObject());
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(resets.size(), 0);
    QCOMPARE(inserted.size(), 0);
    QCOMPARE(removed.size(), 0);
    QCOMPARE(presenter.currentQueueIndex(), 0);
    QCOMPARE(presenter.currentOccurrenceId(), current_id);
    QCOMPARE(model->data(model->index(1, 0), QueueListModel::OccurrenceIdRole).toString(), queued_id);
    QCOMPARE(presenter.currentTitle(), QStringLiteral("current"));
    QCOMPARE(metadata.requests.size(), std::size_t{1});
    QVERIFY(metadata.cancelled_generations.empty());
    QCOMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::Rejected);
    QCOMPARE(presenter.sourceImportIssueCount(), 1ULL);
    QCOMPARE(source_imports.size(), 1);
}

void PlayerPresenterTests::rejectedAddPreservesTheCurrentLocalQueue() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector<std::vector<std::filesystem::path>> selections{
        {localTestPath(u8"current.wav"), localTestPath(u8"queued.wav")},
        {std::filesystem::path{}},
    };
    std::size_t selection_index = 0;
    PlayerPresenter presenter(platform, backend, metadata, [&] { return selections.at(selection_index++); });
    QAbstractItemModel* const model = presenter.queueModelObject();
    QVERIFY(presenter.openAudio());
    const QString current_id = presenter.currentOccurrenceId();
    const QString queued_id = model->data(model->index(1, 0), QueueListModel::OccurrenceIdRole).toString();
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    QSignalSpy inserted(model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy states(&presenter, &PlayerPresenter::stateChanged);
    QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);
    QSignalSpy source_imports(&presenter, &PlayerPresenter::sourceImportChanged);

    QVERIFY(!presenter.addAudio());

    QCOMPARE(model, presenter.queueModelObject());
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(resets.size(), 0);
    QCOMPARE(inserted.size(), 0);
    QCOMPARE(removed.size(), 0);
    QCOMPARE(presenter.currentQueueIndex(), 0);
    QCOMPARE(presenter.currentOccurrenceId(), current_id);
    QCOMPARE(model->data(model->index(1, 0), QueueListModel::OccurrenceIdRole).toString(), queued_id);
    QCOMPARE(presenter.currentTitle(), QStringLiteral("current"));
    QCOMPARE(metadata.requests.size(), std::size_t{1});
    QVERIFY(metadata.cancelled_generations.empty());
    QCOMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::Rejected);
    QCOMPARE(presenter.sourceImportIssueCount(), 1ULL);
    QCOMPARE(states.size(), 0);
    QCOMPARE(playback.size(), 0);
    QCOMPARE(source_imports.size(), 1);
}

void PlayerPresenterTests::localPlaybackErrorKeepsContextAndOffersReopen() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector<std::filesystem::path> selections{
        std::filesystem::path{LR"(C:\Música\Falha 日本語.mp3)"},
        std::filesystem::path{LR"(C:\Música\Recuperada 日本語.mp3)"},
    };
    std::size_t selection_index = 0;
    PlayerPresenter presenter(platform, backend, metadata, [&]() -> std::vector<std::filesystem::path> {
        if (selection_index >= selections.size())
            return {};
        return {selections[selection_index++]};
    });

    QVERIFY(presenter.openAudio());
    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::InvalidMedia});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Error);

    QCOMPARE(presenter.stateId(), QStringLiteral("error"));
    QVERIFY(presenter.hasError());
    QCOMPARE(presenter.errorCode(), QStringLiteral("invalid_media"));
    QCOMPARE(presenter.errorMessageKey(), QStringLiteral("invalid_media_error"));
    QCOMPARE(presenter.errorActionId(), QStringLiteral("open-audio"));
    QVERIFY(presenter.detailsAvailable());
    QVERIFY(presenter.hasDetailsFileName());
    QCOMPARE(presenter.detailsFileName(), displayPath(selections.front().filename()));
    QVERIFY(presenter.hasDetailsFilePath());
    QCOMPARE(presenter.detailsFilePath(), displayPath(selections.front()));
    const QVariantList local_error_diagnostics{
        QVariantMap{{QStringLiteral("code"), QStringLiteral("playback_error")},
                    {QStringLiteral("errorCode"), QStringLiteral("invalid_media")}},
    };
    QCOMPARE(presenter.detailsDiagnostics(), local_error_diagnostics);
    QVERIFY(presenter.hasCurrentTrack());
    QCOMPARE(presenter.currentTitle(), QStringLiteral("Falha 日本語"));
    QVERIFY(presenter.hasContextTrack());
    QCOMPARE(presenter.contextTitle(), QStringLiteral("Falha 日本語"));
    QCOMPARE(presenter.primaryActionId(), QStringLiteral("open-audio"));

    QVERIFY(presenter.openAudio());
    QCOMPARE(metadata.cancelled_generations, std::vector<MetadataGeneration>{MetadataGeneration{1}});
    QCOMPARE(presenter.currentTitle(), QStringLiteral("Recuperada 日本語"));
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
    QVERIFY(!presenter.hasError());
    QVERIFY(!presenter.hasContextTrack());
    QCOMPARE(presenter.primaryActionId(), QStringLiteral("play-pause"));

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 2});
    QTRY_COMPARE(backend.play_calls, 1);
    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 100,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = 2});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);

    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::InvalidMedia});
    QCoreApplication::processEvents();
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QCOMPARE(presenter.currentTitle(), QStringLiteral("Recuperada 日本語"));
}

void PlayerPresenterTests::typedLocalPlaybackErrorsExposeAnExecutableRecovery_data() {
    QTest::addColumn<int>("playback_error");
    QTest::addColumn<QString>("error_code");
    QTest::addColumn<QString>("message_key");

    QTest::newRow("source unavailable") << static_cast<int>(PlaybackErrorCode::SourceUnavailable)
                                        << QStringLiteral("source_unavailable")
                                        << QStringLiteral("source_unavailable_error");
    QTest::newRow("invalid media") << static_cast<int>(PlaybackErrorCode::InvalidMedia)
                                   << QStringLiteral("invalid_media") << QStringLiteral("invalid_media_error");
    QTest::newRow("access denied") << static_cast<int>(PlaybackErrorCode::AccessDenied)
                                   << QStringLiteral("access_denied") << QStringLiteral("access_denied_error");
    QTest::newRow("backend failure") << static_cast<int>(PlaybackErrorCode::BackendFailure)
                                     << QStringLiteral("backend_failure") << QStringLiteral("backend_failure_error");
}

void PlayerPresenterTests::typedLocalPlaybackErrorsExposeAnExecutableRecovery() {
    QFETCH(int, playback_error);
    QFETCH(QString, error_code);
    QFETCH(QString, message_key);

    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector<std::filesystem::path> selections{
        localTestPath(u8"Falha contextual 日本語.mp3"),
        localTestPath(u8"Recuperada pela ação ☕.flac"),
    };
    std::size_t selection_index = 0;
    PlayerPresenter presenter(platform, backend, metadata, [&]() -> std::vector<std::filesystem::path> {
        if (selection_index >= selections.size())
            return {};
        return {selections[selection_index++]};
    });

    QVERIFY(presenter.openAudio());
    backend.publish(
        {.status = PlaybackStatus::Error, .generation = 1, .error = static_cast<PlaybackErrorCode>(playback_error)});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Error);

    QCOMPARE(presenter.errorCode(), error_code);
    QCOMPARE(presenter.errorMessageKey(), message_key);
    QCOMPARE(presenter.errorActionId(), QStringLiteral("open-audio"));
    QVERIFY(presenter.hasContextTrack());
    QCOMPARE(presenter.contextTitle(), QStringLiteral("Falha contextual 日本語"));
    QVERIFY(presenter.detailsAvailable());
    QCOMPARE(presenter.detailsFilePath(), displayPath(selections.front()));
    const QVariantList expected_diagnostics{
        QVariantMap{{QStringLiteral("code"), QStringLiteral("playback_error")},
                    {QStringLiteral("errorCode"), error_code}},
    };
    QCOMPARE(presenter.detailsDiagnostics(), expected_diagnostics);

    QVERIFY(presenter.executeErrorAction());
    QCOMPARE(backend.loaded_paths, selections);
    QCOMPARE(presenter.currentTitle(), QStringLiteral("Recuperada pela ação ☕"));
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
    QVERIFY(!presenter.hasError());
    QVERIFY(presenter.errorCode().isEmpty());
    QVERIFY(presenter.errorMessageKey().isEmpty());
    QVERIFY(presenter.errorActionId().isEmpty());
}

void PlayerPresenterTests::localEndedAdvancesToNextOccurrenceWithAutoplay() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector<std::filesystem::path> selections{
        localTestPath(u8"Primeira 日本語.mp3"),
        localTestPath(u8"Segunda ☕.mp3"),
    };
    PlayerPresenter presenter(platform, backend, metadata, [selections] { return selections; });
    QAbstractItemModel* const model = presenter.queueModelObject();
    QVERIFY(presenter.openAudio());
    const QString second_id = model->data(model->index(1, 0), QueueListModel::OccurrenceIdRole).toString();
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 1'900,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);

    const PlaybackSnapshot final_snapshot{.status = PlaybackStatus::Paused,
                                          .position_ms = 2'000,
                                          .duration_ms = 2'000,
                                          .seekable = true,
                                          .generation = 1};
    backend.publish(final_snapshot);
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    backend.publishEvent(PlaybackEventType::Ended, final_snapshot);

    QTRY_COMPARE(presenter.currentQueueIndex(), 1);
    QCOMPARE(presenter.currentOccurrenceId(), second_id);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
    QCOMPARE(backend.loaded_paths, selections);
    QCOMPARE(resets.size(), 0);

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 3'000, .seekable = true, .generation = 2});
    QTRY_COMPARE(backend.play_calls, 2);
}

void PlayerPresenterTests::localEndedAtQueueTailKeepsSelection() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::filesystem::path source = localTestPath(u8"Última 日本語.mp3");
    PlayerPresenter presenter(platform, backend, metadata, [source] { return std::vector{source}; });
    QVERIFY(presenter.openAudio());
    const QString occurrence_id = presenter.currentOccurrenceId();

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    const PlaybackSnapshot final_snapshot{.status = PlaybackStatus::Paused,
                                          .position_ms = 2'000,
                                          .duration_ms = 2'000,
                                          .seekable = true,
                                          .generation = 1};
    backend.publish(final_snapshot);
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    backend.publishEvent(PlaybackEventType::Ended, final_snapshot);
    QCoreApplication::processEvents();

    QCOMPARE(presenter.currentQueueIndex(), 0);
    QCOMPARE(presenter.currentOccurrenceId(), occurrence_id);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QCOMPARE(presenter.positionMs(), 2'000);
    QCOMPARE(backend.loaded_paths, std::vector<std::filesystem::path>{source});
    QCOMPARE(backend.play_calls, 1);
    QCOMPARE(backend.unload_calls, 0);
}

void PlayerPresenterTests::localEndedAtQueueTailClearsSelectionWhenConfigured() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::filesystem::path source = localTestPath(u8"Última 日本語.mp3");
    PlayerPresenter presenter(platform, backend, metadata, [source] { return std::vector{source}; });
    QAbstractItemModel* const model = presenter.queueModelObject();
    QVERIFY(presenter.setQueueEndPreference(PlayerPresenter::QueueEndPreference::StopClearSelection));
    QVERIFY(presenter.openAudio());
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    const PlaybackSnapshot final_snapshot{.status = PlaybackStatus::Paused,
                                          .position_ms = 2'000,
                                          .duration_ms = 2'000,
                                          .seekable = true,
                                          .generation = 1};
    backend.publish(final_snapshot);
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    backend.publishEvent(PlaybackEventType::Ended, final_snapshot);

    QTRY_COMPARE(presenter.currentQueueIndex(), -1);
    QVERIFY(presenter.queueLoaded());
    QVERIFY(!presenter.hasCurrentOccurrence());
    QVERIFY(presenter.currentOccurrenceId().isEmpty());
    QVERIFY(!presenter.hasCurrentTrack());
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Unloaded);
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(resets.size(), 0);
    QCOMPARE(backend.loaded_paths, std::vector<std::filesystem::path>{source});
    QCOMPARE(backend.play_calls, 1);
    QCOMPARE(backend.unload_calls, 1);
}

void PlayerPresenterTests::typedLocalErrorMarksOnlyFailedOccurrenceAndContinues_data() {
    QTest::addColumn<int>("error_code");
    QTest::addColumn<QString>("model_error_code");
    QTest::addColumn<bool>("continues");

    QTest::newRow("source unavailable") << static_cast<int>(PlaybackErrorCode::SourceUnavailable)
                                        << QStringLiteral("source_unavailable") << true;
    QTest::newRow("invalid media") << static_cast<int>(PlaybackErrorCode::InvalidMedia)
                                   << QStringLiteral("invalid_media") << true;
    QTest::newRow("access denied") << static_cast<int>(PlaybackErrorCode::AccessDenied)
                                   << QStringLiteral("access_denied") << true;
    QTest::newRow("backend failure") << static_cast<int>(PlaybackErrorCode::BackendFailure) << QString{} << false;
}

void PlayerPresenterTests::typedLocalErrorMarksOnlyFailedOccurrenceAndContinues() {
    QFETCH(int, error_code);
    QFETCH(QString, model_error_code);
    QFETCH(bool, continues);

    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::filesystem::path duplicate = localTestPath(u8"Duplicada 日本語.mp3");
    const std::vector<std::filesystem::path> selections{duplicate, duplicate};
    PlayerPresenter presenter(platform, backend, metadata, [selections] { return selections; });
    QAbstractItemModel* const model = presenter.queueModelObject();
    QVERIFY(presenter.openAudio());
    const QString first_id = presenter.currentOccurrenceId();
    const QString second_id = model->data(model->index(1, 0), QueueListModel::OccurrenceIdRole).toString();
    QVERIFY(first_id != second_id);
    QSignalSpy data_changed(model, &QAbstractItemModel::dataChanged);
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 500,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    backend.publish(
        {.status = PlaybackStatus::Error, .generation = 1, .error = static_cast<PlaybackErrorCode>(error_code)});

    if (!continues) {
        QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Error);
        QCOMPARE(presenter.currentQueueIndex(), 0);
        QCOMPARE(presenter.currentOccurrenceId(), first_id);
        QCOMPARE(backend.loaded_paths, std::vector<std::filesystem::path>{duplicate});
        QCOMPARE(data_changed.size(), 0);
        QVERIFY(model->data(model->index(0, 0), QueueListModel::AvailableRole).toBool());
        QVERIFY(model->data(model->index(1, 0), QueueListModel::AvailableRole).toBool());
        QCOMPARE(resets.size(), 0);
        return;
    }

    QTRY_COMPARE(presenter.currentQueueIndex(), 1);
    QCOMPARE(presenter.currentOccurrenceId(), second_id);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
    QCOMPARE(backend.loaded_paths, selections);
    QCOMPARE(data_changed.size(), 1);
    QVERIFY(!model->data(model->index(0, 0), QueueListModel::AvailableRole).toBool());
    QCOMPARE(model->data(model->index(0, 0), QueueListModel::ErrorCodeRole).toString(), model_error_code);
    QVERIFY(model->data(model->index(1, 0), QueueListModel::AvailableRole).toBool());
    QCOMPARE(model->data(model->index(1, 0), QueueListModel::ErrorCodeRole).toString(), QString{});
    QCOMPARE(resets.size(), 0);

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 2});
    QTRY_COMPARE(backend.play_calls, 2);
}

void PlayerPresenterTests::automaticSourceSkipPublishesContextBeforePlayback_data() {
    QTest::addColumn<int>("error_code");
    QTest::addColumn<QString>("expected_code");
    QTest::newRow("missing") << static_cast<int>(PlaybackErrorCode::SourceUnavailable)
                             << QStringLiteral("source_unavailable");
    QTest::newRow("invalid") << static_cast<int>(PlaybackErrorCode::InvalidMedia) << QStringLiteral("invalid_media");
    QTest::newRow("denied") << static_cast<int>(PlaybackErrorCode::AccessDenied) << QStringLiteral("access_denied");
}

void PlayerPresenterTests::automaticSourceSkipPublishesContextBeforePlayback() {
    QFETCH(int, error_code);
    QFETCH(QString, expected_code);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    metadata.start_result = MetadataCommandResult::Shutdown;
    const std::vector sources{localTestPath(u8"Indisponível 日本語.mp3"), localTestPath(u8"Próxima.wav")};
    PlayerPresenter presenter(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(presenter.openAudio());
    const auto failed_id = presenter.currentOccurrenceId();
    auto* const model = presenter.queueModelObject();
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    QStringList notifications;
    QSignalSpy skipped(&presenter, &PlayerPresenter::occurrenceAutomaticallySkipped);
    connect(&presenter, &PlayerPresenter::occurrenceAutomaticallySkipped, &presenter,
            [&](const QString& title, const QString& code) {
                notifications.append(QStringLiteral("skipped"));
                QCOMPARE(title, QString::fromUtf8("Indisponível 日本語"));
                QCOMPARE(code, expected_code);
                QCOMPARE(presenter.currentQueueIndex(), 1);
                QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
                QVERIFY(presenter.currentOccurrenceId() != failed_id);
            });
    connect(&presenter, &PlayerPresenter::playbackChanged, &presenter,
            [&] { notifications.append(QStringLiteral("playback")); });

    const PlaybackSnapshot failure{
        .status = PlaybackStatus::Error, .generation = 1, .error = static_cast<PlaybackErrorCode>(error_code)};
    backend.publish(failure);
    QTRY_COMPARE(presenter.currentQueueIndex(), 1);
    QCOMPARE(notifications, QStringList({QStringLiteral("skipped"), QStringLiteral("playback")}));
    QCOMPARE(skipped.size(), 1);
    QCOMPARE(backend.loaded_paths, sources);
    QCOMPARE(backend.play_calls, 0);
    QCOMPARE(backend.pause_calls, 0);
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(resets.size(), 0);
    QVERIFY(!model->data(model->index(0, 0), QueueListModel::AvailableRole).toBool());

    backend.publishEvent(PlaybackEventType::SnapshotChanged, failure, false);
    QCoreApplication::processEvents();
    QCOMPARE(skipped.size(), 1);
    QCOMPARE(notifications.size(), 2);
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 2});
    QTRY_COMPARE(backend.play_calls, 1);
    QCOMPARE(skipped.size(), 1);
}

void PlayerPresenterTests::nonAutomaticSourceFailuresDoNotPublishSkippedContext_data() {
    QTest::addColumn<QString>("scenario");
    for (const auto* scenario : {"terminal", "rejected-load", "retry", "restored", "output", "ended", "stale"})
        QTest::newRow(scenario) << QString::fromLatin1(scenario);
}

void PlayerPresenterTests::nonAutomaticSourceFailuresDoNotPublishSkippedContext() {
    QFETCH(QString, scenario);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    metadata.start_result = MetadataCommandResult::Shutdown;
    PresenterPreferencesClock clock;
    PresenterPreferencesWriter preferences_writer;
    PreferencesAutosave preferences_autosave{preferences_writer, {}, {.now = [&clock] { return clock.now(); }}};
    PresenterSessionWriter writer;
    const std::vector sources{localTestPath(u8"Erro.wav"), localTestPath(u8"Sucessora.wav")};
    SessionSnapshot initial;
    if (scenario == QStringLiteral("restored")) {
        initial = {.queue = {.items = {{{1}, sources[0]}, {{2}, sources[1]}},
                             .current_occurrence_id = LocalQueueOccurrenceId{1}},
                   .position_ms = 20'000};
    }
    SessionAutosave autosave{writer, initial, {.now = [&clock] { return clock.now(); }}};
    PlayerPresenter presenter(
        platform, backend, metadata,
        [sources, scenario] { return scenario == QStringLiteral("terminal") ? std::vector{sources[0]} : sources; },
        PlayerPresenterPreferences{.autosave = &preferences_autosave,
                                   .session = {.initial = initial,
                                               .load_status = SessionLoadStatus::Loaded,
                                               .autosave = &autosave,
                                               .now = [&clock] { return clock.now(); }}});
    if (scenario != QStringLiteral("restored"))
        QVERIFY(presenter.openAudio());
    const auto failed_id = presenter.currentOccurrenceId();
    if (scenario == QStringLiteral("retry")) {
        backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::InvalidMedia});
        QTRY_COMPARE(presenter.currentQueueIndex(), 1);
        QVERIFY(presenter.retryQueueOccurrence(failed_id));
    }
    if (scenario == QStringLiteral("rejected-load"))
        backend.load_result = PlaybackCommandResult::Busy;
    QSignalSpy skipped(&presenter, &PlayerPresenter::occurrenceAutomaticallySkipped);
    const auto generation = backend.snapshot().generation;
    if (scenario == QStringLiteral("ended")) {
        backend.publishEvent(PlaybackEventType::Ended, {.status = PlaybackStatus::Paused, .generation = generation});
    } else {
        backend.publishEvent(PlaybackEventType::SnapshotChanged,
                             {.status = PlaybackStatus::Error,
                              .generation = scenario == QStringLiteral("stale") ? 0 : generation,
                              .error = scenario == QStringLiteral("output") ? PlaybackErrorCode::AudioOutputUnavailable
                                                                            : PlaybackErrorCode::InvalidMedia},
                             scenario != QStringLiteral("stale"));
    }
    QCoreApplication::processEvents();
    QCOMPARE(skipped.size(), 0);
    QCOMPARE(presenter.currentQueueIndex(), scenario == QStringLiteral("ended") ? 1 : 0);
}

void PlayerPresenterTests::automaticSourceSkipObserversMayDestroyThePresenter_data() {
    QTest::addColumn<int>("notification");
    QTest::newRow("skipped") << 0;
    QTest::newRow("state") << 1;
    QTest::newRow("playback") << 2;
}

void PlayerPresenterTests::automaticSourceSkipObserversMayDestroyThePresenter() {
    QFETCH(int, notification);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    metadata.start_result = MetadataCommandResult::Shutdown;
    const std::vector sources{localTestPath(u8"Falha.wav"), localTestPath(u8"Destino.wav")};
    auto presenter = std::make_unique<PlayerPresenter>(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(presenter->openAudio());
    int notifications = 0;
    const auto observe = [&] {
        if (notifications++ == notification)
            presenter.reset();
    };
    connect(presenter.get(), &PlayerPresenter::occurrenceAutomaticallySkipped, this, observe);
    connect(presenter.get(), &PlayerPresenter::stateChanged, this, observe);
    connect(presenter.get(), &PlayerPresenter::playbackChanged, this, observe);

    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::InvalidMedia});
    QTRY_VERIFY(!presenter);
    QCOMPARE(notifications, notification + 1);
    QVERIFY(!backend.callbackInstalled());
}

void PlayerPresenterTests::automaticSourceSkipDoesNotPublishObsoleteNotificationsAfterSelection() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    metadata.start_result = MetadataCommandResult::Shutdown;
    const std::vector sources{localTestPath(u8"Falha.wav"), localTestPath(u8"Destino.wav"),
                              localTestPath(u8"Escolha explícita.wav")};
    PlayerPresenter presenter(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(presenter.openAudio());
    const auto explicit_id = presenter.queueModelObject()
                                 ->data(presenter.queueModelObject()->index(2, 0), QueueListModel::OccurrenceIdRole)
                                 .toString();
    QSignalSpy state(&presenter, &PlayerPresenter::stateChanged);
    QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);
    QSignalSpy skipped(&presenter, &PlayerPresenter::occurrenceAutomaticallySkipped);
    connect(&presenter, &PlayerPresenter::occurrenceAutomaticallySkipped, &presenter,
            [&] { QVERIFY(presenter.playQueueOccurrence(explicit_id)); });

    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::InvalidMedia});
    QTRY_COMPARE(presenter.currentQueueIndex(), 2);
    QCOMPARE(skipped.size(), 1);
    QCOMPARE(state.size(), 1);
    QCOMPARE(playback.size(), 1);
    QCOMPARE(presenter.currentOccurrenceId(), explicit_id);
    QCOMPARE(backend.loaded_paths, sources);
}

void PlayerPresenterTests::automaticSourceSkipKeepsImportNotificationsIndependent_data() {
    QTest::addColumn<int>("notification");
    QTest::addColumn<bool>("replace_import");
    for (int notification = 0; notification < 3; ++notification) {
        const auto stage = QByteArray::number(notification);
        QTest::newRow(("select-after-" + stage).constData()) << notification << false;
        QTest::newRow(("replace-import-after-" + stage).constData()) << notification << true;
    }
}

void PlayerPresenterTests::automaticSourceSkipKeepsImportNotificationsIndependent() {
    QFETCH(int, notification);
    QFETCH(bool, replace_import);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    metadata.start_result = MetadataCommandResult::Shutdown;
    FakeLocalSourceDiscovery discovery;
    const std::vector sources{localTestPath(u8"Falha.wav"), localTestPath(u8"Destino.wav"),
                              localTestPath(u8"Escolha explícita.wav")};
    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; }, discovery);
    QVERIFY(presenter.importDroppedUrls({localTestUrl(localTestPath(u8"Importação original"))}, 0));
    const auto generation = discovery.requests.back().generation;
    discovery.publishBatch(generation, 1, sources, {}, 3);
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    discovery.publishCompleted({.inspected_count = 3, .accepted_count = 3});
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QCOMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::Completed);
    const auto explicit_id = presenter.queueModelObject()
                                 ->data(presenter.queueModelObject()->index(2, 0), QueueListModel::OccurrenceIdRole)
                                 .toString();
    QSignalSpy imports(&presenter, &PlayerPresenter::sourceImportChanged);
    const auto reenter = [&] {
        if (replace_import) {
            QVERIFY(presenter.importDroppedUrls({localTestUrl(localTestPath(u8"Nova importação"))}, 3));
        } else {
            QVERIFY(presenter.playQueueOccurrence(explicit_id));
        }
    };
    if (notification == 0) {
        connect(&presenter, &PlayerPresenter::occurrenceAutomaticallySkipped, &presenter, reenter,
                Qt::SingleShotConnection);
    } else if (notification == 1) {
        connect(&presenter, &PlayerPresenter::stateChanged, &presenter, reenter, Qt::SingleShotConnection);
    } else {
        connect(&presenter, &PlayerPresenter::playbackChanged, &presenter, reenter, Qt::SingleShotConnection);
    }

    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::InvalidMedia});
    QCoreApplication::processEvents();
    QCOMPARE(presenter.currentQueueIndex(), replace_import ? 1 : 2);
    QCOMPARE(presenter.sourceImportState(), replace_import ? PlayerPresenter::SourceImportState::Discovering
                                                           : PlayerPresenter::SourceImportState::CompletedWithIssues);
    QCOMPARE(presenter.sourceImportIssueCount(), replace_import ? 0ULL : 1ULL);
    QCOMPARE(imports.size(), 1);
    if (replace_import)
        QVERIFY(discovery.requests.back().generation != generation);
    else
        QCOMPARE(discovery.requests.back().generation, generation);
}

void PlayerPresenterTests::outputLossDuringRecoveryPreservesThePersistedTarget_data() {
    QTest::addColumn<bool>("during_pause");
    QTest::newRow("pause") << true;
    QTest::newRow("seek") << false;
}

void PlayerPresenterTests::outputLossDuringRecoveryPreservesThePersistedTarget() {
    QFETCH(bool, during_pause);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterSessionWriter writer;
    PresenterPreferencesClock clock;
    PresenterPreferencesWriter preferences_writer;
    PreferencesAutosave preferences_autosave{
        preferences_writer, Preferences{}, {.now = [&clock] { return clock.now(); }}};
    const SessionSnapshot persisted = persistedSessionSnapshot();
    SessionAutosave autosave{writer, persisted, {.now = [&clock] { return clock.now(); }}};
    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
        PlayerPresenterPreferences{.autosave = &preferences_autosave,
                                   .session = {.initial = persisted,
                                               .load_status = SessionLoadStatus::Loaded,
                                               .autosave = &autosave,
                                               .now = [&clock] { return clock.now(); }}});
    backend.publish({.status = PlaybackStatus::Error,
                     .position_ms = 42'000,
                     .duration_ms = 60'000,
                     .generation = 1,
                     .error = PlaybackErrorCode::AudioOutputUnavailable});
    QTRY_VERIFY(presenter.hasError());
    QVERIFY(presenter.executeErrorAction());
    const auto lose_output = [&] {
        backend.publish({.status = PlaybackStatus::Error,
                         .duration_ms = 60'000,
                         .generation = 2,
                         .error = PlaybackErrorCode::AudioOutputUnavailable});
        backend.seek_result = PlaybackCommandResult::NotReady;
        backend.pause_result = PlaybackCommandResult::NotReady;
    };
    if (during_pause)
        backend.on_pause = lose_output;
    else
        backend.on_seek = lose_output;
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 60'000, .seekable = true, .generation = 2});
    QTRY_VERIFY(presenter.hasError());
    autosave.scheduleRepair();
    QCOMPARE(autosave.flush(), SessionSaveStatus::Saved);
    QVERIFY(!writer.flushes.empty());
    QCOMPARE(writer.flushes.back().snapshot.position_ms, persisted.position_ms);
    QCOMPARE(writer.flushes.back().snapshot.queue, persisted.queue);
    backend.on_pause = {};
    backend.on_seek = {};
    backend.pause_result = PlaybackCommandResult::Accepted;
    backend.seek_result = PlaybackCommandResult::Accepted;
    backend.seek_positions.clear();
    QVERIFY(presenter.executeErrorAction());
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 60'000, .seekable = true, .generation = 3});
    QTRY_COMPARE(backend.seek_positions, (std::vector<std::int64_t>{42'000}));
    QCOMPARE(backend.play_calls, 0);
}

void PlayerPresenterTests::audioOutputRecoveryPreservesOccurrenceAndPositionWithoutAutoplay_data() {
    QTest::addColumn<PlaybackErrorCode>("error");
    QTest::newRow("disconnected") << PlaybackErrorCode::AudioOutputUnavailable;
    QTest::newRow("default-changed") << PlaybackErrorCode::AudioOutputChanged;
}

void PlayerPresenterTests::audioOutputRecoveryPreservesOccurrenceAndPositionWithoutAutoplay() {
    QFETCH(PlaybackErrorCode, error);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const auto source = localTestPath(u8"Saída 日本語.wav");
    PlayerPresenter presenter(platform, backend, metadata, [source] { return std::vector{source, source}; });
    QVERIFY(presenter.openAudio());
    const QString id = presenter.currentOccurrenceId();
    auto* const model = presenter.queueModelObject();
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 750,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.confirmedPositionMs(), qint64{750});
    backend.publish(
        {.status = PlaybackStatus::Error, .position_ms = 750, .duration_ms = 2'000, .generation = 1, .error = error});
    QTRY_VERIFY(presenter.hasError());
    QCOMPARE(presenter.currentOccurrenceId(), id);
    QCOMPARE(presenter.errorActionId(), QStringLiteral("recover-audio-output"));
    QVERIFY(!presenter.canRetryCurrentOccurrence());
    QVERIFY(model->data(model->index(0, 0), QueueListModel::AvailableRole).toBool());
    QVERIFY(!model->data(model->index(0, 0), QueueListModel::ErrorCodeRole).isValid());
    QCOMPARE(backend.loaded_paths, (std::vector{source}));
    QVERIFY(presenter.executeErrorAction());
    QVERIFY(!presenter.executeErrorAction());
    QCOMPARE(backend.loaded_paths, (std::vector{source, source}));
    QCOMPARE(presenter.currentOccurrenceId(), id);
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 2});
    QTRY_COMPARE(backend.seek_positions, (std::vector<std::int64_t>{750}));
    QCOMPARE(backend.pause_calls, 1);
    QCOMPARE(backend.play_calls, 0);
    backend.publish({.status = PlaybackStatus::Paused,
                     .position_ms = 750,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = 2});
    QTRY_COMPARE(presenter.confirmedPositionMs(), qint64{750});
    QVERIFY(!presenter.hasError());
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(resets.size(), 0);
}

void PlayerPresenterTests::manualAudioOutputSwitchRestoresPausedAndTopologyAloneNeverResumes() {
    AvailableTrayPlatform platform;
    PresenterAudioOutputBackend backend;
    PresenterMetadataReader metadata;
    const AudioOutputDevice speakers{"output-a", "Speakers"};
    const AudioOutputDevice headphones{"output-b", "Headphones"};
    backend.setOutputs({speakers, headphones});
    const auto source = localTestPath(u8"Saída manual.wav");
    PlayerPresenter presenter(platform, backend, metadata, [source] { return std::vector{source, source}; });
    QVERIFY(presenter.openAudio());
    const auto occurrence = presenter.currentOccurrenceId();
    auto* const model = presenter.queueModelObject();
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 750,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.confirmedPositionMs(), qint64{750});
    QVERIFY(presenter.property("audioOutputSelectionSupported").toBool());
    const auto devices = presenter.property("audioOutputDevices").toList();
    QVERIFY(std::ranges::any_of(devices, [](const QVariant& item) {
        const auto row = item.toMap();
        return row.value("id").toString() == "output-b" && row.value("label").toString() == "Headphones";
    }));
    QVERIFY(presenter.property("selectedAudioOutputId").toString().isEmpty());
    QSignalSpy output_changes(&presenter, SIGNAL(audioOutputsChanged()));
    QVERIFY(output_changes.isValid());

    bool accepted{};
    QVERIFY(QMetaObject::invokeMethod(&presenter, "selectAudioOutput", Qt::DirectConnection,
                                      Q_RETURN_ARG(bool, accepted), Q_ARG(QString, QStringLiteral("stale"))));
    QVERIFY(!accepted);
    QVERIFY(presenter.property("selectedAudioOutputId").toString().isEmpty());
    QCOMPARE(backend.loaded_paths, (std::vector{source}));
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);

    QVERIFY(QMetaObject::invokeMethod(&presenter, "selectAudioOutput", Qt::DirectConnection,
                                      Q_RETURN_ARG(bool, accepted), Q_ARG(QString, QStringLiteral("output-b"))));
    QVERIFY(accepted);
    QCOMPARE(presenter.property("selectedAudioOutputId").toString(), QStringLiteral("output-b"));
    QTRY_COMPARE(backend.loaded_paths, (std::vector{source, source}));
    QCOMPARE(presenter.currentOccurrenceId(), occurrence);
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 2});
    QTRY_COMPARE(backend.seek_positions, (std::vector<std::int64_t>{750}));
    QCOMPARE(backend.pause_calls, 1);
    QCOMPARE(backend.play_calls, 0);
    backend.publish({.status = PlaybackStatus::Paused,
                     .position_ms = 750,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = 2});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QCOMPARE(presenter.confirmedPositionMs(), qint64{750});

    backend.interruptOutput(PlaybackErrorCode::AudioOutputUnavailable);
    backend.setOutputs({speakers});
    QTRY_VERIFY(presenter.hasError());
    QCOMPARE(presenter.property("selectedAudioOutputId").toString(), QStringLiteral("output-b"));
    output_changes.clear();
    backend.setOutputs({speakers, headphones});
    QTRY_VERIFY(!output_changes.isEmpty());
    QVERIFY(presenter.hasError());
    QCOMPARE(presenter.errorActionId(), QStringLiteral("recover-audio-output"));
    QCOMPARE(backend.loaded_paths, (std::vector{source, source}));
    QCOMPARE(backend.play_calls, 0);
    QCOMPARE(presenter.currentOccurrenceId(), occurrence);
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(resets.size(), 0);
}

void PlayerPresenterTests::sourceVolumeRemovalKeepsTheOccurrenceAndRecoversPaused_data() {
    QTest::addColumn<bool>("locate");
    QTest::newRow("retry after still unavailable") << false;
    QTest::newRow("locate replacement") << true;
}

void PlayerPresenterTests::sourceVolumeRemovalKeepsTheOccurrenceAndRecoversPaused() {
    QFETCH(bool, locate);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterFileLocator locator;
    const auto source = localTestPath(u8"Unidade removida.wav");
    const auto replacement = std::filesystem::absolute(localTestPath(u8"Áudio recuperado.wav"));
    PlayerPresenter presenter(platform, backend, metadata, [source] { return std::vector{source, source}; });
    presenter.setAudioFileLocator(&locator);
    QVERIFY(presenter.openAudio());
    const auto occurrence = presenter.currentOccurrenceId();
    auto* const model = presenter.queueModelObject();
    QSignalSpy skipped(&presenter, &PlayerPresenter::occurrenceAutomaticallySkipped);
    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 750,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.confirmedPositionMs(), qint64{750});
    const PlaybackSnapshot removed{
        .status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::SourceVolumeRemoved};
    backend.publish(removed);
    QTRY_VERIFY(presenter.canRetryCurrentOccurrence());
    QVERIFY(presenter.canLocateCurrentOccurrence());
    QCOMPARE(presenter.currentOccurrenceId(), occurrence);
    QCOMPARE(presenter.errorCode(), QStringLiteral("source_volume_removed"));
    QCOMPARE(presenter.errorMessageKey(), QStringLiteral("source_volume_removed_error"));
    QCOMPARE(presenter.confirmedPositionMs(), qint64{750});
    QCOMPARE(model->data(model->index(0, 0), QueueListModel::ErrorCodeRole).toString(),
             QStringLiteral("source_volume_removed"));
    QVERIFY(!model->data(model->index(0, 0), QueueListModel::AvailableRole).toBool());
    QVERIFY(model->data(model->index(1, 0), QueueListModel::AvailableRole).toBool());
    backend.publishEvent(PlaybackEventType::SnapshotChanged,
                         {.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 1},
                         false);
    backend.publishEvent(PlaybackEventType::Ended,
                         {.status = PlaybackStatus::Ready, .position_ms = 2'000, .duration_ms = 2'000, .generation = 1},
                         false);
    QCoreApplication::processEvents();
    QCOMPARE(presenter.errorCode(), QStringLiteral("source_volume_removed"));
    QCOMPARE(backend.loaded_paths, std::vector{source});
    QCOMPARE(skipped.count(), 0);

    std::uint64_t recovery_generation = 2;
    if (locate) {
        QVERIFY(presenter.locateQueueOccurrence(occurrence));
        QCOMPARE(locator.requests, std::vector{source});
        locator.complete({AudioFileLocationStatus::Selected, replacement});
        QTRY_COMPARE(backend.loaded_paths, (std::vector{source, replacement}));
    } else {
        backend.accepted_load_status = PlaybackStatus::Error;
        QVERIFY(presenter.retryQueueOccurrence(occurrence));
        QVERIFY(presenter.canRetryCurrentOccurrence());
        backend.accepted_load_status.reset();
        QVERIFY(presenter.retryQueueOccurrence(occurrence));
        QCOMPARE(backend.loaded_paths, (std::vector{source, source, source}));
        recovery_generation = 3;
    }
    backend.publish(
        {.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = recovery_generation});
    QTRY_COMPARE(backend.seek_positions, (std::vector<std::int64_t>{750}));
    QCOMPARE(backend.pause_calls, 1);
    backend.publish({.status = PlaybackStatus::Paused,
                     .position_ms = 750,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = recovery_generation});
    QTRY_COMPARE(presenter.confirmedPositionMs(), qint64{750});
    backend.publishEvent(PlaybackEventType::SnapshotChanged, removed, false);
    QCoreApplication::processEvents();
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QCOMPARE(presenter.currentOccurrenceId(), occurrence);
    QVERIFY(model->data(model->index(0, 0), QueueListModel::AvailableRole).toBool());
    QCOMPARE(backend.play_calls, 0);
}

void PlayerPresenterTests::observedSourceVolumeRemovalSupersedesQueuedTerminalDecoderEvents() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const auto source = localTestPath(u8"Unidade removida.wav");
    const auto next = localTestPath(u8"Próxima faixa.wav");
    PlayerPresenter presenter(platform, backend, metadata, [source, next] { return std::vector{source, next}; });
    QVERIFY(presenter.openAudio());
    const auto occurrence = presenter.currentOccurrenceId();
    QSignalSpy skipped(&presenter, &PlayerPresenter::occurrenceAutomaticallySkipped);
    const PlaybackSnapshot playing{
        .status = PlaybackStatus::Playing, .position_ms = 750, .duration_ms = 2'000, .seekable = true, .generation = 1};
    backend.publish(playing);
    QTRY_COMPARE(presenter.confirmedPositionMs(), qint64{750});

    auto latest = playing;
    latest.position_ms = 900;
    backend.publish(latest);
    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::SourceUnavailable});
    backend.publishEvent(PlaybackEventType::SnapshotChanged, {.status = PlaybackStatus::Empty, .generation = 1}, false);
    backend.publishEvent(PlaybackEventType::Ended, {.status = PlaybackStatus::Paused, .generation = 1}, false);
    backend.observed_source_volume_removal = 1;
    // The removal has been received, but its own event has yet to be posted.
    QCoreApplication::sendPostedEvents(&presenter, QEvent::MetaCall);
    QCOMPARE(presenter.currentOccurrenceId(), occurrence);
    QCOMPARE(presenter.confirmedPositionMs(), qint64{900});
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QCOMPARE(backend.loaded_paths, std::vector{source});

    backend.publish(
        {.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::SourceVolumeRemoved});
    QTRY_VERIFY(presenter.canRetryCurrentOccurrence());
    QCOMPARE(presenter.errorCode(), QStringLiteral("source_volume_removed"));
    QCOMPARE(presenter.currentOccurrenceId(), occurrence);
    QCOMPARE(presenter.confirmedPositionMs(), qint64{900});
    QCOMPARE(backend.loaded_paths, std::vector{source});
    QCOMPARE(skipped.count(), 0);
}

void PlayerPresenterTests::sourceVolumeRemovalDoesNotAutoplayLaterDiscoveryBatches() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    FakeLocalSourceDiscovery discovery;
    const auto removed = localTestPath(u8"Unidade removida.wav");
    const auto later = localTestPath(u8"Próxima faixa.wav");
    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; }, discovery);
    QVERIFY(presenter.importDroppedUrls({localTestUrl(localTestPath(u8"pasta"))}, 0));
    const auto generation = discovery.requests.front().generation;
    discovery.publishBatch(generation, 1, {removed}, {}, 1);
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QCOMPARE(backend.loaded_paths, std::vector{removed});
    const auto occurrence = presenter.currentOccurrenceId();
    backend.publish(
        {.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::SourceVolumeRemoved});
    QTRY_VERIFY(presenter.canRetryCurrentOccurrence());
    discovery.publishBatch(generation, 2, {later}, {}, 1);
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QCOMPARE(backend.loaded_paths, std::vector{removed});
    QCOMPARE(presenter.currentOccurrenceId(), occurrence);
    QCOMPARE(presenter.sourceImportIssueCount(), 1ULL);
    QCOMPARE(presenter.queueModelObject()->rowCount(), 2);
    QCOMPARE(backend.play_calls, 0);
}

void PlayerPresenterTests::repeatedAudioOutputRecoveryRetainsPositionAndRejectsBusyLoads() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const auto source = localTestPath(u8"Saída.wav");
    PlayerPresenter presenter(platform, backend, metadata, [source] { return std::vector{source, source}; });
    QVERIFY(presenter.openAudio());
    backend.publish({.status = PlaybackStatus::Error,
                     .position_ms = 750,
                     .duration_ms = 2'000,
                     .generation = 1,
                     .error = PlaybackErrorCode::AudioOutputUnavailable});
    QTRY_VERIFY(presenter.hasError());
    backend.load_result = PlaybackCommandResult::Busy;
    QVERIFY(!presenter.executeErrorAction());
    QCOMPARE(presenter.confirmedPositionMs(), qint64{750});
    QCOMPARE(presenter.currentQueueIndex(), 0);
    backend.load_result = PlaybackCommandResult::Accepted;
    backend.accepted_load_status = PlaybackStatus::Error;
    backend.accepted_load_error = PlaybackErrorCode::AudioOutputUnavailable;
    QVERIFY(presenter.executeErrorAction());
    QVERIFY(presenter.hasError());
    QCOMPARE(presenter.currentQueueIndex(), 0);
    QCOMPARE(presenter.errorActionId(), QStringLiteral("recover-audio-output"));
    backend.accepted_load_status = PlaybackStatus::Ready;
    QVERIFY(presenter.executeErrorAction());
    backend.publish({.status = PlaybackStatus::Paused, .duration_ms = 2'000, .seekable = true, .generation = 3});
    QTRY_COMPARE(backend.seek_positions, (std::vector<std::int64_t>{750}));
    QCOMPARE(backend.play_calls, 0);
    QCOMPARE(presenter.currentQueueIndex(), 0);
}

void PlayerPresenterTests::supersededAudioOutputRecoveryCannotSeekTheNewSelection() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const auto source = localTestPath(u8"Saída.wav");
    PlayerPresenter presenter(platform, backend, metadata, [source] { return std::vector{source, source}; });
    QVERIFY(presenter.openAudio());
    backend.publish({.status = PlaybackStatus::Error,
                     .position_ms = 750,
                     .duration_ms = 2'000,
                     .generation = 1,
                     .error = PlaybackErrorCode::AudioOutputChanged});
    QTRY_VERIFY(presenter.hasError());
    QVERIFY(presenter.executeErrorAction());
    auto* const model = presenter.queueModelObject();
    const QString next = model->data(model->index(1, 0), QueueListModel::OccurrenceIdRole).toString();
    QVERIFY(presenter.playQueueOccurrence(next));
    backend.publishEvent(PlaybackEventType::SnapshotChanged,
                         {.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 2},
                         false);
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 3});
    QTRY_COMPARE(backend.play_calls, 1);
    QCOMPARE(presenter.currentOccurrenceId(), next);
    QVERIFY(backend.seek_positions.empty());
    QCOMPARE(backend.pause_calls, 0);
}

void PlayerPresenterTests::locatesCurrentMissingFileAndRecoversPausedAtTheConfirmedPosition() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterFileLocator locator;
    const auto source = localTestPath(u8"Antiga 日本語.wav");
    const auto replacement = std::filesystem::absolute(localTestPath(u8"Nova pasta/Renomeada 日本語.wav"));
    PlayerPresenter presenter(platform, backend, metadata, [source] { return std::vector{source}; });
    QVERIFY(!presenter.fileLocationAvailable());
    presenter.setAudioFileLocator(&locator);
    QVERIFY(presenter.fileLocationAvailable());
    QVERIFY(presenter.openAudio());
    backend.publish({.status = PlaybackStatus::Error,
                     .position_ms = 42'000,
                     .duration_ms = 180'000,
                     .generation = 1,
                     .error = PlaybackErrorCode::SourceUnavailable});
    QTRY_VERIFY(presenter.canLocateCurrentOccurrence());
    const QString id = presenter.currentOccurrenceId();
    auto* model = presenter.queueModelObject();
    const QPersistentModelIndex row(model->index(0, 0));
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    QSignalSpy finished(&presenter, &PlayerPresenter::fileLocationFinished);
    QVERIFY(presenter.locateQueueOccurrence(id));
    QCOMPARE(presenter.locatingOccurrenceId(), id);
    QVERIFY(!presenter.locateQueueOccurrence(id));
    QCOMPARE(locator.requests, std::vector{source});
    locator.complete({AudioFileLocationStatus::Selected, replacement});
    QTRY_COMPARE(backend.loaded_paths, (std::vector{source, replacement}));
    QCOMPARE(presenter.currentOccurrenceId(), id);
    QVERIFY(row.isValid());
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(resets.count(), 0);
    QVERIFY(!model->data(row, QueueListModel::AvailableRole).toBool());
    QCOMPARE(model->data(row, QueueListModel::FileNameRole).toString(), QStringLiteral("Renomeada 日本語.wav"));
    QCOMPARE(metadata.requests.back().source_path, replacement);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(1).toBool(), true);
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 180'000, .seekable = true, .generation = 2});
    QTRY_COMPARE(backend.pause_calls, 1);
    QCOMPARE(backend.play_calls, 0);
    backend.publish({.status = PlaybackStatus::Paused, .duration_ms = 180'000, .seekable = true, .generation = 2});
    QTRY_COMPARE(backend.seek_positions, (std::vector<std::int64_t>{42'000}));
    QCOMPARE(backend.play_calls, 0);
    QVERIFY(model->data(row, QueueListModel::AvailableRole).toBool());
    QVERIFY(presenter.retryingOccurrenceId().isEmpty());
    backend.publishEvent(
        PlaybackEventType::SnapshotChanged,
        {.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::SourceUnavailable}, false);
    QCoreApplication::processEvents();
    QVERIFY(!presenter.hasError());
}

void PlayerPresenterTests::locatesOnlyTheChosenOccurrenceWithoutInterruptingAnotherTrack() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterFileLocator locator;
    const auto source = localTestPath(u8"Duplicada.wav");
    const auto replacement = std::filesystem::absolute(localTestPath(u8"Outra pasta/Duplicada.wav"));
    PlayerPresenter presenter(platform, backend, metadata, [source] { return std::vector{source, source, source}; });
    presenter.setAudioFileLocator(&locator);
    QVERIFY(presenter.openAudio());
    const QString missing = presenter.currentOccurrenceId();
    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::SourceUnavailable});
    QTRY_COMPARE(presenter.currentQueueIndex(), 1);
    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 500,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = 2});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    const QString current = presenter.currentOccurrenceId();
    const auto before = backend.snapshot();
    const auto loads = backend.loaded_paths;
    const auto metadata_count = metadata.requests.size();
    const auto plays = backend.play_calls;
    const auto pauses = backend.pause_calls;
    auto* model = presenter.queueModelObject();
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    QVERIFY(presenter.locateQueueOccurrence(missing));
    QVERIFY(presenter.moveQueueOccurrence(missing, 2));
    locator.complete({AudioFileLocationStatus::Selected, replacement});
    QTRY_VERIFY(presenter.locatingOccurrenceId().isEmpty());
    QCOMPARE(presenter.currentOccurrenceId(), current);
    QCOMPARE(backend.snapshot(), before);
    QCOMPARE(backend.loaded_paths, loads);
    QCOMPARE(metadata.requests.size(), metadata_count);
    QCOMPARE(backend.play_calls, plays);
    QCOMPARE(backend.pause_calls, pauses);
    QCOMPARE(resets.count(), 0);
    const auto changed = model->index(2, 0);
    QCOMPARE(model->data(changed, QueueListModel::OccurrenceIdRole).toString(), missing);
    QVERIFY(!model->data(changed, QueueListModel::AvailableRole).toBool());
    QCOMPARE(model->data(changed, QueueListModel::ErrorCodeRole).toString(), QStringLiteral("source_relocated"));
    QVERIFY(model->data(model->index(1, 0), QueueListModel::AvailableRole).toBool());
    QVERIFY(presenter.retryQueueOccurrence(missing));
    QCOMPARE(backend.loaded_paths.back(), replacement);
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 3});
    QTRY_VERIFY(model->data(changed, QueueListModel::AvailableRole).toBool());
}

void PlayerPresenterTests::fileLocationCancellationRemovalAndDuplicateCompletionAreInert() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterFileLocator locator;
    const auto source = localTestPath(u8"Ausente.wav");
    PlayerPresenter presenter(platform, backend, metadata, [source] { return std::vector{source}; });
    presenter.setAudioFileLocator(&locator);
    QVERIFY(presenter.openAudio());
    const QString id = presenter.currentOccurrenceId();
    QVERIFY(!presenter.locateQueueOccurrence(id));
    QVERIFY(!presenter.locateQueueOccurrence(QStringLiteral("invalid-id")));
    QVERIFY(locator.requests.empty());
    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::SourceUnavailable});
    QTRY_VERIFY(presenter.hasError());
    QVERIFY(presenter.locateQueueOccurrence(id));
    const auto late = locator.pending;
    locator.complete({AudioFileLocationStatus::Cancelled, {}});
    QTRY_VERIFY(presenter.locatingOccurrenceId().isEmpty());
    QVERIFY(presenter.fileLocationErrorOccurrenceId().isEmpty());
    late({AudioFileLocationStatus::Selected, localTestPath(u8"Ignorada.wav")});
    QCoreApplication::processEvents();
    QCOMPARE(backend.loaded_paths, std::vector{source});
    QVERIFY(presenter.locateQueueOccurrence(id));
    QVERIFY(presenter.removeQueueOccurrence(id));
    locator.complete({AudioFileLocationStatus::Selected, localTestPath(u8"Removida.wav")});
    QTRY_VERIFY(presenter.locatingOccurrenceId().isEmpty());
    QCOMPARE(presenter.queueModelObject()->rowCount(), 0);
    QCOMPARE(backend.loaded_paths, std::vector{source});
}

void PlayerPresenterTests::failedFileLocationPreservesThePreviousSourceAndRemainsActionable() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterFileLocator locator;
    const auto source = localTestPath(u8"Original.wav");
    const auto replacement = std::filesystem::absolute(localTestPath(u8"Recusada.wav"));
    PlayerPresenter presenter(platform, backend, metadata, [source] { return std::vector{source}; });
    presenter.setAudioFileLocator(&locator);
    QVERIFY(presenter.openAudio());
    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::SourceUnavailable});
    QTRY_VERIFY(presenter.hasError());
    const QString id = presenter.currentOccurrenceId();
    locator.accept = false;
    QVERIFY(!presenter.locateQueueOccurrence(id));
    QCOMPARE(presenter.fileLocationErrorOccurrenceId(), id);
    QVERIFY(presenter.locatingOccurrenceId().isEmpty());
    locator.accept = true;
    QVERIFY(presenter.locateQueueOccurrence(id));
    backend.load_result = PlaybackCommandResult::Busy;
    locator.complete({AudioFileLocationStatus::Selected, replacement});
    QTRY_COMPARE(presenter.fileLocationErrorOccurrenceId(), id);
    QVERIFY(presenter.canLocateCurrentOccurrence());
    QCOMPARE(presenter.currentOccurrenceId(), id);
    QCOMPARE(presenter.queueModelObject()
                 ->data(presenter.queueModelObject()->index(0, 0), QueueListModel::FileNameRole)
                 .toString(),
             QStringLiteral("Original.wav"));
    QVERIFY(presenter.locateQueueOccurrence(id));
    QCOMPARE(locator.requests.back(), source);
    locator.complete({AudioFileLocationStatus::Failed, {}});
    QTRY_COMPARE(presenter.fileLocationErrorOccurrenceId(), id);
    backend.load_result = PlaybackCommandResult::Accepted;
    QVERIFY(presenter.retryQueueOccurrence(id));
    QVERIFY(presenter.fileLocationErrorOccurrenceId().isEmpty());
}

void PlayerPresenterTests::fileLocationCompletionCannotOutliveExitOrThePresenter() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterFileLocator locator;
    const auto source = localTestPath(u8"Saída.wav");
    auto presenter =
        std::make_unique<PlayerPresenter>(platform, backend, metadata, [source] { return std::vector{source}; });
    presenter->setAudioFileLocator(&locator);
    QVERIFY(presenter->openAudio());
    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::SourceUnavailable});
    QTRY_VERIFY(presenter->hasError());
    QVERIFY(presenter->locateQueueOccurrence(presenter->currentOccurrenceId()));
    const auto late = locator.pending;
    const int cancellations = locator.cancels;
    QVERIFY(presenter->requestExit());
    QVERIFY(locator.cancels > cancellations);
    late({AudioFileLocationStatus::Selected, localTestPath(u8"Depois.wav")});
    QCoreApplication::processEvents();
    QCOMPARE(backend.loaded_paths, std::vector{source});
    presenter.reset();
    late({AudioFileLocationStatus::Selected, localTestPath(u8"Destruída.wav")});
    QCoreApplication::processEvents();
    QCOMPARE(backend.loaded_paths, std::vector{source});
}

void PlayerPresenterTests::synchronousFileLocationCompletionIsDeferredAndSingleShot() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterFileLocator locator;
    const auto source = localTestPath(u8"Antes.wav");
    const auto replacement = std::filesystem::absolute(localTestPath(u8"Depois.wav"));
    PlayerPresenter presenter(platform, backend, metadata, [source] { return std::vector{source}; });
    presenter.setAudioFileLocator(&locator);
    QVERIFY(presenter.openAudio());
    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::SourceUnavailable});
    QTRY_VERIFY(presenter.hasError());
    const QString id = presenter.currentOccurrenceId();
    locator.immediate = AudioFileLocationResult{AudioFileLocationStatus::Selected, replacement};
    QSignalSpy finished(&presenter, &PlayerPresenter::fileLocationFinished);
    QVERIFY(presenter.locateQueueOccurrence(id));
    QCOMPARE(backend.loaded_paths.size(), std::size_t{1});
    QCOMPARE(finished.count(), 0);
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(backend.loaded_paths, (std::vector{source, replacement}));
}

void PlayerPresenterTests::relocatedSourceIsSavedWithItsOccurrenceAndPendingRestorePosition_data() {
    QTest::addColumn<PlaybackErrorCode>("error");
    QTest::newRow("source-unavailable") << PlaybackErrorCode::SourceUnavailable;
    QTest::newRow("invalid-media") << PlaybackErrorCode::InvalidMedia;
    QTest::newRow("access-denied") << PlaybackErrorCode::AccessDenied;
}

void PlayerPresenterTests::relocatedSourceIsSavedWithItsOccurrenceAndPendingRestorePosition() {
    QFETCH(PlaybackErrorCode, error);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterFileLocator locator;
    PresenterSessionWriter writer;
    PresenterPreferencesClock clock;
    PresenterPreferencesWriter preferences_writer;
    PreferencesAutosave preferences_autosave{
        preferences_writer, Preferences{}, {.now = [&clock] { return clock.now(); }}};
    const auto source = std::filesystem::absolute(localTestPath(u8"Sessão anterior.wav"));
    const auto replacement = std::filesystem::absolute(localTestPath(u8"Movida/Sessão.wav"));
    const SessionSnapshot initial{
        .queue = {.items = {{LocalQueueOccurrenceId{51}, source}, {LocalQueueOccurrenceId{52}, source}},
                  .current_occurrence_id = LocalQueueOccurrenceId{51}},
        .position_ms = 42'000};
    SessionAutosave autosave{writer, initial, {.now = [&clock] { return clock.now(); }}};
    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
        PlayerPresenterPreferences{.autosave = &preferences_autosave,
                                   .session = {.initial = initial,
                                               .load_status = SessionLoadStatus::Loaded,
                                               .autosave = &autosave,
                                               .now = [&clock] { return clock.now(); }}});
    presenter.setAudioFileLocator(&locator);
    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = error});
    QTRY_VERIFY(presenter.hasError());
    QVERIFY(presenter.locateQueueOccurrence(QStringLiteral("local-51")));
    locator.complete({AudioFileLocationStatus::Selected, replacement});
    QTRY_COMPARE(backend.loaded_paths, (std::vector{source, replacement}));
    for (int pass = 0; pass < 2; ++pass) {
        if (writer.active.has_value())
            writer.complete(SessionSaveStatus::Saved);
        PlayerPresenterTestAccess::pollPersistence(presenter);
        autosave.requestSaveNow();
        PlayerPresenterTestAccess::pollPersistence(presenter);
    }
    QVERIFY(!writer.starts.empty());
    const auto& saved = writer.starts.back().snapshot;
    QCOMPARE(saved.queue.items[0].source_path, replacement);
    QCOMPARE(saved.queue.items[1].source_path, source);
    QCOMPARE(saved.queue.current_occurrence_id, std::optional{LocalQueueOccurrenceId{51}});
    QCOMPARE(saved.position_ms, std::int64_t{42'000});
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
    QCOMPARE(backend.play_calls, 0);
}

void PlayerPresenterTests::cancellingFileLocationForExitNotifiesTheBusyProperty() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterFileLocator locator;
    PresenterPreferencesWriter writer;
    PresenterPreferencesClock clock;
    const Preferences initial{};
    PreferencesAutosave autosave{writer, initial, {.now = [&clock] { return clock.now(); }}};
    const auto source = localTestPath(u8"Cancelada.wav");
    PlayerPresenter presenter(
        platform, backend, metadata, [source] { return std::vector{source}; },
        PlayerPresenterPreferences{.initial = initial, .autosave = &autosave, .now = [&clock] { return clock.now(); }});
    presenter.setAudioFileLocator(&locator);
    QVERIFY(presenter.openAudio());
    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::SourceUnavailable});
    QTRY_VERIFY(presenter.hasError());
    QVERIFY(presenter.locateQueueOccurrence(presenter.currentOccurrenceId()));
    QVERIFY(presenter.setVolume(0.24));
    QSignalSpy state(&presenter, &PlayerPresenter::stateChanged);
    QVERIFY(presenter.requestExit());
    QVERIFY(presenter.locatingOccurrenceId().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(!state.empty(), 100);
    clock.advance(std::chrono::seconds{3});
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QVERIFY(!presenter.exiting());
    QVERIFY(presenter.locatingOccurrenceId().isEmpty());
    QVERIFY(presenter.canLocateCurrentOccurrence());
}

void PlayerPresenterTests::asynchronousOpenKeepsPlaybackEventsLiveAndAppendsToTheLatestQueue() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterFilesPicker picker;
    const auto first = std::filesystem::absolute(localTestPath(u8"Primeira.wav"));
    const auto second = std::filesystem::absolute(localTestPath(u8"Segunda.wav"));
    const auto selected = std::filesystem::absolute(localTestPath(u8"Escolhida 日本語.wav"));
    PlayerPresenter presenter(platform, backend, metadata, [=] { return std::vector{first, second}; });
    QVERIFY(presenter.openAudio());
    const QString first_id = presenter.currentOccurrenceId();
    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 100,
                     .duration_ms = 1000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.confirmedPositionMs(), qint64{100});
    presenter.setAudioFilesPicker(&picker);
    QSignalSpy finished(&presenter, &PlayerPresenter::audioFileSelectionFinished);
    QSignalSpy resets(presenter.queueModelObject(), &QAbstractItemModel::modelReset);
    const int play_calls = backend.play_calls;
    const int pause_calls = backend.pause_calls;
    QVERIFY(presenter.openAudio());
    QVERIFY(presenter.selectingAudioFiles());
    QVERIFY(!presenter.openAudio());
    QVERIFY(!presenter.addAudio());
    QCOMPARE(picker.requests, 1);
    QCOMPARE(backend.loaded_paths, std::vector{first});
    QCOMPARE(backend.play_calls, play_calls);
    QCOMPARE(backend.pause_calls, pause_calls);
    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 550,
                     .duration_ms = 1000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.confirmedPositionMs(), qint64{550});
    QVERIFY(presenter.togglePlayback());
    QCOMPARE(backend.pause_calls, pause_calls + 1);
    backend.publishEvent(PlaybackEventType::Ended, {.status = PlaybackStatus::Paused,
                                                    .position_ms = 1000,
                                                    .duration_ms = 1000,
                                                    .seekable = true,
                                                    .generation = 1});
    QTRY_COMPARE(backend.loaded_paths, (std::vector{first, second}));
    QVERIFY(presenter.selectingAudioFiles());
    picker.complete({AudioFilesSelectionStatus::Selected, {selected, selected}});
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(finished.front().front().toBool());
    QVERIFY(!presenter.selectingAudioFiles());
    QVERIFY(!presenter.audioFileSelectionFailed());
    QCOMPARE(backend.loaded_paths, (std::vector{first, second, selected}));
    QCOMPARE(presenter.currentQueueIndex(), 2);
    QCOMPARE(presenter.queueModelObject()->rowCount(), 4);
    QCOMPARE(presenter.queueModelObject()
                 ->data(presenter.queueModelObject()->index(0, 0), QueueListModel::OccurrenceIdRole)
                 .toString(),
             first_id);
    const auto second_new = presenter.queueModelObject()
                                ->data(presenter.queueModelObject()->index(3, 0), QueueListModel::OccurrenceIdRole)
                                .toString();
    QVERIFY(second_new != presenter.currentOccurrenceId());
    QCOMPARE(resets.count(), 0);
}

void PlayerPresenterTests::asynchronousAddPreservesTheCurrentTrackOrStartsAnEmptyQueue_data() {
    QTest::addColumn<bool>("clear_during_selection");
    QTest::newRow("keep-current") << false;
    QTest::newRow("queue-cleared-while-selecting") << true;
}

void PlayerPresenterTests::asynchronousAddPreservesTheCurrentTrackOrStartsAnEmptyQueue() {
    QFETCH(bool, clear_during_selection);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterFilesPicker picker;
    const auto source = std::filesystem::absolute(localTestPath(u8"Atual.wav"));
    const auto selected = std::filesystem::absolute(localTestPath(u8"Adicionada.wav"));
    PlayerPresenter presenter(platform, backend, metadata, [=] { return std::vector{source}; });
    QVERIFY(presenter.openAudio());
    const auto current_id = presenter.currentOccurrenceId();
    presenter.setAudioFilesPicker(&picker);
    QVERIFY(presenter.addAudio());
    if (clear_during_selection)
        QVERIFY(presenter.clearQueue());
    const auto before = backend.loaded_paths;
    picker.complete({AudioFilesSelectionStatus::Selected, {selected, selected}});
    QTRY_VERIFY(!presenter.selectingAudioFiles());
    if (clear_during_selection) {
        QCOMPARE(backend.loaded_paths, (std::vector{source, selected}));
        QCOMPARE(presenter.currentQueueIndex(), 0);
        QCOMPARE(presenter.queueModelObject()->rowCount(), 2);
    } else {
        QCOMPARE(backend.loaded_paths, before);
        QCOMPARE(presenter.currentOccurrenceId(), current_id);
        QCOMPARE(presenter.queueModelObject()->rowCount(), 3);
    }
}

void PlayerPresenterTests::asynchronousSelectionCancellationFailureAndDuplicateCompletionAreSafe() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterFilesPicker picker;
    PlayerPresenter presenter(platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; });
    presenter.setAudioFilesPicker(&picker);
    QVERIFY(presenter.openAudio());
    const auto obsolete = picker.pending;
    picker.complete({AudioFilesSelectionStatus::Cancelled, {}});
    QTRY_VERIFY(!presenter.selectingAudioFiles());
    QVERIFY(!presenter.audioFileSelectionFailed());
    QVERIFY(presenter.openAudio());
    obsolete({AudioFilesSelectionStatus::Selected, {std::filesystem::absolute(localTestPath(u8"Antiga.wav"))}});
    QCoreApplication::processEvents();
    QVERIFY(presenter.selectingAudioFiles());
    QVERIFY(backend.loaded_paths.empty());
    picker.complete({AudioFilesSelectionStatus::Failed, {}});
    QTRY_VERIFY(presenter.audioFileSelectionFailed());
    QVERIFY(!presenter.selectingAudioFiles());
    picker.accept = false;
    QVERIFY(!presenter.addAudio());
    QTRY_VERIFY(!presenter.selectingAudioFiles());
    QVERIFY(presenter.audioFileSelectionFailed());
    QVERIFY(backend.loaded_paths.empty());
    picker.accept = true;
    QVERIFY(presenter.openAudio());
    QVERIFY(!presenter.audioFileSelectionFailed());
    backend.load_result = PlaybackCommandResult::Busy;
    picker.complete({AudioFilesSelectionStatus::Selected, {std::filesystem::absolute(localTestPath(u8"Nova.wav"))}});
    QTRY_VERIFY(!presenter.selectingAudioFiles());
    QVERIFY(presenter.audioFileSelectionFailed());
    QCOMPARE(presenter.queueModelObject()->rowCount(), 0);
}

void PlayerPresenterTests::asynchronousSelectionRejectsInvalidPathsWithoutChangingPlayback_data() {
    QTest::addColumn<std::vector<std::filesystem::path>>("paths");
    QTest::newRow("empty") << std::vector<std::filesystem::path>{};
    const std::filesystem::path relative{u8"local-test/Relativa.wav"};
    QVERIFY(!relative.is_absolute());
    QTest::newRow("relative") << std::vector{relative};
    QTest::newRow("root") << std::vector{std::filesystem::current_path().root_path()};
    auto nul = std::filesystem::absolute(localTestPath(u8"Nul.wav")).native();
    nul.push_back(std::filesystem::path::value_type{});
    nul.push_back(static_cast<std::filesystem::path::value_type>('x'));
    QTest::newRow("embedded-nul") << std::vector{std::filesystem::path{nul}};
}

void PlayerPresenterTests::asynchronousSelectionRejectsInvalidPathsWithoutChangingPlayback() {
    QFETCH(std::vector<std::filesystem::path>, paths);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterFilesPicker picker;
    const auto source = std::filesystem::absolute(localTestPath(u8"Mantida.wav"));
    PlayerPresenter presenter(platform, backend, metadata, [=] { return std::vector{source}; });
    QVERIFY(presenter.openAudio());
    const auto id = presenter.currentOccurrenceId();
    presenter.setAudioFilesPicker(&picker);
    QVERIFY(presenter.openAudio());
    picker.complete({AudioFilesSelectionStatus::Selected, paths});
    QTRY_VERIFY(presenter.audioFileSelectionFailed());
    QVERIFY(!presenter.selectingAudioFiles());
    QCOMPARE(presenter.currentOccurrenceId(), id);
    QCOMPARE(backend.loaded_paths, std::vector{source});
    QCOMPARE(presenter.queueModelObject()->rowCount(), 1);
}

void PlayerPresenterTests::asynchronousSelectionIsCancelledOnExitAndCannotOutliveThePresenter() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterFilesPicker picker;
    PresenterPreferencesWriter writer;
    PresenterPreferencesClock clock;
    PreferencesAutosave autosave{writer, Preferences{}, {.now = [&clock] { return clock.now(); }}};
    auto presenter = std::make_unique<PlayerPresenter>(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
        PlayerPresenterPreferences{.autosave = &autosave, .now = [&clock] { return clock.now(); }});
    presenter->setAudioFilesPicker(&picker);
    QVERIFY(presenter->setVolume(0.19));
    QVERIFY(presenter->openAudio());
    const auto late = picker.pending;
    QSignalSpy state(presenter.get(), &PlayerPresenter::stateChanged);
    QVERIFY(presenter->requestExit());
    QCOMPARE(picker.cancels, 1);
    QTRY_VERIFY(!state.empty());
    QVERIFY(!presenter->selectingAudioFiles());
    late({AudioFilesSelectionStatus::Selected, {std::filesystem::absolute(localTestPath(u8"Tardia.wav"))}});
    QCoreApplication::processEvents();
    QVERIFY(backend.loaded_paths.empty());
    clock.advance(std::chrono::seconds{3});
    PlayerPresenterTestAccess::pollPersistence(*presenter);
    QVERIFY(!presenter->exiting());
    QVERIFY(presenter->openAudio());
    const auto after_destruction = picker.pending;
    presenter.reset();
    after_destruction({AudioFilesSelectionStatus::Cancelled, {}});
    QCoreApplication::processEvents();
    QCOMPARE(picker.cancels, 2);
    QVERIFY(backend.loaded_paths.empty());
}

void PlayerPresenterTests::asynchronousSelectionRebindingAndInlineCompletionAreGenerationSafe() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterFilesPicker first;
    PresenterFilesPicker replacement;
    const auto source = std::filesystem::absolute(localTestPath(u8"Inline.wav"));
    PlayerPresenter presenter(platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; });
    presenter.setAudioFilesPicker(&first);
    QVERIFY(presenter.openAudio());
    const auto obsolete = first.pending;
    presenter.setAudioFilesPicker(&replacement);
    QCOMPARE(first.cancels, 1);
    QVERIFY(!presenter.selectingAudioFiles());
    replacement.immediate = AudioFilesSelectionResult{AudioFilesSelectionStatus::Selected, {source}};
    QVERIFY(presenter.openAudio());
    QVERIFY(presenter.selectingAudioFiles());
    QVERIFY(backend.loaded_paths.empty());
    obsolete({AudioFilesSelectionStatus::Selected, {source, source}});
    QTRY_VERIFY(!presenter.selectingAudioFiles());
    QCOMPARE(backend.loaded_paths, std::vector{source});
    QCOMPARE(presenter.queueModelObject()->rowCount(), 1);
}

void PlayerPresenterTests::mainSelectionAndFileLocationCannotOverlap() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterFilesPicker picker;
    PresenterFileLocator locator;
    const auto source = std::filesystem::absolute(localTestPath(u8"Ausente.wav"));
    PlayerPresenter presenter(platform, backend, metadata, [=] { return std::vector{source}; });
    QVERIFY(presenter.openAudio());
    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::SourceUnavailable});
    QTRY_VERIFY(presenter.hasError());
    presenter.setAudioFilesPicker(&picker);
    presenter.setAudioFileLocator(&locator);
    const auto id = presenter.currentOccurrenceId();
    QVERIFY(presenter.locateQueueOccurrence(id));
    QVERIFY(!presenter.openAudio());
    QVERIFY(!presenter.addAudio());
    locator.complete({AudioFileLocationStatus::Cancelled, {}});
    QTRY_VERIFY(presenter.locatingOccurrenceId().isEmpty());
    QVERIFY(presenter.openAudio());
    QVERIFY(!presenter.locateQueueOccurrence(id));
    picker.complete({AudioFilesSelectionStatus::Cancelled, {}});
    QTRY_VERIFY(!presenter.selectingAudioFiles());
    QVERIFY(presenter.locateQueueOccurrence(id));
}

void PlayerPresenterTests::selectionObserverCanCancelOrDestroyBeforeTheNativeRequest_data() {
    QTest::addColumn<QString>("action");
    QTest::newRow("exit") << QStringLiteral("exit");
    QTest::newRow("rebind") << QStringLiteral("rebind");
    QTest::newRow("destroy") << QStringLiteral("destroy");
}

void PlayerPresenterTests::selectionObserverCanCancelOrDestroyBeforeTheNativeRequest() {
    QFETCH(QString, action);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterFilesPicker picker;
    PresenterFilesPicker replacement;
    auto presenter = std::make_unique<PlayerPresenter>(platform, backend, metadata,
                                                       [] { return std::vector<std::filesystem::path>{}; });
    presenter->setAudioFilesPicker(&picker);
    bool handled = false;
    connect(presenter.get(), &PlayerPresenter::stateChanged, presenter.get(), [&] {
        if (handled || !presenter->selectingAudioFiles())
            return;
        handled = true;
        if (action == QStringLiteral("exit"))
            static_cast<void>(presenter->requestExit());
        else if (action == QStringLiteral("rebind"))
            presenter->setAudioFilesPicker(&replacement);
        else
            presenter.reset();
    });
    QVERIFY(!presenter->openAudio());
    QVERIFY(handled);
    QCOMPARE(picker.requests, 0);
    QCOMPARE(replacement.requests, 0);
    QVERIFY(backend.loaded_paths.empty());
}

void PlayerPresenterTests::inlineSelectionCannotWinAfterLaunchFailure_data() {
    QTest::addColumn<bool>("throws");
    QTest::newRow("returns-false") << false;
    QTest::newRow("throws") << true;
}

void PlayerPresenterTests::inlineSelectionCannotWinAfterLaunchFailure() {
    QFETCH(bool, throws);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterFilesPicker picker;
    picker.immediate = AudioFilesSelectionResult{AudioFilesSelectionStatus::Selected,
                                                 {std::filesystem::absolute(localTestPath(u8"Não abrir.wav"))}};
    picker.throw_after_completion = throws;
    picker.reject_after_completion = !throws;
    PlayerPresenter presenter(platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; });
    presenter.setAudioFilesPicker(&picker);
    QVERIFY(!presenter.openAudio());
    QCoreApplication::processEvents();
    QVERIFY(!presenter.selectingAudioFiles());
    QVERIFY(presenter.audioFileSelectionFailed());
    QVERIFY(backend.loaded_paths.empty());
}

void PlayerPresenterTests::obsoleteSelectionFinishedCannotAffectAReentrantNewRequest() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterFilesPicker picker;
    PlayerPresenter presenter(platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; });
    presenter.setAudioFilesPicker(&picker);
    QSignalSpy finished(&presenter, &PlayerPresenter::audioFileSelectionFinished);
    bool restarted = false;
    connect(&presenter, &PlayerPresenter::stateChanged, &presenter, [&] {
        if (restarted || presenter.selectingAudioFiles() || presenter.queueModelObject()->rowCount() != 1)
            return;
        restarted = true;
        QVERIFY(presenter.addAudio());
    });
    QVERIFY(presenter.openAudio());
    picker.complete(
        {AudioFilesSelectionStatus::Selected, {std::filesystem::absolute(localTestPath(u8"Selecionada.wav"))}});
    QTRY_VERIFY(restarted);
    QVERIFY(presenter.selectingAudioFiles());
    QCOMPARE(picker.requests, 2);
    QCOMPARE(finished.count(), 0);
    picker.complete({AudioFilesSelectionStatus::Cancelled, {}});
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(!finished.front().front().toBool());
    QCOMPARE(presenter.queueModelObject()->rowCount(), 1);
}

void PlayerPresenterTests::failedAudioSelectionPreservesAnActiveImport_data() {
    QTest::addColumn<bool>("append_only");
    QTest::newRow("open") << false;
    QTest::newRow("add") << true;
}

void PlayerPresenterTests::failedAudioSelectionPreservesAnActiveImport() {
    QFETCH(bool, append_only);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterFilesPicker picker;
    FakeLocalSourceDiscovery discovery;
    const auto source = localTestPath(u8"Descoberta.wav");
    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; }, discovery);
    presenter.setAudioFilesPicker(&picker);
    QVERIFY(presenter.importDroppedUrls({localTestUrl(source)}, 0));
    const auto generation = discovery.requests.front().generation;
    QVERIFY(append_only ? presenter.addAudio() : presenter.openAudio());
    backend.load_result = PlaybackCommandResult::InvalidArgument;
    picker.complete({AudioFilesSelectionStatus::Selected, {std::filesystem::absolute(localTestPath(u8"Falha.wav"))}});
    QTRY_VERIFY(presenter.audioFileSelectionFailed());
    QVERIFY(presenter.sourceImportActive());
    QCOMPARE(presenter.sourceImportAcceptedCount(), 0ULL);
    QCOMPARE(presenter.sourceImportIssueCount(), 0ULL);
    QVERIFY(discovery.cancelled_generations.empty());
    backend.load_result = PlaybackCommandResult::Accepted;
    discovery.publishBatch(generation, 1, {source}, {}, 1);
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QCOMPARE(presenter.queueModelObject()->rowCount(), 1);
    QCOMPARE(presenter.currentTitle(), QStringLiteral("Descoberta"));
    discovery.publishCompleted({.inspected_count = 1, .accepted_count = 1, .issue_count = 0});
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QCOMPARE(presenter.sourceImportState(), PlayerPresenter::SourceImportState::Completed);
    QCOMPARE(presenter.sourceImportAcceptedCount(), 1ULL);
}

void PlayerPresenterTests::selectionCompletionMayDestroyThePresenter_data() {
    QTest::addColumn<bool>("append_only");
    QTest::newRow("open") << false;
    QTest::newRow("add") << true;
}

void PlayerPresenterTests::selectionCompletionMayDestroyThePresenter() {
    QFETCH(bool, append_only);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterFilesPicker picker;
    auto presenter = std::make_unique<PlayerPresenter>(platform, backend, metadata,
                                                       [] { return std::vector<std::filesystem::path>{}; });
    presenter->setAudioFilesPicker(&picker);
    connect(presenter.get(), &PlayerPresenter::stateChanged, presenter.get(), [&] {
        if (presenter->queueModelObject()->rowCount() == 1)
            presenter.reset();
    });
    QVERIFY(append_only ? presenter->addAudio() : presenter->openAudio());
    picker.complete({AudioFilesSelectionStatus::Selected, {std::filesystem::absolute(localTestPath(u8"Aberta.wav"))}});
    QTRY_VERIFY(!presenter);
    QCOMPARE(backend.loaded_paths.size(), std::size_t{1});
}

void PlayerPresenterTests::pickerCancellationCannotStartAnotherDialog_data() {
    QTest::addColumn<bool>("exit_requested");
    QTest::newRow("exit") << true;
    QTest::newRow("rebind") << false;
}

void PlayerPresenterTests::pickerCancellationCannotStartAnotherDialog() {
    QFETCH(bool, exit_requested);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterFilesPicker picker;
    PresenterFilesPicker replacement;
    PlayerPresenter presenter(platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; });
    presenter.setAudioFilesPicker(&picker);
    QVERIFY(presenter.openAudio());
    bool reopened = false;
    picker.on_cancel = [&] { reopened = presenter.addAudio(); };
    if (exit_requested)
        QVERIFY(presenter.requestExit());
    else
        presenter.setAudioFilesPicker(&replacement);
    picker.on_cancel = {};
    QVERIFY(!reopened);
    QVERIFY(!presenter.selectingAudioFiles());
    QCOMPARE(picker.requests, 1);
    QCOMPARE(picker.cancels, 1);
    if (!exit_requested) {
        QVERIFY(presenter.addAudio());
        QCOMPARE(replacement.requests, 1);
    }
}

void PlayerPresenterTests::pickerCancellationMayDestroyThePresenter_data() {
    QTest::addColumn<bool>("exit_requested");
    QTest::newRow("exit") << true;
    QTest::newRow("rebind") << false;
}

void PlayerPresenterTests::pickerCancellationMayDestroyThePresenter() {
    QFETCH(bool, exit_requested);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterFilesPicker picker;
    PresenterFilesPicker replacement;
    auto presenter = std::make_unique<PlayerPresenter>(platform, backend, metadata,
                                                       [] { return std::vector<std::filesystem::path>{}; });
    presenter->setAudioFilesPicker(&picker);
    QVERIFY(presenter->openAudio());
    picker.on_cancel = [&] { presenter.reset(); };
    if (exit_requested)
        static_cast<void>(presenter->requestExit());
    else
        presenter->setAudioFilesPicker(&replacement);
    QVERIFY(!presenter);
    QCOMPARE(picker.cancels, 1);
    QCOMPARE(replacement.requests, 0);
}

void PlayerPresenterTests::retriesTheOriginalUnavailableOccurrenceWithoutDuplicatingOrResettingTheQueue() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const auto source = localTestPath(u8"Reconectada 日本語.wav");
    PlayerPresenter presenter(platform, backend, metadata, [source] { return std::vector{source, source}; });
    QVERIFY(presenter.openAudio());
    auto* const model = presenter.queueModelObject();
    const QString first_id = presenter.currentOccurrenceId();
    const QString second_id = model->data(model->index(1, 0), QueueListModel::OccurrenceIdRole).toString();
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::SourceUnavailable});
    QTRY_COMPARE(presenter.currentOccurrenceId(), second_id);
    QVERIFY(!model->data(model->index(0, 0), QueueListModel::AvailableRole).toBool());

    QVERIFY(presenter.retryQueueOccurrence(first_id));
    QCOMPARE(presenter.currentOccurrenceId(), first_id);
    QCOMPARE(presenter.retryingOccurrenceId(), first_id);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
    QCOMPARE(backend.loaded_paths, (std::vector{source, source, source}));
    QCOMPARE(model->rowCount(), 2);
    QVERIFY(!model->data(model->index(0, 0), QueueListModel::AvailableRole).toBool());
    QCOMPARE(model->data(model->index(0, 0), QueueListModel::ErrorCodeRole).toString(),
             QStringLiteral("source_unavailable"));
    QVERIFY(!presenter.canRetryCurrentOccurrence());
    QVERIFY(!presenter.retryQueueOccurrence(first_id));
    QCOMPARE(backend.loaded_paths.size(), std::size_t{3});
    QCOMPARE(backend.play_calls, 0);

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 3});
    QTRY_COMPARE(backend.play_calls, 1);
    QTRY_VERIFY(presenter.retryingOccurrenceId().isEmpty());
    QVERIFY(model->data(model->index(0, 0), QueueListModel::AvailableRole).toBool());
    QVERIFY(!model->data(model->index(0, 0), QueueListModel::ErrorCodeRole).isValid());
    QCOMPARE(model->data(model->index(1, 0), QueueListModel::OccurrenceIdRole).toString(), second_id);
    QVERIFY(model->data(model->index(1, 0), QueueListModel::AvailableRole).toBool());
    backend.publishEvent(
        PlaybackEventType::SnapshotChanged,
        {.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::SourceUnavailable}, false);
    QCoreApplication::processEvents();
    QCOMPARE(presenter.currentOccurrenceId(), first_id);
    QVERIFY(!presenter.hasError());
    QVERIFY(model->data(model->index(0, 0), QueueListModel::AvailableRole).toBool());
    QCOMPARE(resets.size(), 0);
}

void PlayerPresenterTests::rejectedRetryPreservesTheCurrentTrackAndRepeatedFailureRemainsActionable() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const auto source = localTestPath(u8"Retry.wav");
    PlayerPresenter presenter(platform, backend, metadata, [source] { return std::vector{source, source}; });
    QVERIFY(presenter.openAudio());
    auto* const model = presenter.queueModelObject();
    const QString first_id = presenter.currentOccurrenceId();
    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::SourceUnavailable});
    QTRY_COMPARE(presenter.currentQueueIndex(), 1);
    const QString second_id = presenter.currentOccurrenceId();
    backend.publish({.status = PlaybackStatus::Paused,
                     .position_ms = 400,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = 2});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    const auto metadata_count = metadata.requests.size();
    backend.load_result = PlaybackCommandResult::Busy;
    QVERIFY(!presenter.retryQueueOccurrence(first_id));
    QCOMPARE(presenter.currentOccurrenceId(), second_id);
    QCOMPARE(presenter.confirmedPositionMs(), qint64{400});
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QCOMPARE(metadata.requests.size(), metadata_count);
    QVERIFY(presenter.retryingOccurrenceId().isEmpty());
    QVERIFY(!model->data(model->index(0, 0), QueueListModel::AvailableRole).toBool());
    QVERIFY(!presenter.retryQueueOccurrence(QStringLiteral("invalid-id")));
    QVERIFY(!presenter.retryQueueOccurrence(second_id));

    backend.load_result = PlaybackCommandResult::Accepted;
    QVERIFY(presenter.retryQueueOccurrence(first_id));
    backend.publish({.status = PlaybackStatus::Error, .generation = 3, .error = PlaybackErrorCode::AccessDenied});
    QTRY_VERIFY(presenter.canRetryCurrentOccurrence());
    QCOMPARE(presenter.currentOccurrenceId(), first_id);
    QCOMPARE(presenter.errorCode(), QStringLiteral("access_denied"));
    QCOMPARE(model->data(model->index(0, 0), QueueListModel::ErrorCodeRole).toString(),
             QStringLiteral("access_denied"));
    QCOMPARE(backend.loaded_paths.size(), std::size_t{4});
    QVERIFY(presenter.retryingOccurrenceId().isEmpty());
    backend.publish(
        {.status = PlaybackStatus::Error, .position_ms = 1, .generation = 3, .error = PlaybackErrorCode::AccessDenied});
    QCoreApplication::processEvents();
    QCOMPARE(presenter.currentOccurrenceId(), first_id);
    QCOMPARE(backend.loaded_paths.size(), std::size_t{4});
    QVERIFY(presenter.canRetryCurrentOccurrence());
    QVERIFY(presenter.retryQueueOccurrence(first_id));
    backend.publish({.status = PlaybackStatus::Error, .generation = 4, .error = PlaybackErrorCode::AccessDenied});
    QTRY_VERIFY(presenter.canRetryCurrentOccurrence());
    QCOMPARE(backend.loaded_paths.size(), std::size_t{5});
    QCOMPARE(backend.play_calls, 0);
    QCOMPARE(model->rowCount(), 2);
    QVERIFY(presenter.requestExit());
    QVERIFY(!presenter.canRetryCurrentOccurrence());
    QVERIFY(!presenter.retryQueueOccurrence(first_id));
}

void PlayerPresenterTests::retryCompletionCannotReviveAnOccurrenceAfterAnotherExplicitSelection() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const auto source = localTestPath(u8"Original.wav");
    PlayerPresenter presenter(platform, backend, metadata, [source] { return std::vector{source, source}; });
    QVERIFY(presenter.openAudio());
    const QString first_id = presenter.currentOccurrenceId();
    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::SourceUnavailable});
    QTRY_COMPARE(presenter.currentQueueIndex(), 1);
    const QString second_id = presenter.currentOccurrenceId();
    QVERIFY(presenter.retryQueueOccurrence(first_id));
    QVERIFY(presenter.playQueueOccurrence(second_id));
    QVERIFY(presenter.retryingOccurrenceId().isEmpty());
    backend.publishEvent(PlaybackEventType::SnapshotChanged,
                         {.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 3},
                         false);
    QCoreApplication::processEvents();
    QCOMPARE(presenter.currentOccurrenceId(), second_id);
    auto* const model = presenter.queueModelObject();
    QVERIFY(!model->data(model->index(0, 0), QueueListModel::AvailableRole).toBool());
    QCOMPARE(backend.play_calls, 0);
    QCOMPARE(model->rowCount(), 2);
}

void PlayerPresenterTests::retryReconcilesASynchronousLoadCompletion_data() {
    QTest::addColumn<bool>("fails");
    QTest::newRow("ready") << false;
    QTest::newRow("error") << true;
}

void PlayerPresenterTests::retryReconcilesASynchronousLoadCompletion() {
    QFETCH(bool, fails);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const auto source = localTestPath(u8"Synchronous.wav");
    PlayerPresenter presenter(platform, backend, metadata, [source] { return std::vector{source}; });
    QVERIFY(presenter.openAudio());
    const QString id = presenter.currentOccurrenceId();
    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::SourceUnavailable});
    QTRY_VERIFY(presenter.canRetryCurrentOccurrence());
    backend.accepted_load_status = fails ? PlaybackStatus::Error : PlaybackStatus::Ready;

    QVERIFY(presenter.retryQueueOccurrence(id));
    QVERIFY(presenter.retryingOccurrenceId().isEmpty());
    QCOMPARE(presenter.canRetryCurrentOccurrence(), fails);
    QCOMPARE(presenter.hasError(), fails);
    QCOMPARE(backend.play_calls, fails ? 0 : 1);
    auto* const model = presenter.queueModelObject();
    QCOMPARE(model->data(model->index(0, 0), QueueListModel::AvailableRole).toBool(), !fails);
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(presenter.currentOccurrenceId(), id);
}

void PlayerPresenterTests::explicitRetryPreventsLaterImportBatchesFromTakingOver_data() {
    QTest::addColumn<bool>("retry_failed");
    QTest::newRow("loading") << false;
    QTest::newRow("failed") << true;
}

void PlayerPresenterTests::explicitRetryPreventsLaterImportBatchesFromTakingOver() {
    QFETCH(bool, retry_failed);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    FakeLocalSourceDiscovery discovery;
    const auto source = localTestPath(u8"Restaurada.wav");
    const auto later = localTestPath(u8"Lote seguinte.wav");
    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; }, discovery);
    QVERIFY(presenter.importDroppedUrls({localTestUrl(source), localTestUrl(later)}, 0));
    const auto generation = discovery.requests.front().generation;
    discovery.publishBatch(generation, 1, {source}, {}, 1);
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    const QString id = presenter.currentOccurrenceId();
    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::SourceUnavailable});
    QTRY_VERIFY(presenter.hasError());

    QVERIFY(presenter.retryQueueOccurrence(id));
    if (retry_failed) {
        backend.publish(
            {.status = PlaybackStatus::Error, .generation = 2, .error = PlaybackErrorCode::SourceUnavailable});
        QTRY_VERIFY(presenter.hasError());
    }
    discovery.publishBatch(generation, 2, {later}, {}, 2);
    PlayerPresenterTestAccess::pollSourceImport(presenter);
    QCOMPARE(presenter.currentOccurrenceId(), id);
    QCOMPARE(backend.loaded_paths, (std::vector{source, source}));
    QCOMPARE(presenter.queueModelObject()->rowCount(), 2);
    QCOMPARE(presenter.sourceImportAcceptedCount(), 2ULL);
    QCOMPARE(presenter.hasError(), retry_failed);
}

void PlayerPresenterTests::localErrorContinuationPreservesStayReadyIntent() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector<std::filesystem::path> selections{
        localTestPath(u8"Primeira.mp3"),
        localTestPath(u8"Inválida.mp3"),
        localTestPath(u8"Terceira.mp3"),
    };
    PlayerPresenter presenter(platform, backend, metadata, [selections] { return selections; });
    QAbstractItemModel* const model = presenter.queueModelObject();
    QVERIFY(presenter.openAudio());
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    backend.publish({.status = PlaybackStatus::Paused,
                     .position_ms = 500,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QVERIFY(presenter.nextTrack());
    QCOMPARE(presenter.currentQueueIndex(), 1);

    backend.publish({.status = PlaybackStatus::Error, .generation = 2, .error = PlaybackErrorCode::InvalidMedia});
    QTRY_COMPARE(presenter.currentQueueIndex(), 2);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
    QVERIFY(!model->data(model->index(1, 0), QueueListModel::AvailableRole).toBool());
    QCOMPARE(model->data(model->index(1, 0), QueueListModel::ErrorCodeRole).toString(),
             QStringLiteral("invalid_media"));

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 3'000, .seekable = true, .generation = 3});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Ready);
    QCOMPARE(backend.play_calls, 1);
}

void PlayerPresenterTests::localErrorContinuationPreservesPausedPlayback() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector<std::filesystem::path> selections{
        localTestPath(u8"Primeira.mp3"),
        localTestPath(u8"Segunda.mp3"),
    };
    PlayerPresenter presenter(platform, backend, metadata, [selections] { return selections; });
    QVERIFY(presenter.openAudio());
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    backend.publish({.status = PlaybackStatus::Paused,
                     .position_ms = 500,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);

    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::SourceUnavailable});
    QTRY_COMPARE(presenter.currentQueueIndex(), 1);
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 3'000, .seekable = true, .generation = 2});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Ready);

    QCOMPARE(backend.play_calls, 1);
}

void PlayerPresenterTests::rejectedAutomaticContinuationRemainsManuallyRecoverable() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector<std::filesystem::path> selections{
        localTestPath(u8"Inválida.mp3"),
        localTestPath(u8"Recuperável.mp3"),
    };
    PlayerPresenter presenter(platform, backend, metadata, [selections] { return selections; });
    QAbstractItemModel* const model = presenter.queueModelObject();
    QVERIFY(presenter.openAudio());
    const QString failed_id = presenter.currentOccurrenceId();
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    backend.publish({.status = PlaybackStatus::Paused,
                     .position_ms = 500,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    backend.load_result = PlaybackCommandResult::Busy;

    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::InvalidMedia});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Error);
    QCOMPARE(presenter.currentQueueIndex(), 0);
    QCOMPARE(presenter.currentOccurrenceId(), failed_id);
    QVERIFY(!model->data(model->index(0, 0), QueueListModel::AvailableRole).toBool());
    QVERIFY(model->data(model->index(1, 0), QueueListModel::AvailableRole).toBool());
    QVERIFY(presenter.canGoNext());
    backend.publish({.status = PlaybackStatus::Error,
                     .position_ms = 600,
                     .generation = 1,
                     .error = PlaybackErrorCode::InvalidMedia});
    QCoreApplication::processEvents();

    backend.load_result = PlaybackCommandResult::Accepted;
    QVERIFY(presenter.nextTrack());
    QCOMPARE(presenter.currentQueueIndex(), 1);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 3'000, .seekable = true, .generation = 2});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Ready);
    QCOMPARE(backend.play_calls, 1);
}

void PlayerPresenterTests::rejectedContinuationPreservesConfirmedPlayingIntent() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector<std::filesystem::path> selections{
        localTestPath(u8"Primeira.mp3"),
        localTestPath(u8"Segunda.mp3"),
        localTestPath(u8"Terceira.mp3"),
    };
    PlayerPresenter presenter(platform, backend, metadata, [selections] { return selections; });
    QVERIFY(presenter.openAudio());
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    backend.publish({.status = PlaybackStatus::Paused,
                     .position_ms = 500,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QVERIFY(presenter.nextTrack());
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 2});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Ready);
    QCOMPARE(backend.play_calls, 1);
    QVERIFY(presenter.togglePlayback());
    QCOMPARE(backend.play_calls, 2);
    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 500,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = 2});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    backend.load_result = PlaybackCommandResult::Busy;

    backend.publish({.status = PlaybackStatus::Error, .generation = 2, .error = PlaybackErrorCode::InvalidMedia});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Error);
    QCOMPARE(presenter.currentQueueIndex(), 1);
    QVERIFY(presenter.canGoNext());
    backend.publish({.status = PlaybackStatus::Error,
                     .position_ms = 600,
                     .generation = 2,
                     .error = PlaybackErrorCode::InvalidMedia});
    QCoreApplication::processEvents();

    backend.load_result = PlaybackCommandResult::Accepted;
    QVERIFY(presenter.nextTrack());
    QCOMPARE(presenter.currentQueueIndex(), 2);
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 3'000, .seekable = true, .generation = 3});
    QTRY_COMPARE(backend.play_calls, 3);
}

void PlayerPresenterTests::staleLocalTerminalEventsAreInertAfterAdvance() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector<std::filesystem::path> selections{
        localTestPath(u8"Primeira.mp3"),
        localTestPath(u8"Segunda.mp3"),
    };
    PlayerPresenter presenter(platform, backend, metadata, [selections] { return selections; });
    QAbstractItemModel* const model = presenter.queueModelObject();
    QVERIFY(presenter.openAudio());
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    const PlaybackSnapshot first_final{.status = PlaybackStatus::Paused,
                                       .position_ms = 2'000,
                                       .duration_ms = 2'000,
                                       .seekable = true,
                                       .generation = 1};
    backend.publish(first_final);
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    backend.publishEvent(PlaybackEventType::Ended, first_final);
    QTRY_COMPARE(presenter.currentQueueIndex(), 1);
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 3'000, .seekable = true, .generation = 2});
    QTRY_COMPARE(backend.play_calls, 2);
    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 200,
                     .duration_ms = 3'000,
                     .seekable = true,
                     .generation = 2});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);

    const QString current_id = presenter.currentOccurrenceId();
    QSignalSpy data_changed(model, &QAbstractItemModel::dataChanged);
    QSignalSpy state_changed(&presenter, &PlayerPresenter::stateChanged);
    QSignalSpy playback_changed(&presenter, &PlayerPresenter::playbackChanged);
    backend.publishEvent(PlaybackEventType::Ended, first_final, false);
    backend.publishEvent(PlaybackEventType::SnapshotChanged,
                         {.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::InvalidMedia},
                         false);
    QCoreApplication::processEvents();

    QCOMPARE(presenter.currentQueueIndex(), 1);
    QCOMPARE(presenter.currentOccurrenceId(), current_id);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QCOMPARE(backend.loaded_paths, selections);
    QCOMPARE(backend.play_calls, 2);
    QCOMPARE(data_changed.size(), 0);
    QCOMPARE(state_changed.size(), 0);
    QCOMPARE(playback_changed.size(), 0);
}

void PlayerPresenterTests::currentReadyEventAutoplaysAndStaleEventsAreIgnored() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::filesystem::path source{LR"(C:\audio\ready.wav)"};
    PlayerPresenter presenter(platform, backend, metadata, [source] { return std::vector{source}; });
    QVERIFY(presenter.openAudio());
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);

    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 9'000,
                     .duration_ms = 60'000,
                     .seekable = true,
                     .generation = 0});
    QCoreApplication::processEvents();
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
    QCOMPARE(backend.play_calls, 0);

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 60'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Ready);
    QCOMPARE(presenter.durationMs(), 60'000);
    QVERIFY(presenter.seekable());

    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 500,
                     .duration_ms = 60'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QCOMPARE(presenter.positionMs(), 500);
    QCOMPARE(backend.play_calls, 1);
}

void PlayerPresenterTests::localTransportAcceptsAsynchronousPlayAndSeekWithALocalQueue() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::filesystem::path source{LR"(C:\audio\transport.wav)"};
    PlayerPresenter presenter(platform, backend, metadata, [source] { return std::vector{source}; });
    QVERIFY(presenter.openAudio());
    backend.publish({.status = PlaybackStatus::Ready,
                     .position_ms = 5'000,
                     .duration_ms = 60'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Ready);
    QCOMPARE(backend.play_calls, 1);

    QVERIFY(presenter.togglePlayback());
    QCOMPARE(backend.play_calls, 2);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Ready);
    QVERIFY(presenter.beginSeekPreview(10'000));
    QVERIFY(presenter.updateSeekPreview(20'000));
    QVERIFY(presenter.commitSeekPreview());
    QCOMPARE(backend.seek_positions, std::vector<std::int64_t>{20'000});
    QVERIFY(presenter.seekPending());

    backend.publish({.status = PlaybackStatus::Ready,
                     .position_ms = 20'000,
                     .duration_ms = 60'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_VERIFY(!presenter.seekPending());
    QCOMPARE(presenter.positionMs(), 20'000);

    QVERIFY(presenter.beginSeekPreview(20'000));
    QVERIFY(presenter.commitSeekPreview());
    QVERIFY(!presenter.seekPending());
    QVERIFY(!presenter.seekPreviewActive());
    QCOMPARE(backend.seek_positions, std::vector<std::int64_t>{20'000});

    backend.seek_result = PlaybackCommandResult::NotSeekable;
    QVERIFY(presenter.beginSeekPreview(30'000));
    QVERIFY(!presenter.commitSeekPreview());
    QVERIFY(!presenter.seekPending());
    QVERIFY(!presenter.seekPreviewActive());
    QCOMPARE(presenter.positionMs(), 20'000);

    backend.publish({.status = PlaybackStatus::Paused,
                     .position_ms = 60'000,
                     .duration_ms = 60'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QVERIFY(presenter.togglePlayback());
    QCOMPARE(backend.play_calls, 3);
}

void PlayerPresenterTests::rejectedLocalSeekNotifiesRestoredStateBeforeFeedback_data() {
    QTest::addColumn<int>("rejection");
    QTest::addColumn<int>("command");
    for (const auto rejection :
         {PlaybackCommandResult::NotSeekable, PlaybackCommandResult::Busy, PlaybackCommandResult::NotReady}) {
        for (int command = 0; command < 3; ++command) {
            const auto name =
                QByteArray::number(static_cast<int>(rejection)) + "-command-" + QByteArray::number(command);
            QTest::newRow(name.constData()) << static_cast<int>(rejection) << command;
        }
    }
}

void PlayerPresenterTests::rejectedLocalSeekNotifiesRestoredStateBeforeFeedback() {
    QFETCH(int, rejection);
    QFETCH(int, command);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    metadata.start_result = MetadataCommandResult::Shutdown;
    PresenterSessionWriter writer;
    PresenterPreferencesClock clock;
    PresenterPreferencesWriter preferences_writer;
    PreferencesAutosave preferences_autosave{preferences_writer, {}, {.now = [&clock] { return clock.now(); }}};
    SessionAutosave autosave{writer, {}, {.now = [&clock] { return clock.now(); }}};
    const auto source = localTestPath(u8"Seek recusado.wav");
    PlayerPresenter presenter(
        platform, backend, metadata, [source] { return std::vector{source}; },
        PlayerPresenterPreferences{.autosave = &preferences_autosave,
                                   .session = {.autosave = &autosave, .now = [&clock] { return clock.now(); }}});
    QVERIFY(presenter.openAudio());
    backend.publish({.status = PlaybackStatus::Paused,
                     .position_ms = 20'000,
                     .duration_ms = 60'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    const auto occurrence = presenter.currentOccurrenceId();
    const auto* queue = presenter.queueModel();
    const int play_calls = backend.play_calls;
    const int pause_calls = backend.pause_calls;
    if (command == 0)
        QVERIFY(presenter.beginSeekPreview(30'000));
    backend.seek_result = static_cast<PlaybackCommandResult>(rejection);
    QStringList notifications;
    connect(&presenter, &PlayerPresenter::playbackChanged, &presenter, [&] {
        notifications.append(QStringLiteral("playback"));
        QCOMPARE(presenter.positionMs(), 20'000);
        QVERIFY(!presenter.seekPreviewActive());
        QVERIFY(!presenter.seekPending());
    });
    connect(&presenter, &PlayerPresenter::seekRejected, &presenter,
            [&] { notifications.append(QStringLiteral("rejected")); });

    const auto accepted = command == 0   ? presenter.commitSeekPreview()
                          : command == 1 ? presenter.skipBackward()
                                         : presenter.skipForward();
    QVERIFY(!accepted);
    QCOMPARE(notifications, QStringList({QStringLiteral("playback"), QStringLiteral("rejected")}));
    QCOMPARE(presenter.confirmedPositionMs(), 20'000);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QCOMPARE(presenter.currentOccurrenceId(), occurrence);
    QCOMPARE(presenter.queueModel(), queue);
    QCOMPARE(backend.loaded_paths, std::vector{source});
    const std::int64_t target = command == 0 ? 30'000 : (command == 1 ? 15'000 : 25'000);
    QCOMPARE(backend.seek_positions, std::vector<std::int64_t>{target});
    QCOMPARE(backend.play_calls, play_calls);
    QCOMPARE(backend.pause_calls, pause_calls);
    QVERIFY(!presenter.commitSeekPreview());
    QCOMPARE(notifications.size(), 2);

    if (writer.active.has_value())
        writer.complete(SessionSaveStatus::Saved);
    autosave.requestSaveNow();
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QVERIFY(!writer.starts.empty());
    const auto& saved = writer.starts.back().snapshot;
    QCOMPARE(saved.position_ms, std::int64_t{20'000});
    QCOMPARE(saved.queue.items.size(), std::size_t{1});
    QCOMPARE(saved.queue.items.front().source_path, source);
    QVERIFY(saved.queue.current_occurrence_id.has_value());
    QCOMPARE(LocalQueueListModel::occurrenceId(*saved.queue.current_occurrence_id), occurrence);
}

void PlayerPresenterTests::rejectedLocalSeekNotificationMayDestroyThePresenter() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const auto source = localTestPath(u8"Seek e destruição.wav");
    auto presenter =
        std::make_unique<PlayerPresenter>(platform, backend, metadata, [source] { return std::vector{source}; });
    QVERIFY(presenter->openAudio());
    backend.publish({.status = PlaybackStatus::Paused,
                     .position_ms = 20'000,
                     .duration_ms = 60'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter->playbackState(), PlayerPresenter::PlaybackState::Paused);
    QVERIFY(presenter->beginSeekPreview(30'000));
    backend.seek_result = PlaybackCommandResult::NotSeekable;
    connect(presenter.get(), &PlayerPresenter::playbackChanged, this, [&] { presenter.reset(); });

    QVERIFY(!presenter->commitSeekPreview());
    QVERIFY(!presenter);
    QVERIFY(!backend.callbackInstalled());
}

void PlayerPresenterTests::rejectedLocalSeekDoesNotAnnounceAfterAReentrantTrackChange() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::vector sources{localTestPath(u8"Seek anterior.wav"), localTestPath(u8"Faixa seguinte.wav")};
    PlayerPresenter presenter(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(presenter.openAudio());
    backend.publish({.status = PlaybackStatus::Paused,
                     .position_ms = 20'000,
                     .duration_ms = 60'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QVERIFY(presenter.beginSeekPreview(30'000));
    backend.seek_result = PlaybackCommandResult::Busy;
    QSignalSpy rejected(&presenter, &PlayerPresenter::seekRejected);
    connect(
        &presenter, &PlayerPresenter::playbackChanged, &presenter, [&] { QVERIFY(presenter.nextTrack()); },
        Qt::SingleShotConnection);

    QVERIFY(!presenter.commitSeekPreview());
    QCOMPARE(presenter.currentQueueIndex(), 1);
    QCOMPARE(backend.loaded_paths, sources);
    QCOMPARE(rejected.size(), 0);
}

void PlayerPresenterTests::inertLocalSeekCommandsDoNotAnnounceRejection() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PlayerPresenter presenter(platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; });
    QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);
    QSignalSpy rejected(&presenter, &PlayerPresenter::seekRejected);
    QVERIFY(!presenter.commitSeekPreview());
    QVERIFY(!presenter.skipBackward());
    QVERIFY(!presenter.skipForward());
    QVERIFY(backend.seek_positions.empty());
    QCOMPARE(playback.size(), 0);
    QCOMPARE(rejected.size(), 0);
}

void PlayerPresenterTests::currentMetadataCompletionReplacesFallbackAndStopsPolling() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::filesystem::path source{LR"(C:\audio\metadata.wav)"};
    TrackMetadata track{
        .source_path = source,
        .file_size_bytes = 123,
        .tags = {.title = std::string{"Decoded title"},
                 .artist = std::string{"Decoded artist"},
                 .album = std::string{"Decoded album"}},
        .display_title = "Decoded title",
        .audio = {.duration_ms = 91'000, .container = "WAV", .codec = "PCM"},
    };
    metadata.poll_results.push_back(
        {MetadataPollStatus::Completed, MetadataReadCompletion{MetadataGeneration{1}, source, std::move(track)}});
    PlayerPresenter presenter(platform, backend, metadata, [source] { return std::vector{source}; });
    QSignalSpy states(&presenter, &PlayerPresenter::stateChanged);
    QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);
    QAbstractItemModel* const model = presenter.queueModelObject();
    QSignalSpy model_changes(model, &QAbstractItemModel::dataChanged);

    QVERIFY(presenter.openAudio());
    QTRY_COMPARE(presenter.currentTitle(), QStringLiteral("Decoded title"));

    QCOMPARE(presenter.currentArtist(), QStringLiteral("Decoded artist"));
    QCOMPARE(presenter.currentAlbum(), QStringLiteral("Decoded album"));
    QCOMPARE(model->data(model->index(0, 0), QueueListModel::TitleRole).toString(), QStringLiteral("Decoded title"));
    QCOMPARE(model->data(model->index(0, 0), QueueListModel::ArtistRole).toString(), QStringLiteral("Decoded artist"));
    QCOMPARE(model->data(model->index(0, 0), QueueListModel::AlbumRole).toString(), QStringLiteral("Decoded album"));
    QCOMPARE(model->data(model->index(0, 0), QueueListModel::DurationMsRole).toLongLong(), 91'000);
    QCOMPARE(model_changes.size(), 1);
    QVERIFY(presenter.hasDuration());
    QCOMPARE(presenter.durationMs(), 91'000);
    QCOMPARE(metadata.polled_generations, std::vector<MetadataGeneration>{MetadataGeneration{1}});
    const QList<QTimer*> timers = presenter.findChildren<QTimer*>(QString{}, Qt::FindDirectChildrenOnly);
    QCOMPARE(timers.size(), 1);
    QVERIFY(!timers.front()->isActive());
    QCOMPARE(states.size(), 2);
    QCOMPARE(playback.size(), 2);
}

void PlayerPresenterTests::projectsCompleteFixtureDetailsWithExplicitUnknowns() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);

    QVERIFY(!presenter.detailsAvailable());
    QVERIFY(!presenter.hasDetailsFileName());
    QVERIFY(presenter.detailsFileName().isEmpty());
    QVERIFY(!presenter.hasDetailsFilePath());
    QVERIFY(presenter.detailsFilePath().isEmpty());
    QVERIFY(!presenter.hasDetailsFileSize());
    QVERIFY(!presenter.detailsFileSizeBytes().isValid());
    QVERIFY(presenter.detailsFileSizeBytesText().isEmpty());
    QVERIFY(!presenter.hasDetailsModifiedUtc());
    QVERIFY(presenter.detailsModifiedUtc().isEmpty());
    QVERIFY(!presenter.hasDetailsTrackNumber());
    QVERIFY(!presenter.detailsTrackNumber().isValid());
    QVERIFY(!presenter.hasDetailsYear());
    QVERIFY(!presenter.detailsYear().isValid());
    QVERIFY(!presenter.hasDetailsDuration());
    QVERIFY(!presenter.detailsDurationMs().isValid());
    QVERIFY(!presenter.hasDetailsBitDepth());
    QVERIFY(!presenter.detailsBitDepth().isValid());
    QVERIFY(!presenter.hasDetailsDiagnostics());
    QVERIFY(presenter.detailsDiagnostics().isEmpty());

    QVERIFY(presenter.loadFrozenQueue());

    QVERIFY(presenter.detailsAvailable());
    QVERIFY(presenter.hasDetailsFileName());
    QCOMPARE(presenter.detailsFileName(), QStringLiteral("Canção nº 17 — São João, ação e café ☕.m4a"));
    QVERIFY(!presenter.hasDetailsFilePath());
    QVERIFY(presenter.detailsFilePath().isEmpty());
    QVERIFY(presenter.hasDetailsFileSize());
    QCOMPARE(presenter.detailsFileSizeBytes().toULongLong(), qulonglong{7'483'921});
    QCOMPARE(presenter.detailsFileSizeBytesText(), QStringLiteral("7483921"));
    QVERIFY(presenter.hasDetailsModifiedUtc());
    QCOMPARE(presenter.detailsModifiedUtc(), QStringLiteral("2026-08-23T00:00:00Z"));
    QVERIFY(presenter.hasDetailsTitle());
    QCOMPARE(presenter.detailsTitle(), QStringLiteral("Canção nº 17 — São João, ação e café ☕"));
    QVERIFY(presenter.hasDetailsArtist());
    QCOMPARE(presenter.detailsArtist(), QStringLiteral("Artista Sintética Ç"));
    QVERIFY(presenter.hasDetailsAlbum());
    QCOMPARE(presenter.detailsAlbum(), QStringLiteral("Coleção Ação e Emoção"));
    QVERIFY(!presenter.hasDetailsTrackNumber());
    QVERIFY(!presenter.detailsTrackNumber().isValid());
    QVERIFY(!presenter.hasDetailsYear());
    QVERIFY(!presenter.detailsYear().isValid());
    QVERIFY(presenter.hasDetailsDuration());
    QCOMPARE(presenter.detailsDurationMs().toLongLong(), qint64{248'000});
    QVERIFY(presenter.hasDetailsContainer());
    QCOMPARE(presenter.detailsContainer(), QStringLiteral("M4A"));
    QVERIFY(presenter.hasDetailsCodec());
    QCOMPARE(presenter.detailsCodec(), QStringLiteral("AAC-LC"));
    QVERIFY(presenter.hasDetailsBitrate());
    QCOMPARE(presenter.detailsBitrateBps().toULongLong(), qulonglong{256'000});
    QVERIFY(presenter.hasDetailsSampleRate());
    QCOMPARE(presenter.detailsSampleRateHz().toULongLong(), qulonglong{48'000});
    QVERIFY(presenter.hasDetailsChannels());
    QCOMPARE(presenter.detailsChannels().toUInt(), 2U);
    QVERIFY(!presenter.hasDetailsBitDepth());
    QVERIFY(!presenter.detailsBitDepth().isValid());
    QVERIFY(!presenter.hasDetailsDiagnostics());
    QVERIFY(presenter.detailsDiagnostics().isEmpty());
}

void PlayerPresenterTests::projectsStructuredLocalDetailsWithoutLeakingReaderMessages() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::filesystem::path source = localTestPath(u8"Coleção 日本/Faixa número 07.mp3");
    TrackMetadata track{
        .source_path = source,
        .file_size_bytes = 4'096,
        .last_modified_time = std::filesystem::file_time_type::clock::now() - std::chrono::hours{24},
        .tags = {.title = std::string{"Título controlado"},
                 .artist = std::string{"Artista"},
                 .album = std::string{"Álbum"},
                 .track_number = 7,
                 .year = 2026},
        .display_title = "Título controlado",
        .audio = {.duration_ms = 123'456,
                  .sample_rate_hz = 48'000,
                  .channels = 2,
                  .bit_depth = 24,
                  .bitrate_kbps = 320,
                  .container = "MP3",
                  .codec = "MPEG-1 Audio Layer III"},
        .diagnostics = {{.code = MetadataDiagnosticCode::FieldTooLarge,
                         .field = MetadataField::Title,
                         .observed_bytes = 16'385,
                         .limit_bytes = 16'384},
                        {.code = MetadataDiagnosticCode::RetainedMetadataTooLarge,
                         .field = MetadataField::Album,
                         .observed_bytes = 262'145,
                         .limit_bytes = 262'144}},
        .artwork_diagnostics = {{.code = ArtworkDiagnosticCode::AxisPixelsExceeded,
                                 .encoded_bytes = 1'024,
                                 .width_pixels = 4'097,
                                 .height_pixels = 320}},
    };
    metadata.poll_results.push_back(
        {MetadataPollStatus::Completed, MetadataReadCompletion{MetadataGeneration{1}, source, std::move(track)}});
    const std::filesystem::path failed_source = localTestPath(u8"Falha secreta.flac");
    PlayerPresenter presenter(
        platform, backend, metadata,
        [selections = std::deque<std::vector<std::filesystem::path>>{{source}, {failed_source}}]() mutable {
            auto selection = std::move(selections.front());
            selections.pop_front();
            return selection;
        });

    QVERIFY(presenter.openAudio());
    QTRY_VERIFY(presenter.hasDetailsContainer());

    QVERIFY(presenter.detailsAvailable());
    QVERIFY(presenter.hasDetailsFileName());
    QCOMPARE(presenter.detailsFileName(), displayPath(source.filename()));
    QVERIFY(presenter.hasDetailsFilePath());
    QCOMPARE(presenter.detailsFilePath(), displayPath(source));
    QVERIFY(presenter.hasDetailsFileSize());
    QCOMPARE(presenter.detailsFileSizeBytes().toULongLong(), qulonglong{4'096});
    QCOMPARE(presenter.detailsFileSizeBytesText(), QStringLiteral("4096"));
    QVERIFY(presenter.hasDetailsModifiedUtc());
    const QString modified_utc = presenter.detailsModifiedUtc();
    QVERIFY(modified_utc.endsWith(QLatin1Char('Z')));
    const QDateTime parsed_modified_utc = QDateTime::fromString(modified_utc, Qt::ISODate);
    QVERIFY(parsed_modified_utc.isValid());
    QCOMPARE(parsed_modified_utc.offsetFromUtc(), 0);
    QVERIFY(std::abs(parsed_modified_utc.secsTo(QDateTime::currentDateTimeUtc().addSecs(-24 * 60 * 60))) <= 2);
    QVERIFY(presenter.hasDetailsTitle());
    QCOMPARE(presenter.detailsTitle(), QStringLiteral("Título controlado"));
    QVERIFY(presenter.hasDetailsArtist());
    QCOMPARE(presenter.detailsArtist(), QStringLiteral("Artista"));
    QVERIFY(presenter.hasDetailsAlbum());
    QCOMPARE(presenter.detailsAlbum(), QStringLiteral("Álbum"));
    QVERIFY(presenter.hasDetailsTrackNumber());
    QCOMPARE(presenter.detailsTrackNumber().toUInt(), 7U);
    QVERIFY(presenter.hasDetailsYear());
    QCOMPARE(presenter.detailsYear().toUInt(), 2026U);
    QVERIFY(presenter.hasDetailsDuration());
    QCOMPARE(presenter.detailsDurationMs().toLongLong(), qint64{123'456});
    QVERIFY(presenter.hasDetailsContainer());
    QCOMPARE(presenter.detailsContainer(), QStringLiteral("MP3"));
    QVERIFY(presenter.hasDetailsCodec());
    QCOMPARE(presenter.detailsCodec(), QStringLiteral("MPEG-1 Audio Layer III"));
    QVERIFY(presenter.hasDetailsBitrate());
    QCOMPARE(presenter.detailsBitrateBps().toULongLong(), qulonglong{320'000});
    QVERIFY(presenter.hasDetailsSampleRate());
    QCOMPARE(presenter.detailsSampleRateHz().toULongLong(), qulonglong{48'000});
    QVERIFY(presenter.hasDetailsChannels());
    QCOMPARE(presenter.detailsChannels().toUInt(), 2U);
    QVERIFY(presenter.hasDetailsBitDepth());
    QCOMPARE(presenter.detailsBitDepth().toUInt(), 24U);
    QVERIFY(presenter.hasDetailsDiagnostics());
    const QVariantList expected_diagnostics{
        QVariantMap{{QStringLiteral("code"), QStringLiteral("metadata_field_too_large")},
                    {QStringLiteral("field"), QStringLiteral("title")},
                    {QStringLiteral("observedBytes"), QVariant::fromValue<qulonglong>(16'385)},
                    {QStringLiteral("observedBytesText"), QStringLiteral("16385")},
                    {QStringLiteral("limitBytes"), QVariant::fromValue<qulonglong>(16'384)},
                    {QStringLiteral("limitBytesText"), QStringLiteral("16384")}},
        QVariantMap{{QStringLiteral("code"), QStringLiteral("metadata_total_too_large")},
                    {QStringLiteral("field"), QStringLiteral("album")},
                    {QStringLiteral("observedBytes"), QVariant::fromValue<qulonglong>(262'145)},
                    {QStringLiteral("observedBytesText"), QStringLiteral("262145")},
                    {QStringLiteral("limitBytes"), QVariant::fromValue<qulonglong>(262'144)},
                    {QStringLiteral("limitBytesText"), QStringLiteral("262144")}},
        QVariantMap{{QStringLiteral("code"), QStringLiteral("artwork_axis_pixels_exceeded")},
                    {QStringLiteral("encodedBytes"), QVariant::fromValue<qulonglong>(1'024)},
                    {QStringLiteral("encodedBytesText"), QStringLiteral("1024")},
                    {QStringLiteral("widthPixels"), QVariant::fromValue<qulonglong>(4'097)},
                    {QStringLiteral("heightPixels"), QVariant::fromValue<qulonglong>(320)}},
    };
    QCOMPARE(presenter.detailsDiagnostics(), expected_diagnostics);

    metadata.poll_results.push_back(
        {MetadataPollStatus::Completed,
         MetadataReadCompletion{
             MetadataGeneration{2}, failed_source,
             MetadataReadError{MetadataReadErrorCode::InvalidData, "DO NOT EXPOSE: C:\\private\\reader-secret.flac"}}});
    QVERIFY(presenter.openAudio());

    // Accepting a new generation clears every detail owned by the replaced metadata immediately.
    QVERIFY(presenter.detailsAvailable());
    QVERIFY(!presenter.hasDetailsFileSize());
    QVERIFY(!presenter.detailsFileSizeBytes().isValid());
    QVERIFY(presenter.detailsFileSizeBytesText().isEmpty());
    QVERIFY(!presenter.hasDetailsModifiedUtc());
    QVERIFY(presenter.detailsModifiedUtc().isEmpty());
    QVERIFY(!presenter.hasDetailsTitle());
    QVERIFY(!presenter.hasDetailsDiagnostics());

    QTRY_VERIFY(presenter.hasDetailsDiagnostics());

    QVERIFY(presenter.detailsAvailable());
    QCOMPARE(presenter.detailsFileName(), displayPath(failed_source.filename()));
    QCOMPARE(presenter.detailsFilePath(), displayPath(failed_source));
    QVERIFY(!presenter.hasDetailsFileSize());
    QVERIFY(!presenter.detailsFileSizeBytes().isValid());
    QVERIFY(presenter.detailsFileSizeBytesText().isEmpty());
    QVERIFY(!presenter.hasDetailsTitle());
    QVERIFY(presenter.detailsTitle().isEmpty());
    QVERIFY(!presenter.hasDetailsContainer());
    QVERIFY(presenter.detailsContainer().isEmpty());
    const QVariantList failed_diagnostics{
        QVariantMap{{QStringLiteral("code"), QStringLiteral("metadata_read_failed")},
                    {QStringLiteral("errorCode"), QStringLiteral("invalid_data")}},
    };
    QCOMPARE(presenter.detailsDiagnostics(), failed_diagnostics);
    QVERIFY(!presenter.detailsDiagnostics().front().toMap().values().contains(
        QStringLiteral("DO NOT EXPOSE: C:\\private\\reader-secret.flac")));
}

void PlayerPresenterTests::projectsSanitizedMetadataDiagnosticsAsClosedIdentifiers_data() {
    QTest::addColumn<MetadataDiagnosticCode>("code");
    QTest::addColumn<MetadataField>("field");
    QTest::addColumn<QString>("code_id");
    QTest::addColumn<QString>("field_id");
    QTest::newRow("sanitized-title") << MetadataDiagnosticCode::TextSanitized << MetadataField::Title
                                     << QStringLiteral("metadata_text_sanitized") << QStringLiteral("title");
    QTest::newRow("sanitized-artist") << MetadataDiagnosticCode::TextSanitized << MetadataField::Artist
                                      << QStringLiteral("metadata_text_sanitized") << QStringLiteral("artist");
    QTest::newRow("sanitized-album") << MetadataDiagnosticCode::TextSanitized << MetadataField::Album
                                     << QStringLiteral("metadata_text_sanitized") << QStringLiteral("album");
    QTest::newRow("invalid-track-number") << MetadataDiagnosticCode::InvalidValue << MetadataField::TrackNumber
                                          << QStringLiteral("metadata_invalid_value") << QStringLiteral("track_number");
    QTest::newRow("invalid-year") << MetadataDiagnosticCode::InvalidValue << MetadataField::Year
                                  << QStringLiteral("metadata_invalid_value") << QStringLiteral("year");
}

void PlayerPresenterTests::projectsSanitizedMetadataDiagnosticsAsClosedIdentifiers() {
    QFETCH(MetadataDiagnosticCode, code);
    QFETCH(MetadataField, field);
    QFETCH(QString, code_id);
    QFETCH(QString, field_id);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::filesystem::path source = localTestPath(u8"Diagnóstico seguro 日本語.mp3");
    TrackMetadata track{
        .source_path = source,
        .tags = {.title = std::string{"Título ajustado"}},
        .display_title = "Título ajustado",
        .diagnostics = {{.code = code, .field = field}},
    };
    metadata.poll_results.push_back(
        {MetadataPollStatus::Completed, MetadataReadCompletion{MetadataGeneration{1}, source, std::move(track)}});
    PlayerPresenter presenter(platform, backend, metadata,
                              [source] { return std::vector<std::filesystem::path>{source}; });
    QVERIFY(presenter.openAudio());
    QTRY_VERIFY(presenter.hasDetailsDiagnostics());
    const QVariantList expected{
        QVariantMap{{QStringLiteral("code"), code_id}, {QStringLiteral("field"), field_id}},
    };
    QCOMPARE(presenter.detailsDiagnostics(), expected);
    QVERIFY(!presenter.hasDetailsTrackNumber());
    QVERIFY(!presenter.detailsTrackNumber().isValid());
    QVERIFY(!presenter.hasDetailsYear());
    QVERIFY(!presenter.detailsYear().isValid());
    QCOMPARE(presenter.detailsTitle(), QStringLiteral("Título ajustado"));
    QCOMPARE(backend.loaded_paths.size(), std::size_t{1});
}

void PlayerPresenterTests::projectsExactDecimalByteCountsBeyondJavascriptSafeInteger() {
    if constexpr (std::numeric_limits<std::size_t>::digits <= 53)
        QSKIP("This contract requires a platform with size_t wider than JavaScript's safe integer range.");

    constexpr std::uint64_t beyond_javascript_safe_integer = 9'007'199'254'740'993ULL;
    const auto diagnostic_bytes = static_cast<std::size_t>(beyond_javascript_safe_integer);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::filesystem::path source = localTestPath(u8"Exact byte counts.flac");
    TrackMetadata track{
        .source_path = source,
        .file_size_bytes = beyond_javascript_safe_integer,
        .display_title = "Exact byte counts",
        .diagnostics = {{.code = MetadataDiagnosticCode::FieldTooLarge,
                         .field = MetadataField::Title,
                         .observed_bytes = diagnostic_bytes,
                         .limit_bytes = diagnostic_bytes - 1}},
        .artwork_diagnostics = {{.code = ArtworkDiagnosticCode::EncodedBytesExceeded,
                                 .encoded_bytes = diagnostic_bytes - 2}},
    };
    metadata.poll_results.push_back(
        {MetadataPollStatus::Completed, MetadataReadCompletion{MetadataGeneration{1}, source, std::move(track)}});
    PlayerPresenter presenter(platform, backend, metadata, [source] { return std::vector{source}; });

    QVERIFY(presenter.openAudio());
    QTRY_VERIFY(presenter.hasDetailsDiagnostics());

    QCOMPARE(presenter.detailsFileSizeBytes().toULongLong(), qulonglong{beyond_javascript_safe_integer});
    QCOMPARE(presenter.detailsFileSizeBytesText(), QStringLiteral("9007199254740993"));
    const QVariantList diagnostics = presenter.detailsDiagnostics();
    QCOMPARE(diagnostics.size(), 2);
    const QVariantMap metadata_diagnostic = diagnostics.at(0).toMap();
    QCOMPARE(metadata_diagnostic.value(QStringLiteral("observedBytes")).toULongLong(),
             qulonglong{beyond_javascript_safe_integer});
    QCOMPARE(metadata_diagnostic.value(QStringLiteral("observedBytesText")).toString(),
             QStringLiteral("9007199254740993"));
    QCOMPARE(metadata_diagnostic.value(QStringLiteral("limitBytesText")).toString(),
             QStringLiteral("9007199254740992"));
    const QVariantMap artwork_diagnostic = diagnostics.at(1).toMap();
    QCOMPARE(artwork_diagnostic.value(QStringLiteral("encodedBytesText")).toString(),
             QStringLiteral("9007199254740991"));
}

void PlayerPresenterTests::artworkCacheServesOnlyTheCurrentGenerationWithoutCopyingPixels() {
    auto cache = std::make_shared<ArtworkImageCache>();
    ArtworkImageProvider provider(cache);
    auto pixels =
        std::make_shared<const std::vector<std::uint8_t>>(std::vector<std::uint8_t>{255, 0, 0, 255, 0, 255, 0, 255});
    const ArtworkThumbnail artwork{.width_pixels = 2, .height_pixels = 1, .rgba8888 = pixels};

    cache->beginGeneration(MetadataGeneration{7});
    QVERIFY(cache->publish(MetadataGeneration{7}, artwork));
    const QUrl source = cache->currentSource();
    QCOMPARE(source.scheme(), QStringLiteral("image"));
    QCOMPARE(source.host(), ArtworkImageProvider::providerId());
    QVERIFY(!source.toString().startsWith(QStringLiteral("data:")));
    QCOMPARE(source.path().size(), 25);
    QVERIFY(provider.flags().testFlag(QQmlImageProviderBase::ForceAsynchronousImageLoading));

    QSize decoded_size;
    QImage decoded = provider.requestImage(source.path().sliced(1), &decoded_size, {});
    QCOMPARE(decoded_size, QSize(2, 1));
    QCOMPARE(decoded.size(), QSize(2, 1));
    QCOMPARE(decoded.constBits(), pixels->data());
    QCOMPARE(decoded.pixelColor(0, 0), QColor(255, 0, 0, 255));
    QCOMPARE(decoded.pixelColor(1, 0), QColor(0, 255, 0, 255));

    cache->beginGeneration(MetadataGeneration{8});
    QVERIFY(cache->currentSource().isEmpty());
    QVERIFY(!cache->publish(MetadataGeneration{7}, artwork));
    const ArtworkThumbnail oversized{
        .width_pixels = ArtworkLimits::thumbnail_max_axis_pixels + 1,
        .height_pixels = 1,
        .rgba8888 = std::make_shared<const std::vector<std::uint8_t>>(
            (ArtworkLimits::thumbnail_max_axis_pixels + 1) * 4, std::uint8_t{0}),
    };
    QVERIFY(!cache->publish(MetadataGeneration{8}, oversized));
    QVERIFY(provider.requestImage(source.path().sliced(1), nullptr, {}).isNull());
    pixels.reset();
    QCOMPARE(decoded.pixelColor(1, 0), QColor(0, 255, 0, 255));
}

void PlayerPresenterTests::currentArtworkUsesAnOpaqueUrlAndAcceptedOpenClearsIt() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::filesystem::path first_source{LR"(C:\audio\first.mp3)"};
    const std::filesystem::path rejected_source{LR"(C:\audio\rejected.mp3)"};
    const std::filesystem::path second_source{LR"(C:\audio\second.mp3)"};
    const std::vector<std::filesystem::path> selections{first_source, rejected_source, second_source};
    std::size_t selection_index = 0;
    auto pixels = std::make_shared<const std::vector<std::uint8_t>>(std::vector<std::uint8_t>{10, 20, 30, 255});
    TrackMetadata first_track{
        .source_path = first_source,
        .file_size_bytes = 123,
        .display_title = "First",
        .audio = {.duration_ms = 1'000, .container = "MP3", .codec = "MP3"},
        .artwork = ArtworkThumbnail{.width_pixels = 1, .height_pixels = 1, .rgba8888 = pixels},
    };
    metadata.poll_results.push_back({
        MetadataPollStatus::Completed,
        MetadataReadCompletion{MetadataGeneration{1}, first_source, std::move(first_track)},
    });
    auto cache = std::make_shared<ArtworkImageCache>();
    PlayerPresenter presenter(
        platform, backend, metadata,
        [&]() -> std::vector<std::filesystem::path> {
            if (selection_index >= selections.size())
                return {};
            return {selections[selection_index++]};
        },
        cache);

    QVERIFY(presenter.currentArtworkSource().isEmpty());
    QVERIFY(presenter.openAudio());
    QTRY_VERIFY(!presenter.currentArtworkSource().isEmpty());
    const QUrl first_url = presenter.currentArtworkSource();
    QCOMPARE(first_url.scheme(), QStringLiteral("image"));
    QCOMPARE(first_url.host(), ArtworkImageProvider::providerId());

    backend.load_result = PlaybackCommandResult::NotReady;
    QVERIFY(!presenter.openAudio());
    QCOMPARE(presenter.currentArtworkSource(), first_url);

    metadata.poll_results.push_back({
        MetadataPollStatus::Completed,
        MetadataReadCompletion{
            MetadataGeneration{1}, first_source,
            TrackMetadata{.source_path = first_source,
                          .artwork = ArtworkThumbnail{.width_pixels = 1, .height_pixels = 1, .rgba8888 = pixels}}},
    });
    backend.load_result = PlaybackCommandResult::Accepted;
    QVERIFY(presenter.openAudio());
    QVERIFY(presenter.currentArtworkSource().isEmpty());
    QTRY_COMPARE(metadata.polled_generations.back(), MetadataGeneration{2});
    QVERIFY(presenter.currentArtworkSource().isEmpty());
    ArtworkImageProvider provider(cache);
    QVERIFY(provider.requestImage(first_url.path().sliced(1), nullptr, {}).isNull());
}

void PlayerPresenterTests::terminalMetadataStatusStopsPollingWithoutReplacingFallback() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    metadata.poll_results.push_back({MetadataPollStatus::StaleGeneration, std::nullopt});
    const std::filesystem::path source{LR"(C:\audio\fallback.wav)"};
    PlayerPresenter presenter(platform, backend, metadata, [source] { return std::vector{source}; });
    QSignalSpy states(&presenter, &PlayerPresenter::stateChanged);

    QVERIFY(presenter.openAudio());
    QTRY_COMPARE(metadata.polled_generations.size(), std::size_t{1});

    QCOMPARE(presenter.currentTitle(), QStringLiteral("fallback"));
    const QList<QTimer*> timers = presenter.findChildren<QTimer*>(QString{}, Qt::FindDirectChildrenOnly);
    QCOMPARE(timers.size(), 1);
    QVERIFY(!timers.front()->isActive());
    QCOMPARE(states.size(), 2);
    QTest::qWait(timers.front()->interval() * 2);
    QCOMPARE(metadata.polled_generations.size(), std::size_t{1});
}

void PlayerPresenterTests::destructionClearsPlaybackCallbackAndCancelsPendingMetadata() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::filesystem::path source{LR"(C:\audio\pending.wav)"};
    auto presenter =
        std::make_unique<PlayerPresenter>(platform, backend, metadata, [source] { return std::vector{source}; });
    QVERIFY(backend.callbackInstalled());
    QVERIFY(presenter->openAudio());
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 1'000, .generation = 1});

    presenter.reset();

    QVERIFY(!backend.callbackInstalled());
    QCOMPARE(backend.callback_assignments, 2);
    QCOMPARE(metadata.cancelled_generations, std::vector<MetadataGeneration>{MetadataGeneration{1}});
    QCoreApplication::processEvents();
}

void PlayerPresenterTests::startsFromNullableEmptyStateAndAppliesEveryDefault() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);

    QCOMPARE(presenter.stateId(), QStringLiteral("empty"));
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Unloaded);
    QCOMPARE(presenter.queueLoaded(), false);
    QCOMPARE(presenter.hasCurrentOccurrence(), false);
    QCOMPARE(presenter.currentOccurrenceId(), QString{});
    QCOMPARE(presenter.currentQueueIndex(), -1);
    QCOMPARE(presenter.hasContextOccurrence(), false);
    QCOMPARE(presenter.contextOccurrenceId(), QString{});
    QCOMPARE(presenter.hasPosition(), false);
    QCOMPARE(presenter.positionMs(), 0);
    QCOMPARE(presenter.hasDuration(), false);
    QCOMPARE(presenter.durationMs(), 0);
    QCOMPARE(presenter.seekable(), false);
    QCOMPARE(presenter.assetId(), QStringLiteral("cover-placeholder"));
    QCOMPARE(presenter.hasMessage(), true);
    QCOMPARE(presenter.messageKey(), QStringLiteral("empty_title"));
    QCOMPARE(presenter.primaryActionId(), QStringLiteral("open-audio"));
    QCOMPARE(presenter.hasError(), false);
    QCOMPARE(presenter.errorCode(), QString{});
    QCOMPARE(presenter.hasCurrentTrack(), false);
    QCOMPARE(presenter.currentTitle(), QString{});
    QCOMPARE(presenter.currentArtist(), QString{});
    QCOMPARE(presenter.currentAlbum(), QString{});
    QCOMPARE(presenter.hasContextTrack(), false);
    QCOMPARE(presenter.contextTitle(), QString{});
    QCOMPARE(presenter.contextArtist(), QString{});
    QCOMPARE(presenter.contextAlbum(), QString{});

    const Preferences& preferences = presenter.preferences();
    QCOMPARE(preferences.theme, ThemeMode::System);
    QCOMPARE(preferences.motion, MotionMode::System);
    QCOMPARE(preferences.volume, 0.8);
    QCOMPARE(preferences.muted, false);
    QCOMPARE(preferences.skip_backward_seconds, 5);
    QCOMPARE(preferences.skip_forward_seconds, 5);
    QCOMPARE(preferences.close_behavior, CloseBehavior::MinimizeToTray);
    QCOMPARE(preferences.restore_last_session, true);
    QCOMPARE(preferences.start_minimized, false);
    QCOMPARE(preferences.queue_end_behavior, QueueEndBehavior::StopKeepSelection);
    QCOMPARE(presenter.volume(), 0.8);
    QCOMPARE(presenter.muted(), false);
}

void PlayerPresenterTests::appliesDefaultsThatDifferFromTheCoreDefaults() {
    AvailableTrayPlatform platform;
    FixtureBundle bundle = frozenBundle();
    FixtureDefaults& defaults = bundle.preferences.defaults;
    defaults.theme = QStringLiteral("light");
    defaults.motion = QStringLiteral("reduced");
    defaults.volume_percent = 35;
    defaults.muted = true;
    defaults.skip_backward_seconds = 7;
    defaults.skip_forward_seconds = 20;
    defaults.close_behavior = QStringLiteral("exit");
    defaults.restore_last_session = false;
    defaults.start_minimized = true;
    defaults.queue_end_behavior = QStringLiteral("stop_clear_selection");

    PlayerPresenter presenter(std::move(bundle), platform);
    const Preferences& preferences = presenter.preferences();
    QCOMPARE(preferences.theme, ThemeMode::Light);
    QCOMPARE(preferences.motion, MotionMode::Reduced);
    QCOMPARE(preferences.volume, 0.35);
    QCOMPARE(preferences.muted, true);
    QCOMPARE(preferences.skip_backward_seconds, 7);
    QCOMPARE(preferences.skip_forward_seconds, 20);
    QCOMPARE(preferences.close_behavior, CloseBehavior::Exit);
    QCOMPARE(preferences.restore_last_session, false);
    QCOMPARE(preferences.start_minimized, true);
    QCOMPARE(preferences.queue_end_behavior, QueueEndBehavior::StopClearSelection);
    QCOMPARE(presenter.volume(), 0.35);
    QCOMPARE(presenter.muted(), true);
}

void PlayerPresenterTests::loadsFrozenQueueOnceWithoutReplacingTheModel() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QueueListModel* const identity = presenter.queueModel();
    QAbstractItemModelTester tester(identity, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy state_changed(&presenter, &PlayerPresenter::stateChanged);
    QSignalSpy playback_changed(&presenter, &PlayerPresenter::playbackChanged);
    bool reset_was_atomic = false;
    connect(identity, &QAbstractItemModel::modelReset, &presenter, [&] {
        reset_was_atomic = identity->rowCount() == 1000 && presenter.queueLoaded() &&
                           presenter.stateId() == QStringLiteral("playing") &&
                           presenter.currentOccurrenceId() == QStringLiteral("queue-0017") &&
                           presenter.durationMs() == 248'000;
    });

    QVERIFY(presenter.loadFrozenQueue());
    QCOMPARE(presenter.queueModel(), identity);
    QCOMPARE(identity->rowCount(), 1000);
    QCOMPARE(presenter.stateId(), QStringLiteral("playing"));
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("queue-0017"));
    QCOMPARE(presenter.currentQueueIndex(), 16);
    QCOMPARE(presenter.hasCurrentOccurrence(), true);
    QCOMPARE(presenter.hasContextOccurrence(), false);
    QCOMPARE(presenter.contextOccurrenceId(), QString{});
    QCOMPARE(presenter.hasPosition(), true);
    QCOMPARE(presenter.positionMs(), 84'000);
    QCOMPARE(presenter.durationMs(), 248'000);
    QCOMPARE(presenter.hasDuration(), true);
    QCOMPARE(presenter.seekable(), true);
    QCOMPARE(presenter.assetId(), QStringLiteral("cover-track"));
    QCOMPARE(presenter.hasMessage(), false);
    QCOMPARE(presenter.messageKey(), QString{});
    QCOMPARE(presenter.primaryActionId(), QStringLiteral("play-pause"));
    QCOMPARE(presenter.hasError(), false);
    QCOMPARE(presenter.errorCode(), QString{});
    QCOMPARE(state_changed.size(), 1);
    QCOMPARE(playback_changed.size(), 1);
    QCOMPARE(reset_was_atomic, true);

    QVERIFY(!presenter.loadFrozenQueue());
    QCOMPARE(presenter.queueModel(), identity);
    QCOMPARE(identity->rowCount(), 1000);
    QCOMPARE(state_changed.size(), 1);
    QCOMPARE(playback_changed.size(), 1);
}

void PlayerPresenterTests::derivesBackendSnapshotFromTheTypedPlayingState() {
    AvailableTrayPlatform platform;
    FixtureBundle bundle = frozenBundle();
    FixtureState& playing = bundle.states[1];
    playing.playback_state = QStringLiteral("paused");
    playing.position_ms = 12'000;
    playing.duration_ms = 30'000;
    playing.seekable = false;

    PlayerPresenter presenter(std::move(bundle), platform);
    QVERIFY(presenter.loadFrozenQueue());
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QCOMPARE(presenter.positionMs(), 12'000);
    QCOMPARE(presenter.durationMs(), 30'000);
    QCOMPARE(presenter.seekable(), false);
}

void PlayerPresenterTests::rejectsUnknownMappingsAndExposesNoInfrastructureApi() {
    AvailableTrayPlatform platform;
    for (const QString field :
         {QStringLiteral("theme"), QStringLiteral("motion"), QStringLiteral("close"), QStringLiteral("queue-end"),
          QStringLiteral("playback"), QStringLiteral("default-state"), QStringLiteral("playing-missing")}) {
        FixtureBundle bundle = frozenBundle();
        if (field == QStringLiteral("theme"))
            bundle.preferences.defaults.theme = QStringLiteral("unknown");
        if (field == QStringLiteral("motion"))
            bundle.preferences.defaults.motion = QStringLiteral("unknown");
        if (field == QStringLiteral("close"))
            bundle.preferences.defaults.close_behavior = QStringLiteral("unknown");
        if (field == QStringLiteral("queue-end"))
            bundle.preferences.defaults.queue_end_behavior = QStringLiteral("unknown");
        if (field == QStringLiteral("playback"))
            bundle.states[1].playback_state = QStringLiteral("unknown");
        if (field == QStringLiteral("default-state"))
            bundle.default_state_id = QStringLiteral("missing");
        if (field == QStringLiteral("playing-missing"))
            bundle.states.removeAt(1);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                 static_cast<void>(PlayerPresenter(std::move(bundle), platform)));
    }

    PlayerPresenter presenter(frozenBundle(), platform);
    const QMetaObject* meta = presenter.metaObject();
    QVERIFY(meta->indexOfMethod("openAudio()") >= 0);
    QVERIFY(meta->indexOfMethod("loadFrozenQueue()") >= 0);
    for (const char* method :
         {"play()", "pause()", "togglePlayback()", "skipBackward()", "skipForward()", "beginSeekPreview(qlonglong)",
          "updateSeekPreview(qlonglong)", "commitSeekPreview()", "setVolume(double)", "toggleMute()",
          "skipUnavailable()", "previousTrack()", "nextTrack()"})
        QVERIFY(meta->indexOfMethod(method) >= 0);
    const int queue_model_property = meta->indexOfProperty("queueModel");
    QVERIFY(queue_model_property >= 0);
    QCOMPARE(meta->property(queue_model_property).metaType().id(), qMetaTypeId<QAbstractItemModel*>());
    for (const char* property_name :
         {"volume", "muted", "canGoPrevious", "canGoNext", "errorMessageKey", "errorActionId"}) {
        const int property_index = meta->indexOfProperty(property_name);
        QVERIFY(property_index >= 0);
        QVERIFY(meta->property(property_index).hasNotifySignal());
    }
    for (const char* property_name : {
             "detailsAvailable",      "hasDetailsFileName",  "detailsFileName",       "hasDetailsFilePath",
             "detailsFilePath",       "hasDetailsFileSize",  "detailsFileSizeBytes",  "detailsFileSizeBytesText",
             "hasDetailsModifiedUtc", "detailsModifiedUtc",  "hasDetailsTitle",       "detailsTitle",
             "hasDetailsArtist",      "detailsArtist",       "hasDetailsAlbum",       "detailsAlbum",
             "hasDetailsTrackNumber", "detailsTrackNumber",  "hasDetailsYear",        "detailsYear",
             "hasDetailsDuration",    "detailsDurationMs",   "hasDetailsContainer",   "detailsContainer",
             "hasDetailsCodec",       "detailsCodec",        "hasDetailsBitrate",     "detailsBitrateBps",
             "hasDetailsSampleRate",  "detailsSampleRateHz", "hasDetailsChannels",    "detailsChannels",
             "hasDetailsBitDepth",    "detailsBitDepth",     "hasDetailsDiagnostics", "detailsDiagnostics",
         }) {
        const int property_index = meta->indexOfProperty(property_name);
        QVERIFY2(property_index >= 0, property_name);
        const QMetaProperty property = meta->property(property_index);
        QVERIFY2(property.hasNotifySignal(), property_name);
        QCOMPARE(property.notifySignal().name(), QByteArrayView{"stateChanged"});
    }
    QCOMPARE(meta->property(meta->indexOfProperty("detailsFileSizeBytes")).metaType().id(), QMetaType::QVariant);
    QCOMPARE(meta->property(meta->indexOfProperty("detailsFileSizeBytesText")).metaType().id(), QMetaType::QString);
    QCOMPARE(meta->property(meta->indexOfProperty("detailsDiagnostics")).metaType().id(), QMetaType::QVariantList);
    QCOMPARE(meta->indexOfProperty("fixturePath"), -1);
    QCOMPARE(meta->indexOfProperty("queuePath"), -1);
    QCOMPARE(meta->indexOfProperty("backend"), -1);
    QCOMPARE(meta->indexOfProperty("timer"), -1);
    for (const char* method : {"advanceOneTick()", "rejectNextSeekForTest()", "enterFrozenUnavailableScenario()",
                               "loadFrozenUnavailableScenario()", "applyBackendSnapshot()"})
        QCOMPARE(meta->indexOfMethod(method), -1);
    QCOMPARE(presenter.findChildren<QTimer*>().size(), 1);
}

void PlayerPresenterTests::explicitPlayPauseShareIdempotentTransportSemantics() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QVERIFY(presenter.loadFrozenQueue());
    QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);

    QVERIFY(!presenter.play());
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QCOMPARE(playback.size(), 0);

    QVERIFY(presenter.pause());
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QCOMPARE(playback.size(), 1);
    QVERIFY(!presenter.pause());
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QCOMPARE(playback.size(), 1);

    QVERIFY(presenter.play());
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QCOMPARE(playback.size(), 2);
    QVERIFY(!presenter.play());
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QCOMPARE(playback.size(), 2);
}

void PlayerPresenterTests::asynchronousExplicitTransportUsesTheLatestUnacknowledgedIntent() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const std::filesystem::path source{LR"(C:\audio\transport-intent.wav)"};
    PlayerPresenter presenter(platform, backend, metadata,
                              [source] { return std::vector<std::filesystem::path>{source}; });
    QVERIFY(presenter.openAudio());
    backend.publish({.status = PlaybackStatus::Paused,
                     .position_ms = 1'000,
                     .duration_ms = 60'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    backend.play_calls = 0;
    backend.pause_calls = 0;

    QVERIFY(presenter.play());
    QVERIFY(!presenter.play());
    QCOMPARE(backend.play_calls, 1);
    QVERIFY(presenter.pause());
    QCOMPARE(backend.pause_calls, 0);

    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 1'000,
                     .duration_ms = 60'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QCOMPARE(backend.pause_calls, 1);
    QVERIFY(!presenter.pause());
    QCOMPARE(backend.pause_calls, 1);

    backend.publish({.status = PlaybackStatus::Paused,
                     .position_ms = 1'000,
                     .duration_ms = 60'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QVERIFY(presenter.play());
    QCOMPARE(backend.play_calls, 2);
    QVERIFY(!presenter.play());
}

void PlayerPresenterTests::navigationUsesTheLatestTransportIntentBeforeAcknowledgement() {
    const std::vector<std::filesystem::path> selections{localTestPath(u8"Primeira.mp3"),
                                                        localTestPath(u8"Segunda.m4a")};

    {
        AvailableTrayPlatform platform;
        PresenterPlaybackBackend backend;
        PresenterMetadataReader metadata;
        PlayerPresenter presenter(platform, backend, metadata, [selections] { return selections; });
        QVERIFY(presenter.openAudio());
        backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 60'000, .seekable = true, .generation = 1});
        QTRY_COMPARE(backend.play_calls, 1);
        backend.publish({.status = PlaybackStatus::Playing,
                         .position_ms = 1'000,
                         .duration_ms = 60'000,
                         .seekable = true,
                         .generation = 1});
        QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);

        QVERIFY(presenter.pause());
        QCOMPARE(backend.pause_calls, 1);
        QVERIFY(presenter.nextTrack());
        backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 60'000, .seekable = true, .generation = 2});
        QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Ready);
        QCOMPARE(backend.play_calls, 1);
    }

    {
        AvailableTrayPlatform platform;
        PresenterPlaybackBackend backend;
        PresenterMetadataReader metadata;
        PlayerPresenter presenter(platform, backend, metadata, [selections] { return selections; });
        QVERIFY(presenter.openAudio());
        backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 60'000, .seekable = true, .generation = 1});
        QTRY_COMPARE(backend.play_calls, 1);
        backend.publish({.status = PlaybackStatus::Paused,
                         .position_ms = 1'000,
                         .duration_ms = 60'000,
                         .seekable = true,
                         .generation = 1});
        QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);

        QVERIFY(presenter.play());
        QCOMPARE(backend.play_calls, 2);
        QVERIFY(presenter.nextTrack());
        backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 60'000, .seekable = true, .generation = 2});
        QTRY_COMPARE(backend.play_calls, 3);
    }
}

void PlayerPresenterTests::drivesTransportPreferencesAndManualTicksDeterministically() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);
    QSignalSpy preferences(&presenter, &PlayerPresenter::preferencesChanged);
    QSignalSpy rejected(&presenter, &PlayerPresenter::seekRejected);

    QVERIFY(!presenter.advanceOneTick());
    QCOMPARE(playback.size(), 0);
    QVERIFY(presenter.loadFrozenQueue());
    playback.clear();
    QCOMPARE(presenter.confirmedPositionMs(), 84'000);
    QCOMPARE(presenter.skipBackwardSeconds(), 5);
    QCOMPARE(presenter.skipForwardSeconds(), 5);

    QVERIFY(presenter.togglePlayback());
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QCOMPARE(playback.size(), 1);
    QVERIFY(!presenter.advanceOneTick());
    QCOMPARE(playback.size(), 1);
    QVERIFY(presenter.togglePlayback());
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);

    QVERIFY(presenter.skipBackward());
    QCOMPARE(presenter.positionMs(), 79'000);
    QCOMPARE(presenter.confirmedPositionMs(), 84'000);
    QVERIFY(presenter.advanceOneTick());
    QCOMPARE(presenter.confirmedPositionMs(), 79'000);
    QVERIFY(presenter.skipForward());
    QCOMPARE(presenter.positionMs(), 84'000);
    QCOMPARE(presenter.confirmedPositionMs(), 79'000);
    QVERIFY(presenter.seekPreviewActive());
    QVERIFY(presenter.seekPending());
    QVERIFY(!presenter.skipBackward());
    QVERIFY(presenter.advanceOneTick());
    QCOMPARE(presenter.positionMs(), 84'000);
    QCOMPARE(presenter.confirmedPositionMs(), 84'000);
    QVERIFY(!presenter.seekPreviewActive());
    QVERIFY(!presenter.seekPending());

    QVERIFY(presenter.beginSeekPreview(std::numeric_limits<qint64>::max()));
    QVERIFY(!presenter.beginSeekPreview(1));
    QCOMPARE(presenter.positionMs(), presenter.durationMs());
    QVERIFY(presenter.advanceOneTick());
    QCOMPARE(presenter.positionMs(), presenter.durationMs());
    QCOMPARE(presenter.confirmedPositionMs(), 84'200);
    QVERIFY(presenter.updateSeekPreview(std::numeric_limits<qint64>::min()));
    QCOMPARE(presenter.positionMs(), 0);
    const int same_update_signals = playback.size();
    QVERIFY(presenter.updateSeekPreview(0));
    QCOMPARE(playback.size(), same_update_signals);
    QVERIFY(presenter.commitSeekPreview());
    QVERIFY(!presenter.commitSeekPreview());
    QStringList rejection_signal_order;
    connect(&presenter, &PlayerPresenter::playbackChanged, &presenter,
            [&rejection_signal_order] { rejection_signal_order.append(QStringLiteral("playback")); });
    connect(&presenter, &PlayerPresenter::seekRejected, &presenter,
            [&rejection_signal_order] { rejection_signal_order.append(QStringLiteral("rejected")); });
    const int playback_before_rejection = playback.size();
    PlayerPresenterTestAccess::rejectNextSeek(presenter);
    QVERIFY(presenter.advanceOneTick());
    QCOMPARE(rejected.size(), 1);
    QCOMPARE(playback.size(), playback_before_rejection + 1);
    QCOMPARE(rejection_signal_order, QStringList({QStringLiteral("playback"), QStringLiteral("rejected")}));
    QCOMPARE(presenter.positionMs(), 84'200);
    QVERIFY(presenter.advanceOneTick());
    QCOMPARE(rejected.size(), 1);
    QCOMPARE(presenter.positionMs(), 84'400);

    QVERIFY(presenter.setVolume(0.25));
    QCOMPARE(presenter.volume(), 0.25);
    QCOMPARE(preferences.size(), 1);
    QVERIFY(!presenter.setVolume(std::numeric_limits<double>::quiet_NaN()));
    QVERIFY(!presenter.setVolume(std::numeric_limits<double>::infinity()));
    QVERIFY(!presenter.setVolume(-0.1));
    QVERIFY(!presenter.setVolume(2.0));
    QVERIFY(!presenter.setVolume(0.25));
    QCOMPARE(preferences.size(), 1);
    // Zero volume mutes; unmuting restores the last audible volume, and moving
    // the volume while muted unmutes.
    QVERIFY(presenter.setVolume(0.0));
    QVERIFY(presenter.muted());
    QVERIFY(presenter.toggleMute());
    QVERIFY(!presenter.muted());
    QCOMPARE(presenter.volume(), 0.25);
    QVERIFY(presenter.toggleMute());
    QVERIFY(presenter.muted());
    QVERIFY(presenter.setVolume(0.4));
    QVERIFY(!presenter.muted());
}

void PlayerPresenterTests::keepsUnavailableAndTerminalTransportInert() {
    AvailableTrayPlatform platform;
    PlayerPresenter empty(frozenBundle(), platform);
    QSignalSpy empty_playback(&empty, &PlayerPresenter::playbackChanged);
    QVERIFY(!empty.togglePlayback());
    QVERIFY(!empty.skipBackward());
    QVERIFY(!empty.beginSeekPreview(1));
    QCOMPARE(empty_playback.size(), 0);

    FixtureBundle error_bundle = frozenBundle();
    error_bundle.states[1].playback_state = QStringLiteral("error");
    PlayerPresenter error(std::move(error_bundle), platform);
    QVERIFY(error.loadFrozenQueue());
    QSignalSpy error_playback(&error, &PlayerPresenter::playbackChanged);
    QVERIFY(!error.togglePlayback());
    QVERIFY(!error.skipForward());
    QVERIFY(!error.beginSeekPreview(10));
    QVERIFY(!error.commitSeekPreview());
    QVERIFY(!error.advanceOneTick());
    QCOMPARE(error_playback.size(), 0);

    FixtureBundle terminal_bundle = frozenBundle();
    terminal_bundle.states[1].position_ms = 247'900;
    PlayerPresenter terminal(std::move(terminal_bundle), platform);
    QVERIFY(terminal.loadFrozenQueue());
    QVERIFY(terminal.advanceOneTick());
    QCOMPARE(terminal.positionMs(), 248'000);
    QCOMPARE(terminal.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QSignalSpy terminal_playback(&terminal, &PlayerPresenter::playbackChanged);
    QVERIFY(!terminal.togglePlayback());
    QCOMPARE(terminal_playback.size(), 0);

    FixtureBundle paused_bundle = frozenBundle();
    paused_bundle.states[1].playback_state = QStringLiteral("paused");
    PlayerPresenter paused(std::move(paused_bundle), platform);
    QVERIFY(paused.loadFrozenQueue());
    QVERIFY(paused.beginSeekPreview(90'000));
    QVERIFY(paused.commitSeekPreview());
    QVERIFY(paused.advanceOneTick());
    QCOMPARE(paused.positionMs(), 90'000);
    QCOMPARE(paused.playbackState(), PlayerPresenter::PlaybackState::Paused);
}

void PlayerPresenterTests::schedulesTicksOnlyWhilePlaybackOrSeekNeedsThem() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    const QList<QTimer*> timers = presenter.findChildren<QTimer*>(QString{}, Qt::FindDirectChildrenOnly);
    QCOMPARE(timers.size(), 1);
    QTimer* const timer = timers.front();
    QCOMPARE(timer->interval(), 200);
    QCOMPARE(timer->timerType(), Qt::PreciseTimer);
    QVERIFY(!timer->isActive());
    QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);
    QTest::qWait(250);
    QCOMPARE(playback.size(), 0);

    QVERIFY(presenter.loadFrozenQueue());
    QVERIFY(timer->isActive());
    QTRY_COMPARE_WITH_TIMEOUT(presenter.confirmedPositionMs(), 84'200, 500);
    QVERIFY(presenter.togglePlayback());
    QVERIFY(!timer->isActive());
    const qint64 paused_position = presenter.confirmedPositionMs();
    QTest::qWait(250);
    QCOMPARE(presenter.confirmedPositionMs(), paused_position);

    QVERIFY(presenter.beginSeekPreview(90'000));
    QVERIFY(presenter.commitSeekPreview());
    QVERIFY(timer->isActive());
    QTRY_COMPARE_WITH_TIMEOUT(presenter.confirmedPositionMs(), 90'000, 500);
    QVERIFY(!presenter.seekPending());
    QVERIFY(!timer->isActive());

    QSignalSpy rejected(&presenter, &PlayerPresenter::seekRejected);
    QVERIFY(presenter.beginSeekPreview(91'000));
    QVERIFY(presenter.commitSeekPreview());
    PlayerPresenterTestAccess::rejectNextSeek(presenter);
    QVERIFY(timer->isActive());
    QTRY_COMPARE_WITH_TIMEOUT(rejected.size(), 1, 500);
    QVERIFY(!presenter.seekPending());
    QVERIFY(!timer->isActive());

    QVERIFY(presenter.togglePlayback());
    QVERIFY(timer->isActive());
    QVERIFY(presenter.togglePlayback());
    QVERIFY(!timer->isActive());

    FixtureBundle terminal_bundle = frozenBundle();
    terminal_bundle.states[1].position_ms = 247'900;
    PlayerPresenter terminal(std::move(terminal_bundle), platform);
    QTimer* const terminal_timer = terminal.findChild<QTimer*>(QString{}, Qt::FindDirectChildrenOnly);
    QVERIFY(terminal_timer != nullptr);
    QVERIFY(terminal.loadFrozenQueue());
    QVERIFY(terminal_timer->isActive());
    QTRY_COMPARE_WITH_TIMEOUT(terminal.playbackState(), PlayerPresenter::PlaybackState::Paused, 500);
    QCOMPARE(terminal.positionMs(), 248'000);
    QVERIFY(!terminal_timer->isActive());
}

void PlayerPresenterTests::projectsTypedMetadataAndRecoversFrozenUnavailableScenario() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QueueListModel* const model = presenter.queueModel();
    QVERIFY(presenter.loadFrozenQueue());
    QTimer* const timer = presenter.findChild<QTimer*>(QString{}, Qt::FindDirectChildrenOnly);
    QVERIFY(timer != nullptr);
    QVERIFY(timer->isActive());
    QCOMPARE(presenter.hasCurrentTrack(), true);
    QCOMPARE(presenter.currentTitle(), QStringLiteral("Canção nº 17 — São João, ação e café ☕"));
    QCOMPARE(presenter.currentArtist(), QStringLiteral("Artista Sintética Ç"));
    QCOMPARE(presenter.currentAlbum(), QStringLiteral("Coleção Ação e Emoção"));
    QCOMPARE(presenter.hasContextTrack(), false);
    QCOMPARE(presenter.contextTitle(), QString{});
    QCOMPARE(presenter.contextArtist(), QString{});
    QCOMPARE(presenter.contextAlbum(), QString{});

    QVERIFY(presenter.beginSeekPreview(124'000));
    QVERIFY(presenter.commitSeekPreview());
    QVERIFY(presenter.seekPreviewActive());
    QVERIFY(presenter.seekPending());

    QSignalSpy state_changed(&presenter, &PlayerPresenter::stateChanged);
    QSignalSpy playback_changed(&presenter, &PlayerPresenter::playbackChanged);
    QVERIFY(experiment::PlayerPresenterScenarioControl::enterUnavailable(presenter));
    QCOMPARE(presenter.queueModel(), model);
    QCOMPARE(model->rowCount(), 1000);
    QCOMPARE(presenter.stateId(), QStringLiteral("error"));
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Error);
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("queue-0999"));
    QCOMPARE(presenter.currentQueueIndex(), 998);
    QCOMPARE(presenter.contextOccurrenceId(), QStringLiteral("queue-0017"));
    QCOMPARE(presenter.hasPosition(), false);
    QCOMPARE(presenter.positionMs(), 0);
    QCOMPARE(presenter.hasDuration(), false);
    QCOMPARE(presenter.durationMs(), 0);
    QCOMPARE(presenter.seekable(), false);
    QCOMPARE(presenter.seekPreviewActive(), false);
    QCOMPARE(presenter.seekPending(), false);
    QCOMPARE(presenter.hasError(), true);
    QCOMPARE(presenter.errorCode(), QStringLiteral("fixture_source_unavailable"));
    QCOMPARE(presenter.detailsAvailable(), true);
    QCOMPARE(presenter.detailsFileName(), QStringLiteral("faixa-indisponivel-0999.mp3"));
    QCOMPARE(presenter.hasDetailsFileSize(), false);
    QCOMPARE(presenter.detailsFileSizeBytes().isValid(), false);
    const QVariantList fixture_error_diagnostics{
        QVariantMap{{QStringLiteral("code"), QStringLiteral("playback_error")},
                    {QStringLiteral("errorCode"), QStringLiteral("fixture_source_unavailable")}},
    };
    QCOMPARE(presenter.detailsDiagnostics(), fixture_error_diagnostics);
    QCOMPARE(presenter.assetId(), QStringLiteral("cover-track"));
    QCOMPARE(presenter.messageKey(), QStringLiteral("error_body"));
    QCOMPARE(presenter.primaryActionId(), QStringLiteral("skip"));
    QCOMPARE(presenter.hasCurrentTrack(), true);
    QCOMPARE(presenter.currentTitle(), QStringLiteral("Faixa indisponível de teste"));
    QCOMPARE(presenter.currentArtist(), QStringLiteral("Artista sintético indisponível"));
    QCOMPARE(presenter.currentAlbum(), QStringLiteral("Falhas recuperáveis"));
    QCOMPARE(presenter.hasContextTrack(), true);
    QCOMPARE(presenter.contextTitle(), QStringLiteral("Canção nº 17 — São João, ação e café ☕"));
    QCOMPARE(presenter.contextArtist(), QStringLiteral("Artista Sintética Ç"));
    QCOMPARE(presenter.contextAlbum(), QStringLiteral("Coleção Ação e Emoção"));
    QCOMPARE(state_changed.size(), 1);
    QCOMPARE(playback_changed.size(), 1);
    QVERIFY(!timer->isActive());
    QVERIFY(!presenter.advanceOneTick());
    QCOMPARE(state_changed.size(), 1);
    QCOMPARE(playback_changed.size(), 1);

    QVERIFY(presenter.skipUnavailable());
    QCOMPARE(presenter.queueModel(), model);
    QCOMPARE(model->rowCount(), 1000);
    QCOMPARE(presenter.stateId(), QStringLiteral("playing"));
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("queue-0017"));
    QCOMPARE(presenter.currentQueueIndex(), 16);
    QCOMPARE(presenter.hasContextTrack(), false);
    QCOMPARE(state_changed.size(), 2);
    QCOMPARE(playback_changed.size(), 2);
    QVERIFY(timer->isActive());
    QVERIFY(!presenter.skipUnavailable());
    QCOMPARE(state_changed.size(), 2);
    QCOMPARE(playback_changed.size(), 2);
}

void PlayerPresenterTests::navigatesDistinctOccurrencesWithoutResettingTheModel() {
    AvailableTrayPlatform platform;
    const FixtureBundle bundle = frozenBundle();
    PlayerPresenter presenter(bundle, platform);
    QVERIFY(presenter.loadFrozenQueue());
    QVERIFY(presenter.setVolume(0.35));
    QVERIFY(presenter.toggleMute());
    QueueListModel* const model = presenter.queueModel();
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    QSignalSpy states(&presenter, &PlayerPresenter::stateChanged);
    QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);

    QVERIFY(presenter.canGoPrevious());
    QVERIFY(presenter.canGoNext());
    QVERIFY(presenter.nextTrack());
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("queue-0018"));
    QCOMPARE(presenter.currentQueueIndex(), 17);
    QCOMPARE(presenter.currentTitle(), bundle.queue.at(17).title);
    QCOMPARE(presenter.currentArtist(), bundle.queue.at(17).artist);
    QCOMPARE(presenter.currentAlbum(), bundle.queue.at(17).album);
    QCOMPARE(presenter.durationMs(), *bundle.queue.at(17).duration_ms);
    QCOMPARE(presenter.positionMs(), 0);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QCOMPARE(presenter.volume(), 0.35);
    QVERIFY(presenter.muted());
    QCOMPARE(states.size(), 1);
    QCOMPARE(playback.size(), 1);
    QCOMPARE(presenter.queueModel(), model);
    QCOMPARE(model->rowCount(), 1000);
    QCOMPARE(resets.size(), 0);

    QVERIFY(presenter.previousTrack());
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("queue-0017"));
    QCOMPARE(presenter.positionMs(), 0);
    QVERIFY(presenter.advanceOneTick());
    QCOMPARE(presenter.positionMs(), 200);
    QVERIFY(presenter.nextTrack());
    QVERIFY(presenter.nextTrack());
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("queue-0019"));
    QCOMPARE(resets.size(), 0);
}

void PlayerPresenterTests::previousUsesConfirmedThresholdAndPreservesPause() {
    AvailableTrayPlatform platform;
    for (const qint64 position : {2'999, 3'000, 3'001}) {
        FixtureBundle bundle = frozenBundle();
        bundle.states[1].playback_state = QStringLiteral("paused");
        bundle.states[1].position_ms = position;
        PlayerPresenter presenter(std::move(bundle), platform);
        QVERIFY(presenter.loadFrozenQueue());
        QSignalSpy states(&presenter, &PlayerPresenter::stateChanged);
        QVERIFY(presenter.previousTrack());
        QCOMPARE(presenter.positionMs(), 0);
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
        if (position > 3'000) {
            QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("queue-0017"));
            QCOMPARE(presenter.confirmedPositionMs(), position);
            QVERIFY(presenter.seekPending());
            QVERIFY(!presenter.previousTrack());
            QVERIFY(presenter.advanceOneTick());
            QVERIFY(!presenter.seekPending());
            QCOMPARE(presenter.confirmedPositionMs(), 0);
            QCOMPARE(states.size(), 0);
        } else {
            QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("queue-0016"));
            QVERIFY(!presenter.seekPending());
            QCOMPARE(states.size(), 1);
        }
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    }
}

void PlayerPresenterTests::navigationSkipsUnavailableItemsAndStopsAtBounds() {
    AvailableTrayPlatform platform;
    FixtureBundle bundle = frozenBundle();
    bundle.states[1].current_occurrence_id = QStringLiteral("queue-0998");
    bundle.states[1].position_ms = 0;
    bundle.states[1].duration_ms = bundle.queue.at(997).duration_ms;
    bundle.states[1].playback_state = QStringLiteral("paused");
    PlayerPresenter presenter(bundle, platform);
    QVERIFY(presenter.loadFrozenQueue());
    QueueListModel* const model = presenter.queueModel();
    QVERIFY(presenter.nextTrack());
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("queue-1000"));
    QCOMPARE(presenter.currentQueueIndex(), 999);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QVERIFY(!presenter.canGoNext());
    QVERIFY(!presenter.nextTrack());
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("queue-1000"));
    QVERIFY(!model->data(model->index(998, 0), QueueListModel::AvailableRole).toBool());
    QCOMPARE(model->data(model->index(998, 0), QueueListModel::ErrorCodeRole).toString(),
             QStringLiteral("fixture_source_unavailable"));
    QVERIFY(presenter.previousTrack());
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("queue-0998"));
    QCOMPARE(presenter.queueModel(), model);

    bundle.states[1].current_occurrence_id = QStringLiteral("queue-0001");
    bundle.states[1].duration_ms = bundle.queue.at(0).duration_ms;
    PlayerPresenter first(bundle, platform);
    QVERIFY(first.loadFrozenQueue());
    QVERIFY(!first.canGoPrevious());
    QVERIFY(!first.previousTrack());
    QCOMPARE(first.currentOccurrenceId(), QStringLiteral("queue-0001"));
    QCOMPARE(first.positionMs(), 0);
}

void PlayerPresenterTests::navigationIsInertDuringSeekAndInactiveStates() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QVERIFY(!presenter.canGoPrevious());
    QVERIFY(!presenter.canGoNext());
    QVERIFY(!presenter.previousTrack());
    QVERIFY(!presenter.nextTrack());
    QVERIFY(presenter.loadFrozenQueue());
    QVERIFY(presenter.beginSeekPreview(120'000));
    QVERIFY(!presenter.canGoPrevious());
    QVERIFY(!presenter.canGoNext());
    QVERIFY(!presenter.previousTrack());
    QVERIFY(!presenter.nextTrack());
    QVERIFY(presenter.commitSeekPreview());
    QVERIFY(!presenter.canGoPrevious());
    QVERIFY(!presenter.canGoNext());
    QVERIFY(!presenter.previousTrack());
    QVERIFY(!presenter.nextTrack());
    QVERIFY(presenter.advanceOneTick());
    QVERIFY(presenter.canGoPrevious());
    QVERIFY(presenter.canGoNext());
    QVERIFY(presenter.nextTrack());
    QVERIFY(experiment::PlayerPresenterScenarioControl::enterUnavailable(presenter));
    QVERIFY(!presenter.canGoPrevious());
    QVERIFY(!presenter.canGoNext());
    QVERIFY(!presenter.previousTrack());
    QVERIFY(!presenter.nextTrack());
    QVERIFY(presenter.skipUnavailable());
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("queue-0017"));
    QCOMPARE(presenter.positionMs(), 84'000);
}

void PlayerPresenterTests::exposesTypedPreferenceDefaults() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QCOMPARE(presenter.themePreference(), PlayerPresenter::ThemePreference::SystemTheme);
    QCOMPARE(presenter.motionPreference(), PlayerPresenter::MotionPreference::SystemMotion);
    QCOMPARE(presenter.closePreference(), PlayerPresenter::ClosePreference::MinimizeToTray);
    QCOMPARE(presenter.queueEndPreference(), PlayerPresenter::QueueEndPreference::StopKeepSelection);
    QVERIFY(presenter.restoreLastSession());
    QVERIFY(!presenter.startMinimized());
    QVERIFY(presenter.trayAvailable());
    const QMetaObject* meta = presenter.metaObject();
    for (const char* name : {"themePreference", "motionPreference", "closePreference", "queueEndPreference",
                             "restoreLastSession", "startMinimized", "trayAvailable"}) {
        const int index = meta->indexOfProperty(name);
        QVERIFY(index >= 0);
        const QMetaProperty property = meta->property(index);
        QVERIFY(property.isReadable());
        QVERIFY(!property.isWritable());
        QVERIFY(property.hasNotifySignal());
    }
    for (const char* name : {"themePreference", "motionPreference", "closePreference", "queueEndPreference"})
        QVERIFY(meta->property(meta->indexOfProperty(name)).isEnumType());
    QCOMPARE(meta->indexOfMethod("refreshPlatformAvailability()"), -1);
}

void PlayerPresenterTests::editsPreferencesWithoutChangingPlaybackOrTheQueue() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QVERIFY(presenter.loadFrozenQueue());
    QVERIFY(presenter.togglePlayback());
    QueueListModel* const model = presenter.queueModel();
    QSignalSpy preferences(&presenter, &PlayerPresenter::preferencesChanged);
    QSignalSpy states(&presenter, &PlayerPresenter::stateChanged);
    QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    QVERIFY(presenter.setThemePreference(PlayerPresenter::ThemePreference::DarkTheme));
    QCOMPARE(presenter.preferences().theme, ThemeMode::Dark);
    QVERIFY(presenter.setThemePreference(PlayerPresenter::ThemePreference::LightTheme));
    QCOMPARE(presenter.preferences().theme, ThemeMode::Light);
    QVERIFY(presenter.setThemePreference(PlayerPresenter::ThemePreference::SystemTheme));
    QCOMPARE(presenter.preferences().theme, ThemeMode::System);
    QVERIFY(presenter.setMotionPreference(PlayerPresenter::MotionPreference::ReducedMotion));
    QCOMPARE(presenter.preferences().motion, MotionMode::Reduced);
    QVERIFY(presenter.setMotionPreference(PlayerPresenter::MotionPreference::SystemMotion));
    QCOMPARE(presenter.preferences().motion, MotionMode::System);
    QVERIFY(presenter.setSkipBackwardSeconds(7));
    QVERIFY(presenter.setSkipForwardSeconds(12));
    QVERIFY(presenter.setClosePreference(PlayerPresenter::ClosePreference::Exit));
    QVERIFY(presenter.setRestoreLastSession(false));
    QVERIFY(presenter.setStartMinimized(true));
    QVERIFY(presenter.setQueueEndPreference(PlayerPresenter::QueueEndPreference::StopClearSelection));
    QCOMPARE(preferences.size(), 11);
    QCOMPARE(presenter.skipBackwardSeconds(), 7);
    QCOMPARE(presenter.skipForwardSeconds(), 12);
    QCOMPARE(presenter.preferences().close_behavior, CloseBehavior::Exit);
    QVERIFY(!presenter.restoreLastSession());
    QVERIFY(presenter.startMinimized());
    QCOMPARE(presenter.preferences().queue_end_behavior, QueueEndBehavior::StopClearSelection);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("queue-0017"));
    QCOMPARE(presenter.positionMs(), 84'000);
    QCOMPARE(presenter.volume(), 0.8);
    QVERIFY(!presenter.muted());
    QCOMPARE(presenter.queueModel(), model);
    QCOMPARE(resets.size(), 0);
    QCOMPARE(states.size(), 0);
    QCOMPARE(playback.size(), 0);

    QVERIFY(presenter.skipBackward());
    QCOMPARE(presenter.positionMs(), 77'000);
    QVERIFY(presenter.advanceOneTick());
    QVERIFY(presenter.skipForward());
    QCOMPARE(presenter.positionMs(), 89'000);
    QVERIFY(presenter.advanceOneTick());
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
}

void PlayerPresenterTests::rejectsInvalidAndUnchangedPreferencesWithoutNotifications() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QSignalSpy changed(&presenter, &PlayerPresenter::preferencesChanged);
    for (const int value : {-1, 99}) {
        QVERIFY(!presenter.setThemePreference(static_cast<PlayerPresenter::ThemePreference>(value)));
        QVERIFY(!presenter.setMotionPreference(static_cast<PlayerPresenter::MotionPreference>(value)));
        QVERIFY(!presenter.setClosePreference(static_cast<PlayerPresenter::ClosePreference>(value)));
        QVERIFY(!presenter.setQueueEndPreference(static_cast<PlayerPresenter::QueueEndPreference>(value)));
    }
    for (const int value : {std::numeric_limits<int>::min(), 0, 301, std::numeric_limits<int>::max()}) {
        QVERIFY(!presenter.setSkipBackwardSeconds(value));
        QVERIFY(!presenter.setSkipForwardSeconds(value));
    }
    QVERIFY(!presenter.setThemePreference(PlayerPresenter::ThemePreference::SystemTheme));
    QVERIFY(!presenter.setMotionPreference(PlayerPresenter::MotionPreference::SystemMotion));
    QVERIFY(!presenter.setClosePreference(PlayerPresenter::ClosePreference::MinimizeToTray));
    QVERIFY(!presenter.setQueueEndPreference(PlayerPresenter::QueueEndPreference::StopKeepSelection));
    QVERIFY(!presenter.setRestoreLastSession(true));
    QVERIFY(!presenter.setStartMinimized(false));
    QVERIFY(!presenter.setSkipBackwardSeconds(5));
    QVERIFY(!presenter.setSkipForwardSeconds(5));
    QCOMPARE(changed.size(), 0);
    QCOMPARE(presenter.themePreference(), PlayerPresenter::ThemePreference::SystemTheme);
    QCOMPARE(presenter.motionPreference(), PlayerPresenter::MotionPreference::SystemMotion);
    QCOMPARE(presenter.closePreference(), PlayerPresenter::ClosePreference::MinimizeToTray);
    QCOMPARE(presenter.queueEndPreference(), PlayerPresenter::QueueEndPreference::StopKeepSelection);
    QCOMPARE(presenter.skipBackwardSeconds(), 5);
    QCOMPARE(presenter.skipForwardSeconds(), 5);
    for (const int value : {1, 300}) {
        QVERIFY(presenter.setSkipBackwardSeconds(value));
        QVERIFY(presenter.setSkipForwardSeconds(value));
    }
    QCOMPARE(changed.size(), 4);
}

void PlayerPresenterTests::refreshesTrayAvailabilityAndGuardsStartMinimized() {
    AvailableTrayPlatform platform;
    platform.available = false;
    PlayerPresenter presenter(frozenBundle(), platform);
    QSignalSpy preferences(&presenter, &PlayerPresenter::preferencesChanged);
    QSignalSpy availability(&presenter, &PlayerPresenter::platformChanged);
    QVERIFY(!presenter.trayAvailable());
    QVERIFY(!presenter.setStartMinimized(true));
    QVERIFY(!presenter.startMinimized());
    QCOMPARE(preferences.size(), 0);
    presenter.refreshPlatformAvailability();
    QCOMPARE(availability.size(), 0);
    platform.available = true;
    presenter.refreshPlatformAvailability();
    QCOMPARE(availability.size(), 1);
    QVERIFY(presenter.trayAvailable());
    QVERIFY(presenter.setStartMinimized(true));
    QVERIFY(presenter.startMinimized());
    QCOMPARE(preferences.size(), 1);
    platform.available = false;
    presenter.refreshPlatformAvailability();
    QCOMPARE(availability.size(), 2);
    QVERIFY(!presenter.trayAvailable());
    QVERIFY(presenter.startMinimized());
    QCOMPARE(preferences.size(), 1);
    presenter.refreshPlatformAvailability();
    QCOMPARE(availability.size(), 2);
    QCOMPARE(preferences.size(), 1);
}

void PlayerPresenterTests::rejectsPersistedWindowPlacementWithNonPositiveSize() {
    AvailableTrayPlatform platform;
    for (const WindowPlacement placement : {WindowPlacement{0, 0, 0, 520}, WindowPlacement{0, 0, 720, -1}}) {
        PresenterPreferencesWriter writer;
        Preferences invalid{};
        invalid.window_placement = placement;
        PreferencesAutosave autosave{writer, invalid};

        QVERIFY_THROWS_EXCEPTION(
            std::invalid_argument,
            static_cast<void>(PlayerPresenter(frozenBundle(), platform,
                                              PlayerPresenterPreferences{.initial = invalid,
                                                                         .load_status = PreferencesLoadStatus::Loaded,
                                                                         .autosave = &autosave})));
    }
}

void PlayerPresenterTests::loadsPersistedPreferencesBeforePresentationWithoutAutosavingAValidDocument() {
    AvailableTrayPlatform platform;
    platform.available = false;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterPreferencesWriter writer;
    PresenterPreferencesClock clock;
    const Preferences loaded = nonDefaultPreferences();
    PreferencesAutosave autosave{writer, loaded, {.now = [&clock] { return clock.now(); }}};

    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
        PlayerPresenterPreferences{
            .initial = loaded, .load_status = PreferencesLoadStatus::Loaded, .autosave = &autosave});

    QCOMPARE(presenter.preferences(), loaded);
    QCOMPARE(presenter.themePreference(), PlayerPresenter::ThemePreference::DarkTheme);
    QCOMPARE(presenter.motionPreference(), PlayerPresenter::MotionPreference::ReducedMotion);
    QCOMPARE(presenter.closePreference(), PlayerPresenter::ClosePreference::Exit);
    QCOMPARE(presenter.queueEndPreference(), PlayerPresenter::QueueEndPreference::StopClearSelection);
    QCOMPARE(presenter.skipBackwardSeconds(), 7);
    QCOMPARE(presenter.skipForwardSeconds(), 20);
    QCOMPARE(presenter.volume(), 0.35);
    QVERIFY(presenter.muted());
    QVERIFY(!presenter.restoreLastSession());
    QVERIFY(presenter.startMinimized());
    QVERIFY(!presenter.trayAvailable());
    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::NoPreferenceWarning);
    QCOMPARE(backend.volumes.back(), 0.35);
    QCOMPARE(backend.muted_values.back(), true);
    QCOMPARE(autosave.latestRevision(), PreferencesRevision{0});
    QVERIFY(!autosave.dirty());
    QVERIFY(writer.starts.empty());

    presenter.refreshPlatformAvailability();
    QVERIFY(presenter.startMinimized());
    QCOMPARE(autosave.latestRevision(), PreferencesRevision{0});
}

void PlayerPresenterTests::restoresPersistedSessionPausedBeforePresentationWithoutAutoplay() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    metadata.start_result = MetadataCommandResult::Shutdown;
    PresenterPreferencesWriter preferences_writer;
    PresenterSessionWriter session_writer;
    PresenterPreferencesClock clock;
    Preferences preferences{};
    preferences.restore_last_session = true;
    PreferencesAutosave preferences_autosave{
        preferences_writer, preferences, {.now = [&clock] { return clock.now(); }}};
    const SessionSnapshot persisted = persistedSessionSnapshot();
    SessionAutosave session_autosave{session_writer, persisted, {.now = [&clock] { return clock.now(); }}};

    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
        PlayerPresenterPreferences{
            .initial = preferences,
            .load_status = PreferencesLoadStatus::Loaded,
            .autosave = &preferences_autosave,
            .session = {.initial = persisted,
                        .load_status = SessionLoadStatus::Loaded,
                        .autosave = &session_autosave,
                        .now = [&clock] { return clock.now(); }},
        });

    QCOMPARE(presenter.queueModelObject()->rowCount(), 2);
    QCOMPARE(presenter.currentQueueIndex(), 1);
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("local-42"));
    QCOMPARE(backend.loaded_paths, std::vector<std::filesystem::path>{persisted.queue.items[1].source_path});
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
    QCOMPARE(backend.play_calls, 0);
    QCOMPARE(session_autosave.latestRevision(), SessionRevision{0});
    QVERIFY(!session_autosave.dirty());

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 60'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.pause_calls, 1);
    QTRY_COMPARE(backend.seek_positions, std::vector<std::int64_t>{42'000});
    QCOMPARE(backend.play_calls, 0);

    backend.publish({.status = PlaybackStatus::Paused,
                     .position_ms = 42'000,
                     .duration_ms = 60'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QCOMPARE(presenter.confirmedPositionMs(), qint64{42'000});
    QCOMPARE(backend.play_calls, 0);
    QCOMPARE(session_autosave.latestRevision(), SessionRevision{0});
    QVERIFY(!session_autosave.dirty());
    QVERIFY(session_writer.starts.empty());

    backend.publishEvent(PlaybackEventType::SnapshotChanged,
                         {.status = PlaybackStatus::Playing,
                          .position_ms = 7,
                          .duration_ms = 60'000,
                          .seekable = true,
                          .generation = 999},
                         false);
    QCoreApplication::processEvents();
    QCOMPARE(presenter.confirmedPositionMs(), qint64{42'000});
    QCOMPARE(session_autosave.latestRevision(), SessionRevision{0});
}

void PlayerPresenterTests::retryRestoresThePausedSessionAfterTheSavedSourceReturns() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterPreferencesWriter preferences_writer;
    PresenterSessionWriter session_writer;
    PresenterPreferencesClock clock;
    const Preferences preferences{};
    PreferencesAutosave preferences_autosave{
        preferences_writer, preferences, {.now = [&clock] { return clock.now(); }}};
    const auto persisted = persistedSessionSnapshot();
    SessionAutosave session_autosave{session_writer, persisted, {.now = [&clock] { return clock.now(); }}};
    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
        PlayerPresenterPreferences{.initial = preferences,
                                   .autosave = &preferences_autosave,
                                   .session = {.initial = persisted,
                                               .load_status = SessionLoadStatus::Loaded,
                                               .autosave = &session_autosave,
                                               .now = [&clock] { return clock.now(); }}});

    const auto occurrence = presenter.currentOccurrenceId();
    QCOMPARE(occurrence, QStringLiteral("local-42"));
    backend.publish({.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::SourceUnavailable});
    QTRY_VERIFY(presenter.canRetryCurrentOccurrence());
    QVERIFY(presenter.retryQueueOccurrence(occurrence));
    QCOMPARE(session_autosave.latestRevision(), SessionRevision{0});

    const PlaybackSnapshot failed_retry{
        .status = PlaybackStatus::Error, .generation = 2, .error = PlaybackErrorCode::AccessDenied};
    backend.publish(failed_retry);
    QTRY_VERIFY(presenter.canRetryCurrentOccurrence());
    QVERIFY(presenter.retryQueueOccurrence(occurrence));
    QCOMPARE(session_autosave.latestRevision(), SessionRevision{0});

    backend.publishEvent(PlaybackEventType::SnapshotChanged, failed_retry, false);
    QCoreApplication::processEvents();
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 60'000, .seekable = true, .generation = 3});
    QTRY_COMPARE(backend.pause_calls, 1);
    QTRY_COMPARE(backend.seek_positions, std::vector<std::int64_t>{persisted.position_ms});
    backend.publish({.status = PlaybackStatus::Paused,
                     .position_ms = persisted.position_ms,
                     .duration_ms = 60'000,
                     .seekable = true,
                     .generation = 3});
    QTRY_COMPARE(presenter.confirmedPositionMs(), qint64{persisted.position_ms});

    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QCOMPARE(presenter.currentOccurrenceId(), occurrence);
    QCOMPARE(presenter.playlistQueueSnapshot(), persisted.queue);
    QVERIFY(presenter.queueModelObject()
                ->data(presenter.queueModelObject()->index(1, 0), QueueListModel::AvailableRole)
                .toBool());
    QCOMPARE(backend.play_calls, 0);
    QCOMPARE(session_autosave.latestRevision(), SessionRevision{0});
}

void PlayerPresenterTests::acceptsAnApproximateConfirmedPositionWhenRestoring() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    metadata.start_result = MetadataCommandResult::Shutdown;
    PresenterPreferencesWriter preferences_writer;
    PresenterSessionWriter session_writer;
    PresenterPreferencesClock clock;
    Preferences preferences{};
    preferences.restore_last_session = true;
    PreferencesAutosave preferences_autosave{
        preferences_writer, preferences, {.now = [&clock] { return clock.now(); }}};
    const SessionSnapshot persisted = persistedSessionSnapshot();
    SessionAutosave session_autosave{session_writer, persisted, {.now = [&clock] { return clock.now(); }}};

    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
        PlayerPresenterPreferences{
            .initial = preferences,
            .load_status = PreferencesLoadStatus::Loaded,
            .autosave = &preferences_autosave,
            .session = {.initial = persisted,
                        .load_status = SessionLoadStatus::Loaded,
                        .autosave = &session_autosave,
                        .now = [&clock] { return clock.now(); }},
        });

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 60'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.seek_positions, std::vector<std::int64_t>{42'000});
    backend.publish({.status = PlaybackStatus::Paused,
                     .position_ms = 41'975,
                     .duration_ms = 60'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.confirmedPositionMs(), qint64{41'975});

    QCOMPARE(backend.play_calls, 0);
    QCOMPARE(session_autosave.latestRevision(), SessionRevision{1});
    QVERIFY(session_autosave.dirty());
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(session_writer.starts.size(), std::size_t{1});
    QCOMPARE(session_writer.starts.back().snapshot.position_ms, std::int64_t{41'975});
}

void PlayerPresenterTests::waitsForRestorableDurationBeforeApplyingThePersistedPosition() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    metadata.start_result = MetadataCommandResult::Shutdown;
    PresenterPreferencesWriter preferences_writer;
    PresenterSessionWriter session_writer;
    PresenterPreferencesClock clock;
    Preferences preferences{};
    preferences.restore_last_session = true;
    PreferencesAutosave preferences_autosave{
        preferences_writer, preferences, {.now = [&clock] { return clock.now(); }}};
    const SessionSnapshot persisted = persistedSessionSnapshot();
    SessionAutosave session_autosave{session_writer, persisted, {.now = [&clock] { return clock.now(); }}};

    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
        PlayerPresenterPreferences{
            .initial = preferences,
            .load_status = PreferencesLoadStatus::Loaded,
            .autosave = &preferences_autosave,
            .session = {.initial = persisted,
                        .load_status = SessionLoadStatus::Loaded,
                        .autosave = &session_autosave,
                        .now = [&clock] { return clock.now(); }},
        });

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 0, .seekable = false, .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Ready);
    QVERIFY(backend.seek_positions.empty());
    QCOMPARE(session_autosave.latestRevision(), SessionRevision{0});

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 60'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.seek_positions, std::vector<std::int64_t>{42'000});
    QCOMPARE(backend.play_calls, 0);
}

void PlayerPresenterTests::userSelectionCancelsPendingRestoreWithoutApplyingItsPosition() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    metadata.start_result = MetadataCommandResult::Shutdown;
    PresenterPreferencesWriter preferences_writer;
    PresenterSessionWriter session_writer;
    PresenterPreferencesClock clock;
    Preferences preferences{};
    preferences.restore_last_session = true;
    PreferencesAutosave preferences_autosave{
        preferences_writer, preferences, {.now = [&clock] { return clock.now(); }}};
    const SessionSnapshot persisted = persistedSessionSnapshot();
    SessionAutosave session_autosave{session_writer, persisted, {.now = [&clock] { return clock.now(); }}};

    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
        PlayerPresenterPreferences{
            .initial = preferences,
            .load_status = PreferencesLoadStatus::Loaded,
            .autosave = &preferences_autosave,
            .session = {.initial = persisted,
                        .load_status = SessionLoadStatus::Loaded,
                        .autosave = &session_autosave,
                        .now = [&clock] { return clock.now(); }},
        });

    QVERIFY(presenter.playQueueOccurrence(QStringLiteral("local-41")));
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("local-41"));
    QCOMPARE(backend.loaded_paths.size(), std::size_t{2});
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 60'000, .seekable = true, .generation = 2});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Ready);

    QVERIFY(backend.seek_positions.empty());
    QCOMPARE(session_autosave.latestRevision(), SessionRevision{1});
    clock.advance(std::chrono::milliseconds{500});
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(session_writer.starts.size(), std::size_t{1});
    QVERIFY(session_writer.starts.back().snapshot.queue.current_occurrence_id ==
            std::optional{LocalQueueOccurrenceId{41}});
    QCOMPARE(session_writer.starts.back().snapshot.position_ms, std::int64_t{0});
}

void PlayerPresenterTests::explicitPlayOfTheRestoredLoadingOccurrenceCancelsItsPendingPosition() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    metadata.start_result = MetadataCommandResult::Shutdown;
    PresenterPreferencesWriter preferences_writer;
    PresenterSessionWriter session_writer;
    PresenterPreferencesClock clock;
    Preferences preferences{};
    preferences.restore_last_session = true;
    PreferencesAutosave preferences_autosave{
        preferences_writer, preferences, {.now = [&clock] { return clock.now(); }}};
    SessionSnapshot persisted = persistedSessionSnapshot();
    persisted.queue.current_occurrence_id = LocalQueueOccurrenceId{41};
    SessionAutosave session_autosave{session_writer, persisted, {.now = [&clock] { return clock.now(); }}};

    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
        PlayerPresenterPreferences{
            .initial = preferences,
            .load_status = PreferencesLoadStatus::Loaded,
            .autosave = &preferences_autosave,
            .session = {.initial = persisted,
                        .load_status = SessionLoadStatus::Loaded,
                        .autosave = &session_autosave,
                        .now = [&clock] { return clock.now(); }},
        });

    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("local-41"));
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
    QVERIFY(presenter.playQueueOccurrence(QStringLiteral("local-41")));
    QCOMPARE(backend.loaded_paths.size(), std::size_t{1});

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 60'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    QVERIFY(backend.seek_positions.empty());
    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 100,
                     .duration_ms = 60'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QVERIFY(session_autosave.dirty());

    const PlaybackSnapshot final_snapshot{.status = PlaybackStatus::Paused,
                                          .position_ms = 60'000,
                                          .duration_ms = 60'000,
                                          .seekable = true,
                                          .generation = 1};
    backend.publish(final_snapshot);
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    backend.publishEvent(PlaybackEventType::Ended, final_snapshot);

    QTRY_COMPARE(presenter.currentQueueIndex(), 1);
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("local-42"));
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
    QCOMPARE(backend.loaded_paths.size(), std::size_t{2});
}

void PlayerPresenterTests::explicitTransportCancelsRestoreAfterReadyBeforeSeekConfirmation_data() {
    QTest::addColumn<QString>("action");
    QTest::newRow("play") << QStringLiteral("play");
    QTest::newRow("skip") << QStringLiteral("skip");
    QTest::newRow("seek") << QStringLiteral("seek");
}

void PlayerPresenterTests::explicitTransportCancelsRestoreAfterReadyBeforeSeekConfirmation() {
    QFETCH(QString, action);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    metadata.start_result = MetadataCommandResult::Shutdown;
    PresenterPreferencesWriter preferences_writer;
    PresenterSessionWriter session_writer;
    PresenterPreferencesClock clock;
    Preferences preferences{};
    preferences.restore_last_session = true;
    PreferencesAutosave preferences_autosave{
        preferences_writer, preferences, {.now = [&clock] { return clock.now(); }}};
    SessionSnapshot persisted = persistedSessionSnapshot();
    persisted.queue.current_occurrence_id = LocalQueueOccurrenceId{41};
    SessionAutosave session_autosave{session_writer, persisted, {.now = [&clock] { return clock.now(); }}};

    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
        PlayerPresenterPreferences{
            .initial = preferences,
            .load_status = PreferencesLoadStatus::Loaded,
            .autosave = &preferences_autosave,
            .session = {.initial = persisted,
                        .load_status = SessionLoadStatus::Loaded,
                        .autosave = &session_autosave,
                        .now = [&clock] { return clock.now(); }},
        });

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 60'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.seek_positions, std::vector<std::int64_t>{42'000});
    if (action == QStringLiteral("play")) {
        QVERIFY(presenter.togglePlayback());
    } else if (action == QStringLiteral("skip")) {
        QVERIFY(presenter.skipForward());
    } else {
        QVERIFY(presenter.beginSeekPreview(1'000));
        QVERIFY(presenter.commitSeekPreview());
    }
    QVERIFY(session_autosave.dirty());

    const PlaybackSnapshot final_snapshot{.status = PlaybackStatus::Paused,
                                          .position_ms = 60'000,
                                          .duration_ms = 60'000,
                                          .seekable = true,
                                          .generation = 1};
    backend.publish(final_snapshot);
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    backend.publishEvent(PlaybackEventType::Ended, final_snapshot);

    QTRY_COMPARE(presenter.currentQueueIndex(), 1);
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("local-42"));
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
}

void PlayerPresenterTests::restoredEndOfFileRemainsPausedWhenEndedArrives() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    metadata.start_result = MetadataCommandResult::Shutdown;
    PresenterPreferencesWriter preferences_writer;
    PresenterSessionWriter session_writer;
    PresenterPreferencesClock clock;
    Preferences preferences{};
    preferences.restore_last_session = true;
    PreferencesAutosave preferences_autosave{
        preferences_writer, preferences, {.now = [&clock] { return clock.now(); }}};
    SessionSnapshot persisted = persistedSessionSnapshot();
    persisted.queue.current_occurrence_id = LocalQueueOccurrenceId{41};
    SessionAutosave session_autosave{session_writer, persisted, {.now = [&clock] { return clock.now(); }}};

    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
        PlayerPresenterPreferences{
            .initial = preferences,
            .load_status = PreferencesLoadStatus::Loaded,
            .autosave = &preferences_autosave,
            .session = {.initial = persisted,
                        .load_status = SessionLoadStatus::Loaded,
                        .autosave = &session_autosave,
                        .now = [&clock] { return clock.now(); }},
        });

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 42'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.seek_positions, std::vector<std::int64_t>{42'000});
    const PlaybackSnapshot end_of_file{.status = PlaybackStatus::Paused,
                                       .position_ms = 42'000,
                                       .duration_ms = 42'000,
                                       .seekable = true,
                                       .generation = 1};
    backend.publish(end_of_file);
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QTRY_COMPARE(presenter.confirmedPositionMs(), qint64{42'000});
    backend.publishEvent(PlaybackEventType::Ended, end_of_file);
    QCoreApplication::processEvents();

    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("local-41"));
    QCOMPARE(presenter.currentQueueIndex(), 0);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QCOMPARE(backend.loaded_paths.size(), std::size_t{1});
    QCOMPARE(backend.play_calls, 0);
}

void PlayerPresenterTests::explicitPlayAfterRestoredEndOfFileAllowsANewEndedEventToAdvance() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    metadata.start_result = MetadataCommandResult::Shutdown;
    PresenterPreferencesWriter preferences_writer;
    PresenterSessionWriter session_writer;
    PresenterPreferencesClock clock;
    Preferences preferences{};
    preferences.restore_last_session = true;
    PreferencesAutosave preferences_autosave{
        preferences_writer, preferences, {.now = [&clock] { return clock.now(); }}};
    SessionSnapshot persisted = persistedSessionSnapshot();
    persisted.queue.current_occurrence_id = LocalQueueOccurrenceId{41};
    SessionAutosave session_autosave{session_writer, persisted, {.now = [&clock] { return clock.now(); }}};

    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
        PlayerPresenterPreferences{
            .initial = preferences,
            .load_status = PreferencesLoadStatus::Loaded,
            .autosave = &preferences_autosave,
            .session = {.initial = persisted,
                        .load_status = SessionLoadStatus::Loaded,
                        .autosave = &session_autosave,
                        .now = [&clock] { return clock.now(); }},
        });

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 42'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.seek_positions, std::vector<std::int64_t>{42'000});
    const PlaybackSnapshot end_of_file{.status = PlaybackStatus::Paused,
                                       .position_ms = 42'000,
                                       .duration_ms = 42'000,
                                       .seekable = true,
                                       .generation = 1};
    backend.publish(end_of_file);
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);

    QVERIFY(presenter.play());
    QCOMPARE(backend.play_calls, 1);
    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 42'000,
                     .duration_ms = 42'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    backend.publishEvent(PlaybackEventType::Ended, end_of_file);

    QTRY_COMPARE(presenter.currentQueueIndex(), 1);
    QCOMPARE(presenter.currentOccurrenceId(), QStringLiteral("local-42"));
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
    QCOMPARE(backend.loaded_paths.size(), std::size_t{2});
}

void PlayerPresenterTests::restoreDisabledIgnoresAProvidedPersistedSession() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterPreferencesWriter preferences_writer;
    PresenterSessionWriter session_writer;
    PresenterPreferencesClock clock;
    Preferences preferences{};
    preferences.restore_last_session = false;
    PreferencesAutosave preferences_autosave{
        preferences_writer, preferences, {.now = [&clock] { return clock.now(); }}};
    const SessionSnapshot persisted = persistedSessionSnapshot();
    SessionAutosave session_autosave{session_writer, persisted, {.now = [&clock] { return clock.now(); }}};

    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
        PlayerPresenterPreferences{
            .initial = preferences,
            .load_status = PreferencesLoadStatus::Loaded,
            .autosave = &preferences_autosave,
            .session = {.initial = persisted,
                        .load_status = SessionLoadStatus::Loaded,
                        .autosave = &session_autosave,
                        .now = [&clock] { return clock.now(); }},
        });

    QCOMPARE(presenter.queueModelObject()->rowCount(), 0);
    QVERIFY(!presenter.hasCurrentOccurrence());
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Unloaded);
    QVERIFY(backend.loaded_paths.empty());
    QCOMPARE(backend.play_calls, 0);
    QCOMPARE(session_autosave.latestRevision(), SessionRevision{0});
    QVERIFY(!session_autosave.dirty());
    QVERIFY(session_writer.starts.empty());
}

void PlayerPresenterTests::reenablingRestoreForcesTheCurrentSessionToReplaceStaleDiskState() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterPreferencesWriter preferences_writer;
    PresenterSessionWriter session_writer;
    PresenterPreferencesClock clock;
    Preferences preferences{};
    preferences.restore_last_session = false;
    PreferencesAutosave preferences_autosave{
        preferences_writer, preferences, {.now = [&clock] { return clock.now(); }}};
    SessionAutosave session_autosave{session_writer, SessionSnapshot{}, {.now = [&clock] { return clock.now(); }}};

    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
        PlayerPresenterPreferences{
            .initial = preferences,
            .load_status = PreferencesLoadStatus::Loaded,
            .autosave = &preferences_autosave,
            .session = {.initial = {},
                        .load_status = SessionLoadStatus::Missing,
                        .autosave = &session_autosave,
                        .now = [&clock] { return clock.now(); }},
        });

    QVERIFY(presenter.setRestoreLastSession(true));
    QVERIFY(session_autosave.dirty());
    QCOMPARE(session_autosave.latestRevision(), SessionRevision{1});
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(session_writer.starts.size(), std::size_t{1});
    QVERIFY(session_writer.starts.back().snapshot.queue.items.empty());
    QCOMPARE(session_writer.starts.back().snapshot.position_ms, std::int64_t{0});
}

void PlayerPresenterTests::exitWaitsForBothPreferenceAndSessionPersistence() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterPreferencesWriter preferences_writer;
    PresenterSessionWriter session_writer;
    PresenterPreferencesClock clock;
    Preferences preferences{};
    PreferencesAutosave preferences_autosave{
        preferences_writer, preferences, {.now = [&clock] { return clock.now(); }}};
    SessionAutosave session_autosave{session_writer, SessionSnapshot{}, {.now = [&clock] { return clock.now(); }}};
    const auto source = localTestPath(u8"Saída 日本語.flac");
    QStringList shutdown_order;
    backend.on_shutdown = [&] { shutdown_order.append(QStringLiteral("backend-shutdown")); };
    platform.on_exit = [&] { shutdown_order.append(QStringLiteral("platform-exit")); };

    PlayerPresenter presenter(
        platform, backend, metadata, [source] { return std::vector{source}; },
        PlayerPresenterPreferences{
            .initial = preferences,
            .load_status = PreferencesLoadStatus::Missing,
            .autosave = &preferences_autosave,
            .session = {.initial = {},
                        .load_status = SessionLoadStatus::Missing,
                        .autosave = &session_autosave,
                        .now = [&clock] { return clock.now(); }},
        });

    QVERIFY(presenter.openAudio());
    QVERIFY(presenter.setThemePreference(PlayerPresenter::ThemePreference::DarkTheme));
    QVERIFY(session_autosave.dirty());
    QVERIFY(preferences_autosave.dirty());
    QVERIFY(presenter.requestExit());
    QVERIFY(presenter.exiting());
    QVERIFY(!presenter.exitCommitted());
    QCOMPARE(platform.exit_calls, 0);
    QCOMPARE(backend.shutdown_calls, 0);
    QVERIFY(shutdown_order.empty());
    QCOMPARE(preferences_writer.starts.size(), std::size_t{1});
    QCOMPARE(session_writer.starts.size(), std::size_t{1});
    QCOMPARE(session_writer.starts.back().snapshot.queue.items.size(), std::size_t{1});
    QCOMPARE(session_writer.starts.back().snapshot.queue.current_occurrence_id,
             std::optional{session_writer.starts.back().snapshot.queue.items.front().occurrence_id});

    preferences_writer.complete(PreferencesSaveStatus::Saved);
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(platform.exit_calls, 0);
    QCOMPARE(backend.shutdown_calls, 0);
    QVERIFY(shutdown_order.empty());
    QVERIFY(!presenter.exitCommitted());

    session_writer.complete(SessionSaveStatus::Saved);
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(platform.exit_calls, 1);
    QCOMPARE(backend.shutdown_calls, 1);
    QCOMPARE(shutdown_order, QStringList({QStringLiteral("backend-shutdown"), QStringLiteral("platform-exit")}));
    QVERIFY(presenter.exitCommitted());
    QVERIFY(!preferences_autosave.dirty());
    QVERIFY(!session_autosave.dirty());
    QVERIFY(!presenter.requestExit());
    QCOMPARE(backend.shutdown_calls, 1);
    QCOMPARE(platform.exit_calls, 1);
}

void PlayerPresenterTests::pendingSessionExitFreezesLocalPlaybackAndMetadataUntilCancellation() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterPreferencesWriter preferences_writer;
    PresenterSessionWriter session_writer;
    PresenterPreferencesClock clock;
    const Preferences preferences{};
    PreferencesAutosave preferences_autosave{
        preferences_writer, preferences, {.now = [&clock] { return clock.now(); }}};
    SessionAutosave session_autosave{session_writer, SessionSnapshot{}, {.now = [&clock] { return clock.now(); }}};
    const std::vector<std::filesystem::path> sources{localTestPath(u8"Antes da saída.wav"),
                                                     localTestPath(u8"Depois da saída.wav")};

    PlayerPresenter presenter(
        platform, backend, metadata, [sources] { return sources; },
        PlayerPresenterPreferences{
            .initial = preferences,
            .load_status = PreferencesLoadStatus::Loaded,
            .autosave = &preferences_autosave,
            .session = {.initial = {},
                        .load_status = SessionLoadStatus::Missing,
                        .autosave = &session_autosave,
                        .now = [&clock] { return clock.now(); }},
        });

    QVERIFY(presenter.openAudio());
    const QString current_occurrence = presenter.currentOccurrenceId();
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 2'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 1'900,
                     .duration_ms = 2'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);

    QVERIFY(presenter.requestExit());
    QVERIFY(presenter.exiting());
    QCOMPARE(session_writer.starts.size(), std::size_t{1});
    const std::size_t metadata_polls_before = metadata.polled_generations.size();
    PlayerPresenterTestAccess::pollMetadata(presenter);
    QCOMPARE(metadata.polled_generations.size(), metadata_polls_before);

    const PlaybackSnapshot final_snapshot{.status = PlaybackStatus::Paused,
                                          .position_ms = 2'000,
                                          .duration_ms = 2'000,
                                          .seekable = true,
                                          .generation = 1};
    backend.publish(final_snapshot);
    backend.publishEvent(PlaybackEventType::Ended, final_snapshot);
    QCoreApplication::processEvents();
    QCOMPARE(presenter.currentOccurrenceId(), current_occurrence);
    QCOMPARE(presenter.currentQueueIndex(), 0);
    QCOMPARE(backend.loaded_paths.size(), std::size_t{1});

    session_writer.complete(SessionSaveStatus::Failed);
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QVERIFY(!presenter.exiting());
    QCOMPARE(platform.exit_calls, 0);
    QCOMPARE(backend.shutdown_calls, 0);
    QTRY_COMPARE(presenter.currentQueueIndex(), 1);
    QCOMPARE(backend.loaded_paths.size(), std::size_t{2});
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
    PlayerPresenterTestAccess::pollMetadata(presenter);
    QCOMPARE(metadata.polled_generations.size(), metadata_polls_before + 1);
}

void PlayerPresenterTests::coalescesStructuralSessionMutationsIntoTheLatestSnapshot() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    metadata.start_result = MetadataCommandResult::Shutdown;
    PresenterPreferencesWriter preferences_writer;
    PresenterSessionWriter session_writer;
    PresenterPreferencesClock clock;
    const Preferences preferences{};
    PreferencesAutosave preferences_autosave{
        preferences_writer, preferences, {.now = [&clock] { return clock.now(); }}};
    SessionAutosave session_autosave{session_writer, SessionSnapshot{}, {.now = [&clock] { return clock.now(); }}};
    const auto first = localTestPath(u8"Primeira 日本語.mp3");
    const auto second = localTestPath(u8"Segunda.flac");
    const auto third = localTestPath(u8"Terceira.ogg");
    const std::vector<std::vector<std::filesystem::path>> selections{{first, second}, {third}};
    std::size_t selection_index = 0;

    PlayerPresenter presenter(
        platform, backend, metadata, [&] { return selections.at(selection_index++); },
        PlayerPresenterPreferences{
            .initial = preferences,
            .load_status = PreferencesLoadStatus::Missing,
            .autosave = &preferences_autosave,
            .session = {.initial = {},
                        .load_status = SessionLoadStatus::Missing,
                        .autosave = &session_autosave,
                        .now = [&clock] { return clock.now(); }},
        });

    QVERIFY(presenter.openAudio());
    clock.advance(std::chrono::milliseconds{100});
    QVERIFY(presenter.addAudio());
    clock.advance(std::chrono::milliseconds{100});
    QVERIFY(presenter.moveQueueOccurrence(QStringLiteral("local-3"), 0));
    clock.advance(std::chrono::milliseconds{100});
    QVERIFY(presenter.removeQueueOccurrence(QStringLiteral("local-2")));

    QCOMPARE(session_autosave.latestRevision(), SessionRevision{4});
    QVERIFY(session_autosave.dirty());
    QVERIFY(session_writer.starts.empty());
    clock.advance(std::chrono::milliseconds{499});
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QVERIFY(session_writer.starts.empty());
    clock.advance(std::chrono::milliseconds{1});
    PlayerPresenterTestAccess::pollPersistence(presenter);

    QCOMPARE(session_writer.starts.size(), std::size_t{1});
    const SessionSnapshot& coalesced = session_writer.starts.back().snapshot;
    QCOMPARE(coalesced.queue.items.size(), std::size_t{2});
    QVERIFY(coalesced.queue.items[0].occurrence_id == LocalQueueOccurrenceId{3});
    QVERIFY(coalesced.queue.items[0].source_path == third);
    QVERIFY(coalesced.queue.items[1].occurrence_id == LocalQueueOccurrenceId{1});
    QVERIFY(coalesced.queue.items[1].source_path == first);
    QVERIFY(coalesced.queue.current_occurrence_id == std::optional{LocalQueueOccurrenceId{1}});
    QCOMPARE(coalesced.position_ms, std::int64_t{0});

    session_writer.complete(SessionSaveStatus::Saved);
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QVERIFY(!session_autosave.dirty());

    QVERIFY(presenter.clearQueue());
    QCOMPARE(session_autosave.latestRevision(), SessionRevision{5});
    clock.advance(std::chrono::milliseconds{500});
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(session_writer.starts.size(), std::size_t{2});
    QVERIFY(session_writer.starts.back().snapshot.queue.items.empty());
    QVERIFY(!session_writer.starts.back().snapshot.queue.current_occurrence_id.has_value());
    QCOMPARE(session_writer.starts.back().snapshot.position_ms, std::int64_t{0});
}

void PlayerPresenterTests::persistsOnlyConfirmedPositionsOnPeriodicAndUrgentCheckpoints() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    metadata.start_result = MetadataCommandResult::Shutdown;
    PresenterPreferencesWriter preferences_writer;
    PresenterSessionWriter session_writer;
    PresenterPreferencesClock clock;
    const Preferences preferences{};
    PreferencesAutosave preferences_autosave{
        preferences_writer, preferences, {.now = [&clock] { return clock.now(); }}};
    SessionAutosave session_autosave{session_writer, SessionSnapshot{}, {.now = [&clock] { return clock.now(); }}};
    const auto source = localTestPath(u8"Checkpoint 日本語.wav");

    PlayerPresenter presenter(
        platform, backend, metadata, [source] { return std::vector{source}; },
        PlayerPresenterPreferences{
            .initial = preferences,
            .load_status = PreferencesLoadStatus::Missing,
            .autosave = &preferences_autosave,
            .session = {.initial = {},
                        .load_status = SessionLoadStatus::Missing,
                        .autosave = &session_autosave,
                        .now = [&clock] { return clock.now(); }},
        });

    QVERIFY(presenter.openAudio());
    clock.advance(std::chrono::milliseconds{500});
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(session_writer.starts.size(), std::size_t{1});
    session_writer.complete(SessionSaveStatus::Saved);
    PlayerPresenterTestAccess::pollPersistence(presenter);

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 5'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 100,
                     .duration_ms = 5'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.confirmedPositionMs(), qint64{100});

    clock.advance(std::chrono::milliseconds{9'000});
    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 200,
                     .duration_ms = 5'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.confirmedPositionMs(), qint64{200});
    QCOMPARE(session_autosave.latestRevision(), SessionRevision{3});
    QVERIFY(presenter.beginSeekPreview(800));
    QVERIFY(presenter.updateSeekPreview(900));
    QCOMPARE(presenter.positionMs(), qint64{900});
    QCOMPARE(presenter.confirmedPositionMs(), qint64{200});

    backend.publishEvent(PlaybackEventType::SnapshotChanged,
                         {.status = PlaybackStatus::Playing,
                          .position_ms = 4'000,
                          .duration_ms = 5'000,
                          .seekable = true,
                          .generation = 999},
                         false);
    QCoreApplication::processEvents();
    QCOMPARE(presenter.confirmedPositionMs(), qint64{200});
    QCOMPARE(session_autosave.latestRevision(), SessionRevision{3});

    clock.advance(std::chrono::milliseconds{999});
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(session_writer.starts.size(), std::size_t{1});
    clock.advance(std::chrono::milliseconds{1});
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(session_writer.starts.size(), std::size_t{2});
    QCOMPARE(session_writer.starts.back().snapshot.position_ms, std::int64_t{200});

    session_writer.complete(SessionSaveStatus::Saved);
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QVERIFY(presenter.commitSeekPreview());
    QCOMPARE(backend.seek_positions, std::vector<std::int64_t>{900});
    QVERIFY(presenter.seekPending());

    backend.publishEvent(PlaybackEventType::SnapshotChanged,
                         {.status = PlaybackStatus::Playing,
                          .position_ms = 900,
                          .duration_ms = 5'000,
                          .seekable = true,
                          .generation = 999},
                         false);
    QCoreApplication::processEvents();
    QVERIFY(presenter.seekPending());
    QVERIFY(!session_autosave.dirty());
    QCOMPARE(session_writer.starts.size(), std::size_t{2});

    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 900,
                     .duration_ms = 5'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_VERIFY(!presenter.seekPending());
    QTRY_COMPARE(presenter.confirmedPositionMs(), qint64{900});
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(session_writer.starts.size(), std::size_t{3});
    QCOMPARE(session_writer.starts.back().snapshot.position_ms, std::int64_t{900});

    session_writer.complete(SessionSaveStatus::Saved);
    PlayerPresenterTestAccess::pollPersistence(presenter);
    backend.publish({.status = PlaybackStatus::Paused,
                     .position_ms = 1'000,
                     .duration_ms = 5'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QTRY_COMPARE(presenter.confirmedPositionMs(), qint64{1'000});
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(session_writer.starts.size(), std::size_t{4});
    QCOMPARE(session_writer.starts.back().snapshot.position_ms, std::int64_t{1'000});
}

void PlayerPresenterTests::systemSuspendStartsAnImmediateConfirmedCheckpointWithoutChangingPlayback() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    metadata.start_result = MetadataCommandResult::Shutdown;
    PresenterPreferencesWriter preferences_writer;
    PresenterSessionWriter session_writer;
    PresenterPreferencesClock clock;
    const Preferences preferences{};
    PreferencesAutosave preferences_autosave{
        preferences_writer, preferences, {.now = [&clock] { return clock.now(); }}};
    SessionAutosave session_autosave{session_writer, SessionSnapshot{}, {.now = [&clock] { return clock.now(); }}};
    const auto source = localTestPath(u8"Suspend checkpoint.wav");

    PlayerPresenter presenter(
        platform, backend, metadata, [source] { return std::vector{source}; },
        PlayerPresenterPreferences{
            .initial = preferences,
            .load_status = PreferencesLoadStatus::Missing,
            .autosave = &preferences_autosave,
            .session = {.initial = {},
                        .load_status = SessionLoadStatus::Missing,
                        .autosave = &session_autosave,
                        .now = [&clock] { return clock.now(); }},
        });

    QVERIFY(presenter.openAudio());
    clock.advance(std::chrono::milliseconds{500});
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(session_writer.starts.size(), std::size_t{1});
    session_writer.complete(SessionSaveStatus::Saved);
    PlayerPresenterTestAccess::pollPersistence(presenter);

    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 5'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 320,
                     .duration_ms = 5'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.confirmedPositionMs(), qint64{320});
    const SessionRevision revision_before = session_autosave.latestRevision();
    const int play_calls_before = backend.play_calls;
    const int pause_calls_before = backend.pause_calls;

    QVERIFY(presenter.handleSystemPowerEvent(SystemPowerEvent::Suspending));
    QCOMPARE(session_writer.starts.size(), std::size_t{2});
    QCOMPARE(session_writer.starts.back().snapshot.position_ms, std::int64_t{320});
    QCOMPARE(session_autosave.latestRevision(), revision_before);
    QVERIFY(session_writer.flushes.empty());
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QCOMPARE(backend.play_calls, play_calls_before);
    QCOMPARE(backend.pause_calls, pause_calls_before);

    QVERIFY(!presenter.handleSystemPowerEvent(SystemPowerEvent::Suspending));
    QCOMPARE(session_writer.starts.size(), std::size_t{2});
    QCOMPARE(session_autosave.latestRevision(), revision_before);
    QVERIFY(session_writer.flushes.empty());
}

void PlayerPresenterTests::systemSuspendQuiescesApplicationPollingUntilResume() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QTimer* const timer = presenter.findChild<QTimer*>(QString{}, Qt::FindDirectChildrenOnly);
    QVERIFY(timer != nullptr);
    QVERIFY(!timer->isActive());

    QVERIFY(presenter.loadFrozenQueue());
    QVERIFY(timer->isActive());
    QVERIFY(presenter.handleSystemPowerEvent(SystemPowerEvent::Suspending));
    QVERIFY(!timer->isActive());
    QVERIFY(presenter.handleSystemPowerEvent(SystemPowerEvent::Resumed));
    QVERIFY(timer->isActive());
}

void PlayerPresenterTests::resumeDuringReversibleExitReactivatesPollingWhenTheExitFails() {
    AvailableTrayPlatform platform;
    PresenterPreferencesWriter writer;
    PresenterPreferencesClock clock;
    const Preferences initial{};
    PreferencesAutosave autosave{writer, initial, {.now = [&clock] { return clock.now(); }}};
    PlayerPresenter presenter(frozenBundle(), platform,
                              PlayerPresenterPreferences{.initial = initial,
                                                         .load_status = PreferencesLoadStatus::Missing,
                                                         .autosave = &autosave});
    QTimer* const timer = presenter.findChild<QTimer*>(QString{}, Qt::FindDirectChildrenOnly);
    QVERIFY(timer != nullptr);
    QVERIFY(presenter.loadFrozenQueue());
    QVERIFY(presenter.setThemePreference(PlayerPresenter::ThemePreference::DarkTheme));
    QVERIFY(presenter.handleSystemPowerEvent(SystemPowerEvent::Suspending));
    QVERIFY(timer->isActive());

    const qint64 suspended_position = presenter.positionMs();
    QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
    QCOMPARE(presenter.positionMs(), suspended_position);

    QVERIFY(presenter.requestExit());
    QVERIFY(presenter.exiting());
    QVERIFY(!presenter.exitCommitted());
    QCOMPARE(writer.starts.size(), std::size_t{1});
    QVERIFY(presenter.handleSystemPowerEvent(SystemPowerEvent::Resumed));

    writer.complete(PreferencesSaveStatus::Failed);
    PlayerPresenterTestAccess::pollPreferencePersistence(presenter);
    QVERIFY(!presenter.exiting());
    QVERIFY(!presenter.exitCommitted());
    QVERIFY(timer->isActive());
    QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
    QVERIFY(presenter.positionMs() > suspended_position);
}

void PlayerPresenterTests::systemResumeReconcilesUnreportedBackendStateAndDeduplicatesTransitions() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    metadata.start_result = MetadataCommandResult::Shutdown;
    const auto source = localTestPath(u8"Resume reconciliation.mp3");
    PlayerPresenter presenter(platform, backend, metadata, [source] { return std::vector{source}; });

    QVERIFY(presenter.openAudio());
    backend.publish({.status = PlaybackStatus::Ready, .duration_ms = 5'000, .seekable = true, .generation = 1});
    QTRY_COMPARE(backend.play_calls, 1);
    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 200,
                     .duration_ms = 5'000,
                     .seekable = true,
                     .generation = 1});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QSignalSpy playback_changed(&presenter, &PlayerPresenter::playbackChanged);
    const int play_calls_before = backend.play_calls;
    const int pause_calls_before = backend.pause_calls;

    backend.snapshot_ = {
        .status = PlaybackStatus::Paused, .position_ms = 640, .duration_ms = 5'000, .seekable = true, .generation = 1};
    QVERIFY(presenter.handleSystemPowerEvent(SystemPowerEvent::Resumed));
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QCOMPARE(presenter.confirmedPositionMs(), qint64{640});
    QCOMPARE(playback_changed.size(), 1);
    QCOMPARE(backend.play_calls, play_calls_before);
    QCOMPARE(backend.pause_calls, pause_calls_before);

    QVERIFY(!presenter.handleSystemPowerEvent(SystemPowerEvent::Resumed));
    QCOMPARE(playback_changed.size(), 1);

    QVERIFY(presenter.handleSystemPowerEvent(SystemPowerEvent::Suspending));
    backend.snapshot_ = {.status = PlaybackStatus::Playing,
                         .position_ms = 2'000,
                         .duration_ms = 5'000,
                         .seekable = true,
                         .generation = 999};
    QVERIFY(presenter.handleSystemPowerEvent(SystemPowerEvent::Resumed));
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QCOMPARE(presenter.confirmedPositionMs(), qint64{640});
    QCOMPARE(playback_changed.size(), 1);

    QVERIFY(presenter.handleSystemPowerEvent(SystemPowerEvent::Suspending));
    backend.snapshot_ = {.status = PlaybackStatus::Error, .generation = 1, .error = PlaybackErrorCode::BackendFailure};
    QVERIFY(presenter.handleSystemPowerEvent(SystemPowerEvent::Resumed));
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Error);
    QVERIFY(presenter.hasError());
    QCOMPARE(presenter.errorActionId(), QStringLiteral("open-audio"));
    QCOMPARE(backend.play_calls, play_calls_before);
    QCOMPARE(backend.pause_calls, pause_calls_before);

    QVERIFY(presenter.requestExit());
    QVERIFY(presenter.exitCommitted());
    QVERIFY(!presenter.handleSystemPowerEvent(SystemPowerEvent::Suspending));
    QVERIFY(!presenter.handleSystemPowerEvent(SystemPowerEvent::Resumed));
}

void PlayerPresenterTests::sessionSaveFailureUsesTheExistingWarningAndRetryAction() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    metadata.start_result = MetadataCommandResult::Shutdown;
    PresenterPreferencesWriter preferences_writer;
    PresenterSessionWriter session_writer;
    PresenterPreferencesClock clock;
    const Preferences preferences{};
    PreferencesAutosave preferences_autosave{
        preferences_writer, preferences, {.now = [&clock] { return clock.now(); }}};
    SessionAutosave session_autosave{session_writer, SessionSnapshot{}, {.now = [&clock] { return clock.now(); }}};
    const auto source = localTestPath(u8"Falha de sessão.mp3");

    PlayerPresenter presenter(
        platform, backend, metadata, [source] { return std::vector{source}; },
        PlayerPresenterPreferences{
            .initial = preferences,
            .load_status = PreferencesLoadStatus::Missing,
            .autosave = &preferences_autosave,
            .session = {.initial = {},
                        .load_status = SessionLoadStatus::Missing,
                        .autosave = &session_autosave,
                        .now = [&clock] { return clock.now(); }},
        });

    QVERIFY(presenter.openAudio());
    clock.advance(std::chrono::milliseconds{500});
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(session_writer.starts.size(), std::size_t{1});
    session_writer.complete(SessionSaveStatus::Failed);
    PlayerPresenterTestAccess::pollPersistence(presenter);

    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::SaveFailed);
    QCOMPARE(presenter.persistenceWarningScope(), PlayerPresenter::PersistenceWarningScope::SessionPersistence);
    QVERIFY(presenter.retryPreferenceSave());
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(session_writer.starts.size(), std::size_t{2});
}

void PlayerPresenterTests::sessionFutureBlockUsesTheExistingWarningAndReplacementAction() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    metadata.start_result = MetadataCommandResult::Shutdown;
    PresenterPreferencesWriter preferences_writer;
    PresenterSessionWriter session_writer;
    PresenterPreferencesClock clock;
    const Preferences preferences{};
    PreferencesAutosave preferences_autosave{
        preferences_writer, preferences, {.now = [&clock] { return clock.now(); }}};
    SessionAutosave session_autosave{session_writer, SessionSnapshot{}, {.now = [&clock] { return clock.now(); }}};
    const auto source = localTestPath(u8"Sessão futura.flac");

    PlayerPresenter presenter(
        platform, backend, metadata, [source] { return std::vector{source}; },
        PlayerPresenterPreferences{
            .initial = preferences,
            .load_status = PreferencesLoadStatus::Missing,
            .autosave = &preferences_autosave,
            .session = {.initial = {},
                        .load_status = SessionLoadStatus::Missing,
                        .autosave = &session_autosave,
                        .now = [&clock] { return clock.now(); }},
        });

    QVERIFY(presenter.openAudio());
    clock.advance(std::chrono::milliseconds{500});
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(session_writer.starts.size(), std::size_t{1});
    session_writer.complete(SessionSaveStatus::BlockedByFutureVersion);
    PlayerPresenterTestAccess::pollPersistence(presenter);

    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::FutureVersion);
    QCOMPARE(presenter.persistenceWarningScope(), PlayerPresenter::PersistenceWarningScope::SessionPersistence);
    QVERIFY(presenter.confirmPreferenceReplacement(presenter.persistenceWarningRevision()));
    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::NoPreferenceWarning);
    QCOMPARE(presenter.persistenceWarningScope(), PlayerPresenter::PersistenceWarningScope::NoPersistenceWarningScope);
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::NoPreferenceWarning);
    QCOMPARE(session_writer.starts.size(), std::size_t{2});
    QVERIFY(session_writer.starts.back().replace_future_version);
}

void PlayerPresenterTests::initialSessionFutureVersionUsesTheExistingReplacementAction() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterPreferencesWriter preferences_writer;
    PresenterSessionWriter session_writer;
    PresenterPreferencesClock clock;
    const Preferences preferences{};
    PreferencesAutosave preferences_autosave{
        preferences_writer, preferences, {.now = [&clock] { return clock.now(); }}};
    SessionAutosave session_autosave{session_writer, SessionSnapshot{}, {.now = [&clock] { return clock.now(); }}};

    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
        PlayerPresenterPreferences{
            .initial = preferences,
            .load_status = PreferencesLoadStatus::Loaded,
            .autosave = &preferences_autosave,
            .session = {.initial = {},
                        .load_status = SessionLoadStatus::FutureVersion,
                        .autosave = &session_autosave,
                        .now = [&clock] { return clock.now(); }},
        });

    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::FutureVersion);
    QCOMPARE(presenter.persistenceWarningScope(), PlayerPresenter::PersistenceWarningScope::SessionPersistence);
    QVERIFY(presenter.confirmPreferenceReplacement(presenter.persistenceWarningRevision()));
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(session_writer.starts.size(), std::size_t{1});
    QVERIFY(session_writer.starts.back().replace_future_version);
    QVERIFY(preferences_writer.starts.empty());
}

void PlayerPresenterTests::replacementConsentRejectsScopeExpansion_data() {
    QTest::addColumn<bool>("preferencesFirst");
    QTest::newRow("preferences-to-combined") << true;
    QTest::newRow("session-to-combined") << false;
}

void PlayerPresenterTests::replacementConsentRejectsScopeExpansion() {
    QFETCH(bool, preferencesFirst);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterPreferencesWriter preferences_writer;
    PresenterSessionWriter session_writer;
    PresenterPreferencesClock clock;
    const Preferences preferences{};
    PreferencesAutosave preferences_autosave{
        preferences_writer, preferences, {.now = [&clock] { return clock.now(); }}};
    SessionAutosave session_autosave{session_writer, SessionSnapshot{}, {.now = [&clock] { return clock.now(); }}};
    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
        PlayerPresenterPreferences{
            .initial = preferences,
            .load_status = preferencesFirst ? PreferencesLoadStatus::FutureVersion : PreferencesLoadStatus::Loaded,
            .autosave = &preferences_autosave,
            .session = {.initial = {},
                        .load_status = preferencesFirst ? SessionLoadStatus::Missing : SessionLoadStatus::FutureVersion,
                        .autosave = &session_autosave,
                        .now = [&clock] { return clock.now(); }},
        });

    const QString displayed_revision = presenter.persistenceWarningRevision();
    QVERIFY(!displayed_revision.isEmpty());
    QCOMPARE(presenter.persistenceWarningScope(), preferencesFirst
                                                      ? PlayerPresenter::PersistenceWarningScope::PreferencePersistence
                                                      : PlayerPresenter::PersistenceWarningScope::SessionPersistence);
    // A completion from the other store arrives while confirmation is open.
    if (preferencesFirst) {
        session_autosave.scheduleRepair();
        PlayerPresenterTestAccess::pollPersistence(presenter);
        session_writer.complete(SessionSaveStatus::BlockedByFutureVersion);
    } else {
        QVERIFY(presenter.setThemePreference(PlayerPresenter::ThemePreference::DarkTheme));
        clock.advance(std::chrono::milliseconds{500});
        PlayerPresenterTestAccess::pollPersistence(presenter);
        preferences_writer.complete(PreferencesSaveStatus::BlockedByFutureVersion);
    }
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(presenter.persistenceWarningScope(), PlayerPresenter::PersistenceWarningScope::CombinedPersistence);
    const auto preference_revision = preferences_autosave.latestRevision();
    const auto session_revision = session_autosave.latestRevision();
    const auto preference_writes = preferences_writer.starts.size();
    const auto session_writes = session_writer.starts.size();
    QVERIFY(!presenter.confirmPreferenceReplacement(displayed_revision));
    QVERIFY(presenter.persistenceWarningRevision() != displayed_revision);
    QCOMPARE(preferences_autosave.latestRevision(), preference_revision);
    QCOMPARE(session_autosave.latestRevision(), session_revision);
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(preferences_writer.starts.size(), preference_writes);
    QCOMPARE(session_writer.starts.size(), session_writes);
    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::FutureVersion);
    QVERIFY(presenter.confirmPreferenceReplacement(presenter.persistenceWarningRevision()));
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(preferences_writer.starts.size(), preference_writes + 1);
    QCOMPARE(session_writer.starts.size(), session_writes + 1);
    QVERIFY(preferences_writer.starts.back().replace_future_version);
    QVERIFY(session_writer.starts.back().replace_future_version);
}

void PlayerPresenterTests::replacementConsentRejectsMissingAndUnknownRevision_data() {
    QTest::addColumn<QString>("revision");
    QTest::newRow("empty") << QString{};
    QTest::newRow("unknown") << QStringLiteral("not-the-displayed-revision");
}

void PlayerPresenterTests::replacementConsentRejectsMissingAndUnknownRevision() {
    QFETCH(QString, revision);
    AvailableTrayPlatform platform;
    PresenterPreferencesWriter writer;
    PreferencesAutosave autosave{writer, Preferences{}};
    PlayerPresenter presenter(frozenBundle(), platform,
                              PlayerPresenterPreferences{.initial = {},
                                                         .load_status = PreferencesLoadStatus::FutureVersion,
                                                         .autosave = &autosave});
    QVERIFY(!presenter.confirmPreferenceReplacement(revision));
    QVERIFY(!autosave.dirty());
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QVERIFY(writer.starts.empty());
    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::FutureVersion);
    const QMetaObject* const meta = presenter.metaObject();
    QCOMPARE(meta->indexOfMethod("confirmPreferenceReplacement()"), -1);
    QVERIFY(meta->indexOfMethod("confirmPreferenceReplacement(QString)") >= 0);
    const int property_index = meta->indexOfProperty("persistenceWarningRevision");
    QVERIFY(property_index >= 0);
    const auto property = meta->property(property_index);
    QVERIFY(!property.isWritable());
    QCOMPARE(property.notifySignal().methodSignature(), QByteArray("preferencePersistenceChanged()"));
}

void PlayerPresenterTests::replacementConsentCannotBeReusedAfterWarningReturns() {
    AvailableTrayPlatform platform;
    PresenterPreferencesWriter writer;
    PreferencesAutosave autosave{writer, Preferences{}};
    PlayerPresenter presenter(frozenBundle(), platform,
                              PlayerPresenterPreferences{.initial = {},
                                                         .load_status = PreferencesLoadStatus::FutureVersion,
                                                         .autosave = &autosave});
    const QString original_revision = presenter.persistenceWarningRevision();
    QVERIFY(presenter.confirmPreferenceReplacement(original_revision));
    QVERIFY(!presenter.confirmPreferenceReplacement(original_revision));
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(writer.starts.size(), std::size_t{1});
    // The same public warning/scope can recur after an intervening state.
    writer.complete(PreferencesSaveStatus::BlockedByFutureVersion);
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::FutureVersion);
    QCOMPARE(presenter.persistenceWarningScope(), PlayerPresenter::PersistenceWarningScope::PreferencePersistence);
    QVERIFY(!presenter.confirmPreferenceReplacement(original_revision));
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(writer.starts.size(), std::size_t{1});
    QVERIFY(presenter.confirmPreferenceReplacement(presenter.persistenceWarningRevision()));
}

void PlayerPresenterTests::replacementConsentRejectsReentrantActivation_data() {
    QTest::addColumn<bool>("readCurrentRevision");
    QTest::newRow("captured-revision") << false;
    QTest::newRow("current-revision") << true;
}

void PlayerPresenterTests::replacementConsentRejectsReentrantActivation() {
    QFETCH(bool, readCurrentRevision);
    AvailableTrayPlatform platform;
    PresenterPreferencesWriter writer;
    PresenterPreferencesClock clock;
    std::function<void()> on_now;
    PreferencesAutosave autosave{writer, Preferences{}, {.now = [&] {
                                     if (on_now) {
                                         auto callback = std::exchange(on_now, {});
                                         callback();
                                     }
                                     return clock.now();
                                 }}};
    PlayerPresenter presenter(frozenBundle(), platform,
                              PlayerPresenterPreferences{.initial = {},
                                                         .load_status = PreferencesLoadStatus::FutureVersion,
                                                         .autosave = &autosave});
    const QString revision = presenter.persistenceWarningRevision();
    bool nested_accepted = true;
    on_now = [&] {
        nested_accepted = presenter.confirmPreferenceReplacement(
            readCurrentRevision ? presenter.persistenceWarningRevision() : revision);
    };
    QVERIFY(presenter.confirmPreferenceReplacement(revision));
    QVERIFY(!nested_accepted);
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(writer.starts.size(), std::size_t{1});
    QVERIFY(writer.starts.front().replace_future_version);
}

void PlayerPresenterTests::replacementConsentDoesNotExpandDuringScheduling() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterPreferencesWriter preferences_writer;
    PresenterSessionWriter session_writer;
    PresenterPreferencesClock clock;
    std::function<void()> on_now;
    PreferencesAutosave preferences_autosave{preferences_writer, Preferences{}, {.now = [&] {
                                                 if (on_now) {
                                                     auto callback = std::exchange(on_now, {});
                                                     callback();
                                                 }
                                                 return clock.now();
                                             }}};
    SessionAutosave session_autosave{session_writer, SessionSnapshot{}, {.now = [&clock] { return clock.now(); }}};
    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
        PlayerPresenterPreferences{
            .initial = {},
            .load_status = PreferencesLoadStatus::FutureVersion,
            .autosave = &preferences_autosave,
            .session = {.initial = {},
                        .load_status = SessionLoadStatus::Missing,
                        .autosave = &session_autosave,
                        .now = [&clock] { return clock.now(); }},
        });
    session_autosave.scheduleRepair();
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(session_writer.starts.size(), std::size_t{1});
    QVERIFY(!session_writer.starts.front().replace_future_version);
    const QString consent = presenter.persistenceWarningRevision();
    bool final_notification_received = false;
    bool nested_notification_accepted = true;
    QObject::connect(&presenter, &PlayerPresenter::preferencePersistenceChanged, &presenter, [&] {
        if (final_notification_received ||
            presenter.persistenceWarningScope() != PlayerPresenter::PersistenceWarningScope::SessionPersistence)
            return;
        final_notification_received = true;
        nested_notification_accepted = presenter.confirmPreferenceReplacement(presenter.persistenceWarningRevision());
    });
    on_now = [&] {
        session_writer.complete(SessionSaveStatus::BlockedByFutureVersion);
        PlayerPresenterTestAccess::pollPersistence(presenter);
        QCOMPARE(presenter.persistenceWarningScope(), PlayerPresenter::PersistenceWarningScope::CombinedPersistence);
    };
    QVERIFY(presenter.confirmPreferenceReplacement(consent));
    QVERIFY(final_notification_received);
    QVERIFY(!nested_notification_accepted);
    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::FutureVersion);
    QCOMPARE(presenter.persistenceWarningScope(), PlayerPresenter::PersistenceWarningScope::SessionPersistence);
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(preferences_writer.starts.size(), std::size_t{1});
    QVERIFY(preferences_writer.starts.front().replace_future_version);
    QCOMPARE(session_writer.starts.size(), std::size_t{1});
    QVERIFY(!presenter.confirmPreferenceReplacement(consent));
    QVERIFY(presenter.confirmPreferenceReplacement(presenter.persistenceWarningRevision()));
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QCOMPARE(session_writer.starts.size(), std::size_t{2});
    QVERIFY(session_writer.starts.back().replace_future_version);
}

void PlayerPresenterTests::replacementConsentMayDestroyPresenterAtNotification() {
    AvailableTrayPlatform platform;
    PresenterPreferencesWriter writer;
    PreferencesAutosave autosave{writer, Preferences{}};
    auto presenter = std::make_unique<PlayerPresenter>(
        frozenBundle(), platform,
        PlayerPresenterPreferences{
            .initial = {}, .load_status = PreferencesLoadStatus::FutureVersion, .autosave = &autosave});
    const QString consent = presenter->persistenceWarningRevision();
    QObject::connect(presenter.get(), &PlayerPresenter::preferencePersistenceChanged, [&] { presenter.reset(); });
    QVERIFY(presenter->confirmPreferenceReplacement(consent));
    QVERIFY(!presenter);
    QVERIFY(autosave.dirty());
    autosave.poll();
    QCOMPARE(writer.starts.size(), std::size_t{1});
    QVERIFY(writer.starts.front().replace_future_version);
}

void PlayerPresenterTests::replacementConsentStopsWhenSchedulingDestroysPresenter_data() {
    QTest::addColumn<bool>("preferencesCallback");
    QTest::addColumn<int>("destroyAtCall");
    QTest::newRow("preference-repair") << true << 1;
    QTest::newRow("preference-authorization") << true << 2;
    QTest::newRow("session-repair") << false << 1;
    QTest::newRow("session-authorization") << false << 2;
}

void PlayerPresenterTests::replacementConsentStopsWhenSchedulingDestroysPresenter() {
    QFETCH(bool, preferencesCallback);
    QFETCH(int, destroyAtCall);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterPreferencesWriter preferences_writer;
    PresenterSessionWriter session_writer;
    PresenterPreferencesClock clock;
    std::unique_ptr<PlayerPresenter> presenter;
    int callbacks = 0;
    const auto callback = [&] {
        if (++callbacks == destroyAtCall)
            presenter.reset();
        return clock.now();
    };
    PreferencesAutosave preferences_autosave{
        preferences_writer, Preferences{}, {.now = [&] { return preferencesCallback ? callback() : clock.now(); }}};
    SessionAutosave session_autosave{
        session_writer, SessionSnapshot{}, {.now = [&] { return preferencesCallback ? clock.now() : callback(); }}};
    presenter = std::make_unique<PlayerPresenter>(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
        PlayerPresenterPreferences{
            .initial = {},
            .load_status = PreferencesLoadStatus::FutureVersion,
            .autosave = &preferences_autosave,
            .session = {.initial = {},
                        .load_status = SessionLoadStatus::FutureVersion,
                        .autosave = &session_autosave,
                        .now = [&clock] { return clock.now(); }},
        });
    const QString consent = presenter->persistenceWarningRevision();
    QVERIFY(!presenter->confirmPreferenceReplacement(consent));
    QVERIFY(!presenter);
    QCOMPARE(callbacks, destroyAtCall);
    if (preferencesCallback)
        QVERIFY(!session_autosave.dirty());
}

void PlayerPresenterTests::initialSessionRecoveryWarningsIdentifySessionPersistence_data() {
    QTest::addColumn<int>("loadStatus");
    QTest::addColumn<int>("warning");

    QTest::newRow("backup") << static_cast<int>(SessionLoadStatus::RecoveredFromBackup)
                            << static_cast<int>(PlayerPresenter::PreferenceWarning::RecoveredFromBackup);
    QTest::newRow("invalid-uses-defaults") << static_cast<int>(SessionLoadStatus::InvalidUsingDefaults)
                                           << static_cast<int>(PlayerPresenter::PreferenceWarning::UsingDefaults);
}

void PlayerPresenterTests::initialSessionRecoveryWarningsIdentifySessionPersistence() {
    QFETCH(int, loadStatus);
    QFETCH(int, warning);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterPreferencesWriter preferences_writer;
    PresenterSessionWriter session_writer;
    PresenterPreferencesClock clock;
    const Preferences preferences{};
    PreferencesAutosave preferences_autosave{
        preferences_writer, preferences, {.now = [&clock] { return clock.now(); }}};
    SessionAutosave session_autosave{session_writer, SessionSnapshot{}, {.now = [&clock] { return clock.now(); }}};

    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
        PlayerPresenterPreferences{
            .initial = preferences,
            .load_status = PreferencesLoadStatus::Loaded,
            .autosave = &preferences_autosave,
            .session = {.initial = {},
                        .load_status = static_cast<SessionLoadStatus>(loadStatus),
                        .autosave = &session_autosave,
                        .now = [&clock] { return clock.now(); }},
        });

    QCOMPARE(static_cast<int>(presenter.preferenceWarning()), warning);
    QCOMPARE(presenter.persistenceWarningScope(), PlayerPresenter::PersistenceWarningScope::SessionPersistence);
    QVERIFY(presenter.dismissPreferenceWarning());
    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::NoPreferenceWarning);
    QCOMPARE(presenter.persistenceWarningScope(), PlayerPresenter::PersistenceWarningScope::NoPersistenceWarningScope);
}

void PlayerPresenterTests::mixedRecoveryWarningsRevealTheResidualWarning() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterPreferencesWriter preferences_writer;
    PresenterSessionWriter session_writer;
    PresenterPreferencesClock clock;
    const Preferences preferences{};
    PreferencesAutosave preferences_autosave{
        preferences_writer, preferences, {.now = [&clock] { return clock.now(); }}};
    SessionAutosave session_autosave{session_writer, SessionSnapshot{}, {.now = [&clock] { return clock.now(); }}};

    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
        PlayerPresenterPreferences{
            .initial = preferences,
            .load_status = PreferencesLoadStatus::RecoveredFromBackup,
            .autosave = &preferences_autosave,
            .session = {.initial = {},
                        .load_status = SessionLoadStatus::InvalidUsingDefaults,
                        .autosave = &session_autosave,
                        .now = [&clock] { return clock.now(); }},
        });

    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::UsingDefaults);
    QCOMPARE(presenter.persistenceWarningScope(), PlayerPresenter::PersistenceWarningScope::SessionPersistence);
    QVERIFY(presenter.dismissPreferenceWarning());
    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::RecoveredFromBackup);
    QCOMPARE(presenter.persistenceWarningScope(), PlayerPresenter::PersistenceWarningScope::PreferencePersistence);
    QVERIFY(presenter.dismissPreferenceWarning());
    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::NoPreferenceWarning);
    QCOMPARE(presenter.persistenceWarningScope(), PlayerPresenter::PersistenceWarningScope::NoPersistenceWarningScope);
}

void PlayerPresenterTests::mixedFutureVersionAndDefaultsRevealTheResidualWarning_data() {
    QTest::addColumn<int>("preferencesLoadStatus");
    QTest::addColumn<int>("sessionLoadStatus");
    QTest::addColumn<int>("futureScope");
    QTest::addColumn<int>("defaultsScope");

    QTest::newRow("future-preferences-invalid-session")
        << static_cast<int>(PreferencesLoadStatus::FutureVersion)
        << static_cast<int>(SessionLoadStatus::InvalidUsingDefaults)
        << static_cast<int>(PlayerPresenter::PersistenceWarningScope::PreferencePersistence)
        << static_cast<int>(PlayerPresenter::PersistenceWarningScope::SessionPersistence);
    QTest::newRow("invalid-preferences-future-session")
        << static_cast<int>(PreferencesLoadStatus::InvalidUsingDefaults)
        << static_cast<int>(SessionLoadStatus::FutureVersion)
        << static_cast<int>(PlayerPresenter::PersistenceWarningScope::SessionPersistence)
        << static_cast<int>(PlayerPresenter::PersistenceWarningScope::PreferencePersistence);
}

void PlayerPresenterTests::mixedFutureVersionAndDefaultsRevealTheResidualWarning() {
    QFETCH(int, preferencesLoadStatus);
    QFETCH(int, sessionLoadStatus);
    QFETCH(int, futureScope);
    QFETCH(int, defaultsScope);
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterPreferencesWriter preferences_writer;
    PresenterSessionWriter session_writer;
    PresenterPreferencesClock clock;
    const Preferences preferences{};
    PreferencesAutosave preferences_autosave{
        preferences_writer, preferences, {.now = [&clock] { return clock.now(); }}};
    SessionAutosave session_autosave{session_writer, SessionSnapshot{}, {.now = [&clock] { return clock.now(); }}};

    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
        PlayerPresenterPreferences{
            .initial = preferences,
            .load_status = static_cast<PreferencesLoadStatus>(preferencesLoadStatus),
            .autosave = &preferences_autosave,
            .session = {.initial = {},
                        .load_status = static_cast<SessionLoadStatus>(sessionLoadStatus),
                        .autosave = &session_autosave,
                        .now = [&clock] { return clock.now(); }},
        });

    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::FutureVersion);
    QCOMPARE(static_cast<int>(presenter.persistenceWarningScope()), futureScope);
    QVERIFY(presenter.confirmPreferenceReplacement(presenter.persistenceWarningRevision()));
    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::UsingDefaults);
    QCOMPARE(static_cast<int>(presenter.persistenceWarningScope()), defaultsScope);
    QVERIFY(presenter.dismissPreferenceWarning());
    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::NoPreferenceWarning);
    QCOMPARE(presenter.persistenceWarningScope(), PlayerPresenter::PersistenceWarningScope::NoPersistenceWarningScope);
}

void PlayerPresenterTests::sessionOnlyExitTimeoutUsesTheExistingFailureWarning() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    metadata.start_result = MetadataCommandResult::Shutdown;
    PresenterPreferencesWriter preferences_writer;
    PresenterSessionWriter session_writer;
    PresenterPreferencesClock clock;
    const Preferences preferences{};
    PreferencesAutosave preferences_autosave{
        preferences_writer, preferences, {.now = [&clock] { return clock.now(); }}};
    SessionAutosave session_autosave{session_writer, SessionSnapshot{}, {.now = [&clock] { return clock.now(); }}};
    const auto source = localTestPath(u8"Timeout de sessão.wav");

    PlayerPresenter presenter(
        platform, backend, metadata, [source] { return std::vector{source}; },
        PlayerPresenterPreferences{
            .initial = preferences,
            .load_status = PreferencesLoadStatus::Loaded,
            .autosave = &preferences_autosave,
            .exit_flush_timeout = std::chrono::milliseconds{100},
            .now = [&clock] { return clock.now(); },
            .session = {.initial = {},
                        .load_status = SessionLoadStatus::Missing,
                        .autosave = &session_autosave,
                        .now = [&clock] { return clock.now(); }},
        });

    QVERIFY(presenter.openAudio());
    QVERIFY(presenter.requestExit());
    QVERIFY(presenter.exiting());
    QCOMPARE(session_writer.starts.size(), std::size_t{1});
    QVERIFY(preferences_writer.starts.empty());

    clock.advance(std::chrono::milliseconds{100});
    PlayerPresenterTestAccess::pollPersistence(presenter);
    QVERIFY(!presenter.exiting());
    QVERIFY(!presenter.exitCommitted());
    QCOMPARE(platform.exit_calls, 0);
    QCOMPARE(backend.shutdown_calls, 0);
    QCOMPARE(platform.restore_calls, 1);
    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::SaveFailed);
    QCOMPARE(presenter.persistenceWarningScope(), PlayerPresenter::PersistenceWarningScope::SessionPersistence);
    QVERIFY(presenter.retryPreferenceSave());
}

void PlayerPresenterTests::recordsWindowPlacementAsASilentPreferenceMutation() {
    AvailableTrayPlatform platform;
    PresenterPreferencesWriter writer;
    PresenterPreferencesClock clock;
    const Preferences initial{};
    PreferencesAutosave autosave{writer, initial, {.now = [&clock] { return clock.now(); }}};
    PlayerPresenter presenter(frozenBundle(), platform,
                              PlayerPresenterPreferences{.initial = initial,
                                                         .load_status = PreferencesLoadStatus::Missing,
                                                         .autosave = &autosave});
    QSignalSpy preferences_changed(&presenter, &PlayerPresenter::preferencesChanged);
    constexpr WindowPlacement placement{-1'800, 40, 960, 640};

    QCOMPARE(autosave.latestRevision(), PreferencesRevision{0});
    QVERIFY(presenter.recordWindowPlacement(placement));

    QVERIFY(presenter.preferences().window_placement.has_value());
    QCOMPARE(*presenter.preferences().window_placement, placement);
    QCOMPARE(autosave.latestRevision(), PreferencesRevision{1});
    QVERIFY(autosave.dirty());
    QVERIFY(writer.starts.empty());
    QCOMPARE(preferences_changed.size(), 0);

    QVERIFY(!presenter.recordWindowPlacement(placement));
    QVERIFY(!presenter.recordWindowPlacement(WindowPlacement{0, 0, 0, 640}));
    QVERIFY(!presenter.recordWindowPlacement(WindowPlacement{0, 0, 960, -1}));
    QCOMPARE(autosave.latestRevision(), PreferencesRevision{1});
    QCOMPARE(preferences_changed.size(), 0);
}

void PlayerPresenterTests::flushesPendingWindowPlacementBeforeRequestingPreferenceSaveOnExit() {
    AvailableTrayPlatform platform;
    PresenterPreferencesWriter writer;
    PresenterPreferencesClock clock;
    const Preferences initial{};
    PreferencesAutosave autosave{writer, initial, {.now = [&clock] { return clock.now(); }}};
    PlayerPresenter presenter(frozenBundle(), platform,
                              PlayerPresenterPreferences{.initial = initial,
                                                         .load_status = PreferencesLoadStatus::Missing,
                                                         .autosave = &autosave});
    constexpr WindowPlacement pending{-1'760, 60, 900, 600};
    bool recorded_during_flush = false;
    QObject::connect(
        &presenter, &PlayerPresenter::windowPlacementFlushRequested, &presenter,
        [&] { recorded_during_flush = presenter.recordWindowPlacement(pending); }, Qt::DirectConnection);
    QSignalSpy flush_requested(&presenter, &PlayerPresenter::windowPlacementFlushRequested);
    QSignalSpy preferences_changed(&presenter, &PlayerPresenter::preferencesChanged);

    QVERIFY(presenter.requestExit());

    QVERIFY(recorded_during_flush);
    QCOMPARE(flush_requested.size(), 1);
    QCOMPARE(preferences_changed.size(), 0);
    QCOMPARE(writer.starts.size(), std::size_t{1});
    QVERIFY(writer.starts.back().preferences.window_placement.has_value());
    QCOMPARE(*writer.starts.back().preferences.window_placement, pending);
    QVERIFY(presenter.exiting());
    QVERIFY(!presenter.exitCommitted());
    QCOMPARE(platform.exit_calls, 0);

    QVERIFY(!presenter.requestExit());
    QCOMPARE(flush_requested.size(), 1);
}

void PlayerPresenterTests::coalescesPreferenceChangesAndSavesTheFullSnapshotBeforeExit() {
    AvailableTrayPlatform platform;
    PresenterPreferencesWriter writer;
    PresenterPreferencesClock clock;
    const Preferences initial{};
    PreferencesAutosave autosave{writer, initial, {.now = [&clock] { return clock.now(); }}};
    PlayerPresenter presenter(frozenBundle(), platform,
                              PlayerPresenterPreferences{.initial = initial,
                                                         .load_status = PreferencesLoadStatus::Missing,
                                                         .autosave = &autosave});

    QVERIFY(!presenter.setSkipBackwardSeconds(5));
    QVERIFY(!presenter.setSkipBackwardSeconds(0));
    QCOMPARE(autosave.latestRevision(), PreferencesRevision{0});
    QVERIFY(presenter.setSkipBackwardSeconds(7));
    QVERIFY(presenter.setSkipForwardSeconds(20));
    QVERIFY(presenter.setThemePreference(PlayerPresenter::ThemePreference::DarkTheme));
    QCOMPARE(autosave.latestRevision(), PreferencesRevision{3});
    QVERIFY(autosave.dirty());
    QVERIFY(writer.starts.empty());

    QVERIFY(presenter.requestExit());
    QVERIFY(presenter.exiting());
    QVERIFY(!presenter.exitCommitted());
    QCOMPARE(platform.exit_calls, 0);
    QCOMPARE(writer.starts.size(), std::size_t{1});
    QCOMPARE(writer.starts.back().preferences, presenter.preferences());
    QCOMPARE(writer.starts.back().preferences.skip_backward_seconds, 7);
    QCOMPARE(writer.starts.back().preferences.skip_forward_seconds, 20);
    QCOMPARE(writer.starts.back().preferences.theme, ThemeMode::Dark);
    QVERIFY(writer.flushes.empty());
    writer.complete(PreferencesSaveStatus::Saved);
    PlayerPresenterTestAccess::pollPreferencePersistence(presenter);
    QCOMPARE(platform.exit_calls, 1);
    QVERIFY(presenter.exitCommitted());
    QVERIFY(!autosave.dirty());
}

void PlayerPresenterTests::pendingPreferenceExitKeepsBackgroundTicksReversible() {
    AvailableTrayPlatform platform;
    PresenterPreferencesWriter writer;
    PresenterPreferencesClock clock;
    const Preferences initial{};
    PreferencesAutosave autosave{writer, initial, {.now = [&clock] { return clock.now(); }}};
    PlayerPresenter presenter(frozenBundle(), platform,
                              PlayerPresenterPreferences{.initial = initial,
                                                         .load_status = PreferencesLoadStatus::Missing,
                                                         .autosave = &autosave});

    QVERIFY(presenter.loadFrozenQueue());
    QVERIFY(presenter.openDefaultAppsSettings());
    QCOMPARE(presenter.defaultAppsSettingsState(), PlayerPresenter::DefaultAppsSettingsState::Opening);
    const qint64 position_before = presenter.positionMs();
    QVERIFY(presenter.setThemePreference(PlayerPresenter::ThemePreference::DarkTheme));
    QVERIFY(presenter.requestExit());
    QVERIFY(presenter.exiting());
    QVERIFY(!presenter.exitCommitted());
    QCOMPARE(platform.exit_calls, 0);
    QCOMPARE(presenter.defaultAppsSettingsState(), PlayerPresenter::DefaultAppsSettingsState::Opening);

    platform.completeDefaultAppsSettings(DefaultAppsSettingsLaunchResult::Dispatched);
    QTRY_COMPARE(presenter.defaultAppsSettingsState(), PlayerPresenter::DefaultAppsSettingsState::Dispatched);

    const Preferences preferences_before = presenter.preferences();
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QVERIFY(!presenter.togglePlayback());
    QVERIFY(!presenter.setVolume(0.4));
    QVERIFY(!presenter.toggleMute());
    QVERIFY(!presenter.setMotionPreference(PlayerPresenter::MotionPreference::ReducedMotion));
    QVERIFY(!presenter.setSkipForwardSeconds(19));
    QCOMPARE(presenter.preferences(), preferences_before);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);

    QTimer* const timer = presenter.findChild<QTimer*>(QString{}, Qt::FindDirectChildrenOnly);
    QVERIFY(timer != nullptr);
    QVERIFY(timer->isActive());
    QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
    QVERIFY(presenter.positionMs() > position_before);

    writer.complete(PreferencesSaveStatus::Failed);
    PlayerPresenterTestAccess::pollPreferencePersistence(presenter);
    QVERIFY(!presenter.exiting());
    QVERIFY(!presenter.exitCommitted());
    QCOMPARE(platform.exit_calls, 0);
    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::SaveFailed);
    QVERIFY(presenter.openDefaultAppsSettings());
    QCOMPARE(platform.default_apps_settings_calls, 2);
}

void PlayerPresenterTests::preferenceSaveTimeoutKeepsTheApplicationResponsiveAndOpen() {
    AvailableTrayPlatform platform;
    PresenterPreferencesWriter writer;
    PresenterPreferencesClock clock;
    const Preferences initial{};
    PreferencesAutosave autosave{writer, initial, {.now = [&clock] { return clock.now(); }}};
    PlayerPresenter presenter(frozenBundle(), platform,
                              PlayerPresenterPreferences{.initial = initial,
                                                         .load_status = PreferencesLoadStatus::Missing,
                                                         .autosave = &autosave,
                                                         .exit_flush_timeout = std::chrono::milliseconds{100},
                                                         .now = [&clock] { return clock.now(); }});

    QVERIFY(presenter.requestHide());
    QCOMPARE(platform.hide_calls, 1);
    QVERIFY(presenter.setThemePreference(PlayerPresenter::ThemePreference::DarkTheme));
    QVERIFY(presenter.requestExit());
    QVERIFY(presenter.exiting());
    QVERIFY(!presenter.exitCommitted());
    QCOMPARE(platform.exit_calls, 0);
    QCOMPARE(writer.starts.size(), std::size_t{1});

    clock.advance(std::chrono::milliseconds{100});
    PlayerPresenterTestAccess::pollPreferencePersistence(presenter);
    QVERIFY(!presenter.exiting());
    QVERIFY(!presenter.exitCommitted());
    QCOMPARE(platform.exit_calls, 0);
    QCOMPARE(platform.restore_calls, 1);
    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::SaveFailed);

    PlayerPresenterTestAccess::pollPreferencePersistence(presenter);
    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::SaveFailed);

    QVERIFY(presenter.retryPreferenceSave());
    writer.complete(PreferencesSaveStatus::Failed);
    PlayerPresenterTestAccess::pollPreferencePersistence(presenter);
    QCOMPARE(writer.starts.size(), std::size_t{2});
    QVERIFY(autosave.writeActive());
    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::SaveFailed);

    writer.complete(PreferencesSaveStatus::Saved);
    PlayerPresenterTestAccess::pollPreferencePersistence(presenter);
    QVERIFY(!autosave.dirty());
    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::NoPreferenceWarning);
}

void PlayerPresenterTests::preferencePollingSleepsAfterTerminalNoticeUntilUserAction_data() {
    QTest::addColumn<int>("saveStatus");
    QTest::addColumn<int>("warning");

    QTest::newRow("failed") << static_cast<int>(PreferencesSaveStatus::Failed)
                            << static_cast<int>(PlayerPresenter::PreferenceWarning::SaveFailed);
    QTest::newRow("future-version") << static_cast<int>(PreferencesSaveStatus::BlockedByFutureVersion)
                                    << static_cast<int>(PlayerPresenter::PreferenceWarning::FutureVersion);
}

void PlayerPresenterTests::preferencePollingSleepsAfterTerminalNoticeUntilUserAction() {
    QFETCH(int, saveStatus);
    QFETCH(int, warning);
    AvailableTrayPlatform platform;
    PresenterPreferencesWriter writer;
    PresenterPreferencesClock clock;
    const Preferences initial{};
    PreferencesAutosave autosave{writer, initial, {.now = [&clock] { return clock.now(); }}};
    PlayerPresenter presenter(frozenBundle(), platform,
                              PlayerPresenterPreferences{.initial = initial,
                                                         .load_status = PreferencesLoadStatus::Missing,
                                                         .autosave = &autosave});
    QTimer* const timer = presenter.findChild<QTimer*>(QString{}, Qt::FindDirectChildrenOnly);
    QVERIFY(timer != nullptr);
    QVERIFY(!timer->isActive());

    QVERIFY(presenter.setThemePreference(PlayerPresenter::ThemePreference::DarkTheme));
    QVERIFY(timer->isActive());
    clock.advance(std::chrono::milliseconds{500});
    PlayerPresenterTestAccess::pollPreferencePersistence(presenter);
    QCOMPARE(writer.starts.size(), std::size_t{1});
    QVERIFY(autosave.writeActive());

    writer.complete(static_cast<PreferencesSaveStatus>(saveStatus));
    PlayerPresenterTestAccess::pollPreferencePersistence(presenter);
    QCOMPARE(static_cast<int>(presenter.preferenceWarning()), warning);
    QVERIFY(autosave.dirty());
    QVERIFY(!autosave.writeActive());
    QVERIFY(!timer->isActive());

    if (static_cast<PreferencesSaveStatus>(saveStatus) == PreferencesSaveStatus::Failed)
        QVERIFY(presenter.retryPreferenceSave());
    else
        QVERIFY(presenter.confirmPreferenceReplacement(presenter.persistenceWarningRevision()));
    QVERIFY(timer->isActive());

    PlayerPresenterTestAccess::pollPreferencePersistence(presenter);
    QCOMPARE(writer.starts.size(), std::size_t{2});
    QVERIFY(autosave.writeActive());
    writer.complete(PreferencesSaveStatus::Saved);
    PlayerPresenterTestAccess::pollPreferencePersistence(presenter);
    QVERIFY(!autosave.dirty());
    QVERIFY(!timer->isActive());
}

void PlayerPresenterTests::projectsTypedPreferenceRecoveryWarningsAndActions() {
    AvailableTrayPlatform platform;
    PresenterPreferencesWriter writer;
    PresenterPreferencesClock clock;
    Preferences recovered = nonDefaultPreferences();
    PreferencesAutosave autosave{writer, recovered, {.now = [&clock] { return clock.now(); }}};
    PlayerPresenter presenter(frozenBundle(), platform,
                              PlayerPresenterPreferences{.initial = recovered,
                                                         .load_status = PreferencesLoadStatus::RecoveredFromBackup,
                                                         .autosave = &autosave});

    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::RecoveredFromBackup);
    QCOMPARE(presenter.persistenceWarningScope(), PlayerPresenter::PersistenceWarningScope::PreferencePersistence);
    QVERIFY(autosave.dirty());
    QVERIFY(presenter.dismissPreferenceWarning());
    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::NoPreferenceWarning);
    QCOMPARE(presenter.persistenceWarningScope(), PlayerPresenter::PersistenceWarningScope::NoPersistenceWarningScope);
    PlayerPresenterTestAccess::pollPreferencePersistence(presenter);
    QCOMPARE(writer.starts.size(), std::size_t{1});
    writer.complete(PreferencesSaveStatus::Failed);
    PlayerPresenterTestAccess::pollPreferencePersistence(presenter);
    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::SaveFailed);
    QVERIFY(presenter.retryPreferenceSave());
    PlayerPresenterTestAccess::pollPreferencePersistence(presenter);
    QCOMPARE(writer.starts.size(), std::size_t{2});
    writer.complete(PreferencesSaveStatus::BlockedByFutureVersion);
    PlayerPresenterTestAccess::pollPreferencePersistence(presenter);
    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::FutureVersion);
    QVERIFY(presenter.confirmPreferenceReplacement(presenter.persistenceWarningRevision()));
    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::NoPreferenceWarning);
    PlayerPresenterTestAccess::pollPreferencePersistence(presenter);
    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::NoPreferenceWarning);
    QCOMPARE(writer.starts.size(), std::size_t{3});
    QVERIFY(writer.starts.back().replace_future_version);
}

void PlayerPresenterTests::initialPreferenceLoadWarningsRequireExplicitActions_data() {
    QTest::addColumn<int>("loadStatus");
    QTest::addColumn<int>("warning");

    QTest::newRow("invalid-uses-defaults") << static_cast<int>(PreferencesLoadStatus::InvalidUsingDefaults)
                                           << static_cast<int>(PlayerPresenter::PreferenceWarning::UsingDefaults);
    QTest::newRow("future-version") << static_cast<int>(PreferencesLoadStatus::FutureVersion)
                                    << static_cast<int>(PlayerPresenter::PreferenceWarning::FutureVersion);
}

void PlayerPresenterTests::initialPreferenceLoadWarningsRequireExplicitActions() {
    QFETCH(int, loadStatus);
    QFETCH(int, warning);
    AvailableTrayPlatform platform;
    PresenterPreferencesWriter writer;
    PresenterPreferencesClock clock;
    const Preferences initial{};
    PreferencesAutosave autosave{writer, initial, {.now = [&clock] { return clock.now(); }}};
    PlayerPresenter presenter(frozenBundle(), platform,
                              PlayerPresenterPreferences{.initial = initial,
                                                         .load_status = static_cast<PreferencesLoadStatus>(loadStatus),
                                                         .autosave = &autosave});

    QCOMPARE(static_cast<int>(presenter.preferenceWarning()), warning);
    QVERIFY(!autosave.dirty());
    QVERIFY(writer.starts.empty());
    if (static_cast<PreferencesLoadStatus>(loadStatus) == PreferencesLoadStatus::InvalidUsingDefaults) {
        QVERIFY(presenter.dismissPreferenceWarning());
        QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::NoPreferenceWarning);
        QVERIFY(!autosave.dirty());
        return;
    }

    QVERIFY(!presenter.dismissPreferenceWarning());
    QVERIFY(presenter.confirmPreferenceReplacement(presenter.persistenceWarningRevision()));
    QCOMPARE(presenter.preferenceWarning(), PlayerPresenter::PreferenceWarning::NoPreferenceWarning);
    QVERIFY(autosave.dirty());
    PlayerPresenterTestAccess::pollPreferencePersistence(presenter);
    QCOMPARE(writer.starts.size(), std::size_t{1});
    QVERIFY(writer.starts.front().replace_future_version);
}

void PlayerPresenterTests::exposesReadOnlyLifecycleDefaultsAndOnlyIntentMethods() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QVERIFY(!presenter.exiting());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
    QVERIFY(presenter.trayNoticeEnabled());
    QVERIFY(presenter.defaultAppsSettingsSupported());
    QVERIFY(presenter.defaultAppsSettingsAvailable());
    QCOMPARE(presenter.defaultAppsSettingsState(), PlayerPresenter::DefaultAppsSettingsState::Idle);
    const QMetaObject* const meta = presenter.metaObject();
    for (const char* name : {"exiting", "exitCommitted", "windowNotice", "trayNoticeEnabled"}) {
        const int index = meta->indexOfProperty(name);
        QVERIFY(index >= 0);
        const QMetaProperty property = meta->property(index);
        QVERIFY(property.isReadable());
        QVERIFY(!property.isWritable());
        QCOMPARE(property.notifySignal().methodSignature(), QByteArray("lifecycleChanged()"));
    }
    const QMetaProperty notice = meta->property(meta->indexOfProperty("windowNotice"));
    QVERIFY(notice.isEnumType());
    QCOMPARE(notice.enumerator().keyCount(), 3);
    QCOMPARE(notice.enumerator().keyToValue("NoNotice"), 0);
    QCOMPARE(notice.enumerator().keyToValue("TrayCloseNotice"), 1);
    QCOMPARE(notice.enumerator().keyToValue("TrayUnavailableNotice"), 2);
    for (const char* name :
         {"defaultAppsSettingsSupported", "defaultAppsSettingsAvailable", "defaultAppsSettingsState"}) {
        const int index = meta->indexOfProperty(name);
        QVERIFY(index >= 0);
        const QMetaProperty property = meta->property(index);
        QVERIFY(property.isReadable());
        QVERIFY(!property.isWritable());
        QCOMPARE(property.notifySignal().methodSignature(), QByteArray("defaultAppsSettingsChanged()"));
    }
    const QMetaProperty default_apps_state = meta->property(meta->indexOfProperty("defaultAppsSettingsState"));
    QVERIFY(default_apps_state.isEnumType());
    QCOMPARE(default_apps_state.enumerator().keyCount(), 5);
    QCOMPARE(default_apps_state.enumerator().keyToValue("Idle"), 0);
    QCOMPARE(default_apps_state.enumerator().keyToValue("Opening"), 1);
    QCOMPARE(default_apps_state.enumerator().keyToValue("Dispatched"), 2);
    QCOMPARE(default_apps_state.enumerator().keyToValue("Unavailable"), 3);
    QCOMPARE(default_apps_state.enumerator().keyToValue("Failed"), 4);
    const int fixture_mode_index = meta->indexOfProperty("usesFrozenFixture");
    QVERIFY(fixture_mode_index >= 0);
    const QMetaProperty fixture_mode = meta->property(fixture_mode_index);
    QVERIFY(fixture_mode.isReadable());
    QVERIFY(!fixture_mode.isWritable());
    QVERIFY(fixture_mode.isConstant());
    QVERIFY(!fixture_mode.hasNotifySignal());
    QVERIFY(!presenter.audioOutputSelectionSupported());
    QVERIFY(presenter.audioOutputDevices().isEmpty());
    QVERIFY(!presenter.selectAudioOutput({}));
    for (const char* name : {"audioOutputDevices", "selectedAudioOutputId"}) {
        const int index = meta->indexOfProperty(name);
        QVERIFY(index >= 0);
        const QMetaProperty property = meta->property(index);
        QVERIFY(property.isReadable());
        QVERIFY(!property.isWritable());
        QCOMPARE(property.notifySignal().methodSignature(), QByteArray("audioOutputsChanged()"));
    }
    const QSet<QByteArray> expected{"openAudio",
                                    "addAudio",
                                    "importDroppedUrls",
                                    "cancelSourceImport",
                                    "dismissSourceImportStatus",
                                    "playQueueOccurrence",
                                    "retryQueueOccurrence",
                                    "locateQueueOccurrence",
                                    "moveQueueOccurrence",
                                    "removeQueueOccurrence",
                                    "clearQueue",
                                    "toggleShuffleQueue",
                                    "toggleRepeatQueue",
                                    "loadFrozenQueue",
                                    "play",
                                    "pause",
                                    "togglePlayback",
                                    "skipBackward",
                                    "skipForward",
                                    "previousTrack",
                                    "nextTrack",
                                    "beginSeekPreview",
                                    "updateSeekPreview",
                                    "commitSeekPreview",
                                    "setVolume",
                                    "selectAudioOutput",
                                    "toggleMute",
                                    "setThemePreference",
                                    "setMotionPreference",
                                    "setLyricsTextSize",
                                    "setRoomOutputOffsetMs",
                                    "setSkipBackwardSeconds",
                                    "setSkipForwardSeconds",
                                    "setClosePreference",
                                    "setRestoreLastSession",
                                    "setStartMinimized",
                                    "setQueueEndPreference",
                                    "dismissPreferenceWarning",
                                    "retryPreferenceSave",
                                    "confirmPreferenceReplacement",
                                    "executeErrorAction",
                                    "skipUnavailable",
                                    "requestClose",
                                    "requestHide",
                                    "requestRestore",
                                    "requestExit",
                                    "openDefaultAppsSettings",
                                    "confirmWindowNotice",
                                    "cancelWindowNotice",
                                    "setTrayNoticeEnabled"};
    QSet<QByteArray> actual;
    for (int index = meta->methodOffset(); index < meta->methodCount(); ++index) {
        const QMetaMethod method = meta->method(index);
        if (method.methodType() == QMetaMethod::Signal)
            continue;
        actual.insert(method.name());
        QCOMPARE(method.returnMetaType().id(), QMetaType::Bool);
    }
    QCOMPARE(actual, expected);
    for (const char* method : {"requestClose()", "requestHide()", "requestRestore()", "requestExit()",
                               "confirmWindowNotice()", "cancelWindowNotice()", "setTrayNoticeEnabled(bool)"})
        QVERIFY(meta->indexOfMethod(method) >= 0);
    QCOMPARE(meta->indexOfMethod("reportTrayUnavailable()"), -1);
    QCOMPARE(meta->indexOfMethod("refreshPlatformAvailability()"), -1);
    QCOMPARE(meta->indexOfProperty("noticeSeen"), -1);
    QCOMPARE(meta->indexOfProperty("backend"), -1);
    QCOMPARE(meta->indexOfProperty("timer"), -1);
}

void PlayerPresenterTests::opensDefaultAppsSettingsOnceUntilTheTypedCompletionArrives() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QSignalSpy changed(&presenter, &PlayerPresenter::defaultAppsSettingsChanged);

    QVERIFY(presenter.defaultAppsSettingsAvailable());
    QCOMPARE(presenter.defaultAppsSettingsState(), PlayerPresenter::DefaultAppsSettingsState::Idle);
    QVERIFY(presenter.openDefaultAppsSettings());
    QCOMPARE(presenter.defaultAppsSettingsState(), PlayerPresenter::DefaultAppsSettingsState::Opening);
    QCOMPARE(platform.default_apps_settings_calls, 1);
    QCOMPARE(changed.size(), 1);

    QVERIFY(!presenter.openDefaultAppsSettings());
    QCOMPARE(platform.default_apps_settings_calls, 1);
    QCOMPARE(changed.size(), 1);

    platform.completeDefaultAppsSettings(DefaultAppsSettingsLaunchResult::Dispatched);
    QTRY_COMPARE(presenter.defaultAppsSettingsState(), PlayerPresenter::DefaultAppsSettingsState::Dispatched);
    QCOMPARE(changed.size(), 2);

    QVERIFY(presenter.openDefaultAppsSettings());
    QCOMPARE(presenter.defaultAppsSettingsState(), PlayerPresenter::DefaultAppsSettingsState::Opening);
    QCOMPARE(platform.default_apps_settings_calls, 2);
}

void PlayerPresenterTests::defaultAppsSettingsUnavailableAndFailureStatesAreRecoverable() {
    AvailableTrayPlatform unavailable_platform;
    unavailable_platform.default_apps_available = false;
    PlayerPresenter unavailable(frozenBundle(), unavailable_platform);
    QSignalSpy unavailable_changed(&unavailable, &PlayerPresenter::defaultAppsSettingsChanged);

    QVERIFY(!unavailable.defaultAppsSettingsAvailable());
    QVERIFY(!unavailable.openDefaultAppsSettings());
    QCOMPARE(unavailable.defaultAppsSettingsState(), PlayerPresenter::DefaultAppsSettingsState::Unavailable);
    QCOMPARE(unavailable_platform.default_apps_settings_calls, 0);
    QCOMPARE(unavailable_changed.size(), 1);

    AvailableTrayPlatform failed_platform;
    failed_platform.default_apps_settings_immediate_result = DefaultAppsSettingsLaunchResult::Failed;
    PlayerPresenter failed(frozenBundle(), failed_platform);
    QVERIFY(failed.openDefaultAppsSettings());
    QTRY_COMPARE(failed.defaultAppsSettingsState(), PlayerPresenter::DefaultAppsSettingsState::Failed);
    QCOMPARE(failed_platform.default_apps_settings_calls, 1);

    failed_platform.default_apps_settings_immediate_result = DefaultAppsSettingsLaunchResult::Dispatched;
    QVERIFY(failed.openDefaultAppsSettings());
    QTRY_COMPARE(failed.defaultAppsSettingsState(), PlayerPresenter::DefaultAppsSettingsState::Dispatched);
    QCOMPARE(failed_platform.default_apps_settings_calls, 2);

    AvailableTrayPlatform throwing_platform;
    throwing_platform.throw_on_default_apps_settings_launch = true;
    PlayerPresenter throwing(frozenBundle(), throwing_platform);
    QVERIFY(throwing.openDefaultAppsSettings());
    QCOMPARE(throwing.defaultAppsSettingsState(), PlayerPresenter::DefaultAppsSettingsState::Failed);
    QCOMPARE(throwing_platform.default_apps_settings_calls, 1);
}

void PlayerPresenterTests::defaultAppsSettingsCompletionIsInertAfterExitOrDestruction() {
    AvailableTrayPlatform exiting_platform;
    PlayerPresenter exiting(frozenBundle(), exiting_platform);
    QVERIFY(exiting.openDefaultAppsSettings());
    QCOMPARE(exiting.defaultAppsSettingsState(), PlayerPresenter::DefaultAppsSettingsState::Opening);
    std::optional<bool> reentrant_launch_result;
    connect(&exiting, &PlayerPresenter::defaultAppsSettingsChanged, &exiting,
            [&] { reentrant_launch_result = exiting.openDefaultAppsSettings(); });
    QVERIFY(exiting.requestExit());
    QVERIFY(exiting.exiting());
    QVERIFY(exiting.exitCommitted());
    QVERIFY(reentrant_launch_result.has_value());
    QVERIFY(!*reentrant_launch_result);
    QCOMPARE(exiting.defaultAppsSettingsState(), PlayerPresenter::DefaultAppsSettingsState::Idle);
    exiting_platform.completeDefaultAppsSettings(DefaultAppsSettingsLaunchResult::Dispatched);
    QCoreApplication::processEvents();
    QCOMPARE(exiting.defaultAppsSettingsState(), PlayerPresenter::DefaultAppsSettingsState::Idle);
    QVERIFY(!exiting.openDefaultAppsSettings());
    QCOMPARE(exiting_platform.default_apps_settings_calls, 1);

    AvailableTrayPlatform destroyed_platform;
    auto destroyed = std::make_unique<PlayerPresenter>(frozenBundle(), destroyed_platform);
    QVERIFY(destroyed->openDefaultAppsSettings());
    destroyed.reset();
    destroyed_platform.completeDefaultAppsSettings(DefaultAppsSettingsLaunchResult::Dispatched);
    QCoreApplication::processEvents();
    QCOMPARE(destroyed_platform.default_apps_settings_calls, 1);
}

void PlayerPresenterTests::firstCloseWaitsForAcknowledgementAndDoesNotRepeatTheNotice() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QSignalSpy lifecycle(&presenter, &PlayerPresenter::lifecycleChanged);
    QVERIFY(presenter.requestClose());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayCloseNotice);
    QCOMPARE(lifecycle.size(), 1);
    QCOMPARE(platform.hide_calls, 0);
    QCOMPARE(platform.exit_calls, 0);
    QVERIFY(!presenter.requestClose());
    QCOMPARE(lifecycle.size(), 1);
    QCOMPARE(platform.hide_calls, 0);
    QVERIFY(presenter.confirmWindowNotice());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
    QCOMPARE(platform.hide_calls, 1);
    QCOMPARE(lifecycle.size(), 2);
    QVERIFY(presenter.requestRestore());
    QCOMPARE(platform.restore_calls, 1);
    QVERIFY(presenter.requestClose());
    QCOMPARE(platform.hide_calls, 2);
    QCOMPARE(platform.minimize_calls, 0);
    QCOMPARE(platform.exit_calls, 0);
    QCOMPARE(lifecycle.size(), 2);
    QVERIFY(!presenter.confirmWindowNotice());
    QVERIFY(!presenter.cancelWindowNotice());
}

void PlayerPresenterTests::unavailableTrayMinimizesAndKeepsTheExplanationUntilRestored() {
    AvailableTrayPlatform platform;
    platform.available = false;
    PlayerPresenter presenter(frozenBundle(), platform);
    QSignalSpy lifecycle(&presenter, &PlayerPresenter::lifecycleChanged);
    QVERIFY(presenter.requestClose());
    QCOMPARE(platform.minimize_calls, 1);
    QCOMPARE(platform.hide_calls, 0);
    QCOMPARE(platform.exit_calls, 0);
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayUnavailableNotice);
    QCOMPARE(lifecycle.size(), 1);
    QVERIFY(presenter.requestRestore());
    QCOMPARE(platform.restore_calls, 1);
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayUnavailableNotice);
    QCOMPARE(lifecycle.size(), 1);
    QVERIFY(presenter.confirmWindowNotice());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
    QCOMPARE(platform.minimize_calls, 1);
    QCOMPARE(lifecycle.size(), 2);
}

void PlayerPresenterTests::cancellingTheFirstNoticeDoesNotConsumeIt() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QSignalSpy lifecycle(&presenter, &PlayerPresenter::lifecycleChanged);
    QVERIFY(presenter.requestClose());
    QVERIFY(presenter.cancelWindowNotice());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
    QCOMPARE(lifecycle.size(), 2);
    QVERIFY(!presenter.cancelWindowNotice());
    QVERIFY(presenter.requestClose());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayCloseNotice);
    QCOMPARE(lifecycle.size(), 3);
    QCOMPARE(platform.hide_calls, 0);
    QCOMPARE(platform.minimize_calls, 0);
    QCOMPARE(platform.exit_calls, 0);
}

void PlayerPresenterTests::noticeOptOutIsInMemoryAndDoesNotActOnThePendingWindow() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QSignalSpy lifecycle(&presenter, &PlayerPresenter::lifecycleChanged);
    QSignalSpy preferences(&presenter, &PlayerPresenter::preferencesChanged);
    QVERIFY(presenter.requestClose());
    QVERIFY(presenter.setTrayNoticeEnabled(false));
    QVERIFY(!presenter.setTrayNoticeEnabled(false));
    QVERIFY(!presenter.trayNoticeEnabled());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayCloseNotice);
    QCOMPARE(lifecycle.size(), 2);
    QCOMPARE(platform.hide_calls, 0);
    QVERIFY(presenter.cancelWindowNotice());
    QVERIFY(presenter.requestClose());
    QCOMPARE(platform.hide_calls, 1);
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
    QCOMPARE(lifecycle.size(), 3);

    QVERIFY(presenter.requestRestore());
    QVERIFY(presenter.setTrayNoticeEnabled(true));
    QVERIFY(presenter.requestClose());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayCloseNotice);
    QVERIFY(presenter.confirmWindowNotice());
    QCOMPARE(platform.hide_calls, 2);
    QVERIFY(presenter.setTrayNoticeEnabled(false));
    QVERIFY(presenter.setTrayNoticeEnabled(true));
    QVERIFY(presenter.requestRestore());
    QVERIFY(presenter.requestClose());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
    QCOMPARE(platform.hide_calls, 3);
    QCOMPARE(lifecycle.size(), 8);
    QCOMPARE(preferences.size(), 0);
    QCOMPARE(platform.exit_calls, 0);

    AvailableTrayPlatform another_platform;
    PlayerPresenter another(frozenBundle(), another_platform);
    QVERIFY(another.trayNoticeEnabled());
    QVERIFY(another.requestClose());
    QCOMPARE(another.windowNotice(), PlayerPresenter::WindowNotice::TrayCloseNotice);
    QVERIFY(another.setTrayNoticeEnabled(false));
    QVERIFY(another.confirmWindowNotice());
    QCOMPARE(another_platform.hide_calls, 1);
    QVERIFY(another.setTrayNoticeEnabled(true));
    QVERIFY(another.requestRestore());
    QVERIFY(another.requestClose());
    QCOMPARE(another.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
    QCOMPARE(another_platform.hide_calls, 2);
}

void PlayerPresenterTests::closeRefreshesAvailabilityBeforeDecidingTheWindowAction() {
    AvailableTrayPlatform platform;
    platform.available = false;
    PlayerPresenter presenter(frozenBundle(), platform);
    QSignalSpy availability(&presenter, &PlayerPresenter::platformChanged);
    platform.available = true;
    QVERIFY(presenter.requestClose());
    QVERIFY(presenter.trayAvailable());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayCloseNotice);
    QCOMPARE(availability.size(), 1);
    QVERIFY(presenter.cancelWindowNotice());
    platform.available = false;
    QVERIFY(presenter.requestClose());
    QVERIFY(!presenter.trayAvailable());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayUnavailableNotice);
    QCOMPARE(availability.size(), 2);
    QCOMPARE(platform.minimize_calls, 1);
    QCOMPARE(platform.hide_calls, 0);
    QCOMPARE(platform.exit_calls, 0);
}

void PlayerPresenterTests::confirmationRechecksAvailabilityWithoutPublishingATransientNotice() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QSignalSpy lifecycle(&presenter, &PlayerPresenter::lifecycleChanged);
    QList<PlayerPresenter::WindowNotice> notices;
    connect(&presenter, &PlayerPresenter::lifecycleChanged, &presenter,
            [&] { notices.append(presenter.windowNotice()); });
    QVERIFY(presenter.requestClose());
    platform.available = false;
    QVERIFY(!presenter.requestClose());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayCloseNotice);
    QCOMPARE(platform.minimize_calls, 0);
    QCOMPARE(lifecycle.size(), 1);
    QVERIFY(presenter.confirmWindowNotice());
    QVERIFY(!presenter.trayAvailable());
    QCOMPARE(platform.minimize_calls, 1);
    QCOMPARE(platform.hide_calls, 0);
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayUnavailableNotice);
    QCOMPARE(notices, QList<PlayerPresenter::WindowNotice>({PlayerPresenter::WindowNotice::TrayCloseNotice,
                                                            PlayerPresenter::WindowNotice::TrayUnavailableNotice}));
    QVERIFY(presenter.confirmWindowNotice());
    platform.available = true;
    QVERIFY(presenter.requestClose());
    QCOMPARE(platform.hide_calls, 1);
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
    QCOMPARE(platform.exit_calls, 0);
}

void PlayerPresenterTests::explicitHideIgnoresClosePreferenceAndBypassesTheNotice_data() {
    QTest::addColumn<bool>("tray_available");
    QTest::addColumn<bool>("close_exits");
    QTest::newRow("tray-minimize") << true << false;
    QTest::newRow("tray-exit") << true << true;
    QTest::newRow("no-tray-minimize") << false << false;
    QTest::newRow("no-tray-exit") << false << true;
}

void PlayerPresenterTests::explicitHideIgnoresClosePreferenceAndBypassesTheNotice() {
    QFETCH(bool, tray_available);
    QFETCH(bool, close_exits);
    AvailableTrayPlatform platform;
    platform.available = tray_available;
    PlayerPresenter presenter(frozenBundle(), platform);
    if (close_exits)
        QVERIFY(presenter.setClosePreference(PlayerPresenter::ClosePreference::Exit));
    QSignalSpy lifecycle(&presenter, &PlayerPresenter::lifecycleChanged);
    QVERIFY(presenter.requestHide());
    QCOMPARE(platform.hide_calls, tray_available ? 1 : 0);
    QCOMPARE(platform.minimize_calls, tray_available ? 0 : 1);
    QCOMPARE(platform.exit_calls, 0);
    QCOMPARE(presenter.closePreference(),
             close_exits ? PlayerPresenter::ClosePreference::Exit : PlayerPresenter::ClosePreference::MinimizeToTray);
    QCOMPARE(presenter.windowNotice(), tray_available ? PlayerPresenter::WindowNotice::NoNotice
                                                      : PlayerPresenter::WindowNotice::TrayUnavailableNotice);
    QCOMPARE(lifecycle.size(), tray_available ? 0 : 1);
    QVERIFY(!presenter.exiting());
}

void PlayerPresenterTests::explicitHideClearsAPendingFirstNoticeWithoutAcknowledgingIt() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QSignalSpy lifecycle(&presenter, &PlayerPresenter::lifecycleChanged);
    QVERIFY(presenter.requestClose());
    QVERIFY(presenter.setClosePreference(PlayerPresenter::ClosePreference::Exit));
    QVERIFY(presenter.requestHide());
    QCOMPARE(platform.hide_calls, 1);
    QCOMPARE(platform.exit_calls, 0);
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
    QCOMPARE(lifecycle.size(), 2);
    QVERIFY(presenter.requestRestore());
    QVERIFY(presenter.setClosePreference(PlayerPresenter::ClosePreference::MinimizeToTray));
    QVERIFY(presenter.requestClose());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayCloseNotice);
    QCOMPARE(lifecycle.size(), 3);
}

void PlayerPresenterTests::closeExitPreferenceOverridesNoticesAndTrayAvailability_data() {
    QTest::addColumn<bool>("tray_available");
    QTest::newRow("tray") << true;
    QTest::newRow("no-tray") << false;
}

void PlayerPresenterTests::closeExitPreferenceOverridesNoticesAndTrayAvailability() {
    QFETCH(bool, tray_available);
    AvailableTrayPlatform platform;
    platform.available = tray_available;
    PlayerPresenter presenter(frozenBundle(), platform);
    if (tray_available)
        QVERIFY(presenter.requestClose());
    else
        presenter.reportTrayUnavailable();
    QVERIFY(presenter.setClosePreference(PlayerPresenter::ClosePreference::Exit));
    QVERIFY(presenter.requestClose());
    QVERIFY(presenter.exiting());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
    QCOMPARE(presenter.closePreference(), PlayerPresenter::ClosePreference::Exit);
    QCOMPARE(platform.exit_calls, 1);
    QCOMPARE(platform.hide_calls, 0);
    QCOMPARE(platform.minimize_calls, 0);
    QVERIFY(!presenter.requestClose());
    QCOMPARE(platform.exit_calls, 1);
}

void PlayerPresenterTests::reportingTrayLossOnlyProjectsAnIdempotentExplanation() {
    AvailableTrayPlatform platform;
    PlayerPresenter presenter(frozenBundle(), platform);
    QVERIFY(presenter.loadFrozenQueue());
    QTimer* const timer = presenter.findChild<QTimer*>(QString{}, Qt::FindDirectChildrenOnly);
    QVERIFY(timer != nullptr);
    QSignalSpy lifecycle(&presenter, &PlayerPresenter::lifecycleChanged);
    QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);
    QSignalSpy preferences(&presenter, &PlayerPresenter::preferencesChanged);
    presenter.reportTrayUnavailable();
    presenter.reportTrayUnavailable();
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayUnavailableNotice);
    QCOMPARE(lifecycle.size(), 1);
    QVERIFY(presenter.requestRestore());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayUnavailableNotice);
    QCOMPARE(lifecycle.size(), 1);
    QVERIFY(presenter.cancelWindowNotice());
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
    QCOMPARE(lifecycle.size(), 2);
    QCOMPARE(platform.hide_calls, 0);
    QCOMPARE(platform.minimize_calls, 0);
    QCOMPARE(platform.exit_calls, 0);
    QCOMPARE(platform.restore_calls, 1);
    QCOMPARE(playback.size(), 0);
    QCOMPARE(preferences.size(), 0);
    QCOMPARE(presenter.positionMs(), 84'000);
    QVERIFY(timer->isActive());
}

void PlayerPresenterTests::lifecyclePreservesPlaybackQueueModelAndPendingBackendWork_data() {
    QTest::addColumn<bool>("tray_available");
    QTest::addColumn<QString>("playback_state");
    QTest::newRow("tray-ready") << true << QStringLiteral("ready");
    QTest::newRow("tray-paused") << true << QStringLiteral("paused");
    QTest::newRow("tray-playing") << true << QStringLiteral("playing");
    QTest::newRow("no-tray-ready") << false << QStringLiteral("ready");
    QTest::newRow("no-tray-paused") << false << QStringLiteral("paused");
    QTest::newRow("no-tray-playing") << false << QStringLiteral("playing");
}

void PlayerPresenterTests::lifecyclePreservesPlaybackQueueModelAndPendingBackendWork() {
    QFETCH(bool, tray_available);
    QFETCH(QString, playback_state);
    AvailableTrayPlatform platform;
    platform.available = tray_available;
    FixtureBundle bundle = frozenBundle();
    bundle.states[1].playback_state = playback_state;
    PlayerPresenter presenter(std::move(bundle), platform);
    QVERIFY(presenter.loadFrozenQueue());
    QVERIFY(presenter.setVolume(0.35));
    QVERIFY(presenter.toggleMute());
    QVERIFY(presenter.setSkipBackwardSeconds(7));
    QVERIFY(presenter.setSkipForwardSeconds(12));
    QVERIFY(presenter.setThemePreference(PlayerPresenter::ThemePreference::DarkTheme));
    QVERIFY(presenter.setMotionPreference(PlayerPresenter::MotionPreference::ReducedMotion));
    QVERIFY(presenter.setRestoreLastSession(false));
    QVERIFY(presenter.setQueueEndPreference(PlayerPresenter::QueueEndPreference::StopClearSelection));
    QVERIFY(presenter.beginSeekPreview(124'000));
    QVERIFY(presenter.commitSeekPreview());
    PlayerPresenterTestAccess::rejectNextSeek(presenter);
    QueueListModel* const model = presenter.queueModel();
    const QPersistentModelIndex current = model->index(16, 0);
    const auto occurrence_ids = [&] {
        QStringList ids;
        for (int row = 0; row < model->rowCount(); ++row)
            ids.append(model->data(model->index(row, 0), QueueListModel::OccurrenceIdRole).toString());
        return ids;
    };
    const QStringList ids = occurrence_ids();
    QCOMPARE(ids.size(), 1000);
    const auto presentation = [&] {
        QVariantMap result;
        for (const char* name : {"stateId",
                                 "playbackState",
                                 "queueLoaded",
                                 "currentOccurrenceId",
                                 "currentQueueIndex",
                                 "contextOccurrenceId",
                                 "positionMs",
                                 "confirmedPositionMs",
                                 "seekPreviewActive",
                                 "seekPending",
                                 "durationMs",
                                 "seekable",
                                 "currentTitle",
                                 "currentArtist",
                                 "currentAlbum",
                                 "volume",
                                 "muted",
                                 "skipBackwardSeconds",
                                 "skipForwardSeconds",
                                 "themePreference",
                                 "motionPreference",
                                 "closePreference",
                                 "queueEndPreference",
                                 "restoreLastSession",
                                 "startMinimized"})
            result.insert(QString::fromLatin1(name), presenter.property(name));
        return result;
    };
    const QVariantMap before = presentation();
    QSignalSpy playback(&presenter, &PlayerPresenter::playbackChanged);
    QSignalSpy states(&presenter, &PlayerPresenter::stateChanged);
    QSignalSpy preferences(&presenter, &PlayerPresenter::preferencesChanged);
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    QSignalSpy changes(model, &QAbstractItemModel::dataChanged);
    const auto verify_preserved = [&] {
        QCOMPARE(presentation(), before);
        QCOMPARE(presenter.queueModel(), model);
        QCOMPARE(occurrence_ids(), ids);
        QVERIFY(current.isValid());
        QCOMPARE(current.data(QueueListModel::OccurrenceIdRole).toString(), QStringLiteral("queue-0017"));
        QCOMPARE(playback.size(), 0);
        QCOMPARE(states.size(), 0);
        QCOMPARE(preferences.size(), 0);
        QCOMPARE(resets.size(), 0);
        QCOMPARE(changes.size(), 0);
    };
    QVERIFY(presenter.requestClose());
    verify_preserved();
    QVERIFY(presenter.cancelWindowNotice());
    verify_preserved();
    QVERIFY(presenter.requestClose());
    QVERIFY(presenter.confirmWindowNotice());
    verify_preserved();
    QVERIFY(presenter.requestRestore());
    verify_preserved();
    QVERIFY(presenter.requestHide());
    verify_preserved();
    presenter.reportTrayUnavailable();
    QVERIFY(presenter.confirmWindowNotice());
    verify_preserved();
    QVERIFY(presenter.setTrayNoticeEnabled(false));
    QVERIFY(presenter.requestClose());
    verify_preserved();
    QVERIFY(presenter.requestRestore());
    verify_preserved();
    QCOMPARE(platform.exit_calls, 0);

    QSignalSpy rejected(&presenter, &PlayerPresenter::seekRejected);
    QVERIFY(presenter.advanceOneTick());
    QCOMPARE(rejected.size(), 1);
    QCOMPARE(presenter.positionMs(), 84'000);
    QCOMPARE(presenter.confirmedPositionMs(), 84'000);
    QVERIFY(!presenter.seekPending());
    QCOMPARE(presenter.volume(), 0.35);
    QVERIFY(presenter.muted());
}

void PlayerPresenterTests::exitPublishesItsGuardAndStopsTicksBeforeRequestingThePlatform_data() {
    explicitHideIgnoresClosePreferenceAndBypassesTheNotice_data();
}

void PlayerPresenterTests::exitPublishesItsGuardAndStopsTicksBeforeRequestingThePlatform() {
    QFETCH(bool, tray_available);
    QFETCH(bool, close_exits);
    AvailableTrayPlatform platform;
    platform.available = tray_available;
    PlayerPresenter presenter(frozenBundle(), platform);
    if (close_exits)
        QVERIFY(presenter.setClosePreference(PlayerPresenter::ClosePreference::Exit));
    QVERIFY(presenter.loadFrozenQueue());
    QVERIFY(presenter.beginSeekPreview(124'000));
    QVERIFY(presenter.commitSeekPreview());
    QTimer* const timer = presenter.findChild<QTimer*>(QString{}, Qt::FindDirectChildrenOnly);
    QVERIFY(timer != nullptr);
    QVERIFY(timer->isActive());
    presenter.reportTrayUnavailable();
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::TrayUnavailableNotice);
    QSignalSpy lifecycle(&presenter, &PlayerPresenter::lifecycleChanged);
    QStringList order;
    connect(&presenter, &PlayerPresenter::lifecycleChanged, &presenter, [&] {
        order.append(QStringLiteral("lifecycle"));
        QVERIFY(presenter.exiting());
        QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
        QVERIFY(!timer->isActive());
        QCOMPARE(platform.exit_calls, 0);
        QVERIFY(!presenter.requestExit());
    });
    platform.on_exit = [&] {
        order.append(QStringLiteral("platform-exit"));
        QVERIFY(presenter.exiting());
        QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
        QVERIFY(!timer->isActive());
        QVERIFY(!presenter.requestClose());
    };

    QVERIFY(presenter.requestExit());

    QCOMPARE(order, QStringList({QStringLiteral("lifecycle"), QStringLiteral("platform-exit")}));
    QCOMPARE(lifecycle.size(), 1);
    QCOMPARE(platform.exit_calls, 1);
    QVERIFY(!presenter.requestExit());
    QVERIFY(!presenter.requestClose());
    QVERIFY(!presenter.requestHide());
    QVERIFY(!presenter.requestRestore());
    QVERIFY(!presenter.confirmWindowNotice());
    QVERIFY(!presenter.cancelWindowNotice());
    QVERIFY(!presenter.setTrayNoticeEnabled(false));
    presenter.reportTrayUnavailable();
    QCOMPARE(presenter.windowNotice(), PlayerPresenter::WindowNotice::NoNotice);
    QVERIFY(presenter.trayNoticeEnabled());
    QCOMPARE(lifecycle.size(), 1);
    QCOMPARE(platform.exit_calls, 1);
    QCOMPARE(platform.hide_calls, 0);
    QCOMPARE(platform.minimize_calls, 0);
    QCOMPARE(platform.restore_calls, 0);
    QTest::qWait(250);
    QCOMPARE(presenter.positionMs(), 124'000);
    QCOMPARE(presenter.confirmedPositionMs(), 84'000);
    QVERIFY(presenter.seekPending());
    QVERIFY(presenter.seekPreviewActive());
    // Direct transport intents are inert after the irreversible exit boundary.
    QVERIFY(!presenter.togglePlayback());
    QVERIFY(!presenter.togglePlayback());
    QVERIFY(!timer->isActive());
}

namespace {
[[nodiscard]] std::vector<std::filesystem::path> roomSources(const int count) {
    std::vector<std::filesystem::path> sources;
    for (int index = 0; index < count; ++index) {
        std::u8string name{u8"Sala "};
        name.push_back(static_cast<char8_t>(u8'A' + index));
        name += u8".wav";
        sources.push_back(localTestPath(name));
    }
    return sources;
}

// A listening room hosted over a real presenter, with fp-dj (a DJ) admitted: states holds the playback of every
// state fp-dj receives. The room's timers are real (QTimer); the host clock is the given one.
class PresenterRoom final {
  public:
    PresenterRoom(PlayerPresenter& presenter, std::function<std::int64_t()> clock) : player_{presenter} {
        roster_.stored.listeners.push_back(KnownListener{"fp-dj", "Dj", 2, {}, false});
        roster_.status = RoomRosterLoadStatus::Loaded;
        host = std::make_unique<ListeningRoomHost>(ListeningRoomHostDependencies{
            .roster_store = roster_,
            .player = player_,
            .endpoint = endpoint_,
            .catalog = catalog_,
            .host_clock_ms = std::move(clock),
            .random_bytes =
                [this](std::span<std::uint8_t> bytes) {
                    for (auto& byte : bytes)
                        byte = next_random_++;
                },
            .host_fingerprint = {0xab},
            .host_profile = ListenerProfile{"Joao", AvatarChoice{}},
            .schedule =
                [this](const std::int64_t delay_ms, std::function<void()> task) {
                    QTimer::singleShot(std::chrono::milliseconds{delay_ms}, &context_, std::move(task));
                },
        });
        if (!host->open({}).has_value())
            return;
        auto [host_side, guest_side] = testing::FakeRoomLink::pair("ab" + std::string(62, '0'), "fp-dj");
        guest_ = guest_side;
        guest_->onControl = [this](RoomControlMessage message) {
            if (const auto* state = std::get_if<RoomStateMessage>(&message))
                states.push_back(state->snapshot.playback);
        };
        endpoint_.admit(host_side);
        joined = guest_->sendControl(RoomHello{room_protocol_version, "Dj", AvatarChoice{}}) && !states.empty();
    }
    PresenterRoom(const PresenterRoom&) = delete;
    PresenterRoom& operator=(const PresenterRoom&) = delete;

    // fp-dj asks for something.
    bool intent(RoomIntent intent) {
        return guest_->sendControl(RoomIntentMessage{++next_request_, std::move(intent)});
    }

  private:
    PresenterRoomHostPlayer player_;
    testing::InMemoryRoomRosterStore roster_;
    testing::FakeRoomHostEndpoint endpoint_;
    testing::FakeRoomTrackCatalog catalog_;
    QObject context_;
    std::uint8_t next_random_{1};
    std::shared_ptr<testing::FakeRoomLink> guest_;
    std::uint64_t next_request_{0};

  public:
    std::unique_ptr<ListeningRoomHost> host;
    std::vector<RoomPlayback> states;
    bool joined{false};
};

// Delivers the presenter's queued backend events without letting room_play_lead_ms pass.
void deliverQueued() {
    for (int turn = 0; turn < 5; ++turn)
        QCoreApplication::processEvents();
}
} // namespace

void PlayerPresenterTests::roomPlayOrderAdvancesInQueueOrderWhileShuffled() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    backend.accepted_load_status = PlaybackStatus::Ready;
    PresenterMetadataReader metadata;
    const auto sources = roomSources(7);
    PlayerPresenter presenter(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(presenter.openAudio());
    presenter.setQueueShuffleSeedForTest(47);
    QVERIFY(presenter.toggleShuffleQueue());
    // While a room is open the queue order is the play order: shuffle is a physical reorder of the
    // pending part (when the room opens shuffled, or when shuffle is turned on), never a random pick.
    presenter.setRoomPlayOrder(true);
    for (int step = 0; step < 5; ++step) {
        const auto before = queueOccurrenceIds(*presenter.queueModelObject());
        const auto expected = before[presenter.currentQueueIndex() + 1];
        if (step % 2 == 0) {
            QVERIFY(presenter.nextTrack());
        } else {
            backend.publishEvent(PlaybackEventType::Ended, {.status = PlaybackStatus::Paused,
                                                            .position_ms = 2'000,
                                                            .duration_ms = 2'000,
                                                            .seekable = true,
                                                            .generation = backend.snapshot().generation});
        }
        QTRY_COMPARE(presenter.currentOccurrenceId(), expected);
        QCOMPARE(queueOccurrenceIds(*presenter.queueModelObject()), before);
    }
    presenter.setRoomPlayOrder(false);
}

namespace {
// The occurrences after the current one, and everything up to it.
[[nodiscard]] QStringList pendingPart(const QStringList& ids, const int current) { return ids.mid(current + 1); }
[[nodiscard]] QStringList playedPart(const QStringList& ids, const int current) { return ids.mid(0, current + 1); }
[[nodiscard]] QStringList sorted(QStringList ids) {
    ids.sort();
    return ids;
}
[[nodiscard]] QStringList roomQueueIds(const LocalFileQueueSnapshot& queue) {
    QStringList ids;
    for (const auto& item : queue.items)
        ids.append(QString::number(item.occurrence_id.value));
    return ids;
}
} // namespace

void PlayerPresenterTests::openingARoomWhileShuffledReordersThePendingQueueOnce() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    backend.accepted_load_status = PlaybackStatus::Ready;
    PresenterMetadataReader metadata;
    const auto sources = roomSources(7);
    PlayerPresenter presenter(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(presenter.openAudio());
    presenter.setQueueShuffleSeedForTest(47);
    QVERIFY(presenter.toggleShuffleQueue());
    QVERIFY(presenter.nextTrack());
    PresenterRoomHostPlayer player{presenter};
    int changes = 0;
    player.onChanged = [&changes] { ++changes; };
    const auto current = presenter.currentQueueIndex();
    const auto current_id = presenter.currentOccurrenceId();
    const auto before = roomQueueIds(player.state().queue);

    presenter.setQueueShuffleSeedForTest(5);
    presenter.setRoomPlayOrder(true);
    const auto opened = roomQueueIds(player.state().queue);
    QCOMPARE(playedPart(opened, current), playedPart(before, current));
    QVERIFY(pendingPart(opened, current) != pendingPart(before, current));
    QCOMPARE(sorted(pendingPart(opened, current)), sorted(pendingPart(before, current)));
    QCOMPARE(presenter.currentOccurrenceId(), current_id);
    QCOMPARE(player.state().queue, presenter.playlistQueueSnapshot());
    QVERIFY(changes > 0);

    // Once: the room staying open does not shuffle again.
    presenter.setRoomPlayOrder(true);
    QCOMPARE(roomQueueIds(player.state().queue), opened);
    QVERIFY(presenter.nextTrack());
    QTRY_COMPARE(presenter.currentQueueIndex(), current + 1);
    QCOMPARE(roomQueueIds(player.state().queue), opened);
    presenter.setRoomPlayOrder(false);
}

void PlayerPresenterTests::turningShuffleOnInARoomReordersWhatGuestsSee() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    backend.accepted_load_status = PlaybackStatus::Ready;
    PresenterMetadataReader metadata;
    const auto sources = roomSources(7);
    PlayerPresenter presenter(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(presenter.openAudio());
    presenter.setRoomPlayOrder(true);
    PresenterRoomHostPlayer player{presenter};
    int changes = 0;
    player.onChanged = [&changes] { ++changes; };
    const auto current = presenter.currentQueueIndex();
    const auto before = roomQueueIds(player.state().queue);

    presenter.setQueueShuffleSeedForTest(47);
    QVERIFY(presenter.toggleShuffleQueue());
    const auto shuffled = roomQueueIds(player.state().queue);
    QCOMPARE(playedPart(shuffled, current), playedPart(before, current));
    QVERIFY(pendingPart(shuffled, current) != pendingPart(before, current));
    QCOMPARE(sorted(pendingPart(shuffled, current)), sorted(pendingPart(before, current)));
    QVERIFY(changes > 0);

    // Advancing follows the order the guests were sent.
    for (int step = 1; step <= 3; ++step) {
        QVERIFY(presenter.nextTrack());
        QTRY_COMPARE(presenter.currentQueueIndex(), current + step);
        QVERIFY(player.state().queue.current_occurrence_id.has_value());
        QCOMPARE(QString::number(player.state().queue.current_occurrence_id->value), shuffled[current + step]);
        QCOMPARE(roomQueueIds(player.state().queue), shuffled);
    }
    presenter.setRoomPlayOrder(false);
}

void PlayerPresenterTests::roomHostPlayerDelegatesQueueAndTransportToThePresenter() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    backend.accepted_load_status = PlaybackStatus::Ready;
    PresenterMetadataReader metadata;
    const auto sources = roomSources(3);
    PlayerPresenter presenter(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(presenter.openAudio());
    PresenterRoomHostPlayer player{presenter};
    int changes = 0;
    player.onChanged = [&changes] { ++changes; };

    const auto initial = player.state();
    QCOMPARE(initial.queue, presenter.playlistQueueSnapshot());
    QCOMPARE(initial.queue.items.size(), std::size_t{3});
    QCOMPARE(initial.status, PlaybackStatus::Ready);

    const auto extra = localTestPath(u8"Sala extra.wav");
    const auto added = player.enqueue(extra);
    QVERIFY(added.has_value());
    QVERIFY(changes > 0);
    QCOMPARE(player.state().queue.items.back().occurrence_id, *added);
    QCOMPARE(player.state().queue.items.back().source_path, extra);
    QCOMPARE(player.state().queue.current_occurrence_id, initial.queue.current_occurrence_id);
    QVERIFY(!player.enqueue(std::filesystem::path{}).has_value());

    QVERIFY(player.move(*added, 1));
    QCOMPARE(player.state().queue.items[1].occurrence_id, *added);
    QVERIFY(!player.move(LocalQueueOccurrenceId{999}, 0));
    QVERIFY(player.remove(*added));
    QCOMPARE(player.state().queue.items.size(), std::size_t{3});
    QVERIFY(!player.remove(*added));

    changes = 0;
    QVERIFY(player.next());
    QCOMPARE(player.state().queue.current_occurrence_id, std::optional{initial.queue.items[1].occurrence_id});
    QVERIFY(changes > 0);
    QVERIFY(player.previous());
    QCOMPARE(player.state().queue.current_occurrence_id, std::optional{initial.queue.items[0].occurrence_id});
    // The presenter's own commands reach the room too.
    changes = 0;
    QVERIFY(presenter.nextTrack());
    QVERIFY(changes > 0);

    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 1'000,
                     .duration_ms = 10'000,
                     .seekable = true,
                     .generation = backend.snapshot().generation});
    QTRY_COMPARE(player.state().status, PlaybackStatus::Playing);
    QCOMPARE(player.state().position_ms, 1'000);
    QVERIFY(player.seek(4'000));
    QCOMPARE(backend.seek_positions.back(), 4'000);
    const auto pauses = backend.pause_calls;
    QVERIFY(player.pause());
    QCOMPARE(backend.pause_calls, pauses + 1);
}

void PlayerPresenterTests::roomSessionGateFiltersTheSavedSessionAndHoldsItsPosition() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    backend.accepted_load_status = PlaybackStatus::Ready;
    PresenterMetadataReader metadata;
    PresenterPreferencesClock clock;
    PresenterPreferencesWriter preferences_writer;
    PreferencesAutosave preferences_autosave{preferences_writer, {}, {.now = [&clock] { return clock.now(); }}};
    PresenterSessionWriter session_writer;
    SessionAutosave session_autosave{session_writer, {}, {.now = [&clock] { return clock.now(); }}};
    const auto sources = roomSources(3);
    PlayerPresenter presenter(
        platform, backend, metadata, [sources] { return sources; },
        PlayerPresenterPreferences{
            .autosave = &preferences_autosave,
            .session = {.autosave = &session_autosave, .now = [&clock] { return clock.now(); }}});
    QVERIFY(presenter.openAudio());
    QCOMPARE(session_autosave.flush(), SessionSaveStatus::Saved);
    const SessionSnapshot pre_room = session_writer.flushes.back().snapshot;

    bool hold = true;
    presenter.setRoomSessionGate(
        {.filter = [&pre_room](SessionSnapshot) { return pre_room; }, .hold_position = [&hold] { return hold; }});
    QVERIFY(presenter.appendPlaylistSources({localTestPath(u8"Sala da sala.wav")}));
    backend.publish({.status = PlaybackStatus::Playing,
                     .position_ms = 30'000,
                     .duration_ms = 60'000,
                     .seekable = true,
                     .generation = backend.snapshot().generation});
    QTRY_COMPARE(presenter.confirmedPositionMs(), 30'000);
    QVERIFY(presenter.pause());
    QCOMPARE(session_autosave.flush(), SessionSaveStatus::Saved);
    QCOMPARE(session_writer.flushes.back().snapshot, pre_room);

    presenter.setRoomSessionGate({});
    QVERIFY(presenter.appendPlaylistSources({localTestPath(u8"Depois da sala.wav")}));
    QCOMPARE(session_autosave.flush(), SessionSaveStatus::Saved);
    QCOMPARE(session_writer.flushes.back().snapshot.queue.items.size(), std::size_t{5});
}

void PlayerPresenterTests::roomSessionGateLeavesTheRoomBeforeTheFinalExitSave() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    backend.accepted_load_status = PlaybackStatus::Ready;
    PresenterMetadataReader metadata;
    PresenterPreferencesClock clock;
    PresenterPreferencesWriter preferences_writer;
    PreferencesAutosave preferences_autosave{preferences_writer, {}, {.now = [&clock] { return clock.now(); }}};
    PresenterSessionWriter session_writer;
    SessionAutosave session_autosave{session_writer, {}, {.now = [&clock] { return clock.now(); }}};
    const auto sources = roomSources(3);
    PlayerPresenter presenter(
        platform, backend, metadata, [sources] { return sources; },
        PlayerPresenterPreferences{
            .autosave = &preferences_autosave,
            .session = {.autosave = &session_autosave, .now = [&clock] { return clock.now(); }}});
    QVERIFY(presenter.openAudio());
    const auto guest_item = localTestPath(u8"Sala convidado.wav");
    QVERIFY(presenter.appendPlaylistSources({guest_item}));
    const auto guest_id = presenter.playlistQueueSnapshot().items.back().occurrence_id;
    int leaves = 0;
    presenter.setRoomSessionGate({.before_exit = [&] {
        ++leaves;
        // Like ListeningRoomHost::close(): the guest items leave the player's queue.
        QVERIFY(presenter.removeQueueOccurrence(LocalQueueListModel::occurrenceId(guest_id)));
    }});
    QVERIFY(presenter.requestExit());
    QCOMPARE(leaves, 1);
    QVERIFY(!session_writer.starts.empty());
    const auto saved = session_writer.starts.back().snapshot;
    QCOMPARE(saved.queue.items.size(), std::size_t{3});
    for (const auto& item : saved.queue.items)
        QVERIFY(item.source_path != guest_item);
}

void PlayerPresenterTests::restoredRoomSessionPutsBackTheQueuePaused() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    backend.accepted_load_status = PlaybackStatus::Ready;
    PresenterMetadataReader metadata;
    const auto sources = roomSources(3);
    PlayerPresenter presenter(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(presenter.openAudio());
    QVERIFY(presenter.nextTrack());
    const auto saved = presenter.liveSessionSnapshot();
    QCOMPARE(saved.queue.current_occurrence_id, std::optional{saved.queue.items[1].occurrence_id});

    QVERIFY(presenter.clearQueue());
    QVERIFY(presenter.appendPlaylistSources({localTestPath(u8"Sala outra.wav")}));
    const auto loads = backend.loaded_paths.size();
    QVERIFY(presenter.restoreRoomSession(saved));
    QCOMPARE(presenter.playlistQueueSnapshot(), saved.queue);
    QCOMPARE(presenter.currentOccurrenceId(), LocalQueueListModel::occurrenceId(saved.queue.items[1].occurrence_id));
    QCOMPARE(backend.loaded_paths.size(), loads + 1);
    QCOMPARE(backend.loaded_paths.back(), sources[1]);
    QCOMPARE(presenter.queueModelObject()->rowCount(), 3);
    // Restoring the session it already has changes nothing.
    QVERIFY(presenter.restoreRoomSession(saved));
    QCOMPARE(backend.loaded_paths.size(), loads + 1);
}

// S9.1: a guest playing the room releases the shared player this way; its saved session comes back on leave.
void PlayerPresenterTests::anEmptyRoomSessionPutsTheLocalQueueAway() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    backend.accepted_load_status = PlaybackStatus::Ready;
    PresenterMetadataReader metadata;
    const auto sources = roomSources(2);
    PlayerPresenter presenter(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(presenter.openAudio());
    const auto saved = presenter.liveSessionSnapshot();
    const auto unloads = backend.unload_calls;

    QVERIFY(presenter.restoreRoomSession(SessionSnapshot{}));
    QCOMPARE(presenter.queueModelObject()->rowCount(), 0);
    QCOMPARE(backend.unload_calls, unloads + 1);
    QCOMPARE(presenter.liveSessionSnapshot().queue, SessionSnapshot{}.queue);

    QVERIFY(presenter.restoreRoomSession(saved));
    QCOMPARE(presenter.playlistQueueSnapshot(), saved.queue);
}

// S13.1: a guest following the room shows the room's item and the follower's playback (its own generation
// on the shared player) instead of "Abra um áudio para começar" and 00:00; nothing of it enters the local
// queue or session, and leaving goes back to the local state.
void PlayerPresenterTests::aGuestFollowingTheRoomShowsTheRoomTrackAndItsTime() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    backend.accepted_load_status = PlaybackStatus::Ready;
    PresenterMetadataReader metadata;
    const auto sources = roomSources(2);
    PlayerPresenter presenter(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(presenter.openAudio());
    QTRY_VERIFY(presenter.hasCurrentTrack());
    const auto local_generation = backend.snapshot_.generation;
    const auto saved = presenter.liveSessionSnapshot();
    const auto local_title = presenter.currentTitle();
    QVERIFY(presenter.restoreRoomSession(SessionSnapshot{}));
    QVERIFY(!presenter.hasCurrentTrack());
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Unloaded);

    QSignalSpy state_changes(&presenter, &PlayerPresenter::stateChanged);
    QSignalSpy playback_changes(&presenter, &PlayerPresenter::playbackChanged);
    presenter.showRoomPlayback(RoomTrackSummary{"Primeira", "Artista", "Album", 600'000});
    QVERIFY(!state_changes.isEmpty());
    QVERIFY(presenter.hasCurrentTrack());
    QCOMPARE(presenter.currentTitle(), QStringLiteral("Primeira"));
    QCOMPARE(presenter.currentArtist(), QStringLiteral("Artista"));
    QCOMPARE(presenter.currentAlbum(), QStringLiteral("Album"));
    QCOMPARE(presenter.stateId(), QStringLiteral("playing"));
    // The file is still on its way: loading, not "open an audio file".
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
    // A late event of the local track that was put away is not the room's.
    backend.publishEvent(PlaybackEventType::SnapshotChanged,
                         {PlaybackStatus::Playing, 9'000, 30'000, true, local_generation}, false);
    QCoreApplication::processEvents();
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);

    // The follower loads the room's file and plays it on the shared player, under its own generation.
    const auto follower_generation = backend.snapshot_.generation + 50;
    backend.publish({PlaybackStatus::Playing, 1'000, 600'000, true, follower_generation});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QVERIFY(!playback_changes.isEmpty());
    QVERIFY(presenter.hasPosition());
    QCOMPARE(presenter.positionMs(), qint64{1'000});
    QVERIFY(presenter.hasDuration());
    QCOMPARE(presenter.durationMs(), qint64{600'000});
    // A guest never seeks the shared player locally.
    QVERIFY(!presenter.seekable());
    backend.publish({PlaybackStatus::Playing, 2'250, 600'000, true, follower_generation});
    QTRY_COMPARE(presenter.positionMs(), qint64{2'250});
    backend.publish({PlaybackStatus::Paused, 2'300, 600'000, true, follower_generation});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
    QCOMPARE(presenter.positionMs(), qint64{2'300});

    // The next room item: new title, the follower's next load.
    presenter.showRoomPlayback(RoomTrackSummary{"Segunda", "Outra", "Disco", 200'000});
    QCOMPARE(presenter.currentTitle(), QStringLiteral("Segunda"));
    backend.publish({PlaybackStatus::Playing, 0, 200'000, true, follower_generation + 1});
    QTRY_COMPARE(presenter.durationMs(), qint64{200'000});
    QCOMPARE(presenter.positionMs(), qint64{0});

    // Nothing of the room entered the local queue or the session.
    QCOMPARE(presenter.queueModelObject()->rowCount(), 0);
    QCOMPARE(presenter.liveSessionSnapshot().queue, SessionSnapshot{}.queue);

    // Leaving: the player goes back to the local state, then the saved session comes back.
    backend.publish({PlaybackStatus::Empty, 0, 0, false, follower_generation + 2});
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Loading);
    presenter.showRoomPlayback(std::nullopt);
    QVERIFY(!presenter.hasCurrentTrack());
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Unloaded);
    QCOMPARE(presenter.stateId(), QStringLiteral("empty"));
    QVERIFY(presenter.restoreRoomSession(saved));
    QTRY_VERIFY(presenter.hasCurrentTrack());
    QCOMPARE(presenter.currentTitle(), local_title);
    QCOMPARE(presenter.playlistQueueSnapshot(), saved.queue);
}

// S9.1: while a guest follows the room on the shared player, nothing new enters the local queue (it would
// be replaced when the pre-room session comes back) and nothing local loads over the room.
void PlayerPresenterTests::aGuestFollowingTheRoomAddsNoLocalSources() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    backend.accepted_load_status = PlaybackStatus::Ready;
    PresenterMetadataReader metadata;
    const auto sources = roomSources(2);
    int picks = 0;
    PlayerPresenter presenter(platform, backend, metadata, [&picks, sources] {
        ++picks;
        return sources;
    });
    bool following = true;
    presenter.setRoomSessionGate({.blocks_local_sources = [&following] { return following; }});

    QVERIFY(!presenter.openAudio());
    QVERIFY(!presenter.addAudio());
    QCOMPARE(picks, 0);
    QVERIFY(!presenter.appendPlaylistSources({sources.front()}));
    QVERIFY(!presenter.openSourcePaths({sources.front()}, 0));
    bool committed = true;
    QVERIFY(!presenter.appendPublishedAudio(sources.back(), committed));
    QVERIFY(!committed);
    QCOMPARE(presenter.queueModelObject()->rowCount(), 0);
    QVERIFY(backend.loaded_paths.empty());

    following = false;
    QVERIFY(presenter.openAudio());
    QCOMPARE(picks, 1);
    QCOMPARE(presenter.queueModelObject()->rowCount(), 2);
}

void PlayerPresenterTests::hostRoomSessionExcludesGuestItems() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    backend.accepted_load_status = PlaybackStatus::Ready;
    PresenterMetadataReader metadata;
    PresenterPreferencesClock clock;
    PresenterPreferencesWriter preferences_writer;
    PreferencesAutosave preferences_autosave{preferences_writer, {}, {.now = [&clock] { return clock.now(); }}};
    PresenterSessionWriter session_writer;
    SessionAutosave session_autosave{session_writer, {}, {.now = [&clock] { return clock.now(); }}};
    const auto sources = roomSources(3);
    PlayerPresenter presenter(
        platform, backend, metadata, [sources] { return sources; },
        PlayerPresenterPreferences{
            .autosave = &preferences_autosave,
            .session = {.autosave = &session_autosave, .now = [&clock] { return clock.now(); }}});
    QVERIFY(presenter.openAudio());

    PresenterRoomHostPlayer player{presenter};
    testing::InMemoryRoomRosterStore roster;
    roster.stored.listeners.push_back(KnownListener{"fp-dj", "Dj", 2, {}, false});
    roster.status = RoomRosterLoadStatus::Loaded;
    testing::FakeRoomHostEndpoint endpoint;
    testing::FakeRoomTrackCatalog catalog;
    std::uint8_t next_random = 1;
    auto host = std::make_unique<ListeningRoomHost>(ListeningRoomHostDependencies{
        .roster_store = roster,
        .player = player,
        .endpoint = endpoint,
        .catalog = catalog,
        .host_clock_ms = [] { return std::int64_t{10'000}; },
        .random_bytes =
            [&next_random](std::span<std::uint8_t> bytes) {
                for (auto& byte : bytes)
                    byte = next_random++;
            },
        .host_fingerprint = {0xab},
        .host_profile = ListenerProfile{"Joao", AvatarChoice{}},
        .schedule = [](std::int64_t, std::function<void()>) {},
    });
    presenter.setRoomSessionGate(
        {.filter = [&host](SessionSnapshot live) { return host->withoutGuestItems(std::move(live)); },
         .before_exit = [&host] { host->close(); }});
    host->onChanged = [&presenter](const RoomSnapshot&) { presenter.refreshRoomSession(); };
    QVERIFY(host->open({}).has_value());
    auto [host_side, guest_side] = testing::FakeRoomLink::pair("ab" + std::string(62, '0'), "fp-dj");
    endpoint.admit(host_side);
    QVERIFY(guest_side->sendControl(RoomHello{room_protocol_version, "Dj", AvatarChoice{}}));
    RoomItem upload;
    upload.content_sha256 = std::string(64, 'e');
    const auto guest_file = localTestPath(u8"room-cache/convidado.mp3");
    QVERIFY(host->addGuestTrack("fp-dj", guest_file, upload).has_value());
    QCOMPARE(presenter.playlistQueueSnapshot().items.size(), std::size_t{4});

    QCOMPARE(session_autosave.flush(), SessionSaveStatus::Saved);
    const auto during = session_writer.flushes.back().snapshot;
    QCOMPARE(during.queue.items.size(), std::size_t{3});
    for (const auto& item : during.queue.items)
        QVERIFY(item.source_path != guest_file);

    QVERIFY(presenter.requestExit());
    QVERIFY(!host->isOpen());
    QCOMPARE(presenter.playlistQueueSnapshot().items.size(), std::size_t{3});
    // Nothing guest-specific is left to save: the last write is still the guest-free session.
    const auto& final_save =
        session_writer.starts.empty() ? session_writer.flushes.back().snapshot : session_writer.starts.back().snapshot;
    QCOMPARE(final_save.queue.items.size(), std::size_t{3});
    host.reset();
}

// The room host over the real presenter, whose backend events arrive queued (where 66db347 broke): with a
// guest listening each start is broadcast once, room_play_lead_ms ahead; the host's controls show playing while
// its player waits, the player plays from the anchor on, and no pause/start loop follows.
void PlayerPresenterTests::roomHostLeadsEachStartOnceOverQueuedConfirmations() {
    AvailableTrayPlatform platform;
    DeterministicPlaybackBackend backend;
    PresenterMetadataReader metadata;
    const auto sources = roomSources(3);
    PlayerPresenter presenter(platform, backend, metadata, [sources] { return sources; });
    QVERIFY(presenter.openAudio());
    QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    presenter.setRoomPlayOrder(true);

    PresenterRoomHostPlayer player{presenter};
    testing::InMemoryRoomRosterStore roster;
    roster.stored.listeners.push_back(KnownListener{"fp-dj", "Dj", 2, {}, false});
    roster.status = RoomRosterLoadStatus::Loaded;
    testing::FakeRoomHostEndpoint endpoint;
    testing::FakeRoomTrackCatalog catalog;
    QObject context;
    // The room's timers are real; the host clock moves only when this test moves it.
    std::int64_t now = 10'000;
    std::uint8_t next_random = 1;
    auto host = std::make_unique<ListeningRoomHost>(ListeningRoomHostDependencies{
        .roster_store = roster,
        .player = player,
        .endpoint = endpoint,
        .catalog = catalog,
        .host_clock_ms = [&now] { return now; },
        .random_bytes =
            [&next_random](std::span<std::uint8_t> bytes) {
                for (auto& byte : bytes)
                    byte = next_random++;
            },
        .host_fingerprint = {0xab},
        .host_profile = ListenerProfile{"Joao", AvatarChoice{}},
        .schedule =
            [&context](const std::int64_t delay_ms, std::function<void()> task) {
                QTimer::singleShot(std::chrono::milliseconds{delay_ms}, &context, std::move(task));
            },
    });
    QVERIFY(host->open({}).has_value());
    auto [host_side, guest_side] = testing::FakeRoomLink::pair("ab" + std::string(62, '0'), "fp-dj");
    std::vector<RoomPlayback> states;
    guest_side->onControl = [&states](RoomControlMessage message) {
        if (const auto* state = std::get_if<RoomStateMessage>(&message))
            states.push_back(state->snapshot.playback);
    };
    endpoint.admit(host_side);
    QVERIFY(guest_side->sendControl(RoomHello{room_protocol_version, "Dj", AvatarChoice{}}));
    QTest::qWait(50);
    QVERIFY(!states.empty());
    QVERIFY(states.back().playing);
    const auto items = presenter.playlistQueueSnapshot().items;
    // Some playback: the clock and the player move together.
    const auto play = [&](const int steps) {
        for (int step = 0; step < steps; ++step) {
            now += 200;
            static_cast<void>(backend.tick());
            QTest::qWait(100);
        }
    };
    const auto requireLedStart = [&](const std::function<bool()>& start, const std::size_t index,
                                     const std::int64_t position) {
        const auto before = states.size();
        const auto started_at = now;
        QVERIFY(start());
        // Held before control goes back to the event loop: not one turn of sound before the anchor.
        QVERIFY(backend.snapshot().status != PlaybackStatus::Playing);
        deliverQueued();
        QCOMPARE(states.size(), before + 1);
        const auto led = states.back();
        QVERIFY(led.playing);
        QCOMPARE(led.current_item, std::optional{items[index].occurrence_id.value});
        QCOMPARE(led.anchor_host_ms, started_at + room_play_lead_ms);
        QCOMPARE(led.anchor_position_ms, position);
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
        QVERIFY(backend.snapshot().status != PlaybackStatus::Playing);
        QTRY_COMPARE(backend.snapshot().status, PlaybackStatus::Playing);
        // The backend's confirmation is queued: the clock moves only once the room's player has it, or the
        // room would see the player still at the anchor position after the anchor and re-anchor for drift.
        QTRY_VERIFY(!player.state().starting);
        QCOMPARE(backend.snapshot().position_ms, position);
        now = led.anchor_host_ms;
        play(3);
        QCOMPARE(states.size(), before + 1);
        QCOMPARE(states.back(), led);
        QCOMPARE(backend.snapshot().status, PlaybackStatus::Playing);
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    };
    const auto requirePause = [&](const std::function<bool()>& pause) {
        const auto before = states.size();
        QVERIFY(pause());
        QTRY_COMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
        play(2);
        QCOMPARE(states.size(), before + 1);
        QVERIFY(!states.back().playing);
        QCOMPARE(backend.snapshot().status, PlaybackStatus::Paused);
    };

    requireLedStart([&] { return presenter.nextTrack(); }, 1, 0);
    if (QTest::currentTestFailed())
        return;
    requirePause([&] { return presenter.togglePlayback(); });
    if (QTest::currentTestFailed())
        return;
    requireLedStart([&] { return presenter.togglePlayback(); }, 1, 600);
    if (QTest::currentTestFailed())
        return;
    requireLedStart(
        [&] {
            const bool seeked = presenter.beginSeekPreview(5'000) && presenter.commitSeekPreview();
            static_cast<void>(backend.tick()); // the deterministic backend applies a seek on its next tick
            return seeked;
        },
        1, 5'000);
    if (QTest::currentTestFailed())
        return;
    requirePause(
        [&] { return guest_side->sendControl(RoomIntentMessage{1, RoomIntent{.kind = RoomIntentKind::Pause}}); });
    if (QTest::currentTestFailed())
        return;
    requireLedStart(
        [&] { return guest_side->sendControl(RoomIntentMessage{2, RoomIntent{.kind = RoomIntentKind::Resume}}); }, 1,
        5'600);
    host.reset();
}

namespace {
// A presenter on PresenterPlaybackBackend (events published by hand), playing the first of count room sources.
struct LoadingPresenter final {
    explicit LoadingPresenter(const int count)
        : sources{roomSources(count)}, presenter{platform, backend, metadata, [this] { return sources; }} {}
    // The engine reports status at position_ms (of 200 s) for the loaded track.
    void publish(const PlaybackStatus status, const std::int64_t position_ms = 0) {
        backend.publish({.status = status,
                         .position_ms = position_ms,
                         .duration_ms = 200'000,
                         .seekable = true,
                         .generation = backend.snapshot().generation});
    }
    [[nodiscard]] bool startPlaying() {
        if (!presenter.openAudio())
            return false;
        publish(PlaybackStatus::Ready);
        for (int turn = 0; turn < 50 && backend.play_calls == 0; ++turn)
            QTest::qWait(10);
        publish(PlaybackStatus::Playing);
        deliverQueued();
        return backend.play_calls == 1 && presenter.playbackState() == PlayerPresenter::PlaybackState::Playing;
    }
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    std::vector<std::filesystem::path> sources;
    PlayerPresenter presenter;
};

// A host clock that is real time, so a late start is really late.
[[nodiscard]] std::function<std::int64_t()> realHostClock() {
    auto elapsed = std::make_shared<QElapsedTimer>();
    elapsed->start();
    return [elapsed] { return std::int64_t{10'000} + elapsed->elapsed(); };
}
} // namespace

// As in the app, a track loads asynchronously and autoplays once Ready, and the host clock runs. A room start holds
// that autoplay (the track stays Ready, the controls show playing) and plays it at the anchor. A load slower than
// the lead keeps the anchor until the track plays (its position stands still meanwhile), then re-anchors once,
// where it really started. A pause during the wait leaves the track Ready.
void PlayerPresenterTests::roomHostHoldsALoadingTrackUntilTheAnchor() {
    LoadingPresenter local{4};
    QVERIFY(local.startPlaying());
    auto& presenter = local.presenter;
    auto& backend = local.backend;
    presenter.setRoomPlayOrder(true);
    const auto clock = realHostClock();
    PresenterRoom room{presenter, clock};
    QVERIFY(room.joined);
    auto& states = room.states;
    QVERIFY(states.back().playing);
    const auto items = presenter.playlistQueueSnapshot().items;
    // The engine plays on from anchor: positions that follow the host clock, published every 100 ms.
    const auto playOn = [&](const RoomPlayback& anchor, const int steps) {
        for (int step = 0; step < steps; ++step) {
            QTest::qWait(100);
            local.publish(PlaybackStatus::Playing,
                          anchor.anchor_position_ms + std::max<std::int64_t>(0, clock() - anchor.anchor_host_ms));
        }
        deliverQueued();
    };

    // On time: the next track is Ready before the anchor and starts at it.
    auto before = states.size();
    auto plays = backend.play_calls;
    auto started_at = clock();
    QVERIFY(presenter.nextTrack());
    deliverQueued();
    QCOMPARE(states.size(), before + 1);
    auto led = states.back();
    QVERIFY(led.playing);
    QCOMPARE(led.current_item, std::optional{items[1].occurrence_id.value});
    QVERIFY(led.anchor_host_ms >= started_at + room_play_lead_ms);
    QVERIFY(led.anchor_host_ms <= clock() + room_play_lead_ms);
    QCOMPARE(led.anchor_position_ms, 0);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    local.publish(PlaybackStatus::Ready);
    deliverQueued();
    QCOMPARE(backend.play_calls, plays);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QTRY_COMPARE(backend.play_calls, plays + 1);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    local.publish(PlaybackStatus::Playing, std::max<std::int64_t>(0, clock() - led.anchor_host_ms));
    playOn(led, 3);
    QCOMPARE(states.size(), before + 1);
    QCOMPARE(states.back(), led);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);

    // Slower than the lead: well past the anchor it is still loading. A notification meanwhile (its duration) keeps
    // the anchor; when it plays, the room re-anchors once, now, at the position it starts from.
    before = states.size();
    plays = backend.play_calls;
    QVERIFY(presenter.nextTrack());
    deliverQueued();
    QCOMPARE(states.size(), before + 1);
    led = states.back();
    QCOMPARE(led.current_item, std::optional{items[2].occurrence_id.value});
    QCOMPARE(led.anchor_position_ms, 0);
    QTest::qWait(room_play_lead_ms + room_anchor_tolerance_ms + 100);
    QCOMPARE(backend.play_calls, plays);
    backend.publish(
        {.status = PlaybackStatus::Loading, .duration_ms = 200'000, .generation = backend.snapshot().generation});
    deliverQueued();
    QCOMPARE(states.size(), before + 1);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    local.publish(PlaybackStatus::Ready);
    QTRY_COMPARE(backend.play_calls, plays + 1);
    QCOMPARE(states.size(), before + 1);
    const auto played_at = clock();
    local.publish(PlaybackStatus::Playing);
    deliverQueued();
    QCOMPARE(states.size(), before + 2);
    const auto late = states.back();
    QVERIFY(late.playing);
    QCOMPARE(late.current_item, led.current_item);
    QCOMPARE(late.anchor_position_ms, 0);
    QVERIFY(late.anchor_host_ms >= played_at);
    QVERIFY(late.anchor_host_ms <= clock());
    playOn(late, 3);
    QCOMPARE(states.size(), before + 2);
    QCOMPARE(states.back(), late);

    // A pause during the wait: one paused state, and the track stays Ready past the anchor.
    before = states.size();
    plays = backend.play_calls;
    QVERIFY(presenter.nextTrack());
    deliverQueued();
    QCOMPARE(states.size(), before + 1);
    QVERIFY(presenter.togglePlayback());
    deliverQueued();
    QCOMPARE(states.size(), before + 2);
    QVERIFY(!states.back().playing);
    QCOMPARE(states.back().current_item, std::optional{items[3].occurrence_id.value});
    local.publish(PlaybackStatus::Ready);
    QTest::qWait(room_play_lead_ms + 100);
    QCOMPARE(backend.play_calls, plays);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Ready);
    QCOMPARE(states.size(), before + 2);
}

// A resume from the host's own controls (play button, tray, media keys, a double click on the paused current item):
// the engine is not told to play before the anchor, not even for one turn of the event loop.
void PlayerPresenterTests::roomHostHoldsAResumeFromItsOwnControlsUntilTheAnchor() {
    LoadingPresenter local{3};
    QVERIFY(local.startPlaying());
    auto& presenter = local.presenter;
    auto& backend = local.backend;
    presenter.setRoomPlayOrder(true);
    const auto clock = realHostClock();
    PresenterRoom room{presenter, clock};
    QVERIFY(room.joined);
    auto& states = room.states;
    const auto items = presenter.playlistQueueSnapshot().items;
    const auto current = LocalQueueListModel::occurrenceId(items[0].occurrence_id);
    std::int64_t position = 40'000;

    const std::vector<std::function<bool()>> resumes{
        [&] { return presenter.play(); },
        [&] { return presenter.togglePlayback(); },
        [&] { return presenter.playQueueOccurrence(current); },
    };
    for (const auto& resume : resumes) {
        // Paused at position.
        QVERIFY(presenter.pause());
        local.publish(PlaybackStatus::Paused, position);
        deliverQueued();
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
        QVERIFY(!states.back().playing);

        const auto before = states.size();
        const auto plays = backend.play_calls;
        const auto started_at = clock();
        QVERIFY(resume());
        QCOMPARE(backend.play_calls, plays);
        deliverQueued();
        QCOMPARE(backend.play_calls, plays);
        QCOMPARE(states.size(), before + 1);
        const auto led = states.back();
        QVERIFY(led.playing);
        QCOMPARE(led.current_item, std::optional{items[0].occurrence_id.value});
        QVERIFY(led.anchor_host_ms >= started_at + room_play_lead_ms);
        QVERIFY(led.anchor_host_ms <= clock() + room_play_lead_ms);
        QCOMPARE(led.anchor_position_ms, position);
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
        QTRY_COMPARE(backend.play_calls, plays + 1);
        local.publish(PlaybackStatus::Playing, position + std::max<std::int64_t>(0, clock() - led.anchor_host_ms));
        QTest::qWait(100);
        local.publish(PlaybackStatus::Playing, position + std::max<std::int64_t>(0, clock() - led.anchor_host_ms));
        deliverQueued();
        QCOMPARE(states.size(), before + 1);
        QCOMPARE(states.back(), led);
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
        position += 10'000;
    }
}

// Removing the current item while its start waits for the anchor, from the host's queue or by a guest: the next
// item starts in its place, led again, instead of stopping the room.
void PlayerPresenterTests::roomHostLeadsTheSuccessorOfACurrentItemRemovedDuringTheWait() {
    LoadingPresenter local{5};
    QVERIFY(local.startPlaying());
    auto& presenter = local.presenter;
    auto& backend = local.backend;
    presenter.setRoomPlayOrder(true);
    const auto clock = realHostClock();
    PresenterRoom room{presenter, clock};
    QVERIFY(room.joined);
    auto& states = room.states;
    const auto items = presenter.playlistQueueSnapshot().items;

    const auto requireSuccessorLed = [&](const std::function<bool(LocalQueueOccurrenceId)>& remove,
                                         const std::size_t removed, const std::size_t successor) {
        const auto before = states.size();
        const auto plays = backend.play_calls;
        QVERIFY(presenter.nextTrack());
        deliverQueued();
        QCOMPARE(states.size(), before + 1);
        QCOMPARE(states.back().current_item, std::optional{items[removed].occurrence_id.value});
        const auto removed_at = clock();
        QVERIFY(remove(items[removed].occurrence_id));
        deliverQueued();
        QCOMPARE(states.size(), before + 2);
        const auto led = states.back();
        QVERIFY(led.playing);
        QCOMPARE(led.current_item, std::optional{items[successor].occurrence_id.value});
        QVERIFY(led.anchor_host_ms >= removed_at + room_play_lead_ms);
        QVERIFY(led.anchor_host_ms <= clock() + room_play_lead_ms);
        QCOMPARE(led.anchor_position_ms, 0);
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
        local.publish(PlaybackStatus::Ready);
        deliverQueued();
        QCOMPARE(backend.play_calls, plays);
        QTRY_COMPARE(backend.play_calls, plays + 1);
        local.publish(PlaybackStatus::Playing, std::max<std::int64_t>(0, clock() - led.anchor_host_ms));
        deliverQueued();
        QCOMPARE(states.size(), before + 2);
        QCOMPARE(states.back(), led);
    };
    requireSuccessorLed(
        [&](const LocalQueueOccurrenceId id) {
            return presenter.removeQueueOccurrence(LocalQueueListModel::occurrenceId(id));
        },
        1, 2);
    if (QTest::currentTestFailed())
        return;
    requireSuccessorLed(
        [&](const LocalQueueOccurrenceId id) {
            return room.intent(RoomIntent{.kind = RoomIntentKind::RemoveItem, .item_id = id.value});
        },
        3, 4);
}

// The room released its start and the engine has not confirmed it yet: a pause (button, toggle) shows paused at
// once, to the controls, tray, media keys and the room, instead of waiting for the engine. A start released while
// its track still loads does not start when it is Ready.
void PlayerPresenterTests::pausingAReleasedRoomStartShowsPausedAtOnce() {
    LoadingPresenter local{3};
    QVERIFY(local.startPlaying());
    auto& presenter = local.presenter;
    auto& backend = local.backend;

    for (const bool toggle : {false, true}) {
        QVERIFY(presenter.holdRoomStart());
        local.publish(PlaybackStatus::Paused, 5'000);
        deliverQueued();
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
        const auto plays = backend.play_calls;
        QVERIFY(presenter.releaseRoomStart());
        QCOMPARE(backend.play_calls, plays + 1);
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
        QSignalSpy changes(&presenter, &PlayerPresenter::playbackChanged);
        QVERIFY(toggle ? presenter.togglePlayback() : presenter.pause());
        QVERIFY(!changes.isEmpty());
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
        QVERIFY(presenter.roomHostState().status != PlaybackStatus::Playing);
        // The engine confirms the play, then the pause that followed it.
        local.publish(PlaybackStatus::Playing, 5'000);
        deliverQueued();
        local.publish(PlaybackStatus::Paused, 5'000);
        deliverQueued();
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Paused);
        // Playing again for the next round.
        QVERIFY(presenter.play());
        local.publish(PlaybackStatus::Playing, 5'000);
        deliverQueued();
        QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    }

    // The next track loads to play; the room holds it and releases it before it is Ready; then the pause.
    QVERIFY(presenter.nextTrack());
    QVERIFY(presenter.holdRoomStart());
    QVERIFY(presenter.releaseRoomStart());
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    const auto plays = backend.play_calls;
    QSignalSpy changes(&presenter, &PlayerPresenter::playbackChanged);
    QVERIFY(presenter.pause());
    QVERIFY(!changes.isEmpty());
    QVERIFY(presenter.playbackState() != PlayerPresenter::PlaybackState::Playing);
    QVERIFY(presenter.roomHostState().status != PlaybackStatus::Playing);
    local.publish(PlaybackStatus::Ready);
    deliverQueued();
    QCOMPARE(backend.play_calls, plays);
    QCOMPARE(presenter.playbackState(), PlayerPresenter::PlaybackState::Ready);
}

void PlayerPresenterTests::guestRoomSessionKeepsThePreRoomSession() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    backend.accepted_load_status = PlaybackStatus::Ready;
    PresenterMetadataReader metadata;
    PresenterPreferencesClock clock;
    PresenterPreferencesWriter preferences_writer;
    PreferencesAutosave preferences_autosave{preferences_writer, {}, {.now = [&clock] { return clock.now(); }}};
    PresenterSessionWriter session_writer;
    SessionAutosave session_autosave{session_writer, {}, {.now = [&clock] { return clock.now(); }}};
    const auto sources = roomSources(3);
    PlayerPresenter presenter(
        platform, backend, metadata, [sources] { return sources; },
        PlayerPresenterPreferences{
            .autosave = &preferences_autosave,
            .session = {.autosave = &session_autosave, .now = [&clock] { return clock.now(); }}});
    QVERIFY(presenter.openAudio());
    QVERIFY(presenter.nextTrack());
    QCOMPARE(session_autosave.flush(), SessionSaveStatus::Saved);
    const auto pre_room = session_writer.flushes.back().snapshot;

    testing::FakeRoomGuestConnector connector;
    auto guest = std::make_unique<ListeningRoomGuest>(ListeningRoomGuestDependencies{
        .connector = connector,
        .profile = ListenerProfile{"Bia", AvatarChoice{}},
        .stored_custom_avatar_sha256 = [] { return std::optional<std::string>{}; },
        .capture_session = [&presenter] { return presenter.liveSessionSnapshot(); },
        .restore_session =
            [&presenter](const SessionSnapshot& saved) { static_cast<void>(presenter.restoreRoomSession(saved)); },
        .schedule = [](std::int64_t, std::function<void()>) {},
    });
    presenter.setRoomSessionGate(
        {.filter = [&guest](SessionSnapshot live) { return guest->sessionForAutosave(std::move(live)); },
         .hold_position = [&guest] { return guest->holdsLocalSession(); },
         .before_exit = [&guest] { guest->leave(); }});
    guest->join(RoomInviteLink{.host_fingerprint = {0xab}});
    auto [host_side, guest_side] = testing::FakeRoomLink::pair("fp-bia", "ab" + std::string(62, '0'));
    connector.complete(guest_side);
    QVERIFY(host_side->sendControl(RoomJoinResult{JoinDecision::Admitted}));
    QCOMPARE(guest->state(), GuestRoomState::Joined);
    QVERIFY(guest->holdsLocalSession());

    // Room playback changes the live queue and position; the saved session stays the pre-room one.
    QVERIFY(presenter.clearQueue());
    QVERIFY(presenter.appendPlaylistSources({localTestPath(u8"Sala tocando.wav")}));
    QCOMPARE(session_autosave.flush(), SessionSaveStatus::Saved);
    QCOMPARE(session_writer.flushes.back().snapshot, pre_room);

    QVERIFY(presenter.requestExit());
    QVERIFY(!guest->holdsLocalSession());
    QCOMPARE(guest->state(), GuestRoomState::Idle);
    // Leaving put the pre-room queue back before the final save.
    QCOMPARE(presenter.playlistQueueSnapshot(), pre_room.queue);
    const auto& final_save =
        session_writer.starts.empty() ? session_writer.flushes.back().snapshot : session_writer.starts.back().snapshot;
    QCOMPARE(final_save.queue, pre_room.queue);
    guest.reset();
}

void PlayerPresenterTests::roomPrivacyNoticeIsMarkedOnceAndPersisted() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterPreferencesClock clock;
    PresenterPreferencesWriter preferences_writer;
    PreferencesAutosave preferences_autosave{preferences_writer, {}, {.now = [&clock] { return clock.now(); }}};
    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
        PlayerPresenterPreferences{.autosave = &preferences_autosave});
    QVERIFY(!presenter.roomPrivacyNoticeShown());
    QVERIFY(presenter.markRoomPrivacyNoticeShown());
    QVERIFY(presenter.roomPrivacyNoticeShown());
    QVERIFY(!presenter.markRoomPrivacyNoticeShown());
    QVERIFY(preferences_autosave.dirty());
    QCOMPARE(preferences_autosave.flush(), PreferencesSaveStatus::Saved);
    QVERIFY(preferences_writer.flushes.back().preferences.room_privacy_notice_shown);
}

void PlayerPresenterTests::roomOutputOffsetIsBoundedAndPersisted() {
    AvailableTrayPlatform platform;
    PresenterPlaybackBackend backend;
    PresenterMetadataReader metadata;
    PresenterPreferencesClock clock;
    PresenterPreferencesWriter preferences_writer;
    PreferencesAutosave preferences_autosave{preferences_writer, {}, {.now = [&clock] { return clock.now(); }}};
    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
        PlayerPresenterPreferences{.autosave = &preferences_autosave});
    QCOMPARE(presenter.roomOutputOffsetMs(), 0);
    QCOMPARE(presenter.property("roomOutputOffsetMinimumMs").toInt(), -500);
    QCOMPARE(presenter.property("roomOutputOffsetMaximumMs").toInt(), 500);
    QSignalSpy changed(&presenter, &PlayerPresenter::preferencesChanged);
    QVERIFY(presenter.setRoomOutputOffsetMs(120));
    QCOMPARE(presenter.roomOutputOffsetMs(), 120);
    QCOMPARE(changed.count(), 1);
    QVERIFY(!presenter.setRoomOutputOffsetMs(120));
    QVERIFY(!presenter.setRoomOutputOffsetMs(501));
    QVERIFY(!presenter.setRoomOutputOffsetMs(-501));
    QCOMPARE(presenter.roomOutputOffsetMs(), 120);
    QVERIFY(presenter.setRoomOutputOffsetMs(-500));
    QVERIFY(preferences_autosave.dirty());
    QCOMPARE(preferences_autosave.flush(), PreferencesSaveStatus::Saved);
    QCOMPARE(preferences_writer.flushes.back().preferences.room_output_offset_ms, -500);
}

} // namespace music_player::m0::test

QTEST_MAIN(music_player::m0::test::PlayerPresenterTests)

#include "PlayerPresenterTests.moc"
