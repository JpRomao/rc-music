#include "WindowsDownloadExecution.hpp"
#include "support/YoutubeResolverTestSupport.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace music_player::m0;
using namespace std::chrono_literals;
namespace {
const DownloadItem item{"https://media.example.test/audio.mp3", "Synthetic", DownloadMediaKind::Audio};
struct Harness final {
    QTemporaryDir parent;
    QString runtime{parent.filePath("runtime")}, staging{parent.filePath("staging")}, finals{parent.filePath("finals")};
    WindowsDownloadNetworkLaunchConfig launch;
    WindowsDownloadJobSessionConfig job;
    Harness() {
        QDir().mkdir(runtime);
        QDir().mkdir(staging);
        QDir().mkdir(finals);
        const auto path = parent.filePath("source.bin");
        QFile source(path);
        if (!source.open(QIODevice::WriteOnly) || source.write("authenticated-synthetic") != 23)
            return;
        source.close();
        QFile input(path);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!input.open(QIODevice::ReadOnly) || !hash.addData(&input))
            return;
        launch.runtime_parent = runtime;
        for (auto& artifact : launch.artifacts)
            artifact = {path, hash.result().toHex()};
        job.workspace = {staging, finals, 0}; // Fail before ANY DNS, socket or media process.
    }
    QStringList runtimes() const { return QDir(runtime).entryList(QDir::Dirs | QDir::NoDotAndDotDot); }
    QStringList workspaces() const { return QDir(staging).entryList(QDir::Dirs | QDir::NoDotAndDotDot); }
};
struct Gate final {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered{false}, released{false}, timed_out{false};
    void enter() {
        std::unique_lock lock(mutex);
        entered = true;
        changed.notify_all();
        timed_out = !changed.wait_for(lock, 5s, [&] { return released; });
    }
    bool wait() {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, 5s, [&] { return entered; });
    }
    bool blocked() {
        std::lock_guard lock(mutex);
        return entered && !released && !timed_out;
    }
    void release() {
        std::lock_guard lock(mutex);
        released = true;
        changed.notify_all();
    }
};
struct Drain final {
    Gate& gate;
    WindowsDownloadExecution& execution;
    ~Drain() {
        gate.release();
        execution.shutdown();
        // Destructor is only a failure fallback. Test assertions observe real
        // stopped before normal teardown; do not fake an ACK here.
    }
};
} // namespace

class WindowsPreparedDownloadTaskTests final : public QObject {
    Q_OBJECT
  private slots:
    void expiredYoutubeCapabilityCannotPrepareARuntime() {
        Harness h;
        const auto cap = test::resolvedYoutubeFixture();
        QVERIFY(cap);
        int calls{};
        auto task = makeWindowsPreparedDownloadTask(h.launch, h.job, [&](const auto&) {
            ++calls;
            return std::shared_ptr<const WindowsDownloadNetworkLaunch>{};
        });
        QVERIFY(!task->startResolvedYoutube(cap, 0, cap->expiresAtSeconds()));
        QCOMPARE(task->poll(1).error, DownloadFailure::Expired);
        QCOMPARE(calls, 0);
        QVERIFY(h.runtimes().isEmpty());
        QVERIFY(h.workspaces().isEmpty());
    }
    void youtubePreparationUsesFreshExpiryAndPreservesCancellation_data() {
        QTest::addColumn<bool>("cancel");
        QTest::newRow("expired-during-preparation") << false;
        QTest::newRow("cancelled-after-preparation") << true;
    }
    void youtubePreparationUsesFreshExpiryAndPreservesCancellation() {
        QFETCH(bool, cancel);
        Harness h;
        const auto cap = test::resolvedYoutubeFixture();
        QVERIFY(cap);
        auto task = makeWindowsPreparedDownloadTask(h.launch, h.job);
        QVERIFY(task->startResolvedYoutube(cap, 10, cap->expiresAtSeconds() - 1));
        QCOMPARE(h.runtimes().size(), 1);
        QVERIFY(h.workspaces().isEmpty());
        if (cancel)
            task->cancel();
        const auto update = task->poll(1010);
        QCOMPARE(update.job_id, cap->jobId());
        QCOMPARE(update.state, cancel ? DownloadExecutionState::Cancelled : DownloadExecutionState::Failed);
        QCOMPARE(update.error, cancel ? DownloadFailure::None : DownloadFailure::Expired);
        QVERIFY(h.workspaces().isEmpty());
        task.reset();
        QVERIFY(h.runtimes().isEmpty());
    }
    void constructionIsInert() {
        Harness h;
        QVERIFY(h.parent.isValid());
        int calls = 0;
        auto task = makeWindowsPreparedDownloadTask(h.launch, h.job, [&](const auto&) {
            ++calls;
            return std::shared_ptr<const WindowsDownloadNetworkLaunch>{};
        });
        QVERIFY(task);
        QCOMPARE(calls, 0);
        QVERIFY(h.runtimes().isEmpty());
        task.reset();
        QCOMPARE(calls, 0);
    }
    void preparationPrecedesSessionAndTeardownReleasesLease() {
        Harness h;
        auto task = makeWindowsPreparedDownloadTask(h.launch, h.job);
        QVERIFY(task);
        QVERIFY(h.runtimes().isEmpty());
        QVERIFY(task->start("job-1", item, 7));
        QCOMPARE(h.runtimes().size(), 1);
        QVERIFY(h.workspaces().isEmpty());
        const auto result = task->poll(7);
        QCOMPARE(result.job_id, std::string("job-1"));
        QCOMPARE(result.state, DownloadExecutionState::Failed);
        QCOMPARE(result.error, DownloadFailure::InvalidResult);
        task->cancel();
        QCOMPARE(task->poll(8).error, DownloadFailure::InvalidResult);
        QVERIFY(result.published_mp3.empty());
        QVERIFY(!h.runtimes().isEmpty());
        task.reset();
        QVERIFY(h.runtimes().isEmpty());
    }
    void cancelAfterPreparationDoesNotStartSession() {
        Harness h;
        auto task = makeWindowsPreparedDownloadTask(h.launch, h.job);
        QVERIFY(task);
        QVERIFY(task->start("job-1", item, 0));
        task->cancel();
        const auto result = task->poll(1);
        QCOMPARE(result.state, DownloadExecutionState::Cancelled);
        QCOMPARE(result.job_id, std::string("job-1"));
        QCOMPARE(result.error, DownloadFailure::None);
        QVERIFY(result.published_mp3.empty());
        QVERIFY(h.workspaces().isEmpty());
        task.reset();
        QVERIFY(h.runtimes().isEmpty());
    }
    void observedClockIncludesPreparation_data() {
        QTest::addColumn<qint64>("start");
        QTest::addColumn<qint64>("poll");
        QTest::addColumn<DownloadFailure>("error");
        QTest::newRow("exact-deadline") << qint64{100} << qint64{110} << DownloadFailure::Timeout;
        QTest::newRow("after-deadline") << qint64{100} << qint64{111} << DownloadFailure::Timeout;
        QTest::newRow("before-deadline-invalid-workspace")
            << qint64{100} << qint64{109} << DownloadFailure::InvalidResult;
        QTest::newRow("regression") << qint64{100} << qint64{99} << DownloadFailure::InvalidResult;
        QTest::newRow("negative") << qint64{100} << qint64{-1} << DownloadFailure::InvalidResult;
        QTest::newRow("max-clock") << std::numeric_limits<qint64>::max() - 10 << std::numeric_limits<qint64>::max()
                                   << DownloadFailure::Timeout;
    }
    void observedClockIncludesPreparation() {
        QFETCH(qint64, start);
        QFETCH(qint64, poll);
        QFETCH(DownloadFailure, error);
        Harness h;
        h.job.total_timeout_ms = 10;
        auto task = makeWindowsPreparedDownloadTask(h.launch, h.job);
        QVERIFY(task);
        QVERIFY(task->start("job-1", item, start));
        const auto result = task->poll(poll);
        QCOMPARE(result.state, DownloadExecutionState::Failed);
        QCOMPARE(result.error, error);
        QCOMPARE(result.job_id, std::string("job-1"));
        QVERIFY(result.published_mp3.empty());
        QVERIFY(h.workspaces().isEmpty());
        task.reset();
        QVERIFY(h.runtimes().isEmpty());
    }
    void preparationFailureIsStable_data() {
        QTest::addColumn<bool>("throws");
        QTest::newRow("unavailable") << false;
        QTest::newRow("exception-malfunction") << true;
    }
    void preparationFailureIsStable() {
        QFETCH(bool, throws);
        Harness h;
        auto task = makeWindowsPreparedDownloadTask(h.launch, h.job, [&](const auto&) {
            if (throws)
                throw std::runtime_error("private data never exposed");
            return std::shared_ptr<const WindowsDownloadNetworkLaunch>{};
        });
        QVERIFY(task);
        QVERIFY(!task->start("job-1", item, 0));
        task->cancel();
        const auto result = task->poll(1);
        QCOMPARE(result.state, DownloadExecutionState::Failed);
        QCOMPARE(result.error, throws ? DownloadFailure::InvalidResult : DownloadFailure::Unavailable);
        QCOMPARE(result.job_id, std::string("job-1"));
        QVERIFY(result.published_mp3.empty());
        QCOMPARE(task->poll(2).error, result.error);
        QVERIFY(h.runtimes().isEmpty());
    }
    void terminalBeforeStartRefusesPreparation_data() {
        QTest::addColumn<bool>("cancel");
        QTest::newRow("cancelled-before-start") << true;
        QTest::newRow("polled-before-start") << false;
    }
    void terminalBeforeStartRefusesPreparation() {
        QFETCH(bool, cancel);
        Harness h;
        int calls = 0;
        auto task = makeWindowsPreparedDownloadTask(h.launch, h.job, [&](const auto& config) {
            ++calls;
            return WindowsDownloadNetworkLaunch::prepare(config);
        });
        QVERIFY(task);
        if (cancel)
            task->cancel();
        const auto before = task->poll(0);
        QCOMPARE(before.state, cancel ? DownloadExecutionState::Cancelled : DownloadExecutionState::Failed);
        QCOMPARE(before.error, cancel ? DownloadFailure::None : DownloadFailure::InvalidResult);
        QVERIFY(!task->start("late-job", item, 1));
        QCOMPARE(calls, 0);
        QCOMPARE(task->poll(2).state, before.state);
        QCOMPARE(task->poll(2).job_id, before.job_id);
        QVERIFY(h.runtimes().isEmpty());
    }
    void startIsOneShotAfterPreparationOrFailure_data() {
        QTest::addColumn<bool>("success");
        QTest::newRow("prepared") << true;
        QTest::newRow("failed") << false;
    }
    void startIsOneShotAfterPreparationOrFailure() {
        QFETCH(bool, success);
        Harness h;
        int calls = 0;
        auto task = makeWindowsPreparedDownloadTask(h.launch, h.job, [&](const auto& config) {
            ++calls;
            return success ? WindowsDownloadNetworkLaunch::prepare(config)
                           : std::shared_ptr<const WindowsDownloadNetworkLaunch>{};
        });
        QVERIFY(task);
        QCOMPARE(task->start("job-1", item, 0), success);
        QVERIFY(!task->start("job-2", item, 1));
        QCOMPARE(calls, 1);
        task->cancel();
        const auto result = task->poll(2);
        QCOMPARE(result.job_id, std::string("job-1"));
        QCOMPARE(result.state, success ? DownloadExecutionState::Cancelled : DownloadExecutionState::Failed);
        QCOMPARE(result.error, success ? DownloadFailure::None : DownloadFailure::Unavailable);
        QVERIFY(result.published_mp3.empty());
        task.reset();
        QVERIFY(h.runtimes().isEmpty());
    }
    void callerIsPromptWhilePreparationIsBlocked_data() {
        QTest::addColumn<bool>("shutdown");
        QTest::newRow("cancel") << false;
        QTest::newRow("shutdown") << true;
    }
    void callerIsPromptWhilePreparationIsBlocked() {
        QFETCH(bool, shutdown);
        Harness h;
        Gate gate;
        const auto caller = std::this_thread::get_id();
        std::thread::id preparer_thread;
        WindowsDownloadExecution execution([&] {
            return makeWindowsPreparedDownloadTask(h.launch, h.job, [&](const auto& config) {
                preparer_thread = std::this_thread::get_id();
                gate.enter();
                return WindowsDownloadNetworkLaunch::prepare(config);
            });
        });
        Drain drain{gate, execution};
        QVERIFY(execution.start("job-1", item));
        QVERIFY(gate.wait());
        QVERIFY(preparer_thread != caller);
        QVERIFY(gate.blocked());
        if (shutdown)
            execution.shutdown();
        else
            execution.cancel("job-1");
        QCOMPARE(execution.poll("job-1").state, DownloadExecutionState::Running);
        QVERIFY(!execution.stopped());
        QVERIFY(!execution.start("job-2", item));
        QVERIFY(gate.blocked());
        gate.release();
        QTRY_COMPARE_WITH_TIMEOUT(execution.poll("job-1").state, DownloadExecutionState::Cancelled, 10000);
        QVERIFY(h.workspaces().isEmpty());
        QVERIFY(h.runtimes().isEmpty());
        execution.shutdown();
        QTRY_VERIFY_WITH_TIMEOUT(execution.stopped(), 10000);
    }
};
QTEST_GUILESS_MAIN(WindowsPreparedDownloadTaskTests)
#include "WindowsPreparedDownloadTaskTests.moc"
