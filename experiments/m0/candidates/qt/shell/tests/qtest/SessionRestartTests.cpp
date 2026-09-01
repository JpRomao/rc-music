#include "adapters/QtJsonWindowsSessionStore.hpp"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <utility>

using music_player::m0::LocalQueueOccurrenceId;
using music_player::m0::QtJsonWindowsSessionStore;
using music_player::m0::SessionLoadStatus;
using music_player::m0::SessionSaveRequest;
using music_player::m0::SessionSaveStatus;
using music_player::m0::SessionSnapshot;

namespace {

constexpr int process_start_timeout_ms = 5'000;
constexpr int process_finish_timeout_ms = 10'000;
constexpr int process_cleanup_timeout_ms = 5'000;

const QByteArray before_primary_commit_marker{"M0_SESSION_BEFORE_PRIMARY_COMMIT"};
const QByteArray replace_failed_marker{"M0_SESSION_REPLACE_FAILED"};

class UniqueWindowsHandle final {
  public:
    explicit UniqueWindowsHandle(HANDLE handle) noexcept : handle_(handle) {}

    ~UniqueWindowsHandle() { static_cast<void>(close()); }

    UniqueWindowsHandle(const UniqueWindowsHandle&) = delete;
    UniqueWindowsHandle& operator=(const UniqueWindowsHandle&) = delete;

    UniqueWindowsHandle(UniqueWindowsHandle&& other) noexcept
        : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE)) {}

    UniqueWindowsHandle& operator=(UniqueWindowsHandle&& other) noexcept {
        if (this != &other) {
            static_cast<void>(close());
            handle_ = std::exchange(other.handle_, INVALID_HANDLE_VALUE);
        }
        return *this;
    }

    [[nodiscard]] bool isValid() const noexcept { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }

    bool close() noexcept {
        if (!isValid())
            return true;
        const HANDLE handle = std::exchange(handle_, INVALID_HANDLE_VALUE);
        return CloseHandle(handle) != FALSE;
    }

  private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
};

class RunningProcessGuard final {
  public:
    explicit RunningProcessGuard(QProcess& process) noexcept : process_(process) {}

    ~RunningProcessGuard() {
        if (process_.state() == QProcess::NotRunning)
            return;
        process_.kill();
        static_cast<void>(process_.waitForFinished(process_cleanup_timeout_ms));
    }

    RunningProcessGuard(const RunningProcessGuard&) = delete;
    RunningProcessGuard& operator=(const RunningProcessGuard&) = delete;

  private:
    QProcess& process_;
};

[[nodiscard]] QString sessionPath(const QString& root) { return QDir{root}.filePath(QStringLiteral("session.json")); }

[[nodiscard]] QString backupPath(const QString& root) {
    return QDir{root}.filePath(QStringLiteral("session.json.bak"));
}

[[nodiscard]] QString lockPath(const QString& root) { return QDir{root}.filePath(QStringLiteral("session.json.lock")); }

[[nodiscard]] QString retryGatePath(const QString& root) {
    return QDir{root}.filePath(QStringLiteral("m0-session-retry"));
}

[[nodiscard]] bool writeAll(const QString& path, const QByteArray& bytes) {
    QFile file{path};
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(bytes) == bytes.size() && file.flush();
}

[[nodiscard]] QByteArray readAll(const QString& path) {
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

[[nodiscard]] UniqueWindowsHandle denyDeleteSharing(const QString& path) {
    return UniqueWindowsHandle{CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                           FILE_ATTRIBUTE_NORMAL, nullptr)};
}

[[nodiscard]] SessionSnapshot restartSnapshot(const QString& unicode_audio) {
    return {
        .queue =
            {
                .items = {{{77}, std::filesystem::path{unicode_audio.toStdWString()}}},
                .current_occurrence_id = LocalQueueOccurrenceId{77},
            },
        .position_ms = 750,
    };
}

[[nodiscard]] QProcessEnvironment shellEnvironment() {
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    environment.insert(QStringLiteral("QSG_RHI_BACKEND"), QStringLiteral("software"));
    environment.insert(QStringLiteral("QT_QUICK_CONTROLS_STYLE"), QStringLiteral("Basic"));
    return environment;
}

void configureShellProcess(QProcess& process, const QString& executable, const QString& smoke,
                           const QString& config_root) {
    process.setProcessEnvironment(shellEnvironment());
    process.setProgram(executable);
    process.setArguments(
        {QStringLiteral("--m0-smoke=") + smoke, QStringLiteral("--m0-test-config-root=") + config_root});
}

[[nodiscard]] bool waitForStdoutMarker(QProcess& process, const QByteArray& marker, QByteArray& captured,
                                       const int timeout_ms) {
    const auto contains_protocol_line = [&marker](const QByteArray& output) {
        for (QByteArray line : output.split('\n')) {
            if (line.endsWith('\r'))
                line.chop(1);
            if (line == marker)
                return true;
        }
        return false;
    };
    QDeadlineTimer deadline{timeout_ms};
    captured.append(process.readAllStandardOutput());
    while (!contains_protocol_line(captured)) {
        if (process.state() == QProcess::NotRunning)
            break;
        const qint64 remaining = deadline.remainingTime();
        if (remaining <= 0)
            break;
        static_cast<void>(process.waitForReadyRead(static_cast<int>(remaining)));
        captured.append(process.readAllStandardOutput());
    }
    captured.append(process.readAllStandardOutput());
    return contains_protocol_line(captured);
}

[[nodiscard]] QByteArray diagnostics(QProcess& process, QByteArray captured_stdout = {}) {
    captured_stdout.append(process.readAllStandardOutput());
    captured_stdout.append(process.readAllStandardError());
    if (captured_stdout.isEmpty())
        captured_stdout = process.errorString().toUtf8();
    return captured_stdout;
}

} // namespace

class SessionRestartTests final : public QObject {
    Q_OBJECT

  public:
    SessionRestartTests(QString shell_executable, QString audio_fixture)
        : shell_executable_(std::move(shell_executable)), audio_fixture_(std::move(audio_fixture)) {}

  private slots:
    void restoresAUnicodeSessionPausedAtItsConfirmedPosition() {
        QTemporaryDir config_root{QDir::tempPath() + QStringLiteral("/music player session 日本-XXXXXX")};
        QVERIFY(config_root.isValid());
        const QString unicode_audio = config_root.filePath(QStringLiteral("Abertura 日本語.wav"));
        QVERIFY(QFile::copy(audio_fixture_, unicode_audio));

        const SessionSnapshot snapshot = restartSnapshot(unicode_audio);
        QtJsonWindowsSessionStore store{config_root.path()};
        QCOMPARE(store.save(SessionSaveRequest{.snapshot = snapshot}), SessionSaveStatus::Saved);

        QProcess process;
        configureShellProcess(process, shell_executable_, QStringLiteral("session-restart-read"), config_root.path());
        process.start();
        QVERIFY2(process.waitForStarted(process_start_timeout_ms), qPrintable(process.errorString()));
        if (!process.waitForFinished(process_finish_timeout_ms)) {
            process.kill();
            process.waitForFinished(process_cleanup_timeout_ms);
            QFAIL("the restored-session shell did not finish before the watchdog");
        }
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
    }

    void restoresThePreviousSessionAfterForcedTerminationBeforePrimaryCommit() {
        QTemporaryDir config_root{QDir::tempPath() + QStringLiteral("/music player crash 日本-XXXXXX")};
        QVERIFY(config_root.isValid());
        const QString unicode_audio = config_root.filePath(QStringLiteral("Abertura de crash 日本語.wav"));
        QVERIFY(QFile::copy(audio_fixture_, unicode_audio));

        const SessionSnapshot snapshot = restartSnapshot(unicode_audio);
        QtJsonWindowsSessionStore store{config_root.path()};
        QCOMPARE(store.save(SessionSaveRequest{.snapshot = snapshot}), SessionSaveStatus::Saved);
        const QString primary_path = sessionPath(config_root.path());
        const QString backup_path = backupPath(config_root.path());
        const QString lock_path = lockPath(config_root.path());
        const QByteArray original_primary = readAll(primary_path);
        QVERIFY(!original_primary.isEmpty());
        QVERIFY(!QFile::exists(backup_path));

        QProcess crashing_process;
        RunningProcessGuard crashing_guard{crashing_process};
        configureShellProcess(crashing_process, shell_executable_,
                              QStringLiteral("session-crash-before-primary-commit"), config_root.path());
        crashing_process.start();
        QVERIFY2(crashing_process.waitForStarted(process_start_timeout_ms), qPrintable(crashing_process.errorString()));

        QByteArray crash_stdout;
        const bool reached_commit = waitForStdoutMarker(crashing_process, before_primary_commit_marker, crash_stdout,
                                                        process_finish_timeout_ms);
        if (!reached_commit) {
            const QByteArray output = diagnostics(crashing_process, std::move(crash_stdout));
            QFAIL(qPrintable(QStringLiteral("the shell did not reach the primary commit barrier: %1")
                                 .arg(QString::fromUtf8(output))));
        }
        QCOMPARE(crashing_process.state(), QProcess::Running);
        QCOMPARE(readAll(primary_path), original_primary);
        QCOMPARE(readAll(backup_path), original_primary);
        QLockFile transaction_lock{lock_path};
        qint64 lock_owner_pid = 0;
        QString lock_owner_host;
        QString lock_owner_application;
        QVERIFY(transaction_lock.getLockInfo(&lock_owner_pid, &lock_owner_host, &lock_owner_application));
        QCOMPARE(lock_owner_pid, crashing_process.processId());

        crashing_process.kill();
        QVERIFY2(crashing_process.waitForFinished(process_cleanup_timeout_ms),
                 qPrintable(crashing_process.errorString()));
        QCOMPARE(crashing_process.exitStatus(), QProcess::CrashExit);
        QVERIFY(QFile::exists(lock_path));

        const auto recovered = store.load();
        QVERIFY(recovered.status == SessionLoadStatus::Loaded ||
                recovered.status == SessionLoadStatus::RecoveredFromBackup);
        QCOMPARE(recovered.snapshot, snapshot);
        QCOMPARE(store.save(SessionSaveRequest{.snapshot = snapshot}), SessionSaveStatus::Saved);
        QVERIFY(!QFile::exists(lock_path));
        QCOMPARE(store.load().snapshot, snapshot);

        QProcess restart_process;
        RunningProcessGuard restart_guard{restart_process};
        configureShellProcess(restart_process, shell_executable_, QStringLiteral("session-restart-read"),
                              config_root.path());
        restart_process.start();
        QVERIFY2(restart_process.waitForStarted(process_start_timeout_ms), qPrintable(restart_process.errorString()));
        const bool restarted = restart_process.waitForFinished(process_finish_timeout_ms);
        const QByteArray restart_output = diagnostics(restart_process);
        QVERIFY2(restarted, restart_output.constData());
        QCOMPARE(restart_process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(restart_process.exitCode() == 0, restart_output.constData());
    }

    void repairsARecoveredSessionAfterARealReplaceFailure() {
        QTemporaryDir config_root{QDir::tempPath() + QStringLiteral("/music player repair 日本-XXXXXX")};
        QVERIFY(config_root.isValid());
        const QString unicode_audio = config_root.filePath(QStringLiteral("Abertura reparada 日本語.wav"));
        QVERIFY(QFile::copy(audio_fixture_, unicode_audio));

        const SessionSnapshot snapshot = restartSnapshot(unicode_audio);
        QtJsonWindowsSessionStore store{config_root.path()};
        QCOMPARE(store.save(SessionSaveRequest{.snapshot = snapshot}), SessionSaveStatus::Saved);
        const QString primary_path = sessionPath(config_root.path());
        const QString backup_path = backupPath(config_root.path());
        const QByteArray valid_session = readAll(primary_path);
        QVERIFY(!valid_session.isEmpty());
        QVERIFY(QFile::copy(primary_path, backup_path));
        const QByteArray corrupt_primary{"{truncated-primary"};
        QVERIFY(writeAll(primary_path, corrupt_primary));

        UniqueWindowsHandle replacement_denied = denyDeleteSharing(primary_path);
        QVERIFY(replacement_denied.isValid());

        QProcess process;
        RunningProcessGuard process_guard{process};
        configureShellProcess(process, shell_executable_, QStringLiteral("session-recovery-repair"),
                              config_root.path());
        process.start();
        QVERIFY2(process.waitForStarted(process_start_timeout_ms), qPrintable(process.errorString()));

        QByteArray repair_stdout;
        const bool replace_failed =
            waitForStdoutMarker(process, replace_failed_marker, repair_stdout, process_finish_timeout_ms);
        if (!replace_failed) {
            const QByteArray output = diagnostics(process, std::move(repair_stdout));
            QFAIL(qPrintable(QStringLiteral("the shell did not expose the real replace failure: %1")
                                 .arg(QString::fromUtf8(output))));
        }
        QCOMPARE(process.state(), QProcess::Running);
        QCOMPARE(readAll(primary_path), corrupt_primary);
        QCOMPARE(readAll(backup_path), valid_session);

        QVERIFY(replacement_denied.close());
        QVERIFY(writeAll(retryGatePath(config_root.path()), QByteArray{"retry\n"}));

        const bool finished = process.waitForFinished(process_finish_timeout_ms);
        const QByteArray output = diagnostics(process, std::move(repair_stdout));
        QVERIFY2(finished, output.constData());
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, output.constData());

        const auto repaired = store.load();
        QCOMPARE(repaired.status, SessionLoadStatus::Loaded);
        QCOMPARE(repaired.snapshot, snapshot);
    }

    void usesDefaultsWhenBothSessionDocumentsAreInvalid() {
        QTemporaryDir config_root{QDir::tempPath() + QStringLiteral("/music player defaults 日本-XXXXXX")};
        QVERIFY(config_root.isValid());
        const QByteArray corrupt_primary{"{invalid-primary"};
        const QByteArray corrupt_backup{"{invalid-backup"};
        QVERIFY(writeAll(sessionPath(config_root.path()), corrupt_primary));
        QVERIFY(writeAll(backupPath(config_root.path()), corrupt_backup));

        QProcess process;
        RunningProcessGuard process_guard{process};
        configureShellProcess(process, shell_executable_, QStringLiteral("session-defaults-read"), config_root.path());
        process.start();
        QVERIFY2(process.waitForStarted(process_start_timeout_ms), qPrintable(process.errorString()));
        const bool finished = process.waitForFinished(process_finish_timeout_ms);
        const QByteArray output = diagnostics(process);
        QVERIFY2(finished, output.constData());
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, output.constData());
        QCOMPARE(readAll(sessionPath(config_root.path())), corrupt_primary);
        QCOMPARE(readAll(backupPath(config_root.path())), corrupt_backup);
    }

  private:
    QString shell_executable_;
    QString audio_fixture_;
};

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    if (arguments.size() != 3)
        return 2;
    SessionRestartTests tests(arguments.at(1), arguments.at(2));
    int test_argc = 1;
    char* test_argv[] = {argv[0], nullptr};
    return QTest::qExec(&tests, test_argc, test_argv);
}

#include "SessionRestartTests.moc"
