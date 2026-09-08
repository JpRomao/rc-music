#include "PinnedDownloadStream.hpp"

#include <QElapsedTimer>
#include <QFile>
#include <QNetworkProxy>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <limits>
#include <memory>

using namespace music_player::m0;

namespace {

[[nodiscard]] PinnedDownloadEndpoint endpoint(quint16 port, bool tls = false) {
    return {QHostAddress::LocalHost, QStringLiteral("media.rc-music.invalid"),
            QByteArrayLiteral("/a%2fb/%41.mp4?sig=%2f%2F%252f+z&empty=&dup=1&dup=2"), port, tls};
}

[[nodiscard]] QByteArray expectedRequest(const PinnedDownloadEndpoint& value) {
    return "GET " + value.request_target + " HTTP/1.1\r\nHost: " + value.host.toLatin1() + ':' +
           QByteArray::number(value.port) + "\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n";
}

[[nodiscard]] QByteArray fixture(const char* name) {
    const auto path = QFINDTESTDATA(QStringLiteral("network-fixtures/") + QString::fromLatin1(name));
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

[[nodiscard]] QSslConfiguration trustedConfiguration() {
    auto configuration = QSslConfiguration::defaultConfiguration();
    configuration.setCaCertificates({QSslCertificate(fixture("server-cert.pem"))});
    return configuration;
}

[[nodiscard]] QSslConfiguration serverConfiguration() {
    auto configuration = trustedConfiguration();
    configuration.setLocalCertificate(QSslCertificate(fixture("server-cert.pem")));
    configuration.setPrivateKey(QSslKey(fixture("server-key.pem"), QSsl::Rsa));
    configuration.setProtocol(QSsl::TlsV1_2OrLater);
    configuration.setPeerVerifyMode(QSslSocket::VerifyNone); // The fixture server does not request client identity.
    configuration.setAllowedNextProtocols({QByteArrayLiteral("http/1.1")});
    return configuration;
}

// Parse just the synthetic ClientHello's SNI extension to prove the logical name
// on the wire. This helper never parses downloaded HTTP or enters production.
[[nodiscard]] QByteArray clientHelloServerName(const QByteArray& bytes) {
    const auto at = [&bytes](qsizetype offset) { return static_cast<unsigned char>(bytes.at(offset)); };
    const auto word = [&at](qsizetype offset) { return (at(offset) << 8) | at(offset + 1); };
    if (bytes.size() < 44 || at(0) != 22 || at(5) != 1 || bytes.size() < 5 + word(3))
        return {};
    qsizetype position = 43;
    position += 1 + at(position);
    if (position + 2 > bytes.size())
        return {};
    position += 2 + word(position);
    if (position + 1 > bytes.size())
        return {};
    position += 1 + at(position);
    if (position + 2 > bytes.size())
        return {};
    const auto extensions_end = position + 2 + word(position);
    position += 2;
    if (extensions_end > bytes.size())
        return {};
    while (position + 4 <= extensions_end) {
        const auto type = word(position);
        const auto length = word(position + 2);
        position += 4;
        if (position + length > extensions_end)
            return {};
        if (type == 0 && length >= 5 && at(position + 2) == 0) {
            const auto name_length = word(position + 3);
            if (name_length + 5 <= length)
                return bytes.sliced(position + 5, name_length);
            return {};
        }
        position += length;
    }
    return {};
}

} // namespace

class PinnedDownloadStreamTests final : public QObject {
    Q_OBJECT

  private slots:
    void startsIdleWithoutNetworkOrReadableData() {
        PinnedDownloadStream stream;
        QCOMPARE(stream.poll().state, PinnedDownloadStreamState::Idle);
        QCOMPARE(stream.poll().error, PinnedDownloadStreamError::None);
        QCOMPARE(stream.poll().available_bytes, 0);
        QVERIFY(stream.read(1024).isEmpty());
        stream.cancel();
        QCOMPARE(stream.poll().state, PinnedDownloadStreamState::Cancelled);
    }

    void sendsExactSignedTargetAndLogicalHostToPinnedLoopback_data() {
        QTest::addColumn<QByteArray>("target");
        QTest::addColumn<QString>("host");
        QTest::newRow("signed escapes") << endpoint(80).request_target << endpoint(80).host;
        QTest::newRow("shared policy path punctuation") << QByteArrayLiteral("/a^|b.mp3") << endpoint(80).host;
        QTest::newRow("shared policy query punctuation")
            << QByteArrayLiteral("/a.mp3?signature=`{}|^") << endpoint(80).host;
        const auto maximum_host =
            QString(63, 'a') + '.' + QString(63, 'b') + '.' + QString(63, 'c') + '.' + QString(61, 'd') + '.';
        QTest::newRow("253-byte host plus terminal root dot") << QByteArrayLiteral("/a.mp3") << maximum_host;
    }

    void sendsExactSignedTargetAndLogicalHostToPinnedLoopback() {
        QFETCH(QByteArray, target);
        QFETCH(QString, host);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        PinnedDownloadStream stream;
        auto value = endpoint(server.serverPort());
        value.request_target = target;
        value.host = host;
        QVERIFY(stream.open(value));
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 3000);
        std::unique_ptr<QTcpSocket> peer(server.nextPendingConnection());
        const auto request = expectedRequest(value);
        QTRY_VERIFY_WITH_TIMEOUT(peer->bytesAvailable() >= request.size(), 3000);
        QCOMPARE(peer->readAll(), request);
        QCOMPARE(stream.poll().state, PinnedDownloadStreamState::Receiving);
        QCOMPARE(peer->peerAddress(), QHostAddress(QHostAddress::LocalHost));
        QVERIFY(!request.contains("Cookie:"));
        QVERIFY(!request.contains("Authorization:"));
    }

    void preservesBufferedBinaryResponseAfterPeerEof() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        PinnedDownloadStream stream;
        QVERIFY(stream.open(endpoint(server.serverPort())));
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 3000);
        std::unique_ptr<QTcpSocket> peer(server.nextPendingConnection());
        QTRY_VERIFY_WITH_TIMEOUT(peer->bytesAvailable() > 0, 3000);
        const QByteArray response =
            QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\n") + QByteArray::fromHex("00ff0d0a");
        QCOMPARE(peer->write(response), response.size());
        peer->disconnectFromHost();
        QTRY_COMPARE_WITH_TIMEOUT(stream.poll().state, PinnedDownloadStreamState::Finished, 3000);
        QCOMPARE(stream.poll().error, PinnedDownloadStreamError::None);
        QVERIFY(stream.poll().authenticated_eof);
        QCOMPARE(stream.poll().available_bytes, response.size());
        QCOMPARE(stream.read(7), response.first(7));
        QCOMPARE(stream.read(1000), response.sliced(7));
        QVERIFY(stream.read(1000).isEmpty());
        QCOMPARE(stream.poll().state, PinnedDownloadStreamState::Finished);
    }

    void appliesBackpressureAndBoundsEveryRead_data() {
        QTest::addColumn<bool>("tls");
        QTest::addColumn<qint64>("read_limit");
        QTest::newRow("HTTP plus peer EOF") << false << std::numeric_limits<qint64>::max();
        QTest::newRow("HTTPS plus peer EOF") << true << std::numeric_limits<qint64>::max();
        QTest::newRow("HTTP unaligned reads plus peer EOF") << false << qint64{4093};
        QTest::newRow("HTTPS unaligned reads plus peer EOF") << true << qint64{4093};
        QTest::newRow("HTTPS TLS-record-minus-one reads plus peer EOF") << true << qint64{16383};
        QTest::newRow("HTTPS two-records-minus-one reads plus peer EOF") << true << qint64{32767};
    }

    void appliesBackpressureAndBoundsEveryRead() {
        QFETCH(bool, tls);
        QFETCH(qint64, read_limit);
        std::unique_ptr<QTcpServer> server;
        if (tls) {
            QVERIFY(QSslSocket::supportsSsl());
            auto tls_server = std::make_unique<QSslServer>();
            tls_server->setSslConfiguration(serverConfiguration());
            server = std::move(tls_server);
        } else {
            server = std::make_unique<QTcpServer>();
        }
        QVERIFY(server->listen(QHostAddress::LocalHost));
        PinnedDownloadStream stream;
        if (tls)
            QVERIFY(stream.setTlsConfiguration(trustedConfiguration()));
        QVERIFY(stream.open(endpoint(server->serverPort(), tls)));
        QTRY_VERIFY_WITH_TIMEOUT(server->hasPendingConnections(), 3000);
        std::unique_ptr<QTcpSocket> peer(server->nextPendingConnection());
        QTRY_VERIFY_WITH_TIMEOUT(peer->bytesAvailable() > 0, 3000);
        const QByteArray response(5 * pinned_download_stream_buffer_bytes, 'x');
        QCOMPARE(peer->write(response), response.size());
        peer->disconnectFromHost();
        QTRY_VERIFY_WITH_TIMEOUT(stream.poll().available_bytes > 0, 3000);
        QTest::qWait(25);
        QVERIFY(stream.poll().available_bytes <= pinned_download_stream_buffer_bytes);
        QVERIFY(stream.read(0).isEmpty());
        QVERIFY(stream.read(-1).isEmpty());
        QByteArray received;
        bool stalled_before_eof = false;
        QElapsedTimer deadline;
        deadline.start();
        while (deadline.elapsed() < 5000 && received.size() < response.size()) {
            const auto chunk = stream.read(read_limit);
            QVERIFY(chunk.size() <= pinned_download_stream_buffer_bytes);
            received += chunk;
            if (tls && !stalled_before_eof && received.size() >= response.size() - 65536) {
                // Hold a deliberately unaligned remainder while the sender's
                // final ciphertext and close reach the bounded receive queues.
                stalled_before_eof = true;
                QTest::qWait(25);
            }
            QTest::qWait(1);
            const auto update = stream.poll();
            QVERIFY2(update.state != PinnedDownloadStreamState::Failed,
                     qPrintable(QStringLiteral("state=%1 error=%2 received=%3 available=%4")
                                    .arg(static_cast<int>(update.state))
                                    .arg(static_cast<int>(update.error))
                                    .arg(received.size())
                                    .arg(update.available_bytes)));
            QVERIFY(stream.poll().available_bytes <= pinned_download_stream_buffer_bytes);
        }
        const auto final_update = stream.poll();
        QVERIFY2(received.size() == response.size(),
                 qPrintable(QStringLiteral("state=%1 error=%2 received=%3 expected=%4 available=%5 peerState=%6 "
                                           "peerPending=%7 backend=%8")
                                .arg(static_cast<int>(final_update.state))
                                .arg(static_cast<int>(final_update.error))
                                .arg(received.size())
                                .arg(response.size())
                                .arg(final_update.available_bytes)
                                .arg(static_cast<int>(peer->state()))
                                .arg(peer->bytesToWrite())
                                .arg(QSslSocket::activeBackend())));
        QCOMPARE(received, response);
        QTRY_COMPARE_WITH_TIMEOUT(stream.poll().state, PinnedDownloadStreamState::Finished, 3000);
    }

    void rejectsInvalidWireFieldsBeforeConnecting_data() {
        QTest::addColumn<int>("mutation");
        const char* cases[] = {"empty address",  "zero port",        "empty host",       "host CRLF",
                               "host NUL",       "host Unicode",     "host userinfo",    "host port",
                               "empty target",   "absolute target",  "target CRLF",      "target NUL",
                               "target Unicode", "target fragment",  "target backslash", "target space",
                               "bad percent",    "oversized target", "oversized host",   "scoped address"};
        for (int index = 0; index < 20; ++index)
            QTest::newRow(cases[index]) << index;
    }

    void rejectsInvalidWireFieldsBeforeConnecting() {
        QFETCH(int, mutation);
        auto value = endpoint(12345);
        switch (mutation) {
        case 0:
            value.address = {};
            break;
        case 1:
            value.port = 0;
            break;
        case 2:
            value.host.clear();
            break;
        case 3:
            value.host += "\r\nCookie: x";
            break;
        case 4:
            value.host += QChar(0);
            break;
        case 5:
            value.host = QString::fromUtf8("música.invalid");
            break;
        case 6:
            value.host = "user@media.invalid";
            break;
        case 7:
            value.host = "media.invalid:443";
            break;
        case 8:
            value.request_target.clear();
            break;
        case 9:
            value.request_target = "https://elsewhere.invalid/a";
            break;
        case 10:
            value.request_target += "\r\nCookie: x";
            break;
        case 11:
            value.request_target += '\0';
            break;
        case 12:
            value.request_target = QByteArray::fromHex("2fc3a9");
            break;
        case 13:
            value.request_target += "#fragment";
            break;
        case 14:
            value.request_target += "\\x";
            break;
        case 15:
            value.request_target += " x";
            break;
        case 16:
            value.request_target += "%2z";
            break;
        case 17:
            value.request_target = '/' + QByteArray(8192, 'x');
            break;
        case 18:
            value.host = QString(254, 'a');
            break;
        case 19:
            value.address = QHostAddress("fe80::1%1");
            break;
        default:
            QFAIL("Unknown mutation");
        }
        PinnedDownloadStream stream;
        QVERIFY(!stream.open(value));
        QCOMPARE(stream.poll().state, PinnedDownloadStreamState::Failed);
        QCOMPARE(stream.poll().error, PinnedDownloadStreamError::InvalidEndpoint);
        QCOMPARE(stream.poll().available_bytes, 0);
    }

    void refusesConcurrentOpenAndCancelsWithoutKeepingBufferedData() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        PinnedDownloadStream stream;
        const auto value = endpoint(server.serverPort());
        QVERIFY(stream.open(value));
        QVERIFY(!stream.open(value));
        QVERIFY(!stream.setTlsConfiguration(trustedConfiguration()));
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 3000);
        std::unique_ptr<QTcpSocket> peer(server.nextPendingConnection());
        QTRY_VERIFY_WITH_TIMEOUT(peer->bytesAvailable() > 0, 3000);
        QCOMPARE(peer->write("partial", 7), 7);
        QTRY_VERIFY_WITH_TIMEOUT(stream.poll().available_bytes > 0, 3000);
        stream.cancel();
        stream.cancel();
        QCOMPARE(stream.poll().state, PinnedDownloadStreamState::Cancelled);
        QCOMPARE(stream.poll().available_bytes, 0);
        QVERIFY(stream.read(1024).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(peer->state(), QAbstractSocket::UnconnectedState, 3000);
        QVERIFY(stream.open(value));
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 3000);
        std::unique_ptr<QTcpSocket> second(server.nextPendingConnection());
        QTRY_VERIFY_WITH_TIMEOUT(second->bytesAvailable() > 0, 3000);
        QCOMPARE(second->readAll(), expectedRequest(value));
        QCOMPARE(stream.poll().state, PinnedDownloadStreamState::Receiving);
    }

    void cancellationWhileConnectingNeverReactivatesTheStream() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        PinnedDownloadStream stream;
        QVERIFY(stream.open(endpoint(server.serverPort(), true)));
        stream.cancel();
        QTest::qWait(25);
        QCOMPARE(stream.poll().state, PinnedDownloadStreamState::Cancelled);
        QCOMPARE(stream.poll().error, PinnedDownloadStreamError::None);
        QVERIFY(stream.read(1024).isEmpty());
    }

    void connectionRefusalIsTypedAndNeverReportedAsEof() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const auto value = endpoint(server.serverPort());
        server.close();
        PinnedDownloadStream stream;
        QVERIFY(stream.open(value));
        // Windows can report loopback refusal after more than 3 seconds. This is
        // a harness allowance, not the owning downloader's connection deadline.
        QTRY_COMPARE_WITH_TIMEOUT(stream.poll().state, PinnedDownloadStreamState::Failed, 5000);
        QCOMPARE(stream.poll().error, PinnedDownloadStreamError::Connection);
        QVERIFY(!stream.poll().authenticated_eof);
    }

    void ignoresApplicationProxyAndConnectsOnlyToPinnedAddress() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const auto old_proxy = QNetworkProxy::applicationProxy();
        struct RestoreProxy final {
            QNetworkProxy value;
            ~RestoreProxy() { QNetworkProxy::setApplicationProxy(value); }
        } restore{old_proxy};
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", 1));
        PinnedDownloadStream stream;
        QVERIFY(stream.open(endpoint(server.serverPort())));
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 3000);
        std::unique_ptr<QTcpSocket> peer(server.nextPendingConnection());
        QTRY_VERIFY_WITH_TIMEOUT(peer->bytesAvailable() > 0, 3000);
        QCOMPARE(peer->readAll(), expectedRequest(endpoint(server.serverPort())));
    }

    void sendsLogicalSniBeforeAnyHttpAndDoesNotFallBackToPlaintext() {
        QVERIFY(QSslSocket::supportsSsl());
        QTcpServer raw_server;
        QVERIFY(raw_server.listen(QHostAddress::LocalHost));
        PinnedDownloadStream stream;
        const auto value = endpoint(raw_server.serverPort(), true);
        QVERIFY(stream.open(value));
        QTRY_VERIFY_WITH_TIMEOUT(raw_server.hasPendingConnections(), 3000);
        std::unique_ptr<QTcpSocket> peer(raw_server.nextPendingConnection());
        QByteArray hello;
        QTRY_VERIFY_WITH_TIMEOUT((hello += peer->readAll(), !clientHelloServerName(hello).isEmpty()), 3000);
        QCOMPARE(clientHelloServerName(hello), value.host.toLatin1());
        QVERIFY(!hello.contains("GET "));
        QCOMPARE(peer->write("HTTP/1.1 200 OK\r\n\r\ncleartext"), 28);
        peer->disconnectFromHost();
        QTRY_COMPARE_WITH_TIMEOUT(stream.poll().state, PinnedDownloadStreamState::Failed, 3000);
        QVERIFY(stream.read(1024).isEmpty());
        QVERIFY(!raw_server.hasPendingConnections());
    }

    void verifiesLogicalCertificateWhileConnectingToNumericAddress_data() {
        QTest::addColumn<bool>("trailing_dot");
        QTest::newRow("ordinary DNS host") << false;
        QTest::newRow("absolute DNS host preserves HTTP Host") << true;
    }

    void verifiesLogicalCertificateWhileConnectingToNumericAddress() {
        QFETCH(bool, trailing_dot);
        QVERIFY(QSslSocket::supportsSsl());
        const auto default_configuration = QSslConfiguration::defaultConfiguration();
        QSslServer server;
        server.setSslConfiguration(serverConfiguration());
        QVERIFY(server.listen(QHostAddress::LocalHost));
        PinnedDownloadStream stream;
        QVERIFY(stream.setTlsConfiguration(trustedConfiguration()));
        auto value = endpoint(server.serverPort(), true);
        if (trailing_dot)
            value.host += '.';
        QVERIFY(stream.open(value));
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5000);
        std::unique_ptr<QTcpSocket> peer(server.nextPendingConnection());
        auto* tls_peer = qobject_cast<QSslSocket*>(peer.get());
        QVERIFY(tls_peer);
        QVERIFY(tls_peer->isEncrypted());
        QVERIFY(tls_peer->sessionProtocol() == QSsl::TlsV1_2 || tls_peer->sessionProtocol() == QSsl::TlsV1_3);
        QTRY_VERIFY_WITH_TIMEOUT(peer->bytesAvailable() > 0, 3000);
        QCOMPARE(peer->readAll(), expectedRequest(value));
        const auto response = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nmp3");
        QCOMPARE(peer->write(response), response.size());
        peer->disconnectFromHost();
        QTRY_COMPARE_WITH_TIMEOUT(stream.poll().state, PinnedDownloadStreamState::Finished, 3000);
        QCOMPARE(stream.read(65536), response);
        QCOMPARE(QSslConfiguration::defaultConfiguration(), default_configuration);
    }

    void certificateFailuresCannotBeRelaxedBySuppliedConfiguration_data() {
        QTest::addColumn<bool>("trust");
        QTest::newRow("untrusted self signed") << false;
        QTest::newRow("trusted wrong hostname") << true;
    }

    void certificateFailuresCannotBeRelaxedBySuppliedConfiguration() {
        QFETCH(bool, trust);
        QVERIFY(QSslSocket::supportsSsl());
        QSslServer server;
        server.setSslConfiguration(serverConfiguration());
        QByteArray received;
        connect(&server, &QSslServer::startedEncryptionHandshake, &server, [&received](QSslSocket* socket) {
            connect(socket, &QSslSocket::readyRead, socket, [socket, &received] { received += socket->readAll(); });
        });
        QVERIFY(server.listen(QHostAddress::LocalHost));
        PinnedDownloadStream stream;
        auto configuration = trust ? trustedConfiguration() : QSslConfiguration::defaultConfiguration();
        configuration.setPeerVerifyMode(QSslSocket::VerifyNone);
        configuration.setProtocol(QSsl::AnyProtocol);
        configuration.setAllowedNextProtocols({QByteArrayLiteral("h2")});
        QVERIFY(stream.setTlsConfiguration(configuration));
        auto value = endpoint(server.serverPort(), true);
        if (trust)
            value.host = QStringLiteral("wrong.rc-music.invalid");
        QVERIFY(stream.open(value));
        QTRY_COMPARE_WITH_TIMEOUT(stream.poll().state, PinnedDownloadStreamState::Failed, 5000);
        QCOMPARE(stream.poll().error, PinnedDownloadStreamError::Tls);
        QVERIFY(received.isEmpty());
        QVERIFY(stream.read(65536).isEmpty());
        QVERIFY(!stream.poll().authenticated_eof);
    }
};

QTEST_GUILESS_MAIN(PinnedDownloadStreamTests)
#include "PinnedDownloadStreamTests.moc"
