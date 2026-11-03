#include "adapters/WindowsNativeDownloadPeerCheck.hpp"

#include <QCoreApplication>
#include <QProcess>
#include <QScopeGuard>
#include <QTest>
#include <QUuid>

#include <qt_windows.h>

#include <type_traits>

using namespace music_player::m0;

namespace {
static_assert(!std::is_copy_constructible_v<WindowsNativeDownloadPeerCheck>);
static_assert(!std::is_copy_assignable_v<WindowsNativeDownloadPeerCheck>);

class Handle final {
  public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value_; }
    bool valid() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
    void reset() {
        if (valid())
            CloseHandle(value_);
        value_ = nullptr;
    }

  private:
    HANDLE value_;
};

QString endpoint() { return QStringLiteral("rc-music-peer-check-") + QUuid::createUuid().toString(QUuid::Id128); }
QString pipePath(const QString& value) { return QStringLiteral("\\\\.\\pipe\\") + value; }
HANDLE createServer(const QString& value) {
    return CreateNamedPipeW(pipePath(value).toStdWString().c_str(),
                            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 0, 0,
                            3'000, nullptr);
}
HANDLE connectClient(const QString& value) {
    return CreateFileW(pipePath(value).toStdWString().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                       FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS, nullptr);
}
QStringList selfImage() { return {QCoreApplication::applicationFilePath()}; }

class Child final {
  public:
    QProcess process;
    ~Child() {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            static_cast<void>(process.waitForFinished(2'000));
        }
    }
    bool start(const QString& value) {
        process.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--native-peer-child"), value});
        return process.waitForStarted(2'000) && process.waitForReadyRead(2'000) &&
               process.readAllStandardOutput() == "ready\n";
    }
};

int runChild(const QString& value) {
    const Handle pipe{connectClient(value)};
    if (!pipe.valid())
        return 2;
    DWORD written = 0;
    if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "ready\n", 6, &written, nullptr) || written != 6)
        return 2;
    char command = 0;
    DWORD read = 0;
    static_cast<void>(ReadFile(GetStdHandle(STD_INPUT_HANDLE), &command, 1, &read, nullptr));
    return 0;
}
} // namespace

class WindowsNativeDownloadPeerCheckTests final : public QObject {
    Q_OBJECT

  private slots:
    void idleIsInertAndCanBeStoppedRepeatedly() {
        WindowsNativeDownloadPeerCheck check;
        QCOMPARE(check.poll(), NativeDownloadPeerState::Idle);
        QVERIFY(check.stopped());
        check.shutdown();
        check.shutdown();
        QVERIFY(check.stopped());
        QCOMPARE(check.poll(), NativeDownloadPeerState::Rejected);
    }

    void acceptsLocalDriveImagesWithoutFilesystemAccess() {
        QVERIFY(validNativePeerImages({QStringLiteral("C:\\not-installed\\RC Music\\host.exe")}));
        QVERIFY(validNativePeerImages({QStringLiteral("d:/unicode/á/host.exe"), QStringLiteral("E:\\Other\\App.EXE")}));
        QVERIFY(validNativePeerImages({QStringLiteral("C:\\a\\one.exe"), QStringLiteral("C:\\a\\two.exe"),
                                       QStringLiteral("C:\\a\\three.exe"), QStringLiteral("C:\\a\\four.exe")}));
        QVERIFY(validNativePeerImages({QStringLiteral("C:\\") + QString(32'764, QLatin1Char('a'))}));
    }

    void rejectsUntrustedImageLists_data() {
        QTest::addColumn<QStringList>("images");
        QTest::newRow("empty") << QStringList{};
        QTest::newRow("five") << QStringList(5, QStringLiteral("C:\\a\\host.exe"));
        QTest::newRow("empty-entry") << QStringList{QString{}};
        QTest::newRow("relative") << QStringList{QStringLiteral("host.exe")};
        QTest::newRow("drive-relative") << QStringList{QStringLiteral("C:host.exe")};
        QTest::newRow("UNC") << QStringList{QStringLiteral("\\\\server\\share\\host.exe")};
        QTest::newRow("device") << QStringList{QStringLiteral("\\\\?\\C:\\a\\host.exe")};
        QTest::newRow("device-dot") << QStringList{QStringLiteral("\\\\.\\C:\\a\\host.exe")};
        QTest::newRow("ADS") << QStringList{QStringLiteral("C:\\a\\host.exe:stream")};
        QTest::newRow("parent") << QStringList{QStringLiteral("C:\\a\\..\\host.exe")};
        QTest::newRow("dot") << QStringList{QStringLiteral("C:\\a\\.\\host.exe")};
        QTest::newRow("wildcard") << QStringList{QStringLiteral("C:\\a\\*.exe")};
        QTest::newRow("trailing-separator") << QStringList{QStringLiteral("C:\\a\\")};
        QTest::newRow("trailing-dot") << QStringList{QStringLiteral("C:\\a\\host.exe.")};
        QTest::newRow("DOS-NUL") << QStringList{QStringLiteral("C:\\a\\NUL.exe")};
        QTest::newRow("DOS-CON") << QStringList{QStringLiteral("C:\\CON\\host.exe")};
        QTest::newRow("DOS-COM1") << QStringList{QStringLiteral("C:\\a\\com1.exe")};
        QTest::newRow("DOS-LPT9") << QStringList{QStringLiteral("C:\\a\\LPT9.exe")};
        QTest::newRow("DOS-CONIN") << QStringList{QStringLiteral("C:\\a\\CONIN$")};
        QTest::newRow("NUL") << QStringList{QStringLiteral("C:\\a\\host") + QChar{0} + QStringLiteral(".exe")};
        QTest::newRow("too-long") << QStringList{QStringLiteral("C:\\") + QString(32'765, QLatin1Char('a'))};
    }

    void rejectsUntrustedImageLists() {
        QFETCH(QStringList, images);
        QVERIFY(!validNativePeerImages(images));
    }

    void verifiesBothNativePeerSides_data() {
        QTest::addColumn<bool>("server_handle");
        QTest::newRow("client-image-from-server-handle") << true;
        QTest::newRow("server-image-from-client-handle") << false;
    }

    void verifiesBothNativePeerSides() {
        QFETCH(bool, server_handle);
        const auto value = endpoint();
        Handle server{createServer(value)}, client{connectClient(value)};
        QVERIFY(server.valid());
        QVERIFY(client.valid());
        WindowsNativeDownloadPeerCheck check;
        QVERIFY(check.begin(reinterpret_cast<qintptr>(server_handle ? server.get() : client.get()),
                            server_handle ? NativeDownloadPeerSide::Client : NativeDownloadPeerSide::Server,
                            selfImage()));
        // The caller's original handle is borrowed; only the check's duplicate
        // may be consumed by its worker. The opposite end remains connected.
        if (server_handle)
            server.reset();
        else
            client.reset();
        QTRY_COMPARE_WITH_TIMEOUT(check.poll(), NativeDownloadPeerState::Eligible, 3'000);
        QVERIFY(!check.stopped());
        check.shutdown();
        QTRY_VERIFY_WITH_TIMEOUT(check.stopped(), 3'000);
        QCOMPARE(check.poll(), NativeDownloadPeerState::Rejected);
    }

    void comparesWindowsImagePathsOrdinallyIgnoringCaseAndSeparators() {
        const auto value = endpoint();
        const Handle server{createServer(value)}, client{connectClient(value)};
        QVERIFY(server.valid());
        QVERIFY(client.valid());
        WindowsNativeDownloadPeerCheck check;
        const auto alias = QCoreApplication::applicationFilePath().toUpper().replace('/', '\\');
        QVERIFY(check.begin(reinterpret_cast<qintptr>(server.get()), NativeDownloadPeerSide::Client, {alias}));
        QTRY_COMPARE_WITH_TIMEOUT(check.poll(), NativeDownloadPeerState::Eligible, 3'000);
        check.shutdown();
        QTRY_VERIFY_WITH_TIMEOUT(check.stopped(), 3'000);
    }

    void retiredDuplicateDoesNotKeepPipePeerEofAlive() {
        const auto value = endpoint();
        Handle server{createServer(value)};
        const Handle client{connectClient(value)};
        QVERIFY(server.valid());
        QVERIFY(client.valid());
        WindowsNativeDownloadPeerCheck check;
        QVERIFY(check.begin(reinterpret_cast<qintptr>(server.get()), NativeDownloadPeerSide::Client, selfImage()));
        QTRY_COMPARE_WITH_TIMEOUT(check.poll(), NativeDownloadPeerState::Eligible, 3'000);
        server.reset();
        const auto peeked = PeekNamedPipe(client.get(), nullptr, 0, nullptr, nullptr, nullptr);
        const auto error = GetLastError();
        QVERIFY(!peeked);
        QVERIFY(error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED);
        check.shutdown();
        QTRY_VERIFY_WITH_TIMEOUT(check.stopped(), 3'000);
    }

    void mismatchedImageIsRejectedWithoutOpeningThatPath() {
        const auto value = endpoint();
        const Handle server{createServer(value)}, client{connectClient(value)};
        QVERIFY(server.valid());
        QVERIFY(client.valid());
        WindowsNativeDownloadPeerCheck check;
        QVERIFY(check.begin(reinterpret_cast<qintptr>(server.get()), NativeDownloadPeerSide::Client,
                            {QStringLiteral("C:\\not-installed\\wrong-image.exe")}));
        QTRY_COMPARE_WITH_TIMEOUT(check.poll(), NativeDownloadPeerState::Rejected, 3'000);
        QTRY_VERIFY_WITH_TIMEOUT(check.stopped(), 3'000);
    }

    void invalidHandleAndNonPipeObjectFailClosed() {
        WindowsNativeDownloadPeerCheck invalid;
        QVERIFY(!invalid.begin(-1, NativeDownloadPeerSide::Client, selfImage()));
        QCOMPARE(invalid.poll(), NativeDownloadPeerState::Rejected);
        QVERIFY(invalid.stopped());
        const Handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        QVERIFY(event.valid());
        WindowsNativeDownloadPeerCheck wrong_type;
        QVERIFY(wrong_type.begin(reinterpret_cast<qintptr>(event.get()), NativeDownloadPeerSide::Client, selfImage()));
        QTRY_COMPARE_WITH_TIMEOUT(wrong_type.poll(), NativeDownloadPeerState::Rejected, 3'000);
        QTRY_VERIFY_WITH_TIMEOUT(wrong_type.stopped(), 3'000);
        QVERIFY(SetEvent(event.get()));
    }

    void beginIsOneShotAndShutdownRevokesPendingOrEligibleResult() {
        const auto value = endpoint();
        const Handle server{createServer(value)}, client{connectClient(value)};
        QVERIFY(server.valid());
        QVERIFY(client.valid());
        WindowsNativeDownloadPeerCheck check;
        QVERIFY(check.begin(reinterpret_cast<qintptr>(server.get()), NativeDownloadPeerSide::Client, selfImage()));
        QVERIFY(!check.begin(reinterpret_cast<qintptr>(server.get()), NativeDownloadPeerSide::Client, selfImage()));
        check.shutdown();
        QCOMPARE(check.poll(), NativeDownloadPeerState::Rejected);
        QTRY_VERIFY_WITH_TIMEOUT(check.stopped(), 3'000);
        QVERIFY(!check.begin(reinterpret_cast<qintptr>(server.get()), NativeDownloadPeerSide::Client, selfImage()));
    }

    void realChildExitRevokesEligibility() {
        const auto value = endpoint();
        const Handle server{createServer(value)};
        QVERIFY(server.valid());
        Child child;
        QVERIFY(child.start(value));
        WindowsNativeDownloadPeerCheck check;
        QVERIFY(check.begin(reinterpret_cast<qintptr>(server.get()), NativeDownloadPeerSide::Client, selfImage()));
        QTRY_COMPARE_WITH_TIMEOUT(check.poll(), NativeDownloadPeerState::Eligible, 3'000);
        QCOMPARE(child.process.write("x", 1), qint64{1});
        QVERIFY(child.process.waitForFinished(2'000));
        QCOMPARE(child.process.exitCode(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(check.poll(), NativeDownloadPeerState::Rejected, 3'000);
        QVERIFY(!check.stopped());
        check.shutdown();
        QTRY_VERIFY_WITH_TIMEOUT(check.stopped(), 3'000);
    }
};

int main(int argc, char** argv) {
    QCoreApplication application{argc, argv};
    const auto arguments = application.arguments();
    if (arguments.size() == 3 && arguments[1] == QStringLiteral("--native-peer-child"))
        return runChild(arguments[2]);
    WindowsNativeDownloadPeerCheckTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "WindowsNativeDownloadPeerCheckTests.moc"
