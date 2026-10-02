#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "m0/application/DownloadService.hpp"
#include "presentation/PlayerDownloadImport.hpp"
#include "presentation/PlayerPresenter.hpp"

#include <QAbstractItemModelTester>
#include <QCoreApplication>
#include <QEvent>
#include <QMetaMethod>
#include <QSignalSpy>
#include <QStringList>
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

} // namespace music_player::m0::test

QTEST_MAIN(music_player::m0::test::PlayerDownloadImportTests)

#include "PlayerDownloadImportTests.moc"
