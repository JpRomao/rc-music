#include "../../qt/support/YoutubeResolverTestSupport.hpp"
#include "adapters/DownloadJobsModel.hpp"
#include "adapters/NativeYoutubeAudioSession.hpp"
#include "adapters/WindowsDownloadExecution.hpp"
#include "adapters/YoutubeAudioCodec.hpp"
#include "adapters/YoutubeDownloadExecution.hpp"
#include "m0/application/PreferencesAutosave.hpp"
#include "m0/application/SessionAutosave.hpp"
#include "m0/ports/AsyncDownloadSubmission.hpp"
#include "presentation/PlayerPresenter.hpp"
#include "presentation/PlayerPresenterTestAccess.hpp"
#include "presentation/WindowsPlayerDownloads.hpp"

#include <QElapsedTimer>
#include <QTest>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <utility>

using namespace music_player::m0;

namespace {
DownloadRequest request() {
    return {"01234567-89ab-4cde-8fab-0123456789ab",
            {{"https://synthetic.example.test/known.mp3", "Synthetic", DownloadMediaKind::Audio}}};
}
class Platform final : public PlatformIntegration {
  public:
    bool trayAvailable() const noexcept override { return true; }
    void hideWindow() override {}
    void minimizeWindow() override {}
    void showAndActivateWindow() override {}
    void requestExit() override { ++exits; }
    int exits{0};
};
class Backend final : public PlaybackBackend {
  public:
    const PlaybackSnapshot& snapshot() const noexcept override { return state; }
    void setEventCallback(PlaybackEventCallback) override {}
    PlaybackCommandResult load(const std::filesystem::path&) override {
        ++loads;
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult play() override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult pause() override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult seekTo(std::int64_t) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult setVolume(double) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult setMuted(bool) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult unload() override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult shutdown() override { return PlaybackCommandResult::Accepted; }
    PlaybackSnapshot state{};
    int loads{0};
};
class Metadata final : public MetadataReader {
  public:
    MetadataCommandResult startRead(MetadataReadRequest) override { return MetadataCommandResult::Accepted; }
    MetadataCommandResult cancelRead(MetadataGeneration) override { return MetadataCommandResult::Accepted; }
    MetadataPollResult poll(MetadataGeneration) override { return {}; }
    std::optional<MetadataReadCompletion> tryTakeCompletion() override { return std::nullopt; }
    MetadataCommandResult shutdown() override { return MetadataCommandResult::Accepted; }
};
class PreferencesWriterStub final : public PreferencesWriter {
  public:
    PreferencesWriterCommand start(PreferencesWriteRequest) override { return PreferencesWriterCommand::Accepted; }
    std::optional<PreferencesWriteCompletion> poll() override { return std::nullopt; }
    std::optional<PreferencesWriteCompletion> flush(PreferencesWriteRequest value) override {
        return PreferencesWriteCompletion{.revision = value.revision, .status = PreferencesSaveStatus::Saved};
    }
    bool shutdown() override { return true; }
};
class SessionWriterStub final : public SessionWriter {
  public:
    SessionWriterCommand start(SessionWriteRequest value) override {
        if (active)
            return SessionWriterCommand::Busy;
        active = std::move(value);
        return SessionWriterCommand::Accepted;
    }
    std::optional<SessionWriteCompletion> poll() override { return std::exchange(completion, std::nullopt); }
    std::optional<SessionWriteCompletion> flush(SessionWriteRequest value) override {
        return SessionWriteCompletion{.revision = value.revision, .status = SessionSaveStatus::Saved};
    }
    bool shutdown() override { return !active; }
    void save() {
        disk = active->snapshot;
        completion = SessionWriteCompletion{.revision = active->revision, .status = SessionSaveStatus::Saved};
        active.reset();
    }
    std::optional<SessionWriteRequest> active;
    std::optional<SessionWriteCompletion> completion;
    SessionSnapshot disk{};
};
struct CheckpointState final {
    std::mutex mutex;
    std::condition_variable changed;
    bool allow_save{true};
    std::atomic<bool> fail_save{false};
    std::atomic<int> loads{0}, saves{0};
    std::atomic<bool> destroyed{false};
    std::optional<DownloadCheckpoint> disk;
    void allow(bool value) {
        {
            const std::lock_guard lock(mutex);
            allow_save = value;
        }
        changed.notify_all();
    }
};
class CheckpointStore final : public DownloadCheckpointStore {
  public:
    explicit CheckpointStore(std::shared_ptr<CheckpointState> value) : state_(std::move(value)) {}
    ~CheckpointStore() override { state_->destroyed = true; }
    DownloadCheckpointLoadResult load() override {
        ++state_->loads;
        const std::lock_guard lock(state_->mutex);
        return state_->disk ? DownloadCheckpointLoadResult{state_->disk, DownloadCheckpointLoadStatus::Loaded}
                            : DownloadCheckpointLoadResult{};
    }
    DownloadCheckpointSaveStatus save(const DownloadCheckpoint& value) override {
        ++state_->saves;
        std::unique_lock lock(state_->mutex);
        state_->changed.wait(lock, [&] { return state_->allow_save; });
        if (state_->fail_save)
            return DownloadCheckpointSaveStatus::Failed;
        state_->disk = value;
        return DownloadCheckpointSaveStatus::Saved;
    }

  private:
    std::shared_ptr<CheckpointState> state_;
};
struct TaskState final {
    std::atomic<int> starts{0};
    std::atomic<bool> complete{true}, cancelled{false}, allow_cancel_terminal{true};
};
class Task final : public WindowsDownloadTask {
  public:
    explicit Task(std::shared_ptr<TaskState> value) : state_(std::move(value)) {}
    bool start(const std::string& id, const DownloadItem&, qint64) override {
        ++state_->starts;
        id_ = id;
        return true;
    }
    bool startResolvedYoutube(std::shared_ptr<const ResolvedYoutubeAudioCapability> cap, qint64 now, qint64) override {
        return cap && start(cap->jobId(), {}, now);
    }
    void cancel() override { state_->cancelled = true; }
    DownloadExecutionUpdate poll(qint64) override {
        if (state_->cancelled && state_->allow_cancel_terminal)
            return {.job_id = id_, .state = DownloadExecutionState::Cancelled};
        if (!state_->cancelled && state_->complete)
            return {.job_id = id_,
                    .state = DownloadExecutionState::Published,
                    .phase = DownloadPhase::Publishing,
                    .published_mp3 = std::filesystem::path{L"C:/synthetic-published/final.mp3"}};
        return {.job_id = id_};
    }

  private:
    std::shared_ptr<TaskState> state_;
    std::string id_;
};
struct Harness final {
    Platform platform;
    Backend backend;
    Metadata metadata;
    SessionWriterStub session_writer;
    PreferencesWriterStub preferences_writer;
    PreferencesAutosave preferences{preferences_writer, {}};
    SessionAutosave autosave{session_writer, {}};
    PlayerPresenter presenter{platform,
                              backend,
                              metadata,
                              [] { return std::vector<std::filesystem::path>{}; },
                              {.autosave = &preferences, .session = {.autosave = &autosave}}};
    std::shared_ptr<CheckpointState> checkpoint{std::make_shared<CheckpointState>()};
    std::shared_ptr<TaskState> task{std::make_shared<TaskState>()};
    std::unique_ptr<WindowsPlayerDownloads> owner;
    explicit Harness(bool youtube = false) {
        WindowsDownloadExecution::TaskFactory destination = [state = task] { return std::make_unique<Task>(state); };
        auto execution =
            youtube ? makeWindowsMediaAndYoutubeExecution(
                          [] { return std::make_unique<music_player::m0::test::FixtureYoutubeResolverProcess>(); },
                          destination, [] { return qint64{1'999'999'000}; })
                    : std::make_unique<WindowsDownloadExecution>(destination);
        owner = std::make_unique<WindowsPlayerDownloads>(
            presenter, SessionLoadResult{}, std::move(execution),
            [state = checkpoint] { return std::make_unique<CheckpointStore>(state); },
            WindowsPlayerDownloadsOptions{.job_namespace = "owner", .binding = {.automatic_polling = false}});
    }
    ~Harness() {
        // Failed assertions must not strand a deliberately held worker.
        checkpoint->allow(true);
        task->allow_cancel_terminal = true;
        task->complete = true;
    }
    void poll() {
        music_player::m0::test::PlayerPresenterTestAccess::pollPersistence(presenter);
        owner->poll();
    }
    bool wait(const std::function<bool()>& ready) {
        QElapsedTimer timer;
        timer.start();
        do {
            poll();
            if (ready())
                return true;
            QTest::qWait(5);
        } while (timer.elapsed() < 4000);
        return false;
    }
    bool ready() {
        if (!owner->start())
            return false;
        return wait([&] {
            return checkpoint->loads == 1 &&
                   owner->binding()->statusText() == DownloadApplicationBinding::tr("Histórico de downloads carregado");
        });
    }
    QString state(int row = 0) const {
        auto* model = owner->binding()->jobsModel();
        return model->data(model->index(row, 0), DownloadJobsModel::StateRole).toString();
    }
    bool drain() {
        static_cast<void>(presenter.requestExit());
        return wait([&] {
            if (session_writer.active)
                session_writer.save();
            return owner->stopped();
        });
    }
};
} // namespace

class WindowsPlayerDownloadsTests final : public QObject {
    Q_OBJECT
  private slots:
    void youtubeSessionSurvivesDisconnectAndImportsOnlyAfterBothSavedAcknowledgements() {
        Harness h(true);
        QVERIFY(h.ready());
        h.checkpoint->allow(false);
        const YoutubeAudioRequest selected{"01234567-89ab-4cde-8fab-0123456789ab",
                                           {{"aB0-_Cd1EF2", "Synthetic song", "Artist", "", true}}};
        const auto frame = encodeNativeMessageFrame(encodeYoutubeAudioRequest(selected));
        const QString origin = QStringLiteral("chrome-extension://abcdefghijklmnopabcdefghijklmnop/");
        {
            NativeYoutubeAudioSession observer(origin, origin, h.owner->youtubeSubmission());
            QCOMPARE(observer.receive(frame, 1).status, NativeSessionStatus::Awaiting);
            QCOMPARE(observer.finishInput(2).status, NativeSessionStatus::Closed);
        }
        QVERIFY(h.wait([&] { return h.checkpoint->saves == 1; }));
        QCOMPARE(h.task->starts.load(), 0);
        QCOMPARE(h.presenter.queueModelObject()->rowCount(), 0);
        NativeYoutubeAudioSession reconnected(origin, origin, h.owner->youtubeSubmission());
        QCOMPARE(reconnected.receive(frame, 3).status, NativeSessionStatus::Awaiting);
        h.checkpoint->allow(true);
        QVERIFY(h.wait([&] { return h.session_writer.active.has_value(); }));
        const auto reply = reconnected.poll(4);
        QCOMPARE(reply.status, NativeSessionStatus::Replied);
        const auto ack = decodeYoutubeAudioResponse(reply.response_frame.mid(4), selected.request_id, 1);
        QVERIFY(ack);
        QCOMPARE(ack->error, DownloadSubmissionError::None);
        QCOMPARE(h.task->starts.load(), 1);
        QCOMPARE(h.presenter.queueModelObject()->rowCount(), 1);
        QCOMPARE(h.state(), QStringLiteral("importing"));
        QCOMPARE(h.autosave.savedRevision(), SessionRevision{0});
        QCOMPARE(h.backend.loads, 0);
        QVERIFY(!h.presenter.hasCurrentTrack());
        h.session_writer.save();
        QVERIFY(h.wait([&] { return h.state() == QStringLiteral("completed"); }));
        QCOMPARE(h.session_writer.disk.download_import_receipts.size(), std::size_t{1});
        const auto replay = h.owner->youtubeSubmission().begin(selected);
        QCOMPARE(replay.state, DownloadAdmissionState::Completed);
        QCOMPARE(replay.result.job_ids, ack->job_ids);
        QCOMPARE(h.task->starts.load(), 1);
        QCOMPARE(h.presenter.queueModelObject()->rowCount(), 1);
        QVERIFY(h.drain());
    }
    void refusesMissingExecutionOrCheckpointFactory() {
        Harness h;
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                 WindowsPlayerDownloads(h.presenter, {}, nullptr, [state = h.checkpoint] {
                                     return std::make_unique<CheckpointStore>(state);
                                 }));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                 WindowsPlayerDownloads(h.presenter, {}, std::make_unique<WindowsDownloadExecution>([] {
                                                            return std::unique_ptr<WindowsDownloadTask>{};
                                                        }),
                                                        {}));
    }
    void admissionWaitsForCheckpointAndImportWaitsForMatchingSessionSaved() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(!h.owner->start());
        QVERIFY(!h.owner->stopped());
        h.checkpoint->allow(false);
        QVERIFY(
            h.wait([&] { return h.owner->submission().begin(request()).state == DownloadAdmissionState::Pending; }));
        QVERIFY(h.wait([&] { return h.checkpoint->saves == 1; }));
        QVERIFY(!h.owner->submission().poll(request().request_id));
        QCOMPARE(h.task->starts.load(), 0);
        QCOMPARE(h.presenter.queueModelObject()->rowCount(), 0);
        h.checkpoint->allow(true);
        QVERIFY(h.wait([&] { return h.session_writer.active.has_value(); }));
        QCOMPARE(h.task->starts.load(), 1);
        QCOMPARE(h.presenter.queueModelObject()->rowCount(), 1);
        QCOMPARE(h.state(), QStringLiteral("importing"));
        QCOMPARE(h.autosave.savedRevision(), SessionRevision{0});
        QCOMPARE(h.session_writer.active->snapshot.download_import_receipts.size(), std::size_t{1});
        QCOMPARE(h.session_writer.active->snapshot.queue.items.size(), std::size_t{1});
        QCOMPARE(h.backend.loads, 0);
        QVERIFY(!h.presenter.hasCurrentTrack());
        h.session_writer.save();
        QVERIFY(h.wait([&] { return h.state() == QStringLiteral("completed"); }));
        QCOMPARE(h.session_writer.disk.download_import_receipts.size(), std::size_t{1});
        const auto repeated = h.owner->submission().begin(request());
        QCOMPARE(repeated.state, DownloadAdmissionState::Completed);
        QCOMPARE(repeated.result.error, DownloadSubmissionError::None);
        for (int index = 0; index < 5; ++index)
            h.poll();
        QCOMPARE(h.task->starts.load(), 1);
        QCOMPARE(h.presenter.queueModelObject()->rowCount(), 1);
        QVERIFY(h.drain());
        QVERIFY(h.checkpoint->destroyed);
        QVERIFY(!h.owner->start());
    }
    void finalCheckpointFailureRequiresExplicitRetryAndPhysicalDrain() {
        Harness h;
        QVERIFY(h.ready());
        h.checkpoint->fail_save = true;
        QVERIFY(h.presenter.requestExit());
        QVERIFY(h.wait([&] { return h.owner->binding()->canRetryFinalSave(); }));
        QVERIFY(!h.owner->stopped());
        QCOMPARE(h.platform.exits, 0);
        h.checkpoint->fail_save = false;
        QVERIFY(h.owner->binding()->retryFinalSave());
        QVERIFY(h.wait([&] { return h.owner->stopped(); }));
        QVERIFY(h.presenter.requestExit());
        QCOMPARE(h.platform.exits, 1);
        QVERIFY(h.checkpoint->destroyed);
        QCOMPARE(h.task->starts.load(), 0);
    }
    void exitWaitsForTaskAcknowledgementWithoutStartingSuccessorOrImporting() {
        Harness h;
        h.task->complete = false;
        h.task->allow_cancel_terminal = false;
        QVERIFY(h.ready());
        auto batch = request();
        batch.items.push_back(batch.items.front());
        QVERIFY(h.wait([&] { return h.owner->submission().begin(batch).state == DownloadAdmissionState::Pending; }));
        QVERIFY(h.wait([&] { return h.task->starts == 1; }));
        QVERIFY(h.presenter.requestExit());
        QVERIFY(h.wait([&] { return h.task->cancelled.load(); }));
        QVERIFY(!h.owner->stopped());
        QCOMPARE(h.platform.exits, 0);
        QCOMPARE(h.presenter.queueModelObject()->rowCount(), 0);
        h.task->allow_cancel_terminal = true;
        QVERIFY(h.drain());
        QCOMPARE(h.task->starts.load(), 1);
        QCOMPARE(h.state(0), QStringLiteral("cancelled"));
        QCOMPARE(h.state(1), QStringLiteral("cancelled"));
        QCOMPARE(h.owner->submission().begin(batch).result.error, DownloadSubmissionError::NotAvailable);
        QVERIFY(h.session_writer.disk.download_import_receipts.empty());
    }
};
QTEST_MAIN(WindowsPlayerDownloadsTests)
#include "WindowsPlayerDownloadsTests.moc"
