#include "adapters/WindowsDownloadArtifactPins.hpp"
#include "adapters/WindowsDownloadExecution.hpp"
#include "adapters/WindowsDownloadOwnershipStore.hpp"
#include "adapters/WindowsMediaFoundationLaunch.hpp"

#include <sddl.h>

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
#include <vector>

using namespace music_player::m0;
using namespace std::chrono_literals;
namespace {
const DownloadItem item{"https://media.example.test/audio.mp3", "Synthetic", DownloadMediaKind::Audio};
bool privateDirectory(const QString& path) {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
        return false;
    const download_artifacts::Handle token(raw);
    DWORD size = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    if (!size || size > 64 * 1024)
        return false;
    std::vector<unsigned char> bytes(size);
    if (!GetTokenInformation(token.get(), TokenUser, bytes.data(), size, &size))
        return false;
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &sid))
        return false;
    const auto text = QStringLiteral("D:P(A;;FA;;;SY)(A;;FA;;;") + QString::fromWCharArray(sid) + ')';
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(text.toStdWString().c_str(), SDDL_REVISION_1, &descriptor,
                                                              nullptr))
        return false;
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    const bool created = CreateDirectoryW(download_artifacts::nativePath(path).c_str(), &attributes) != FALSE;
    LocalFree(descriptor);
    return created;
}
struct BorrowedStore final : DownloadOwnershipStore {
    int calls{0};
    DownloadOwnershipLoadResult load() override {
        ++calls;
        return {};
    }
    DownloadCheckpointSaveStatus record(const DownloadOwnershipRecord&) override {
        ++calls;
        return DownloadCheckpointSaveStatus::Saved;
    }
};
struct Fixture final {
    QTemporaryDir parent{QDir::tempPath() + QStringLiteral("/RC Music prepared media 日本-XXXXXX")};
    QString network_parent{parent.filePath(QStringLiteral("network runtimes"))};
    QString media_parent{parent.filePath(QStringLiteral("media runtimes"))};
    QString staging{parent.filePath(QStringLiteral("staging"))};
    QString finals{parent.filePath(QStringLiteral("finals"))};
    QString journal{parent.filePath(QStringLiteral("private journal"))};
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
    void ownedJournalFactoryIsInert() {
        Fixture h;
        QVERIFY(h.initialize());
        int preparations = 0;
        auto task = makeWindowsOwnedPreparedDownloadTask(h.network, h.media, h.job, h.journal, [&](const auto&) {
            ++preparations;
            return std::shared_ptr<const WindowsDownloadNetworkLaunch>{};
        });
        QVERIFY(task);
        QVERIFY(!QDir(h.journal).exists());
        QCOMPARE(preparations, 0);
        task.reset();
        QVERIFY(!QDir(h.journal).exists());
        QVERIFY(h.networks().isEmpty());
        QVERIFY(h.mediaRuntimes().isEmpty());
    }
    void ownedJournalRejectsInvalidDirectoryBeforePreparation_data() {
        QTest::addColumn<int>("kind");
        QTest::newRow("empty") << 0;
        QTest::newRow("relative") << 1;
        QTest::newRow("missing") << 2;
        QTest::newRow("nonprivate") << 3;
        QTest::newRow("file") << 4;
        QTest::newRow("unc") << 5;
        QTest::newRow("dot component") << 6;
    }
    void ownedJournalRejectsInvalidDirectoryBeforePreparation() {
        QFETCH(int, kind);
        Fixture h;
        QVERIFY(h.initialize());
        QString path;
        switch (kind) {
        case 0:
            break;
        case 1:
            path = QStringLiteral("relative/journal");
            break;
        case 2:
            path = h.journal;
            break;
        case 3:
            path = h.parent.path();
            break;
        case 4:
            path = h.media.source_executable;
            break;
        case 5:
            path = QStringLiteral("//server/share/journal");
            break;
        default:
            path = h.parent.path() + QStringLiteral("/../journal");
            break;
        }
        int preparations = 0;
        auto task = makeWindowsOwnedPreparedDownloadTask(h.network, h.media, h.job, path, [&](const auto&) {
            ++preparations;
            return std::shared_ptr<const WindowsDownloadNetworkLaunch>{};
        });
        QVERIFY(task);
        QVERIFY(!task->start("job-1", item, 0));
        QCOMPARE(preparations, 0);
        const auto update = task->poll(1);
        QCOMPARE(update.job_id, std::string{"job-1"});
        QCOMPARE(update.state, DownloadExecutionState::Failed);
        QCOMPARE(update.error, DownloadFailure::InvalidResult);
        QVERIFY(!QDir(h.journal).exists());
        QVERIFY(h.networks().isEmpty());
        QVERIFY(h.workspaces().isEmpty());
    }
    void ownedJournalRejectsBorrowedStoreBeforePreparation() {
        Fixture h;
        QVERIFY(h.initialize());
        QVERIFY(privateDirectory(h.journal));
        BorrowedStore borrowed;
        h.job.ownership_store = &borrowed;
        int preparations = 0;
        auto task = makeWindowsOwnedPreparedDownloadTask(h.network, h.media, h.job, h.journal, [&](const auto&) {
            ++preparations;
            return std::shared_ptr<const WindowsDownloadNetworkLaunch>{};
        });
        QVERIFY(task);
        QVERIFY(!task->start("job-1", item, 0));
        QCOMPARE(preparations, 0);
        QCOMPARE(borrowed.calls, 0);
        QCOMPARE(task->poll(1).error, DownloadFailure::InvalidResult);
        QVERIFY(QDir(h.journal).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
    }
    void ownedJournalPreservesUnknownDocumentBeforePreparingRuntime() {
        Fixture h;
        QVERIFY(h.initialize());
        QVERIFY(privateDirectory(h.journal));
        DownloadOwnershipRecord previous{.job_id = "previous-job",
                                         .workspace_id = "abcdefab-cdef-4abc-8def-000000000001",
                                         .staging_parent = {.volume_serial = 7},
                                         .publish_parent = {.volume_serial = 7}};
        previous.staging_parent.file_id[0] = 1;
        previous.publish_parent.file_id[0] = 2;
        WindowsDownloadOwnershipStore seed{h.journal};
        QCOMPARE(seed.record(previous), DownloadCheckpointSaveStatus::Saved);
        const auto path = h.journal + QStringLiteral("/download-ownership.json");
        const QByteArray marker{"unknown private journal must survive"};
        QFile file{path};
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(marker), marker.size());
        file.close();
        int preparations = 0;
        auto task = makeWindowsOwnedPreparedDownloadTask(h.network, h.media, h.job, h.journal, [&](const auto&) {
            ++preparations;
            return std::shared_ptr<const WindowsDownloadNetworkLaunch>{};
        });
        QVERIFY(!task->start("job-1", item, 0));
        QCOMPARE(preparations, 0);
        QCOMPARE(task->poll(1).error, DownloadFailure::InvalidResult);
        task.reset();
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), marker);
        QVERIFY(h.networks().isEmpty());
    }
    void ownedJournalRecordsTheRealSessionAndAcknowledgedCleanup() {
        Fixture h;
        QVERIFY(h.initialize());
        QVERIFY(privateDirectory(h.journal));
        h.job.workspace.max_workspace_bytes = 4096;
        auto task = makeWindowsOwnedPreparedDownloadTask(h.network, h.media, h.job, h.journal);
        QVERIFY(task);
        WindowsDownloadOwnershipStore observer{h.journal};
        QCOMPARE(observer.load().status, DownloadCheckpointLoadStatus::Missing);
        QVERIFY(task->start("job-1", item, 0));
        QCOMPARE(observer.load().status, DownloadCheckpointLoadStatus::Missing);
        // This authenticated fixture is not a PE image. CreateProcess fails
        // before its diskless helper can perform DNS, sockets or conversion.
        DownloadExecutionUpdate result;
        for (int attempt = 0; attempt < 1000; ++attempt) {
            result = task->poll(1);
            if (result.state != DownloadExecutionState::Running)
                break;
            QTest::qWait(1);
        }
        QCOMPARE(result.state, DownloadExecutionState::Failed);
        QVERIFY(result.published_mp3.empty());
        const auto loaded = observer.load();
        QCOMPARE(loaded.status, DownloadCheckpointLoadStatus::Loaded);
        QVERIFY(loaded.journal);
        QCOMPARE(loaded.journal->records.size(), std::size_t{1});
        QCOMPARE(loaded.journal->records.front().job_id, std::string{"job-1"});
        QVERIFY(loaded.journal->records.front().cleanup_complete);
        QVERIFY(h.workspaces().isEmpty());
        task.reset();
        QCOMPARE(observer.load().journal, loaded.journal);
        QVERIFY(h.networks().isEmpty());
        QVERIFY(h.mediaRuntimes().isEmpty());
    }
    void ownedJournalCancellationAndDeadlinePrecedeSessionWrites_data() {
        QTest::addColumn<bool>("cancel");
        QTest::newRow("cancel before session") << true;
        QTest::newRow("inclusive original deadline") << false;
    }
    void ownedJournalCancellationAndDeadlinePrecedeSessionWrites() {
        QFETCH(bool, cancel);
        Fixture h;
        QVERIFY(h.initialize());
        QVERIFY(privateDirectory(h.journal));
        h.job.total_timeout_ms = 10;
        auto task = makeWindowsOwnedPreparedDownloadTask(h.network, h.media, h.job, h.journal);
        QVERIFY(task);
        QVERIFY(task->start("job-1", item, 100));
        if (cancel)
            task->cancel();
        const auto update = task->poll(110);
        QCOMPARE(update.state, cancel ? DownloadExecutionState::Cancelled : DownloadExecutionState::Failed);
        QCOMPARE(update.error, cancel ? DownloadFailure::None : DownloadFailure::Timeout);
        WindowsDownloadOwnershipStore observer{h.journal};
        QCOMPARE(observer.load().status, DownloadCheckpointLoadStatus::Missing);
        QVERIFY(h.workspaces().isEmpty());
        task.reset();
        QVERIFY(h.networks().isEmpty());
        QVERIFY(h.mediaRuntimes().isEmpty());
    }
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
