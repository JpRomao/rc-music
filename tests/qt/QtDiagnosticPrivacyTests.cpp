#include "adapters/QtDiagnosticPrivacy.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QTest>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace music_player::m0::test {
namespace {

constexpr char stdout_marker[] = "PRIVACY_STDOUT_CONTROL";
constexpr char qt_marker[] = "QT_PRIVATE_ação_日本";
constexpr char crt_marker[] = "CRT_PRIVATE_ação_日本";
constexpr char win32_marker[] = "WIN32_PRIVATE_ação_日本";
constexpr char previous_handler_marker[] = "PREVIOUS_HANDLER_CALLED";
int previous_handler_calls = 0;

void previousMessageHandler(QtMsgType, const QMessageLogContext&, const QString&) {
    ++previous_handler_calls;
    std::fputs(previous_handler_marker, stderr);
    std::fflush(stderr);
}

bool emitExternalDiagnostics() {
    QMessageLogger("PRIVATE_CONTEXT.cpp", 71, "PRIVATE_FUNCTION", "PRIVATE_CATEGORY").warning("%s", qt_marker);
    if (std::fputs(crt_marker, stderr) < 0 || std::fflush(stderr) != 0)
        return false;
    DWORD written = 0;
    return ::WriteFile(::GetStdHandle(STD_ERROR_HANDLE), win32_marker, static_cast<DWORD>(sizeof(win32_marker) - 1),
                       &written, nullptr) != FALSE &&
           written == sizeof(win32_marker) - 1;
}

struct DiagnosticsOnExit final {
    ~DiagnosticsOnExit() { static_cast<void>(emitExternalDiagnostics()); }
};

int childMain(const char* mode, int argc, char* argv[]) {
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    const bool control = std::strcmp(mode, "control") == 0;
    const bool previous_handler = std::strcmp(mode, "previous-handler") == 0;
    if (previous_handler)
        qInstallMessageHandler(previousMessageHandler);
    if (!control && !installProcessDiagnosticPrivacy())
        return 61;
    if (std::strcmp(mode, "double-install") == 0 && !installProcessDiagnosticPrivacy())
        return 62;
    if (std::fputs(stdout_marker, stdout) < 0 || std::fflush(stdout) != 0)
        return 63;
    if (std::strcmp(mode, "fatal") == 0) {
        qFatal("QT_FATAL_PRIVATE_ação_日本");
    }
    if (std::strcmp(mode, "teardown") == 0) {
        static DiagnosticsOnExit diagnostics_on_exit;
        QCoreApplication app(argc, argv);
        return 0;
    }
    if (!emitExternalDiagnostics())
        return 65;
    if (previous_handler_calls != 0)
        return 66;
    return 0;
}

struct ChildResult final {
    bool started{false};
    bool finished{false};
    int exit_code{-1};
    QProcess::ExitStatus exit_status{QProcess::NormalExit};
    QByteArray stdout_bytes;
    QByteArray stderr_bytes;
};

ChildResult runChild(const QString& mode) {
    QProcess child;
    auto environment = QProcessEnvironment::systemEnvironment();
    // The runner keeps its own strict policy. Only synthetic children need a
    // nonfatal warning control and deterministic context/category formatting.
    environment.remove(QStringLiteral("QT_FATAL_WARNINGS"));
    environment.remove(QStringLiteral("QT_FATAL_CRITICALS"));
    environment.insert(QStringLiteral("QT_LOGGING_TO_CONSOLE"), QStringLiteral("1"));
    environment.insert(QStringLiteral("QT_FORCE_STDERR_LOGGING"), QStringLiteral("1"));
    environment.insert(QStringLiteral("QT_LOGGING_RULES"), QStringLiteral("*.warning=true"));
    environment.insert(QStringLiteral("QT_MESSAGE_PATTERN"),
                       QStringLiteral("%{category}|%{file}|%{function}|%{message}"));
    child.setProcessEnvironment(environment);
    child.setProgram(QCoreApplication::applicationFilePath());
    child.setArguments({QStringLiteral("--diagnostic-privacy-child"), mode});
    child.start();
    ChildResult result;
    result.started = child.waitForStarted(5'000);
    if (result.started)
        result.finished = child.waitForFinished(10'000);
    if (child.state() != QProcess::NotRunning) {
        child.kill();
        child.waitForFinished(5'000);
    }
    result.exit_code = child.exitCode();
    result.exit_status = child.exitStatus();
    result.stdout_bytes = child.readAllStandardOutput();
    result.stderr_bytes = child.readAllStandardError();
    return result;
}

} // namespace

class QtDiagnosticPrivacyTests final : public QObject {
    Q_OBJECT

  private slots:
    void unprotectedControlExposesAllDiagnosticChannels() {
        const auto child = runChild(QStringLiteral("control"));
        QVERIFY(child.started);
        QVERIFY(child.finished);
        QCOMPARE(child.exit_status, QProcess::NormalExit);
        QCOMPARE(child.exit_code, 0);
        QCOMPARE(child.stdout_bytes, QByteArray{stdout_marker});
        QVERIFY(child.stderr_bytes.contains(qt_marker));
        QVERIFY(child.stderr_bytes.contains("PRIVATE_CONTEXT.cpp"));
        QVERIFY(child.stderr_bytes.contains("PRIVATE_FUNCTION"));
        QVERIFY(child.stderr_bytes.contains("PRIVATE_CATEGORY"));
        QVERIFY(child.stderr_bytes.contains(crt_marker));
        QVERIFY(child.stderr_bytes.contains(win32_marker));
    }

    void protectedChildDiscardsDiagnosticsWithoutChangingStdout_data() {
        QTest::addColumn<QString>("mode");
        for (const auto* mode : {"protected", "double-install", "previous-handler", "teardown"})
            QTest::newRow(mode) << QString::fromLatin1(mode);
    }

    void protectedChildDiscardsDiagnosticsWithoutChangingStdout() {
        QFETCH(QString, mode);
        const auto child = runChild(mode);
        QVERIFY(child.started);
        QVERIFY(child.finished);
        QCOMPARE(child.exit_status, QProcess::NormalExit);
        QCOMPARE(child.exit_code, 0);
        QCOMPARE(child.stdout_bytes, QByteArray{stdout_marker});
        QVERIFY(child.stderr_bytes.isEmpty());
    }

    void fatalStillTerminatesWithoutDisclosingItsMessage() {
        const auto child = runChild(QStringLiteral("fatal"));
        QVERIFY(child.started);
        QVERIFY(child.finished);
        QVERIFY(child.exit_status == QProcess::CrashExit || child.exit_code != 0);
        QCOMPARE(child.stdout_bytes, QByteArray{stdout_marker});
        QVERIFY(child.stderr_bytes.isEmpty());
    }
};

} // namespace music_player::m0::test

int main(int argc, char* argv[]) {
    if (argc == 3 && std::strcmp(argv[1], "--diagnostic-privacy-child") == 0)
        return music_player::m0::test::childMain(argv[2], argc, argv);
    QCoreApplication app(argc, argv);
    music_player::m0::test::QtDiagnosticPrivacyTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "QtDiagnosticPrivacyTests.moc"
