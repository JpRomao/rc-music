#include "adapters/WindowsYoutubeResolverProcess.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLibraryInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <algorithm>
#include <memory>
#include <stdexcept>

using namespace music_player::m0;
namespace {
const QString video_id = QStringLiteral("testVideo01");
WindowsDownloadProcessConfig configuration(const QTemporaryDir& directory, QStringList arguments) {
    WindowsDownloadProcessConfig result;
    result.executable = QString::fromUtf8(RC_MUSIC_DOWNLOAD_PROCESS_HELPER_PATH);
    result.arguments = std::move(arguments);
    result.working_directory = directory.path();
    QProcessEnvironment environment;
    environment.insert(QStringLiteral("PATH"), QLibraryInfo::path(QLibraryInfo::BinariesPath));
    environment.insert(QStringLiteral("SystemRoot"), qEnvironmentVariable("SystemRoot"));
    result.environment = environment;
    result.max_stdout_bytes = 1024 * 1024;
    result.max_stderr_bytes = 128 * 1024;
    result.max_memory_bytes = 256 * 1024 * 1024;
    result.total_timeout_ms = 5000;
    return result;
}
struct Collected final {
    YoutubeResolverProcessUpdate update;
    QByteArray output, errors;
    qsizetype largest_chunk{};
};
bool collect(WindowsYoutubeResolverProcess& process, Collected& result, const qint64 now_ms = 0) {
    QElapsedTimer elapsed;
    elapsed.start();
    do {
        QCoreApplication::processEvents();
        result.update = process.poll(now_ms);
        result.largest_chunk =
            std::max(result.largest_chunk, result.update.stdout_chunk.size() + result.update.stderr_chunk.size());
        result.output += result.update.stdout_chunk;
        result.errors += result.update.stderr_chunk;
        if (result.update.state != YoutubeResolverProcessState::Running)
            return true;
        QThread::msleep(1);
    } while (elapsed.elapsed() < 5000);
    return false;
}
struct DrainGuard final {
    WindowsYoutubeResolverProcess& process;
    QTemporaryDir* directory{};
    ~DrainGuard() {
        process.cancel();
        Collected ignored;
        if (!collect(process, ignored) && directory)
            directory->setAutoRemove(false);
    }
};
auto prepare(WindowsDownloadProcessConfig config, const std::shared_ptr<const void>& lifetime) {
    return [config = std::move(config), lifetime](const QString&) {
        return std::optional<WindowsYoutubeResolverPreparedProcess>{{config, lifetime}};
    };
}
} // namespace
class WindowsYoutubeResolverProcessTests final : public QObject {
    Q_OBJECT
  private slots:
    void constructionDoesNotPrepareOrLaunchAndStartIsOneShot() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        int calls{};
        QString prepared_id;
        WindowsYoutubeResolverProcess process([&](const QString& id) {
            ++calls;
            prepared_id = id;
            return std::optional<WindowsYoutubeResolverPreparedProcess>{
                {configuration(directory, {"echo", id, QStringLiteral("one separated 日本")}),
                 std::make_shared<int>(7)}};
        });
        DrainGuard guard{process, &directory};
        QCOMPARE(calls, 0);
        QVERIFY(process.start(video_id, 0));
        QVERIFY(!process.start(video_id, 0));
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(calls, 1);
        QCOMPARE(prepared_id, video_id);
        QCOMPARE(result.update.state, YoutubeResolverProcessState::Exited);
        QCOMPARE(result.update.exit_code, std::optional<int>{0});
        QCOMPARE(result.update.error, DownloadFailure::None);
        QCOMPARE(QJsonDocument::fromJson(result.output).array(),
                 QJsonArray::fromStringList({video_id, QStringLiteral("one separated 日本")}));
        QVERIFY(!process.start(video_id, 1));
        const auto again = process.poll(1);
        QCOMPARE(again.state, YoutubeResolverProcessState::Exited);
        QVERIFY(again.stdout_chunk.isEmpty());
        QVERIFY(again.stderr_chunk.isEmpty());
    }
    void malformedIdentifiersAreRejectedBeforeTrustedPreparation_data() {
        QTest::addColumn<QString>("id");
        QTest::newRow("empty") << QString{};
        QTest::newRow("url") << QStringLiteral("https://youtube.com/watch?v=testVideo01");
        QTest::newRow("option") << QStringLiteral("--exec=evil");
        QTest::newRow("short") << QStringLiteral("short");
        QTest::newRow("space") << QStringLiteral("testVideo 1");
    }
    void malformedIdentifiersAreRejectedBeforeTrustedPreparation() {
        QFETCH(QString, id);
        int calls{};
        WindowsYoutubeResolverProcess process([&](const QString&) {
            ++calls;
            return std::optional<WindowsYoutubeResolverPreparedProcess>{};
        });
        QVERIFY(!process.start(id, 0));
        QCOMPARE(calls, 0);
        const auto result = process.poll(0);
        QCOMPARE(result.state, YoutubeResolverProcessState::Failed);
        QCOMPARE(result.error, DownloadFailure::InvalidResult);
    }
    void unavailableThrowingOrUnleasedPreparationCannotLaunch_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("missing") << 0;
        QTest::newRow("throws") << 1;
        QTest::newRow("unleased") << 2;
        QTest::newRow("inherited environment") << 3;
    }
    void unavailableThrowingOrUnleasedPreparationCannotLaunch() {
        QFETCH(int, mode);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        WindowsYoutubeResolverProcess process(
            [&](const QString&) -> std::optional<WindowsYoutubeResolverPreparedProcess> {
                if (mode == 0)
                    return {};
                if (mode == 1)
                    throw std::runtime_error("not a user-facing stderr message");
                auto config = configuration(directory, {"hang"});
                if (mode == 3)
                    config.environment.reset();
                return WindowsYoutubeResolverPreparedProcess{config, mode == 2 ? nullptr : std::make_shared<int>(7)};
            });
        DrainGuard guard{process, &directory};
        QVERIFY(!process.start(video_id, 0));
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, YoutubeResolverProcessState::Failed);
        QCOMPARE(result.update.error, DownloadFailure::Unavailable);
        QVERIFY(result.output.isEmpty());
        QVERIFY(result.errors.isEmpty());
        QVERIFY(!result.update.exit_code);
    }
    void privateInputIsEchoedAndLeaseLivesUntilTheStoppedPipeAcknowledgement() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        std::weak_ptr<const int> lease;
        const QByteArray input("{\"synthetic\":\"local only\"}");
        WindowsYoutubeResolverProcess process([&](const QString&) {
            auto retained = std::make_shared<const int>(7);
            lease = retained;
            auto config = configuration(directory, {"read-stdin"});
            config.standard_input = input;
            return std::optional<WindowsYoutubeResolverPreparedProcess>{{config, retained}};
        });
        DrainGuard guard{process, &directory};
        QVERIFY(process.start(video_id, 0));
        QVERIFY(!lease.expired());
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, YoutubeResolverProcessState::Exited);
        QCOMPARE(result.output, input);
        QVERIFY(lease.expired());
    }
    void aRefusedNativeStartRetainsItsLeaseUntilPolledToTerminal() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        std::weak_ptr<const int> lease;
        WindowsYoutubeResolverProcess process([&](const QString&) {
            auto retained = std::make_shared<const int>(7);
            lease = retained;
            auto config = configuration(directory, {"echo"});
            config.executable = QStringLiteral("relative.exe");
            return std::optional<WindowsYoutubeResolverPreparedProcess>{{config, retained}};
        });
        DrainGuard guard{process, &directory};
        QVERIFY(!process.start(video_id, 0));
        QVERIFY(!lease.expired());
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, YoutubeResolverProcessState::Failed);
        QCOMPARE(result.update.error, DownloadFailure::InvalidResult);
        QVERIFY(lease.expired());
    }
    void nonzeroExitIsFailureAndFreeStderrIsOnlyBoundedData() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        WindowsYoutubeResolverProcess process(prepare(configuration(directory, {"nonzero"}), std::make_shared<int>(7)));
        DrainGuard guard{process, &directory};
        QVERIFY(process.start(video_id, 0));
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, YoutubeResolverProcessState::Failed);
        QCOMPARE(result.update.error, DownloadFailure::Unavailable);
        QCOMPARE(result.update.exit_code, std::optional<int>{17});
        QCOMPARE(result.output, QByteArray("OUT"));
        QCOMPARE(result.errors, QByteArray("ERR"));
    }
    void overflowTimeoutCancellationAndClockRegressionWaitForDrain_data() {
        QTest::addColumn<int>("mode");
        QTest::addColumn<int>("failure");
        QTest::newRow("stdout") << 0 << static_cast<int>(DownloadFailure::LimitExceeded);
        QTest::newRow("stderr") << 1 << static_cast<int>(DownloadFailure::LimitExceeded);
        QTest::newRow("timeout") << 2 << static_cast<int>(DownloadFailure::Timeout);
        QTest::newRow("cancel") << 3 << static_cast<int>(DownloadFailure::None);
        QTest::newRow("clock backwards") << 4 << static_cast<int>(DownloadFailure::InvalidResult);
    }
    void overflowTimeoutCancellationAndClockRegressionWaitForDrain() {
        QFETCH(int, mode);
        QFETCH(int, failure);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto config = configuration(directory, mode == 0   ? QStringList{"fill", "16384", "0"}
                                               : mode == 1 ? QStringList{"fill", "0", "16384"}
                                                           : QStringList{"hang"});
        config.max_stdout_bytes = mode == 0 ? 1024 : 64 * 1024;
        config.max_stderr_bytes = mode == 1 ? 1024 : 64 * 1024;
        config.total_timeout_ms = 100;
        WindowsYoutubeResolverProcess process(prepare(config, std::make_shared<int>(7)));
        DrainGuard guard{process, &directory};
        QVERIFY(process.start(video_id, 10));
        if (mode == 3)
            process.cancel();
        Collected result;
        QVERIFY(collect(process, result, mode == 2 ? 110 : mode == 4 ? 9 : 10));
        QCOMPARE(result.update.state,
                 mode == 3 ? YoutubeResolverProcessState::Cancelled : YoutubeResolverProcessState::Failed);
        QCOMPARE(result.update.error, static_cast<DownloadFailure>(failure));
        QVERIFY(result.largest_chunk <= download_process_poll_bytes);
    }
    void nativeJobStillRejectsAnAttemptedDescendant() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        WindowsYoutubeResolverProcess process(
            prepare(configuration(directory, {"spawn-probe"}), std::make_shared<int>(7)));
        DrainGuard guard{process, &directory};
        QVERIFY(process.start(video_id, 0));
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, YoutubeResolverProcessState::Exited);
        QCOMPARE(result.update.exit_code, std::optional<int>{0});
        QCOMPARE(result.output, QByteArray("SPAWN_BLOCKED"));
    }
    void workerDestructorCancelsAHangingOwnedHelperAndReleasesOnlyAfterAcknowledgement() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        std::weak_ptr<const int> lease;
        auto process = std::make_unique<WindowsYoutubeResolverProcess>([&](const QString&) {
            auto retained = std::make_shared<const int>(7);
            lease = retained;
            return std::optional<WindowsYoutubeResolverPreparedProcess>{{configuration(directory, {"hang"}), retained}};
        });
        QVERIFY(process->start(video_id, 0));
        QVERIFY(!lease.expired());
        process.reset();
        if (!lease.expired())
            directory.setAutoRemove(false);
        QVERIFY2(lease.expired(), "No native ACK: the lease and temporary directory were conservatively retained");
    }
};
QTEST_GUILESS_MAIN(WindowsYoutubeResolverProcessTests)
#include "WindowsYoutubeResolverProcessTests.moc"
