#include "adapters/NativeDownloadCodec.hpp"
#include "adapters/NativeDownloadIpcProtocol.hpp"
#include "adapters/NativeMessageFrame.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QLocalSocket>
#include <QProcess>
#include <QTest>
#include <QUuid>
#include <QtEndian>
#include <qt_windows.h>

#include <string>
#include <vector>

using namespace music_player::m0;
namespace {
QString helper_executable;
const QString origin = QStringLiteral("chrome-extension://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/");
const std::string request_id = "01234567-89ab-4cde-8fab-0123456789ab";

QString endpoint() { return QStringLiteral("rc-music-native-owner-") + QUuid::createUuid().toString(QUuid::Id128); }

DWORD firstPipeInstanceError(const QString& endpoint_name) {
    // Probe the kernel name directly, independently of the application's owner
    // lease. Every successful probe releases its handle before returning.
    const auto name = QStringLiteral("\\\\.\\pipe\\") + endpoint_name;
    const auto pipe = CreateNamedPipeW(name.toStdWString().c_str(),
                                       PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4'096, 4'096, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
        return GetLastError();
    CloseHandle(pipe);
    return ERROR_SUCCESS;
}

QByteArray requestFrame() {
    const QByteArray body =
        R"({"items":[{"kind":"audio","title":"Synthetic","url":"https://fixture.invalid/synthetic.mp3"}],"requestId":"01234567-89ab-4cde-8fab-0123456789ab","schemaVersion":1,"type":"download.request"})";
    return native_download_ipc::preamble(origin) + encodeNativeMessageFrame(body);
}

class Child final {
  public:
    QProcess process;
    QByteArray output;
    QByteArray errors;
    bool overflow{false};

    Child() {
        process.setProcessChannelMode(QProcess::SeparateChannels);
        QObject::connect(&process, &QProcess::readyReadStandardOutput, &process,
                         [this] { collect(QProcess::StandardOutput, output); });
        QObject::connect(&process, &QProcess::readyReadStandardError, &process,
                         [this] { collect(QProcess::StandardError, errors); });
    }
    ~Child() {
        // Only the QProcess launched by this object is ever killed. No browser,
        // user app, PID from input or unrelated owner is targeted.
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            static_cast<void>(process.waitForFinished(2'000));
        }
        QObject::disconnect(&process, nullptr, &process, nullptr);
    }
    bool start(const QString& name, const QString& variant = QStringLiteral("primary"),
               const QString& configuration = QStringLiteral("valid")) {
        process.start(helper_executable, {name, variant, configuration});
        return process.waitForStarted(2'000);
    }
    bool command(const QByteArray& value) { return process.write(value) == value.size(); }
    qsizetype count(const QByteArray& value) const { return output.split('\n').count(value); }
    bool cleanOutput() {
        collect(QProcess::StandardOutput, output);
        collect(QProcess::StandardError, errors);
        if (overflow || !errors.isEmpty() || (!output.isEmpty() && !output.endsWith('\n')))
            return false;
        for (const auto& line : output.split('\n')) {
            if (!line.isEmpty() && line != "READY" && line != "REFUSED" && line != "STOPPED" && line != "RELEASED" &&
                line != "ADMITTED")
                return false;
        }
        return true;
    }

  private:
    void collect(const QProcess::ProcessChannel channel, QByteArray& bytes) {
        constexpr qsizetype limit = 1'024;
        process.setReadChannel(channel);
        if (bytes.size() <= limit)
            bytes += process.read(limit + 1 - bytes.size());
        overflow = overflow || bytes.size() > limit || process.bytesAvailable() > 0;
        process.setReadChannel(QProcess::StandardOutput);
    }
};
} // namespace

class NativeDownloadOwnerProcessTests final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { QVERIFY(QFileInfo{helper_executable}.isFile()); }

    void contenderRefusesWithoutDisruptingOriginalOwner_data() {
        QTest::addColumn<bool>("case_alias");
        QTest::addColumn<QString>("variant");
        QTest::newRow("same endpoint and origin") << false << QStringLiteral("primary");
        QTest::newRow("case alias and different origin") << true << QStringLiteral("alternate");
    }
    void contenderRefusesWithoutDisruptingOriginalOwner() {
        QFETCH(bool, case_alias);
        QFETCH(QString, variant);
        const auto name = endpoint();
        Child owner;
        Child contender;
        QVERIFY(owner.start(name));
        QTRY_COMPARE_WITH_TIMEOUT(owner.count("READY"), 1, 3'000);
        QCOMPARE(firstPipeInstanceError(name), DWORD{ERROR_ACCESS_DENIED});
        QCOMPARE(firstPipeInstanceError(case_alias ? name.toUpper() : name), DWORD{ERROR_ACCESS_DENIED});
        QVERIFY(contender.start(case_alias ? name.toUpper() : name, variant));
        QTRY_VERIFY_WITH_TIMEOUT(contender.count("READY") + contender.count("REFUSED") != 0, 3'000);
        QCOMPARE(contender.count("REFUSED"), 1);
        QCOMPARE(contender.count("READY"), 0);
        QCOMPARE(owner.process.state(), QProcess::Running);
        QCOMPARE(contender.process.state(), QProcess::Running);

        QLocalSocket socket;
        socket.setReadBufferSize(native_download_ipc::response_max_payload_bytes + 5);
        socket.connectToServer(name);
        QTRY_COMPARE_WITH_TIMEOUT(socket.state(), QLocalSocket::ConnectedState, 3'000);
        const auto framed = requestFrame();
        QCOMPARE(socket.write(framed), framed.size());
        QTRY_COMPARE_WITH_TIMEOUT(socket.state(), QLocalSocket::UnconnectedState, 3'000);
        const auto response = socket.read(native_download_ipc::response_max_payload_bytes + 5);
        QVERIFY(response.size() > 4 && response.size() <= native_download_ipc::response_max_payload_bytes + 4);
        QCOMPARE(qFromLittleEndian<quint32>(response.constData()), static_cast<quint32>(response.size() - 4));
        const auto decoded = decodeNativeDownloadResponse(response.sliced(4), request_id, 1);
        QVERIFY(decoded);
        QCOMPARE(decoded->error, DownloadSubmissionError::None);
        QCOMPARE(decoded->job_ids, std::vector<std::string>{"owner-job-1"});
        QTRY_COMPARE_WITH_TIMEOUT(owner.count("ADMITTED"), 1, 3'000);
        QCOMPARE(contender.count("ADMITTED"), 0);
        QVERIFY(owner.command("quit\n"));
        QVERIFY(contender.command("quit\n"));
        QTRY_COMPARE_WITH_TIMEOUT(owner.process.state(), QProcess::NotRunning, 3'000);
        QTRY_COMPARE_WITH_TIMEOUT(contender.process.state(), QProcess::NotRunning, 3'000);
        QCOMPARE(owner.process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(owner.process.exitCode(), 0);
        QCOMPARE(contender.process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(contender.process.exitCode(), 0);
        QVERIFY(owner.cleanOutput());
        QVERIFY(contender.cleanOutput());
    }

    void stoppedObjectRetainsLeaseAndDestructionReleasesItWhileProcessLives() {
        const auto name = endpoint();
        Child owner;
        Child rejected;
        Child successor;
        QCOMPARE(firstPipeInstanceError(name), DWORD{ERROR_SUCCESS});
        QVERIFY(owner.start(name));
        QTRY_COMPARE_WITH_TIMEOUT(owner.count("READY"), 1, 3'000);
        QCOMPARE(firstPipeInstanceError(name), DWORD{ERROR_ACCESS_DENIED});
        QVERIFY(owner.command("shutdown\n"));
        QTRY_COMPARE_WITH_TIMEOUT(owner.count("STOPPED"), 1, 3'000);
        QCOMPARE(owner.process.state(), QProcess::Running);
        QCOMPARE(firstPipeInstanceError(name), DWORD{ERROR_ACCESS_DENIED});
        QVERIFY(rejected.start(name));
        QTRY_VERIFY_WITH_TIMEOUT(rejected.count("READY") + rejected.count("REFUSED") != 0, 3'000);
        QCOMPARE(rejected.count("REFUSED"), 1);
        QCOMPARE(rejected.count("READY"), 0);
        QVERIFY(owner.command("release\n"));
        QTRY_COMPARE_WITH_TIMEOUT(owner.count("RELEASED"), 1, 3'000);
        QCOMPARE(owner.process.state(), QProcess::Running);
        QCOMPARE(rejected.process.state(), QProcess::Running);
        QCOMPARE(firstPipeInstanceError(name), DWORD{ERROR_SUCCESS});
        QVERIFY(successor.start(name));
        QTRY_VERIFY_WITH_TIMEOUT(successor.count("READY") + successor.count("REFUSED") != 0, 3'000);
        QCOMPARE(successor.count("READY"), 1);
        QCOMPARE(successor.count("REFUSED"), 0);
        QCOMPARE(firstPipeInstanceError(name), DWORD{ERROR_ACCESS_DENIED});
        for (auto* child : {&owner, &rejected, &successor}) {
            QVERIFY(child->command("quit\n"));
            QTRY_COMPARE_WITH_TIMEOUT(child->process.state(), QProcess::NotRunning, 3'000);
            QCOMPARE(child->process.exitStatus(), QProcess::NormalExit);
            QCOMPARE(child->process.exitCode(), 0);
            QVERIFY(child->cleanOutput());
        }
        QCOMPARE(firstPipeInstanceError(name), DWORD{ERROR_SUCCESS});
    }

    void killedOwnerRecoversWithRejectedContenderObjectsStillAlive() {
        const auto name = endpoint();
        Child owner;
        Child rejected_one;
        Child rejected_two;
        Child successor;
        QCOMPARE(firstPipeInstanceError(name), DWORD{ERROR_SUCCESS});
        QVERIFY(owner.start(name));
        QTRY_COMPARE_WITH_TIMEOUT(owner.count("READY"), 1, 3'000);
        QCOMPARE(firstPipeInstanceError(name), DWORD{ERROR_ACCESS_DENIED});
        for (auto* child : {&rejected_one, &rejected_two}) {
            QVERIFY(child->start(name));
            QTRY_VERIFY_WITH_TIMEOUT(child->count("READY") + child->count("REFUSED") != 0, 3'000);
            QCOMPARE(child->count("REFUSED"), 1);
            QCOMPARE(child->process.state(), QProcess::Running);
        }
        QCOMPARE(firstPipeInstanceError(name), DWORD{ERROR_ACCESS_DENIED});
        owner.process.kill();
        QTRY_COMPARE_WITH_TIMEOUT(owner.process.state(), QProcess::NotRunning, 3'000);
        QCOMPARE(owner.process.exitStatus(), QProcess::CrashExit);
        QCOMPARE(firstPipeInstanceError(name), DWORD{ERROR_SUCCESS});
        QVERIFY(successor.start(name));
        QTRY_VERIFY_WITH_TIMEOUT(successor.count("READY") + successor.count("REFUSED") != 0, 3'000);
        QCOMPARE(successor.count("READY"), 1);
        QCOMPARE(successor.count("REFUSED"), 0);
        QCOMPARE(firstPipeInstanceError(name), DWORD{ERROR_ACCESS_DENIED});
        for (auto* child : {&rejected_one, &rejected_two, &successor}) {
            QVERIFY(child->command("quit\n"));
            QTRY_COMPARE_WITH_TIMEOUT(child->process.state(), QProcess::NotRunning, 3'000);
            QCOMPARE(child->process.exitStatus(), QProcess::NormalExit);
            QCOMPARE(child->process.exitCode(), 0);
            QVERIFY(child->cleanOutput());
        }
        QVERIFY(owner.cleanOutput());
        QCOMPARE(firstPipeInstanceError(name), DWORD{ERROR_SUCCESS});
    }

    void invalidConfigurationDoesNotOwnEndpointAndDistinctEndpointsRemainIndependent() {
        const auto name = endpoint();
        Child invalid;
        Child valid;
        Child independent;
        QVERIFY(invalid.start(name, QStringLiteral("primary"), QStringLiteral("invalid")));
        QTRY_COMPARE_WITH_TIMEOUT(invalid.count("REFUSED"), 1, 3'000);
        QCOMPARE(invalid.count("READY"), 0);
        QCOMPARE(firstPipeInstanceError(name), DWORD{ERROR_SUCCESS});
        QVERIFY(valid.start(name));
        QTRY_COMPARE_WITH_TIMEOUT(valid.count("READY"), 1, 3'000);
        QVERIFY(independent.start(endpoint(), QStringLiteral("alternate")));
        QTRY_COMPARE_WITH_TIMEOUT(independent.count("READY"), 1, 3'000);
        for (auto* child : {&invalid, &valid, &independent}) {
            QCOMPARE(child->process.state(), QProcess::Running);
            QVERIFY(child->command("quit\n"));
            QTRY_COMPARE_WITH_TIMEOUT(child->process.state(), QProcess::NotRunning, 3'000);
            QCOMPARE(child->process.exitStatus(), QProcess::NormalExit);
            QCOMPARE(child->process.exitCode(), 0);
            QVERIFY(child->cleanOutput());
        }
    }
};

int main(int argc, char* argv[]) {
    QCoreApplication application{argc, argv};
    const auto arguments = application.arguments();
    if (arguments.size() < 2)
        return 2;
    helper_executable = arguments.at(1);
    NativeDownloadOwnerProcessTests tests;
    QStringList test_arguments{arguments.front()};
    test_arguments.append(arguments.sliced(2));
    return QTest::qExec(&tests, test_arguments);
}

#include "NativeDownloadOwnerProcessTests.moc"
