#include "adapters/DownloadJobsModel.hpp"
#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "m0/application/DownloadAdmissionController.hpp"
#include "presentation/DownloadApplicationBinding.hpp"
#include "presentation/PlayerPresenter.hpp"
#include "presentation/PlayerPresenterTestAccess.hpp"

#include <QAbstractItemModel>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>

#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace music_player::m0::test {
namespace {
const std::string request_id = "01234567-89ab-4cde-8fab-0123456789ab";
DownloadRequest input() {
    return {.request_id = request_id,
            .items = {{.url = "https://synthetic.example.test/audio.mp3?secret=never-display",
                       .title = "<b>Literal synthetic title</b>",
                       .kind = DownloadMediaKind::Audio}}};
}
std::filesystem::path path() { return std::filesystem::path{L"C:/rc-music-synthetic/known.mp3"}; }
class Platform final : public PlatformIntegration {
  public:
    bool trayAvailable() const noexcept override { return true; }
    void hideWindow() override {}
    void minimizeWindow() override {}
    void showAndActivateWindow() override {}
    void requestExit() override { ++exits; }
    int exits{0};
};
class Execution final : public DownloadExecution {
  public:
    bool start(const std::string& id, const DownloadItem&) override {
        ++starts;
        update = {.job_id = id};
        return true;
    }
    void cancel(std::string_view) override { ++cancellations; }
    DownloadExecutionUpdate poll(std::string_view) override { return update; }
    void shutdown() { ++shutdowns; }
    bool stopped() const {
        if (throw_probe)
            throw std::runtime_error{"private worker error"};
        return shutdowns != 0 && physical_stopped;
    }
    int starts{0};
    int cancellations{0};
    int shutdowns{0};
    bool physical_stopped{false};
    bool throw_probe{false};
    DownloadExecutionUpdate update{};
};
class Importer final : public DownloadImport {
  public:
    bool accept(const std::string&, const std::filesystem::path&) override {
        ++calls;
        if (on_accept)
            on_accept();
        return true;
    }
    int calls{0};
    std::function<void()> on_accept{};
    std::function<void()> on_poll{};
    bool pending{false};
    DownloadImportState polled{DownloadImportState::Pending};
    DownloadImportState begin(const std::string& id, const std::filesystem::path& file) override {
        const bool accepted = accept(id, file);
        return pending ? DownloadImportState::Pending
                       : (accepted ? DownloadImportState::Committed : DownloadImportState::Failed);
    }
    DownloadImportState pollImport(std::string_view) override {
        if (on_poll)
            on_poll();
        return polled;
    }
};
class Identity final : public DownloadRequestIdentity {
  public:
    std::string fingerprint(const DownloadRequest&) override { return std::string(64, 'a'); }
};
class Io final : public DownloadCheckpointIo {
  public:
    DownloadCheckpointIoCommand startLoad(const std::uint64_t id) override {
        loads.push_back(id);
        return DownloadCheckpointIoCommand::Accepted;
    }
    DownloadCheckpointIoCommand startSave(const std::uint64_t id, const DownloadCheckpoint& snapshot) override {
        saves.push_back({id, snapshot});
        return DownloadCheckpointIoCommand::Accepted;
    }
    std::optional<DownloadCheckpointIoResult> poll(std::uint64_t) override {
        ++polls;
        if (on_poll)
            on_poll();
        return std::exchange(completion, std::nullopt);
    }
    void shutdown() override { ++shutdowns; }
    bool stopped() const override { return shutdowns != 0 && physical_stopped; }
    void loaded(DownloadCheckpointLoadResult loaded = {}) {
        completion = DownloadCheckpointIoResult{
            .operation_id = loads.back(), .operation = DownloadCheckpointOperation::Load, .load = std::move(loaded)};
    }
    void saved(DownloadCheckpointSaveStatus status = DownloadCheckpointSaveStatus::Saved) {
        completion = DownloadCheckpointIoResult{
            .operation_id = saves.back().first, .operation = DownloadCheckpointOperation::Save, .save_status = status};
    }
    std::vector<std::uint64_t> loads{};
    std::vector<std::pair<std::uint64_t, DownloadCheckpoint>> saves{};
    std::optional<DownloadCheckpointIoResult> completion{};
    std::function<void()> on_poll{};
    int polls{0};
    int shutdowns{0};
    bool physical_stopped{false};
};
struct Harness final {
    Platform platform;
    PlayerPresenter presenter;
    Execution execution;
    Importer importer;
    Identity identity;
    Io io;
    DownloadService service;
    DownloadAdmissionController controller;
    std::int64_t now{0};
    int clock_reads{0};
    bool throw_clock{false};
    DownloadApplicationBinding binding;
    explicit Harness(SessionLoadResult session = {})
        : presenter(
              FixtureRepository{}.load(QString::fromUtf8(M0_TEST_FIXTURE_FILE), QString::fromUtf8(M0_TEST_QUEUE_FILE)),
              platform),
          service(execution, importer, "fresh", {}, &identity),
          controller(service, io, session, DownloadFinalSavePolicy::HoldForDecision),
          binding(presenter, controller, service,
                  {.shutdown = [&] { execution.shutdown(); }, .stopped = [&] { return execution.stopped(); }},
                  {.now_ms =
                       [&] {
                           ++clock_reads;
                           if (throw_clock)
                               throw std::runtime_error{"private clock error"};
                           return now;
                       },
                   .automatic_polling = false}) {}
    bool ready() {
        if (!binding.start() || io.loads.size() != 1)
            return false;
        io.loaded();
        binding.poll();
        return controller.state() == DownloadAdmissionControllerState::Ready;
    }
    void playerPoll() { PlayerPresenterTestAccess::pollPersistence(presenter); }
};
} // namespace

class DownloadApplicationBindingTests final : public QObject {
    Q_OBJECT
  private slots:
    void startsAndProjectsReconciledHistoryWithoutExecutionOrImport() {
        const SessionLoadResult session{
            .snapshot = {.download_import_receipts = {{.job_id = "old-1", .source_path = path()}}},
            .status = SessionLoadStatus::Loaded};
        Harness h(session);
        QVERIFY(h.binding.start());
        const DownloadCheckpoint checkpoint{
            .receipts = {{.request_id = request_id,
                          .fingerprint = std::string(64, 'a'),
                          .job_ids = {"old-1"},
                          .retained_request_bytes = 128}},
            .jobs = {{.job_id = "old-1", .title = "<b>Literal title</b>", .completed_units = 2, .total_units = 9}}};
        h.io.loaded({.checkpoint = checkpoint, .status = DownloadCheckpointLoadStatus::Loaded});
        h.binding.poll();
        auto* model = h.binding.jobsModel();
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(model->data(model->index(0, 0), DownloadJobsModel::StateRole).toString(), QStringLiteral("imported"));
        QCOMPARE(model->data(model->index(0, 0), DownloadJobsModel::TitleRole).toString(),
                 QStringLiteral("<b>Literal title</b>"));
        QVERIFY(model->data(model->index(0, 0), DownloadJobsModel::OutputPathRole).toString().isEmpty());
        QCOMPARE(h.execution.starts, 0);
        QCOMPARE(h.importer.calls, 0);
        QVERIFY(!h.binding.canRetryStartup());
        QVERIFY(!h.binding.statusText().isEmpty());
        QVERIFY(!h.binding.start());
    }
    void startupFailureAndUnhealthySessionStayExplicitAndNeverRunJobs() {
        Harness h({.status = SessionLoadStatus::RecoveredFromBackup});
        QVERIFY(h.binding.start());
        h.io.loaded();
        h.binding.poll();
        QCOMPARE(h.controller.state(), DownloadAdmissionControllerState::Blocked);
        QVERIFY(!h.binding.warningText().isEmpty());
        QVERIFY(h.binding.canRetryStartup());
        QVERIFY(h.binding.retryStartup());
        h.io.loaded();
        h.binding.poll();
        QCOMPARE(h.controller.state(), DownloadAdmissionControllerState::Blocked);
        QCOMPARE(h.execution.starts, 0);
        QCOMPARE(h.importer.calls, 0);
        QVERIFY(!h.binding.retryAdmission());
        QVERIFY(!h.binding.retryFinalSave());
    }
    void admissionFailureRetriesTheSameCheckpointBeforeStartingExecution() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(h.controller.begin(input()).state == DownloadAdmissionState::Pending);
        h.io.saved(DownloadCheckpointSaveStatus::Failed);
        h.binding.poll();
        QVERIFY(h.binding.canRetryAdmission());
        QVERIFY(!h.binding.warningText().isEmpty());
        QCOMPARE(h.execution.starts, 0);
        const auto first = h.io.saves.back().second;
        QVERIFY(h.binding.retryAdmission());
        QCOMPARE(h.io.saves.back().second, first);
        QVERIFY(!h.binding.canRetryAdmission());
        h.io.saved();
        h.binding.poll();
        QCOMPARE(h.execution.starts, 1);
        QVERIFY(!h.binding.warningText().contains(QStringLiteral("never-display")));
        QCOMPARE(h.binding.jobsModel()->rowCount(), 1);
    }
    void exitNeedsSavedFinalHistoryAndBothPhysicalDrains() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(h.presenter.requestExit());
        QCOMPARE(h.execution.shutdowns, 1);
        QCOMPARE(h.io.saves.size(), std::size_t{1});
        h.io.saved();
        h.binding.poll();
        h.playerPoll();
        QCOMPARE(h.platform.exits, 0);
        QVERIFY(h.presenter.exiting());
        h.io.physical_stopped = true;
        h.binding.poll();
        h.playerPoll();
        QCOMPARE(h.platform.exits, 0);
        h.execution.physical_stopped = true;
        h.binding.poll();
        h.playerPoll();
        QCOMPARE(h.platform.exits, 1);
    }
    void physicalWorkerDrainAndProbeFailureRemainVisibleUntilAHealthyObservation() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(h.presenter.requestExit());
        h.io.saved();
        h.io.physical_stopped = true;
        h.binding.poll();
        QCOMPARE(h.controller.state(), DownloadAdmissionControllerState::Stopped);
        QCOMPARE(h.binding.statusText(), QStringLiteral("Finalizando encerramento dos downloads"));
        h.execution.throw_probe = true;
        h.binding.poll();
        QVERIFY(!h.binding.warningText().isEmpty());
        h.binding.poll();
        QVERIFY(!h.binding.warningText().isEmpty());
        QCOMPARE(h.platform.exits, 0);
        h.execution.throw_probe = false;
        h.execution.physical_stopped = true;
        h.binding.poll();
        QVERIFY(h.binding.warningText().isEmpty());
        QCOMPARE(h.binding.statusText(), QStringLiteral("Downloads encerrados"));
        h.playerPoll();
        QCOMPARE(h.platform.exits, 1);
    }
    void projectionSignalsCannotStackAnotherControllerOrClockPoll() {
        Harness h;
        QVERIFY(h.binding.start());
        h.io.loaded();
        connect(&h.binding, &DownloadApplicationBinding::stateChanged, &h.binding, [&] { h.binding.poll(); });
        h.binding.poll();
        QCOMPARE(h.clock_reads, 1);
        auto* model = h.binding.jobsModel();
        connect(model, &QAbstractItemModel::rowsInserted, &h.binding, [&] { h.binding.poll(); });
        QVERIFY(h.controller.begin(input()).state == DownloadAdmissionState::Pending);
        h.io.saved();
        h.binding.poll();
        QCOMPARE(h.clock_reads, 2);
        QCOMPARE(h.execution.starts, 1);
    }
    void finalSaveFailureReopensLocalIntentAndKeepsIoOpenForExplicitRetry() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(h.presenter.requestExit());
        h.io.saved(DownloadCheckpointSaveStatus::Failed);
        h.binding.poll();
        h.playerPoll();
        QCOMPARE(h.platform.exits, 0);
        QVERIFY(!h.presenter.exiting());
        QCOMPARE(h.io.shutdowns, 0);
        QVERIFY(h.binding.canRetryFinalSave());
        QVERIFY(h.binding.canCloseAfterFinalSaveFailure());
        QVERIFY(!h.binding.canRetryAdmission());
        const auto failed_snapshot = h.io.saves.back().second;
        QVERIFY(h.binding.retryFinalSave());
        QCOMPARE(h.io.saves.back().second, failed_snapshot);
        h.io.saved();
        h.io.physical_stopped = true;
        h.execution.physical_stopped = true;
        h.binding.poll();
        QVERIFY(h.presenter.requestExit());
        QCOMPARE(h.platform.exits, 1);
        QCOMPARE(h.controller.begin(input()).result.error, DownloadSubmissionError::NotAvailable);
    }
    void stoppedPlusFailedIsNotReadyWithoutAnExplicitAcceptedDecision() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(!h.binding.closeAfterFinalSaveFailure());
        QVERIFY(h.presenter.requestExit());
        h.io.saved(DownloadCheckpointSaveStatus::Failed);
        h.binding.poll();
        h.playerPoll();
        h.io.physical_stopped = true;
        h.execution.physical_stopped = true;
        // Bypassing the binding's explicit decision must not silently authorise exit.
        QVERIFY(h.controller.closeAfterFinalSaveFailure());
        h.binding.poll();
        static_cast<void>(h.presenter.requestExit());
        QCOMPARE(h.platform.exits, 0);
        QVERIFY(!h.presenter.exitCommitted());
    }
    void explicitCloseAfterFailureStillRequiresPhysicalDrain() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(h.presenter.requestExit());
        h.io.saved(DownloadCheckpointSaveStatus::Failed);
        h.binding.poll();
        h.playerPoll();
        QVERIFY(h.binding.closeAfterFinalSaveFailure());
        QVERIFY(!h.binding.closeAfterFinalSaveFailure());
        QVERIFY(h.presenter.requestExit());
        h.binding.poll();
        h.playerPoll();
        QCOMPARE(h.platform.exits, 0);
        h.io.physical_stopped = true;
        h.execution.physical_stopped = true;
        h.binding.poll();
        h.playerPoll();
        QCOMPARE(h.platform.exits, 1);
    }
    void blockedStartupPreservesHistoryAndCanExitAfterDrainWithoutWriting() {
        Harness h;
        QVERIFY(h.binding.start());
        h.io.loaded({.status = DownloadCheckpointLoadStatus::FutureVersion});
        h.binding.poll();
        QVERIFY(h.presenter.requestExit());
        QCOMPARE(h.io.saves.size(), std::size_t{0});
        h.io.physical_stopped = true;
        h.execution.physical_stopped = true;
        h.binding.poll();
        h.playerPoll();
        QCOMPARE(h.platform.exits, 1);
    }
    void reentrantPollingAndWrongThreadControlsDoNotAdvanceBorrowedWork() {
        Harness h;
        QVERIFY(h.binding.start());
        h.io.loaded();
        h.io.on_poll = [&] {
            h.binding.poll();
            QVERIFY(!h.binding.start());
            QVERIFY(!h.binding.retryStartup());
        };
        h.binding.poll();
        QCOMPARE(h.io.polls, 1);
        int previous = h.io.polls;
        bool result = true;
        std::thread worker([&] {
            h.binding.poll();
            result = h.binding.retryStartup();
        });
        worker.join();
        QVERIFY(!result);
        QCOMPARE(h.io.polls, previous);
    }
    void nestedImportExitClosesTheSameTickSuccessorStart() {
        Harness h;
        QVERIFY(h.ready());
        auto selected = input();
        selected.items.push_back({.url = "https://synthetic.example.test/second.mp3",
                                  .title = "Second synthetic item",
                                  .kind = DownloadMediaKind::Audio});
        QVERIFY(h.controller.begin(selected).state == DownloadAdmissionState::Pending);
        h.io.saved();
        h.binding.poll();
        QCOMPARE(h.execution.starts, 1);
        h.importer.pending = true;
        h.execution.update.state = DownloadExecutionState::Published;
        h.execution.update.phase = DownloadPhase::Publishing;
        h.execution.update.published_mp3 = path();
        h.binding.poll();
        QCOMPARE(h.importer.calls, 1);
        QCOMPARE(h.execution.starts, 1);
        h.importer.polled = DownloadImportState::Committed;
        h.importer.on_poll = [&] { QVERIFY(h.presenter.requestExit()); };
        h.binding.poll();
        QCOMPARE(h.execution.starts, 1);
        QCOMPARE(h.controller.begin(input()).result.error, DownloadSubmissionError::NotAvailable);
        h.binding.poll();
        QCOMPARE(h.execution.starts, 1);
    }
    void clockAndExecutionProbeExceptionsNeverAcknowledgeExit() {
        Harness h;
        QVERIFY(h.ready());
        h.throw_clock = true;
        h.binding.poll();
        QVERIFY(!h.binding.warningText().isEmpty());
        static_cast<void>(h.presenter.requestExit());
        QCOMPARE(h.platform.exits, 0);
        h.throw_clock = false;
        h.execution.throw_probe = true;
        h.execution.physical_stopped = true;
        h.io.physical_stopped = true;
        if (!h.io.saves.empty())
            h.io.saved();
        h.binding.poll();
        static_cast<void>(h.presenter.requestExit());
        QCOMPARE(h.platform.exits, 0);
        QVERIFY(!h.presenter.exitCommitted());
    }
    void secondBindingCannotReplaceTheRegisteredParticipant() {
        Harness h;
        QVERIFY(h.binding.start());
        h.io.loaded();
        h.binding.poll();
        // A second owner cannot replace a registered participant.
        DownloadApplicationBinding second(h.presenter, h.controller, h.service,
                                          {.shutdown = [] {}, .stopped = [] { return true; }},
                                          {.automatic_polling = false});
        QVERIFY(!second.start());
        QVERIFY(h.presenter.requestExit());
        QCOMPARE(h.platform.exits, 0);
        QVERIFY(!h.presenter.setExitPreparationCallbacks({}));
    }
    void destroyedBindingLeavesAnExpiredUsedGateFailedInsteadOfBypassingDrain() {
        Harness h;
        PlayerPresenter presenter(
            FixtureRepository{}.load(QString::fromUtf8(M0_TEST_FIXTURE_FILE), QString::fromUtf8(M0_TEST_QUEUE_FILE)),
            h.platform);
        auto binding = std::make_unique<DownloadApplicationBinding>(
            presenter, h.controller, h.service,
            DownloadExecutionDrainCallbacks{.shutdown = [&] { h.execution.shutdown(); },
                                            .stopped = [&] { return h.execution.stopped(); }},
            DownloadApplicationBindingOptions{.automatic_polling = false});
        QVERIFY(binding->start());
        h.io.loaded();
        binding->poll();
        QVERIFY(presenter.requestExit());
        binding.reset();
        PlayerPresenterTestAccess::pollPersistence(presenter);
        QVERIFY(!presenter.exiting());
        QVERIFY(!presenter.exitCommitted());
        QVERIFY(!presenter.setExitPreparationCallbacks({}));
        static_cast<void>(presenter.requestExit());
        QCOMPARE(h.platform.exits, 0);
    }
};
} // namespace music_player::m0::test

QTEST_MAIN(music_player::m0::test::DownloadApplicationBindingTests)
#include "DownloadApplicationBindingTests.moc"
