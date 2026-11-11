#include "adapters/DownloadWorkerProtocol.hpp"
#include "adapters/WindowsDownloadNetworkLaunch.hpp"
#include "adapters/WindowsProcessDownloadIo.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QNetworkProxy>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

using namespace music_player::m0;
namespace {
QByteArray fileHash(const QString& path) {
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return file.open(QIODevice::ReadOnly) && hash.addData(&file) ? hash.result().toHex() : QByteArray{};
}
WindowsDownloadNetworkLaunchConfig synthetic(const QTemporaryDir& parent) {
    const auto source = parent.filePath("source.bin");
    QFile file(source);
    if (!file.open(QIODevice::WriteOnly) || file.write("synthetic-private-artifact") != 26)
        return {};
    file.close();
    WindowsDownloadNetworkLaunchConfig config;
    config.runtime_parent = parent.path();
    for (auto& artifact : config.artifacts)
        artifact = {source, fileHash(source)};
    return config;
}
WindowsDownloadNetworkLaunchConfig realConfiguration(const QTemporaryDir& parent) {
    WindowsDownloadNetworkLaunchConfig config;
    config.runtime_parent = parent.path();
    const QString crt = QString::fromUtf8(RC_MUSIC_TEST_CRT_DIR);
    const std::array<QString, download_launch_artifact_count> paths{QString::fromUtf8(RC_MUSIC_TEST_BOOTSTRAP_PATH),
                                                                    QString::fromUtf8(RC_MUSIC_TEST_PAYLOAD_PATH),
                                                                    QString::fromUtf8(RC_MUSIC_TEST_TLS_CORE_PATH),
                                                                    QString::fromUtf8(RC_MUSIC_TEST_TLS_NETWORK_PATH),
                                                                    crt + "/msvcp140.dll",
                                                                    crt + "/msvcp140_1.dll",
                                                                    crt + "/vcruntime140.dll",
                                                                    crt + "/vcruntime140_1.dll",
                                                                    QString::fromUtf8(RC_MUSIC_TEST_TLS_PLUGIN_PATH)};
    for (std::size_t index = 0; index < paths.size(); ++index)
        config.artifacts[index] = {paths[index], fileHash(paths[index])};
    return config;
}
bool realEnabled() {
    return !QString::fromUtf8(RC_MUSIC_TEST_TLS_PLUGIN_PATH).isEmpty() &&
           !QString::fromUtf8(RC_MUSIC_TEST_CRT_DIR).isEmpty();
}
} // namespace
class WindowsDownloadNetworkLaunchTests final : public QObject {
    Q_OBJECT
  private slots:
    void rejectsMissingTrust() {
        WindowsDownloadLaunchError error{};
        QVERIFY(!WindowsDownloadNetworkLaunch::prepare({}, &error));
        QCOMPARE(error, WindowsDownloadLaunchError::InvalidConfiguration);
    }
    void rejectsParentWithoutLaunchPathHeadroom() {
        QTemporaryDir parent;
        QVERIFY(parent.isValid());
        auto config = synthetic(parent);
        config.runtime_parent = QStringLiteral("C:/") + QString(8180, QLatin1Char('a'));
        WindowsDownloadLaunchError error{};
        QVERIFY(!WindowsDownloadNetworkLaunch::prepare(config, &error));
        QCOMPARE(error, WindowsDownloadLaunchError::InvalidConfiguration);
    }
    void preparesNewAuthenticatedPrivateBundle() {
        QTemporaryDir parent;
        QVERIFY(parent.isValid());
        const auto config = synthetic(parent);
        WindowsDownloadLaunchError error{};
        auto lease = WindowsDownloadNetworkLaunch::prepare(config, &error);
        QVERIFY(lease);
        QCOMPARE(error, WindowsDownloadLaunchError::None);
        QVERIFY(lease->unchanged());
        QVERIFY(lease->directory().startsWith(parent.path() + '/'));
        const auto process = lease->processConfig();
        QVERIFY(process.executable.endsWith("/RCMusicDownloadWorker.exe"));
        QCOMPARE(process.working_directory, lease->directory());
        QVERIFY(process.environment);
        QCOMPARE(process.environment->value("PATH"), QString{});
        QVERIFY(!process.environment->value("SystemRoot").isEmpty());
        QCOMPARE(process.environment->keys().size(), 2);
        const auto runtime = lease->tlsConfig();
        QCOMPARE(fileHash(runtime.core_path), config.artifacts[2].sha256);
        QVERIFY(runtime.plugin_path.endsWith("/qrcmusicschanneloffline.dll"));
        const auto directory = lease->directory();
        lease.reset();
        QVERIFY(!QDir(directory).exists());
        QVERIFY(QFile::exists(config.artifacts[0].path));
    }
    void refusesHashMismatchBeforeCreatingDirectory_data() {
        QTest::addColumn<int>("role");
        for (int index = 0; index < static_cast<int>(download_launch_artifact_count); ++index)
            QTest::newRow(qPrintable(QString::number(index))) << index;
    }
    void refusesHashMismatchBeforeCreatingDirectory() {
        QFETCH(int, role);
        QTemporaryDir parent;
        QVERIFY(parent.isValid());
        auto config = synthetic(parent);
        config.artifacts[static_cast<std::size_t>(role)].sha256 = QByteArray(64, '0');
        const auto before = QDir(parent.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
        WindowsDownloadLaunchError error{};
        QVERIFY(!WindowsDownloadNetworkLaunch::prepare(config, &error));
        QCOMPARE(error, WindowsDownloadLaunchError::HashMismatch);
        QCOMPARE(QDir(parent.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot), before);
    }
    void activeStreamRetainsPinsThroughCancelAcknowledgement() {
        if (!realEnabled())
            QSKIP("Reviewed synthetic-only runtime/CRT not configured");
        QTemporaryDir parent;
        QVERIFY(parent.isValid());
        auto lease = WindowsDownloadNetworkLaunch::prepare(realConfiguration(parent));
        QVERIFY(lease);
        const auto directory = lease->directory();
        const auto payload = directory + "/rcmusic-download-network.dll";
        WindowsProcessDownloadIoConfig config;
        config.prepared_launch = lease;
        auto io = std::make_unique<WindowsProcessDownloadIo>(config);
        lease.reset();
        config.prepared_launch.reset();
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QVERIFY(io->open(
            {QHostAddress{QHostAddress::LocalHost}, "media.example.test", "/synthetic", server.serverPort(), false}));
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                     static_cast<void>(io->poll());
                                     return server.hasPendingConnections();
                                 }()),
                                 10000);
        std::unique_ptr<QTcpSocket> peer(server.nextPendingConnection());
        QVERIFY(peer);
        QByteArray request;
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                     static_cast<void>(io->poll());
                                     request += peer->readAll();
                                     return request.contains("\r\n\r\n");
                                 }()),
                                 10000);
        QVERIFY(request.startsWith("GET /synthetic HTTP/1.1\r\n"));
        QFile writer(payload);
        QVERIFY(!writer.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(!QFile::rename(payload, payload + ".replaced"));
        QVERIFY(!QFile::remove(payload));
        io->cancel();
        // A cancellation request is not an ACK. Pins remain even if the child
        // happens to terminate immediately; only drained teardown releases them.
        QVERIFY(!writer.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QTRY_VERIFY_WITH_TIMEOUT(io->pollStop().stopped, 10000);
        QVERIFY(QDir(directory).exists());
        io.reset();
        QVERIFY(!QDir(directory).exists());
    }
    void realBootstrapRejectsUntrustedTlsBeforeHttp() {
        if (!realEnabled())
            QSKIP("Reviewed synthetic-only runtime/CRT not configured");

        QFile certificate_file(QFINDTESTDATA("network-fixtures/server-cert.pem"));
        QFile key_file(QFINDTESTDATA("network-fixtures/server-key.pem"));
        QVERIFY(certificate_file.open(QIODevice::ReadOnly));
        QVERIFY(key_file.open(QIODevice::ReadOnly));
        const QSslCertificate certificate(certificate_file.readAll());
        const QSslKey key(key_file.readAll(), QSsl::Rsa);
        QVERIFY(!certificate.isNull());
        QVERIFY(!key.isNull());
        QVERIFY(QSslSocket::supportsSsl());
        QVERIFY(!QSslConfiguration::systemCaCertificates().contains(certificate));

        auto tls = QSslConfiguration::defaultConfiguration();
        tls.setLocalCertificate(certificate);
        tls.setPrivateKey(key);
        tls.setProtocol(QSsl::TlsV1_2);
        tls.setPeerVerifyMode(QSslSocket::VerifyNone); // The fixture server requests no client identity.
        int handshakes = 0;
        QByteArray received;
        QSslServer server;
        server.setProxy(QNetworkProxy::NoProxy);
        server.setSslConfiguration(tls);
        connect(&server, &QSslServer::startedEncryptionHandshake, &server, [&](QSslSocket* socket) {
            ++handshakes;
            connect(socket, &QSslSocket::readyRead, &server, [&, socket] { received += socket->readAll(); });
        });
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));

        QTemporaryDir parent;
        QVERIFY(parent.isValid());
        auto lease = WindowsDownloadNetworkLaunch::prepare(realConfiguration(parent));
        QVERIFY(lease);
        QVERIFY(lease->unchanged());
        const auto directory = lease->directory();
        {
            DownloadWorkerRequest request;
            request.mode = DownloadWorkerMode::Stream;
            request.runtime = lease->tlsConfig();
            // The logical name matches the fixture SAN. The worker uses its unmodified default trust.
            request.endpoint = {QHostAddress::LocalHost, QStringLiteral("media.rc-music.invalid"),
                                QByteArrayLiteral("/synthetic"), server.serverPort(), true};
            auto config = lease->processConfig();
            config.standard_input = encodeDownloadWorkerRequest(request);
            config.total_timeout_ms = 10000;
            QVERIFY(!config.standard_input.isEmpty());
            WindowsDownloadProcess child;
            QElapsedTimer clock;
            clock.start();
            QVERIFY(child.start(config, clock.elapsed()));
            WindowsDownloadProcessUpdate update;
            QByteArray wire, diagnostics;
            QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                         update = child.poll(clock.elapsed());
                                         wire += update.stdout_chunk;
                                         diagnostics += update.stderr_chunk;
                                         return update.state == WindowsDownloadProcessState::Exited ||
                                                update.state == WindowsDownloadProcessState::Failed;
                                     }()),
                                     10000);
            QCOMPARE(update.state, WindowsDownloadProcessState::Exited);
            QCOMPARE(update.exit_code, std::optional<int>{17});
            QCOMPARE(handshakes, 1);
            QVERIFY(received.isEmpty());
            QVERIFY(diagnostics.isEmpty());
            const DownloadWorkerFrame terminal{.error = DownloadWorkerError::Tls};
            QCOMPARE(wire, encodeDownloadWorkerFrame(terminal));
            QVERIFY(lease->unchanged());
        }
        lease.reset();
        QVERIFY(!QDir(directory).exists());
    }
    void realBootstrapResolvesUsingOwnedCapability() {
        if (!realEnabled())
            QSKIP("Reviewed synthetic-only runtime/CRT not configured");
        QTemporaryDir parent;
        QVERIFY(parent.isValid());
        auto lease = WindowsDownloadNetworkLaunch::prepare(realConfiguration(parent));
        QVERIFY(lease);
        const auto directory = lease->directory();
        WindowsProcessDownloadIoConfig config;
        config.prepared_launch = lease;
        config.process.arguments = {"must-not-be-used"};
        auto io = std::make_unique<WindowsProcessDownloadIo>(config);
        config.prepared_launch.reset();
        lease.reset();
        QVERIFY(io->resolve("127.0.0.1"));
        QTRY_COMPARE_WITH_TIMEOUT(io->resolution().state, DownloadResolutionState::Resolved, 10000);
        QCOMPARE(io->resolution().addresses, QList<QHostAddress>{QHostAddress("127.0.0.1")});
        QVERIFY(io->pollStop().stopped);
        QCOMPARE(io->failure(), DownloadFailure::None);
        QVERIFY(QDir(directory).exists());
        io.reset();
        QVERIFY(!QDir(directory).exists());
    }
    void refusesExtraLoaderControlAndPreservesUnknownFile_data() {
        QTest::addColumn<QString>("leaf");
        QTest::newRow("qt6-config") << QString("qt6.conf");
        QTest::newRow("qt-config") << QString("qt.conf");
        QTest::newRow("dll-redirection") << QString("RCMusicDownloadWorker.exe.local");
        QTest::newRow("external-manifest") << QString("RCMusicDownloadWorker.exe.manifest");
    }
    void refusesExtraLoaderControlAndPreservesUnknownFile() {
        if (!realEnabled())
            QSKIP("Reviewed synthetic-only runtime/CRT not configured");
        QFETCH(QString, leaf);
        QTemporaryDir parent;
        QVERIFY(parent.isValid());
        auto lease = WindowsDownloadNetworkLaunch::prepare(realConfiguration(parent));
        QVERIFY(lease);
        const auto directory = lease->directory();
        QFile extra(directory + '/' + leaf);
        QVERIFY(extra.open(QIODevice::WriteOnly));
        QCOMPARE(extra.write("untrusted-loader-control"), 24);
        extra.close();
        QVERIFY(!lease->unchanged());
        WindowsProcessDownloadIoConfig config;
        config.process = lease->processConfig();
        config.runtime = lease->tlsConfig();
        config.prepared_launch = lease;
        {
            WindowsProcessDownloadIo io(config);
            QVERIFY(!io.resolve("127.0.0.1"));
            QCOMPARE(io.failure(), DownloadFailure::Unavailable);
            QVERIFY(io.pollStop().stopped);
        }
        config.prepared_launch.reset();
        lease.reset();
        QVERIFY(QFile::exists(extra.fileName()));
        QCOMPARE(QDir(directory).entryList(QDir::Files), QStringList{leaf});
        // The test owns this regular fixture, not SDK content or a junction.
        QVERIFY(QFile::remove(extra.fileName()));
        QVERIFY(QDir().rmdir(directory));
    }
    void malformedPinnedPayloadFailsBeforeQtProtocol() {
        if (!realEnabled())
            QSKIP("Reviewed synthetic-only runtime/CRT not configured");
        QTemporaryDir parent;
        QVERIFY(parent.isValid());
        const auto fake = synthetic(parent).artifacts[0];
        auto config = realConfiguration(parent);
        config.artifacts[1] = fake; // Test-only reviewed bytes, never a page pin.
        auto lease = WindowsDownloadNetworkLaunch::prepare(config);
        QVERIFY(lease);
        const auto directory = lease->directory();
        {
            WindowsDownloadProcess child;
            QVERIFY(child.start(lease->processConfig(), 0));
            WindowsDownloadProcessUpdate update;
            QByteArray wire, diagnostics;
            QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                         update = child.poll(0);
                                         wire += update.stdout_chunk;
                                         diagnostics += update.stderr_chunk;
                                         return update.state == WindowsDownloadProcessState::Exited;
                                     }()),
                                     10000);
            QCOMPARE(update.exit_code, std::optional<int>{17});
            QVERIFY(wire.isEmpty());
            QVERIFY(diagnostics.isEmpty());
            QVERIFY(lease->unchanged());
        }
        lease.reset();
        QVERIFY(!QDir(directory).exists());
    }
};
QTEST_GUILESS_MAIN(WindowsDownloadNetworkLaunchTests)
#include "WindowsDownloadNetworkLaunchTests.moc"
