#include "DirectDownloadSession.hpp"
#include "QtDownloadIo.hpp"
#include "YoutubeAudioResolution.hpp"
#include "support/YoutubeResolverTestSupport.hpp"

#include <QBuffer>
#include <QTest>
#include <functional>
#include <limits>
#include <stdexcept>

using namespace music_player::m0;

namespace {
QByteArray diagnosticResponse(QByteArray wire) {
    const auto crlf = QByteArray(1, char{13}) + char{10};
    return wire.replace("|", crlf);
}
class Io final : public DirectDownloadIo {
  public:
    QStringList hosts{};
    QList<PinnedDownloadEndpoint> endpoints{};
    DownloadResolution dns{DownloadResolutionState::Resolved, {QHostAddress{"93.184.215.14"}}};
    PinnedDownloadStreamUpdate update{PinnedDownloadStreamState::Receiving};
    QList<QByteArray> chunks{};
    bool resolve_ok{true};
    bool open_ok{true};
    bool resolve_throws{false};
    bool open_throws{false};
    bool resolution_throws{false};
    bool poll_throws{false};
    bool failure_throws{false};
    bool clear_failure_on_cancel{false};
    DownloadFailure failure_value{DownloadFailure::None};
    int cancels{0};
    std::function<void()> on_read{};
    bool resolve(const QString& host) override {
        hosts.push_back(host);
        if (resolve_throws)
            throw std::runtime_error("synthetic resolve failure");
        return resolve_ok;
    }
    DownloadResolution resolution() const override {
        if (resolution_throws)
            throw std::runtime_error("synthetic resolution failure");
        return dns;
    }
    bool open(const PinnedDownloadEndpoint& endpoint) override {
        endpoints.push_back(endpoint);
        if (open_throws)
            throw std::runtime_error("synthetic open failure");
        return open_ok;
    }
    PinnedDownloadStreamUpdate poll() const override {
        if (poll_throws)
            throw std::runtime_error("synthetic poll failure");
        return update;
    }
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
    DownloadFailure failure() const override {
        if (failure_throws)
            throw std::runtime_error("synthetic failure getter exception");
        return failure_value;
    }
    void cancel() override {
        ++cancels;
        if (clear_failure_on_cancel)
            failure_value = DownloadFailure::None;
    }
    DirectDownloadIoStopUpdate pollStop() override { return {true, DownloadFailure::None}; }
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
    void restrictionObservationNamesOnlyTheClosedFailureBranch_data() {
        QTest::addColumn<QString>("cause");
        QTest::addColumn<int>("reason");
        QTest::newRow("target") << QStringLiteral("target") << int(DirectDownloadRestriction::Target);
        QTest::newRow("DNS") << QStringLiteral("DNS") << int(DirectDownloadRestriction::Dns);
        QTest::newRow("redirect") << QStringLiteral("redirect") << int(DirectDownloadRestriction::Redirect);
        QTest::newRow("HTTP401") << QStringLiteral("HTTP401") << int(DirectDownloadRestriction::Http401);
        QTest::newRow("HTTP403") << QStringLiteral("HTTP403") << int(DirectDownloadRestriction::Http403);
        QTest::newRow("IO") << QStringLiteral("IO") << int(DirectDownloadRestriction::Io);
    }
    void restrictionObservationNamesOnlyTheClosedFailureBranch() {
        QFETCH(QString, cause);
        QFETCH(int, reason);
        QList<DirectDownloadRestriction> observed;
        Harness h({.on_restricted = [&](DirectDownloadRestriction value) { observed.push_back(value); }});
        if (cause == QStringLiteral("target")) {
            QVERIFY(!h.session.start("file:///private/not-a-network-source.mp3", 0));
        } else if (cause == QStringLiteral("IO")) {
            h.io.resolve_ok = false;
            h.io.failure_value = DownloadFailure::Restricted;
            QVERIFY(!h.session.start("https://media.example.test/clip.mp4?sig=not-logged", 0));
        } else {
            if (cause == QStringLiteral("DNS"))
                h.io.dns.addresses.push_back(QHostAddress("127.0.0.1"));
            h.connect();
            if (cause == QStringLiteral("redirect"))
                h.io.chunks = {diagnosticResponse(
                    "HTTP/1.1 302 Found|Location: http://127.0.0.1/private?sig=not-logged|Content-Length: 0||")};
            else if (cause == QStringLiteral("HTTP401"))
                h.io.chunks = {diagnosticResponse("HTTP/1.1 401 Unauthorized|Content-Length: 4||DATA")};
            else if (cause == QStringLiteral("HTTP403"))
                h.io.chunks = {diagnosticResponse("HTTP/1.1 403 Forbidden|Content-Length: 4||DATA")};
            h.session.tick(2);
        }
        QCOMPARE(h.session.snapshot().state, DirectDownloadState::Failed);
        QCOMPARE(h.session.snapshot().error, DownloadFailure::Restricted);
        QVERIFY(h.sink.data().isEmpty());
        QCOMPARE(observed.size(), qsizetype{1});
        QCOMPARE(int(observed.front()), reason);
        h.session.tick(3);
        h.session.tick(4);
        QVERIFY(!h.session.start("https://media.example.test/again.mp4", 5));
        QCOMPARE(observed.size(), qsizetype{1});
        QVERIFY(h.io.endpoints.size() <= 1);
    }
    void restrictionObserverExceptionsCannotReplaceTheFailureOrWriteAudio() {
        int calls = 0;
        Harness h({.on_restricted = [&](DirectDownloadRestriction) {
            ++calls;
            throw std::runtime_error("observer failure must not escape");
        }});
        h.connect();
        h.io.chunks = {diagnosticResponse("HTTP/1.1 403 Forbidden|Content-Length: 4||DATA")};
        h.session.tick(2);
        QCOMPARE(calls, 1);
        QCOMPARE(h.session.snapshot().state, DirectDownloadState::Failed);
        QCOMPARE(h.session.snapshot().error, DownloadFailure::Restricted);
        QCOMPARE(h.io.cancels, 1);
        QVERIFY(h.sink.data().isEmpty());
    }
    void successfulAndUnrelatedFailuresDoNotNotifyTheRestrictionObserver() {
        int calls = 0;
        const DirectDownloadLimits limits{.on_restricted = [&](DirectDownloadRestriction) { ++calls; }};
        Harness success(limits);
        success.connect();
        success.io.chunks = {diagnosticResponse("HTTP/1.1 200 OK|Content-Length: 4||DATA")};
        success.session.tick(2);
        QCOMPARE(success.session.snapshot().state, DirectDownloadState::Completed);
        Harness network(limits);
        network.io.resolve_ok = false;
        QVERIFY(!network.session.start("https://media.example.test/clip.mp4", 0));
        QCOMPARE(network.session.snapshot().error, DownloadFailure::Network);
        QCOMPARE(calls, 0);
    }
    void resolvedYoutubeAudioUsesTheExistingPublicDnsPinAndHttpSink() {
        const auto capability = test::resolvedYoutubeFixture();
        QVERIFY(capability);
        Harness h;
        QVERIFY(h.session.startResolvedYoutube(capability, 0, capability->expiresAtSeconds() - 60));
        QCOMPARE(h.io.hosts.size(), 1);
        h.session.tick(1);
        QCOMPARE(h.io.endpoints.size(), 1);
        QCOMPARE(h.io.endpoints.front().address, QHostAddress("93.184.215.14"));
        QCOMPARE(h.io.endpoints.front().port, quint16{443});
        QVERIFY(h.io.endpoints.front().tls);
        const auto path = capability->streamUrl().mid(capability->streamUrl().indexOf('/', 8)).toLatin1();
        QCOMPARE(h.io.endpoints.front().request_target, path);
        h.io.chunks = {"HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nAAC!"};
        h.session.tick(2);
        QCOMPARE(h.session.snapshot().state, DirectDownloadState::Completed);
        QCOMPARE(h.sink.data(), QByteArray("AAC!"));
        QCOMPARE(h.io.cancels, 1);
        Harness main;
        QVERIFY(!main.session.start(capability->streamUrl(), 0));
        QVERIFY(main.io.hosts.isEmpty());
    }
    void youtubeCapabilityCannotAdmitAnyPrivateDnsAnswer() {
        const auto capability = test::resolvedYoutubeFixture();
        QVERIFY(capability);
        Harness h;
        h.io.dns.addresses.push_back(QHostAddress("127.0.0.1"));
        QVERIFY(h.session.startResolvedYoutube(capability, 0, capability->expiresAtSeconds() - 60));
        h.session.tick(1);
        QCOMPARE(h.session.snapshot().state, DirectDownloadState::Failed);
        QCOMPARE(h.session.snapshot().error, DownloadFailure::Restricted);
        QVERIFY(h.io.endpoints.isEmpty());
        QVERIFY(h.sink.data().isEmpty());
    }
    void youtubeRedirectsAreRevalidatedBeforeResolvingTheNextHost() {
        const auto capability = test::resolvedYoutubeFixture();
        QVERIFY(capability);
        Harness h;
        QVERIFY(h.session.startResolvedYoutube(capability, 0, capability->expiresAtSeconds() - 60));
        h.session.tick(1);
        h.io.chunks = {
            "HTTP/1.1 302 Found\r\nLocation: https://media.example.test/videoplayback\r\nContent-Length: 0\r\n\r\n"};
        h.session.tick(2);
        QCOMPARE(h.session.snapshot().error, DownloadFailure::Restricted);
        QCOMPARE(h.io.hosts.size(), 1);
        QVERIFY(h.sink.data().isEmpty());
    }
    void youtubeAdmissionAndTicksEnforceCapabilityExpiryWithoutAHiddenWallClock() {
        const auto capability = test::resolvedYoutubeFixture();
        QVERIFY(capability);
        Harness expired;
        QVERIFY(!expired.session.startResolvedYoutube(capability, 0, capability->expiresAtSeconds()));
        QCOMPARE(expired.session.snapshot().error, DownloadFailure::Expired);
        QVERIFY(expired.io.hosts.isEmpty());
        Harness live;
        QVERIFY(live.session.startResolvedYoutube(capability, 100, capability->expiresAtSeconds() - 1));
        live.session.tick(1100);
        QCOMPARE(live.session.snapshot().error, DownloadFailure::Expired);
        QCOMPARE(live.io.cancels, 1);
        QVERIFY(live.io.endpoints.isEmpty());
        Harness missing;
        QVERIFY(!missing.session.startResolvedYoutube({}, 0, 0));
        QVERIFY(missing.io.hosts.isEmpty());
    }
    void anExpiredYoutubeRedirectFailsBeforeResolvingAnotherHost() {
        const auto capability = test::resolvedYoutubeFixture();
        QVERIFY(capability);
        Harness h;
        QVERIFY(h.session.startResolvedYoutube(capability, 0, capability->expiresAtSeconds() - 60));
        h.session.tick(1);
        QCOMPARE(h.io.hosts.size(), 1);
        QCOMPARE(h.io.endpoints.size(), 1);
        h.io.chunks = {"HTTP/1.1 302 Found\r\nLocation: "
                       "https://r2.googlevideo.com/videoplayback?expire=1&sig=A\r\nContent-Length: 0\r\n\r\n"};
        h.session.tick(2);
        QCOMPARE(h.session.snapshot().state, DirectDownloadState::Failed);
        QCOMPARE(h.session.snapshot().error, DownloadFailure::Expired);
        QCOMPARE(h.io.hosts.size(), 1);
        QCOMPARE(h.io.endpoints.size(), 1);
        QVERIFY(h.sink.data().isEmpty());
        QVERIFY(h.io.cancels > 0);
    }
    void youtubeAudioKeepsTheExistingBodyQuotaAndRejectsPartialHttpResponses() {
        const auto capability = test::resolvedYoutubeFixture();
        QVERIFY(capability);
        Harness quota({.max_body_bytes = 3});
        QVERIFY(quota.session.startResolvedYoutube(capability, 0, capability->expiresAtSeconds() - 60));
        quota.session.tick(1);
        quota.io.chunks = {"HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nAAC!"};
        quota.session.tick(2);
        QCOMPARE(quota.session.snapshot().error, DownloadFailure::LimitExceeded);
        QVERIFY(quota.sink.data().isEmpty());
        Harness partial;
        QVERIFY(partial.session.startResolvedYoutube(capability, 0, capability->expiresAtSeconds() - 60));
        partial.session.tick(1);
        partial.io.chunks = {"HTTP/1.1 206 Partial Content\r\nContent-Length: 4\r\n\r\nAAC!"};
        partial.session.tick(2);
        QCOMPARE(partial.session.snapshot().error, DownloadFailure::UnsupportedMedia);
        QVERIFY(partial.sink.data().isEmpty());
    }
    void typedIoFailuresAreCapturedBeforeCancelCanEraseThem_data() {
        QTest::addColumn<QString>("operation");
        QTest::addColumn<int>("error");
        const QStringList operations{"resolve refusal",   "open refusal",   "DNS failed",
                                     "stream failed",     "resolve throws", "open throws",
                                     "resolution throws", "poll throws",    "read throws"};
        for (const auto& operation : operations) {
            for (const auto error : {DownloadFailure::Timeout, DownloadFailure::LimitExceeded,
                                     DownloadFailure::InvalidResult, DownloadFailure::Unavailable}) {
                const auto label = operation.toUtf8() + '-' + QByteArray::number(static_cast<int>(error));
                QTest::newRow(label.constData()) << operation << static_cast<int>(error);
            }
        }
    }
    void typedIoFailuresAreCapturedBeforeCancelCanEraseThem() {
        QFETCH(QString, operation);
        QFETCH(int, error);
        Harness h;
        h.io.failure_value = static_cast<DownloadFailure>(error);
        h.io.clear_failure_on_cancel = true;
        if (operation == QStringLiteral("resolve refusal"))
            h.io.resolve_ok = false;
        else if (operation == QStringLiteral("open refusal"))
            h.io.open_ok = false;
        else if (operation == QStringLiteral("DNS failed"))
            h.io.dns.state = DownloadResolutionState::Failed;
        else if (operation == QStringLiteral("stream failed"))
            h.io.update.state = PinnedDownloadStreamState::Failed;
        else if (operation == QStringLiteral("resolve throws"))
            h.io.resolve_throws = true;
        else if (operation == QStringLiteral("open throws"))
            h.io.open_throws = true;
        else if (operation == QStringLiteral("resolution throws"))
            h.io.resolution_throws = true;
        else if (operation == QStringLiteral("poll throws"))
            h.io.poll_throws = true;
        else
            h.io.on_read = [] { throw std::runtime_error("synthetic read failure"); };
        static_cast<void>(h.session.start("https://media.example.test/a", 0));
        h.session.tick(1);
        h.session.tick(2);
        QCOMPARE(h.session.snapshot().state, DirectDownloadState::Failed);
        QCOMPARE(h.session.snapshot().error, static_cast<DownloadFailure>(error));
        QCOMPARE(h.io.failure_value, DownloadFailure::None);
        QVERIFY(h.sink.data().isEmpty());
    }
    void missingUnknownAndThrowingFailureReportsFailClosed_data() {
        QTest::addColumn<int>("reported");
        QTest::addColumn<bool>("throws");
        QTest::addColumn<int>("expected");
        QTest::newRow("missing typed error")
            << static_cast<int>(DownloadFailure::None) << false << static_cast<int>(DownloadFailure::Network);
        QTest::newRow("unknown typed error") << 42 << false << static_cast<int>(DownloadFailure::InvalidResult);
        QTest::newRow("getter throws") << static_cast<int>(DownloadFailure::Timeout) << true
                                       << static_cast<int>(DownloadFailure::Network);
    }
    void missingUnknownAndThrowingFailureReportsFailClosed() {
        QFETCH(int, reported);
        QFETCH(bool, throws);
        QFETCH(int, expected);
        Harness h;
        h.io.dns.state = DownloadResolutionState::Failed;
        h.io.failure_value = static_cast<DownloadFailure>(reported);
        h.io.failure_throws = throws;
        h.io.clear_failure_on_cancel = true;
        QVERIFY(h.session.start("https://media.example.test/a", 0));
        h.session.tick(1);
        QCOMPARE(h.session.snapshot().state, DirectDownloadState::Failed);
        QCOMPARE(h.session.snapshot().error, static_cast<DownloadFailure>(expected));
        QVERIFY(h.sink.data().isEmpty());
    }
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
    void concreteIoCannotClaimNativeResolverTerminationAfterCancel() {
        QtDownloadIo io;
        io.cancel();
        const auto stopped = io.pollStop();
        QVERIFY(!stopped.stopped);
        QCOMPARE(stopped.error, DownloadFailure::Unavailable);
        io.cancel();
        QVERIFY(!io.pollStop().stopped);
    }
};

QTEST_GUILESS_MAIN(DirectDownloadSessionTests)
#include "DirectDownloadSessionTests.moc"
