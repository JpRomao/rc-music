#include "NativeHostBuildConfiguration.hpp"
#include "adapters/NativeDownloadCodec.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

#include <limits>
#include <memory>

using namespace music_player::m0;

namespace {

QString configured_host;
QString disabled_host;
QString watchdog_helper;
const std::string request_id = "01234567-89ab-4cde-8fab-0123456789ab";

// Closed fixture bytes are intentionally independent of the request encoder.
// Canonical member order lets the assertion compare the whole IPC frame,
// including the unchanged signed request-target and UTF-8 title bytes.
QByteArray requestPayload() {
    return R"({"items":[{"kind":"audio","title":"Áudio","url":"https://fixture.example.test/music.mp3?private=%2f%2F&x=1+2"}],"requestId":"01234567-89ab-4cde-8fab-0123456789ab","schemaVersion":1,"type":"download.request"})";
}

QByteArray acceptedPayload() {
    return R"({"jobIds":["job-test-1"],"requestId":"01234567-89ab-4cde-8fab-0123456789ab","schemaVersion":1,"status":"accepted","type":"download.response"})";
}

QByteArray unavailablePayload() {
    return R"({"code":"not_available","requestId":"01234567-89ab-4cde-8fab-0123456789ab","schemaVersion":1,"status":"rejected","type":"download.response"})";
}

QByteArray prefix(quint32 count) {
    QByteArray result(4, Qt::Uninitialized);
    qToLittleEndian<quint32>(count, result.data());
    return result;
}

QByteArray frame(const QByteArray& payload) { return prefix(static_cast<quint32>(payload.size())) + payload; }

QByteArray expectedIpcBytes() {
    return QByteArray{"RCMDLIPC"} + char{1} + nativeHostBuildConfiguration().allowed_origin.toUtf8() +
           frame(requestPayload());
}

class ChildGuard final {
  public:
    explicit ChildGuard(QProcess& process) : process_(process) {}
    ~ChildGuard() {
        // Never target the persistent app, browser, a PID from input, or another
        // test. Only this QProcess child belongs to this guard.
        if (process_.state() != QProcess::NotRunning) {
            process_.kill();
            static_cast<void>(process_.waitForFinished(2'000));
        }
    }

  private:
    QProcess& process_;
};

void prepare(QProcess& child, const QString& program, const QStringList& arguments) {
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QT_LOGGING_TO_CONSOLE"), QStringLiteral("1"));
    environment.insert(QStringLiteral("QT_FORCE_STDERR_LOGGING"), QStringLiteral("1"));
    child.setProcessEnvironment(environment);
    child.setProgram(program);
    child.setArguments(arguments);
    child.setProcessChannelMode(QProcess::SeparateChannels);
}

class FakePeer final {
  public:
    QLocalServer server;
    std::unique_ptr<QLocalSocket> socket;
    QByteArray received;
    int connections{0};
    bool excess_bytes{false};

    FakePeer() {
        server.setSocketOptions(QLocalServer::UserAccessOption);
        QObject::connect(&server, &QLocalServer::newConnection, &server, [this] {
            while (server.hasPendingConnections()) {
                auto* incoming = server.nextPendingConnection();
                if (incoming == nullptr)
                    continue;
                ++connections;
                if (socket) {
                    incoming->abort();
                    incoming->deleteLater();
                    continue;
                }
                socket.reset(incoming);
                socket->setParent(nullptr);
                socket->setReadBufferSize(256 * 1024 + 66);
                QObject::connect(socket.get(), &QLocalSocket::readyRead, &server, [this] { read(); });
                QObject::connect(socket.get(), &QLocalSocket::disconnected, &server, [this] { read(); });
                read();
            }
        });
    }

    ~FakePeer() {
        if (socket) {
            QObject::disconnect(socket.get(), nullptr, &server, nullptr);
            socket->abort();
            socket.reset();
        }
        server.close();
    }

    bool listen() { return server.listen(nativeHostBuildConfiguration().endpoint_name); }

    void read() {
        if (!socket)
            return;
        const auto remaining = 256 * 1024 + 66 - received.size();
        if (remaining <= 0) {
            excess_bytes = true;
            socket->abort();
            return;
        }
        received += socket->read(remaining);
        if (received.size() > expectedIpcBytes().size())
            excess_bytes = true;
    }

    bool reply(const QByteArray& response) {
        if (!socket || socket->write(response) != response.size())
            return false;
        socket->disconnectFromServer();
        return true;
    }
};

} // namespace

class NativeHostProcessTests final : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase() {
        QVERIFY(QFileInfo{configured_host}.isFile());
        QVERIFY(QFileInfo{disabled_host}.isFile());
        QVERIFY(QFileInfo{watchdog_helper}.isFile());
        QVERIFY(isValidNativeHostConfiguration(nativeHostBuildConfiguration()));
    }

    void disabledProductionHostCannotAdoptSpoofedArgumentsOrEnvironment_data() {
        QTest::addColumn<bool>("spoof_arguments");
        QTest::newRow("environment only") << false;
        QTest::newRow("environment and arguments") << true;
    }

    void disabledProductionHostCannotAdoptSpoofedArgumentsOrEnvironment() {
        QFETCH(bool, spoof_arguments);
        FakePeer peer;
        QVERIFY(peer.listen());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QProcess child;
        ChildGuard guard{child};
        QStringList arguments{nativeHostBuildConfiguration().allowed_origin};
        if (spoof_arguments) {
            arguments.push_back(QStringLiteral("--allowed-origin=") + nativeHostBuildConfiguration().allowed_origin);
            arguments.push_back(QStringLiteral("--endpoint=") + nativeHostBuildConfiguration().endpoint_name);
        }
        prepare(child, disabled_host, arguments);
        auto environment = child.processEnvironment();
        environment.insert(QStringLiteral("RC_MUSIC_NATIVE_MAIN_ORIGIN"),
                           nativeHostBuildConfiguration().allowed_origin);
        environment.insert(QStringLiteral("RC_MUSIC_NATIVE_ENDPOINT"), nativeHostBuildConfiguration().endpoint_name);
        child.setProcessEnvironment(environment);
        child.setWorkingDirectory(directory.path());
        child.start();
        QVERIFY(child.waitForStarted(2'000));
        static_cast<void>(child.write(frame(requestPayload())));
        QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 5'000);
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QVERIFY(child.exitCode() != 0);
        QCOMPARE(peer.connections, 0);
        QVERIFY(peer.received.isEmpty());
        QVERIFY(child.readAllStandardOutput().isEmpty());
        QVERIFY(child.readAllStandardError().isEmpty());
        QCOMPARE(QDir{directory.path()}.entryList(QDir::Files | QDir::NoDotAndDotDot).size(), qsizetype{0});
    }

    void unauthorizedOriginAndFlagsNeverConnect_data() {
        QTest::addColumn<QStringList>("arguments");
        QTest::newRow("different extension")
            << QStringList{QStringLiteral("chrome-extension://bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/")};
        QTest::newRow("page origin") << QStringList{QStringLiteral("https://fixture.example.test/")};
        QTest::newRow("origin suffix") << QStringList{nativeHostBuildConfiguration().allowed_origin +
                                                      QStringLiteral("popup.html")};
        QTest::newRow("unknown flag") << QStringList{nativeHostBuildConfiguration().allowed_origin,
                                                     QStringLiteral("--config=anything")};
        QTest::newRow("noncanonical window")
            << QStringList{nativeHostBuildConfiguration().allowed_origin, QStringLiteral("--parent-window=01")};
        QTest::newRow("duplicate window")
            << QStringList{nativeHostBuildConfiguration().allowed_origin, QStringLiteral("--parent-window=0"),
                           QStringLiteral("--parent-window=1")};
    }

    void unauthorizedOriginAndFlagsNeverConnect() {
        QFETCH(QStringList, arguments);
        FakePeer peer;
        QVERIFY(peer.listen());
        QProcess child;
        ChildGuard guard{child};
        prepare(child, configured_host, arguments);
        child.start();
        QVERIFY(child.waitForStarted(2'000));
        static_cast<void>(child.write(frame(requestPayload())));
        QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 5'000);
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QVERIFY(child.exitCode() != 0);
        QCOMPARE(peer.connections, 0);
        QVERIFY(peer.received.isEmpty());
        QVERIFY(child.readAllStandardOutput().isEmpty());
        QVERIFY(child.readAllStandardError().isEmpty());
    }

    void fragmentedStdinProducesOneExactBinaryReplyAndOneIpcRequest() {
        FakePeer peer;
        QVERIFY(peer.listen());
        QProcess child;
        ChildGuard guard{child};
        prepare(child, configured_host,
                {nativeHostBuildConfiguration().allowed_origin, QStringLiteral("--parent-window=0")});
        child.start();
        QVERIFY(child.waitForStarted(2'000));
        const auto input = frame(requestPayload());
        for (const auto& fragment : {input.first(1), input.sliced(1, 2), input.sliced(3, 1), input.sliced(4, 11)}) {
            QCOMPARE(child.write(fragment), fragment.size());
            QTRY_COMPARE_WITH_TIMEOUT(child.bytesToWrite(), qint64{0}, 1'000);
            QCoreApplication::processEvents();
            QCOMPARE(peer.connections, 0);
        }
        const auto last = input.sliced(15);
        QCOMPARE(child.write(last), last.size());
        QTRY_VERIFY_WITH_TIMEOUT(peer.received == expectedIpcBytes() || child.state() == QProcess::NotRunning, 5'000);
        QCOMPARE(peer.received, expectedIpcBytes());
        QCOMPARE(peer.connections, 1);
        QVERIFY(!peer.excess_bytes);
        // Keep stdin open: EOF while Pending closes only the observer and must
        // not be used as a success trigger or implicit request completion.
        QVERIFY(child.readAllStandardOutput().isEmpty());
        QVERIFY(peer.reply(frame(acceptedPayload())));
        QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 5'000);
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QCOMPARE(child.exitCode(), 0);
        const auto wire = child.readAllStandardOutput();
        QCOMPARE(wire, frame(acceptedPayload()));
        const auto result = decodeNativeDownloadResponse(wire.sliced(4), request_id, 1);
        QVERIFY(result);
        QCOMPARE(result->error, DownloadSubmissionError::None);
        QCOMPARE(result->job_ids, std::vector<std::string>{"job-test-1"});
        QVERIFY(child.readAllStandardError().isEmpty());
        QCOMPARE(peer.connections, 1);
        QCOMPARE(peer.received, expectedIpcBytes());
    }

    void malformedFramesAndIncompleteEofNeverConnect_data() {
        QTest::addColumn<QByteArray>("input");
        QTest::newRow("aligned frame plus extra") << frame(requestPayload()) + QByteArray{"x"};
        QTest::newRow("two coalesced frames") << frame(requestPayload()) + frame(requestPayload());
        QTest::newRow("zero length") << prefix(0);
        QTest::newRow("oversized length") << prefix(256 * 1024 + 1);
        QTest::newRow("overflow length") << prefix(std::numeric_limits<quint32>::max());
        QTest::newRow("EOF partial prefix") << prefix(12).first(2);
        QTest::newRow("EOF partial body") << prefix(12) + QByteArray{"{}"};
        QTest::newRow("invalid JSON") << frame("not JSON");
        QTest::newRow("local opening command") << frame(R"({"schemaVersion":1,"type":"openSources","paths":[]})");
    }

    void malformedFramesAndIncompleteEofNeverConnect() {
        QFETCH(QByteArray, input);
        FakePeer peer;
        QVERIFY(peer.listen());
        QProcess child;
        ChildGuard guard{child};
        prepare(child, configured_host, {nativeHostBuildConfiguration().allowed_origin});
        child.start();
        QVERIFY(child.waitForStarted(2'000));
        static_cast<void>(child.write(input));
        child.closeWriteChannel();
        QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 5'000);
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QVERIFY(child.exitCode() != 0);
        QCOMPARE(peer.connections, 0);
        QVERIFY(peer.received.isEmpty());
        QVERIFY(child.readAllStandardOutput().isEmpty());
        QVERIFY(child.readAllStandardError().isEmpty());
    }

    void missingOwnerProducesCorrelatedNotAvailableWithoutRetry() {
        QProcess child;
        ChildGuard guard{child};
        prepare(child, configured_host, {nativeHostBuildConfiguration().allowed_origin});
        child.start();
        QVERIFY(child.waitForStarted(2'000));
        QCOMPARE(child.write(frame(requestPayload())), frame(requestPayload()).size());
        QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 7'000);
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        const auto wire = child.readAllStandardOutput();
        QCOMPARE(wire, frame(unavailablePayload()));
        const auto result = decodeNativeDownloadResponse(wire.sliced(4), request_id, 1);
        QVERIFY(result);
        QCOMPARE(result->error, DownloadSubmissionError::NotAvailable);
        QVERIFY(result->job_ids.empty());
        QVERIFY(child.readAllStandardError().isEmpty());
        // A late owner cannot observe a reconnect after a terminal rejection.
        FakePeer late_peer;
        QVERIFY(late_peer.listen());
        QCoreApplication::processEvents();
        QCOMPARE(late_peer.connections, 0);
    }

    void pendingEofClosesObserverWithoutAckOrRetransmission() {
        FakePeer peer;
        QVERIFY(peer.listen());
        QProcess child;
        ChildGuard guard{child};
        prepare(child, configured_host, {nativeHostBuildConfiguration().allowed_origin});
        child.start();
        QVERIFY(child.waitForStarted(2'000));
        QCOMPARE(child.write(frame(requestPayload())), frame(requestPayload()).size());
        QTRY_VERIFY_WITH_TIMEOUT(peer.received == expectedIpcBytes() || child.state() == QProcess::NotRunning, 5'000);
        QCOMPARE(peer.received, expectedIpcBytes());
        QCOMPARE(peer.connections, 1);
        QVERIFY(child.readAllStandardOutput().isEmpty());
        child.closeWriteChannel();
        QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 5'000);
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QVERIFY(child.exitCode() != 0);
        QVERIFY(child.readAllStandardOutput().isEmpty());
        QVERIFY(child.readAllStandardError().isEmpty());
        QCOMPARE(peer.received, expectedIpcBytes());
        QCOMPARE(peer.connections, 1);
        QVERIFY(!peer.excess_bytes);
    }

    void hostilePeerReplyNeverProducesAckOrReconnect_data() {
        QTest::addColumn<QByteArray>("reply");
        QTest::newRow("nonJSON response") << frame("not JSON");
        auto mismatch = acceptedPayload();
        mismatch.replace("0123456789ab", "0123456789ac");
        QTest::newRow("uncorrelated response") << frame(mismatch);
        QTest::newRow("double response") << frame(acceptedPayload()) + frame(acceptedPayload());
        QTest::newRow("zero peer length") << prefix(0);
        QTest::newRow("oversized peer length") << prefix(4097);
        QTest::newRow("peer incomplete response") << prefix(200) + QByteArray{"{}"};
    }

    void hostilePeerReplyNeverProducesAckOrReconnect() {
        QFETCH(QByteArray, reply);
        FakePeer peer;
        QVERIFY(peer.listen());
        QProcess child;
        ChildGuard guard{child};
        prepare(child, configured_host, {nativeHostBuildConfiguration().allowed_origin});
        child.start();
        QVERIFY(child.waitForStarted(2'000));
        QCOMPARE(child.write(frame(requestPayload())), frame(requestPayload()).size());
        QTRY_VERIFY_WITH_TIMEOUT(peer.received == expectedIpcBytes() || child.state() == QProcess::NotRunning, 5'000);
        QCOMPARE(peer.received, expectedIpcBytes());
        QCOMPARE(peer.connections, 1);
        QVERIFY(peer.reply(reply));
        QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 5'000);
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QVERIFY(child.exitCode() != 0);
        QVERIFY(child.readAllStandardOutput().isEmpty());
        QVERIFY(child.readAllStandardError().isEmpty());
        QCOMPARE(peer.connections, 1);
        QCOMPARE(peer.received, expectedIpcBytes());
        QVERIFY(!peer.excess_bytes);
    }

    void noInputExpiresByTotalAssemblyDeadlineWithoutIpc() {
        FakePeer peer;
        QVERIFY(peer.listen());
        QProcess child;
        ChildGuard guard{child};
        prepare(child, configured_host, {nativeHostBuildConfiguration().allowed_origin});
        QElapsedTimer elapsed;
        elapsed.start();
        child.start();
        QVERIFY(child.waitForStarted(2'000));
        QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 8'000);
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QVERIFY(child.exitCode() != 0);
        // This checks a finite protocol deadline, not a performance budget.
        QVERIFY(elapsed.elapsed() >= 4'500);
        QVERIFY(elapsed.elapsed() < 8'000);
        QCOMPARE(peer.connections, 0);
        QVERIFY(peer.received.isEmpty());
        QVERIFY(child.readAllStandardOutput().isEmpty());
        QVERIFY(child.readAllStandardError().isEmpty());
    }

    void independentWatchdogCoversNormalExitStallAndLateTeardown_data() {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<int>("expected_exit");
        QTest::newRow("finish") << QStringLiteral("finish") << 0;
        QTest::newRow("stall") << QStringLiteral("stall") << 2;
        QTest::newRow("late-teardown") << QStringLiteral("late-teardown") << 2;
    }

    void independentWatchdogCoversNormalExitStallAndLateTeardown() {
        QFETCH(QString, mode);
        QFETCH(int, expected_exit);
        QProcess child;
        ChildGuard guard{child};
        prepare(child, watchdog_helper, {mode});
        QElapsedTimer elapsed;
        elapsed.start();
        child.start();
        QVERIFY(child.waitForStarted(2'000));
        QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 3'000);
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QCOMPARE(child.exitCode(), expected_exit);
        QVERIFY(elapsed.elapsed() < 3'000);
        QVERIFY(child.readAllStandardOutput().isEmpty());
        QVERIFY(child.readAllStandardError().isEmpty());
    }
};

int main(int argc, char** argv) {
    QCoreApplication application{argc, argv};
    if (application.arguments().size() != 4)
        return 1;
    configured_host = application.arguments()[1];
    disabled_host = application.arguments()[2];
    watchdog_helper = application.arguments()[3];
    NativeHostProcessTests tests;
    return QTest::qExec(&tests);
}

#include "NativeHostProcessTests.moc"
