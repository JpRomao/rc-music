#include "adapters/WindowsDownloadExecution.hpp"
#include "adapters/WindowsMediaFoundationLaunch.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <chrono>
#include <limits>
#include <semaphore>
#include <stdexcept>
#include <thread>

using namespace music_player::m0;
using namespace std::chrono_literals;
namespace {
const DownloadItem item{"https://media.example.test/audio.mp3", "Synthetic", DownloadMediaKind::Audio};
struct Fixture final {
    QTemporaryDir parent{QDir::tempPath() + QStringLiteral("/RC Music prepared media 日本-XXXXXX")};
    QString network_parent{parent.filePath(QStringLiteral("network runtimes"))};
    QString media_parent{parent.filePath(QStringLiteral("media runtimes"))};
    QString staging{parent.filePath(QStringLiteral("staging"))};
    QString finals{parent.filePath(QStringLiteral("finals"))};
    WindowsDownloadNetworkLaunchConfig network;
    WindowsMediaFoundationLaunchConfig media;
    WindowsDownloadJobSessionConfig job;
    bool initialize() {
        if (!parent.isValid() || !QDir().mkdir(network_parent) || !QDir().mkdir(media_parent) ||
            !QDir().mkdir(staging) || !QDir().mkdir(finals))
            return false;
        const auto path = parent.filePath(QStringLiteral("trusted source.bin"));
        const QByteArray bytes{"authenticated synthetic bytes; never execute"};
        QFile source{path};
        if (!source.open(QIODevice::WriteOnly | QIODevice::NewOnly) || source.write(bytes) != bytes.size())
            return false;
        source.close();
        const auto hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
        network.runtime_parent = network_parent;
        for (auto& artifact : network.artifacts)
            artifact = {path, hash};
        media = {media_parent, path, hash};
        // This failure fence precedes DNS, sockets and ANY executable launch.
        // Preparation copies authenticated non-PE fixtures, never runs them.
        job.workspace = {staging, finals, 0};
        return true;
    }
    QStringList networks() const { return QDir(network_parent).entryList(QDir::Dirs | QDir::NoDotAndDotDot); }
    QStringList mediaRuntimes() const { return QDir(media_parent).entryList(QDir::Dirs | QDir::NoDotAndDotDot); }
    QStringList workspaces() const { return QDir(staging).entryList(QDir::AllEntries | QDir::NoDotAndDotDot); }
};
struct Gate final {
    std::binary_semaphore entered{0}, continuation{0};
    std::atomic<bool> blocked{false}, released{false}, timed_out{false};
    void enter() {
        blocked = true;
        entered.release();
        timed_out = !continuation.try_acquire_for(5s);
        blocked = false;
    }
    bool wait() { return entered.try_acquire_for(5s); }
    void release() {
        if (!released.exchange(true))
            continuation.release();
    }
};
struct Drain final {
    Gate& gate;
    WindowsDownloadExecution& execution;
    ~Drain() {
        gate.release();
        execution.shutdown();
    }
};
} // namespace

class WindowsPreparedMediaDownloadTaskTests final : public QObject {
    Q_OBJECT
  private slots:
    void constructionDoesNotPrepareEitherRuntime() {
        Fixture h;
        QVERIFY(h.initialize());
        int network_calls = 0, media_calls = 0;
        auto task = makeWindowsPreparedDownloadTask(
            h.network, h.media, h.job,
            [&](const auto&) {
                ++network_calls;
                return std::shared_ptr<const WindowsDownloadNetworkLaunch>{};
            },
            [&](const auto&) {
                ++media_calls;
                return std::shared_ptr<const WindowsMediaFoundationLaunch>{};
            });
        QVERIFY(task);
        QCOMPARE(network_calls, 0);
        QCOMPARE(media_calls, 0);
        QVERIFY(h.networks().isEmpty());
        QVERIFY(h.mediaRuntimes().isEmpty());
        QVERIFY(h.workspaces().isEmpty());
        task.reset();
        QCOMPARE(network_calls, 0);
        QCOMPARE(media_calls, 0);
    }
    void startPreparesBothCapabilitiesBeforeTheFirstSessionPoll() {
        Fixture h;
        QVERIFY(h.initialize());
        int network_calls = 0, media_calls = 0;
        const auto worker = std::this_thread::get_id();
        std::thread::id network_thread, media_thread;
        std::weak_ptr<const WindowsDownloadNetworkLaunch> network_lease;
        std::weak_ptr<const WindowsMediaFoundationLaunch> media_lease;
        auto task = makeWindowsPreparedDownloadTask(
            h.network, h.media, h.job,
            [&](const auto& config) {
                ++network_calls;
                network_thread = std::this_thread::get_id();
                auto lease = WindowsDownloadNetworkLaunch::prepare(config);
                network_lease = lease;
                return lease;
            },
            [&](const auto& config) {
                ++media_calls;
                media_thread = std::this_thread::get_id();
                auto lease = WindowsMediaFoundationLaunch::prepare(config);
                media_lease = lease;
                return lease;
            });
        QVERIFY(task);
        QVERIFY(task->start("job-1", item, 7));
        QCOMPARE(network_calls, 1);
        QCOMPARE(media_calls, 1);
        QVERIFY(network_thread == worker);
        QVERIFY(media_thread == worker);
        QCOMPARE(h.networks().size(), 1);
        QCOMPARE(h.mediaRuntimes().size(), 1);
        QVERIFY(!network_lease.expired());
        QVERIFY(!media_lease.expired());
        QVERIFY(h.workspaces().isEmpty());
        const auto result = task->poll(7);
        QCOMPARE(result.job_id, std::string{"job-1"});
        QCOMPARE(result.state, DownloadExecutionState::Failed);
        QCOMPARE(result.error, DownloadFailure::InvalidResult);
        QVERIFY(result.published_mp3.empty());
        // A terminal snapshot is not permission to release the task's leases.
        QVERIFY(!network_lease.expired());
        QVERIFY(!media_lease.expired());
        task.reset();
        QVERIFY(network_lease.expired());
        QVERIFY(media_lease.expired());
        QVERIFY(h.networks().isEmpty());
        QVERIFY(h.mediaRuntimes().isEmpty());
    }
    void cancellingBeforeFirstPollRetainsBothPinsUntilTaskDestruction() {
        Fixture h;
        QVERIFY(h.initialize());
        auto task = makeWindowsPreparedDownloadTask(h.network, h.media, h.job);
        QVERIFY(task);
        QVERIFY(task->start("job-1", item, 0));
        QCOMPARE(h.networks().size(), 1);
        QCOMPARE(h.mediaRuntimes().size(), 1);
        const auto network_directory = h.network_parent + '/' + h.networks().front();
        const auto media_directory = h.media_parent + '/' + h.mediaRuntimes().front();
        QFile media_writer{media_directory + QStringLiteral("/RCMusicMediaWorker.exe")};
        QFile network_writer{network_directory + QStringLiteral("/RCMusicDownloadWorker.exe")};
        task->cancel();
        const auto result = task->poll(1);
        QCOMPARE(result.state, DownloadExecutionState::Cancelled);
        QCOMPARE(result.error, DownloadFailure::None);
        QCOMPARE(result.job_id, std::string{"job-1"});
        QVERIFY(result.published_mp3.empty());
        QVERIFY(h.workspaces().isEmpty());
        QVERIFY(!media_writer.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(!network_writer.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(QDir(media_directory).exists());
        QVERIFY(QDir(network_directory).exists());
        task.reset();
        QVERIFY(!QDir(media_directory).exists());
        QVERIFY(!QDir(network_directory).exists());
    }
    void mediaHashMismatchFailsBeforeWorkspaceOrChild() {
        Fixture h;
        QVERIFY(h.initialize());
        h.media.source_sha256 = QByteArray(64, '0');
        auto task = makeWindowsPreparedDownloadTask(h.network, h.media, h.job);
        QVERIFY(task);
        QVERIFY(!task->start("job-1", item, 0));
        const auto result = task->poll(1);
        QCOMPARE(result.state, DownloadExecutionState::Failed);
        QCOMPARE(result.error, DownloadFailure::Unavailable);
        QVERIFY(result.published_mp3.empty());
        QVERIFY(h.workspaces().isEmpty());
        QVERIFY(h.mediaRuntimes().isEmpty());
        task->cancel();
        QCOMPARE(task->poll(2).error, DownloadFailure::Unavailable);
        task.reset();
        QVERIFY(h.networks().isEmpty());
        QVERIFY(h.mediaRuntimes().isEmpty());
    }
    void oldPreparedFactoryRejectsRawMediaFoundationBeforeAnyPreparation() {
        Fixture h;
        QVERIFY(h.initialize());
        h.job.converter.backend = WindowsLocalMediaBackend::MediaFoundation;
        h.job.converter.media_foundation_executable = h.media.source_executable;
        int preparations = 0;
        auto task = makeWindowsPreparedDownloadTask(h.network, h.job, [&](const auto& config) {
            ++preparations;
            return WindowsDownloadNetworkLaunch::prepare(config);
        });
        QVERIFY(task);
        QVERIFY(!task->start("job-1", item, 0));
        QCOMPARE(preparations, 0);
        const auto result = task->poll(1);
        QCOMPARE(result.state, DownloadExecutionState::Failed);
        QCOMPARE(result.error, DownloadFailure::InvalidResult);
        QVERIFY(h.networks().isEmpty());
        QVERIFY(h.mediaRuntimes().isEmpty());
        QVERIFY(h.workspaces().isEmpty());
    }
    void originalDeadlineIncludesBothPreparations_data() {
        QTest::addColumn<qint64>("start");
        QTest::addColumn<qint64>("poll");
        QTest::addColumn<DownloadFailure>("error");
        QTest::newRow("inclusive deadline") << qint64{100} << qint64{110} << DownloadFailure::Timeout;
        QTest::newRow("after deadline") << qint64{100} << qint64{111} << DownloadFailure::Timeout;
        QTest::newRow("before deadline hits invalid workspace")
            << qint64{100} << qint64{109} << DownloadFailure::InvalidResult;
        QTest::newRow("backward clock") << qint64{100} << qint64{99} << DownloadFailure::InvalidResult;
        QTest::newRow("negative clock") << qint64{100} << qint64{-1} << DownloadFailure::InvalidResult;
        QTest::newRow("maximum clock") << std::numeric_limits<qint64>::max() - 10 << std::numeric_limits<qint64>::max()
                                       << DownloadFailure::Timeout;
    }
    void originalDeadlineIncludesBothPreparations() {
        QFETCH(qint64, start);
        QFETCH(qint64, poll);
        QFETCH(DownloadFailure, error);
        Fixture h;
        QVERIFY(h.initialize());
        h.job.total_timeout_ms = 10;
        auto task = makeWindowsPreparedDownloadTask(h.network, h.media, h.job);
        QVERIFY(task);
        QVERIFY(task->start("job-1", item, start));
        QCOMPARE(h.mediaRuntimes().size(), 1);
        const auto result = task->poll(poll);
        QCOMPARE(result.state, DownloadExecutionState::Failed);
        QCOMPARE(result.error, error);
        QCOMPARE(result.job_id, std::string{"job-1"});
        QVERIFY(result.published_mp3.empty());
        QVERIFY(h.workspaces().isEmpty());
        task.reset();
        QVERIFY(h.networks().isEmpty());
        QVERIFY(h.mediaRuntimes().isEmpty());
    }
    void failedMediaPreparerPreservesItsTypedFailure_data() {
        QTest::addColumn<bool>("throws");
        QTest::newRow("returns no capability") << false;
        QTest::newRow("throws without escaped resources") << true;
    }
    void failedMediaPreparerPreservesItsTypedFailure() {
        QFETCH(bool, throws);
        Fixture h;
        QVERIFY(h.initialize());
        int media_calls = 0;
        auto task = makeWindowsPreparedDownloadTask(h.network, h.media, h.job, {}, [&](const auto&) {
            ++media_calls;
            if (throws)
                throw std::runtime_error{"trusted preparer malfunction; private diagnostic"};
            return std::shared_ptr<const WindowsMediaFoundationLaunch>{};
        });
        QVERIFY(task);
        QVERIFY(!task->start("job-1", item, 0));
        QCOMPARE(media_calls, 1);
        task->cancel();
        const auto result = task->poll(1);
        QCOMPARE(result.state, DownloadExecutionState::Failed);
        QCOMPARE(result.error, throws ? DownloadFailure::InvalidResult : DownloadFailure::Unavailable);
        QVERIFY(result.published_mp3.empty());
        QVERIFY(h.workspaces().isEmpty());
        QVERIFY(h.mediaRuntimes().isEmpty());
        QVERIFY(!task->start("job-2", item, 2));
        QCOMPARE(media_calls, 1);
        QCOMPARE(task->poll(3).error, result.error);
        task.reset();
        QVERIFY(h.networks().isEmpty());
        QVERIFY(h.mediaRuntimes().isEmpty());
    }
    void uiRemainsPromptWhileMediaPreparationIsBlocked_data() {
        QTest::addColumn<bool>("shutdown");
        QTest::newRow("cancel") << false;
        QTest::newRow("shutdown") << true;
    }
    void uiRemainsPromptWhileMediaPreparationIsBlocked() {
        QFETCH(bool, shutdown);
        Fixture h;
        QVERIFY(h.initialize());
        Gate gate;
        const auto caller = std::this_thread::get_id();
        std::thread::id media_thread;
        std::atomic<bool> prepared{false};
        WindowsDownloadExecution execution([&] {
            return makeWindowsPreparedDownloadTask(h.network, h.media, h.job, {}, [&](const auto& config) {
                media_thread = std::this_thread::get_id();
                gate.enter();
                auto lease = WindowsMediaFoundationLaunch::prepare(config);
                prepared = static_cast<bool>(lease);
                return lease;
            });
        });
        Drain drain{gate, execution};
        QVERIFY(execution.start("job-1", item));
        QVERIFY(gate.wait());
        QVERIFY(media_thread != caller);
        QVERIFY(gate.blocked.load());
        QVERIFY(h.workspaces().isEmpty());
        if (shutdown)
            execution.shutdown();
        else
            execution.cancel("job-1");
        QCOMPARE(execution.poll("job-1").state, DownloadExecutionState::Running);
        QVERIFY(!execution.stopped());
        QVERIFY(!execution.start("job-2", item));
        QVERIFY(gate.blocked.load());
        gate.release();
        QTRY_COMPARE_WITH_TIMEOUT(execution.poll("job-1").state, DownloadExecutionState::Cancelled, 10000);
        QVERIFY(prepared.load());
        QVERIFY(!gate.timed_out.load());
        QVERIFY(h.workspaces().isEmpty());
        QVERIFY(h.networks().isEmpty());
        QVERIFY(h.mediaRuntimes().isEmpty());
        execution.shutdown();
        QTRY_VERIFY_WITH_TIMEOUT(execution.stopped(), 10000);
    }
};
QTEST_GUILESS_MAIN(WindowsPreparedMediaDownloadTaskTests)
#include "WindowsPreparedMediaDownloadTaskTests.moc"
