#include "WindowsLocalMediaConverter.hpp"
#include "WindowsLocalMp3Validator.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace music_player::m0;

namespace {
QString workerPath() { return QString::fromUtf8(RC_MUSIC_TEST_MF_WORKER_PATH); }

WindowsLocalMediaConverterConfig conversionConfig() {
    WindowsLocalMediaConverterConfig result;
    result.backend = WindowsLocalMediaBackend::MediaFoundation;
    result.media_foundation_executable = workerPath();
    return result;
}

WindowsLocalMp3ValidatorConfig validationConfig() {
    WindowsLocalMp3ValidatorConfig result;
    result.backend = WindowsLocalMediaBackend::MediaFoundation;
    result.media_foundation_executable = workerPath();
    return result;
}

QByteArray fixture(const QString& name) {
    QFile input(QString::fromUtf8(RC_MUSIC_TEST_AUDIO_FIXTURES) + QLatin1Char('/') + name);
    if (!input.open(QIODevice::ReadOnly))
        return {};
    const auto bytes = input.readAll();
    QByteArray hash;
    qint64 size = 0;
    if (name == QStringLiteral("mp3-unicode-tags.mp3")) {
        hash = "65e66cf43d999e98cf37cb64003d29b374a1238e454eca138c0dc5be2b18c000";
        size = 24'966;
    } else if (name == QStringLiteral("m4a-aac-lc.m4a")) {
        hash = "aefe894a1a9277bf4795d421c97d4780fa342eef300819a459ca45329a614558";
        size = 33'400;
    } else if (name == QStringLiteral("wav-pcm-mono.wav")) {
        hash = "03338fdac9010c786b2e1849b27565545208254d51bd6ae611f1936ff0fab04f";
        size = 96'044;
    }
    return bytes.size() == size && QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() == hash
               ? bytes
               : QByteArray{};
}

bool write(const QString& path, const QByteArray& bytes) {
    QFile output(path);
    return !bytes.isEmpty() && output.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
           output.write(bytes) == bytes.size() && output.flush();
}

QByteArray read(const QString& path) {
    QFile input(path);
    return input.open(QIODevice::ReadOnly) ? input.readAll() : QByteArray{};
}

struct Workspace final {
    QTemporaryDir directory{QDir::tempPath() + QStringLiteral("/RC Music MF convert 日本-XXXXXX")};
    QString staging{directory.filePath(QStringLiteral("staging"))};
    QString publication{directory.filePath(QStringLiteral("published"))};
    WindowsDownloadWorkspace workspace;
    explicit Workspace(const quint64 quota = 512ULL * 1024 * 1024) : workspace({staging, publication, quota}) {
        QDir().mkpath(staging);
        QDir().mkpath(publication);
    }
    bool prepare(const QByteArray& bytes) {
        return directory.isValid() && !bytes.isEmpty() && workspace.create() && workspace.inputSink() &&
               workspace.inputSink()->write(bytes) == bytes.size() && workspace.sealInput();
    }
};

LocalMediaConversionUpdate finish(WindowsLocalMediaConverter& converter, const qint64 fixed_now = -1,
                                  bool* saw_validation = nullptr) {
    QElapsedTimer timer;
    timer.start();
    LocalMediaConversionUpdate result;
    do {
        result = converter.poll(fixed_now < 0 ? timer.elapsed() : fixed_now);
        if (saw_validation && result.state == LocalMediaConversionState::Validating)
            *saw_validation = true;
        if (result.state == LocalMediaConversionState::Validated || result.state == LocalMediaConversionState::Failed ||
            result.state == LocalMediaConversionState::Cancelled)
            return result;
        QTest::qWait(5);
    } while (timer.elapsed() < 15'000);
    return result;
}

LocalMp3ValidationUpdate finish(WindowsLocalMp3Validator& validator, const qint64 fixed_now = -1) {
    QElapsedTimer timer;
    timer.start();
    LocalMp3ValidationUpdate result;
    do {
        result = validator.poll(fixed_now < 0 ? timer.elapsed() : fixed_now);
        if (result.state == LocalMp3ValidationState::Validated || result.state == LocalMp3ValidationState::Failed ||
            result.state == LocalMp3ValidationState::Cancelled)
            return result;
        QTest::qWait(5);
    } while (timer.elapsed() < 15'000);
    return result;
}
} // namespace

class WindowsMediaFoundationConversionTests final : public QObject {
    Q_OBJECT

  private slots:
    void legacyAggregateConfigurationsKeepTheirBackend() {
        const WindowsLocalMediaConverterConfig conversion{QStringLiteral("ffmpeg"), QStringLiteral("ffprobe"), 1, 2};
        const WindowsLocalMp3ValidatorConfig validation{QStringLiteral("ffmpeg"), QStringLiteral("ffprobe"), 1, 2};
        QCOMPARE(conversion.backend, WindowsLocalMediaBackend::FfmpegLegacy);
        QCOMPARE(validation.backend, WindowsLocalMediaBackend::FfmpegLegacy);
        QVERIFY(conversion.media_foundation_executable.isEmpty());
        QVERIFY(validation.media_foundation_executable.isEmpty());
    }

    void convertsLocalFixturesAndValidatesInASeparateWorker_data() {
        QTest::addColumn<QString>("name");
        QTest::newRow("MP3") << QStringLiteral("mp3-unicode-tags.mp3");
        QTest::newRow("AAC M4A") << QStringLiteral("m4a-aac-lc.m4a");
        QTest::newRow("PCM WAV") << QStringLiteral("wav-pcm-mono.wav");
    }

    void convertsLocalFixturesAndValidatesInASeparateWorker() {
        QFETCH(QString, name);
        Workspace owned;
        const auto bytes = fixture(name);
        QVERIFY(owned.prepare(bytes));
        WindowsLocalMediaConverter converter{conversionConfig()};
        QVERIFY(converter.start(owned.workspace, 0));
        const auto first = converter.poll(0).state;
        QVERIFY(first == LocalMediaConversionState::Converting || first == LocalMediaConversionState::Validating);
        bool saw_validation = first == LocalMediaConversionState::Validating;
        const auto converted = finish(converter, -1, &saw_validation);
        QCOMPARE(converted.error, DownloadFailure::None);
        QCOMPARE(converted.state, LocalMediaConversionState::Validated);
        QVERIFY(converted.processed_us > 0);
        QVERIFY(saw_validation);
        const auto output = owned.workspace.outputPath();
        QVERIFY(!output.isEmpty());
        QVERIFY(QFileInfo(output).size() > 0);
        QCOMPARE(read(owned.workspace.inputPath()), bytes);
        QVERIFY(QDir(owned.publication).entryList(QDir::Files).isEmpty());
        WindowsLocalMp3Validator validator{validationConfig()};
        QVERIFY(validator.start(output, 0));
        const auto validated = finish(validator);
        QCOMPARE(validated.state, LocalMp3ValidationState::Validated);
        QCOMPARE(validated.error, DownloadFailure::None);
        QVERIFY(QDir(owned.publication).entryList(QDir::Files).isEmpty());
    }

    void validatesUnicodeMp3WithoutChangingItsBytes() {
        QTemporaryDir directory{QDir::tempPath() + QStringLiteral("/RC Music MF validate 日本-XXXXXX")};
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("áudio 日本.mp3"));
        const auto bytes = fixture(QStringLiteral("mp3-unicode-tags.mp3"));
        QVERIFY(write(path, bytes));
        WindowsLocalMp3Validator validator{validationConfig()};
        QVERIFY(validator.start(path, 0));
        QVERIFY(!validator.start(path, 0));
        QCOMPARE(finish(validator).state, LocalMp3ValidationState::Validated);
        QCOMPARE(read(path), bytes);
        QCOMPARE(QDir(directory.path()).entryList(QDir::Files), QStringList{QStringLiteral("áudio 日本.mp3")});
    }

    void rejectsInvalidInputWithoutASealedOutput() {
        Workspace owned;
        QVERIFY(owned.prepare("not an audio container"));
        WindowsLocalMediaConverter converter{conversionConfig()};
        QVERIFY(converter.start(owned.workspace, 0));
        const auto result = finish(converter);
        QCOMPARE(result.state, LocalMediaConversionState::Failed);
        QCOMPARE(result.error, DownloadFailure::UnsupportedMedia);
        QVERIFY(owned.workspace.outputPath().isEmpty());
        QVERIFY(QDir(owned.publication).entryList(QDir::Files).isEmpty());
    }

    void reportsNoAudioForAnMp4WithoutAudioHandler() {
        auto bytes = fixture(QStringLiteral("m4a-aac-lc.m4a"));
        const auto handler = bytes.indexOf("soun");
        QVERIFY(handler > 0);
        bytes.replace(handler, 4, "vide");
        Workspace owned;
        QVERIFY(owned.prepare(bytes));
        WindowsLocalMediaConverter converter{conversionConfig()};
        QVERIFY(converter.start(owned.workspace, 0));
        const auto result = finish(converter);
        QCOMPARE(result.state, LocalMediaConversionState::Failed);
        QCOMPARE(result.error, DownloadFailure::NoAudio);
        QVERIFY(owned.workspace.outputPath().isEmpty());
        QCOMPARE(owned.workspace.outputSink()->size(), qint64{0});
    }

    void validatorRejectsAacDisguisedAsMp3() {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("candidate.mp3"));
        const auto bytes = fixture(QStringLiteral("m4a-aac-lc.m4a"));
        QVERIFY(write(path, bytes));
        WindowsLocalMp3Validator validator{validationConfig()};
        QVERIFY(validator.start(path, 0));
        const auto result = finish(validator);
        QCOMPARE(result.state, LocalMp3ValidationState::Failed);
        QCOMPARE(result.error, DownloadFailure::UnsupportedMedia);
        QCOMPARE(read(path), bytes);
    }

    void missingWorkerNeverFallsBackToTheLegacyFields() {
        Workspace owned;
        QVERIFY(owned.prepare(fixture(QStringLiteral("mp3-unicode-tags.mp3"))));
        auto conversion = conversionConfig();
        conversion.ffmpeg_executable = conversion.ffprobe_executable = workerPath();
        conversion.media_foundation_executable = owned.directory.filePath(QStringLiteral("absent-worker.exe"));
        WindowsLocalMediaConverter converter{conversion};
        QVERIFY(!converter.start(owned.workspace, 0));
        QCOMPARE(converter.poll(0).state, LocalMediaConversionState::Failed);
        QCOMPARE(converter.poll(0).error, DownloadFailure::Unavailable);
        auto validation = validationConfig();
        validation.ffmpeg_executable = validation.ffprobe_executable = workerPath();
        validation.media_foundation_executable = conversion.media_foundation_executable;
        WindowsLocalMp3Validator validator{validation};
        QVERIFY(!validator.start(owned.workspace.inputPath(), 0));
        QCOMPARE(validator.poll(0).error, DownloadFailure::Unavailable);
    }

    void sharedWorkspaceQuotaStopsConversionAndNeverPublishes() {
        const auto bytes = fixture(QStringLiteral("m4a-aac-lc.m4a"));
        Workspace owned{static_cast<quint64>(bytes.size()) + 16};
        QVERIFY(owned.prepare(bytes));
        WindowsLocalMediaConverter converter{conversionConfig()};
        QVERIFY(converter.start(owned.workspace, 0));
        const auto result = finish(converter);
        QCOMPARE(result.state, LocalMediaConversionState::Failed);
        QCOMPARE(result.error, DownloadFailure::LimitExceeded);
        QVERIFY(owned.workspace.outputPath().isEmpty());
        QVERIFY(QDir(owned.publication).entryList(QDir::Files).isEmpty());
    }

    void conversionDeadlineIsInclusiveAndAcknowledgesDrain() {
        Workspace owned;
        QVERIFY(owned.prepare(fixture(QStringLiteral("wav-pcm-mono.wav"))));
        auto config = conversionConfig();
        config.total_timeout_ms = 5;
        WindowsLocalMediaConverter converter{config};
        QVERIFY(converter.start(owned.workspace, 0));
        const auto result = finish(converter, 5);
        QCOMPARE(result.state, LocalMediaConversionState::Failed);
        QCOMPARE(result.error, DownloadFailure::Timeout);
        QVERIFY(owned.workspace.outputPath().isEmpty());
    }

    void cancellingConversionWaitsForTheOwnedWorker() {
        Workspace owned;
        QVERIFY(owned.prepare(fixture(QStringLiteral("wav-pcm-mono.wav"))));
        WindowsLocalMediaConverter converter{conversionConfig()};
        QVERIFY(converter.start(owned.workspace, 0));
        converter.cancel();
        const auto observed = converter.poll(0).state;
        QVERIFY(observed == LocalMediaConversionState::Stopping || observed == LocalMediaConversionState::Cancelled);
        const auto result = finish(converter, 0);
        QCOMPARE(result.state, LocalMediaConversionState::Cancelled);
        QCOMPARE(result.error, DownloadFailure::None);
        QVERIFY(owned.workspace.outputPath().isEmpty());
    }

    void validatorDeadlineAndCancellationPreserveCandidate_data() {
        QTest::addColumn<bool>("cancel");
        QTest::newRow("inclusive timeout") << false;
        QTest::newRow("cancellation") << true;
    }

    void validatorDeadlineAndCancellationPreserveCandidate() {
        QFETCH(bool, cancel);
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("candidate.mp3"));
        const auto bytes = fixture(QStringLiteral("mp3-unicode-tags.mp3"));
        QVERIFY(write(path, bytes));
        auto config = validationConfig();
        config.total_timeout_ms = 5;
        WindowsLocalMp3Validator validator{config};
        QVERIFY(validator.start(path, 0));
        if (cancel)
            validator.cancel();
        const auto result = finish(validator, cancel ? 0 : 5);
        QCOMPARE(result.state, cancel ? LocalMp3ValidationState::Cancelled : LocalMp3ValidationState::Failed);
        QCOMPARE(result.error, cancel ? DownloadFailure::None : DownloadFailure::Timeout);
        QCOMPARE(read(path), bytes);
    }
};

QTEST_GUILESS_MAIN(WindowsMediaFoundationConversionTests)
#include "WindowsMediaFoundationConversionTests.moc"
