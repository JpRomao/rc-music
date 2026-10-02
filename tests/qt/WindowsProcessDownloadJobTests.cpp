#include "WindowsDownloadJobSession.hpp"
#include "WindowsProcessDownloadIo.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace music_player::m0;

namespace {
class ObservedIo final : public DirectDownloadIo {
  public:
    WindowsProcessDownloadIo io;
    bool stopped_before_conversion{false};
    QByteArray transferred{};
    explicit ObservedIo(WindowsProcessDownloadIoConfig config) : io(std::move(config)) {}
    bool resolve(const QString& host) override { return io.resolve(host); }
    DownloadResolution resolution() const override { return io.resolution(); }
    bool open(const PinnedDownloadEndpoint& endpoint) override { return io.open(endpoint); }
    PinnedDownloadStreamUpdate poll() const override { return io.poll(); }
    QByteArray read(qint64 maximum) override {
        auto bytes = io.read(maximum);
        if (transferred.size() + bytes.size() <= 64 * 1024)
            transferred += bytes;
        return bytes;
    }
    void cancel() override { io.cancel(); }
    DownloadFailure failure() const override { return io.failure(); }
    DirectDownloadIoStopUpdate pollStop() override {
        const auto update = io.pollStop();
        if (update.stopped)
            stopped_before_conversion = true;
        return update;
    }
};
WindowsProcessDownloadIoConfig ioConfiguration(const QString& working_directory) {
    WindowsProcessDownloadIoConfig config;
    config.process.executable = QString::fromUtf8(RC_MUSIC_TEST_IO_HELPER_PATH);
    config.process.working_directory = working_directory;
    QProcessEnvironment environment;
    environment.insert("PATH", QFileInfo(QString::fromUtf8(RC_MUSIC_TEST_TLS_CORE_PATH)).absolutePath());
    environment.insert("SystemRoot", qEnvironmentVariable("SystemRoot"));
    config.process.environment = environment;
    config.runtime = {
        QString::fromUtf8(RC_MUSIC_TEST_TLS_CORE_PATH),        QByteArray(RC_MUSIC_TEST_TLS_CORE_SHA256),
        QString::fromUtf8(RC_MUSIC_TEST_TLS_NETWORK_PATH),     QByteArray(RC_MUSIC_TEST_TLS_NETWORK_SHA256),
        QDir::tempPath() + "/private-not-loaded-test-pin.dll", QByteArray(64, '0')};
    return config;
}
struct Harness final {
    QTemporaryDir directory{};
    QString staging{directory.path() + "/staging"};
    QString publication{directory.path() + "/published"};
    Harness() {
        QDir().mkpath(staging);
        QDir().mkpath(publication);
    }
    WindowsDownloadJobSessionConfig configuration() const {
        WindowsDownloadJobSessionConfig config;
        config.workspace = {staging, publication};
        config.converter = {QString::fromUtf8(RC_MUSIC_TEST_FFMPEG_PATH),
                            QString::fromUtf8(RC_MUSIC_TEST_FFPROBE_PATH)};
        return config;
    }
    QStringList jobs() const { return QDir(staging).entryList(QDir::Dirs | QDir::NoDotAndDotDot); }
    QStringList finals() const { return QDir(publication).entryList(QDir::Files); }
};
DownloadExecutionUpdate finish(WindowsDownloadJobSession& job, QElapsedTimer& clock,
                               const std::function<void(const DownloadExecutionUpdate&)>& observe = {}) {
    DownloadExecutionUpdate update;
    do {
        update = job.poll(clock.elapsed());
        if (observe)
            observe(update);
        if (update.state != DownloadExecutionState::Running)
            return update;
        QTest::qWait(5);
    } while (clock.elapsed() < 10000);
    job.cancel();
    do {
        update = job.poll(clock.elapsed());
        QTest::qWait(5);
    } while (update.state == DownloadExecutionState::Running && clock.elapsed() < 15000);
    return update;
}
} // namespace

class WindowsProcessDownloadJobTests final : public QObject {
    Q_OBJECT
  private slots:
    void framedChildBytesAreConvertedValidatedAndPublishedOnlyAfterNativeStop_data() {
        QTest::addColumn<QString>("suffix");
        QTest::addColumn<quint64>("bytes");
        QTest::newRow("MP3") << QStringLiteral("mp3") << quint64{24966};
        QTest::newRow("MP4 audio") << QStringLiteral("mp4") << quint64{33400};
    }
    void framedChildBytesAreConvertedValidatedAndPublishedOnlyAfterNativeStop() {
        QFETCH(QString, suffix);
        QFETCH(quint64, bytes);
        Harness h;
        QVERIFY(h.directory.isValid());
        const auto personal = h.publication + "/personal.mp3";
        QFile original(personal);
        QVERIFY(original.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        QCOMPARE(original.write("preserve-personal-file"), qint64{22});
        original.close();
        ObservedIo io(ioConfiguration(h.directory.path()));
        WindowsDownloadJobSession job(io, h.configuration());
        QElapsedTimer clock;
        clock.start();
        DownloadItem item{"https://media.example.test/synthetic-" + suffix.toStdString(), "../../personal.mp3 & cmd",
                          suffix == "mp4" ? DownloadMediaKind::Video : DownloadMediaKind::Audio};
        QVERIFY(job.start("job-1", item, clock.elapsed()));
        bool conversion_seen = false;
        const auto result = finish(job, clock, [&](const DownloadExecutionUpdate& update) {
            if (update.phase == DownloadPhase::Converting || update.phase == DownloadPhase::Publishing) {
                conversion_seen = true;
                QVERIFY(io.stopped_before_conversion);
            }
        });
        QCOMPARE(result.state, DownloadExecutionState::Published);
        QCOMPARE(result.error, DownloadFailure::None);
        QVERIFY(conversion_seen);
        const auto header_end = io.transferred.indexOf("\r\n\r\n");
        QVERIFY(header_end >= 0);
        const auto input = io.transferred.mid(header_end + 4);
        QCOMPARE(static_cast<quint64>(input.size()), bytes);
        QCOMPARE(QCryptographicHash::hash(input, QCryptographicHash::Sha256).toHex(),
                 suffix == "mp4" ? QByteArray{"aefe894a1a9277bf4795d421c97d4780fa342eef300819a459ca45329a614558"}
                                 : QByteArray{"65e66cf43d999e98cf37cb64003d29b374a1238e454eca138c0dc5be2b18c000"});
        QVERIFY(h.jobs().isEmpty());
        QCOMPARE(h.finals().size(), 2);
        const auto final = QString::fromStdWString(result.published_mp3.wstring());
        QVERIFY(final != personal);
        QVERIFY(QFileInfo(final).size() > 0);
        QVERIFY(final.endsWith(".mp3"));
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray{"preserve-personal-file"});
        QVERIFY(io.io.pollStop().stopped);
    }
    void childFailureOrCancellationNeverPublishes_data() {
        QTest::addColumn<QString>("host");
        QTest::addColumn<int>("expected");
        QTest::newRow("cancel") << QStringLiteral("stall.example.test") << int(DownloadFailure::None);
        QTest::newRow("child timeout") << QStringLiteral("timeout.example.test") << int(DownloadFailure::Timeout);
        QTest::newRow("protocol invalid")
            << QStringLiteral("missing-terminal.example.test") << int(DownloadFailure::InvalidResult);
        QTest::newRow("mixed DNS") << QStringLiteral("mixed.example.test") << int(DownloadFailure::Restricted);
    }
    void childFailureOrCancellationNeverPublishes() {
        QFETCH(QString, host);
        QFETCH(int, expected);
        Harness h;
        auto config = ioConfiguration(h.directory.path());
        if (host.startsWith("timeout"))
            config.process.total_timeout_ms = 80;
        ObservedIo io(config);
        WindowsDownloadJobSession job(io, h.configuration());
        QElapsedTimer clock;
        clock.start();
        QVERIFY(job.start("job-1",
                          {"https://" + host.toStdString() + "/synthetic-mp3", "private", DownloadMediaKind::Audio},
                          clock.elapsed()));
        if (expected == int(DownloadFailure::None))
            job.cancel();
        const auto result = finish(job, clock);
        QCOMPARE(result.state, expected == int(DownloadFailure::None) ? DownloadExecutionState::Cancelled
                                                                      : DownloadExecutionState::Failed);
        QCOMPARE(result.error, static_cast<DownloadFailure>(expected));
        QVERIFY(result.published_mp3.empty());
        QVERIFY(h.jobs().isEmpty());
        QVERIFY(h.finals().isEmpty());
        QVERIFY(io.stopped_before_conversion);
        QVERIFY(io.io.pollStop().stopped);
    }
};
QTEST_GUILESS_MAIN(WindowsProcessDownloadJobTests)
#include "WindowsProcessDownloadJobTests.moc"
