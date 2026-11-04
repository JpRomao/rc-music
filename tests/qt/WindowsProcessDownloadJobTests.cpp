#include "WindowsDownloadExecution.hpp"
#include "WindowsDownloadJobSession.hpp"
#include "WindowsProcessDownloadIo.hpp"
#include "m0/application/DownloadService.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <vector>

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
bool waitFor(const std::function<bool()>& condition) {
    QElapsedTimer watchdog;
    watchdog.start();
    do {
        if (condition())
            return true;
        QTest::qWait(5);
    } while (watchdog.elapsed() < 10000);
    return condition();
}
struct DrainOnExit final {
    DownloadService& service;
    WindowsDownloadExecution& execution;
    QElapsedTimer& clock;
    ~DrainOnExit() {
        service.shutdown();
        static_cast<void>(waitFor([&] {
            service.tick(clock.elapsed());
            return service.stopped();
        }));
        execution.shutdown();
        static_cast<void>(waitFor([&] { return execution.stopped(); }));
    }
};
class ObservedImport final : public DownloadImport {
  public:
    struct Call final {
        std::string job_id;
        std::filesystem::path path;
        DownloadExecutionUpdate update;
        bool staging_empty;
        bool final_exists;
    };
    std::vector<Call> calls;
    explicit ObservedImport(WindowsDownloadExecution& execution, const Harness& files)
        : execution_(execution), files_(files) {}
    bool accept(const std::string& job_id, const std::filesystem::path& path) override {
        calls.push_back({job_id, path, execution_.poll(job_id), files_.jobs().isEmpty(),
                         QFileInfo(QString::fromStdWString(path.wstring())).isFile()});
        return true;
    }

  private:
    WindowsDownloadExecution& execution_;
    const Harness& files_;
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
    void asynchronousExecutionPublishesMp3AndMp4AndServiceImportsEachJobOnlyOnce_data() {
        QTest::addColumn<bool>("native_conversion");
        QTest::newRow("legacy trusted-fixture CLI") << false;
        QTest::newRow("native Media Foundation without converter CLI") << true;
    }
    void asynchronousExecutionPublishesMp3AndMp4AndServiceImportsEachJobOnlyOnce() {
        QFETCH(bool, native_conversion);
        Harness h;
        QVERIFY(h.directory.isValid());
        // Only the framed synthetic helper transfers bytes. This exercises the real
        // async port, native processes, filesystem and converter, not public TLS or
        // the still-open pre-launch authentication/tool-isolation/player gates.
        auto configuration = h.configuration();
        if (native_conversion) {
            configuration.converter = {};
            configuration.converter.backend = WindowsLocalMediaBackend::MediaFoundation;
            configuration.converter.media_foundation_executable = QString::fromUtf8(RC_MUSIC_TEST_MF_WORKER_PATH);
        }
        WindowsDownloadExecution execution(ioConfiguration(h.directory.path()), configuration);
        ObservedImport importer(execution, h);
        DownloadService service(execution, importer, "real-composition");
        QElapsedTimer clock;
        clock.start();
        DrainOnExit drain{service, execution, clock};
        const DownloadRequest request{
            "abcdefab-cdef-4abc-8def-000000000001",
            {{"https://media.example.test/synthetic-mp3", "Synthetic MP3", DownloadMediaKind::Audio},
             {"https://media.example.test/synthetic-mp4", "Synthetic MP4 audio", DownloadMediaKind::Video}}};
        const auto receipt = service.submit(request);
        QCOMPARE(receipt.error, DownloadSubmissionError::None);
        QCOMPARE(receipt.job_ids.size(), std::size_t{2});
        QVERIFY(h.jobs().isEmpty());
        QVERIFY(h.finals().isEmpty());
        QVERIFY(importer.calls.empty());
        QCOMPARE(service.submit(request).job_ids, receipt.job_ids);
        QVERIFY(waitFor([&] {
            service.tick(clock.elapsed());
            const auto jobs = service.snapshot();
            return std::all_of(jobs.begin(), jobs.end(), [](const DownloadJobSnapshot& job) {
                return job.state == DownloadJobState::Completed || job.state == DownloadJobState::ImportFailed ||
                       job.state == DownloadJobState::Failed || job.state == DownloadJobState::Cancelled;
            });
        }));
        const auto jobs = service.snapshot();
        QCOMPARE(jobs.size(), std::size_t{2});
        QCOMPARE(importer.calls.size(), std::size_t{2});
        QVERIFY(jobs[0].completed_mp3 != jobs[1].completed_mp3);
        for (std::size_t index = 0; index < jobs.size(); ++index) {
            const auto& job = jobs[index];
            const auto& imported = importer.calls[index];
            QCOMPARE(job.job_id, receipt.job_ids[index]);
            QCOMPARE(job.state, DownloadJobState::Completed);
            QCOMPARE(job.error, DownloadFailure::None);
            QVERIFY(job.completed_mp3.is_absolute());
            QVERIFY(job.completed_mp3 == job.completed_mp3.lexically_normal());
            QCOMPARE(imported.job_id, job.job_id);
            QVERIFY(imported.path == job.completed_mp3);
            QCOMPARE(imported.update.state, DownloadExecutionState::Published);
            QCOMPARE(imported.update.phase, DownloadPhase::Publishing);
            QCOMPARE(imported.update.error, DownloadFailure::None);
            QVERIFY(imported.update.published_mp3 == imported.path);
            QVERIFY(imported.staging_empty);
            QVERIFY(imported.final_exists);
            const QFileInfo final(QString::fromStdWString(job.completed_mp3.wstring()));
            QCOMPARE(final.suffix(), QStringLiteral("mp3"));
            QCOMPARE(final.absolutePath(), QFileInfo(h.publication).absoluteFilePath());
            QVERIFY(final.size() > 0);
            QVERIFY(!service.retryImport(job.job_id));
        }
        QVERIFY(h.jobs().isEmpty());
        QCOMPARE(h.finals().size(), 2);
        QVERIFY(execution.retainedPublicationPath(receipt.job_ids[1]) == jobs[1].completed_mp3);
        auto replay = request;
        replay.request_id = "ABCDEFAB-CDEF-4ABC-8DEF-000000000001";
        const auto repeated = service.submit(replay);
        QCOMPARE(repeated.error, DownloadSubmissionError::None);
        QCOMPARE(repeated.job_ids, receipt.job_ids);
        for (int index = 0; index < 5; ++index) {
            service.tick(clock.elapsed());
            QTest::qWait(5);
        }
        QCOMPARE(importer.calls.size(), std::size_t{2});
        QCOMPARE(h.finals().size(), 2);
        service.shutdown();
        QVERIFY(service.stopped());
        execution.shutdown();
        QVERIFY(waitFor([&] { return execution.stopped(); }));
        QVERIFY(!execution.start("after-shutdown", request.items[0]));
    }
    void serviceShutdownDrainsTheRealAsyncJobWithoutImportOrStartingItsQueuedSuccessor() {
        Harness h;
        QVERIFY(h.directory.isValid());
        auto io = ioConfiguration(h.directory.path());
        io.process.total_timeout_ms = 5000; // cleanup watchdog even when an assertion fails
        WindowsDownloadExecution execution(std::move(io), h.configuration());
        ObservedImport importer(execution, h);
        DownloadService service(execution, importer, "real-cancel");
        QElapsedTimer clock;
        clock.start();
        DrainOnExit drain{service, execution, clock};
        const DownloadRequest request{
            "abcdefab-cdef-4abc-8def-000000000002",
            {{"https://stall.example.test/synthetic-mp3", "Stalled synthetic", DownloadMediaKind::Audio},
             {"https://media.example.test/synthetic-mp4", "Never started", DownloadMediaKind::Video}}};
        const auto receipt = service.submit(request);
        QCOMPARE(receipt.error, DownloadSubmissionError::None);
        QCOMPARE(receipt.job_ids.size(), std::size_t{2});
        service.tick(clock.elapsed());
        QVERIFY(waitFor([&] {
            service.tick(clock.elapsed());
            return !h.jobs().isEmpty();
        }));
        QVERIFY(importer.calls.empty());
        service.shutdown();
        QVERIFY(!service.stopped()); // cancel is a request; only terminal observation drains
        const auto cancelling = service.snapshot();
        QCOMPARE(cancelling[0].state, DownloadJobState::Cancelling);
        QCOMPARE(cancelling[1].state, DownloadJobState::Cancelled);
        QVERIFY(waitFor([&] {
            service.tick(clock.elapsed());
            return service.stopped();
        }));
        execution.shutdown();
        QVERIFY(waitFor([&] { return execution.stopped(); }));
        const auto jobs = service.snapshot();
        QCOMPARE(jobs.size(), std::size_t{2});
        for (const auto& job : jobs) {
            QCOMPARE(job.state, DownloadJobState::Cancelled);
            QCOMPARE(job.error, DownloadFailure::None);
            QVERIFY(job.completed_mp3.empty());
        }
        QCOMPARE(execution.poll(receipt.job_ids[0]).state, DownloadExecutionState::Cancelled);
        QVERIFY(execution.retainedPublicationPath(receipt.job_ids[0]).empty());
        QVERIFY(importer.calls.empty());
        QVERIFY(h.jobs().isEmpty());
        QVERIFY(h.finals().isEmpty());
        QCOMPARE(service.submit(request).error, DownloadSubmissionError::NotAvailable);
    }
};
QTEST_GUILESS_MAIN(WindowsProcessDownloadJobTests)
#include "WindowsProcessDownloadJobTests.moc"
