#include "DirectDownloadSession.hpp"
#include "QtDownloadIo.hpp"

#include <QBuffer>
#include <QTest>
#include <functional>
#include <limits>

using namespace music_player::m0;

namespace {
class Io final : public DirectDownloadIo {
  public:
    QStringList hosts{};
    QList<PinnedDownloadEndpoint> endpoints{};
    DownloadResolution dns{DownloadResolutionState::Resolved, {QHostAddress{"93.184.215.14"}}};
    PinnedDownloadStreamUpdate update{PinnedDownloadStreamState::Receiving};
    QList<QByteArray> chunks{};
    bool resolve_ok{true};
    bool open_ok{true};
    int cancels{0};
    std::function<void()> on_read{};
    bool resolve(const QString& host) override {
        hosts.push_back(host);
        return resolve_ok;
    }
    DownloadResolution resolution() const override { return dns; }
    bool open(const PinnedDownloadEndpoint& endpoint) override {
        endpoints.push_back(endpoint);
        return open_ok;
    }
    PinnedDownloadStreamUpdate poll() const override { return update; }
    QByteArray read(qint64 maximum) override {
        if (on_read)
            on_read();
        if (chunks.isEmpty())
            return {};
        auto result = chunks.front().left(maximum);
        chunks.front().remove(0, result.size());
        if (chunks.front().isEmpty())
            chunks.removeFirst();
        return result;
    }
    void cancel() override { ++cancels; }
};

struct Harness final {
    Io io{};
    QBuffer sink{};
    DirectDownloadSession session;
    explicit Harness(DirectDownloadLimits limits = {}) : session(io, sink, limits) { sink.open(QIODevice::WriteOnly); }
    void connect() {
        QVERIFY(session.start("https://media.example.test/music.mp4?sig=%41%2f&x=1+2", 0));
        session.tick(1);
    }
};
} // namespace

class DirectDownloadSessionTests final : public QObject {
    Q_OBJECT
  private slots:
    void approvedDnsIsPinnedAndSuccessfulBytesAreOnlyATransferNotAnImport() {
        Harness h;
        h.connect();
        QCOMPARE(h.io.hosts, QStringList{"media.example.test"});
        QCOMPARE(h.io.endpoints.size(), 1);
        QCOMPARE(h.io.endpoints.front().address, QHostAddress{"93.184.215.14"});
        QCOMPARE(h.io.endpoints.front().host, QStringLiteral("media.example.test"));
        QCOMPARE(h.io.endpoints.front().request_target, QByteArray{"/music.mp4?sig=%41%2f&x=1+2"});
        QCOMPARE(h.io.endpoints.front().port, quint16{443});
        QVERIFY(h.io.endpoints.front().tls);
        h.io.chunks = {"HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n", "audio"};
        h.session.tick(2);
        QCOMPARE(h.session.snapshot().total_bytes, std::optional<quint64>{5});
        QVERIFY(h.sink.data().isEmpty());
        h.session.tick(3);
        QCOMPARE(h.sink.data(), QByteArray{"audio"});
        QCOMPARE(h.session.snapshot().state, DirectDownloadState::Completed);
        QCOMPARE(h.session.snapshot().received_bytes, quint64{5});
        QCOMPARE(h.io.cancels, 1);
        h.session.tick(4);
        QCOMPARE(h.sink.data(), QByteArray{"audio"});
    }
    void rejectedUrlOrMixedDnsNeverOpensAConnection() {
        Harness invalid;
        QVERIFY(!invalid.session.start("http://127.0.0.1/private", 0));
        QVERIFY(invalid.io.hosts.isEmpty());
        Harness mixed;
        mixed.io.dns.addresses.push_back(QHostAddress{"127.0.0.1"});
        QVERIFY(mixed.session.start("https://media.example.test/a", 0));
        mixed.session.tick(1);
        QVERIFY(mixed.io.endpoints.isEmpty());
        QCOMPARE(mixed.session.snapshot().error, DownloadFailure::Restricted);
    }
    void everyRedirectResolvesAgainAndDoesNotWriteItsBody() {
        Harness h;
        h.connect();
        h.io.chunks = {"HTTP/1.1 302 Found\r\nLocation: /other.mp3?sig=%42%2f\r\nContent-Length: 5\r\n\r\nwrong"};
        h.session.tick(2);
        QCOMPARE(h.io.hosts.size(), 2);
        QVERIFY(h.sink.data().isEmpty());
        h.io.dns.addresses = {QHostAddress{"10.0.0.1"}};
        h.session.tick(3);
        QCOMPARE(h.io.endpoints.size(), 1);
        QCOMPARE(h.session.snapshot().error, DownloadFailure::Restricted);
    }
    void redirectDowngradeAndRedirectLimitFailClosed() {
        Harness h;
        h.connect();
        h.io.chunks = {"HTTP/1.1 302 Found\r\nLocation: http://media.example.test/a\r\n\r\n"};
        h.session.tick(2);
        QCOMPARE(h.session.snapshot().error, DownloadFailure::Restricted);
        Harness capped({.max_redirects = 0});
        capped.connect();
        capped.io.chunks = {"HTTP/1.1 302 Found\r\nLocation: /a\r\n\r\n"};
        capped.session.tick(2);
        QCOMPARE(capped.session.snapshot().error, DownloadFailure::LimitExceeded);
    }
    void remoteFailuresAndMalformedResponsesNeverBecomeMedia() {
        for (const auto& [status, error] :
             QList<QPair<int, DownloadFailure>>{{401, DownloadFailure::Restricted},
                                                {403, DownloadFailure::Restricted},
                                                {404, DownloadFailure::Expired},
                                                {410, DownloadFailure::Expired},
                                                {429, DownloadFailure::Network},
                                                {500, DownloadFailure::Network},
                                                {206, DownloadFailure::UnsupportedMedia}}) {
            Harness h;
            h.connect();
            h.io.chunks = {"HTTP/1.1 " + QByteArray::number(status) + " Failed\r\n\r\nnot media"};
            h.session.tick(2);
            QCOMPARE(h.session.snapshot().error, error);
            QVERIFY(h.sink.data().isEmpty());
        }
        Harness malformed;
        malformed.connect();
        malformed.io.chunks = {"HTTP/1.1 200 OK\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\na"};
        malformed.session.tick(2);
        QCOMPARE(malformed.session.snapshot().state, DirectDownloadState::Failed);
        QVERIFY(malformed.sink.data().isEmpty());
    }
    void wireAndBodyCapsRejectWithoutPublishingPartials() {
        Harness body({.max_body_bytes = 3});
        body.connect();
        body.io.chunks = {"HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\naaaa"};
        body.session.tick(2);
        QCOMPARE(body.session.snapshot().state, DirectDownloadState::Failed);
        QCOMPARE(body.session.snapshot().error, DownloadFailure::LimitExceeded);
        QVERIFY(body.sink.data().isEmpty());
        Harness wire({.max_wire_bytes = 4});
        wire.connect();
        wire.io.chunks = {"HTTP/1.1 200 OK\r\n\r\naaaa"};
        wire.session.tick(2);
        QCOMPARE(wire.session.snapshot().error, DownloadFailure::LimitExceeded);
        QVERIFY(wire.sink.data().isEmpty());
        for (const auto& response :
             QList<QByteArray>{"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\naaaa\r\n0\r\n\r\n",
                               "HTTP/1.1 200 OK\r\n\r\naaaa"}) {
            Harness bounded({.max_body_bytes = 3});
            bounded.connect();
            bounded.io.chunks = {response};
            bounded.session.tick(2);
            QCOMPARE(bounded.session.snapshot().error, DownloadFailure::LimitExceeded);
        }
    }
    void cancelAndTimeoutAreTerminalAndLateInputIsInert() {
        Harness h;
        h.connect();
        h.session.cancel();
        QCOMPARE(h.session.snapshot().state, DirectDownloadState::Cancelled);
        h.io.chunks = {"HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\na"};
        h.session.tick(50);
        h.session.cancel();
        QVERIFY(h.sink.data().isEmpty());
        QCOMPARE(h.io.cancels, 1);
        Harness timeout({.total_timeout_ms = 5});
        timeout.connect();
        timeout.session.tick(5);
        QCOMPARE(timeout.session.snapshot().error, DownloadFailure::Timeout);
        Harness dns({.dns_timeout_ms = 5});
        dns.io.dns.state = DownloadResolutionState::Pending;
        QVERIFY(dns.session.start("https://media.example.test/a", 0));
        dns.session.tick(5);
        QCOMPARE(dns.session.snapshot().error, DownloadFailure::Timeout);
    }
    void clockRegressionAndSignedBoundariesDoNotRenewDeadlines() {
        Harness h;
        h.connect();
        h.session.tick(-1);
        QCOMPARE(h.session.snapshot().error, DownloadFailure::InvalidResult);
        Harness boundary({.total_timeout_ms = 5});
        boundary.io.dns.state = DownloadResolutionState::Pending;
        QVERIFY(boundary.session.start("https://media.example.test/a", std::numeric_limits<qint64>::min()));
        boundary.session.tick(std::numeric_limits<qint64>::max());
        QCOMPARE(boundary.session.snapshot().error, DownloadFailure::Timeout);
    }
    void sinkMustBeFreshAndWriteFailureCannotComplete() {
        Harness existing;
        existing.sink.write("keep");
        QVERIFY(!existing.session.start("https://media.example.test/a", 0));
        QCOMPARE(existing.sink.data(), QByteArray{"keep"});
        Harness closed;
        closed.connect();
        closed.sink.close();
        closed.io.chunks = {"HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\na"};
        closed.session.tick(2);
        QCOMPARE(closed.session.snapshot().error, DownloadFailure::Publication);
    }
    void socketFailureDoesNotTreatCloseDelimitedPartialAsComplete() {
        Harness h;
        h.connect();
        h.io.chunks = {"HTTP/1.1 200 OK\r\n\r\npartial"};
        h.session.tick(2);
        h.io.update.state = PinnedDownloadStreamState::Failed;
        h.session.tick(3);
        QCOMPARE(h.session.snapshot().error, DownloadFailure::Network);
        QCOMPARE(h.session.snapshot().state, DirectDownloadState::Failed);
    }
    void chunkedAndOrderlyCloseBodiesCompleteOnlyWhenFramingProvesEnd() {
        Harness chunked;
        chunked.connect();
        chunked.io.chunks = {"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nab\r\n0\r\n\r\n"};
        chunked.session.tick(2);
        QCOMPARE(chunked.session.snapshot().state, DirectDownloadState::Completed);
        QCOMPARE(chunked.sink.data(), QByteArray{"ab"});
        for (const bool authenticated : {false, true}) {
            Harness close;
            close.connect();
            close.io.chunks = {"HTTP/1.1 200 OK\r\n\r\nabc"};
            close.session.tick(2);
            QCOMPARE(close.session.snapshot().state, DirectDownloadState::Receiving);
            close.io.update.state = PinnedDownloadStreamState::Finished;
            close.io.update.authenticated_eof = authenticated;
            close.session.tick(3);
            QCOMPARE(close.session.snapshot().state,
                     authenticated ? DirectDownloadState::Completed : DirectDownloadState::Failed);
        }
    }
    void redirectKeepsSignedBytesAndDeadlineAndCanCompleteOnTheNewHost() {
        Harness h;
        h.connect();
        h.io.chunks = {"HTTP/1.1 307 Redirect\r\nLocation: https://cdn.example.test/new?sig=%7e%2f\r\n\r\n"};
        h.session.tick(2);
        h.io.dns.addresses = {QHostAddress{"1.1.1.1"}};
        h.session.tick(3);
        QCOMPARE(h.io.endpoints.size(), 2);
        QCOMPARE(h.io.endpoints.back().host, QStringLiteral("cdn.example.test"));
        QCOMPARE(h.io.endpoints.back().request_target, QByteArray{"/new?sig=%7e%2f"});
        h.io.chunks = {"HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nx"};
        h.session.tick(4);
        QCOMPARE(h.session.snapshot().state, DirectDownloadState::Completed);
        QCOMPARE(h.sink.data(), QByteArray{"x"});
    }
    void idleTimeoutAndTransportRefusalsStayActionable() {
        Harness idle({.idle_timeout_ms = 3});
        idle.connect();
        idle.session.tick(4);
        QCOMPARE(idle.session.snapshot().error, DownloadFailure::Timeout);
        Harness dns;
        dns.io.resolve_ok = false;
        QVERIFY(!dns.session.start("https://media.example.test/a", 0));
        QCOMPARE(dns.session.snapshot().error, DownloadFailure::Network);
        Harness socket;
        socket.io.open_ok = false;
        QVERIFY(socket.session.start("https://media.example.test/a", 0));
        socket.session.tick(1);
        QCOMPARE(socket.session.snapshot().error, DownloadFailure::Network);
    }
    void reentrantCancelDuringReadCannotPublishACompletedTransfer() {
        Harness h;
        h.connect();
        h.io.chunks = {"HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\na"};
        h.io.on_read = [&] { h.session.cancel(); };
        h.session.tick(2);
        QCOMPARE(h.session.snapshot().state, DirectDownloadState::Cancelled);
        QVERIFY(h.sink.data().isEmpty());
    }
    void reentrantCancelDuringWriteCannotBeOverwrittenByCompletion() {
        class Sink final : public QBuffer {
          public:
            std::function<void()> on_write;
            qint64 writeData(const char* bytes, qint64 size) override {
                on_write();
                return QBuffer::writeData(bytes, size);
            }
        } sink;
        sink.open(QIODevice::WriteOnly);
        Io io;
        DirectDownloadSession session(io, sink);
        sink.on_write = [&] { session.cancel(); };
        QVERIFY(session.start("https://media.example.test/a", 0));
        session.tick(1);
        io.chunks = {"HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\na"};
        session.tick(2);
        QCOMPARE(session.snapshot().state, DirectDownloadState::Cancelled);
    }
    void concreteIoDoesNotResolveOrConnectOnConstructionAndCancelIsSafe() {
        QtDownloadIo io;
        QCOMPARE(io.poll().state, PinnedDownloadStreamState::Idle);
        io.cancel();
        QCOMPARE(io.poll().state, PinnedDownloadStreamState::Cancelled);
        QVERIFY(!io.resolve("host\r\nInjected"));
    }
};

QTEST_GUILESS_MAIN(DirectDownloadSessionTests)
#include "DirectDownloadSessionTests.moc"
