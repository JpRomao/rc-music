#include "WindowsDownloadExecution.hpp"
#include "m0/application/DownloadService.hpp"

#include <QTest>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace music_player::m0;
using namespace std::chrono_literals;

namespace {
struct Gate final {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered{false};
    bool released{false};
    bool timed_out{false};
    void enter() {
        std::unique_lock lock(mutex);
        entered = true;
        changed.notify_all();
        timed_out = !changed.wait_for(lock, 5s, [&] { return released; }); // watchdog only
    }
    bool wait() {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, 5s, [&] { return entered; });
    }
    void release() {
        std::lock_guard lock(mutex);
        released = true;
        changed.notify_all();
    }
    bool stillBlocked() {
        std::lock_guard lock(mutex);
        return entered && !released && !timed_out;
    }
};
struct Control final {
    std::mutex mutex;
    Gate factory_gate, start_gate, poll_gate, cancel_gate, destruction_gate;
    bool block_factory{false}, block_start{false}, block_poll{false}, block_cancel{false}, block_destruction{false};
    bool start_result{true};
    std::atomic_bool throw_start{false}, throw_poll{false}, throw_cancel{false};
    std::atomic_int factories{0}, starts{0}, polls{0}, cancels{0}, destructions{0};
    std::thread::id worker_id{}, destroyed_id{};
    DownloadExecutionUpdate update{};
    std::filesystem::path retained{};
    void publish(DownloadExecutionUpdate value) {
        std::lock_guard lock(mutex);
        update = std::move(value);
    }
    void releaseAll() {
        factory_gate.release();
        start_gate.release();
        poll_gate.release();
        cancel_gate.release();
        destruction_gate.release();
    }
};
class Task final : public WindowsDownloadTask {
  public:
    explicit Task(std::shared_ptr<Control> control) : c(std::move(control)) {}
    ~Task() override {
        if (c->block_destruction)
            c->destruction_gate.enter();
        c->destroyed_id = std::this_thread::get_id();
        ++c->destructions;
    }
    bool start(const std::string& id, const DownloadItem&, qint64) override {
        c->worker_id = std::this_thread::get_id();
        ++c->starts;
        if (c->block_start)
            c->start_gate.enter();
        if (c->throw_start)
            throw std::runtime_error("private detail never exposed");
        std::lock_guard lock(c->mutex);
        if (c->update.job_id.empty())
            c->update.job_id = id;
        return c->start_result;
    }
    void cancel() override {
        ++c->cancels;
        if (c->block_cancel)
            c->cancel_gate.enter();
        if (c->throw_cancel)
            throw std::runtime_error("not an acknowledgement");
    }
    DownloadExecutionUpdate poll(qint64) override {
        ++c->polls;
        if (c->block_poll)
            c->poll_gate.enter();
        if (c->throw_poll)
            throw std::runtime_error("not an acknowledgement");
        std::lock_guard lock(c->mutex);
        return c->update;
    }
    std::filesystem::path retainedPublicationPath() const override {
        std::lock_guard lock(c->mutex);
        return c->retained;
    }

  private:
    std::shared_ptr<Control> c;
};
WindowsDownloadExecution::TaskFactory factory(const std::shared_ptr<Control>& c) {
    return [c] {
        ++c->factories;
        if (c->block_factory)
            c->factory_gate.enter();
        return std::make_unique<Task>(c);
    };
}
const DownloadItem item{"https://media.example.test/audio.mp4", "Audio", DownloadMediaKind::Video};
DownloadExecutionUpdate cancelled(const std::string& id = "job-1") {
    DownloadExecutionUpdate result;
    result.job_id = id;
    result.state = DownloadExecutionState::Cancelled;
    return result;
}
struct Finish final {
    std::shared_ptr<Control> control;
    WindowsDownloadExecution& execution;
    ~Finish() {
        control->throw_start = false;
        control->throw_poll = false;
        control->throw_cancel = false;
        control->publish(cancelled());
        control->releaseAll();
        execution.shutdown();
    }
};
class Import final : public DownloadImport {
  public:
    int calls{0};
    bool accept(const std::string&, const std::filesystem::path&) override {
        ++calls;
        return true;
    }
};
} // namespace

class WindowsDownloadExecutionTests final : public QObject {
    Q_OBJECT
  private slots:
    void uiMethodsReturnWhileSlowWorkerOperationIsBlocked_data() {
        QTest::addColumn<int>("operation");
        QTest::newRow("factory") << 0;
        QTest::newRow("start") << 1;
        QTest::newRow("poll") << 2;
        QTest::newRow("cancel") << 3;
        QTest::newRow("destruction") << 4;
    }
    void uiMethodsReturnWhileSlowWorkerOperationIsBlocked() {
        QFETCH(int, operation);
        auto c = std::make_shared<Control>();
        c->block_factory = operation == 0;
        c->block_start = operation == 1;
        c->block_poll = operation == 2;
        c->block_cancel = operation == 3;
        c->block_destruction = operation == 4;
        WindowsDownloadExecution execution(factory(c));
        Finish finish{c, execution};
        QVERIFY(execution.start("job-1", item));
        if (operation >= 3) {
            QTRY_VERIFY(c->polls > 0);
            if (operation == 3)
                execution.cancel("job-1");
            else
                c->publish(cancelled());
        }
        Gate* gates[]{&c->factory_gate, &c->start_gate, &c->poll_gate, &c->cancel_gate, &c->destruction_gate};
        QVERIFY(gates[operation]->wait());
        QCOMPARE(execution.poll("job-1").state, DownloadExecutionState::Running);
        QVERIFY(!execution.start("job-2", item));
        execution.cancel("stale");
        execution.cancel("job-1");
        execution.shutdown();
        QVERIFY(!execution.stopped());
        QVERIFY(!execution.start("job-2", item));
        QVERIFY(gates[operation]->stillBlocked()); // caller returned BEFORE release/watchdog
        c->publish(cancelled());
        c->releaseAll();
        QTRY_VERIFY(execution.stopped());
        QCOMPARE(execution.poll("job-1").state, DownloadExecutionState::Cancelled);
        if (operation != 0) {
            QVERIFY(c->destroyed_id != std::this_thread::get_id());
            if (operation != 4)
                QCOMPARE(c->destroyed_id, c->worker_id);
        }
    }
    void cancellationDuringFactoryNeverStartsAndAcknowledgesOnlyAfterDestruction() {
        auto c = std::make_shared<Control>();
        c->block_factory = true;
        c->block_destruction = true;
        WindowsDownloadExecution execution(factory(c));
        Finish finish{c, execution};
        QVERIFY(execution.start("job-1", item));
        QVERIFY(c->factory_gate.wait());
        execution.cancel("job-1");
        c->factory_gate.release();
        QVERIFY(c->destruction_gate.wait());
        QCOMPARE(c->starts.load(), 0);
        QCOMPARE(execution.poll("job-1").state, DownloadExecutionState::Running);
        c->destruction_gate.release();
        QTRY_COMPARE(execution.poll("job-1").state, DownloadExecutionState::Cancelled);
        QCOMPARE(c->destructions.load(), 1);
        execution.shutdown();
        QTRY_VERIFY(execution.stopped());
    }
    void terminalIsStableAndOnlyMatchingObservationAllowsAnotherStart() {
        auto c = std::make_shared<Control>();
        c->block_destruction = true;
        WindowsDownloadExecution execution(factory(c));
        Finish finish{c, execution};
        QVERIFY(execution.start("job-1", item));
        QTRY_VERIFY(c->polls > 0);
        c->publish(cancelled());
        QVERIFY(c->destruction_gate.wait());
        c->destruction_gate.release();
        QTRY_COMPARE(c->destructions.load(), 1);
        QVERIFY(!execution.start("job-2", item));
        QVERIFY(execution.poll("stale").job_id != "stale");
        execution.cancel("stale");
        QVERIFY(!execution.start("job-2", item));
        QTRY_COMPARE(execution.poll("job-1").state, DownloadExecutionState::Cancelled);
        c->publish({});
        QCOMPARE(execution.poll("job-1").state, DownloadExecutionState::Cancelled);
        QVERIFY(execution.start("job-2", item));
        c->publish(cancelled("job-2"));
        QTRY_COMPARE(execution.poll("job-2").state, DownloadExecutionState::Cancelled);
        execution.shutdown();
        QTRY_VERIFY(execution.stopped());
    }
    void invalidAdmissionRetainsNoWork_data() {
        QTest::addColumn<std::string>("id");
        QTest::addColumn<DownloadItem>("request");
        QTest::newRow("empty id") << std::string{} << item;
        QTest::newRow("oversized id") << std::string(65, 'a') << item;
        QTest::newRow("invalid id") << std::string{"../job"} << item;
        auto invalid = item;
        invalid.url = "file:///C:/personal.mp3";
        QTest::newRow("local source") << std::string{"job-1"} << invalid;
        invalid = item;
        invalid.title = std::string(801, 'a');
        QTest::newRow("oversized title") << std::string{"job-1"} << invalid;
        invalid = item;
        invalid.kind = static_cast<DownloadMediaKind>(99);
        QTest::newRow("unknown kind") << std::string{"job-1"} << invalid;
    }
    void invalidAdmissionRetainsNoWork() {
        QFETCH(std::string, id);
        QFETCH(DownloadItem, request);
        auto c = std::make_shared<Control>();
        WindowsDownloadExecution execution(factory(c));
        Finish finish{c, execution};
        QVERIFY(!execution.start(id, request));
        execution.shutdown();
        QTRY_VERIFY(execution.stopped());
        QCOMPARE(c->factories.load(), 0);
    }
    void invalidArchivedEvidenceRetainsNoWork_data() {
        QTest::addColumn<bool>("retired");
        QTest::addColumn<bool>("oversized");
        QTest::newRow("invalid archive id") << false << false;
        QTest::newRow("invalid retired id") << true << false;
        QTest::newRow("too many archived ids") << false << true;
        QTest::newRow("too many retired ids") << true << true;
    }
    void invalidArchivedEvidenceRetainsNoWork() {
        QFETCH(bool, retired);
        QFETCH(bool, oversized);
        const std::vector<std::string> ids =
            oversized ? std::vector<std::string>(5'121, "archived-job") : std::vector<std::string>{"../job"};
        ArchivedDownloadJobEvidence evidence;
        (retired ? evidence.retired_import_job_ids : evidence.archived_job_ids) = ids;
        auto c = std::make_shared<Control>();
        WindowsDownloadExecution execution(factory(c));
        Finish finish{c, execution};
        QVERIFY(!execution.startWithArchivedJobs("job-1", item, evidence));
        execution.shutdown();
        QTRY_VERIFY(execution.stopped());
        QCOMPARE(c->factories.load(), 0);
    }
    void cancellationDuringPreflightDetachesEvidenceAndSkipsFactory_data() {
        QTest::addColumn<DownloadFailure>("preflight_error");
        QTest::addColumn<DownloadExecutionState>("terminal_state");
        QTest::newRow("capacity available") << DownloadFailure::None << DownloadExecutionState::Cancelled;
        QTest::newRow("unconfirmed journal replacement")
            << DownloadFailure::Publication << DownloadExecutionState::Failed;
    }
    void cancellationDuringPreflightDetachesEvidenceAndSkipsFactory() {
        QFETCH(DownloadFailure, preflight_error);
        QFETCH(DownloadExecutionState, terminal_state);
        const auto c = std::make_shared<Control>();
        const auto caller_thread = std::this_thread::get_id();
        std::array<std::string, 1> archived{"archived-job"};
        std::array<std::string, 1> retired{"retired-job"};
        bool observed_original_evidence = false;
        bool ran_on_worker = false;
        WindowsDownloadExecution execution(factory(c), [&](const ArchivedDownloadJobEvidence& evidence) {
            ran_on_worker = std::this_thread::get_id() != caller_thread;
            c->factory_gate.enter();
            observed_original_evidence =
                evidence.archived_job_ids.size() == 1 && evidence.archived_job_ids.front() == "archived-job" &&
                evidence.retired_import_job_ids.size() == 1 && evidence.retired_import_job_ids.front() == "retired-job";
            return preflight_error;
        });
        Finish finish{c, execution};
        QVERIFY(execution.startWithArchivedJobs("job-1", item, {archived, retired}));
        QVERIFY(c->factory_gate.wait());
        archived.front() = "changed-archive";
        retired.front() = "changed-receipt";
        execution.cancel("job-1");
        QCOMPARE(execution.poll("job-1").state, DownloadExecutionState::Running);
        QVERIFY(c->factory_gate.stillBlocked());
        c->factory_gate.release();
        QTRY_COMPARE(execution.poll("job-1").state, terminal_state);
        QCOMPARE(execution.poll("job-1").error, preflight_error);
        QVERIFY(observed_original_evidence);
        QVERIFY(ran_on_worker);
        QCOMPARE(c->factories.load(), 0);
    }
    void factoryFailureIsAcceptedThenAcknowledged_data() {
        QTest::addColumn<bool>("throws");
        QTest::newRow("null") << false;
        QTest::newRow("throw") << true;
    }
    void progressIsCoalescedAndPublicationIsStable() {
        auto c = std::make_shared<Control>();
        WindowsDownloadExecution execution(factory(c));
        Finish finish{c, execution};
        QVERIFY(execution.start("job-1", item));
        QTRY_VERIFY(c->polls > 0);
        DownloadExecutionUpdate value;
        value.job_id = "job-1";
        value.completed_units = 100;
        value.total_units = 1000;
        c->publish(value);
        QTRY_COMPARE(execution.poll("job-1").completed_units, std::uint64_t{100});
        value.phase = DownloadPhase::Converting;
        value.completed_units = 0;
        value.total_units.reset();
        c->publish(value);
        QTRY_COMPARE(execution.poll("job-1").phase, DownloadPhase::Converting);
        value.phase = DownloadPhase::Publishing;
        value.state = DownloadExecutionState::Published;
        value.published_mp3 = std::filesystem::path{L"C:\\private\\final.mp3"};
        c->publish(value);
        QTRY_COMPARE(execution.poll("job-1").state, DownloadExecutionState::Published);
        c->publish({});
        execution.cancel("job-1");
        QCOMPARE(execution.poll("job-1").published_mp3, value.published_mp3);
        QCOMPARE(execution.poll("job-1").state, DownloadExecutionState::Published);
        execution.shutdown();
        QTRY_VERIFY(execution.stopped());
    }
    void factoryFailureIsAcceptedThenAcknowledged() {
        QFETCH(bool, throws);
        WindowsDownloadExecution execution([throws]() -> std::unique_ptr<WindowsDownloadTask> {
            if (throws)
                throw std::runtime_error("factory released all work");
            return {};
        });
        QVERIFY(execution.start("job-1", item));
        QTRY_COMPARE(execution.poll("job-1").state, DownloadExecutionState::Failed);
        QCOMPARE(execution.poll("job-1").error, DownloadFailure::Unavailable);
        execution.shutdown();
        QTRY_VERIFY(execution.stopped());
    }
    void uncertainWorkerResultsNeverAcknowledgeUntilValidDrain_data() {
        QTest::addColumn<int>("fault");
        QTest::newRow("start false") << 0;
        QTest::newRow("start throws") << 1;
        QTest::newRow("poll throws") << 2;
        QTest::newRow("unknown state") << 3;
        QTest::newRow("wrong job terminal") << 4;
        QTest::newRow("cancel throws") << 5;
        QTest::newRow("malformed terminal") << 6;
        QTest::newRow("oversized running path") << 7;
    }
    void uncertainWorkerResultsNeverAcknowledgeUntilValidDrain() {
        QFETCH(int, fault);
        auto c = std::make_shared<Control>();
        c->start_result = fault != 0;
        c->throw_start = fault == 1;
        c->throw_poll = fault == 2;
        c->throw_cancel = fault == 5;
        if (fault == 3) {
            auto value = cancelled();
            value.state = static_cast<DownloadExecutionState>(99);
            c->publish(value);
        } else if (fault == 4) {
            c->publish(cancelled("stale"));
        } else if (fault == 6) {
            auto value = cancelled();
            value.state = DownloadExecutionState::Failed;
            c->publish(value); // None is not a valid failed acknowledgement
        } else if (fault == 7) {
            DownloadExecutionUpdate value;
            value.job_id = "job-1";
            value.published_mp3 = std::filesystem::path{std::wstring(30001, L'a')};
            c->publish(value);
        }
        WindowsDownloadExecution execution(factory(c));
        Finish finish{c, execution};
        QVERIFY(execution.start("job-1", item));
        if (fault == 5) {
            QTRY_VERIFY(c->polls > 0);
            execution.cancel("job-1");
        }
        QTRY_VERIFY(c->cancels > 0);
        QCOMPARE(execution.poll("job-1").state, DownloadExecutionState::Running);
        QVERIFY(!execution.start("job-2", item));
        QCOMPARE(c->destructions.load(), 0);
        c->throw_start = false;
        c->throw_poll = false;
        c->throw_cancel = false;
        c->publish(cancelled());
        QTRY_COMPARE(execution.poll("job-1").state, DownloadExecutionState::Failed);
        QCOMPARE(execution.poll("job-1").error, DownloadFailure::InvalidResult);
        QCOMPARE(c->destructions.load(), 1);
        execution.shutdown();
        QTRY_VERIFY(execution.stopped());
    }
    void committedFinalSurvivesCancellationAndCleanupFailure_data() {
        QTest::addColumn<bool>("cleanup_failure");
        QTest::newRow("published racing cancel") << false;
        QTest::newRow("cleanup after rename") << true;
    }
    void refusedTaskStartPreservesAcknowledgedTypedFailure_data() {
        QTest::addColumn<DownloadFailure>("error");
        QTest::newRow("unavailable configuration") << DownloadFailure::Unavailable;
        QTest::newRow("publication cleanup") << DownloadFailure::Publication;
        QTest::newRow("network refused") << DownloadFailure::Network;
    }
    void refusedTaskStartPreservesAcknowledgedTypedFailure() {
        QFETCH(DownloadFailure, error);
        auto c = std::make_shared<Control>();
        c->start_result = false;
        c->block_poll = true;
        WindowsDownloadExecution execution(factory(c));
        Finish finish{c, execution};
        QVERIFY(execution.start("job-1", item));
        QVERIFY(c->poll_gate.wait());
        QCOMPARE(execution.poll("job-1").state, DownloadExecutionState::Running);
        auto value = cancelled();
        value.state = DownloadExecutionState::Failed;
        value.error = error;
        c->publish(value);
        c->poll_gate.release();
        QTRY_COMPARE(execution.poll("job-1").state, DownloadExecutionState::Failed);
        QCOMPARE(execution.poll("job-1").error, error);
        execution.shutdown();
        QTRY_VERIFY(execution.stopped());
    }
    void committedFinalSurvivesCancellationAndCleanupFailure() {
        QFETCH(bool, cleanup_failure);
        auto c = std::make_shared<Control>();
        c->block_poll = true;
        WindowsDownloadExecution execution(factory(c));
        Finish finish{c, execution};
        QVERIFY(execution.start("job-1", item));
        QVERIFY(c->poll_gate.wait());
        const std::filesystem::path final{L"C:\\private\\committed.mp3"};
        auto value = cancelled();
        value.phase = DownloadPhase::Publishing;
        value.state = cleanup_failure ? DownloadExecutionState::Failed : DownloadExecutionState::Published;
        value.error = cleanup_failure ? DownloadFailure::Publication : DownloadFailure::None;
        if (cleanup_failure)
            c->retained = final;
        else
            value.published_mp3 = final;
        c->publish(value);
        execution.cancel("job-1");
        c->poll_gate.release();
        QTRY_COMPARE(execution.poll("job-1").state, value.state);
        QCOMPARE(execution.poll("job-1").error, value.error);
        QCOMPARE(execution.retainedPublicationPath("job-1"), final);
        QVERIFY(execution.retainedPublicationPath("stale").empty());
        QCOMPARE(c->destructions.load(), 1);
        execution.shutdown();
        QTRY_VERIFY(execution.stopped());
    }
    void serviceCancellationDoesNotImportAndSuccessorWaitsForDrain() {
        auto c = std::make_shared<Control>();
        c->block_poll = true;
        WindowsDownloadExecution execution(factory(c));
        Finish finish{c, execution};
        Import importer;
        DownloadService service(execution, importer, "job");
        const auto submitted = service.submit({"11111111-1111-4111-8111-111111111111", {item, item}});
        QCOMPARE(submitted.job_ids.size(), std::size_t{2});
        service.tick(0);
        QVERIFY(c->poll_gate.wait());
        QVERIFY(service.cancel(submitted.job_ids.front()));
        service.tick(1);
        QCOMPARE(c->factories.load(), 1);
        QCOMPARE(importer.calls, 0);
        service.shutdown();
        QVERIFY(!service.stopped());
        execution.shutdown();
        QVERIFY(!execution.stopped());
        c->publish(cancelled(submitted.job_ids.front()));
        c->poll_gate.release();
        QTRY_VERIFY(execution.stopped());
        service.tick(2);
        QVERIFY(service.stopped());
        QCOMPARE(importer.calls, 0);
        QCOMPARE(c->factories.load(), 1);
    }
};

QTEST_GUILESS_MAIN(WindowsDownloadExecutionTests)
#include "WindowsDownloadExecutionTests.moc"
