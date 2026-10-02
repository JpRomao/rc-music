#include "WindowsDownloadJobSession.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <functional>
#include <stdexcept>

using namespace music_player::m0;

namespace {
class FakeIo final : public DirectDownloadIo {
  public:
    QStringList hosts{};
    QList<PinnedDownloadEndpoint> endpoints{};
    DownloadResolution dns{DownloadResolutionState::Resolved, {QHostAddress{"93.184.215.14"}}};
    QList<QByteArray> chunks{};
    std::function<void()> on_resolve{};
    std::function<void()> on_read{};
    std::function<void()> on_stop{};
    DirectDownloadIoStopUpdate stop_update{true, DownloadFailure::None};
    PinnedDownloadStreamUpdate stream_update{PinnedDownloadStreamState::Receiving};
    DownloadFailure failure_value{DownloadFailure::None};
    bool clear_failure_on_cancel{false};
    int stop_polls{0};
    int premature_stop_polls{0};
    int cancels{0};
    bool resolve(const QString& host) override {
        hosts.push_back(host);
        if (on_resolve)
            on_resolve();
        return true;
    }
    DownloadResolution resolution() const override { return dns; }
    bool open(const PinnedDownloadEndpoint& endpoint) override {
        endpoints.push_back(endpoint);
        return true;
    }
    PinnedDownloadStreamUpdate poll() const override { return stream_update; }
    QByteArray read(qint64 maximum) override {
        if (on_read)
            on_read();
        if (chunks.isEmpty())
            return {};
        auto bytes = chunks.front().left(maximum);
        chunks.front().remove(0, bytes.size());
        if (chunks.front().isEmpty())
            chunks.removeFirst();
        return bytes;
    }
    DownloadFailure failure() const override { return failure_value; }
    void cancel() override {
        ++cancels;
        if (clear_failure_on_cancel)
            failure_value = DownloadFailure::None;
    }
    DirectDownloadIoStopUpdate pollStop() override {
        ++stop_polls;
        if (cancels == 0) {
            ++premature_stop_polls;
            return {false, DownloadFailure::InvalidResult};
        }
        if (on_stop)
            on_stop();
        return stop_update;
    }
    void respond(const QByteArray& body) {
        chunks = {"HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) +
                  "\r\nContent-Type: application/octet-stream\r\n\r\n" + body};
    }
};

QByteArray fixture(const QString& leaf) {
    QFile file(QString::fromUtf8(RC_MUSIC_TEST_AUDIO_FIXTURES) + '/' + leaf);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const auto bytes = file.readAll();
    const auto mp4 = leaf == QStringLiteral("m4a-aac-lc.m4a");
    const auto expected_size = mp4 ? 33400 : 24966;
    const auto hash = mp4 ? QByteArray("aefe894a1a9277bf4795d421c97d4780fa342eef300819a459ca45329a614558")
                          : QByteArray("65e66cf43d999e98cf37cb64003d29b374a1238e454eca138c0dc5be2b18c000");
    return bytes.size() == expected_size && QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() == hash
               ? bytes
               : QByteArray{};
}
DownloadItem item() {
    return {"https://media.example.test/music.mp4?sig=%41%2f&x=1+2", "../../not-a-path & not-a-command.mp3",
            DownloadMediaKind::Video};
}
struct Harness final {
    QTemporaryDir directory{QDir::tempPath() + QStringLiteral("/RC Music job 日本-XXXXXX")};
    QString staging{directory.path() + "/staging"};
    QString publication{directory.path() + "/published"};
    FakeIo io{};
    Harness() {
        QDir().mkpath(staging);
        QDir().mkpath(publication);
    }
    WindowsDownloadJobSessionConfig configuration(quint64 quota = 512 * 1024 * 1024) const {
        WindowsDownloadJobSessionConfig config;
        config.workspace = {staging, publication, quota};
        config.converter = {QString::fromUtf8(RC_MUSIC_TEST_FFMPEG_PATH),
                            QString::fromUtf8(RC_MUSIC_TEST_FFPROBE_PATH)};
        return config;
    }
    QStringList jobs() const { return QDir(staging).entryList(QDir::Dirs | QDir::NoDotAndDotDot); }
    QStringList published() const { return QDir(publication).entryList(QDir::Files); }
};
struct Clock final {
    QElapsedTimer clock{};
    Clock() { clock.start(); }
    qint64 now() const { return clock.elapsed(); }
};
bool terminal(const DownloadExecutionUpdate& update) { return update.state != DownloadExecutionState::Running; }
DownloadExecutionUpdate await(WindowsDownloadJobSession& session, Clock& clock,
                              const std::function<bool(const DownloadExecutionUpdate&)>& stop = terminal) {
    DownloadExecutionUpdate update;
    const auto started = clock.now();
    do {
        update = session.poll(clock.now());
        if (stop(update))
            return update;
        QTest::qWait(5);
    } while (clock.now() - started < 15000);
    return update;
}
DownloadExecutionUpdate drainAt(WindowsDownloadJobSession& session, qint64 now) {
    QElapsedTimer elapsed;
    elapsed.start();
    DownloadExecutionUpdate update;
    do {
        update = session.poll(now);
        if (terminal(update))
            return update;
        QTest::qWait(5);
    } while (elapsed.elapsed() < 5000);
    return update;
}
bool publishingOrTerminal(const DownloadExecutionUpdate& update) {
    return terminal(update) || update.phase == DownloadPhase::Publishing;
}
} // namespace

class WindowsDownloadJobSessionTests final : public QObject {
    Q_OBJECT
  private slots:
    void typedIoFailuresSurviveSessionCompositionAndNativeDraining_data() {
        QTest::addColumn<bool>("dns");
        QTest::addColumn<int>("error");
        for (const bool dns : {true, false}) {
            for (const auto error : {DownloadFailure::Timeout, DownloadFailure::LimitExceeded,
                                     DownloadFailure::InvalidResult, DownloadFailure::Unavailable}) {
                const auto label = QByteArray(dns ? "DNS-" : "stream-") + QByteArray::number(static_cast<int>(error));
                QTest::newRow(label.constData()) << dns << static_cast<int>(error);
            }
        }
    }
    void typedIoFailuresSurviveSessionCompositionAndNativeDraining() {
        QFETCH(bool, dns);
        QFETCH(int, error);
        const auto expected = static_cast<DownloadFailure>(error);
        Harness h;
        h.io.failure_value = expected;
        h.io.clear_failure_on_cancel = true;
        h.io.stop_update = {false, expected};
        if (dns)
            h.io.dns.state = DownloadResolutionState::Failed;
        else
            h.io.stream_update.state = PinnedDownloadStreamState::Failed;
        WindowsDownloadJobSession session(h.io, h.configuration());
        QVERIFY(session.start("job-1", item(), 0));
        for (int index = 0; index != 3; ++index) {
            const auto waiting = session.poll(0);
            QCOMPARE(waiting.state, DownloadExecutionState::Running);
            QCOMPARE(waiting.error, DownloadFailure::None);
            QVERIFY(waiting.published_mp3.empty());
            QCOMPARE(h.jobs().size(), 1);
            QVERIFY(h.published().isEmpty());
        }
        QCOMPARE(h.io.failure_value, DownloadFailure::None);
        h.io.stop_update = {true, DownloadFailure::None};
        const auto stopped = drainAt(session, 0);
        QCOMPARE(stopped.state, DownloadExecutionState::Failed);
        QCOMPARE(stopped.error, expected);
        QVERIFY(stopped.published_mp3.empty());
        QVERIFY(h.jobs().isEmpty());
        QVERIFY(h.published().isEmpty());
        QCOMPARE(session.poll(-1).error, expected);
    }
    void completedTransferCannotConvertOrCleanBeforeNativeIoStops() {
        const auto bytes = fixture("m4a-aac-lc.m4a");
        QVERIFY(!bytes.isEmpty());
        Harness h;
        h.io.respond(bytes);
        h.io.stop_update = {};
        WindowsDownloadJobSession session(h.io, h.configuration());
        QVERIFY(session.start("job-1", item(), 0));
        static_cast<void>(session.poll(0));
        for (int index = 0; index != 5; ++index) {
            const auto waiting = session.poll(0);
            QCOMPARE(waiting.state, DownloadExecutionState::Running);
            QCOMPARE(waiting.phase, DownloadPhase::Downloading);
            QCOMPARE(waiting.error, DownloadFailure::None);
            QCOMPARE(waiting.completed_units, static_cast<std::uint64_t>(bytes.size()));
            QVERIFY(waiting.published_mp3.empty());
            QCOMPARE(h.jobs().size(), 1);
            QCOMPARE(QFileInfo(h.staging + '/' + h.jobs().front() + "/input.bin").size(), bytes.size());
            QCOMPARE(QFileInfo(h.staging + '/' + h.jobs().front() + "/converted.part").size(), qint64{0});
            QVERIFY(h.published().isEmpty());
        }
        QVERIFY(h.io.stop_polls >= 5);
        QCOMPARE(h.io.premature_stop_polls, 0);
        h.io.stop_update.stopped = true;
        const auto final = drainAt(session, 0);
        QCOMPARE(final.state, DownloadExecutionState::Published);
        QCOMPARE(final.error, DownloadFailure::None);
        QVERIFY(h.jobs().isEmpty());
        QCOMPARE(h.published().size(), 1);
    }
    void everyStopCauseWaitsForNativeAcknowledgement_data() {
        QTest::addColumn<QString>("cause");
        QTest::addColumn<int>("failure");
        QTest::newRow("cancel during DNS") << QStringLiteral("cancel") << static_cast<int>(DownloadFailure::None);
        QTest::newRow("network rejected")
            << QStringLiteral("restricted") << static_cast<int>(DownloadFailure::Restricted);
        QTest::newRow("source expired") << QStringLiteral("expired") << static_cast<int>(DownloadFailure::Expired);
        QTest::newRow("deadline") << QStringLiteral("deadline") << static_cast<int>(DownloadFailure::Timeout);
        QTest::newRow("backward clock") << QStringLiteral("clock") << static_cast<int>(DownloadFailure::InvalidResult);
        QTest::newRow("quota") << QStringLiteral("quota") << static_cast<int>(DownloadFailure::LimitExceeded);
        QTest::newRow("reentrant resolve cancel")
            << QStringLiteral("resolve") << static_cast<int>(DownloadFailure::None);
        QTest::newRow("reentrant read cancel") << QStringLiteral("read") << static_cast<int>(DownloadFailure::None);
    }
    void everyStopCauseWaitsForNativeAcknowledgement() {
        QFETCH(QString, cause);
        QFETCH(int, failure);
        Harness h;
        h.io.stop_update = {};
        auto config = h.configuration(cause == QStringLiteral("quota") ? 16 : 512 * 1024 * 1024);
        config.total_timeout_ms = 10;
        WindowsDownloadJobSession session(h.io, config);
        if (cause == QStringLiteral("resolve"))
            h.io.on_resolve = [&] { session.cancel(); };
        if (cause == QStringLiteral("read"))
            h.io.on_read = [&] { session.cancel(); };
        if (cause == QStringLiteral("restricted"))
            h.io.dns.addresses = {QHostAddress{"127.0.0.1"}};
        if (cause == QStringLiteral("expired"))
            h.io.chunks = {"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"};
        if (cause == QStringLiteral("quota") || cause == QStringLiteral("read"))
            h.io.respond("synthetic body exceeds this job's small quota");
        static_cast<void>(session.start("job-1", item(), 1));
        if (cause == QStringLiteral("cancel"))
            session.cancel();
        const auto now = cause == QStringLiteral("deadline") ? qint64{11}
                         : cause == QStringLiteral("clock")  ? qint64{0}
                                                             : qint64{1};
        for (int index = 0; index != 4; ++index) {
            const auto waiting = session.poll(now);
            QCOMPARE(waiting.state, DownloadExecutionState::Running);
            QCOMPARE(waiting.error, DownloadFailure::None);
            QVERIFY(waiting.published_mp3.empty());
            QCOMPARE(h.jobs().size(), 1);
            QVERIFY(h.published().isEmpty());
        }
        QVERIFY(h.io.stop_polls != 0);
        QCOMPARE(h.io.premature_stop_polls, 0);
        h.io.stop_update.stopped = true;
        const auto stopped = drainAt(session, now);
        QCOMPARE(stopped.state, failure == 0 ? DownloadExecutionState::Cancelled : DownloadExecutionState::Failed);
        QCOMPARE(stopped.error, static_cast<DownloadFailure>(failure));
        QVERIFY(h.jobs().isEmpty());
        QVERIFY(h.published().isEmpty());
    }
    void stopErrorsRemainPrivateUntilAcknowledgementAndCannotBeClearedByCancel() {
        const auto bytes = fixture("m4a-aac-lc.m4a");
        QVERIFY(!bytes.isEmpty());
        Harness h;
        h.io.respond(bytes);
        h.io.stop_update = {false, DownloadFailure::Network};
        WindowsDownloadJobSession session(h.io, h.configuration());
        QVERIFY(session.start("job-1", item(), 0));
        static_cast<void>(session.poll(0));
        const auto first = session.poll(0);
        QCOMPARE(first.state, DownloadExecutionState::Running);
        QCOMPARE(first.phase, DownloadPhase::Downloading);
        QCOMPARE(first.error, DownloadFailure::None);
        QCOMPARE(h.jobs().size(), 1);
        session.cancel();
        h.io.stop_update = {};
        QCOMPARE(session.poll(0).state, DownloadExecutionState::Running);
        QCOMPARE(h.jobs().size(), 1);
        h.io.stop_update.stopped = true;
        const auto stopped = drainAt(session, 0);
        QCOMPARE(stopped.state, DownloadExecutionState::Failed);
        QCOMPARE(stopped.error, DownloadFailure::Network);
        QVERIFY(h.jobs().isEmpty());
        QVERIFY(h.published().isEmpty());
    }
    void unknownStopErrorsAreNormalizedWithoutErasingTheFirstFailure_data() {
        QTest::addColumn<bool>("prior_failure");
        QTest::newRow("unknown first error") << false;
        QTest::newRow("earlier expired cause survives") << true;
    }
    void unknownStopErrorsAreNormalizedWithoutErasingTheFirstFailure() {
        QFETCH(bool, prior_failure);
        Harness h;
        h.io.stop_update = {false, static_cast<DownloadFailure>(42)};
        if (prior_failure)
            h.io.chunks = {"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"};
        WindowsDownloadJobSession session(h.io, h.configuration());
        QVERIFY(session.start("job-1", item(), 0));
        if (!prior_failure)
            session.cancel();
        for (int index = 0; index != 3; ++index) {
            const auto waiting = session.poll(0);
            QCOMPARE(waiting.state, DownloadExecutionState::Running);
            QCOMPARE(waiting.error, DownloadFailure::None);
            QVERIFY(waiting.published_mp3.empty());
            QCOMPARE(h.jobs().size(), 1);
            QVERIFY(h.published().isEmpty());
        }
        h.io.stop_update = {true, DownloadFailure::None};
        const auto stopped = drainAt(session, 0);
        QCOMPARE(stopped.state, DownloadExecutionState::Failed);
        QCOMPARE(stopped.error, prior_failure ? DownloadFailure::Expired : DownloadFailure::InvalidResult);
        QVERIFY(stopped.published_mp3.empty());
        QVERIFY(h.jobs().isEmpty());
        QVERIFY(h.published().isEmpty());
        QCOMPARE(session.poll(-1).error, stopped.error);
    }
    void clockFailureDuringCancellationDrainIsNotLost_data() {
        QTest::addColumn<bool>("backwards");
        QTest::newRow("clock regresses") << true;
        QTest::newRow("total deadline expires") << false;
    }
    void clockFailureDuringCancellationDrainIsNotLost() {
        QFETCH(bool, backwards);
        Harness h;
        h.io.stop_update = {};
        auto config = h.configuration();
        config.total_timeout_ms = 10;
        WindowsDownloadJobSession session(h.io, config);
        QVERIFY(session.start("job-1", item(), 10));
        session.cancel();
        QCOMPARE(session.poll(10).state, DownloadExecutionState::Running);
        const auto now = backwards ? qint64{9} : qint64{20};
        const auto waiting = session.poll(now);
        QCOMPARE(waiting.state, DownloadExecutionState::Running);
        QCOMPARE(waiting.error, DownloadFailure::None);
        QCOMPARE(h.jobs().size(), 1);
        h.io.stop_update.stopped = true;
        const auto stopped = drainAt(session, now);
        QCOMPARE(stopped.state, DownloadExecutionState::Failed);
        QCOMPARE(stopped.error, backwards ? DownloadFailure::InvalidResult : DownloadFailure::Timeout);
        QVERIFY(h.jobs().isEmpty());
    }
    void firstFailureSurvivesLaterClockAckErrorsAndCancellation() {
        Harness h;
        h.io.stop_update = {};
        h.io.chunks = {"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"};
        WindowsDownloadJobSession session(h.io, h.configuration());
        QVERIFY(session.start("job-1", item(), 10));
        static_cast<void>(session.poll(10));
        QCOMPARE(session.poll(10).state, DownloadExecutionState::Running);
        session.cancel();
        h.io.stop_update.error = DownloadFailure::Network;
        QCOMPARE(session.poll(9).state, DownloadExecutionState::Running);
        h.io.stop_update = {true, DownloadFailure::None};
        const auto stopped = drainAt(session, 9);
        QCOMPARE(stopped.state, DownloadExecutionState::Failed);
        QCOMPARE(stopped.error, DownloadFailure::Expired);
        QVERIFY(h.jobs().isEmpty());
    }
    void reentrantCancellationWhileStopIsAcknowledgedCannotBeginConversion() {
        const auto bytes = fixture("m4a-aac-lc.m4a");
        QVERIFY(!bytes.isEmpty());
        Harness h;
        h.io.respond(bytes);
        WindowsDownloadJobSession session(h.io, h.configuration());
        h.io.on_stop = [&] { session.cancel(); };
        QVERIFY(session.start("job-1", item(), 0));
        const auto stopped = drainAt(session, 0);
        QCOMPARE(stopped.state, DownloadExecutionState::Cancelled);
        QCOMPARE(stopped.error, DownloadFailure::None);
        QCOMPARE(stopped.phase, DownloadPhase::Downloading);
        QVERIFY(h.jobs().isEmpty());
        QVERIFY(h.published().isEmpty());
    }
    void stopPollExceptionDoesNotAcknowledgeCleanupOrEraseItsFailure() {
        Harness h;
        h.io.stop_update = {};
        WindowsDownloadJobSession session(h.io, h.configuration());
        QVERIFY(session.start("job-1", item(), 0));
        session.cancel();
        h.io.on_stop = [] { throw std::runtime_error("synthetic stop poll failure"); };
        QCOMPARE(session.poll(0).state, DownloadExecutionState::Running);
        QCOMPARE(h.jobs().size(), 1);
        h.io.on_stop = {};
        QCOMPARE(session.poll(0).state, DownloadExecutionState::Running);
        h.io.stop_update.stopped = true;
        const auto stopped = drainAt(session, 0);
        QCOMPARE(stopped.state, DownloadExecutionState::Failed);
        QCOMPARE(stopped.error, DownloadFailure::Network);
        QVERIFY(h.jobs().isEmpty());
    }
    void cleanupFailureAfterDelayedStopStillPreventsCleanCancellation() {
        Harness h;
        h.io.stop_update = {};
        WindowsDownloadJobSession session(h.io, h.configuration());
        QVERIFY(session.start("job-1", item(), 0));
        const auto unknown_path = h.staging + '/' + h.jobs().front() + "/unknown.txt";
        QFile unknown(unknown_path);
        QVERIFY(unknown.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        QCOMPARE(unknown.write("not owned"), qint64{9});
        unknown.close();
        session.cancel();
        QCOMPARE(session.poll(0).state, DownloadExecutionState::Running);
        QVERIFY(QFileInfo::exists(unknown_path));
        h.io.stop_update.stopped = true;
        const auto stopped = drainAt(session, 0);
        QCOMPARE(stopped.state, DownloadExecutionState::Failed);
        QCOMPARE(stopped.error, DownloadFailure::Publication);
        QVERIFY(QFileInfo::exists(unknown_path));
        QVERIFY(stopped.published_mp3.empty());
        QVERIFY(QFile::remove(unknown_path));
    }
    void constructorDoesNotCreateFilesOrStartIo() {
        Harness h;
        WindowsDownloadJobSession session(h.io, h.configuration());
        QVERIFY(h.jobs().isEmpty());
        QVERIFY(h.published().isEmpty());
        QVERIFY(h.io.hosts.isEmpty());
        QVERIFY(session.retainedPublicationPath().isEmpty());
    }
    void invalidIdsAndRequestsDoNotStartIo_data() {
        QTest::addColumn<QByteArray>("id");
        QTest::addColumn<QString>("url");
        QTest::addColumn<qint64>("now");
        const auto url = QString::fromStdString(item().url);
        QTest::newRow("empty ID") << QByteArray{} << url << qint64{0};
        QTest::newRow("long ID") << QByteArray(65, 'a') << url << qint64{0};
        QTest::newRow("path ID") << QByteArray("../file") << url << qint64{0};
        QTest::newRow("NUL ID") << QByteArray("ab\0cd", 5) << url << qint64{0};
        QTest::newRow("unicode ID") << QStringLiteral("日本").toUtf8() << url << qint64{0};
        QTest::newRow("local URL") << QByteArray("job-1") << QStringLiteral("http://127.0.0.1/private") << qint64{0};
        QTest::newRow("negative clock") << QByteArray("job-1") << url << qint64{-1};
    }
    void invalidIdsAndRequestsDoNotStartIo() {
        QFETCH(QByteArray, id);
        QFETCH(QString, url);
        QFETCH(qint64, now);
        Harness h;
        WindowsDownloadJobSession session(h.io, h.configuration());
        auto source = item();
        source.url = url.toStdString();
        QVERIFY(!session.start(id.toStdString(), source, now));
        QVERIFY(h.io.hosts.isEmpty());
        QVERIFY(h.jobs().isEmpty());
        QVERIFY(h.published().isEmpty());
    }
    void malformedUtf8ControlsOversizedUrlsAndUnknownKindsNeverReachIo_data() {
        QTest::addColumn<QByteArray>("url");
        QTest::addColumn<int>("kind");
        const auto valid = QByteArray::fromStdString(item().url);
        QTest::newRow("invalid UTF8") << QByteArray("https://media.example.test/a?x=\xc3\x28") << 1;
        QTest::newRow("NUL URL") << (valid + QByteArray("\0", 1)) << 1;
        QTest::newRow("control URL") << (valid + '\n') << 1;
        QTest::newRow("empty URL") << QByteArray{} << 1;
        QTest::newRow("oversized URL") << (valid + QByteArray(8192, 'a')) << 1;
        QTest::newRow("unknown kind") << valid << 999;
    }
    void malformedUtf8ControlsOversizedUrlsAndUnknownKindsNeverReachIo() {
        QFETCH(QByteArray, url);
        QFETCH(int, kind);
        Harness h;
        WindowsDownloadJobSession session(h.io, h.configuration());
        auto source = item();
        source.url = url.toStdString();
        source.kind = static_cast<DownloadMediaKind>(kind);
        QVERIFY(!session.start("job-1", source, 0));
        QVERIFY(h.io.hosts.isEmpty());
        QVERIFY(h.jobs().isEmpty());
        QVERIFY(h.published().isEmpty());
    }
    void authenticatedSyntheticTransferConvertsThenPublishesOnlyInSeparatePhase_data() {
        QTest::addColumn<QString>("leaf");
        QTest::newRow("MP4 audio") << QStringLiteral("m4a-aac-lc.m4a");
        QTest::newRow("MP3 Unicode metadata") << QStringLiteral("mp3-unicode-tags.mp3");
    }
    void authenticatedSyntheticTransferConvertsThenPublishesOnlyInSeparatePhase() {
        QFETCH(QString, leaf);
        const auto bytes = fixture(leaf);
        QVERIFY(!bytes.isEmpty());
        Harness h;
        h.io.respond(bytes);
        WindowsDownloadJobSession session(h.io, h.configuration());
        Clock clock;
        QVERIFY(session.start("job_1-ABC", item(), clock.now()));
        auto previous = DownloadPhase::Downloading;
        bool converted = false;
        const auto before = await(session, clock, [&](const DownloadExecutionUpdate& update) {
            if (update.phase < previous)
                return true;
            previous = update.phase;
            if (update.phase == DownloadPhase::Converting) {
                converted = true;
                if (update.total_units || update.error != DownloadFailure::None || !update.published_mp3.empty())
                    return true;
            }
            return publishingOrTerminal(update);
        });
        QVERIFY(converted);
        QCOMPARE(before.state, DownloadExecutionState::Running);
        QCOMPARE(before.job_id, std::string("job_1-ABC"));
        QCOMPARE(before.phase, DownloadPhase::Publishing);
        QCOMPARE(before.completed_units, std::uint64_t{0});
        QCOMPARE(before.total_units, std::optional<std::uint64_t>{1});
        QCOMPARE(before.error, DownloadFailure::None);
        QVERIFY(before.published_mp3.empty());
        QVERIFY(h.published().isEmpty());
        QCOMPARE(h.io.hosts, QStringList{QStringLiteral("media.example.test")});
        QCOMPARE(h.io.endpoints.size(), 1);
        QCOMPARE(h.io.endpoints.front().address, QHostAddress{"93.184.215.14"});
        QCOMPARE(h.io.endpoints.front().request_target, QByteArray("/music.mp4?sig=%41%2f&x=1+2"));
        const auto final = await(session, clock);
        QCOMPARE(final.state, DownloadExecutionState::Published);
        QCOMPARE(final.phase, DownloadPhase::Publishing);
        QCOMPARE(final.completed_units, std::uint64_t{1});
        QCOMPARE(final.total_units, std::optional<std::uint64_t>{1});
        QCOMPARE(final.error, DownloadFailure::None);
        QVERIFY(final.published_mp3.is_absolute());
        QCOMPARE(final.published_mp3.extension(), std::filesystem::path(".mp3"));
        QVERIFY(QFileInfo(QString::fromStdWString(final.published_mp3.native())).size() > 0);
        QCOMPARE(h.published().size(), 1);
        QVERIFY(h.jobs().isEmpty());
        const auto again = session.poll(clock.now());
        QCOMPARE(again.state, final.state);
        QCOMPARE(again.published_mp3, final.published_mp3);
        QCOMPARE(again.completed_units, final.completed_units);
        session.cancel();
        QCOMPARE(session.poll(clock.now()).state, DownloadExecutionState::Published);
        QVERIFY(!session.start("job-2", item(), clock.now()));
        QCOMPARE(h.published().size(), 1);
        QCOMPARE(h.io.hosts.size(), 1);
    }
    void cancellationDuringResolveCannotCreateAnActiveDownload() {
        Harness h;
        WindowsDownloadJobSession session(h.io, h.configuration());
        h.io.on_resolve = [&] { session.cancel(); };
        static_cast<void>(session.start("job-1", item(), 0));
        const auto result = drainAt(session, 0);
        QCOMPARE(result.state, DownloadExecutionState::Cancelled);
        QCOMPARE(result.error, DownloadFailure::None);
        QVERIFY(result.published_mp3.empty());
        QVERIFY(h.io.endpoints.isEmpty());
        QVERIFY(h.jobs().isEmpty());
        QVERIFY(h.published().isEmpty());
    }
    void cancellationDuringReadCannotSealConvertOrPublish() {
        const auto bytes = fixture("m4a-aac-lc.m4a");
        QVERIFY(!bytes.isEmpty());
        Harness h;
        h.io.respond(bytes);
        WindowsDownloadJobSession session(h.io, h.configuration());
        h.io.on_read = [&] { session.cancel(); };
        Clock clock;
        QVERIFY(session.start("job-1", item(), clock.now()));
        const auto result = await(session, clock);
        QCOMPARE(result.state, DownloadExecutionState::Cancelled);
        QCOMPARE(result.error, DownloadFailure::None);
        QCOMPARE(result.phase, DownloadPhase::Downloading);
        QVERIFY(result.published_mp3.empty());
        QVERIFY(h.jobs().isEmpty());
        QVERIFY(h.published().isEmpty());
    }
    void cancellationBeforePublicationNeverCreatesAFinalFile() {
        const auto bytes = fixture("m4a-aac-lc.m4a");
        QVERIFY(!bytes.isEmpty());
        Harness h;
        h.io.respond(bytes);
        WindowsDownloadJobSession session(h.io, h.configuration());
        Clock clock;
        QVERIFY(session.start("job-1", item(), clock.now()));
        const auto before = await(session, clock, publishingOrTerminal);
        QCOMPARE(before.state, DownloadExecutionState::Running);
        QCOMPARE(before.phase, DownloadPhase::Publishing);
        session.cancel();
        const auto result = await(session, clock);
        QCOMPARE(result.state, DownloadExecutionState::Cancelled);
        QCOMPARE(result.error, DownloadFailure::None);
        QVERIFY(result.published_mp3.empty());
        QVERIFY(h.jobs().isEmpty());
        QVERIFY(h.published().isEmpty());
    }
    void cancellationDuringConversionKeepsOwnershipUntilTheProcessStops() {
        const auto bytes = fixture("m4a-aac-lc.m4a");
        QVERIFY(!bytes.isEmpty());
        Harness h;
        h.io.respond(bytes);
        WindowsDownloadJobSession session(h.io, h.configuration());
        QVERIFY(session.start("job-1", item(), 0));
        static_cast<void>(session.poll(1));
        QCOMPARE(session.poll(2).phase, DownloadPhase::Converting);
        session.cancel();
        const auto stopping = session.poll(2);
        if (stopping.state == DownloadExecutionState::Running) {
            QCOMPARE(stopping.error, DownloadFailure::None);
            QVERIFY(stopping.published_mp3.empty());
            QCOMPARE(h.jobs().size(), 1);
        }
        const auto result = drainAt(session, 2);
        QCOMPARE(result.state, DownloadExecutionState::Cancelled);
        QCOMPARE(result.error, DownloadFailure::None);
        QVERIFY(h.jobs().isEmpty());
        QVERIFY(h.published().isEmpty());
    }
    void aggregateQuotaFailuresPreserveLimitExceeded_data() {
        QTest::addColumn<quint64>("quota");
        QTest::newRow("input exceeds aggregate") << quint64{128};
        QTest::newRow("input fits but output exceeds aggregate") << quint64{33400 + 128};
    }
    void aggregateQuotaFailuresPreserveLimitExceeded() {
        const auto bytes = fixture("m4a-aac-lc.m4a");
        QVERIFY(!bytes.isEmpty());
        QFETCH(quint64, quota);
        Harness h;
        h.io.respond(bytes);
        WindowsDownloadJobSession session(h.io, h.configuration(quota));
        Clock clock;
        QVERIFY(session.start("job-1", item(), clock.now()));
        const auto result = await(session, clock);
        QCOMPARE(result.state, DownloadExecutionState::Failed);
        QCOMPARE(result.error, DownloadFailure::LimitExceeded);
        QVERIFY(result.published_mp3.empty());
        QVERIFY(h.jobs().isEmpty());
        QVERIFY(h.published().isEmpty());
    }
    void backwardClockStopsAndCleansWithoutPublishing() {
        Harness h;
        WindowsDownloadJobSession session(h.io, h.configuration());
        QVERIFY(session.start("job-1", item(), 10));
        const auto result = session.poll(9);
        QCOMPARE(result.state, DownloadExecutionState::Failed);
        QCOMPARE(result.error, DownloadFailure::InvalidResult);
        QVERIFY(result.published_mp3.empty());
        QVERIFY(h.jobs().isEmpty());
        QVERIFY(h.published().isEmpty());
    }
    void totalDeadlineIncludesTimeSpentDownloadingBeforeConversion() {
        const auto bytes = fixture("m4a-aac-lc.m4a");
        QVERIFY(!bytes.isEmpty());
        Harness h;
        h.io.respond(bytes);
        auto config = h.configuration();
        config.total_timeout_ms = 100;
        WindowsDownloadJobSession session(h.io, config);
        QVERIFY(session.start("job-1", item(), 0));
        QCOMPARE(session.poll(80).phase, DownloadPhase::Downloading);
        QCOMPARE(session.poll(80).phase, DownloadPhase::Converting);
        const auto stopped = session.poll(100);
        if (stopped.state == DownloadExecutionState::Running) {
            QCOMPARE(stopped.error, DownloadFailure::None);
            QVERIFY(stopped.published_mp3.empty());
        }
        const auto result = drainAt(session, 100);
        QCOMPARE(result.state, DownloadExecutionState::Failed);
        QCOMPARE(result.error, DownloadFailure::Timeout);
        QVERIFY(result.published_mp3.empty());
        QVERIFY(h.jobs().isEmpty());
        QVERIFY(h.published().isEmpty());
    }
    void deadlineAndCancelBeforeCommitAreRecheckedInPublishingPhase() {
        const auto bytes = fixture("m4a-aac-lc.m4a");
        QVERIFY(!bytes.isEmpty());
        Harness h;
        h.io.respond(bytes);
        auto config = h.configuration();
        config.total_timeout_ms = 10000;
        WindowsDownloadJobSession session(h.io, config);
        Clock clock;
        const auto started_ms = clock.now();
        QVERIFY(session.start("job-1", item(), started_ms));
        const auto before = await(session, clock, publishingOrTerminal);
        QCOMPARE(before.state, DownloadExecutionState::Running);
        QCOMPARE(before.phase, DownloadPhase::Publishing);
        const auto result = session.poll(started_ms + config.total_timeout_ms);
        QCOMPARE(result.state, DownloadExecutionState::Failed);
        QCOMPARE(result.error, DownloadFailure::Timeout);
        QVERIFY(h.published().isEmpty());
        QVERIFY(h.jobs().isEmpty());
    }
    void aValidMp3DoesNotWaiveFullDecodeBeforePublication() {
        const auto bytes = fixture("m4a-aac-lc.m4a");
        QVERIFY(!bytes.isEmpty());
        Harness h;
        h.io.respond(bytes);
        auto config = h.configuration();
        config.converter.ffmpeg_executable = QString::fromUtf8(RC_MUSIC_TEST_CONVERTER_HELPER_PATH);
        WindowsDownloadJobSession session(h.io, config);
        Clock clock;
        QVERIFY(session.start("job-1", item(), clock.now()));
        const auto result = await(session, clock);
        QCOMPARE(result.state, DownloadExecutionState::Failed);
        QCOMPARE(result.error, DownloadFailure::Conversion);
        QVERIFY(result.published_mp3.empty());
        QVERIFY(h.jobs().isEmpty());
        QVERIFY(h.published().isEmpty());
    }
    void cleanupFailureIsNotReportedAsSuccessfulPublicationOrCleanCancellation() {
        const auto bytes = fixture("m4a-aac-lc.m4a");
        QVERIFY(!bytes.isEmpty());
        Harness h;
        h.io.respond(bytes);
        WindowsDownloadJobSession session(h.io, h.configuration());
        Clock clock;
        QVERIFY(session.start("job-1", item(), clock.now()));
        const auto before = await(session, clock, publishingOrTerminal);
        QCOMPARE(before.state, DownloadExecutionState::Running);
        QCOMPARE(before.phase, DownloadPhase::Publishing);
        QCOMPARE(h.jobs().size(), 1);
        const auto unknown_path = h.staging + '/' + h.jobs().front() + "/unknown.txt";
        QFile unknown(unknown_path);
        QVERIFY(unknown.open(QIODevice::WriteOnly));
        QCOMPARE(unknown.write("not owned"), qint64{9});
        unknown.close();
        const auto result = await(session, clock);
        QCOMPARE(result.state, DownloadExecutionState::Failed);
        QCOMPARE(result.error, DownloadFailure::Publication);
        QVERIFY(result.published_mp3.empty());
        QVERIFY(QFileInfo::exists(unknown_path));
        const auto retained = session.retainedPublicationPath();
        QVERIFY(!retained.isEmpty());
        QVERIFY(QFileInfo(retained).size() > 0);
        QCOMPARE(h.published().size(), 1);
        QCOMPARE(session.poll(clock.now()).error, DownloadFailure::Publication);
        QVERIFY(QFile::remove(unknown_path)); // Only this synthetic test's own extra file.
    }
    void anUnknownFilePreventsCleanCancellationWithoutDeletingIt() {
        Harness h;
        WindowsDownloadJobSession session(h.io, h.configuration());
        QVERIFY(session.start("job-1", item(), 0));
        QCOMPARE(h.jobs().size(), 1);
        const auto unknown_path = h.staging + '/' + h.jobs().front() + "/unknown.txt";
        QFile unknown(unknown_path);
        QVERIFY(unknown.open(QIODevice::WriteOnly));
        QCOMPARE(unknown.write("not owned"), qint64{9});
        unknown.close();
        session.cancel();
        const auto result = drainAt(session, 0);
        QCOMPARE(result.state, DownloadExecutionState::Failed);
        QCOMPARE(result.error, DownloadFailure::Publication);
        QVERIFY(result.published_mp3.empty());
        QVERIFY(session.retainedPublicationPath().isEmpty());
        QVERIFY(QFileInfo::exists(unknown_path));
        QVERIFY(h.published().isEmpty());
        QVERIFY(QFile::remove(unknown_path));
    }
    void terminalFailuresAndCancellationRemainStableAfterLateInput_data() {
        QTest::addColumn<bool>("cancelled");
        QTest::newRow("cancelled") << true;
        QTest::newRow("expired source") << false;
    }
    void terminalFailuresAndCancellationRemainStableAfterLateInput() {
        QFETCH(bool, cancelled);
        Harness h;
        WindowsDownloadJobSession session(h.io, h.configuration());
        QVERIFY(session.start("job-1", item(), 0));
        if (cancelled)
            session.cancel();
        else {
            h.io.chunks = {"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"};
            static_cast<void>(session.poll(0));
        }
        const auto result = drainAt(session, 0);
        QCOMPARE(result.state, cancelled ? DownloadExecutionState::Cancelled : DownloadExecutionState::Failed);
        QCOMPARE(result.error, cancelled ? DownloadFailure::None : DownloadFailure::Expired);
        h.io.respond("late bytes cannot become a file");
        session.cancel();
        const auto late = session.poll(-1);
        QCOMPARE(late.job_id, result.job_id);
        QCOMPARE(late.state, result.state);
        QCOMPARE(late.phase, result.phase);
        QCOMPARE(late.completed_units, result.completed_units);
        QCOMPARE(late.total_units, result.total_units);
        QCOMPARE(late.published_mp3, result.published_mp3);
        QCOMPARE(late.error, result.error);
        QVERIFY(h.jobs().isEmpty());
        QVERIFY(h.published().isEmpty());
    }
    void ioExceptionsFailNetworkAndCleanupWithoutPublishing_data() {
        QTest::addColumn<bool>("throw_on_resolve");
        QTest::newRow("resolve throws") << true;
        QTest::newRow("read throws") << false;
    }
    void ioExceptionsFailNetworkAndCleanupWithoutPublishing() {
        QFETCH(bool, throw_on_resolve);
        Harness h;
        WindowsDownloadJobSession session(h.io, h.configuration());
        const auto fail = [] { throw std::runtime_error("synthetic IO failure"); };
        if (throw_on_resolve)
            h.io.on_resolve = fail;
        else
            h.io.on_read = fail;
        static_cast<void>(session.start("job-1", item(), 0));
        const auto result = drainAt(session, 0);
        QCOMPARE(result.state, DownloadExecutionState::Failed);
        QCOMPARE(result.error, DownloadFailure::Network);
        QVERIFY(result.published_mp3.empty());
        QVERIFY(h.jobs().isEmpty());
        QVERIFY(h.published().isEmpty());
        QCOMPARE(session.poll(-1).error, DownloadFailure::Network);
    }
};

QTEST_GUILESS_MAIN(WindowsDownloadJobSessionTests)
#include "WindowsDownloadJobSessionTests.moc"
