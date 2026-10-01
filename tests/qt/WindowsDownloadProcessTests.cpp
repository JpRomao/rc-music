#include "WindowsDownloadProcess.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <algorithm>
#include <memory>

using namespace music_player::m0;

namespace {

QString helperPath() { return QString::fromUtf8(RC_MUSIC_DOWNLOAD_PROCESS_HELPER_PATH); }

WindowsDownloadProcessConfig configuration(const QTemporaryDir& directory, const QStringList& arguments) {
    WindowsDownloadProcessConfig config;
    config.executable = helperPath();
    config.arguments = arguments;
    config.working_directory = directory.path();
    return config;
}

bool terminal(WindowsDownloadProcessState state) {
    return state == WindowsDownloadProcessState::Exited || state == WindowsDownloadProcessState::Failed ||
           state == WindowsDownloadProcessState::Cancelled;
}

struct Collected final {
    WindowsDownloadProcessUpdate update{};
    QByteArray output{};
    QByteArray error_output{};
    qint64 maximum_poll_bytes{0};
};

bool collect(WindowsDownloadProcess& process, Collected& result, qint64 now_ms = 0, int timeout_ms = 5000) {
    QElapsedTimer timeout;
    timeout.start();
    do {
        QCoreApplication::processEvents();
        result.update = process.poll(now_ms);
        const auto bytes = result.update.stdout_chunk.size() + result.update.stderr_chunk.size();
        result.maximum_poll_bytes = std::max(result.maximum_poll_bytes, bytes);
        result.output += result.update.stdout_chunk;
        result.error_output += result.update.stderr_chunk;
        if (terminal(result.update.state))
            return true;
        QThread::msleep(1);
    } while (timeout.elapsed() < timeout_ms);
    return false;
}

bool waitForReady(WindowsDownloadProcess& process, QByteArray& output) {
    QElapsedTimer timeout;
    timeout.start();
    do {
        QCoreApplication::processEvents();
        const auto update = process.poll(0);
        output += update.stdout_chunk;
        if (output.contains('\n'))
            return true;
        if (terminal(update.state))
            return false;
        QThread::msleep(1);
    } while (timeout.elapsed() < 5000);
    return false;
}

DWORD readyPid(const QByteArray& bytes) {
    return static_cast<DWORD>(
        QJsonDocument::fromJson(bytes.trimmed()).object().value(QStringLiteral("pid")).toInteger());
}

struct NativeProcessGuard final {
    HANDLE handle{nullptr};
    ~NativeProcessGuard() {
        if (!handle)
            return;
        if (::WaitForSingleObject(handle, 0) == WAIT_TIMEOUT) {
            ::TerminateProcess(handle, 90);
            ::WaitForSingleObject(handle, 5000);
        }
        ::CloseHandle(handle);
    }
};

struct QtProcessGuard final {
    QProcess& process;
    ~QtProcessGuard() {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(5000);
        }
    }
};

int supervisorChild(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    WindowsDownloadProcess process;
    if (!directory.isValid() || !process.start(configuration(directory, {QStringLiteral("hang")}), 0))
        return 82;
    QByteArray output;
    if (!waitForReady(process, output))
        return 83;
    DWORD written = 0;
    if (!::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), output.constData(), static_cast<DWORD>(output.size()), &written,
                     nullptr) ||
        static_cast<qsizetype>(written) != output.size())
        return 84;
    // The parent test deliberately terminates this supervisor, not its child.
    ::Sleep(INFINITE);
    return 85;
}

} // namespace

class WindowsDownloadProcessTests final : public QObject {
    Q_OBJECT

  private slots:
    void startsIdleWithoutAChildOrOutput() {
        WindowsDownloadProcess process;
        const auto update = process.poll(0);
        QCOMPARE(update.state, WindowsDownloadProcessState::Idle);
        QVERIFY(update.stdout_chunk.isEmpty());
        QVERIFY(update.stderr_chunk.isEmpty());
        QVERIFY(!update.exit_code);
        process.cancel();
        QCOMPARE(process.poll(0).state, WindowsDownloadProcessState::Cancelled);
    }

    void preservesUnicodeAndShellPunctuationAsArguments() {
        QTemporaryDir directory(QDir::tempPath() + QStringLiteral("/RCMusic-process-日本-XXXXXX"));
        QVERIFY(directory.isValid());
        const QStringList payload{QStringLiteral("ação 日本"), QStringLiteral("a\"b\\c\\"),
                                  QStringLiteral("& | > < % ! ; $(echo) `value`"), QString{}};
        auto arguments = QStringList{QStringLiteral("echo")};
        arguments += payload;
        WindowsDownloadProcess process;
        QVERIFY(process.start(configuration(directory, arguments), 0));
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, WindowsDownloadProcessState::Exited);
        QCOMPARE(result.update.exit_code, std::optional<int>{0});
        QCOMPARE(QJsonDocument::fromJson(result.output).array(), QJsonArray::fromStringList(payload));
        QVERIFY(result.error_output.isEmpty());
    }

    void drainsBothChannelsWithBoundedPollChunksBeforeExit() {
        QTemporaryDir directory;
        WindowsDownloadProcess process;
        QVERIFY(process.start(
            configuration(directory, {QStringLiteral("fill"), QStringLiteral("327680"), QStringLiteral("131072")}), 0));
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, WindowsDownloadProcessState::Exited);
        QCOMPARE(result.update.exit_code, std::optional<int>{0});
        QCOMPARE(result.output, QByteArray(327680, 'A'));
        QCOMPARE(result.error_output, QByteArray(131072, 'B'));
        QVERIFY(result.maximum_poll_bytes <= download_process_poll_bytes);
        const auto after = process.poll(0);
        QVERIFY(after.stdout_chunk.isEmpty());
        QVERIFY(after.stderr_chunk.isEmpty());
        QCOMPARE(after.state, WindowsDownloadProcessState::Exited);
    }

    void hidesConsoleAndAppliesJobLimitsBeforeHelperExecution() {
        QTemporaryDir directory;
        auto config = configuration(directory, {QStringLiteral("inspect")});
        config.max_memory_bytes = 256ULL * 1024 * 1024;
        WindowsDownloadProcess process;
        QVERIFY(process.start(config, 0));
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.exit_code, std::optional<int>{0});
        const auto object = QJsonDocument::fromJson(result.output).object();
        QCOMPARE(object.value(QStringLiteral("console")).toBool(true), false);
        QCOMPARE(object.value(QStringLiteral("in_job")).toBool(), true);
        QCOMPARE(object.value(QStringLiteral("kill_on_close")).toBool(), true);
        QCOMPARE(object.value(QStringLiteral("active_process_limit")).toInt(), 1);
        QCOMPARE(object.value(QStringLiteral("memory_limit")).toString(), QString::number(config.max_memory_bytes));
    }

    void handlesQuotaBoundaries_data() {
        QTest::addColumn<quint64>("stdout_count");
        QTest::addColumn<quint64>("stderr_count");
        QTest::addColumn<bool>("exceeded");
        QTest::newRow("stdout below") << quint64{65535} << quint64{0} << false;
        QTest::newRow("stdout exact") << quint64{65536} << quint64{0} << false;
        QTest::newRow("stdout above") << quint64{65537} << quint64{0} << true;
        QTest::newRow("stderr below") << quint64{0} << quint64{65535} << false;
        QTest::newRow("stderr exact") << quint64{0} << quint64{65536} << false;
        QTest::newRow("stderr above") << quint64{0} << quint64{65537} << true;
        QTest::newRow("continuous stdout flood") << quint64{16 * 1024 * 1024} << quint64{0} << true;
        QTest::newRow("continuous stderr flood") << quint64{0} << quint64{16 * 1024 * 1024} << true;
    }

    void handlesQuotaBoundaries() {
        QFETCH(quint64, stdout_count);
        QFETCH(quint64, stderr_count);
        QFETCH(bool, exceeded);
        QTemporaryDir directory;
        auto config = configuration(
            directory, {QStringLiteral("fill"), QString::number(stdout_count), QString::number(stderr_count)});
        config.max_stdout_bytes = 65536;
        config.max_stderr_bytes = 65536;
        WindowsDownloadProcess process;
        QVERIFY(process.start(config, 0));
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state,
                 exceeded ? WindowsDownloadProcessState::Failed : WindowsDownloadProcessState::Exited);
        QCOMPARE(result.update.error,
                 exceeded ? WindowsDownloadProcessError::LimitExceeded : WindowsDownloadProcessError::None);
        QVERIFY(result.output.size() <= 65536);
        QVERIFY(result.error_output.size() <= 65536);
        QVERIFY(result.maximum_poll_bytes <= download_process_poll_bytes);
        if (!exceeded) {
            QCOMPARE(static_cast<quint64>(result.output.size()), stdout_count);
            QCOMPARE(static_cast<quint64>(result.error_output.size()), stderr_count);
        }
    }

    void acknowledgesCancellationOnlyAfterOwnChildHasTerminated() {
        QTemporaryDir directory;
        WindowsDownloadProcess process;
        QVERIFY(process.start(configuration(directory, {QStringLiteral("hang")}), 0));
        QByteArray ready;
        QVERIFY(waitForReady(process, ready));
        NativeProcessGuard child{::OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, readyPid(ready))};
        QVERIFY(child.handle != nullptr);
        process.cancel();
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, WindowsDownloadProcessState::Cancelled);
        QCOMPARE(::WaitForSingleObject(child.handle, 0), DWORD{WAIT_OBJECT_0});
        process.cancel();
        QCOMPARE(process.poll(0).state, WindowsDownloadProcessState::Cancelled);
    }

    void expiresAtInclusiveDeadlineAndRejectsBackwardClock() {
        QTemporaryDir directory;
        auto config = configuration(directory, {QStringLiteral("hang")});
        config.total_timeout_ms = 10;
        WindowsDownloadProcess process;
        QVERIFY(process.start(config, 0));
        QByteArray ready;
        QVERIFY(waitForReady(process, ready));
        QCOMPARE(process.poll(9).state, WindowsDownloadProcessState::Running);
        Collected result;
        QVERIFY(collect(process, result, 10));
        QCOMPARE(result.update.state, WindowsDownloadProcessState::Failed);
        QCOMPARE(result.update.error, WindowsDownloadProcessError::Timeout);
        QVERIFY(process.start(config, 0));
        QVERIFY(collect(process, result, -1));
        QCOMPARE(result.update.state, WindowsDownloadProcessState::Failed);
        QCOMPARE(result.update.error, WindowsDownloadProcessError::InvalidConfiguration);
    }

    void preservesNonzeroExitAndDoesNotTreatCrashAsSuccess() {
        QTemporaryDir directory;
        WindowsDownloadProcess process;
        QVERIFY(process.start(configuration(directory, {QStringLiteral("nonzero")}), 0));
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, WindowsDownloadProcessState::Exited);
        QCOMPARE(result.update.exit_code, std::optional<int>{17});
        QCOMPARE(result.output, QByteArrayLiteral("OUT"));
        QCOMPARE(result.error_output, QByteArrayLiteral("ERR"));
        QVERIFY(process.start(configuration(directory, {QStringLiteral("crash")}), 0));
        Collected crashed;
        QVERIFY(collect(process, crashed));
        QCOMPARE(crashed.update.state, WindowsDownloadProcessState::Exited);
        QVERIFY(crashed.update.exit_code && *crashed.update.exit_code != 0);
    }

    void rejectsInvalidConfigurationWithoutResourcesAndCanStartAfterward() {
        QTemporaryDir directory;
        const auto valid = configuration(directory, {QStringLiteral("echo")});
        auto config = valid;
        WindowsDownloadProcess process;
        config.executable = QStringLiteral("relative.exe");
        QVERIFY(!process.start(config, 0));
        QCOMPARE(process.poll(0).state, WindowsDownloadProcessState::Failed);
        QCOMPARE(process.poll(0).error, WindowsDownloadProcessError::InvalidConfiguration);
        config = valid;
        config.arguments.append(QString::fromUtf8("nul\0argument", 12));
        QVERIFY(!process.start(config, 0));
        config = valid;
        config.working_directory = QStringLiteral("relative");
        QVERIFY(!process.start(config, 0));
        config = valid;
        config.total_timeout_ms = 0;
        QVERIFY(!process.start(config, 0));
        config = valid;
        config.max_memory_bytes = 0;
        QVERIFY(!process.start(config, 0));
        config = valid;
        config.max_stdout_bytes = 0;
        QVERIFY(!process.start(config, 0));
        QVERIFY(process.start(valid, 0));
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.exit_code, std::optional<int>{0});
    }

    void rejectsConcurrentStartAndPreventsChildCreation() {
        QTemporaryDir directory;
        WindowsDownloadProcess process;
        const auto hang = configuration(directory, {QStringLiteral("hang")});
        QVERIFY(process.start(hang, 0));
        QVERIFY(!process.start(hang, 0));
        process.cancel();
        Collected cancelled;
        QVERIFY(collect(process, cancelled));
        QVERIFY(process.start(configuration(directory, {QStringLiteral("spawn-probe")}), 0));
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.exit_code, std::optional<int>{0});
        QCOMPARE(result.output, QByteArrayLiteral("SPAWN_BLOCKED"));
    }

    void destructorStopsOnlyItsOwnedProcess() {
        QTemporaryDir directory;
        NativeProcessGuard child;
        {
            WindowsDownloadProcess process;
            QVERIFY(process.start(configuration(directory, {QStringLiteral("hang")}), 0));
            QByteArray ready;
            QVERIFY(waitForReady(process, ready));
            child.handle = ::OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, readyPid(ready));
            QVERIFY(child.handle != nullptr);
        }
        QCOMPARE(::WaitForSingleObject(child.handle, 5000), DWORD{WAIT_OBJECT_0});
    }

    void supervisorDeathTerminatesItsChildThroughJobOwnership() {
        QProcess supervisor;
        QtProcessGuard guard{supervisor};
        supervisor.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--supervise-test")});
        QVERIFY(supervisor.waitForStarted(5000));
        QTRY_VERIFY_WITH_TIMEOUT(supervisor.canReadLine(), 5000);
        const auto ready = supervisor.readLine();
        NativeProcessGuard child{::OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, readyPid(ready))};
        QVERIFY(child.handle != nullptr);
        supervisor.kill();
        QVERIFY(supervisor.waitForFinished(5000));
        QCOMPARE(::WaitForSingleObject(child.handle, 5000), DWORD{WAIT_OBJECT_0});
    }
};

int main(int argc, char* argv[]) {
    if (argc == 2 && QByteArray(argv[1]) == QByteArrayLiteral("--supervise-test"))
        return supervisorChild(argc, argv);
    QCoreApplication application(argc, argv);
    WindowsDownloadProcessTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "WindowsDownloadProcessTests.moc"
