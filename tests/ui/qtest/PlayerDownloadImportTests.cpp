#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "m0/application/DownloadAdmissionController.hpp"
#include "m0/application/DownloadService.hpp"
#ifdef Q_OS_WIN
#include "adapters/QtJsonWindowsSessionStore.hpp"
#include "m0/adapters/SingleWorkerSessionWriter.hpp"
#endif
#include "presentation/PlayerDownloadImport.hpp"
#include "presentation/PlayerPresenter.hpp"
#include "presentation/PlayerPresenterTestAccess.hpp"

#include <QAbstractItemModelTester>
#include <QCoreApplication>
#include <QEvent>
#include <QMetaMethod>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <deque>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace music_player::m0::test {
namespace {

// These paths are lexical, trusted Published-result stand-ins. Tests do not
// inspect, create or decode them; publication/content approval belongs upstream.
std::filesystem::path publishedPath(const std::u8string& name = u8"Final 日本語.mp3") {
#ifdef _WIN32
    return std::filesystem::path{u8"C:/rc-music-import-synthetic"} / std::filesystem::path{name};
#else
    return std::filesystem::path{u8"/rc-music-import-synthetic"} / std::filesystem::path{name};
#endif
}

class ImportPlatform final : public PlatformIntegration {
  public:
    bool trayAvailable() const noexcept override {
        ++tray_reads;
        return true;
    }
    void hideWindow() override {}
    void minimizeWindow() override {}
    void showAndActivateWindow() override {}
    void requestExit() override { ++exits; }
    int exits{0};
    mutable int tray_reads{0};
};

class ImportBackend final : public PlaybackBackend {
  public:
    const PlaybackSnapshot& snapshot() const noexcept override { return state; }
    void setEventCallback(PlaybackEventCallback callback) override { events = std::move(callback); }
    PlaybackCommandResult load(const std::filesystem::path& path) override {
        loads.push_back(path);
        state = {.status = PlaybackStatus::Loading, .generation = ++generation};
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult play() override {
        ++plays;
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult pause() override {
        ++pauses;
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult seekTo(std::int64_t) override {
        ++seeks;
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult setVolume(double) override {
        ++volumes;
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult setMuted(bool) override {
        ++mutes;
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult unload() override {
        ++unloads;
        state = {.generation = ++generation};
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult shutdown() override {
        ++shutdowns;
        return PlaybackCommandResult::Accepted;
    }
    void publish(const PlaybackStatus status) {
        state = {.status = status,
                 .position_ms = 321,
                 .duration_ms = 2'000,
                 .seekable = status != PlaybackStatus::Loading,
                 .generation = generation,
                 .error =
                     status == PlaybackStatus::Error ? PlaybackErrorCode::SourceUnavailable : PlaybackErrorCode::None};
        if (events)
            events({PlaybackEventType::SnapshotChanged, state});
    }
    void publishEnded() {
        state.position_ms = state.duration_ms;
        if (events)
            events({PlaybackEventType::Ended, state});
    }
    std::vector<int> commands() const { return {plays, pauses, seeks, volumes, mutes, unloads, shutdowns}; }
    PlaybackSnapshot state{};
    PlaybackEventCallback events{};
    std::vector<std::filesystem::path> loads{};
    std::uint64_t generation{0};
    int plays{0};
    int pauses{0};
    int seeks{0};
    int volumes{0};
    int mutes{0};
    int unloads{0};
    int shutdowns{0};
};

class ImportMetadata final : public MetadataReader {
  public:
    MetadataCommandResult startRead(MetadataReadRequest request) override {
        requests.push_back(std::move(request));
        return MetadataCommandResult::Accepted;
    }
    MetadataCommandResult cancelRead(MetadataGeneration) override {
        ++cancels;
        return MetadataCommandResult::Accepted;
    }
    MetadataPollResult poll(MetadataGeneration) override {
        ++polls;
        if (completions.empty())
            return {.status = MetadataPollStatus::Pending};
        ++consumed;
        auto result = std::move(completions.front());
        completions.pop_front();
        return result;
    }
    std::optional<MetadataReadCompletion> tryTakeCompletion() override {
        ++takes;
        return std::nullopt;
    }
    MetadataCommandResult shutdown() override { return MetadataCommandResult::Accepted; }
    std::vector<MetadataReadRequest> requests{};
    std::deque<MetadataPollResult> completions{};
    int cancels{0};
    int consumed{0};
    int polls{0};
    int takes{0};
};

class ImportDiscovery final : public LocalSourceDiscovery {
  public:
    DiscoveryCommandResult start(LocalSourceDiscoveryRequest request) override {
        starts.push_back(std::move(request));
        return DiscoveryCommandResult::Accepted;
    }
    DiscoveryCommandResult cancel(DiscoveryGeneration) override {
        results.push_back({.status = DiscoveryPollStatus::Cancelled});
        return DiscoveryCommandResult::Accepted;
    }
    DiscoveryPollResult poll(DiscoveryGeneration) override {
        if (results.empty())
            return {.status = DiscoveryPollStatus::Pending};
        auto result = std::move(results.front());
        results.pop_front();
        return result;
    }
    DiscoveryCommandResult shutdown() override { return DiscoveryCommandResult::Accepted; }
    std::vector<LocalSourceDiscoveryRequest> starts{};
    std::deque<DiscoveryPollResult> results{};
};

class ImportPicker final : public AudioFilesPicker {
  public:
    bool pick(AudioFilesSelectionCompletion completion) override {
        ++requests;
        pending = std::move(completion);
        return true;
    }
    void cancel() noexcept override { pending = {}; }
    AudioFilesSelectionCompletion pending{};
    int requests{0};
};

class ImportLocator final : public AudioFileLocator {
  public:
    bool locate(const std::filesystem::path&, AudioFileLocationCompletion completion) override {
        ++requests;
        pending = std::move(completion);
        return true;
    }
    void cancel() noexcept override { pending = {}; }
    AudioFileLocationCompletion pending{};
    int requests{0};
};

class ImportSessionWriter final : public SessionWriter {
  public:
    SessionWriterCommand start(SessionWriteRequest) override { return SessionWriterCommand::Accepted; }
    std::optional<SessionWriteCompletion> poll() override { return std::nullopt; }
    std::optional<SessionWriteCompletion> flush(SessionWriteRequest request) override {
        return SessionWriteCompletion{.revision = request.revision, .status = SessionSaveStatus::Saved};
    }
    bool shutdown() override { return true; }
};

class ImportPreferencesWriter final : public PreferencesWriter {
  public:
    PreferencesWriterCommand start(PreferencesWriteRequest) override { return PreferencesWriterCommand::Accepted; }
    std::optional<PreferencesWriteCompletion> poll() override { return std::nullopt; }
    std::optional<PreferencesWriteCompletion> flush(PreferencesWriteRequest request) override {
        return PreferencesWriteCompletion{.revision = request.revision, .status = PreferencesSaveStatus::Saved};
    }
    bool shutdown() override { return true; }
};

class DurableImportWriter final : public SessionWriter {
  public:
    SessionWriterCommand start(SessionWriteRequest request) override {
        ++attempts;
        if (active)
            return SessionWriterCommand::Busy;
        starts.push_back(request);
        active = std::move(request);
        if (on_start)
            on_start();
        return SessionWriterCommand::Accepted;
    }
    std::optional<SessionWriteCompletion> poll() override {
        if (completions.empty())
            return std::nullopt;
        const auto result = completions.front();
        completions.pop_front();
        return result;
    }
    std::optional<SessionWriteCompletion> flush(SessionWriteRequest) override { return std::nullopt; }
    bool shutdown() override { return !active; }
    void complete(SessionSaveStatus status, bool recoverable = false) {
        Q_ASSERT(active);
        if (status == SessionSaveStatus::Saved || recoverable)
            disk = active->snapshot;
        completions.push_back({.revision = active->revision,
                               .status = status,
                               .requested_snapshot_recoverable_after_failure = recoverable});
        active.reset();
    }
    std::optional<SessionWriteRequest> active;
    std::deque<SessionWriteCompletion> completions;
    std::vector<SessionWriteRequest> starts;
    SessionSnapshot disk{};
    std::function<void()> on_start{};
    int attempts{0};
};

struct DurableHarness final {
    ImportPlatform platform;
    ImportBackend backend;
    ImportMetadata metadata;
    DurableImportWriter writer;
    ImportPreferencesWriter preferences_writer;
    SessionAutosave::TimePoint clock{};
    PreferencesAutosave preferences_autosave;
    SessionAutosave autosave;
    PlayerPresenter presenter;
    explicit DurableHarness(SessionSnapshot initial = {}, SessionLoadStatus status = SessionLoadStatus::Missing,
                            bool restore = false)
        : preferences_autosave(preferences_writer, {.restore_last_session = restore},
                               {.now = [this] { return clock; }}),
          autosave(writer, initial, {.now = [this] { return clock; }}),
          presenter(
              platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
              PlayerPresenterPreferences{.initial = {.restore_last_session = restore},
                                         .autosave = &preferences_autosave,
                                         .now = [this] { return clock; },
                                         .session = {.initial = std::move(initial),
                                                     .load_status = status,
                                                     .autosave = &autosave,
                                                     .now = [this] { return clock; }}}) {}
    QAbstractItemModel* model() { return presenter.queueModelObject(); }
    QString id(int row) {
        return model()->data(model()->index(row, 0), LocalQueueListModel::OccurrenceIdRole).toString();
    }
    void poll() { PlayerPresenterTestAccess::pollPersistence(presenter); }
};

struct Harness final {
    ImportPlatform platform;
    ImportBackend backend;
    ImportMetadata metadata;
    ImportDiscovery discovery;
    int picker_calls{0};
    std::vector<std::filesystem::path> selected{};
    PlayerPresenter presenter;
    explicit Harness()
        : presenter(
              platform, backend, metadata,
              [this] {
                  ++picker_calls;
                  return selected;
              },
              discovery) {}
    QAbstractItemModel* model() { return presenter.queueModelObject(); }
    QString id(const int row) {
        return model()->data(model()->index(row, 0), LocalQueueListModel::OccurrenceIdRole).toString();
    }
    QStringList ids() {
        QStringList result;
        for (int row = 0; row < model()->rowCount(); ++row)
            result.push_back(id(row));
        return result;
    }
    bool seed(const int count = 1) {
        selected = {publishedPath(u8"Atual.mp3")};
        if (count > 1)
            selected.push_back(publishedPath(u8"Seguinte.mp3"));
        return presenter.openAudio();
    }
};

class ImportExecution final : public DownloadExecution {
  public:
    bool start(const std::string& id, const DownloadItem&) override {
        starts.push_back(id);
        update = {.job_id = id};
        return true;
    }
    void cancel(std::string_view id) override { cancellations.emplace_back(id); }
    DownloadExecutionUpdate poll(std::string_view) override { return update; }
    void publish(const std::string& id, const std::filesystem::path& path) {
        update = {.job_id = id,
                  .state = DownloadExecutionState::Published,
                  .phase = DownloadPhase::Publishing,
                  .completed_units = 1,
                  .total_units = 1,
                  .published_mp3 = path};
    }
    std::vector<std::string> starts{};
    std::vector<std::string> cancellations{};
    DownloadExecutionUpdate update{};
};

DownloadRequest request() {
    return {.request_id = "abcdefab-cdef-4abc-8def-000000000001",
            .items = {{.url = "https://media.example.test/audio.mp3",
                       .title = "Published synthetic track",
                       .kind = DownloadMediaKind::Audio}}};
}

#ifdef Q_OS_WIN
class RecoveredImportObserver final : public DownloadImport {
  public:
    explicit RecoveredImportObserver(DownloadImport& importer) : importer_(importer) {}
    bool accept(const std::string& id, const std::filesystem::path& path) override {
        ++calls;
        return importer_.accept(id, path);
    }
    DownloadImportState begin(const std::string& id, const std::filesystem::path& path) override {
        ++calls;
        return importer_.begin(id, path);
    }
    DownloadImportState pollImport(std::string_view id) override {
        ++calls;
        return importer_.pollImport(id);
    }
    int calls{0};

  private:
    DownloadImport& importer_;
};

class RecoveredImportIdentity final : public DownloadRequestIdentity {
  public:
    std::string fingerprint(const DownloadRequest&) override { return std::string(64, 'a'); }
};

// Only the session store/writer are real in this integration. The download
// checkpoint stands in for the older admission document left by a crash.
class RecoveredImportCheckpointIo final : public DownloadCheckpointIo {
  public:
    DownloadCheckpointIoCommand startLoad(std::uint64_t id) override {
        if (closed)
            return DownloadCheckpointIoCommand::Closed;
        if (completion)
            return DownloadCheckpointIoCommand::Busy;
        completion = DownloadCheckpointIoResult{
            .operation_id = id, .load = {.checkpoint = checkpoint, .status = DownloadCheckpointLoadStatus::Loaded}};
        return DownloadCheckpointIoCommand::Accepted;
    }
    DownloadCheckpointIoCommand startSave(std::uint64_t id, const DownloadCheckpoint& value) override {
        if (closed)
            return DownloadCheckpointIoCommand::Closed;
        if (completion)
            return DownloadCheckpointIoCommand::Busy;
        saves.push_back(value);
        completion = DownloadCheckpointIoResult{.operation_id = id,
                                                .operation = DownloadCheckpointOperation::Save,
                                                .save_status = DownloadCheckpointSaveStatus::Saved};
        return DownloadCheckpointIoCommand::Accepted;
    }
    std::optional<DownloadCheckpointIoResult> poll(std::uint64_t id) override {
        if (!completion || completion->operation_id != id)
            return std::nullopt;
        return std::exchange(completion, std::nullopt);
    }
    void shutdown() override { closed = true; }
    bool stopped() const override { return closed && !completion; }
    DownloadCheckpoint checkpoint{};
    std::vector<DownloadCheckpoint> saves{};
    std::optional<DownloadCheckpointIoResult> completion{};
    bool closed{false};
};
#endif

std::size_t receiptBytes(const std::string& id, const std::filesystem::path& path) {
    return id.size() + path.native().size() * sizeof(std::filesystem::path::value_type);
}

PlayerPresenter::PlaybackState presenterState(const PlaybackStatus status) {
    switch (status) {
    case PlaybackStatus::Empty:
        return PlayerPresenter::PlaybackState::Unloaded;
    case PlaybackStatus::Loading:
        return PlayerPresenter::PlaybackState::Loading;
    case PlaybackStatus::Ready:
        return PlayerPresenter::PlaybackState::Ready;
    case PlaybackStatus::Paused:
        return PlayerPresenter::PlaybackState::Paused;
    case PlaybackStatus::Playing:
        return PlayerPresenter::PlaybackState::Playing;
    case PlaybackStatus::Error:
        return PlayerPresenter::PlaybackState::Error;
    }
    return PlayerPresenter::PlaybackState::Unloaded;
}

} // namespace

class PlayerDownloadImportTests final : public QObject {
    Q_OBJECT
  private slots:
    void emptyQueueAppendsWithoutLoadingAndExplicitSelectionUsesLocalPlayback();
    void existingPlaybackIsPreserved_data();
    void existingPlaybackIsPreserved();
    void replayAndConflictingPathsNeverCreateAnotherOccurrence();
    void distinctJobsKeepDuplicatePathsAsDistinctOccurrences();
    void invalidIdentifiers_data();
    void invalidIdentifiers();
    void invalidPaths_data();
    void invalidPaths();
    void validIdentifierAndPathBoundaries();
    void receiptCapacityIsInclusiveAndDoesNotEvict();
    void retainedBytesAreInclusive_data();
    void retainedBytesAreInclusive();
    void zeroLimitsRefuseAdmission_data();
    void zeroLimitsRefuseAdmission();
    void busyDiscoveryAllowsExplicitRetryWithoutSideEffects();
    void refusedHandoffReleasesReceiptAndByteCapacity();
    void postCommitClockExceptionRetainsReceiptAndReleasesCallGuard();
    void fileSelectionAndLocationRefuseImport();
    void committedReplayIsSafeDuringNotifications_data();
    void committedReplayIsSafeDuringNotifications();
    void modelCommitRunsBeforeNotificationAndNeverForInvalidInput();
    void nestedPlaybackEndWaitsForAppendThenAdvancesExactlyOnce();
    void nestedMetadataPollingWaitsForAppendThenConsumesExactlyOnce();
    void nestedPowerEventsWaitForAppendAndPreserveFinalState_data();
    void nestedPowerEventsWaitForAppendAndPreserveFinalState();
    void realServiceCompletesOnlyAfterAppendAndReplayDoesNotImport();
    void realServiceRetriesImportWithoutExecutingAgain();
    void realServiceDoesNotImportPublicationAfterCancellation();
    void destroyedExitingAndFrozenPresentersRefuseAdmission();
    void foreignThreadCannotReadOrMutatePresenter();
    void privilegedAppendIsNotInvokableFromQml();
    void durableImportWaitsForMatchingSaveOfQueueAndReceipt();
    void durableSavedOlderRevisionAcknowledgesWithoutWaitingForPositionChanges();
    void durableFailureDoesNotPromoteReadbackAndExplicitRetryDoesNotAppend();
    void retiredReceiptReleasesCapacityOnlyAfterSavedWithoutRewritingAnUnrestoredQueue();
    void overlappingReceiptRetirementsKeepPendingImportsAndNewQueueEdits();
    void durableReplaysSurviveRemovalAndRestartWithRestoreDisabled();
    void durableReceiptsStayInStructuralSnapshotsAfterClear();
    void durableUntrustedStartupBlocksNewImports_data();
    void durableUntrustedStartupBlocksNewImports();
    void durableNeedsPersistenceAndHonorsReceiptLimits();
    void durableRecoveryCapabilityRequiresHealthySessionAndSameThread();
    void durableRecoveryCapabilityRejectsUntrustedStartup_data();
    void durableRecoveryCapabilityRejectsUntrustedStartup();
    void durableReentrantReplayCannotAcknowledgeBeforeSave();
    void durableRealServiceWaitsForPersistenceAndDrainsDuringShutdown();
    void durableRestartReplaysDoNotReadOrLoadPublishedFiles();
    void durableWriterReentryCannotStageAnotherWrite_data();
    void durableWriterReentryCannotStageAnotherWrite();
    void durableWriterReentryCannotBypassTheGuardThroughVolatileOrDirectAppend();
#ifdef Q_OS_WIN
    void durableRealStoreKeepsQueueAndReceiptsTogether_data();
    void durableRealStoreKeepsQueueAndReceiptsTogether();
#endif
};

void PlayerDownloadImportTests::emptyQueueAppendsWithoutLoadingAndExplicitSelectionUsesLocalPlayback() {
    Harness h;
    PlayerDownloadImport importer(h.presenter);
    QAbstractItemModelTester tester(h.model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy inserted(h.model(), &QAbstractItemModel::rowsInserted);
    QSignalSpy reset(h.model(), &QAbstractItemModel::modelReset);
    const auto commands = h.backend.commands();
    const auto path = publishedPath();
    QVERIFY(importer.accept("job-1", path));
    QCOMPARE(h.model()->rowCount(), 1);
    QCOMPARE(inserted.size(), 1);
    QCOMPARE(reset.size(), 0);
    QVERIFY(h.presenter.queueLoaded());
    QVERIFY(!h.presenter.hasCurrentOccurrence());
    QCOMPARE(h.presenter.currentQueueIndex(), -1);
    QCOMPARE(h.presenter.playbackState(), PlayerPresenter::PlaybackState::Unloaded);
    QCOMPARE(h.model()->data(h.model()->index(0, 0), LocalQueueListModel::FileNameRole).toString(),
             QString::fromStdWString(path.filename().wstring()));
    QVERIFY(h.backend.loads.empty());
    QCOMPARE(h.backend.commands(), commands);
    QVERIFY(h.metadata.requests.empty());
    QVERIFY(h.discovery.starts.empty());
    QCOMPARE(h.picker_calls, 0);
    QVERIFY(h.presenter.playQueueOccurrence(h.id(0)));
    QCOMPARE(h.backend.loads, std::vector<std::filesystem::path>{path});
    QCOMPARE(h.metadata.requests.size(), std::size_t{1});
    QCOMPARE(h.presenter.currentOccurrenceId(), h.id(0));
    h.backend.publish(PlaybackStatus::Ready);
    QCoreApplication::sendPostedEvents(&h.presenter, QEvent::MetaCall);
    QTRY_COMPARE(h.backend.plays, 1);
}

void PlayerDownloadImportTests::existingPlaybackIsPreserved_data() {
    QTest::addColumn<int>("status");
    QTest::newRow("playing") << static_cast<int>(PlaybackStatus::Playing);
    QTest::newRow("paused") << static_cast<int>(PlaybackStatus::Paused);
    QTest::newRow("ready") << static_cast<int>(PlaybackStatus::Ready);
    QTest::newRow("loading") << static_cast<int>(PlaybackStatus::Loading);
    QTest::newRow("error") << static_cast<int>(PlaybackStatus::Error);
}

void PlayerDownloadImportTests::existingPlaybackIsPreserved() {
    QFETCH(int, status);
    Harness h;
    QVERIFY(h.seed());
    h.backend.publish(static_cast<PlaybackStatus>(status));
    QCoreApplication::sendPostedEvents(&h.presenter, QEvent::MetaCall);
    QTRY_COMPARE(h.presenter.playbackState(), presenterState(static_cast<PlaybackStatus>(status)));
    PlayerDownloadImport importer(h.presenter);
    const auto prior_ids = h.ids();
    const auto current = h.presenter.currentOccurrenceId();
    const auto context = h.presenter.contextOccurrenceId();
    const auto playback = h.presenter.playbackState();
    const auto position = h.presenter.positionMs();
    const auto snapshot = h.backend.snapshot();
    const auto loads = h.backend.loads;
    const auto commands = h.backend.commands();
    const auto reads = h.metadata.requests.size();
    const auto cancellations = h.metadata.cancels;
    QSignalSpy reset(h.model(), &QAbstractItemModel::modelReset);
    QVERIFY(importer.accept("job-1", publishedPath()));
    QCOMPARE(h.model()->rowCount(), 2);
    QCOMPARE(h.id(0), prior_ids.front());
    QCOMPARE(h.presenter.currentOccurrenceId(), current);
    QCOMPARE(h.presenter.contextOccurrenceId(), context);
    QCOMPARE(h.presenter.playbackState(), playback);
    QCOMPARE(h.presenter.positionMs(), position);
    QCOMPARE(h.backend.snapshot(), snapshot);
    QCOMPARE(h.backend.loads, loads);
    QCOMPARE(h.backend.commands(), commands);
    QCOMPARE(h.metadata.requests.size(), reads);
    QCOMPARE(h.metadata.cancels, cancellations);
    QCOMPARE(reset.size(), 0);
    QCOMPARE(h.picker_calls, 1);
    QVERIFY(h.discovery.starts.empty());
}

void PlayerDownloadImportTests::replayAndConflictingPathsNeverCreateAnotherOccurrence() {
    Harness h;
    PlayerDownloadImport importer(h.presenter);
    const auto path = publishedPath();
    QVERIFY(importer.accept("job-1", path));
    const auto occurrence = h.id(0);
    QVERIFY(importer.accept("job-1", path));
    QVERIFY(!importer.accept("job-1", publishedPath(u8"Outra.mp3")));
    QCOMPARE(h.model()->rowCount(), 1);
    QCOMPARE(h.id(0), occurrence);
    QVERIFY(h.presenter.removeQueueOccurrence(occurrence));
    QCOMPARE(h.model()->rowCount(), 0);
    QVERIFY(importer.accept("job-1", path));
    QVERIFY(!importer.accept("job-1", publishedPath(u8"Outra.mp3")));
    QCOMPARE(h.model()->rowCount(), 0);
}

void PlayerDownloadImportTests::distinctJobsKeepDuplicatePathsAsDistinctOccurrences() {
    Harness h;
    PlayerDownloadImport importer(h.presenter);
    QVERIFY(importer.accept("job-1", publishedPath()));
    QVERIFY(importer.accept("job-2", publishedPath()));
    QCOMPARE(h.model()->rowCount(), 2);
    QVERIFY(h.id(0) != h.id(1));
    QVERIFY(!h.presenter.hasCurrentOccurrence());
    QVERIFY(h.backend.loads.empty());
}

void PlayerDownloadImportTests::invalidIdentifiers_data() {
    QTest::addColumn<QByteArray>("id");
    QTest::newRow("empty") << QByteArray{};
    QTest::newRow("too-long") << QByteArray(65, 'a');
    QTest::newRow("nul") << QByteArray("job\0suffix", 10);
    QTest::newRow("space") << QByteArray("job 1");
    QTest::newRow("control") << QByteArray("job\n1");
    QTest::newRow("slash") << QByteArray("job/1");
    QTest::newRow("unicode") << QByteArray::fromHex("6a6f622dc3a9");
}

void PlayerDownloadImportTests::invalidIdentifiers() {
    QFETCH(QByteArray, id);
    Harness h;
    PlayerDownloadImport importer(h.presenter, {.max_receipts = 1});
    QVERIFY(!importer.accept(std::string{id.constData(), static_cast<std::size_t>(id.size())}, publishedPath()));
    QCOMPARE(h.model()->rowCount(), 0);
    QVERIFY(h.backend.loads.empty());
    QVERIFY(importer.accept("valid-1", publishedPath()));
    QCOMPARE(h.model()->rowCount(), 1);
}

void PlayerDownloadImportTests::invalidPaths_data() {
    QTest::addColumn<QString>("path");
    QTest::newRow("empty") << QString{};
    QTest::newRow("relative") << QStringLiteral("final.mp3");
    QTest::newRow("remote") << QStringLiteral("https://media.example.test/final.mp3");
    QTest::newRow("wrong-extension") << QString::fromStdWString(publishedPath(u8"Final.mp4").wstring());
    QTest::newRow("partial") << QString::fromStdWString(publishedPath(u8"Final.mp3.partial").wstring());
    QTest::newRow("unnormalized") << QString::fromStdWString(
        (publishedPath().parent_path() / "nested" / ".." / "Final.mp3").wstring());
    auto nul = QString::fromStdWString(publishedPath().wstring());
    nul.insert(5, QChar{0});
    QTest::newRow("nul") << nul;
    QTest::newRow("too-long") << QString::fromStdWString(
                                     (publishedPath().parent_path() / std::wstring(30'001, L'x')).wstring()) +
                                     QStringLiteral(".mp3");
#ifdef _WIN32
    QTest::newRow("drive-relative") << QStringLiteral("C:Final.mp3");
    QTest::newRow("root-relative") << QStringLiteral("\\Final.mp3");
    QTest::newRow("unc") << QStringLiteral("\\\\server\\share\\Final.mp3");
    QTest::newRow("device") << QStringLiteral("\\\\.\\C:\\Final.mp3");
    QTest::newRow("extended") << QStringLiteral("\\\\?\\C:\\Final.mp3");
    QTest::newRow("alternate-stream") << QStringLiteral("C:/safe/Final:stream.mp3");
    auto lone_surrogate = QStringLiteral("C:/safe/");
    lone_surrogate.append(QChar{0xd800});
    lone_surrogate.append(QStringLiteral(".mp3"));
    QTest::newRow("unpaired-surrogate") << lone_surrogate;
#endif
}

void PlayerDownloadImportTests::invalidPaths() {
    QFETCH(QString, path);
    Harness h;
    PlayerDownloadImport importer(h.presenter, {.max_receipts = 1});
    QVERIFY(!importer.accept("invalid-1", std::filesystem::path{path.toStdWString()}));
    QCOMPARE(h.model()->rowCount(), 0);
    QVERIFY(h.backend.loads.empty());
    QVERIFY(importer.accept("valid-1", publishedPath()));
}

void PlayerDownloadImportTests::validIdentifierAndPathBoundaries() {
    Harness h;
    PlayerDownloadImport importer(h.presenter);
    QVERIFY(importer.accept("x", publishedPath(u8"A.MP3")));
    QVERIFY(importer.accept(std::string(62, 'a') + "_-", publishedPath(u8"音楽 ☕ 😀.mp3")));
    const auto parent = publishedPath().parent_path();
    const auto name = std::wstring(30'000 - parent.native().size() - 1 - 4, L'a') + L".mp3";
    const auto longest = parent / std::filesystem::path{name};
    QCOMPARE(longest.native().size(), std::size_t{30'000});
    QVERIFY(importer.accept("longest", longest));
    QCOMPARE(h.model()->rowCount(), 3);
    QVERIFY(h.backend.loads.empty());
}

void PlayerDownloadImportTests::receiptCapacityIsInclusiveAndDoesNotEvict() {
    Harness h;
    PlayerDownloadImport importer(h.presenter, {.max_receipts = 2});
    QVERIFY(importer.accept("job-1", publishedPath()));
    QVERIFY(importer.accept("job-2", publishedPath(u8"B.mp3")));
    QVERIFY(!importer.accept("job-3", publishedPath(u8"C.mp3")));
    QVERIFY(importer.accept("job-1", publishedPath()));
    QVERIFY(h.presenter.clearQueue());
    QVERIFY(importer.accept("job-2", publishedPath(u8"B.mp3")));
    QVERIFY(!importer.accept("job-3", publishedPath(u8"C.mp3")));
    QCOMPARE(h.model()->rowCount(), 0);
}

void PlayerDownloadImportTests::retainedBytesAreInclusive_data() {
    QTest::addColumn<int>("delta");
    QTest::newRow("one-byte-too-small") << -1;
    QTest::newRow("exact") << 0;
    QTest::newRow("one-byte-headroom") << 1;
}

void PlayerDownloadImportTests::retainedBytesAreInclusive() {
    QFETCH(int, delta);
    Harness h;
    const std::string id = "job-1";
    const auto path = publishedPath();
    const auto bytes = receiptBytes(id, path);
    const auto cap = delta < 0 ? bytes - 1 : bytes + static_cast<std::size_t>(delta);
    PlayerDownloadImport importer(h.presenter, {.max_receipts = 3, .max_retained_bytes = cap});
    QCOMPARE(importer.accept(id, path), delta >= 0);
    QCOMPARE(h.model()->rowCount(), delta >= 0 ? 1 : 0);
    if (delta >= 0) {
        QVERIFY(importer.accept(id, path));
        QVERIFY(!importer.accept("job-2", path));
        QCOMPARE(h.model()->rowCount(), 1);
    }
}

void PlayerDownloadImportTests::zeroLimitsRefuseAdmission_data() {
    QTest::addColumn<bool>("receipts");
    QTest::newRow("zero-receipts") << true;
    QTest::newRow("zero-bytes") << false;
}

void PlayerDownloadImportTests::zeroLimitsRefuseAdmission() {
    QFETCH(bool, receipts);
    Harness h;
    PlayerDownloadImport importer(
        h.presenter, {.max_receipts = receipts ? 0U : 128U, .max_retained_bytes = receipts ? 1024U * 1024U : 0U});
    QVERIFY(!importer.accept("job-1", publishedPath()));
    QCOMPARE(h.model()->rowCount(), 0);
}

void PlayerDownloadImportTests::busyDiscoveryAllowsExplicitRetryWithoutSideEffects() {
    Harness h;
    PlayerDownloadImport importer(h.presenter);
    QVERIFY(h.presenter.openSourcePaths({publishedPath(u8"Discovered.mp3")}, 0));
    QVERIFY(h.presenter.sourceImportActive());
    QVERIFY(!importer.accept("job-1", publishedPath()));
    QCOMPARE(h.model()->rowCount(), 0);
    QCOMPARE(h.discovery.starts.size(), std::size_t{1});
    QVERIFY(h.backend.loads.empty());
    QVERIFY(h.presenter.cancelSourceImport());
    QTRY_VERIFY(!h.presenter.sourceImportActive());
    QVERIFY(importer.accept("job-1", publishedPath()));
    QCOMPARE(h.model()->rowCount(), 1);
    QVERIFY(h.backend.loads.empty());
    QVERIFY(h.metadata.requests.empty());
}

void PlayerDownloadImportTests::refusedHandoffReleasesReceiptAndByteCapacity() {
    Harness h;
    const auto path = publishedPath();
    PlayerDownloadImport importer(h.presenter, {.max_receipts = 1, .max_retained_bytes = receiptBytes("job-1", path)});
    QVERIFY(h.presenter.openSourcePaths({publishedPath(u8"Discovered.mp3")}, 0));
    QVERIFY(!importer.accept("job-1", path));
    QVERIFY(!importer.accept("job-2", path));
    QCOMPARE(h.model()->rowCount(), 0);
    QVERIFY(h.presenter.cancelSourceImport());
    QTRY_VERIFY(!h.presenter.sourceImportActive());
    QVERIFY(importer.accept("job-3", path));
    QCOMPARE(h.model()->rowCount(), 1);
    QVERIFY(!importer.accept("job-1", path));
    QVERIFY(importer.accept("job-3", path));
    QCOMPARE(h.model()->rowCount(), 1);
}

void PlayerDownloadImportTests::postCommitClockExceptionRetainsReceiptAndReleasesCallGuard() {
    ImportPlatform platform;
    ImportBackend backend;
    ImportMetadata metadata;
    ImportSessionWriter writer;
    ImportPreferencesWriter preferences_writer;
    const auto now = [] { return std::chrono::steady_clock::time_point{}; };
    PreferencesAutosave preferences_autosave(preferences_writer, {}, {.now = now});
    bool throw_clock = false;
    SessionAutosave autosave(writer, {}, {.now = [&] {
                                 if (throw_clock)
                                     throw std::runtime_error("synthetic clock failure");
                                 return SessionAutosave::TimePoint{};
                             }});
    PlayerPresenter presenter(
        platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
        PlayerPresenterPreferences{
            .autosave = &preferences_autosave, .now = now, .session = {.autosave = &autosave, .now = now}});
    PlayerDownloadImport importer(presenter, {.max_receipts = 2});
    auto* const model = presenter.queueModelObject();
    QSignalSpy inserted(model, &QAbstractItemModel::rowsInserted);
    // This is a trusted C++ clock fault after commit, not an exception thrown
    // from a Qt row callback. It exercises recovery, not durable persistence.
    throw_clock = true;
    QVERIFY(!importer.accept("job-1", publishedPath()));
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(inserted.size(), 1);
    const auto occurrence = model->data(model->index(0, 0), LocalQueueListModel::OccurrenceIdRole);
    const auto revision = autosave.latestRevision();
    throw_clock = false;
    QVERIFY(importer.accept("job-1", publishedPath()));
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(inserted.size(), 1);
    QCOMPARE(model->data(model->index(0, 0), LocalQueueListModel::OccurrenceIdRole), occurrence);
    QCOMPARE(autosave.latestRevision(), revision);
    QVERIFY(importer.accept("job-2", publishedPath(u8"Another.mp3")));
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(inserted.size(), 2);
    QVERIFY(backend.loads.empty());
    QVERIFY(metadata.requests.empty());
}

void PlayerDownloadImportTests::fileSelectionAndLocationRefuseImport() {
    Harness h;
    ImportPicker picker;
    ImportLocator locator;
    PlayerDownloadImport importer(h.presenter);
    h.presenter.setAudioFilesPicker(&picker);
    QVERIFY(h.presenter.openAudio());
    QVERIFY(h.presenter.selectingAudioFiles());
    QVERIFY(!importer.accept("job-1", publishedPath()));
    QCOMPARE(h.model()->rowCount(), 0);
    QCOMPARE(picker.requests, 1);
    h.presenter.setAudioFilesPicker(nullptr);
    QVERIFY(!h.presenter.selectingAudioFiles());
    QVERIFY(h.seed());
    h.backend.publish(PlaybackStatus::Error);
    QCoreApplication::sendPostedEvents(&h.presenter, QEvent::MetaCall);
    QTRY_COMPARE(h.presenter.playbackState(), PlayerPresenter::PlaybackState::Error);
    h.presenter.setAudioFileLocator(&locator);
    QVERIFY(h.presenter.canLocateCurrentOccurrence());
    QVERIFY(h.presenter.locateQueueOccurrence(h.id(0)));
    QVERIFY(!importer.accept("job-1", publishedPath()));
    QCOMPARE(h.model()->rowCount(), 1);
    QCOMPARE(locator.requests, 1);
    h.presenter.setAudioFileLocator(nullptr);
    QVERIFY(importer.accept("job-1", publishedPath()));
    QCOMPARE(h.model()->rowCount(), 2);
}

void PlayerDownloadImportTests::committedReplayIsSafeDuringNotifications_data() {
    QTest::addColumn<int>("signal");
    QTest::newRow("rows-about-to-be-inserted") << 0;
    QTest::newRow("rows-inserted") << 1;
    QTest::newRow("state-changed") << 2;
}

void PlayerDownloadImportTests::committedReplayIsSafeDuringNotifications() {
    QFETCH(int, signal);
    Harness h;
    QVERIFY(h.seed(2));
    h.backend.publish(PlaybackStatus::Playing);
    QCoreApplication::sendPostedEvents(&h.presenter, QEvent::MetaCall);
    QTRY_COMPARE(h.presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    const auto prior_ids = h.ids();
    const auto current = h.presenter.currentOccurrenceId();
    const auto snapshot = h.backend.snapshot();
    const auto commands = h.backend.commands();
    const auto loads = h.backend.loads;
    const auto reads = h.metadata.requests.size();
    PlayerDownloadImport importer(h.presenter);
    QAbstractItemModelTester tester(h.model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    int observations = 0;
    const auto observe = [&] {
        ++observations;
        QVERIFY(importer.accept("job-1", publishedPath()));
        QVERIFY(!importer.accept("job-2", publishedPath(u8"Concurrent.mp3")));
        QVERIFY(!h.presenter.clearQueue());
        QVERIFY(!h.presenter.removeQueueOccurrence(prior_ids.front()));
        QVERIFY(!h.presenter.moveQueueOccurrence(prior_ids.front(), 1));
        QVERIFY(!h.presenter.shuffleQueue());
        QVERIFY(!h.presenter.toggleShuffleQueue());
        QVERIFY(!h.presenter.toggleRepeatQueue());
        QVERIFY(!h.presenter.nextTrack());
        QVERIFY(!h.presenter.previousTrack());
        QVERIFY(!h.presenter.playQueueOccurrence(prior_ids.back()));
        QVERIFY(!h.presenter.openAudio());
        QVERIFY(!h.presenter.addAudio());
        QVERIFY(!h.presenter.openSourcePaths({publishedPath(u8"Concurrent.mp3")}, 0));
        QVERIFY(!h.presenter.requestExit());
        QVERIFY(!h.presenter.requestClose());
        QVERIFY(!h.presenter.exiting());
    };
    QMetaObject::Connection connection;
    if (signal == 0) {
        connection = QObject::connect(h.model(), &QAbstractItemModel::rowsAboutToBeInserted, &h.presenter,
                                      [&](const QModelIndex&, int, int) { observe(); });
    } else if (signal == 1) {
        connection = QObject::connect(h.model(), &QAbstractItemModel::rowsInserted, &h.presenter,
                                      [&](const QModelIndex&, int, int) { observe(); });
    } else {
        connection = QObject::connect(&h.presenter, &PlayerPresenter::stateChanged, &h.presenter, observe);
    }
    QVERIFY(importer.accept("job-1", publishedPath()));
    QObject::disconnect(connection);
    QCOMPARE(observations, 1);
    QCOMPARE(h.model()->rowCount(), 3);
    QCOMPARE(h.id(0), prior_ids.front());
    QCOMPARE(h.id(1), prior_ids.back());
    QCOMPARE(h.presenter.currentOccurrenceId(), current);
    QCOMPARE(h.backend.snapshot(), snapshot);
    QCOMPARE(h.backend.commands(), commands);
    QCOMPARE(h.backend.loads, loads);
    QCOMPARE(h.metadata.requests.size(), reads);
    QCOMPARE(h.picker_calls, 1);
    QVERIFY(h.discovery.starts.empty());
    QVERIFY(importer.accept("job-2", publishedPath(u8"Concurrent.mp3")));
    QCOMPARE(h.model()->rowCount(), 4);
}

void PlayerDownloadImportTests::modelCommitRunsBeforeNotificationAndNeverForInvalidInput() {
    LocalFileQueue queue;
    QVERIFY(queue.append({publishedPath()}));
    LocalQueueListModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    int commits = 0;
    int notifications = 0;
    QObject::connect(&model, &QAbstractItemModel::rowsAboutToBeInserted, &model, [&](const QModelIndex&, int, int) {
        ++notifications;
        QCOMPARE(commits, 1);
        QCOMPARE(model.rowCount(), 0);
    });
    QVERIFY(model.appendWithCommit(queue.items(), [&] { ++commits; }));
    QCOMPARE(commits, 1);
    QCOMPARE(notifications, 1);
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(!model.appendWithCommit(queue.items(), [&] { ++commits; }));
    QVERIFY(!model.appendWithCommit({}, [&] { ++commits; }));
    const std::vector<LocalFileQueueItem> missing_path{{.occurrence_id = {2}}};
    QVERIFY(!model.appendWithCommit(missing_path, [&] { ++commits; }));
    const std::vector<LocalFileQueueItem> missing_id{{.source_path = publishedPath()}};
    QVERIFY(!model.appendWithCommit(missing_id, [&] { ++commits; }));
    QCOMPARE(commits, 1);
    QCOMPARE(notifications, 1);
    QCOMPARE(model.rowCount(), 1);
}

void PlayerDownloadImportTests::nestedPlaybackEndWaitsForAppendThenAdvancesExactlyOnce() {
    Harness h;
    QVERIFY(h.seed(2));
    h.backend.publish(PlaybackStatus::Playing);
    QCoreApplication::sendPostedEvents(&h.presenter, QEvent::MetaCall);
    QTRY_COMPARE(h.presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    const auto prior_ids = h.ids();
    const auto current = h.presenter.currentOccurrenceId();
    const auto loads = h.backend.loads;
    const auto generation = h.backend.snapshot().generation;
    PlayerDownloadImport importer(h.presenter);
    QAbstractItemModelTester tester(h.model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    int callbacks = 0;
    const auto connection = QObject::connect(h.model(), &QAbstractItemModel::rowsAboutToBeInserted, &h.presenter,
                                             [&](const QModelIndex&, int, int) {
                                                 ++callbacks;
                                                 h.backend.publishEnded();
                                                 // A later same-generation snapshot must not erase a deferred Ended.
                                                 h.backend.publish(PlaybackStatus::Playing);
                                                 QCoreApplication::sendPostedEvents(&h.presenter, QEvent::MetaCall);
                                                 QCOMPARE(h.backend.loads, loads);
                                                 QCOMPARE(h.backend.snapshot().generation, generation);
                                                 QCOMPARE(h.presenter.currentOccurrenceId(), current);
                                                 QCOMPARE(h.model()->rowCount(), 2);
                                             });
    QVERIFY(importer.accept("job-1", publishedPath()));
    QObject::disconnect(connection);
    QCOMPARE(callbacks, 1);
    QTRY_COMPARE(h.backend.loads.size(), std::size_t{2});
    QCOMPARE(h.backend.snapshot().generation, generation + 1);
    QCOMPARE(h.backend.loads.back(), publishedPath(u8"Seguinte.mp3"));
    QTRY_COMPARE(h.presenter.currentOccurrenceId(), prior_ids.back());
    // Repeat-off navigation preserves the historical queue; import does not
    // change that local transport contract or select the newly appended track.
    QCOMPARE(h.model()->rowCount(), 3);
    QCOMPARE(h.id(0), prior_ids.front());
    QCOMPARE(h.id(1), prior_ids.back());
    QCOMPARE(h.model()->data(h.model()->index(2, 0), LocalQueueListModel::FileNameRole).toString(),
             QString::fromStdWString(publishedPath().filename().wstring()));
    QCoreApplication::sendPostedEvents(&h.presenter, QEvent::MetaCall);
    QCOMPARE(h.backend.loads.size(), std::size_t{2});
}

void PlayerDownloadImportTests::nestedMetadataPollingWaitsForAppendThenConsumesExactlyOnce() {
    Harness h;
    QVERIFY(h.seed());
    h.backend.publish(PlaybackStatus::Playing);
    QCoreApplication::sendPostedEvents(&h.presenter, QEvent::MetaCall);
    QTRY_COMPARE(h.presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    QCOMPARE(h.metadata.requests.size(), std::size_t{1});
    const auto& request = h.metadata.requests.front();
    h.metadata.completions.push_back(
        {.status = MetadataPollStatus::Completed,
         .completion = MetadataReadCompletion{
             .generation = request.generation,
             .source_path = request.source_path,
             .result = TrackMetadata{.source_path = request.source_path, .display_title = "Metadata once"}}});
    auto* const timer = h.presenter.findChild<QTimer*>();
    QVERIFY(timer != nullptr);
    const auto current = h.presenter.currentOccurrenceId();
    const auto loads = h.backend.loads;
    const auto old_title = h.presenter.currentTitle();
    const int polls = h.metadata.polls;
    const int takes = h.metadata.takes;
    PlayerDownloadImport importer(h.presenter);
    QSignalSpy metadata_changed(h.model(), &QAbstractItemModel::dataChanged);
    int callbacks = 0;
    const auto connection = QObject::connect(
        h.model(), &QAbstractItemModel::rowsAboutToBeInserted, &h.presenter, [&](const QModelIndex&, int, int) {
            ++callbacks;
            QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
            QCOMPARE(h.metadata.consumed, 0);
            QCOMPARE(h.metadata.polls, polls);
            QCOMPARE(h.metadata.takes, takes);
            QCOMPARE(h.metadata.completions.size(), std::size_t{1});
            QCOMPARE(metadata_changed.size(), 0);
            QCOMPARE(h.presenter.currentTitle(), old_title);
            QCOMPARE(h.model()->rowCount(), 1);
        });
    QVERIFY(importer.accept("job-1", publishedPath()));
    QObject::disconnect(connection);
    QCOMPARE(callbacks, 1);
    QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
    QCOMPARE(h.metadata.consumed, 1);
    QCOMPARE(h.metadata.polls, polls + 1);
    QCOMPARE(h.metadata.takes, takes);
    QVERIFY(h.metadata.completions.empty());
    QCOMPARE(h.presenter.currentTitle(), QStringLiteral("Metadata once"));
    QCOMPARE(metadata_changed.size(), 1);
    QCOMPARE(h.presenter.currentOccurrenceId(), current);
    QCOMPARE(h.backend.loads, loads);
    QCOMPARE(h.model()->rowCount(), 2);
    QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
    QCOMPARE(h.metadata.consumed, 1);
    QCOMPARE(metadata_changed.size(), 1);
}

void PlayerDownloadImportTests::nestedPowerEventsWaitForAppendAndPreserveFinalState_data() {
    QTest::addColumn<bool>("end_suspended");
    QTest::newRow("suspend-resume") << false;
    QTest::newRow("suspend-resume-suspend") << true;
}

void PlayerDownloadImportTests::nestedPowerEventsWaitForAppendAndPreserveFinalState() {
    QFETCH(bool, end_suspended);
    Harness h;
    QVERIFY(h.seed());
    h.backend.publish(PlaybackStatus::Playing);
    QCoreApplication::sendPostedEvents(&h.presenter, QEvent::MetaCall);
    QTRY_COMPARE(h.presenter.playbackState(), PlayerPresenter::PlaybackState::Playing);
    auto* const timer = h.presenter.findChild<QTimer*>();
    QVERIFY(timer != nullptr);
    QVERIFY(timer->isActive());
    PlayerDownloadImport importer(h.presenter);
    const int tray_reads = h.platform.tray_reads;
    const auto current = h.presenter.currentOccurrenceId();
    const auto loads = h.backend.loads;
    int callbacks = 0;
    const auto connection = QObject::connect(
        h.model(), &QAbstractItemModel::rowsAboutToBeInserted, &h.presenter, [&](const QModelIndex&, int, int) {
            ++callbacks;
            QVERIFY(h.presenter.handleSystemPowerEvent(SystemPowerEvent::Suspending));
            QVERIFY(h.presenter.handleSystemPowerEvent(SystemPowerEvent::Resumed));
            if (end_suspended)
                QVERIFY(h.presenter.handleSystemPowerEvent(SystemPowerEvent::Suspending));
            QCOMPARE(h.platform.tray_reads, tray_reads);
            QCOMPARE(h.presenter.currentOccurrenceId(), current);
            QCOMPARE(h.backend.loads, loads);
        });
    QVERIFY(importer.accept("job-1", publishedPath()));
    QObject::disconnect(connection);
    QCOMPARE(callbacks, 1);
    QCoreApplication::sendPostedEvents(&h.presenter, QEvent::MetaCall);
    QCOMPARE(h.platform.tray_reads, tray_reads + (end_suspended ? 0 : 1));
    QCOMPARE(timer->isActive(), !end_suspended);
    QVERIFY(
        !h.presenter.handleSystemPowerEvent(end_suspended ? SystemPowerEvent::Suspending : SystemPowerEvent::Resumed));
    QCOMPARE(h.presenter.currentOccurrenceId(), current);
    QCOMPARE(h.backend.loads, loads);
    QCOMPARE(h.model()->rowCount(), 2);
}

void PlayerDownloadImportTests::realServiceCompletesOnlyAfterAppendAndReplayDoesNotImport() {
    Harness h;
    PlayerDownloadImport importer(h.presenter);
    ImportExecution execution;
    DownloadService service(execution, importer, "integration");
    const auto receipt = service.submit(request());
    QCOMPARE(receipt.error, DownloadSubmissionError::None);
    QCOMPARE(receipt.job_ids.size(), std::size_t{1});
    QCOMPARE(h.model()->rowCount(), 0);
    service.tick(0);
    execution.publish(receipt.job_ids.front(), publishedPath());
    bool publishing_observed = false;
    const auto connection =
        QObject::connect(h.model(), &QAbstractItemModel::rowsInserted, &h.presenter, [&](const QModelIndex&, int, int) {
            publishing_observed = true;
            QCOMPARE(service.snapshot().front().state, DownloadJobState::Publishing);
            QVERIFY(!service.retryImport(receipt.job_ids.front()));
        });
    service.tick(1);
    QObject::disconnect(connection);
    QVERIFY(publishing_observed);
    QCOMPARE(service.snapshot().front().state, DownloadJobState::Completed);
    QCOMPARE(h.model()->rowCount(), 1);
    const auto replay = service.submit(request());
    QCOMPARE(replay.job_ids, receipt.job_ids);
    service.tick(2);
    QCOMPARE(h.model()->rowCount(), 1);
    QCOMPARE(execution.starts.size(), std::size_t{1});
    QVERIFY(h.backend.loads.empty());
    service.shutdown();
    QVERIFY(service.stopped());
}

void PlayerDownloadImportTests::realServiceRetriesImportWithoutExecutingAgain() {
    Harness h;
    PlayerDownloadImport importer(h.presenter);
    ImportExecution execution;
    DownloadService service(execution, importer, "integration");
    QVERIFY(h.presenter.openSourcePaths({publishedPath(u8"Discovered.mp3")}, 0));
    const auto receipt = service.submit(request());
    QCOMPARE(receipt.error, DownloadSubmissionError::None);
    service.tick(0);
    execution.publish(receipt.job_ids.front(), publishedPath());
    service.tick(1);
    QCOMPARE(service.snapshot().front().state, DownloadJobState::ImportFailed);
    QCOMPARE(service.snapshot().front().error, DownloadFailure::Import);
    QCOMPARE(service.snapshot().front().completed_mp3, publishedPath());
    QCOMPARE(h.model()->rowCount(), 0);
    QVERIFY(!service.retryImport(receipt.job_ids.front()));
    QVERIFY(h.presenter.cancelSourceImport());
    QTRY_VERIFY(!h.presenter.sourceImportActive());
    QVERIFY(service.retryImport(receipt.job_ids.front()));
    QCOMPARE(service.snapshot().front().state, DownloadJobState::Completed);
    QCOMPARE(h.model()->rowCount(), 1);
    QCOMPARE(execution.starts.size(), std::size_t{1});
    QVERIFY(!service.retryImport(receipt.job_ids.front()));
    QVERIFY(h.backend.loads.empty());
    service.shutdown();
    QVERIFY(service.stopped());
}

void PlayerDownloadImportTests::realServiceDoesNotImportPublicationAfterCancellation() {
    Harness h;
    PlayerDownloadImport importer(h.presenter);
    ImportExecution execution;
    DownloadService service(execution, importer, "integration");
    const auto receipt = service.submit(request());
    QCOMPARE(receipt.error, DownloadSubmissionError::None);
    service.tick(0);
    QVERIFY(service.cancel(receipt.job_ids.front()));
    QCOMPARE(execution.cancellations.size(), std::size_t{1});
    execution.publish(receipt.job_ids.front(), publishedPath());
    service.tick(1);
    QCOMPARE(service.snapshot().front().state, DownloadJobState::Cancelled);
    QCOMPARE(h.model()->rowCount(), 0);
    QVERIFY(h.backend.loads.empty());
    QVERIFY(h.metadata.requests.empty());
    service.shutdown();
    QVERIFY(service.stopped());
}

void PlayerDownloadImportTests::destroyedExitingAndFrozenPresentersRefuseAdmission() {
    Harness h;
    auto temporary = std::make_unique<PlayerPresenter>(
        h.platform, h.backend, h.metadata, [] { return std::vector<std::filesystem::path>{}; }, h.discovery);
    PlayerDownloadImport destroyed(*temporary);
    temporary.reset();
    QVERIFY(!destroyed.accept("job-1", publishedPath()));
    PlayerDownloadImport exiting(h.presenter);
    QVERIFY(h.presenter.requestExit());
    QVERIFY(h.presenter.exiting());
    QVERIFY(!exiting.accept("job-1", publishedPath()));
    QCOMPARE(h.model()->rowCount(), 0);
    ImportPlatform frozen_platform;
    PlayerPresenter frozen(
        FixtureRepository{}.load(QString::fromUtf8(M0_TEST_FIXTURE_FILE), QString::fromUtf8(M0_TEST_QUEUE_FILE)),
        frozen_platform);
    PlayerDownloadImport frozen_importer(frozen);
    const auto rows = frozen.queueModelObject()->rowCount();
    QVERIFY(!frozen_importer.accept("job-1", publishedPath()));
    QCOMPARE(frozen.queueModelObject()->rowCount(), rows);
}

void PlayerDownloadImportTests::foreignThreadCannotReadOrMutatePresenter() {
    Harness h;
    PlayerDownloadImport importer(h.presenter);
    bool accepted = true;
    std::thread foreign([&] { accepted = importer.accept("job-1", publishedPath()); });
    foreign.join();
    QVERIFY(!accepted);
    QCOMPARE(h.model()->rowCount(), 0);
    QVERIFY(h.backend.loads.empty());
    QVERIFY(importer.accept("job-1", publishedPath()));
    QCOMPARE(h.model()->rowCount(), 1);
}

void PlayerDownloadImportTests::privilegedAppendIsNotInvokableFromQml() {
    Harness h;
    const auto* meta = h.presenter.metaObject();
    for (int index = 0; index < meta->methodCount(); ++index)
        QVERIFY(meta->method(index).name() != QByteArrayLiteral("appendPublishedAudio"));
}

void PlayerDownloadImportTests::durableImportWaitsForMatchingSaveOfQueueAndReceipt() {
    DurableHarness h;
    PlayerDownloadImport importer(h.presenter, {}, PlayerDownloadImportPersistence::Session);
    const auto path = publishedPath();
    QCOMPARE(importer.begin("durable-1", path), DownloadImportState::Pending);
    QCOMPARE(h.model()->rowCount(), 1);
    QVERIFY(h.writer.active);
    QCOMPARE(h.writer.active->snapshot.queue.items.size(), std::size_t{1});
    QCOMPARE(h.writer.active->snapshot.download_import_receipts.size(), std::size_t{1});
    QCOMPARE(h.writer.active->snapshot.queue.items.front().source_path, path);
    QCOMPARE(h.writer.active->snapshot.download_import_receipts.front().job_id, std::string{"durable-1"});
    QCOMPARE(h.writer.active->snapshot.download_import_receipts.front().source_path, path);
    QVERIFY(!h.writer.active->snapshot.queue.current_occurrence_id);
    QVERIFY(!importer.accept("durable-1", path));
    QCOMPARE(importer.pollImport("durable-1"), DownloadImportState::Pending);
    h.writer.complete(SessionSaveStatus::Saved);
    // Delivery to the autosave owner is part of the acknowledgement.
    QCOMPARE(importer.pollImport("durable-1"), DownloadImportState::Pending);
    h.poll();
    QCOMPARE(importer.pollImport("durable-1"), DownloadImportState::Committed);
    QVERIFY(importer.accept("durable-1", path));
    QCOMPARE(h.model()->rowCount(), 1);
    QVERIFY(h.backend.loads.empty());
    QVERIFY(h.metadata.requests.empty());
}

void PlayerDownloadImportTests::durableSavedOlderRevisionAcknowledgesWithoutWaitingForPositionChanges() {
    DurableHarness h;
    PlayerDownloadImport importer(h.presenter, {}, PlayerDownloadImportPersistence::Session);
    QCOMPARE(importer.begin("first", publishedPath()), DownloadImportState::Pending);
    const auto first_revision = h.writer.active->revision;
    QCOMPARE(importer.begin("second", publishedPath(u8"Second.mp3")), DownloadImportState::Pending);
    QVERIFY(h.autosave.latestRevision() > first_revision);
    h.writer.complete(SessionSaveStatus::Saved);
    h.poll();
    QCOMPARE(importer.pollImport("first"), DownloadImportState::Committed);
    QCOMPARE(importer.pollImport("second"), DownloadImportState::Pending);
    QVERIFY(h.writer.active);
    QCOMPARE(h.writer.active->snapshot.download_import_receipts.size(), std::size_t{2});
    h.writer.complete(SessionSaveStatus::Saved);
    h.poll();
    QCOMPARE(importer.pollImport("second"), DownloadImportState::Committed);
}

void PlayerDownloadImportTests::durableFailureDoesNotPromoteReadbackAndExplicitRetryDoesNotAppend() {
    DurableHarness h;
    PlayerDownloadImport importer(h.presenter, {}, PlayerDownloadImportPersistence::Session);
    const auto path = publishedPath();
    QCOMPARE(importer.begin("first", path), DownloadImportState::Pending);
    h.writer.complete(SessionSaveStatus::Failed, true);
    h.poll();
    QCOMPARE(importer.pollImport("first"), DownloadImportState::Failed);
    QVERIFY(!h.writer.active);
    QCOMPARE(h.model()->rowCount(), 1);
    for (int poll = 0; poll < 3; ++poll) {
        QCOMPARE(importer.pollImport("first"), DownloadImportState::Failed);
        h.poll();
    }
    QCOMPARE(h.writer.starts.size(), std::size_t{1});
    QCOMPARE(importer.begin("first", path), DownloadImportState::Pending);
    QCOMPARE(h.model()->rowCount(), 1);
    QVERIFY(h.writer.active);
    QCOMPARE(h.writer.active->snapshot.download_import_receipts.size(), std::size_t{1});
    h.writer.complete(SessionSaveStatus::Saved);
    h.poll();
    QCOMPARE(importer.pollImport("first"), DownloadImportState::Committed);
}

void PlayerDownloadImportTests::retiredReceiptReleasesCapacityOnlyAfterSavedWithoutRewritingAnUnrestoredQueue() {
    const auto old_path = publishedPath();
    const auto new_path = publishedPath(u8"New.mp3");
    LocalFileQueue stored_queue;
    QVERIFY(stored_queue.append({old_path}));
    QVERIFY(stored_queue.select(0));
    const SessionSnapshot initial{.queue = stored_queue.snapshot(),
                                  .position_ms = 450,
                                  .download_import_receipts = {{.job_id = "old", .source_path = old_path}}};
    DurableHarness h(initial, SessionLoadStatus::Loaded, false);
    PlayerDownloadImport importer(h.presenter, {.max_receipts = 1}, PlayerDownloadImportPersistence::Session);
    QCOMPARE(h.model()->rowCount(), 0);
    QVERIFY(!h.presenter.downloadImportRetirementPending());
    QVERIFY(importer.committedRetiredImportJobIds().empty());
    const std::vector<std::string> archived{"old", "unknown"};
    importer.retireArchivedReceipts(archived);
    QVERIFY(h.presenter.downloadImportRetirementPending());
    QVERIFY(importer.committedRetiredImportJobIds().empty());
    QVERIFY(h.writer.active);
    QCOMPARE(h.writer.active->snapshot.queue, initial.queue);
    QCOMPARE(h.writer.active->snapshot.position_ms, initial.position_ms);
    QVERIFY(h.writer.active->snapshot.download_import_receipts.empty());
    QCOMPARE(h.writer.active->snapshot.retired_download_import_job_ids, std::vector<std::string>{"old"});
    QCOMPARE(importer.begin("old", old_path), DownloadImportState::Committed);
    QCOMPARE(importer.begin("new", new_path), DownloadImportState::Failed);

    h.writer.complete(SessionSaveStatus::Failed, true);
    h.poll();
    QVERIFY(h.writer.disk.download_import_receipts.empty());
    QVERIFY(h.presenter.downloadImportRetirementPending());
    QVERIFY(importer.committedRetiredImportJobIds().empty());
    QCOMPARE(importer.begin("new", new_path), DownloadImportState::Failed);
    QVERIFY(!h.writer.active);
    QCOMPARE(h.writer.starts.size(), std::size_t{1});
    QVERIFY(h.presenter.retryPreferenceSave());
    h.poll();
    QVERIFY(h.writer.active);
    QCOMPARE(h.writer.active->snapshot, h.writer.disk);
    h.writer.complete(SessionSaveStatus::Saved);
    h.poll();
    QVERIFY(!h.presenter.downloadImportRetirementPending());
    const auto retired = importer.committedRetiredImportJobIds();
    QCOMPARE(std::vector<std::string>(retired.begin(), retired.end()), std::vector<std::string>{"old"});
    QCOMPARE(importer.begin("old", old_path), DownloadImportState::Failed);
    QCOMPARE(h.writer.disk.queue, initial.queue);
    QCOMPARE(h.writer.disk.position_ms, initial.position_ms);
    QCOMPARE(h.model()->rowCount(), 0);
    QVERIFY(h.backend.loads.empty());

    DurableHarness restarted(h.writer.disk, SessionLoadStatus::Loaded, false);
    PlayerDownloadImport restarted_importer(restarted.presenter, {.max_receipts = 1},
                                            PlayerDownloadImportPersistence::Session);
    const auto restarted_retired = restarted_importer.committedRetiredImportJobIds();
    QCOMPARE(std::vector<std::string>(restarted_retired.begin(), restarted_retired.end()),
             std::vector<std::string>{"old"});
    DurableHarness backup(h.writer.disk, SessionLoadStatus::RecoveredFromBackup, false);
    PlayerDownloadImport backup_importer(backup.presenter, {}, PlayerDownloadImportPersistence::Session);
    QVERIFY(backup_importer.committedRetiredImportJobIds().empty());
    QCOMPARE(restarted_importer.begin("old", old_path), DownloadImportState::Failed);
    QVERIFY(restarted.writer.starts.empty());
    QCOMPARE(importer.begin("new", new_path), DownloadImportState::Pending);
    QCOMPARE(h.model()->rowCount(), 1);
    QVERIFY(h.writer.active);
    QCOMPARE(h.writer.active->snapshot.retired_download_import_job_ids, std::vector<std::string>{"old"});
    h.writer.complete(SessionSaveStatus::Saved);
    h.poll();
}

void PlayerDownloadImportTests::overlappingReceiptRetirementsKeepPendingImportsAndNewQueueEdits() {
    const auto first = publishedPath(u8"First.mp3");
    const auto second = publishedPath(u8"Second.mp3");
    const auto pending = publishedPath(u8"Pending.mp3");
    const auto local = publishedPath(u8"Local.mp3");
    const SessionSnapshot initial{.download_import_receipts = {{.job_id = "first", .source_path = first},
                                                               {.job_id = "second", .source_path = second}}};
    DurableHarness h(initial, SessionLoadStatus::Loaded);
    PlayerDownloadImport importer(h.presenter, {.max_receipts = 3}, PlayerDownloadImportPersistence::Session);
    QCOMPARE(importer.begin("pending", pending), DownloadImportState::Pending);
    const std::vector<std::string> first_archive{"first", "pending"};
    importer.retireArchivedReceipts(first_archive);
    QVERIFY(h.presenter.downloadImportRetirementPending());
    h.writer.complete(SessionSaveStatus::Saved);
    h.poll();
    QVERIFY(h.writer.active);
    QCOMPARE(h.writer.active->snapshot.retired_download_import_job_ids, std::vector<std::string>{"first"});
    QCOMPARE(h.writer.active->snapshot.download_import_receipts.size(), std::size_t{2});
    QCOMPARE(h.writer.active->snapshot.download_import_receipts.back().job_id, std::string{"pending"});

    const std::vector<std::string> second_archive{"second", "first"};
    importer.retireArchivedReceipts(second_archive);
    QVERIFY(h.presenter.appendPlaylistSources({local}));
    const auto latest = h.autosave.latestRevision();
    h.writer.complete(SessionSaveStatus::Saved);
    h.poll();
    QVERIFY(h.presenter.downloadImportRetirementPending());
    QCOMPARE(importer.begin("first", first), DownloadImportState::Failed);
    QCOMPARE(importer.begin("second", second), DownloadImportState::Committed);
    QVERIFY(h.writer.active);
    QCOMPARE(h.writer.active->revision, latest);
    QCOMPARE(h.writer.active->snapshot.retired_download_import_job_ids, (std::vector<std::string>{"first", "second"}));
    QCOMPARE(h.writer.active->snapshot.download_import_receipts.size(), std::size_t{1});
    QCOMPARE(h.writer.active->snapshot.download_import_receipts.front().job_id, std::string{"pending"});
    QCOMPARE(h.writer.active->snapshot.queue.items.size(), std::size_t{2});
    QCOMPARE(h.writer.active->snapshot.queue.items.back().source_path, local);

    h.writer.complete(SessionSaveStatus::Failed, true);
    h.poll();
    QVERIFY(h.presenter.downloadImportRetirementPending());
    QCOMPARE(importer.begin("second", second), DownloadImportState::Committed);
    QVERIFY(h.presenter.retryPreferenceSave());
    h.poll();
    QVERIFY(h.writer.active);
    h.writer.complete(SessionSaveStatus::Saved);
    h.poll();
    QVERIFY(!h.presenter.downloadImportRetirementPending());
    QCOMPARE(importer.begin("second", second), DownloadImportState::Failed);
    QCOMPARE(importer.begin("pending", pending), DownloadImportState::Committed);
    QCOMPARE(h.model()->rowCount(), 2);
    QVERIFY(h.backend.loads.empty());
}

void PlayerDownloadImportTests::durableReplaysSurviveRemovalAndRestartWithRestoreDisabled() {
    DurableHarness h;
    const auto path = publishedPath();
    {
        PlayerDownloadImport importer(h.presenter, {}, PlayerDownloadImportPersistence::Session);
        QCOMPARE(importer.begin("first", path), DownloadImportState::Pending);
        QVERIFY(h.presenter.removeQueueOccurrence(h.id(0)));
        h.writer.complete(SessionSaveStatus::Saved);
        h.poll();
        QCOMPARE(importer.pollImport("first"), DownloadImportState::Committed);
        QVERIFY(h.autosave.dirty());
        h.clock += std::chrono::seconds{1};
        h.poll();
        QVERIFY(h.writer.active);
        h.writer.complete(SessionSaveStatus::Saved);
        h.poll();
    }
    QVERIFY(h.writer.disk.queue.items.empty());
    QCOMPARE(h.writer.disk.download_import_receipts.size(), std::size_t{1});
    DurableHarness restarted(h.writer.disk, SessionLoadStatus::Loaded, false);
    PlayerDownloadImport importer(restarted.presenter, {}, PlayerDownloadImportPersistence::Session);
    QCOMPARE(importer.begin("first", path), DownloadImportState::Committed);
    QCOMPARE(importer.begin("first", publishedPath(u8"Conflict.mp3")), DownloadImportState::Failed);
    QCOMPARE(restarted.model()->rowCount(), 0);
    QVERIFY(restarted.writer.starts.empty());
    QVERIFY(restarted.backend.loads.empty());
    // The ledger belongs to the presenter, not an individual port wrapper.
    PlayerDownloadImport second_wrapper(restarted.presenter, {}, PlayerDownloadImportPersistence::Session);
    QCOMPARE(second_wrapper.begin("first", path), DownloadImportState::Committed);
    QCOMPARE(restarted.model()->rowCount(), 0);
}

void PlayerDownloadImportTests::durableReceiptsStayInStructuralSnapshotsAfterClear() {
    DurableHarness h;
    PlayerDownloadImport importer(h.presenter, {}, PlayerDownloadImportPersistence::Session);
    QCOMPARE(importer.begin("first", publishedPath()), DownloadImportState::Pending);
    h.writer.complete(SessionSaveStatus::Saved);
    h.poll();
    QVERIFY(h.presenter.clearQueue());
    h.autosave.requestSaveNow();
    QVERIFY(h.writer.active);
    QVERIFY(h.writer.active->snapshot.queue.items.empty());
    QCOMPARE(h.writer.active->snapshot.download_import_receipts.size(), std::size_t{1});
    h.writer.complete(SessionSaveStatus::Saved);
    h.poll();
    QCOMPARE(importer.begin("first", publishedPath()), DownloadImportState::Committed);
    QCOMPARE(h.model()->rowCount(), 0);
}

void PlayerDownloadImportTests::durableUntrustedStartupBlocksNewImports_data() {
    QTest::addColumn<int>("status");
    QTest::newRow("invalid-defaults") << static_cast<int>(SessionLoadStatus::InvalidUsingDefaults);
    QTest::newRow("future") << static_cast<int>(SessionLoadStatus::FutureVersion);
    QTest::newRow("backup-history-may-have-rolled-back") << static_cast<int>(SessionLoadStatus::RecoveredFromBackup);
}
void PlayerDownloadImportTests::durableUntrustedStartupBlocksNewImports() {
    QFETCH(int, status);
    DurableHarness h({}, static_cast<SessionLoadStatus>(status));
    PlayerDownloadImport importer(h.presenter, {}, PlayerDownloadImportPersistence::Session);
    QCOMPARE(importer.begin("first", publishedPath()), DownloadImportState::Failed);
    QCOMPARE(h.model()->rowCount(), 0);
    QVERIFY(h.writer.starts.empty());
    QVERIFY(h.backend.loads.empty());
}

void PlayerDownloadImportTests::durableNeedsPersistenceAndHonorsReceiptLimits() {
    Harness volatile_player;
    PlayerDownloadImport no_store(volatile_player.presenter, {}, PlayerDownloadImportPersistence::Session);
    QCOMPARE(no_store.begin("first", publishedPath()), DownloadImportState::Failed);
    QCOMPARE(volatile_player.model()->rowCount(), 0);
    DurableHarness h;
    const auto path = publishedPath();
    PlayerDownloadImport importer(h.presenter, {.max_receipts = 1, .max_retained_bytes = receiptBytes("first", path)},
                                  PlayerDownloadImportPersistence::Session);
    QCOMPARE(importer.begin("first", path), DownloadImportState::Pending);
    h.writer.complete(SessionSaveStatus::Saved);
    h.poll();
    QVERIFY(h.presenter.clearQueue());
    QCOMPARE(importer.begin("second", path), DownloadImportState::Failed);
    QCOMPARE(importer.begin("first", path), DownloadImportState::Committed);
    QCOMPARE(h.model()->rowCount(), 0);
}

void PlayerDownloadImportTests::durableRecoveryCapabilityRequiresHealthySessionAndSameThread() {
    Harness volatile_player;
    PlayerDownloadImport volatile_importer(volatile_player.presenter);
    QVERIFY(!volatile_importer.supportsDurableRecovery());
    PlayerDownloadImport no_store(volatile_player.presenter, {}, PlayerDownloadImportPersistence::Session);
    QVERIFY(!no_store.supportsDurableRecovery());
    DurableHarness h;
    PlayerDownloadImport importer(h.presenter, {}, PlayerDownloadImportPersistence::Session);
    QVERIFY(importer.supportsDurableRecovery());
    QVERIFY(h.writer.starts.empty());
    QVERIFY(h.backend.loads.empty());
    QCOMPARE(h.model()->rowCount(), 0);
    bool foreign_capability = true;
    std::thread foreign([&] { foreign_capability = importer.supportsDurableRecovery(); });
    foreign.join();
    QVERIFY(!foreign_capability);
    QVERIFY(importer.supportsDurableRecovery());
    QVERIFY(h.presenter.requestExit());
    QVERIFY(!importer.supportsDurableRecovery());
    auto temporary = std::make_unique<DurableHarness>();
    PlayerDownloadImport destroyed(temporary->presenter, {}, PlayerDownloadImportPersistence::Session);
    QVERIFY(destroyed.supportsDurableRecovery());
    temporary.reset();
    QVERIFY(!destroyed.supportsDurableRecovery());
}

void PlayerDownloadImportTests::durableRecoveryCapabilityRejectsUntrustedStartup_data() {
    QTest::addColumn<int>("status");
    QTest::newRow("invalid-defaults") << static_cast<int>(SessionLoadStatus::InvalidUsingDefaults);
    QTest::newRow("future") << static_cast<int>(SessionLoadStatus::FutureVersion);
    QTest::newRow("backup") << static_cast<int>(SessionLoadStatus::RecoveredFromBackup);
}
void PlayerDownloadImportTests::durableRecoveryCapabilityRejectsUntrustedStartup() {
    QFETCH(int, status);
    DurableHarness h({}, static_cast<SessionLoadStatus>(status));
    PlayerDownloadImport importer(h.presenter, {}, PlayerDownloadImportPersistence::Session);
    QVERIFY(!importer.supportsDurableRecovery());
    QVERIFY(h.writer.starts.empty());
    QVERIFY(h.backend.loads.empty());
    QCOMPARE(h.model()->rowCount(), 0);
}

void PlayerDownloadImportTests::durableReentrantReplayCannotAcknowledgeBeforeSave() {
    DurableHarness h;
    PlayerDownloadImport importer(h.presenter, {}, PlayerDownloadImportPersistence::Session);
    const auto path = publishedPath();
    int callbacks = 0;
    QObject::connect(h.model(), &QAbstractItemModel::rowsAboutToBeInserted, &h.presenter, [&] {
        ++callbacks;
        QCOMPARE(importer.begin("first", path), DownloadImportState::Pending);
        QCOMPARE(importer.pollImport("first"), DownloadImportState::Pending);
        QCOMPARE(importer.begin("second", path), DownloadImportState::Failed);
        h.poll();
        QVERIFY(h.writer.starts.empty());
    });
    QCOMPARE(importer.begin("first", path), DownloadImportState::Pending);
    QCOMPARE(callbacks, 1);
    QCOMPARE(h.model()->rowCount(), 1);
    QCOMPARE(h.writer.starts.size(), std::size_t{1});
    h.writer.complete(SessionSaveStatus::Saved);
    h.poll();
    QCOMPARE(importer.pollImport("first"), DownloadImportState::Committed);
}

void PlayerDownloadImportTests::durableRealServiceWaitsForPersistenceAndDrainsDuringShutdown() {
    DurableHarness h;
    PlayerDownloadImport importer(h.presenter, {}, PlayerDownloadImportPersistence::Session);
    ImportExecution execution;
    DownloadService service(execution, importer, "durable-service");
    const auto admitted = service.submit(request());
    QCOMPARE(admitted.error, DownloadSubmissionError::None);
    service.tick(0);
    execution.publish(admitted.job_ids.front(), publishedPath());
    service.tick(1);
    QCOMPARE(service.snapshot().front().state, DownloadJobState::Importing);
    QCOMPARE(h.model()->rowCount(), 1);
    service.shutdown();
    QVERIFY(!service.stopped());
    QVERIFY(execution.cancellations.empty());
    service.tick(2);
    QCOMPARE(service.snapshot().front().state, DownloadJobState::Importing);
    h.writer.complete(SessionSaveStatus::Saved);
    h.poll();
    service.tick(3);
    QCOMPARE(service.snapshot().front().state, DownloadJobState::Completed);
    QVERIFY(service.stopped());
    QCOMPARE(execution.starts.size(), std::size_t{1});
    QCOMPARE(h.model()->rowCount(), 1);
    QVERIFY(h.backend.loads.empty());
}

void PlayerDownloadImportTests::durableRestartReplaysDoNotReadOrLoadPublishedFiles() {
    SessionSnapshot saved;
    saved.queue.items.push_back({.occurrence_id = LocalQueueOccurrenceId{1}, .source_path = publishedPath()});
    saved.download_import_receipts.push_back({.job_id = "first", .source_path = publishedPath()});
    DurableHarness h(saved, SessionLoadStatus::Loaded, false);
    PlayerDownloadImport importer(h.presenter, {}, PlayerDownloadImportPersistence::Session);
    QCOMPARE(h.model()->rowCount(), 0);
    QCOMPARE(importer.pollImport("first"), DownloadImportState::Committed);
    QCOMPARE(importer.begin("first", publishedPath()), DownloadImportState::Committed);
    QVERIFY(h.backend.loads.empty());
    QVERIFY(h.metadata.requests.empty());
    QVERIFY(h.writer.starts.empty());
    QCOMPARE(importer.begin("new", publishedPath(u8"New.mp3")), DownloadImportState::Pending);
    QCOMPARE(h.model()->rowCount(), 1);
    QVERIFY(h.writer.active);
    QCOMPARE(h.writer.active->snapshot.download_import_receipts.size(), std::size_t{2});
    QCOMPARE(h.writer.active->snapshot.queue.items.size(), std::size_t{1});
}

#ifdef Q_OS_WIN
void PlayerDownloadImportTests::durableRealStoreKeepsQueueAndReceiptsTogether_data() {
    QTest::addColumn<int>("fault");
    QTest::newRow("saved") << -1;
    QTest::newRow("before-primary-commit") << static_cast<int>(SessionStoreFaultPoint::BeforePrimaryCommit);
    QTest::newRow("after-primary-commit") << static_cast<int>(SessionStoreFaultPoint::AfterPrimaryCommit);
}
void PlayerDownloadImportTests::durableRealStoreKeepsQueueAndReceiptsTogether() {
    QFETCH(int, fault);
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QtJsonWindowsSessionStore store(root.path(), {.inject_fault = [fault](SessionStoreFaultPoint point) {
                                        return fault >= 0 && static_cast<int>(point) == fault;
                                    }});
    {
        ImportPlatform platform;
        ImportBackend backend;
        ImportMetadata metadata;
        ImportPreferencesWriter preferences_writer;
        PreferencesAutosave preferences_autosave(preferences_writer, {.restore_last_session = false});
        SingleWorkerSessionWriter writer(store);
        SessionAutosave autosave(writer, {});
        PlayerPresenter presenter(
            platform, backend, metadata, [] { return std::vector<std::filesystem::path>{}; },
            PlayerPresenterPreferences{.initial = {.restore_last_session = false},
                                       .autosave = &preferences_autosave,
                                       .session = {.autosave = &autosave}});
        PlayerDownloadImport importer(presenter, {}, PlayerDownloadImportPersistence::Session);
        QCOMPARE(importer.begin("real-store", publishedPath()), DownloadImportState::Pending);
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                     PlayerPresenterTestAccess::pollPersistence(presenter);
                                     return importer.pollImport("real-store") != DownloadImportState::Pending;
                                 })(),
                                 5'000);
        QCOMPARE(importer.pollImport("real-store"),
                 fault < 0 ? DownloadImportState::Committed : DownloadImportState::Failed);
        QCOMPARE(presenter.queueModelObject()->rowCount(), 1);
        QVERIFY(backend.loads.empty());
        QVERIFY(writer.shutdown());
    }
    const auto loaded = store.load();
    if (fault == static_cast<int>(SessionStoreFaultPoint::BeforePrimaryCommit)) {
        QCOMPARE(loaded.status, SessionLoadStatus::Missing);
        QVERIFY(loaded.snapshot.queue.items.empty());
        QVERIFY(loaded.snapshot.download_import_receipts.empty());
    } else {
        QCOMPARE(loaded.status, SessionLoadStatus::Loaded);
        QCOMPARE(loaded.snapshot.queue.items.size(), std::size_t{1});
        QCOMPARE(loaded.snapshot.download_import_receipts.size(), std::size_t{1});
    }
    DurableHarness restarted(loaded.snapshot, loaded.status, false);
    PlayerDownloadImport importer(restarted.presenter, {}, PlayerDownloadImportPersistence::Session);
    RecoveredImportObserver observed(importer);
    ImportExecution execution;
    RecoveredImportIdentity identity;
    RecoveredImportCheckpointIo io;
    const auto input = request();
    io.checkpoint = {.receipts = {{.request_id = input.request_id,
                                   .fingerprint = identity.fingerprint(input),
                                   .job_ids = {"real-store"},
                                   .retained_request_bytes = input.request_id.size() + input.items[0].url.size() +
                                                             input.items[0].title.size()}},
                     .jobs = {{.job_id = "real-store", .title = input.items[0].title}}};
    QVERIFY(validDownloadCheckpoint(io.checkpoint));
    DownloadService service(execution, observed, "fresh-owner", {}, &identity);
    DownloadAdmissionController owner(service, io, loaded);
    QVERIFY(owner.start());
    owner.tick(0);
    QCOMPARE(owner.state(), DownloadAdmissionControllerState::Ready);
    QCOMPARE(owner.recoveryReport().status, DownloadRecoveryStatus::Ready);
    const bool saved_import = loaded.status == SessionLoadStatus::Loaded;
    QCOMPARE(service.snapshot().size(), std::size_t{1});
    QCOMPARE(service.snapshot()[0].state, saved_import ? DownloadJobState::Imported : DownloadJobState::Interrupted);
    QVERIFY(service.snapshot()[0].completed_mp3.empty());
    QCOMPARE(owner.recoveryReport().items.size(), std::size_t{1});
    QCOMPARE(owner.recoveryReport().items[0].disposition,
             saved_import ? DownloadRecoveryDisposition::Imported : DownloadRecoveryDisposition::Interrupted);
    QCOMPARE(owner.begin(input).result.job_ids, std::vector<std::string>{"real-store"});
    owner.tick(1);
    QCOMPARE(observed.calls, 0);
    QVERIFY(execution.starts.empty());
    QVERIFY(io.saves.empty());
    QCOMPARE(restarted.model()->rowCount(), 0);
    QVERIFY(restarted.writer.starts.empty());
    QVERIFY(restarted.backend.loads.empty());
    QVERIFY(restarted.metadata.requests.empty());
    owner.shutdown();
    owner.tick(2);
    owner.tick(3);
    QVERIFY(owner.stopped());
    QCOMPARE(owner.saveStatus(), std::optional{DownloadCheckpointSaveStatus::Saved});
    QCOMPARE(io.saves.size(), std::size_t{1});
    QCOMPARE(io.saves[0].jobs[0].state, saved_import ? DownloadJobState::Imported : DownloadJobState::Interrupted);
    QCOMPARE(io.saves[0].jobs[0].completed_mp3, saved_import ? publishedPath() : std::filesystem::path{});
    QCOMPARE(observed.calls, 0);
    if (saved_import) {
        QCOMPARE(importer.begin("real-store", publishedPath()), DownloadImportState::Committed);
        QCOMPARE(restarted.model()->rowCount(), 0);
        QVERIFY(restarted.writer.starts.empty());
        QVERIFY(restarted.backend.loads.empty());
    }
}
#endif

void PlayerDownloadImportTests::durableWriterReentryCannotStageAnotherWrite_data() {
    QTest::addColumn<bool>("another_wrapper");
    QTest::newRow("same-wrapper") << false;
    QTest::newRow("another-wrapper-same-presenter") << true;
}
void PlayerDownloadImportTests::durableWriterReentryCannotStageAnotherWrite() {
    QFETCH(bool, another_wrapper);
    DurableHarness h;
    PlayerDownloadImport importer(h.presenter, {}, PlayerDownloadImportPersistence::Session);
    PlayerDownloadImport second(h.presenter, {}, PlayerDownloadImportPersistence::Session);
    bool entered = false;
    h.writer.on_start = [&] {
        entered = true;
        auto& reentrant = another_wrapper ? second : importer;
        QCOMPARE(reentrant.begin("first", publishedPath()), DownloadImportState::Pending);
        QCOMPARE(reentrant.begin("new", publishedPath(u8"New.mp3")), DownloadImportState::Failed);
        QCOMPARE(h.writer.attempts, 1);
        QCOMPARE(h.model()->rowCount(), 1);
    };
    QCOMPARE(importer.begin("first", publishedPath()), DownloadImportState::Pending);
    QVERIFY(entered);
    QCOMPARE(h.writer.attempts, 1);
    QCOMPARE(h.model()->rowCount(), 1);
    h.writer.complete(SessionSaveStatus::Saved);
    h.poll();
    QCOMPARE(importer.pollImport("first"), DownloadImportState::Committed);
}

void PlayerDownloadImportTests::durableWriterReentryCannotBypassTheGuardThroughVolatileOrDirectAppend() {
    DurableHarness h;
    PlayerDownloadImport importer(h.presenter, {}, PlayerDownloadImportPersistence::Session);
    PlayerDownloadImport volatile_importer(h.presenter);
    h.writer.on_start = [&] {
        QVERIFY(!volatile_importer.accept("volatile", publishedPath(u8"Volatile.mp3")));
        bool committed = true;
        QVERIFY(!h.presenter.appendPublishedAudio(publishedPath(u8"Direct.mp3"), committed));
        QVERIFY(!committed);
        QCOMPARE(h.model()->rowCount(), 1);
    };
    QCOMPARE(importer.begin("first", publishedPath()), DownloadImportState::Pending);
    QCOMPARE(h.model()->rowCount(), 1);
    h.writer.complete(SessionSaveStatus::Saved);
    h.poll();
    QCOMPARE(importer.pollImport("first"), DownloadImportState::Committed);
}

} // namespace music_player::m0::test

QTEST_MAIN(music_player::m0::test::PlayerDownloadImportTests)

#include "PlayerDownloadImportTests.moc"
