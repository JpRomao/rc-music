#include "adapters/YoutubeResolverSession.hpp"
#include "support/YoutubeResolverTestSupport.hpp"

#include <QTest>
#include <deque>
#include <stdexcept>

using namespace music_player::m0;
namespace {
class Process final : public YoutubeResolverProcess {
  public:
    int starts{}, cancels{}, polls{};
    bool accepted{true}, throw_start{}, throw_poll{}, throw_cancel{};
    QString video;
    std::deque<YoutubeResolverProcessUpdate> updates;
    bool start(const QString& id, qint64) override {
        ++starts;
        video = id;
        if (throw_start)
            throw std::runtime_error("private start detail");
        return accepted;
    }
    YoutubeResolverProcessUpdate poll(qint64) override {
        ++polls;
        if (throw_poll)
            throw std::runtime_error("private poll detail");
        if (updates.empty())
            return {};
        auto result = updates.front();
        updates.pop_front();
        return result;
    }
    void cancel() override {
        ++cancels;
        if (throw_cancel)
            throw std::runtime_error("private stop detail");
    }
};
constexpr qint64 epoch = 1'999'999'000;
const QString video = QStringLiteral("aB0-_Cd1EF2");
YoutubeResolverProcessUpdate exited(QByteArray bytes = test::youtubeResolverFixtureJson(), int code = 0) {
    return {.state = YoutubeResolverProcessState::Exited, .stdout_chunk = std::move(bytes), .exit_code = code};
}
} // namespace
class YoutubeResolverSessionTests final : public QObject {
    Q_OBJECT
  private slots:
    void capabilityRetainsOwnedUserAgentHintAfterResolverExit() {
        auto object = QJsonDocument::fromJson(test::youtubeResolverFixtureJson()).object();
        object.insert("http_headers", QJsonObject{{"User-Agent", "RCMusic Synthetic Agent/1"}});
        auto payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
        Process process;
        YoutubeResolverSession session(process);
        QVERIFY(session.start("agent_job", video, 0, epoch));
        process.updates.push_back({.stdout_chunk = payload});
        const auto resolving = session.poll(1, epoch);
        QCOMPARE(resolving.state, YoutubeResolverState::Resolving);
        QVERIFY(!resolving.audio);
        payload.fill('x');
        object.insert("http_headers", QJsonObject{{"User-Agent", "different synthetic value"}});
        process.updates.push_back(exited({}));
        const auto ready = session.poll(2, epoch);
        QCOMPARE(ready.state, YoutubeResolverState::Ready);
        QVERIFY(ready.audio);
        QCOMPARE(ready.audio->jobId(), std::string("agent_job"));
        QCOMPARE(ready.audio->data().user_agent_hint, QByteArray("RCMusic Synthetic Agent/1"));
        QCOMPARE(session.poll(3, epoch).audio, ready.audio);
    }
    void onlyExitZeroAndPipeAcknowledgementCanMintTheMatchingCapability() {
        Process process;
        YoutubeResolverSession session(process);
        QCOMPARE(process.starts, 0);
        QVERIFY(session.start("job_1", video, 10, epoch));
        QCOMPARE(process.video, video);
        process.updates.push_back({.stdout_chunk = test::youtubeResolverFixtureJson()});
        auto update = session.poll(11, epoch);
        QCOMPARE(update.state, YoutubeResolverState::Resolving);
        QVERIFY(!update.audio);
        QVERIFY(!session.start("other", video, 12, epoch));
        process.updates.push_back(exited({}));
        update = session.poll(13, epoch);
        QCOMPARE(update.state, YoutubeResolverState::Ready);
        QVERIFY(update.audio);
        QCOMPARE(update.audio->jobId(), std::string("job_1"));
        QCOMPARE(update.audio->videoId(), video);
        QCOMPARE(update.audio->streamUrl(),
                 QStringLiteral("https://r1.googlevideo.com/videoplayback?expire=2000000000&sig=A%2fb%2F&x=1"));
        QCOMPARE(process.starts, 1);
        QCOMPARE(session.poll(14, epoch).audio, update.audio);
    }
    void malformedOrFailedProcessNeverProducesACapability_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<int>("code");
        QTest::newRow("nonzero exit") << test::youtubeResolverFixtureJson() << 7;
        QTest::newRow("invalid JSON") << QByteArray("not JSON") << 0;
        QTest::newRow("wrong video") << test::youtubeResolverFixtureJson("zzzzzzzzzzz") << 0;
        QTest::newRow("two objects") << (test::youtubeResolverFixtureJson() + "\n{}") << 0;
    }
    void malformedOrFailedProcessNeverProducesACapability() {
        QFETCH(QByteArray, bytes);
        QFETCH(int, code);
        Process process;
        YoutubeResolverSession session(process);
        QVERIFY(session.start("job_1", video, 0, epoch));
        process.updates.push_back(exited(bytes, code));
        const auto update = session.poll(1, epoch);
        QCOMPARE(update.state, YoutubeResolverState::Failed);
        QVERIFY(update.error != DownloadFailure::None);
        QVERIFY(!update.audio);
    }
    void cancellationMustWaitForNativeStopBeforeAcknowledingTerminal() {
        Process process;
        YoutubeResolverSession session(process);
        QVERIFY(session.start("job_1", video, 0, epoch));
        session.cancel();
        session.cancel();
        QCOMPARE(session.poll(1, epoch).state, YoutubeResolverState::Stopping);
        QCOMPARE(process.cancels, 1);
        process.updates.push_back(exited());
        const auto update = session.poll(2, epoch);
        QCOMPARE(update.state, YoutubeResolverState::Cancelled);
        QVERIFY(!update.audio);
    }
    void resourceAndClockFailuresDrainBeforePublishingAFailure() {
        Process process;
        YoutubeResolverSession session(process, {.max_stdout_bytes = 8, .total_timeout_ms = 5});
        QVERIFY(session.start("job_1", video, 10, epoch));
        process.updates.push_back({.stdout_chunk = QByteArray(9, 'x')});
        QCOMPARE(session.poll(11, epoch).state, YoutubeResolverState::Stopping);
        QCOMPARE(process.cancels, 1);
        process.updates.push_back(exited({}));
        const auto update = session.poll(12, epoch);
        QCOMPARE(update.state, YoutubeResolverState::Failed);
        QCOMPARE(update.error, DownloadFailure::LimitExceeded);
        QVERIFY(!update.audio);
    }
    void timeoutAndUncertainStartNeverReleaseBeforeTheStopAcknowledgement() {
        Process process;
        process.accepted = false;
        YoutubeResolverSession session(process);
        QVERIFY(session.start("job_1", video, 0, epoch));
        QCOMPARE(session.poll(1, epoch).state, YoutubeResolverState::Stopping);
        QVERIFY(process.cancels > 0);
        process.updates.push_back(
            {.state = YoutubeResolverProcessState::Failed, .error = DownloadFailure::Unavailable});
        QCOMPARE(session.poll(2, epoch).state, YoutubeResolverState::Failed);
        Process timed;
        YoutubeResolverSession deadline(timed, {.total_timeout_ms = 5});
        QVERIFY(deadline.start("job_2", video, 0, epoch));
        QCOMPARE(deadline.poll(5, epoch).state, YoutubeResolverState::Stopping);
        timed.updates.push_back(exited());
        QCOMPARE(deadline.poll(6, epoch).error, DownloadFailure::Timeout);
    }
    void expiredOrTooShortLivedSignedUrlsCannotBePublished() {
        Process process;
        YoutubeResolverSession session(process);
        QVERIFY(session.start("job_1", video, 0, 1'999'999'990));
        process.updates.push_back(exited());
        const auto update = session.poll(1, 1'999'999'991);
        QCOMPARE(update.state, YoutubeResolverState::Failed);
        QCOMPARE(update.error, DownloadFailure::Expired);
        QVERIFY(!update.audio);
    }
    void invalidIdsAndTimesAreRefusedWithoutStartingAProcess() {
        Process process;
        YoutubeResolverSession session(process);
        QVERIFY(!session.start("bad/path", video, 0, epoch));
        QVERIFY(!session.start("job_1", "https://example.test", 0, epoch));
        QVERIFY(!session.start("job_1", video, -1, epoch));
        QVERIFY(!session.start("job_1", video, 0, -1));
        QCOMPARE(process.starts, 0);
    }
    void unknownProcessStateIsNotAStopAcknowledgement() {
        Process process;
        YoutubeResolverSession session(process);
        QVERIFY(session.start("job_1", video, 0, epoch));
        process.updates.push_back({.state = static_cast<YoutubeResolverProcessState>(99)});
        const auto uncertain = session.poll(1, epoch);
        QCOMPARE(uncertain.state, YoutubeResolverState::Stopping);
        QVERIFY(!uncertain.audio);
        process.updates.push_back(exited());
        const auto stopped = session.poll(2, epoch);
        QCOMPARE(stopped.state, YoutubeResolverState::Failed);
        QCOMPARE(stopped.error, DownloadFailure::InvalidResult);
    }
    void throwingCancellationIsRetriedWhileOwnershipIsRetained() {
        Process process;
        process.throw_cancel = true;
        YoutubeResolverSession session(process);
        QVERIFY(session.start("job_1", video, 0, epoch));
        session.cancel();
        QCOMPARE(process.cancels, 1);
        QCOMPARE(session.poll(1, epoch).state, YoutubeResolverState::Stopping);
        process.throw_cancel = false;
        QCOMPARE(session.poll(2, epoch).state, YoutubeResolverState::Stopping);
        QVERIFY(process.cancels > 1);
        process.updates.push_back(exited());
        QCOMPARE(session.poll(3, epoch).state, YoutubeResolverState::Cancelled);
    }
};
QTEST_GUILESS_MAIN(YoutubeResolverSessionTests)
#include "YoutubeResolverSessionTests.moc"
