#include "PinnedDownloadStream.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

using namespace music_player::m0;

namespace {

QByteArray readFile(const QString& name) {
    QFile file(name);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

bool writeFixture(const QString& name, const QByteArray& bytes) {
    QFile file(name);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QSslConfiguration serverConfiguration(const QSslCertificate& certificate, const QSslKey& key) {
    auto configuration = QSslConfiguration::defaultConfiguration();
    configuration.setLocalCertificate(certificate);
    configuration.setPrivateKey(key);
    configuration.setPeerVerifyMode(QSslSocket::VerifyNone); // Synthetic server requests no client identity.
    configuration.setProtocol(QSsl::TlsV1_2OrLater);
    configuration.setAllowedNextProtocols({QByteArrayLiteral("http/1.1")});
    return configuration;
}

QSslConfiguration clientConfiguration(const QSslCertificate& root) {
    auto configuration = QSslConfiguration::defaultConfiguration();
    configuration.setCaCertificates({root});
    return configuration;
}

PinnedDownloadEndpoint endpoint(quint16 port, QString host = QStringLiteral("media.rc-music.invalid")) {
    return {QHostAddress::LocalHost, std::move(host), QByteArrayLiteral("/audio%2f.mp3?sig=%41+q"), port, true};
}

class StopProcess final {
  public:
    explicit StopProcess(QProcess& value) : process_(value) {}
    ~StopProcess() {
        if (process_.state() != QProcess::NotRunning) {
            process_.kill();
            process_.waitForFinished(3000);
        }
    }

  private:
    QProcess& process_;
};

} // namespace

class RestrictedTlsTests final : public QObject {
    Q_OBJECT

  private:
    QSslCertificate trusted_;
    QSslKey key_;
    QByteArray received_;
    bool before_authentication_{false};

    void observe(QSslServer& server) {
        received_.clear();
        before_authentication_ = false;
        connect(&server, &QSslServer::startedEncryptionHandshake, this, [this](QSslSocket* socket) {
            connect(socket, &QSslSocket::readyRead, this, [this, socket] {
                const auto bytes = socket->readAll();
                if (!bytes.isEmpty() && !socket->isEncrypted())
                    before_authentication_ = true;
                received_ += bytes;
            });
        });
    }

    bool openssl(const QString& directory, const QStringList& arguments) {
        QProcess process;
        process.setWorkingDirectory(directory);
        process.setProgram(QStringLiteral("C:/Program Files/OpenSSL-Win64/bin/openssl.exe"));
        process.setArguments(arguments);
        StopProcess cleanup(process);
        process.start();
        return process.waitForStarted(3000) && process.waitForFinished(10000) &&
               process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    }

  private slots:
    void initTestCase() {
        QCOMPARE(QSslSocket::activeBackend(),
                 qEnvironmentVariable("RC_MUSIC_TLS_TEST_BACKEND", "rcmusic-schannel-offline"));
        const auto certificate = QFINDTESTDATA("../../../tests/qt/network-fixtures/server-cert.pem");
        const auto private_key = QFINDTESTDATA("../../../tests/qt/network-fixtures/server-key.pem");
        trusted_ = QSslCertificate(readFile(certificate));
        key_ = QSslKey(readFile(private_key), QSsl::Rsa);
        QVERIFY(!trusted_.isNull());
        QVERIFY(!key_.isNull());
    }

    void authenticatesTrustedCertificateBeforeSendingExactGet() {
        const auto defaults = QSslConfiguration::defaultConfiguration();
        QSslServer server;
        server.setSslConfiguration(serverConfiguration(trusted_, key_));
        observe(server);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        PinnedDownloadStream stream;
        QVERIFY(stream.setTlsConfiguration(clientConfiguration(trusted_)));
        QVERIFY(stream.open(endpoint(server.serverPort())));
        QTRY_VERIFY_WITH_TIMEOUT(received_.contains("\r\n\r\n"), 5000);
        QCOMPARE(stream.poll().state, PinnedDownloadStreamState::Receiving);
        QVERIFY(received_.startsWith("GET /audio%2f.mp3?sig=%41+q HTTP/1.1\r\n"));
        QVERIFY(!before_authentication_);
        QCOMPARE(QSslConfiguration::defaultConfiguration(), defaults);
        stream.cancel();
    }

    void rejectsWrongNameWithoutSendingMediaGet() {
        QSslServer server;
        server.setSslConfiguration(serverConfiguration(trusted_, key_));
        observe(server);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        PinnedDownloadStream stream;
        QVERIFY(stream.setTlsConfiguration(clientConfiguration(trusted_)));
        QVERIFY(stream.open(endpoint(server.serverPort(), QStringLiteral("wrong.rc-music.invalid"))));
        QTRY_COMPARE_WITH_TIMEOUT(stream.poll().state, PinnedDownloadStreamState::Failed, 5000);
        QCOMPARE(stream.poll().error, PinnedDownloadStreamError::Tls);
        QTest::qWait(50);
        QVERIFY(received_.isEmpty());
        QVERIFY(!before_authentication_);
    }

    void incompleteAiaChainCannotFetchTrapOrSendMediaGet() {
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        QProcess trap;
        trap.setProgram(QStandardPaths::findExecutable(QStringLiteral("node")));
        trap.setArguments({QFINDTESTDATA("aia-trap.mjs"), fixtures.filePath("issuer.der")});
        StopProcess trap_cleanup(trap);
        trap.start();
        QVERIFY(trap.waitForStarted(3000));
        QVERIFY(trap.waitForReadyRead(3000));
        const auto port = QJsonDocument::fromJson(trap.readLine()).object().value("port").toInt();
        QVERIFY(port > 0 && port <= 65535);
        const auto identity = QUuid::createUuid().toString(QUuid::Id128);
        QVERIFY(openssl(fixtures.path(),
                        {"req", "-x509", "-newkey", "rsa:2048", "-nodes", "-sha256", "-keyout", "root.key", "-out",
                         "root.pem", "-days", "2", "-subj", "/CN=RC-Music-Root-" + identity, "-addext",
                         "basicConstraints=critical,CA:TRUE", "-addext", "keyUsage=critical,keyCertSign,cRLSign"}));
        QVERIFY(openssl(fixtures.path(), {"req", "-new", "-newkey", "rsa:2048", "-nodes", "-keyout", "issuer.key",
                                          "-out", "issuer.csr", "-subj", "/CN=RC-Music-Issuer-" + identity}));
        QVERIFY(writeFixture(
            fixtures.filePath("issuer.ext"),
            "basicConstraints=critical,CA:TRUE,pathlen:0\n"
            "keyUsage=critical,keyCertSign,cRLSign\nsubjectKeyIdentifier=hash\nauthorityKeyIdentifier=keyid,issuer\n"));
        QVERIFY(openssl(fixtures.path(),
                        {"x509", "-req", "-in", "issuer.csr", "-CA", "root.pem", "-CAkey", "root.key", "-set_serial",
                         "2", "-out", "issuer.pem", "-days", "2", "-sha256", "-extfile", "issuer.ext"}));
        QVERIFY(openssl(fixtures.path(), {"x509", "-in", "issuer.pem", "-outform", "DER", "-out", "issuer.der"}));
        QVERIFY(openssl(fixtures.path(), {"req", "-new", "-newkey", "rsa:2048", "-nodes", "-keyout", "leaf.key", "-out",
                                          "leaf.csr", "-subj", "/CN=media.rc-music.invalid"}));
        const auto leaf_extensions =
            QByteArrayLiteral(
                "basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\n"
                "extendedKeyUsage=serverAuth\nsubjectAltName=DNS:media.rc-music.invalid\nsubjectKeyIdentifier=hash\n"
                "authorityKeyIdentifier=keyid,issuer\nauthorityInfoAccess=caIssuers;URI:http://127.0.0.1:") +
            QByteArray::number(port) + "/issuer.der\n";
        QVERIFY(writeFixture(fixtures.filePath("leaf.ext"), leaf_extensions));
        QVERIFY(openssl(fixtures.path(),
                        {"x509", "-req", "-in", "leaf.csr", "-CA", "issuer.pem", "-CAkey", "issuer.key", "-set_serial",
                         "3", "-out", "leaf.pem", "-days", "2", "-sha256", "-extfile", "leaf.ext"}));
        QSslServer server;
        server.setSslConfiguration(serverConfiguration(QSslCertificate(readFile(fixtures.filePath("leaf.pem"))),
                                                       QSslKey(readFile(fixtures.filePath("leaf.key")), QSsl::Rsa)));
        observe(server);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        PinnedDownloadStream stream;
        QVERIFY(
            stream.setTlsConfiguration(clientConfiguration(QSslCertificate(readFile(fixtures.filePath("root.pem"))))));
        QVERIFY(stream.open(endpoint(server.serverPort())));
        QTRY_VERIFY_WITH_TIMEOUT(stream.poll().state != PinnedDownloadStreamState::Connecting, 5000);
        QTest::qWait(100);
        const auto requests = trap.readAllStandardOutput();
        QVERIFY2(!requests.contains("REQUEST"), requests.constData());
        QCOMPARE(stream.poll().state, PinnedDownloadStreamState::Failed);
        QCOMPARE(stream.poll().error, PinnedDownloadStreamError::Tls);
        QVERIFY(received_.isEmpty());
        QVERIFY(!before_authentication_);
        QCOMPARE(trap.state(), QProcess::Running);
    }
};

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto plugin_root = qEnvironmentVariable("RC_MUSIC_TLS_TEST_PLUGIN_ROOT");
    if (plugin_root.isEmpty())
        return 2;
    QCoreApplication::addLibraryPath(plugin_root);
    const auto backend = qEnvironmentVariable("RC_MUSIC_TLS_TEST_BACKEND", "rcmusic-schannel-offline");
    if (!QSslSocket::setActiveBackend(backend))
        return 3;
    RestrictedTlsTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "RestrictedTlsTests.moc"
