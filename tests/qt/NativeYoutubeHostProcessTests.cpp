#include "NativeHostBuildConfiguration.hpp"
#include "adapters/YoutubeAudioCodec.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <QtEndian>

#include <memory>

using namespace music_player::m0;
namespace {
QString configured_host, disabled_host;
const QString main_origin{QStringLiteral("chrome-extension://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/")};
const std::string request_id{"01234567-89ab-4cde-8fab-0123456789ab"};
QByteArray requestPayload() {
    return R"({"items":[{"artist":"Synthetic artist","fetchLyrics":false,"lyricsPage":"","title":"Áudio / 日本","videoId":"testVideo01"}],"requestId":"01234567-89ab-4cde-8fab-0123456789ab","schemaVersion":1,"type":"youtube.audio.request"})";
}
QByteArray acceptedPayload() {
    return R"({"jobIds":["youtube-job-1"],"requestId":"01234567-89ab-4cde-8fab-0123456789ab","schemaVersion":1,"status":"accepted","type":"youtube.audio.response"})";
}
QByteArray unavailablePayload() {
    return R"({"code":"not_available","requestId":"01234567-89ab-4cde-8fab-0123456789ab","schemaVersion":1,"status":"rejected","type":"youtube.audio.response"})";
}
QByteArray frame(const QByteArray& payload) {
    QByteArray result(4, Qt::Uninitialized);
    qToLittleEndian<quint32>(static_cast<quint32>(payload.size()), result.data());
    return result + payload;
}
QByteArray expectedIpcBytes() {
    // Independent of the typed IPC/header and request encoder under test.
    return QByteArrayLiteral("RCMYTIPC") + char{1} + nativeHostBuildConfiguration().allowed_origin.toUtf8() +
           frame(requestPayload());
}
class Child final {
  public:
    QProcess process;
    ~Child() {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            static_cast<void>(process.waitForFinished(2000));
        }
    }
    void prepare(const QString& executable, const QStringList& arguments) {
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("QT_LOGGING_TO_CONSOLE"), QStringLiteral("1"));
        environment.insert(QStringLiteral("QT_FORCE_STDERR_LOGGING"), QStringLiteral("1"));
        process.setProcessEnvironment(environment);
        process.setProgram(executable);
        process.setArguments(arguments);
        process.setProcessChannelMode(QProcess::SeparateChannels);
    }
};
class Peer final {
  public:
    QLocalServer server;
    std::unique_ptr<QLocalSocket> socket;
    QByteArray received;
    int connections{};
    bool excess{};
    Peer() {
        server.setSocketOptions(QLocalServer::UserAccessOption);
        QObject::connect(&server, &QLocalServer::newConnection, &server, [this] {
            while (server.hasPendingConnections()) {
                auto* incoming = server.nextPendingConnection();
                if (!incoming)
                    continue;
                ++connections;
                if (socket) {
                    incoming->abort();
                    incoming->deleteLater();
                    continue;
                }
                socket.reset(incoming);
                socket->setParent(nullptr);
                socket->setReadBufferSize(64 * 1024 + 66);
                QObject::connect(socket.get(), &QLocalSocket::readyRead, &server, [this] { read(); });
                QObject::connect(socket.get(), &QLocalSocket::disconnected, &server, [this] { read(); });
                read();
            }
        });
    }
    ~Peer() {
        if (socket) {
            QObject::disconnect(socket.get(), nullptr, &server, nullptr);
            socket->abort();
            socket.reset();
        }
        server.close();
    }
    bool listen(const QString& name = nativeHostBuildConfiguration().endpoint_name) { return server.listen(name); }
    void read() {
        if (!socket)
            return;
        constexpr qsizetype maximum = 64 * 1024 + 66;
        const auto remaining = maximum - received.size();
        if (remaining <= 0) {
            excess = true;
            socket->abort();
            return;
        }
        received += socket->read(remaining);
        excess = excess || received.size() > expectedIpcBytes().size();
    }
    bool reply(const QByteArray& bytes) {
        if (!socket || socket->write(bytes) != bytes.size())
            return false;
        socket->disconnectFromServer();
        return true;
    }
};
} // namespace

class NativeYoutubeHostProcessTests final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QVERIFY(QFileInfo(configured_host).isFile());
        QVERIFY(QFileInfo(disabled_host).isFile());
        QVERIFY(isValidNativeHostConfiguration(nativeHostBuildConfiguration()));
        QVERIFY(nativeHostBuildConfiguration().allowed_origin != main_origin);
    }
    void disabledHostCannotAdoptYoutubeOrMainIdentityFromEnvironmentOrArguments_data() {
        QTest::addColumn<bool>("arguments");
        QTest::newRow("environment") << false;
        QTest::newRow("environment and flags") << true;
    }
    void disabledHostCannotAdoptYoutubeOrMainIdentityFromEnvironmentOrArguments() {
        QFETCH(bool, arguments);
        Peer peer;
        QVERIFY(peer.listen());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Child child;
        QStringList caller{nativeHostBuildConfiguration().allowed_origin};
        if (arguments)
            caller << QStringLiteral("--allowed-origin=") + nativeHostBuildConfiguration().allowed_origin
                   << QStringLiteral("--endpoint=") + nativeHostBuildConfiguration().endpoint_name;
        child.prepare(disabled_host, caller);
        auto environment = child.process.processEnvironment();
        environment.insert(QStringLiteral("RC_MUSIC_NATIVE_YOUTUBE_ORIGIN"),
                           nativeHostBuildConfiguration().allowed_origin);
        environment.insert(QStringLiteral("RC_MUSIC_NATIVE_YOUTUBE_ENDPOINT"),
                           nativeHostBuildConfiguration().endpoint_name);
        environment.insert(QStringLiteral("RC_MUSIC_NATIVE_MAIN_ORIGIN"), main_origin);
        environment.insert(QStringLiteral("RC_MUSIC_NATIVE_ENDPOINT"), nativeHostBuildConfiguration().endpoint_name);
        child.process.setProcessEnvironment(environment);
        child.process.setWorkingDirectory(directory.path());
        child.process.start();
        QVERIFY(child.process.waitForStarted(2000));
        static_cast<void>(child.process.write(frame(requestPayload())));
        QTRY_COMPARE_WITH_TIMEOUT(child.process.state(), QProcess::NotRunning, 5000);
        QCOMPARE(child.process.exitStatus(), QProcess::NormalExit);
        QVERIFY(child.process.exitCode() != 0);
        QCOMPARE(peer.connections, 0);
        QVERIFY(peer.received.isEmpty());
        QVERIFY(child.process.readAllStandardOutput().isEmpty());
        QVERIFY(child.process.readAllStandardError().isEmpty());
        QVERIFY(QDir(directory.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
    }
    void principalOriginAndPrincipalPayloadNeverReachYoutubeOwner_data() {
        QTest::addColumn<QString>("caller");
        QTest::addColumn<QByteArray>("payload");
        QTest::newRow("principal origin") << main_origin << requestPayload();
        QTest::newRow("principal payload")
            << nativeHostBuildConfiguration().allowed_origin
            << QByteArrayLiteral(
                   "{\"items\":[{\"kind\":\"audio\",\"title\":\"Synthetic\",\"url\":\"https://fixture.invalid/"
                   "a.mp3\"}],\"requestId\":\"01234567-89ab-4cde-8fab-0123456789ab\",\"schemaVersion\":1,\"type\":"
                   "\"download.request\"}");
    }
    void principalOriginAndPrincipalPayloadNeverReachYoutubeOwner() {
        QFETCH(QString, caller);
        QFETCH(QByteArray, payload);
        Peer peer;
        QVERIFY(peer.listen());
        Child child;
        child.prepare(configured_host, {caller});
        child.process.start();
        QVERIFY(child.process.waitForStarted(2000));
        static_cast<void>(child.process.write(frame(payload)));
        child.process.closeWriteChannel();
        QTRY_COMPARE_WITH_TIMEOUT(child.process.state(), QProcess::NotRunning, 5000);
        QVERIFY(child.process.exitCode() != 0);
        QCOMPARE(peer.connections, 0);
        QVERIFY(child.process.readAllStandardOutput().isEmpty());
        QVERIFY(child.process.readAllStandardError().isEmpty());
    }
    void fragmentedYoutubeRequestProducesOnlyItsOwnExactIpcAndReply() {
        Peer youtube_peer, main_peer;
        QVERIFY(youtube_peer.listen());
        QVERIFY(main_peer.listen(QStringLiteral("rc-music-principal-isolation-") +
                                 QUuid::createUuid().toString(QUuid::Id128)));
        Child child;
        child.prepare(configured_host, {nativeHostBuildConfiguration().allowed_origin});
        child.process.start();
        QVERIFY(child.process.waitForStarted(2000));
        const auto bytes = frame(requestPayload());
        QCOMPARE(child.process.write(bytes.first(3)), qint64{3});
        QVERIFY(child.process.waitForBytesWritten(2000));
        QTest::qWait(25);
        QCOMPARE(youtube_peer.connections, 0);
        QVERIFY(child.process.readAllStandardOutput().isEmpty());
        QCOMPARE(child.process.write(bytes.sliced(3)), static_cast<qint64>(bytes.size() - 3));
        QTRY_COMPARE_WITH_TIMEOUT(youtube_peer.received, expectedIpcBytes(), 5000);
        QCOMPARE(youtube_peer.connections, 1);
        QVERIFY(!youtube_peer.excess);
        QVERIFY(youtube_peer.reply(frame(acceptedPayload())));
        QTRY_COMPARE_WITH_TIMEOUT(child.process.state(), QProcess::NotRunning, 5000);
        QCOMPARE(child.process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(child.process.exitCode(), 0);
        QCOMPARE(child.process.readAllStandardOutput(), frame(acceptedPayload()));
        QVERIFY(child.process.readAllStandardError().isEmpty());
        QCOMPARE(main_peer.connections, 0);
        QVERIFY(main_peer.received.isEmpty());
        QCOMPARE(youtube_peer.connections, 1);
    }
    void missingOwnerProducesOnlyYoutubeCorrelatedUnavailable() {
        Child child;
        child.prepare(configured_host, {nativeHostBuildConfiguration().allowed_origin});
        child.process.start();
        QVERIFY(child.process.waitForStarted(2000));
        QCOMPARE(child.process.write(frame(requestPayload())), static_cast<qint64>(frame(requestPayload()).size()));
        QTRY_COMPARE_WITH_TIMEOUT(child.process.state(), QProcess::NotRunning, 7000);
        QCOMPARE(child.process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(child.process.exitCode(), 0);
        QCOMPARE(child.process.readAllStandardOutput(), frame(unavailablePayload()));
        QVERIFY(child.process.readAllStandardError().isEmpty());
    }
    void pendingEofClosesObserverWithoutAcknowledgementOrRetry() {
        Peer peer;
        QVERIFY(peer.listen());
        Child child;
        child.prepare(configured_host, {nativeHostBuildConfiguration().allowed_origin});
        child.process.start();
        QVERIFY(child.process.waitForStarted(2000));
        static_cast<void>(child.process.write(frame(requestPayload())));
        QTRY_COMPARE_WITH_TIMEOUT(peer.received, expectedIpcBytes(), 5000);
        QCOMPARE(peer.connections, 1);
        QVERIFY(child.process.readAllStandardOutput().isEmpty());
        child.process.closeWriteChannel();
        QTRY_COMPARE_WITH_TIMEOUT(child.process.state(), QProcess::NotRunning, 5000);
        QVERIFY(child.process.exitCode() != 0);
        QVERIFY(child.process.readAllStandardOutput().isEmpty());
        QVERIFY(child.process.readAllStandardError().isEmpty());
        QCOMPARE(peer.connections, 1);
        QCOMPARE(peer.received, expectedIpcBytes());
    }
    void principalOrMismatchedReplyCannotBecomeYoutubeAcknowledgement_data() {
        QTest::addColumn<QByteArray>("payload");
        QTest::newRow("principal response")
            << QByteArrayLiteral("{\"jobIds\":[\"job-1\"],\"requestId\":\"01234567-89ab-4cde-8fab-0123456789ab\","
                                 "\"schemaVersion\":1,\"status\":\"accepted\",\"type\":\"download.response\"}");
        auto mismatched = acceptedPayload();
        mismatched.replace("01234567-89ab-4cde-8fab-0123456789ab", "11234567-89ab-4cde-8fab-0123456789ab");
        QTest::newRow("mismatched request") << mismatched;
    }
    void principalOrMismatchedReplyCannotBecomeYoutubeAcknowledgement() {
        QFETCH(QByteArray, payload);
        Peer peer;
        QVERIFY(peer.listen());
        Child child;
        child.prepare(configured_host, {nativeHostBuildConfiguration().allowed_origin});
        child.process.start();
        QVERIFY(child.process.waitForStarted(2000));
        static_cast<void>(child.process.write(frame(requestPayload())));
        QTRY_COMPARE_WITH_TIMEOUT(peer.received, expectedIpcBytes(), 5000);
        QVERIFY(peer.reply(frame(payload)));
        QTRY_COMPARE_WITH_TIMEOUT(child.process.state(), QProcess::NotRunning, 5000);
        QVERIFY(child.process.exitCode() != 0);
        QVERIFY(child.process.readAllStandardOutput().isEmpty());
        QVERIFY(child.process.readAllStandardError().isEmpty());
        QCOMPARE(peer.connections, 1);
    }
};
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const auto arguments = application.arguments();
    if (arguments.size() != 3)
        return 2;
    configured_host = arguments.at(1);
    disabled_host = arguments.at(2);
    NativeYoutubeHostProcessTests tests;
    return QTest::qExec(&tests, QStringList{arguments.first()});
}
#include "NativeYoutubeHostProcessTests.moc"
