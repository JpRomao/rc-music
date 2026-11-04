#include "AppContainerObserverDriver.hpp"

#include <QTest>

#include <limits>
#include <memory>

using namespace music_player::sandbox_probe;
namespace {
using Driver = AppContainerObserverDriver;
using Protocol = AppContainerObserverProtocol;
using Process = AppContainerObserverProcess;
using Identity = AppContainerObserverIdentity;
using Observer = AppContainerNetworkObserver;
constexpr std::uint64_t begin = 134'067'564'000'000'000;
constexpr std::int64_t start = 1000;
QByteArray userSid() { return QByteArray::fromHex("010500000000000515000000010000000200000003000000e8030000"); }
QByteArray otherUserSid() { return QByteArray::fromHex("010500000000000515000000010000000200000003000000e9030000"); }
Identity::Bytes nativeBytes(const QByteArray& bytes) {
    const auto* first = reinterpret_cast<const std::uint8_t*>(bytes.constData());
    return {first, first + bytes.size()};
}
Identity::Snapshot token(const bool elevated) {
    Identity::Snapshot result;
    result.complete = true;
    result.error = 0;
    result.user_sid = nativeBytes(elevated ? otherUserSid() : userSid());
    result.session_id = 7;
    result.elevated = elevated;
    result.elevation_type = elevated ? Identity::ElevationType::Full : Identity::ElevationType::Limited;
    result.integrity_rid = elevated ? Identity::high_integrity_rid : Identity::medium_integrity_rid;
    return result;
}
Process::Snapshot process(const bool elevated) {
    return {.complete = true,
            .error = 0,
            .pid = elevated ? 456U : 123U,
            .creation_filetime = elevated ? begin - 20 : begin - 100,
            .image_path = L"C:\\private\\probe.exe",
            .token = token(elevated)};
}
struct Fixture final {
    Driver::Config config;
    Driver::Peer peer;
    Protocol::Ready ready;
    Fixture() {
        config.request = {.nonce = QStringLiteral("0123456789abcdef0123456789abcdef"),
                          .driver_user_sid = userSid(),
                          .driver_pid = 123,
                          .driver_creation_filetime = begin - 100,
                          .probe_sha256 = QByteArray(32, 'p'),
                          .helper_path = QStringLiteral("C:/private/helper.exe"),
                          .helper_sha256 = QByteArray(32, 'h'),
                          .package_sid = QByteArray::fromHex(
                              "010800000000000f0200000001000000020000000300000004000000050000000600000007000000"),
                          .loopback_tcp_port = 54321};
        config.request_sha256 = QByteArray(32, 'r');
        config.driver = process(false);
        config.start_ms = start;
        peer = {.process = process(true),
                .image_sha256 = config.request.probe_sha256,
                .image_pinned = true,
                .poll = {Process::State::Running, 0, {}}};
        ready = {config.request.nonce,
                 config.request_sha256,
                 peer.process.pid,
                 peer.process.creation_filetime,
                 begin,
                 begin + Observer::maximum_window_ticks};
    }
    QByteArray readyFrame() const { return Protocol::encodeReady(ready); }
    Observer::Report report() const {
        Observer::Report result;
        result.state = Observer::State::Stopped;
        result.available = true;
        result.app_id_error = result.open_error = result.query_error = result.keyword_query_error = 0;
        result.subscribe_error = result.unsubscribe_error = result.close_error = 0;
        result.value_is_uint32 = result.keywords_is_uint32 = true;
        result.collection_value = 1;
        result.keywords_value = Observer::capability_drop_keyword;
        result.window_begin_filetime = ready.window_begin_filetime;
        result.window_end_filetime = ready.window_end_filetime;
        return result;
    }
    Protocol::Result result() const {
        return {config.request.nonce,
                config.request_sha256,
                peer.process.pid,
                peer.process.creation_filetime,
                Protocol::ResultStatus::ObserverResult,
                ready,
                report()};
    }
    Driver::Peer stopped(const std::uint32_t code = 0) const {
        auto value = peer;
        value.poll = {Process::State::Stopped, 0, code};
        return value;
    }
};
} // namespace

class AppContainerObserverDriverTests final : public QObject {
    Q_OBJECT
  private slots:
    void constructionHasNoLaunchCleanupOrProofPermission() {
        Fixture f;
        QVERIFY(!Protocol::encodeRequest(f.config.request).isEmpty());
        Driver driver(f.config);
        QCOMPARE(driver.state(), Driver::State::AwaitingReady);
        QCOMPARE(driver.reason(), Driver::Reason::None);
        QVERIFY(!driver.acceptedReady() && !driver.result() && !driver.exitCode());
        QVERIFY(!driver.takeLaunchPermission());
        QCOMPARE(driver.state(), Driver::State::AwaitingReady);
        QVERIFY(!driver.canCleanup() && !driver.proofEligible());
    }
    void malformedOrUnboundConfigurationIsStickyUncertain_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 15; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void malformedOrUnboundConfigurationIsStickyUncertain() {
        QFETCH(int, kind);
        Fixture f;
        auto& c = f.config;
        switch (kind) {
        case 0:
            c.request.nonce = QStringLiteral("bad");
            break;
        case 1:
            c.request_sha256.chop(1);
            break;
        case 2:
            ++c.request.driver_pid;
            break;
        case 3:
            ++c.request.driver_creation_filetime;
            break;
        case 4:
            c.request.driver_user_sid = otherUserSid();
            break;
        case 5:
            c.driver.complete = false;
            break;
        case 6:
            c.driver.error = 5;
            break;
        case 7:
            c.driver.token.complete = false;
            break;
        case 8:
            c.driver.token.elevated = true;
            break;
        case 9:
            c.driver.token.integrity_rid = Identity::high_integrity_rid;
            break;
        case 10:
            c.driver.token.ui_access = true;
            break;
        case 11:
            c.driver.token.app_container = true;
            break;
        case 12:
            c.start_ms = -1;
            break;
        case 13:
            c.driver.image_path.clear();
            break;
        case 14:
            c.request.helper_sha256.clear();
            break;
        }
        Driver driver(c);
        QCOMPARE(driver.state(), Driver::State::Uncertain);
        QVERIFY(!driver.ready(f.readyFrame(), f.peer, start, begin));
        QVERIFY(!driver.takeLaunchPermission());
        QCOMPARE(driver.tick(start + 1), Driver::State::Uncertain);
        QVERIFY(!driver.canCleanup() && !driver.proofEligible());
    }
    void validReadyGrantsExactlyOnePermissionWithoutClaimingAnAck_data() {
        QTest::addColumn<bool>("last");
        QTest::newRow("start") << false;
        QTest::newRow("inclusive limits") << true;
    }
    void validReadyGrantsExactlyOnePermissionWithoutClaimingAnAck() {
        QFETCH(bool, last);
        Fixture f;
        Driver driver(f.config);
        const auto now = last ? start + Driver::ready_timeout_ms : start;
        const auto filetime = last ? begin + 5ULL * 10'000'000 : begin;
        QVERIFY(driver.ready(f.readyFrame(), f.peer, now, filetime));
        QCOMPARE(driver.state(), Driver::State::Ready);
        QVERIFY(driver.acceptedReady());
        QCOMPARE(Protocol::encodeReady(*driver.acceptedReady()), f.readyFrame());
        QVERIFY(!driver.canCleanup() && !driver.proofEligible());
        QVERIFY(driver.takeLaunchPermission());
        QCOMPARE(driver.state(), Driver::State::AwaitingResult);
        QVERIFY(!driver.takeLaunchPermission());
        QCOMPARE(driver.state(), Driver::State::AwaitingResult);
    }
    void readyRefusesMalformedBindingsPeersAndWindowWithoutLaunching_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 26; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void readyRefusesMalformedBindingsPeersAndWindowWithoutLaunching() {
        QFETCH(int, kind);
        Fixture f;
        auto frame = f.ready;
        auto peer = f.peer;
        auto now = start;
        auto filetime = begin;
        switch (kind) {
        case 0:
            frame.nonce = QString(32, 'a');
            break;
        case 1:
            frame.request_sha256 = QByteArray(32, 'x');
            break;
        case 2:
            peer.process.complete = false;
            break;
        case 3:
            peer.process.error = 5;
            break;
        case 4:
            ++peer.process.pid;
            break;
        case 5:
            ++peer.process.creation_filetime;
            break;
        case 6:
            peer.process.pid = frame.observer_pid = f.config.driver.pid;
            break;
        case 7:
            peer.process.token.complete = false;
            break;
        case 8:
            peer.process.token.elevated = false;
            break;
        case 9:
            peer.process.token.integrity_rid = Identity::medium_integrity_rid;
            break;
        case 10:
            ++peer.process.token.session_id;
            break;
        case 11:
            peer.process.token.user_sid.clear();
            break;
        case 12:
            peer.process.token.ui_access = true;
            break;
        case 13:
            peer.process.token.app_container = true;
            break;
        case 14:
            peer.process.token.elevation_type = Identity::ElevationType::Limited;
            break;
        case 15:
            peer.image_pinned = false;
            break;
        case 16:
            peer.image_sha256 = QByteArray(32, 'x');
            break;
        case 17:
            peer.image_sha256.chop(1);
            break;
        case 18:
            peer.poll = {Process::State::Unknown, 0, {}};
            break;
        case 19:
            peer.poll.error = 5;
            break;
        case 20:
            peer.poll = {Process::State::Stopped, 0, 0};
            break;
        case 21:
            peer.poll.exit_code = 0;
            break;
        case 22:
            filetime = begin - 1;
            break;
        case 23:
            filetime = begin + 5ULL * 10'000'000 + 1;
            break;
        case 24:
            now = start + Driver::ready_timeout_ms + 1;
            break;
        case 25:
            peer.process.image_path.clear();
            break;
        }
        Driver driver(f.config);
        QVERIFY(!driver.ready(Protocol::encodeReady(frame), peer, now, filetime));
        QCOMPARE(driver.state(), Driver::State::Uncertain);
        QVERIFY(!driver.acceptedReady() && !driver.takeLaunchPermission());
        QVERIFY(!driver.canCleanup() && !driver.proofEligible());
    }
    void malformedReadyAndReplayAreNeverAccepted() {
        Fixture f;
        Driver malformed(f.config);
        QVERIFY(!malformed.ready(QByteArray{"{broken"}, f.peer, start, begin));
        QCOMPARE(malformed.state(), Driver::State::Uncertain);
        Driver driver(f.config);
        QVERIFY(driver.ready(f.readyFrame(), f.peer, start, begin));
        QVERIFY(!driver.ready(f.readyFrame(), f.peer, start + 1, begin));
        QCOMPARE(driver.state(), Driver::State::Uncertain);
        QVERIFY(!driver.takeLaunchPermission());
    }
    void resultRequiresSeparateExitAckAndDoesNotTreatMatchedFalseAsFailure_data() {
        QTest::addColumn<bool>("alreadyStopped");
        QTest::addColumn<quint32>("exitCode");
        for (bool stopped : {false, true})
            for (quint32 code : {0U, 17U})
                QTest::newRow(qPrintable(QString::number(stopped) + '-' + QString::number(code))) << stopped << code;
    }
    void resultRequiresSeparateExitAckAndDoesNotTreatMatchedFalseAsFailure() {
        QFETCH(bool, alreadyStopped);
        QFETCH(quint32, exitCode);
        Fixture f;
        Driver driver(f.config);
        QVERIFY(driver.ready(f.readyFrame(), f.peer, start + 10, begin));
        QVERIFY(driver.takeLaunchPermission());
        const auto frame = Protocol::encodeResult(f.result());
        QVERIFY(!frame.isEmpty());
        QVERIFY(driver.result(frame, alreadyStopped ? f.stopped(exitCode) : f.peer, start + 20));
        QCOMPARE(driver.state(), Driver::State::AwaitingExit);
        QVERIFY(driver.result() && driver.result()->report && !driver.result()->report->matched);
        QVERIFY(!driver.canCleanup() && !driver.proofEligible() && !driver.exitCode());
        QVERIFY(driver.stopped(f.stopped(exitCode), start + 30));
        QCOMPARE(driver.state(), Driver::State::Complete);
        QVERIFY(driver.exitCode());
        QCOMPARE(*driver.exitCode(), exitCode);
        QVERIFY(driver.canCleanup());
        QCOMPARE(driver.proofEligible(), exitCode == 0);
        QVERIFY(!driver.takeLaunchPermission());
    }
    void earlyTerminalResultsNeverGrantHelperPermission_data() {
        QTest::addColumn<int>("kind");
        QTest::newRow("admission rejected") << 0;
        QTest::newRow("unavailable before subscribe") << 1;
        QTest::newRow("stopped before ready publication") << 2;
    }
    void earlyTerminalResultsNeverGrantHelperPermission() {
        QFETCH(int, kind);
        Fixture f;
        auto result = f.result();
        result.ready.reset();
        if (kind == 0) {
            result.status = Protocol::ResultStatus::AdmissionRejected;
            result.report.reset();
        } else if (kind == 1) {
            Observer::Report report;
            report.state = Observer::State::Unavailable;
            report.reason = Observer::Reason::AppId;
            report.app_id_error = 5;
            report.window_begin_filetime = f.ready.window_begin_filetime;
            report.window_end_filetime = f.ready.window_end_filetime;
            result.report = report;
        }
        Driver driver(f.config);
        QVERIFY(driver.result(Protocol::encodeResult(result), f.stopped(9), start + 10));
        QCOMPARE(driver.state(), Driver::State::AwaitingExit);
        QVERIFY(!driver.takeLaunchPermission() && !driver.acceptedReady());
        QVERIFY(!driver.canCleanup() && !driver.proofEligible());
        QVERIFY(driver.stopped(f.stopped(9), start + 11));
        QVERIFY(driver.canCleanup());
        QVERIFY(!driver.proofEligible());
        QCOMPARE(Protocol::encodeResult(*driver.result()), Protocol::encodeResult(result));
    }
    void resultAfterReadyMustRetainTheOriginalFramePeerAndWindow_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 12; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void resultAfterReadyMustRetainTheOriginalFramePeerAndWindow() {
        QFETCH(int, kind);
        Fixture f;
        Driver driver(f.config);
        QVERIFY(driver.ready(f.readyFrame(), f.peer, start, begin));
        QVERIFY(driver.takeLaunchPermission());
        auto value = f.result();
        auto peer = f.peer;
        switch (kind) {
        case 0:
            value.nonce = value.ready->nonce = QString(32, 'a');
            break;
        case 1:
            value.request_sha256 = value.ready->request_sha256 = QByteArray(32, 'x');
            break;
        case 2:
            value.ready.reset();
            break;
        case 3:
            ++value.ready->window_begin_filetime;
            ++value.ready->window_end_filetime;
            ++value.report->window_begin_filetime;
            ++value.report->window_end_filetime;
            break;
        case 4:
            peer.process.token.user_sid = nativeBytes(userSid());
            break;
        case 5:
            peer.process.image_path = L"C:\\other\\probe.exe";
            break;
        case 6:
            peer.image_pinned = false;
            break;
        case 7:
            peer.poll = {Process::State::Unknown, 0, {}};
            break;
        case 8:
            peer.poll.error = 5;
            break;
        case 9:
            ++peer.process.creation_filetime;
            value.observer_creation_filetime = value.ready->observer_creation_filetime = peer.process.creation_filetime;
            break;
        case 10:
            value.status = Protocol::ResultStatus::AdmissionRejected;
            value.ready.reset();
            value.report.reset();
            break;
        case 11:
            peer.process.token.session_id = f.config.driver.token.session_id + 1;
            break;
        }
        QVERIFY(!driver.result(Protocol::encodeResult(value), peer, start + 10));
        QCOMPARE(driver.state(), Driver::State::Uncertain);
        QVERIFY(!driver.canCleanup() && !driver.proofEligible());
    }
    void malformedOrNonterminalResultCannotAcknowledgeAnything_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 7; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void malformedOrNonterminalResultCannotAcknowledgeAnything() {
        QFETCH(int, kind);
        Fixture f;
        Driver driver(f.config);
        QVERIFY(driver.ready(f.readyFrame(), f.peer, start, begin));
        auto value = f.result();
        if (kind == 0)
            value.report->state = Observer::State::Idle;
        if (kind == 1)
            value.report->state = Observer::State::Observing;
        if (kind == 2)
            value.report->state = Observer::State::StopUnconfirmed;
        if (kind == 3)
            value.report->context_retained = true;
        if (kind == 4)
            value.report->unsubscribe_error = 5;
        if (kind == 5)
            value.report->close_error = 5;
        const auto frame = kind == 6 ? QByteArray{"{broken"} : Protocol::encodeResult(value);
        QVERIFY(!driver.result(frame, f.peer, start + 10));
        QCOMPARE(driver.state(), Driver::State::Uncertain);
        QVERIFY(!driver.stopped(f.stopped(), start + 11));
        QVERIFY(!driver.canCleanup() && !driver.proofEligible());
    }
    void inconclusiveTerminalReportCanBeCleanedButNeverBecomesProof() {
        Fixture f;
        Driver driver(f.config);
        QVERIFY(driver.ready(f.readyFrame(), f.peer, start, begin));
        auto value = f.result();
        value.report->reason = Observer::Reason::FilterQuery;
        value.report->filter_result.error = 5;
        QVERIFY(driver.result(Protocol::encodeResult(value), f.peer, start + 1));
        QVERIFY(driver.stopped(f.stopped(), start + 2));
        QVERIFY(driver.canCleanup());
        QVERIFY(!driver.proofEligible());
        QCOMPARE(driver.result()->report->filter_result.error, 5U);
        QVERIFY(!driver.result()->report->matched);
    }
    void unavailableBeforeSubscribeCannotFollowAnAcceptedReadyEvenWhenThePeerStopped() {
        Fixture f;
        Driver driver(f.config);
        QVERIFY(driver.ready(f.readyFrame(), f.peer, start, begin));
        QVERIFY(driver.takeLaunchPermission());
        auto value = f.result();
        Observer::Report report;
        report.state = Observer::State::Unavailable;
        report.reason = Observer::Reason::CollectionQuery;
        report.app_id_error = report.open_error = 0;
        report.query_error = 5;
        report.close_error = 0;
        report.window_begin_filetime = f.ready.window_begin_filetime;
        report.window_end_filetime = f.ready.window_end_filetime;
        value.report = report;
        const auto frame = Protocol::encodeResult(value);
        QVERIFY(!frame.isEmpty());
        QCOMPARE(value.report->subscribe_error, Observer::not_called);
        QCOMPARE(value.report->unsubscribe_error, Observer::not_called);
        QVERIFY(!value.report->context_retained);
        QVERIFY(!driver.result(frame, f.stopped(), start + 1));
        QCOMPARE(driver.state(), Driver::State::Uncertain);
        QVERIFY(!driver.stopped(f.stopped(), start + 2));
        QVERIFY(!driver.canCleanup() && !driver.proofEligible());
    }
    void stoppedRequiresSameAuthenticatedPeerAndASeparateNativeAck_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 8; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void stoppedRequiresSameAuthenticatedPeerAndASeparateNativeAck() {
        QFETCH(int, kind);
        Fixture f;
        Driver driver(f.config);
        QVERIFY(driver.ready(f.readyFrame(), f.peer, start, begin));
        QVERIFY(driver.result(Protocol::encodeResult(f.result()), f.peer, start + 1));
        auto peer = f.stopped();
        switch (kind) {
        case 0:
            peer.poll = {Process::State::Running, 0, {}};
            break;
        case 1:
            peer.poll = {Process::State::Unknown, 0, {}};
            break;
        case 2:
            peer.poll.error = 5;
            break;
        case 3:
            peer.poll.exit_code.reset();
            break;
        case 4:
            ++peer.process.pid;
            break;
        case 5:
            ++peer.process.creation_filetime;
            break;
        case 6:
            peer.process.token.user_sid = nativeBytes(userSid());
            break;
        case 7:
            peer.image_pinned = false;
            break;
        }
        QVERIFY(!driver.stopped(peer, start + 2));
        QCOMPARE(driver.state(), Driver::State::Uncertain);
        QVERIFY(!driver.exitCode() && !driver.canCleanup() && !driver.proofEligible());
    }
    void runningOrStoppedWithoutAnyResultIsNotCleanupAuthority() {
        Fixture f;
        Driver driver(f.config);
        QVERIFY(!driver.stopped(f.stopped(), start));
        QCOMPARE(driver.state(), Driver::State::Uncertain);
        QVERIFY(!driver.canCleanup() && !driver.proofEligible());
    }
    void deadlinesAndClockRegressionNeverCreateAnAck_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 6; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void deadlinesAndClockRegressionNeverCreateAnAck() {
        QFETCH(int, kind);
        Fixture f;
        Driver driver(f.config);
        if (kind >= 2) {
            QVERIFY(driver.ready(f.readyFrame(), f.peer, start + 10, begin));
            if (kind == 4 || kind == 5)
                QVERIFY(driver.result(Protocol::encodeResult(f.result()), f.peer, start + 11));
        }
        const auto now = kind == 0   ? -1LL
                         : kind == 1 ? start + Driver::ready_timeout_ms + 1
                         : kind == 2 ? start + 9
                         : kind == 3 ? start + 10 + Driver::completion_timeout_ms + 1
                         : kind == 4 ? start + 10
                                     : start + 10 + Driver::completion_timeout_ms + 1;
        QCOMPARE(driver.tick(now), Driver::State::Uncertain);
        QVERIFY(!driver.canCleanup() && !driver.proofEligible());
        QVERIFY(!driver.takeLaunchPermission());
        QVERIFY(!driver.stopped(f.stopped(), now + 1));
    }
    void completionDeadlineIsInclusiveAndDoesNotRestartWhenResultArrives() {
        Fixture f;
        Driver driver(f.config);
        QVERIFY(driver.ready(f.readyFrame(), f.peer, start + 10, begin));
        QVERIFY(driver.result(Protocol::encodeResult(f.result()), f.peer, start + 10 + Driver::completion_timeout_ms));
        QVERIFY(driver.stopped(f.stopped(), start + 10 + Driver::completion_timeout_ms));
        QVERIFY(driver.canCleanup() && driver.proofEligible());
        Driver expired(f.config);
        QVERIFY(expired.ready(f.readyFrame(), f.peer, start + 10, begin));
        QVERIFY(expired.result(Protocol::encodeResult(f.result()), f.peer, start + 10 + Driver::completion_timeout_ms));
        QVERIFY(!expired.stopped(f.stopped(), start + 11 + Driver::completion_timeout_ms));
        QCOMPARE(expired.state(), Driver::State::Uncertain);
        QVERIFY(!expired.canCleanup());
    }
    void resultCannotSubstituteAReadyThatTheDriverNeverAccepted() {
        Fixture f;
        Driver driver(f.config);
        QVERIFY(!driver.result(Protocol::encodeResult(f.result()), f.peer, start));
        QCOMPARE(driver.state(), Driver::State::Uncertain);
        QVERIFY(!driver.acceptedReady() && !driver.takeLaunchPermission());
    }
    void explicitTransportFailureIsStickyAndNeverAcknowledgesAnything() {
        Fixture f;
        Driver driver(f.config);
        driver.transportFailure();
        QCOMPARE(driver.state(), Driver::State::Uncertain);
        QCOMPARE(driver.reason(), Driver::Reason::Transport);
        driver.transportFailure();
        QCOMPARE(driver.reason(), Driver::Reason::Transport);
        QVERIFY(!driver.ready(f.readyFrame(), f.peer, start, begin));
        QVERIFY(!driver.takeLaunchPermission());
        QVERIFY(!driver.result(Protocol::encodeResult(f.result()), f.stopped(), start));
        QVERIFY(!driver.stopped(f.stopped(), start));
        QVERIFY(!driver.canCleanup() && !driver.proofEligible());
    }
};

QTEST_GUILESS_MAIN(AppContainerObserverDriverTests)
#include "AppContainerObserverDriverTests.moc"
