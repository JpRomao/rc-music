#include "WindowsDownloadProcess.hpp"
#include "WindowsDownloadReadInput.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QUuid>

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

struct NativeEventGuard final {
    HANDLE handle{nullptr};
    ~NativeEventGuard() {
        if (handle)
            ::CloseHandle(handle);
    }
};

struct NativeFileGuard final {
    HANDLE handle{INVALID_HANDLE_VALUE};
    ~NativeFileGuard() { close(); }
    void close() {
        if (handle && handle != INVALID_HANDLE_VALUE)
            ::CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
    }
    bool valid() const { return handle && handle != INVALID_HANDLE_VALUE; }
};

HANDLE fixtureFile(const QString& path, const QByteArray& bytes = QByteArrayLiteral("leased-file-input")) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        return INVALID_HANDLE_VALUE;
    file.close();
    return ::CreateFileW(path.toStdWString().c_str(), GENERIC_READ | GENERIC_WRITE | DELETE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                         FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
}

std::uintptr_t handleNumber(HANDLE value) { return reinterpret_cast<std::uintptr_t>(value); }

bool nonInheritable(const WindowsDownloadReadInput& input) {
    DWORD flags = HANDLE_FLAG_INHERIT;
    return ::GetHandleInformation(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(input.handleValue())), &flags) &&
           (flags & HANDLE_FLAG_INHERIT) == 0;
}

struct QtProcessGuard final {
    QProcess& process;
    ~QtProcessGuard() {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(5000);
        }
    }
};

class TestEnvironmentValue final {
  public:
    TestEnvironmentValue(const char* name, const QByteArray& value)
        : name_(name), present_(qEnvironmentVariableIsSet(name)), previous_(qgetenv(name)) {
        qputenv(name_, value);
    }
    ~TestEnvironmentValue() {
        if (present_)
            qputenv(name_, previous_);
        else
            qunsetenv(name_);
    }

  private:
    const char* name_;
    bool present_;
    QByteArray previous_;
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

int naturalExitChild(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    const auto arguments = application.arguments();
    const auto event_name = arguments.at(2).toStdWString();
    NativeEventGuard gate{::OpenEventW(SYNCHRONIZE, FALSE, event_name.c_str())};
    if (!gate.handle)
        return 86;
    const auto ready = QJsonDocument(QJsonObject{{QStringLiteral("pid"), qint64{::GetCurrentProcessId()}}})
                           .toJson(QJsonDocument::Compact) +
                       '\n';
    DWORD written = 0;
    if (!::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), ready.constData(), static_cast<DWORD>(ready.size()), &written,
                     nullptr) ||
        static_cast<qsizetype>(written) != ready.size())
        return 87;
    if (::WaitForSingleObject(gate.handle, 5000) != WAIT_OBJECT_0)
        return 88;
    if (!::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), "OUT", 3, &written, nullptr) || written != 3 ||
        !::WriteFile(::GetStdHandle(STD_ERROR_HANDLE), "ERR", 3, &written, nullptr) || written != 3)
        return 89;
    return arguments.at(3).toInt();
}

} // namespace

class WindowsDownloadProcessTests final : public QObject {
    Q_OBJECT

  private slots:
    void missingRequiredAppContainerNeverFallsBackToLegacyLaunch() {
        QTemporaryDir directory;
        NativeFileGuard file{fixtureFile(directory.filePath(QStringLiteral("input.bin")))};
        const auto input = WindowsDownloadReadInput::fromHandle(handleNumber(file.handle));
        QVERIFY(input);
        auto config = configuration(directory, {QStringLiteral("read-stdin")});
        config.standard_input = QByteArrayLiteral("private request must never enter a legacy child");
        config.read_input = input;
        config.require_app_container = true;
        WindowsDownloadProcess process;
        QVERIFY(!process.start(config, 0));
        const auto failed = process.poll(0);
        QCOMPARE(failed.state, WindowsDownloadProcessState::Failed);
        QCOMPARE(failed.error, WindowsDownloadProcessError::Isolation);
        QVERIFY(failed.stdout_chunk.isEmpty());
        QVERIFY(failed.stderr_chunk.isEmpty());
        QVERIFY(nonInheritable(*input));
        config.require_app_container = false;
        QVERIFY(process.start(config, 0));
        Collected completed;
        QVERIFY(collect(process, completed));
        QCOMPARE(completed.update.exit_code, std::optional<int>{0});
        QCOMPARE(completed.output, config.standard_input);
        QVERIFY(nonInheritable(*input));
    }

    void readInputRejectsNonFileObjectsAndDoesNotChangeBorrowedHandleFlags() {
        QVERIFY(!WindowsDownloadReadInput::fromHandle(0));
        QVERIFY(!WindowsDownloadReadInput::fromHandle(handleNumber(INVALID_HANDLE_VALUE)));
        QVERIFY(!WindowsDownloadReadInput::fromHandle(handleNumber(::GetCurrentProcess())));
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        NativeFileGuard folder{::CreateFileW(directory.path().toStdWString().c_str(), FILE_LIST_DIRECTORY,
                                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                             OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                             nullptr)};
        QVERIFY(folder.valid());
        QVERIFY(!WindowsDownloadReadInput::fromHandle(handleNumber(folder.handle)));
        NativeFileGuard pipe_reader;
        NativeFileGuard pipe_writer;
        QVERIFY(::CreatePipe(&pipe_reader.handle, &pipe_writer.handle, nullptr, 0));
        QVERIFY(!WindowsDownloadReadInput::fromHandle(handleNumber(pipe_reader.handle)));
        NativeEventGuard event{::CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        QVERIFY(event.handle);
        QVERIFY(!WindowsDownloadReadInput::fromHandle(handleNumber(event.handle)));
        NativeFileGuard file{fixtureFile(directory.filePath(QStringLiteral("input.bin")))};
        QVERIFY(file.valid());
        QVERIFY(::SetHandleInformation(file.handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT));
        const auto input = WindowsDownloadReadInput::fromHandle(handleNumber(file.handle));
        QVERIFY(input);
        QVERIFY(nonInheritable(*input));
        DWORD flags = 0;
        QVERIFY(::GetHandleInformation(file.handle, &flags));
        QVERIFY(flags & HANDLE_FLAG_INHERIT);
    }

    void readInputIsReadOnlyAndUsesAnIndependentCursor() {
        QTemporaryDir directory;
        NativeFileGuard file{fixtureFile(directory.filePath(QStringLiteral("input.bin")))};
        QVERIFY(file.valid());
        LARGE_INTEGER position{};
        position.QuadPart = 5;
        QVERIFY(::SetFilePointerEx(file.handle, position, nullptr, FILE_BEGIN));
        const auto input = WindowsDownloadReadInput::fromHandle(handleNumber(file.handle));
        QVERIFY(input);
        const auto reduced = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(input->handleValue()));
        QVERIFY(reduced != file.handle);
        QVERIFY(nonInheritable(*input));
        char bytes[6]{};
        DWORD read = 0;
        QVERIFY(::ReadFile(reduced, bytes, sizeof(bytes), &read, nullptr));
        QCOMPARE(QByteArray(bytes, read), QByteArrayLiteral("leased"));
        position.QuadPart = 0;
        LARGE_INTEGER original_position{};
        QVERIFY(::SetFilePointerEx(file.handle, position, &original_position, FILE_CURRENT));
        QCOMPARE(original_position.QuadPart, LONGLONG{5});
        DWORD written = 0;
        QVERIFY(!::WriteFile(reduced, "X", 1, &written, nullptr));
        QCOMPARE(::GetLastError(), DWORD{ERROR_ACCESS_DENIED});
        QVERIFY(!::SetEndOfFile(reduced));
        QCOMPARE(::GetLastError(), DWORD{ERROR_ACCESS_DENIED});
        FILE_DISPOSITION_INFO disposition{TRUE};
        QVERIFY(!::SetFileInformationByHandle(reduced, FileDispositionInfo, &disposition, sizeof(disposition)));
        QCOMPARE(::GetLastError(), DWORD{ERROR_ACCESS_DENIED});
    }

    void readInputAcceptsAnEmptyRegularFile() {
        QTemporaryDir directory;
        NativeFileGuard file{fixtureFile(directory.filePath(QStringLiteral("empty.bin")), {})};
        QVERIFY(file.valid());
        const auto input = WindowsDownloadReadInput::fromHandle(handleNumber(file.handle));
        QVERIFY(input);
        QVERIFY(nonInheritable(*input));
    }

    void childReceivesOnlyTheSelectedReadHandleAfterTheSourceNameChanges() {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("input.bin"));
        NativeFileGuard file{fixtureFile(path)};
        QVERIFY(file.valid());
        const auto input = WindowsDownloadReadInput::fromHandle(handleNumber(file.handle));
        QVERIFY(input);
        file.close();
        QVERIFY(QFile::rename(path, directory.filePath(QStringLiteral("renamed.bin"))));
        SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        NativeEventGuard canary{::CreateEventW(&attributes, TRUE, FALSE, nullptr)};
        QVERIFY(canary.handle);
        auto config =
            configuration(directory, {QStringLiteral("read-file-handle"), QString::number(input->handleValue()),
                                      QString::number(handleNumber(canary.handle))});
        config.read_input = input;
        WindowsDownloadProcess process;
        QVERIFY(process.start(config, 0));
        QVERIFY(nonInheritable(*input));
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.exit_code, std::optional<int>{0});
        const auto report = QJsonDocument::fromJson(result.output).object();
        QCOMPARE(QByteArray::fromBase64(report.value(QStringLiteral("data")).toString().toLatin1()),
                 QByteArrayLiteral("leased-file-input"));
        QVERIFY(report.value(QStringLiteral("write_denied")).toBool());
        QVERIFY(report.value(QStringLiteral("truncate_denied")).toBool());
        QVERIFY(report.value(QStringLiteral("delete_denied")).toBool());
        QCOMPARE(::WaitForSingleObject(canary.handle, 0), DWORD{WAIT_TIMEOUT});
        QVERIFY(nonInheritable(*input));
    }

    void oneReadInputCannotBeUsedByAnotherChildUntilPhysicalDrain() {
        QTemporaryDir directory;
        NativeFileGuard file{fixtureFile(directory.filePath(QStringLiteral("input.bin")))};
        const auto input = WindowsDownloadReadInput::fromHandle(handleNumber(file.handle));
        QVERIFY(input);
        auto config = configuration(directory, {QStringLiteral("hang")});
        config.read_input = input;
        WindowsDownloadProcess first;
        WindowsDownloadProcess second;
        QVERIFY(first.start(config, 0));
        QVERIFY(nonInheritable(*input));
        QVERIFY(!second.start(config, 0));
        QCOMPARE(second.poll(0).error, WindowsDownloadProcessError::Isolation);
        first.cancel();
        QVERIFY(!second.start(config, 0));
        Collected cancelled;
        QVERIFY(collect(first, cancelled));
        QCOMPARE(cancelled.update.state, WindowsDownloadProcessState::Cancelled);
        config.arguments = {QStringLiteral("read-file-handle"), QString::number(input->handleValue()),
                            QStringLiteral("0")};
        QVERIFY(second.start(config, 0));
        Collected completed;
        QVERIFY(collect(second, completed));
        QCOMPARE(completed.update.exit_code, std::optional<int>{0});
        QVERIFY(nonInheritable(*input));
    }

    void readInputLeaseSurvivesCancellationUntilDrainAndThenPermitsJobDirectoryRemoval() {
        QTemporaryDir directory;
        const auto job = directory.filePath(QStringLiteral("job"));
        QVERIFY(QDir().mkdir(job));
        const auto path = QDir(job).filePath(QStringLiteral("input.bin"));
        NativeFileGuard file{fixtureFile(path)};
        auto input = WindowsDownloadReadInput::fromHandle(handleNumber(file.handle));
        QVERIFY(input);
        file.close();
        std::weak_ptr<const WindowsDownloadReadInput> weak = input;
        WindowsDownloadProcess process;
        auto config = configuration(directory, {QStringLiteral("hang")});
        config.working_directory = job;
        config.read_input = input;
        QVERIFY(process.start(config, 0));
        WindowsDownloadProcess rejected;
        QVERIFY(!rejected.start(config, 0));
        QCOMPARE(rejected.poll(0).error, WindowsDownloadProcessError::Isolation);
        config.read_input.reset();
        input.reset();
        QVERIFY(!weak.expired());
        process.cancel();
        QVERIFY(!weak.expired());
        Collected stopped;
        QVERIFY(collect(process, stopped));
        QCOMPARE(stopped.update.state, WindowsDownloadProcessState::Cancelled);
        // Match the workspace's non-POSIX delete contract. A read capability
        // held after ACK keeps the file delete-pending and the job nonempty.
        NativeFileGuard erased{::CreateFileW(path.toStdWString().c_str(), DELETE,
                                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                             OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
        QVERIFY(erased.valid());
        FILE_DISPOSITION_INFO disposition{TRUE};
        QVERIFY(::SetFileInformationByHandle(erased.handle, FileDispositionInfo, &disposition, sizeof(disposition)));
        erased.close();
        const bool removed = ::RemoveDirectoryW(job.toStdWString().c_str()) != FALSE;
        const auto error = ::GetLastError();
        QVERIFY2(removed, qPrintable(QStringLiteral("Job directory remains busy after data ACK: %1").arg(error)));
        QVERIFY(weak.expired());
    }

    void failedLaunchRestoresReadInputInheritanceAndReleasesItsClaim() {
        QTemporaryDir directory;
        NativeFileGuard file{fixtureFile(directory.filePath(QStringLiteral("input.bin")))};
        const auto input = WindowsDownloadReadInput::fromHandle(handleNumber(file.handle));
        QVERIFY(input);
        const auto invalid_executable = directory.filePath(QStringLiteral("invalid.exe"));
        QFile invalid(invalid_executable);
        QVERIFY(invalid.open(QIODevice::WriteOnly));
        QCOMPARE(invalid.write("invalid exe"), qint64{11});
        invalid.close();
        auto config = configuration(directory, {QStringLiteral("read-file-handle"),
                                                QString::number(input->handleValue()), QStringLiteral("0")});
        config.executable = invalid_executable;
        config.read_input = input;
        WindowsDownloadProcess process;
        QVERIFY(!process.start(config, 0));
        QVERIFY(nonInheritable(*input));
        config.executable = helperPath();
        QVERIFY(process.start(config, 0));
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.exit_code, std::optional<int>{0});
        QVERIFY(nonInheritable(*input));
    }

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

    void suppliedEnvironmentDoesNotInheritParentSecretsOrPluginSearch() {
        const TestEnvironmentValue secret("RC_MUSIC_PROCESS_TEST_SECRET", QByteArrayLiteral("synthetic-secret"));
        const TestEnvironmentValue plugin_path("QT_PLUGIN_PATH", QByteArrayLiteral("synthetic-untrusted-plugins"));
        QTemporaryDir directory;
        auto config = configuration(directory, {QStringLiteral("environment")});
        QProcessEnvironment isolated;
        // Only runtime requirements and the deliberately allowed synthetic value.
        isolated.insert(QStringLiteral("SystemRoot"), qEnvironmentVariable("SystemRoot"));
        isolated.insert(QStringLiteral("PATH"), qEnvironmentVariable("PATH"));
        isolated.insert(QStringLiteral("RC_MUSIC_PROCESS_TEST_ALLOWED"), QStringLiteral("ação 日本"));
        isolated.insert(QStringLiteral("FFREPORT"), QStringLiteral("file=forbidden-report.log"));
        config.environment = isolated;
        WindowsDownloadProcess process;
        QVERIFY(process.start(config, 0));
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.exit_code, std::optional<int>{0});
        const auto values = QJsonDocument::fromJson(result.output).object();
        QCOMPARE(values.value(QStringLiteral("sentinel")).toString(), QString{});
        QCOMPARE(values.value(QStringLiteral("qt_plugin_path")).toString(), QString{});
        QCOMPARE(values.value(QStringLiteral("allowed")).toString(), QStringLiteral("ação 日本"));
        QCOMPARE(values.value(QStringLiteral("ffreport")).toString(), QString{});
        QVERIFY(!QFileInfo::exists(directory.filePath(QStringLiteral("forbidden-report.log"))));
    }

    void defaultEnvironmentRemainsFiltered() {
        const TestEnvironmentValue secret("RC_MUSIC_PROCESS_TEST_SECRET", QByteArrayLiteral("synthetic-inherited"));
        const TestEnvironmentValue report("FFREPORT", QByteArrayLiteral("file=forbidden-report.log"));
        QTemporaryDir directory;
        auto config = configuration(directory, {QStringLiteral("environment")});
        WindowsDownloadProcess process;
        QVERIFY(process.start(config, 0));
        Collected result;
        QVERIFY(collect(process, result));
        const auto values = QJsonDocument::fromJson(result.output).object();
        QCOMPARE(values.value(QStringLiteral("sentinel")).toString(), QStringLiteral("synthetic-inherited"));
        QCOMPARE(values.value(QStringLiteral("ffreport")).toString(), QString{});
    }

    void rejectsMalformedOrImplicitlyInheritedEnvironment_data() {
        QTest::addColumn<QString>("mutation");
        for (const auto& mutation : {"empty", "missing path", "missing root", "empty root", "nul value", "nul key",
                                     "equals key", "too many variables", "too many units"})
            QTest::newRow(mutation) << QString::fromLatin1(mutation);
    }

    void rejectsMalformedOrImplicitlyInheritedEnvironment() {
        QFETCH(QString, mutation);
        QTemporaryDir directory;
        auto config = configuration(directory, {QStringLiteral("environment")});
        QProcessEnvironment environment;
        environment.insert(QStringLiteral("PATH"), QString{});
        environment.insert(QStringLiteral("SystemRoot"), QStringLiteral("C:/Windows"));
        if (mutation == QStringLiteral("empty"))
            environment.clear();
        else if (mutation == QStringLiteral("missing path"))
            environment.remove(QStringLiteral("PATH"));
        else if (mutation == QStringLiteral("missing root"))
            environment.remove(QStringLiteral("SystemRoot"));
        else if (mutation == QStringLiteral("empty root"))
            environment.insert(QStringLiteral("SystemRoot"), QString{});
        else if (mutation == QStringLiteral("nul value"))
            environment.insert(QStringLiteral("VALUE"), QString::fromUtf8("a\0b", 3));
        else if (mutation == QStringLiteral("nul key"))
            environment.insert(QString::fromUtf8("A\0B", 3), QStringLiteral("value"));
        else if (mutation == QStringLiteral("equals key"))
            environment.insert(QStringLiteral("A=B"), QStringLiteral("value"));
        else if (mutation == QStringLiteral("too many variables")) {
            for (int index = 0; index < 127; ++index)
                environment.insert(QStringLiteral("VARIABLE_%1").arg(index), QStringLiteral("value"));
        } else
            environment.insert(QStringLiteral("VALUE"), QString(download_process_max_environment_units, 'X'));
        config.environment = environment;
        WindowsDownloadProcess process;
        QVERIFY(!process.start(config, 0));
        QCOMPARE(process.poll(0).state, WindowsDownloadProcessState::Failed);
        QCOMPARE(process.poll(0).error, WindowsDownloadProcessError::InvalidConfiguration);
    }

    void acceptsBoundedExplicitEnvironmentAndResetsItOnReuse_data() {
        QTest::addColumn<bool>("maximum_entries");
        QTest::addColumn<bool>("exact_units");
        QTest::newRow("exact entries") << true << false;
        QTest::newRow("exact UTF16 units") << false << true;
    }

    void acceptsBoundedExplicitEnvironmentAndResetsItOnReuse() {
        QFETCH(bool, maximum_entries);
        QFETCH(bool, exact_units);
        const TestEnvironmentValue secret("RC_MUSIC_PROCESS_TEST_SECRET", QByteArrayLiteral("synthetic-inherited"));
        QTemporaryDir directory;
        auto config = configuration(directory, {QStringLiteral("environment")});
        QProcessEnvironment environment;
        environment.insert(QStringLiteral("SystemRoot"), qEnvironmentVariable("SystemRoot"));
        environment.insert(QStringLiteral("PATH"), qEnvironmentVariable("PATH"));
        if (maximum_entries) {
            for (int index = 0; index < 126; ++index)
                environment.insert(QStringLiteral("VARIABLE_%1").arg(index), QStringLiteral("value"));
        }
        if (exact_units) {
            environment.insert(QStringLiteral("VALUE"), QString{});
            qsizetype overhead = 1;
            for (const auto& entry : environment.toStringList())
                overhead += entry.size() + 1;
            QVERIFY(overhead < download_process_max_environment_units);
            environment.insert(QStringLiteral("VALUE"),
                               QString(download_process_max_environment_units - overhead, 'X'));
        }
        config.environment = environment;
        WindowsDownloadProcess process;
        QVERIFY(process.start(config, 0));
        Collected isolated;
        QVERIFY(collect(process, isolated));
        QCOMPARE(isolated.update.exit_code, std::optional<int>{0});
        QCOMPARE(QJsonDocument::fromJson(isolated.output).object().value(QStringLiteral("sentinel")).toString(),
                 QString{});
        config.environment.reset();
        QVERIFY(process.start(config, 0));
        Collected inherited;
        QVERIFY(collect(process, inherited));
        QCOMPARE(inherited.update.exit_code, std::optional<int>{0});
        QCOMPARE(QJsonDocument::fromJson(inherited.output).object().value(QStringLiteral("sentinel")).toString(),
                 QStringLiteral("synthetic-inherited"));
    }

    void emptyPathIsExplicitAndDoesNotFallBackToParent_data() {
        QTest::addColumn<bool>("lowercase");
        QTest::newRow("canonical names") << false;
        QTest::newRow("case insensitive names") << true;
    }

    void emptyPathIsExplicitAndDoesNotFallBackToParent() {
        QFETCH(bool, lowercase);
        QTemporaryDir directory;
        const auto executable = directory.filePath(QStringLiteral("environment-helper.exe"));
        QVERIFY(QFile::copy(helperPath(), executable));
        QVERIFY(
            QFile::copy(QDir(QLibraryInfo::path(QLibraryInfo::BinariesPath)).filePath(QStringLiteral("Qt6Core.dll")),
                        directory.filePath(QStringLiteral("Qt6Core.dll"))));
        auto config = configuration(directory, {QStringLiteral("environment")});
        config.executable = executable;
        QProcessEnvironment environment;
        environment.insert(lowercase ? QStringLiteral("path") : QStringLiteral("PATH"), QString{});
        environment.insert(lowercase ? QStringLiteral("systemroot") : QStringLiteral("SystemRoot"),
                           qEnvironmentVariable("SystemRoot"));
        config.environment = environment;
        WindowsDownloadProcess process;
        QVERIFY(process.start(config, 0));
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.exit_code, std::optional<int>{0});
        QCOMPARE(QJsonDocument::fromJson(result.output).object().value(QStringLiteral("path")).toString(), QString{});
    }

    void sendsPrivateBinaryInputInBoundedSegmentsAndThenEof_data() {
        QTest::addColumn<int>("size");
        QTest::newRow("empty NUL") << 0;
        QTest::newRow("small binary UTF8") << 29;
        QTest::newRow("several pipe buffers") << 131073;
        QTest::newRow("exact input ceiling") << int{download_process_max_input_bytes};
    }

    void sendsPrivateBinaryInputInBoundedSegmentsAndThenEof() {
        QFETCH(int, size);
        QTemporaryDir directory;
        auto config = configuration(directory, {QStringLiteral("read-stdin")});
        const QByteArray pattern = QStringLiteral("PRIVATE_REQUEST_ação_日本").toUtf8() + QByteArray("\0\xff", 2);
        while (config.standard_input.size() < size)
            config.standard_input += pattern.left(size - config.standard_input.size());
        WindowsDownloadProcess process;
        QVERIFY(process.start(config, 0));
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, WindowsDownloadProcessState::Exited);
        QCOMPARE(result.update.exit_code, std::optional<int>{0});
        QCOMPARE(result.output, config.standard_input);
        QVERIFY(result.error_output.isEmpty());
        QVERIFY(result.maximum_poll_bytes <= download_process_poll_bytes);
    }

    void rejectsExcessiveInputBeforeCreatingAChild() {
        QTemporaryDir directory;
        auto config = configuration(directory, {QStringLiteral("read-stdin")});
        config.standard_input = QByteArray(download_process_max_input_bytes + 1, 'X');
        WindowsDownloadProcess process;
        QVERIFY(!process.start(config, 0));
        QCOMPARE(process.poll(0).state, WindowsDownloadProcessState::Failed);
        QCOMPARE(process.poll(0).error, WindowsDownloadProcessError::InvalidConfiguration);
        config.standard_input = QByteArrayLiteral("a new request");
        QVERIFY(process.start(config, 0));
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.output, config.standard_input);
    }

    void cancelsWhileInputIsBackpressuredAndCanStartAgain() {
        QTemporaryDir directory;
        auto config = configuration(directory, {QStringLiteral("hang")});
        config.standard_input = QByteArray(download_process_max_input_bytes, 'X');
        WindowsDownloadProcess process;
        QVERIFY(process.start(config, 0));
        QByteArray ready;
        QVERIFY(waitForReady(process, ready));
        NativeProcessGuard child{::OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, readyPid(ready))};
        QVERIFY(child.handle != nullptr);
        process.cancel();
        Collected cancelled;
        QVERIFY(collect(process, cancelled));
        QCOMPARE(cancelled.update.state, WindowsDownloadProcessState::Cancelled);
        QCOMPARE(::WaitForSingleObject(child.handle, 0), DWORD{WAIT_OBJECT_0});
        config.arguments = {QStringLiteral("read-stdin")};
        config.standard_input = QByteArrayLiteral("replacement private payload");
        QVERIFY(process.start(config, 0));
        Collected restarted;
        QVERIFY(collect(process, restarted));
        QCOMPARE(restarted.output, config.standard_input);
        QCOMPARE(restarted.update.exit_code, std::optional<int>{0});
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
        QCOMPARE(object.value(QStringLiteral("app_container")).toBool(true), false);
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

    void preservesNaturalExitAndPendingPipesWhenCancelledAfterNativeExit_data() {
        QTest::addColumn<int>("exit_code");
        QTest::newRow("zero") << 0;
        QTest::newRow("nonzero") << 17;
    }

    void preservesNaturalExitAndPendingPipesWhenCancelledAfterNativeExit() {
        QFETCH(int, exit_code);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto event_name =
            QStringLiteral("Local\\RCMusic-process-natural-exit-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto native_event_name = event_name.toStdWString();
        NativeEventGuard gate{::CreateEventW(nullptr, TRUE, FALSE, native_event_name.c_str())};
        QVERIFY(gate.handle != nullptr);
        QVERIFY(::GetLastError() != ERROR_ALREADY_EXISTS);
        auto config =
            configuration(directory, {QStringLiteral("--natural-exit-test"), event_name, QString::number(exit_code)});
        config.executable = QCoreApplication::applicationFilePath();
        WindowsDownloadProcess process;
        QVERIFY(process.start(config, 0));
        QByteArray ready;
        QVERIFY(waitForReady(process, ready));
        NativeProcessGuard child{
            ::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, readyPid(ready))};
        QVERIFY(child.handle != nullptr);
        QVERIFY(::SetEvent(gate.handle));
        // Witness natural exit without polling the supervisor or draining its final bytes.
        QCOMPARE(::WaitForSingleObject(child.handle, 5000), DWORD{WAIT_OBJECT_0});
        DWORD observed_exit_code = 0;
        QVERIFY(::GetExitCodeProcess(child.handle, &observed_exit_code));
        QCOMPARE(observed_exit_code, static_cast<DWORD>(exit_code));
        process.cancel();
        Collected result;
        QVERIFY(collect(process, result));
        QCOMPARE(result.update.state, WindowsDownloadProcessState::Exited);
        QCOMPARE(result.update.error, WindowsDownloadProcessError::None);
        QCOMPARE(result.update.exit_code, std::optional<int>{exit_code});
        QCOMPARE(result.output, QByteArrayLiteral("OUT"));
        QCOMPARE(result.error_output, QByteArrayLiteral("ERR"));
        QVERIFY(result.maximum_poll_bytes <= download_process_poll_bytes);
        process.cancel();
        const auto stable = process.poll(0);
        QCOMPARE(stable.state, WindowsDownloadProcessState::Exited);
        QCOMPARE(stable.exit_code, std::optional<int>{exit_code});
        QVERIFY(stable.stdout_chunk.isEmpty());
        QVERIFY(stable.stderr_chunk.isEmpty());
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
    if (argc == 4 && QByteArray(argv[1]) == QByteArrayLiteral("--natural-exit-test"))
        return naturalExitChild(argc, argv);
    QCoreApplication application(argc, argv);
    WindowsDownloadProcessTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "WindowsDownloadProcessTests.moc"
