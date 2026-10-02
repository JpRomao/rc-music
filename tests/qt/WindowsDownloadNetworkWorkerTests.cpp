#include "DownloadWorkerProtocol.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include <utility>

using namespace music_player::m0;

namespace {
WindowsDownloadTlsRuntimeConfig runtimeConfiguration() {
    const auto plugin = QString::fromUtf8(RC_MUSIC_TEST_TLS_PLUGIN_PATH);
    return {QString::fromUtf8(RC_MUSIC_TEST_TLS_CORE_PATH),
            QByteArray(RC_MUSIC_TEST_TLS_CORE_SHA256),
            QString::fromUtf8(RC_MUSIC_TEST_TLS_NETWORK_PATH),
            QByteArray(RC_MUSIC_TEST_TLS_NETWORK_SHA256),
            plugin.isEmpty() ? QDir::tempPath() + QStringLiteral("/missing-reviewed-plugin.dll") : plugin,
            plugin.isEmpty() ? QByteArray(64, '0') : QByteArray(RC_MUSIC_TEST_TLS_PLUGIN_SHA256)};
}

bool realPluginEnabled() { return !QString::fromUtf8(RC_MUSIC_TEST_TLS_PLUGIN_PATH).isEmpty(); }

DownloadWorkerRequest resolveRequest(const QString& host = QStringLiteral("127.0.0.1")) {
    DownloadWorkerRequest request;
    request.runtime = runtimeConfiguration();
    request.host = host;
    return request;
}

DownloadWorkerRequest streamRequest(quint16 port) {
    auto request = resolveRequest();
    request.mode = DownloadWorkerMode::Stream;
    request.host.clear();
    request.endpoint = {QHostAddress{QHostAddress::LocalHost}, QStringLiteral("media.example.test"),
                        QByteArray("/signed/%41%2fb?sig=%2f%2F&x=1+2&literal=^|`{}"), port, false};
    return request;
}

struct Result final {
    bool completed{false};
    int exit_code{-1};
    QProcess::ExitStatus exit_status{QProcess::CrashExit};
    QByteArray wire{};
    QByteArray diagnostics{};
};

struct ChildGuard final {
    QProcess& process;
    ~ChildGuard() {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            static_cast<void>(process.waitForFinished(5000));
        }
    }
};

Result run(const QByteArray& input, const QStringList& arguments = {}) {
    // Only a task-owned TEMP is removed; no artifacts or SDK directory are copied
    // into it, linked from it, or passed to recursive cleanup.
    QTemporaryDir directory{QDir::tempPath() + QStringLiteral("/RC Music network 日本-XXXXXX")};
    if (!directory.isValid())
        return {};
    QProcess child;
    ChildGuard guard{child};
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove(QStringLiteral("QT_PLUGIN_PATH"));
    environment.remove(QStringLiteral("QT_SSL_BACKEND"));
    environment.insert(QStringLiteral("QT_LOGGING_TO_CONSOLE"), QStringLiteral("1"));
    environment.insert(QStringLiteral("QT_FORCE_STDERR_LOGGING"), QStringLiteral("1"));
    child.setProcessEnvironment(environment);
    child.setProgram(QString::fromUtf8(RC_MUSIC_TEST_NETWORK_WORKER_PATH));
    child.setArguments(arguments);
    child.setWorkingDirectory(directory.path());
    child.start();
    if (!child.waitForStarted(5000) || child.write(input) != input.size())
        return {};
    child.closeWriteChannel();
    Result result;
    QElapsedTimer timeout;
    timeout.start();
    do {
        QCoreApplication::processEvents();
        result.wire += child.readAllStandardOutput();
        result.diagnostics += child.readAllStandardError();
        if (result.wire.size() > 1024 * 1024 || result.diagnostics.size() > 4096)
            return {};
        if (child.state() == QProcess::NotRunning) {
            result.completed = true;
            result.exit_code = child.exitCode();
            result.exit_status = child.exitStatus();
            return result;
        }
        QTest::qWait(5);
    } while (timeout.elapsed() < 10000);
    return {};
}

std::optional<QList<DownloadWorkerFrame>> frames(const QByteArray& wire) {
    DownloadWorkerFrameDecoder decoder;
    QList<DownloadWorkerFrame> result;
    qsizetype offset = 0;
    while (offset < wire.size()) {
        const auto consumed = decoder.append(QByteArrayView{wire}.sliced(offset));
        if (consumed <= 0 || consumed > wire.size() - offset || decoder.failed())
            return std::nullopt;
        offset += consumed;
        if (decoder.ready()) {
            auto frame = decoder.take();
            if (!frame || result.size() >= 64)
                return std::nullopt;
            result.push_back(std::move(*frame));
        }
    }
    return decoder.failed() || decoder.incomplete() ? std::nullopt
                                                    : std::optional<QList<DownloadWorkerFrame>>{std::move(result)};
}

void expectFailure(const Result& result, DownloadWorkerError error) {
    QVERIFY(result.completed);
    QCOMPARE(result.exit_status, QProcess::NormalExit);
    QVERIFY(result.exit_code != 0);
    QVERIFY(result.diagnostics.isEmpty());
    const auto decoded = frames(result.wire);
    QVERIFY(decoded);
    QCOMPARE(decoded->size(), 1);
    const auto& frame = decoded->front();
    QCOMPARE(frame.kind, DownloadWorkerFrameKind::Terminal);
    QCOMPARE(frame.error, error);
    QVERIFY(!frame.authenticated_eof);
    QVERIFY(frame.addresses.isEmpty());
    QVERIFY(frame.data.isEmpty());
}

struct Loopback final {
    QTcpServer server{};
    QByteArray request{};
    QByteArray response{};
    int connections{0};
    bool replied{false};
    Loopback() {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (server.hasPendingConnections()) {
                auto* socket = server.nextPendingConnection();
                ++connections;
                QObject::connect(socket, &QTcpSocket::readyRead, &server, [this, socket] {
                    request += socket->readAll();
                    if (!replied && request.contains("\r\n\r\n")) {
                        replied = true;
                        static_cast<void>(socket->write(response));
                        socket->disconnectFromHost();
                    }
                });
            }
        });
    }
    bool listen() { return server.listen(QHostAddress::LocalHost, 0); }
};
} // namespace

class WindowsDownloadNetworkWorkerTests final : public QObject {
    Q_OBJECT
  private slots:
    void rejectsMalformedPrivateRequestsBeforeAnyConnection_data() {
        QTest::addColumn<QByteArray>("input");
        QTest::newRow("empty") << QByteArray{};
        QTest::newRow("garbage") << QByteArrayLiteral("PRIVATE_REQUEST_not_a_protocol");
        QTest::newRow("over ceiling") << QByteArray(download_worker_max_request_bytes + 1, 'X');
    }
    void rejectsMalformedPrivateRequestsBeforeAnyConnection() {
        QFETCH(QByteArray, input);
        Loopback local;
        QVERIFY(local.listen());
        expectFailure(run(input), DownloadWorkerError::InvalidRequest);
        QCOMPARE(local.connections, 0);
        QVERIFY(local.request.isEmpty());
    }
    void rejectsTruncatedOrTrailingBytesWithoutConnecting_data() {
        QTest::addColumn<bool>("trailing");
        QTest::newRow("truncated") << false;
        QTest::newRow("trailing") << true;
    }
    void rejectsTruncatedOrTrailingBytesWithoutConnecting() {
        QFETCH(bool, trailing);
        Loopback local;
        QVERIFY(local.listen());
        auto bytes = encodeDownloadWorkerRequest(streamRequest(local.server.serverPort()));
        QVERIFY(!bytes.isEmpty());
        if (trailing)
            bytes += QByteArrayLiteral("PRIVATE_TRAILING_SECRET");
        else
            bytes.chop(1);
        expectFailure(run(bytes), DownloadWorkerError::InvalidRequest);
        QCOMPARE(local.connections, 0);
        QVERIFY(local.request.isEmpty());
    }
    void refusesMissingOrIncorrectPinsBeforeHttp_data() {
        QTest::addColumn<int>("pin");
        for (int pin = 0; pin < 6; ++pin)
            QTest::newRow(qPrintable(QString::number(pin))) << pin;
    }
    void refusesMissingOrIncorrectPinsBeforeHttp() {
        QFETCH(int, pin);
        Loopback local;
        QVERIFY(local.listen());
        QTemporaryDir missing;
        QVERIFY(missing.isValid());
        auto request = streamRequest(local.server.serverPort());
        if (pin == 0)
            request.runtime.core_path = missing.filePath("missing-Qt6Core.dll");
        else if (pin == 1)
            request.runtime.network_path = missing.filePath("missing-Qt6Network.dll");
        else if (pin == 2)
            request.runtime.plugin_path = missing.filePath("missing-plugin.dll");
        else if (pin == 3)
            request.runtime.core_sha256 = QByteArray(64, '0');
        else if (pin == 4)
            request.runtime.network_sha256 = QByteArray(64, '0');
        else
            request.runtime.plugin_sha256 = QByteArray(64, '0');
        const auto bytes = encodeDownloadWorkerRequest(request);
        QVERIFY(!bytes.isEmpty());
        expectFailure(run(bytes), DownloadWorkerError::Runtime);
        QCOMPARE(local.connections, 0);
        QVERIFY(local.request.isEmpty());
    }
    void runtimeIsRequiredEvenForNumericDns() {
        auto request = resolveRequest();
        request.runtime.core_sha256 = QByteArray(64, '0');
        const auto bytes = encodeDownloadWorkerRequest(request);
        QVERIFY(!bytes.isEmpty());
        expectFailure(run(bytes), DownloadWorkerError::Runtime);
    }
    void refusesArgumentsInsteadOfTreatingThemAsMediaInput() {
        Loopback local;
        QVERIFY(local.listen());
        const auto bytes = encodeDownloadWorkerRequest(streamRequest(local.server.serverPort()));
        QVERIFY(!bytes.isEmpty());
        expectFailure(run(bytes, {QStringLiteral("https://PRIVATE_CLI_SECRET.invalid/never")}),
                      DownloadWorkerError::InvalidRequest);
        QCOMPARE(local.connections, 0);
        QVERIFY(local.request.isEmpty());
    }
    void numericDnsProducesOnlyAddressesThenExplicitTerminal_data() {
        QTest::addColumn<QString>("host");
        QTest::newRow("IPv4 no reverse lookup") << QStringLiteral("127.0.0.1");
        QTest::newRow("IPv6 no reverse lookup") << QStringLiteral("::1");
    }
    void numericDnsProducesOnlyAddressesThenExplicitTerminal() {
        QFETCH(QString, host);
        if (!realPluginEnabled())
            QSKIP("Restricted TLS plugin fixture is opt-in.");
        const auto bytes = encodeDownloadWorkerRequest(resolveRequest(host));
        QVERIFY(!bytes.isEmpty());
        const auto result = run(bytes);
        QVERIFY(result.completed);
        QCOMPARE(result.exit_status, QProcess::NormalExit);
        QCOMPARE(result.exit_code, 0);
        QVERIFY(result.diagnostics.isEmpty());
        const auto decoded = frames(result.wire);
        QVERIFY(decoded);
        QCOMPARE(decoded->size(), 2);
        QCOMPARE(decoded->front().kind, DownloadWorkerFrameKind::Addresses);
        QCOMPARE(decoded->front().addresses, QList<QHostAddress>{QHostAddress{host}});
        QVERIFY(decoded->front().data.isEmpty());
        QCOMPARE(decoded->back().kind, DownloadWorkerFrameKind::Terminal);
        QCOMPARE(decoded->back().error, DownloadWorkerError::None);
        QVERIFY(!decoded->back().authenticated_eof);
    }
    void httpStreamsExactRawBytesInBoundedFramesBeforeAuthenticatedEof() {
        if (!realPluginEnabled())
            QSKIP("Restricted TLS plugin fixture is opt-in.");
        Loopback local;
        QVERIFY(local.listen());
        const QByteArray body(131073, 'A');
        local.response = "HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) +
                         "\r\nContent-Type: application/octet-stream\r\n\r\n" + body;
        const auto request = streamRequest(local.server.serverPort());
        const auto bytes = encodeDownloadWorkerRequest(request);
        QVERIFY(!bytes.isEmpty());
        const auto result = run(bytes);
        QVERIFY(result.completed);
        QCOMPARE(result.exit_status, QProcess::NormalExit);
        QCOMPARE(result.exit_code, 0);
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(local.connections, 1);
        QCOMPARE(local.request, "GET " + request.endpoint.request_target + " HTTP/1.1\r\nHost: media.example.test:" +
                                    QByteArray::number(request.endpoint.port) +
                                    "\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n");
        const auto decoded = frames(result.wire);
        QVERIFY(decoded);
        QVERIFY(decoded->size() > 2);
        QByteArray received;
        for (qsizetype index = 0; index < decoded->size() - 1; ++index) {
            const auto& frame = decoded->at(index);
            QCOMPARE(frame.kind, DownloadWorkerFrameKind::Data);
            QVERIFY(!frame.data.isEmpty());
            QVERIFY(frame.data.size() <= download_worker_max_data_bytes);
            QVERIFY(frame.addresses.isEmpty());
            received += frame.data;
        }
        QCOMPARE(received, local.response);
        const auto& terminal = decoded->back();
        QCOMPARE(terminal.kind, DownloadWorkerFrameKind::Terminal);
        QCOMPARE(terminal.error, DownloadWorkerError::None);
        QVERIFY(terminal.authenticated_eof);
        QVERIFY(terminal.data.isEmpty());
        QVERIFY(terminal.addresses.isEmpty());
    }
    void refusedConnectionProducesTypedFailureNotSuccessfulEof() {
        if (!realPluginEnabled())
            QSKIP("Restricted TLS plugin fixture is opt-in.");
        // Keep an exclusive bound TCP socket alive without listening, so the
        // endpoint cannot be claimed by another process during worker startup.
        QTcpSocket reservation;
        QVERIFY(reservation.bind(QHostAddress::LocalHost, 0, QAbstractSocket::DontShareAddress));
        const auto port = reservation.localPort();
        const auto bytes = encodeDownloadWorkerRequest(streamRequest(port));
        QVERIFY(!bytes.isEmpty());
        expectFailure(run(bytes), DownloadWorkerError::Connection);
    }
};

QTEST_GUILESS_MAIN(WindowsDownloadNetworkWorkerTests)
#include "WindowsDownloadNetworkWorkerTests.moc"
