#include "WindowsLocalMediaConverter.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include <Windows.h>

using namespace music_player::m0;

namespace {
WindowsLocalMediaConverterConfig configuration() {
    return {QString::fromUtf8(RC_MUSIC_TEST_FFMPEG_PATH), QString::fromUtf8(RC_MUSIC_TEST_FFPROBE_PATH)};
}
QByteArray fixture(const QString& leaf) {
    QFile file(QString::fromUtf8(RC_MUSIC_TEST_AUDIO_FIXTURES) + '/' + leaf);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const auto bytes = file.readAll();
    const auto expected = leaf == "m4a-aac-lc.m4a"
                              ? QByteArray("aefe894a1a9277bf4795d421c97d4780fa342eef300819a459ca45329a614558")
                              : QByteArray("65e66cf43d999e98cf37cb64003d29b374a1238e454eca138c0dc5be2b18c000");
    const auto expected_size = leaf == "m4a-aac-lc.m4a" ? 33400 : 24966;
    return bytes.size() == expected_size &&
                   QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() == expected
               ? bytes
               : QByteArray{};
}
struct Harness {
    QTemporaryDir directory{QDir::tempPath() + QStringLiteral("/RC Music convert 日本-XXXXXX")};
    QString staging{directory.path() + "/staging"};
    QString publication{directory.path() + "/published"};
    WindowsDownloadWorkspace workspace;
    explicit Harness(quint64 quota = 512 * 1024 * 1024) : workspace({staging, publication, quota}) {
        QDir().mkpath(staging);
        QDir().mkpath(publication);
    }
    bool prepare(const QByteArray& bytes) {
        return !bytes.isEmpty() && workspace.create() && workspace.inputSink() &&
               workspace.inputSink()->write(bytes) == bytes.size() && workspace.sealInput();
    }
};
LocalMediaConversionUpdate finish(WindowsLocalMediaConverter& converter) {
    QElapsedTimer clock;
    clock.start();
    LocalMediaConversionUpdate result;
    do {
        result = converter.poll(clock.elapsed());
        if (result.state == LocalMediaConversionState::Failed || result.state == LocalMediaConversionState::Cancelled ||
            result.state == LocalMediaConversionState::Validated)
            return result;
        QTest::qWait(5);
    } while (clock.elapsed() < 15000);
    return result;
}
} // namespace

class WindowsLocalMediaConverterTests final : public QObject {
    Q_OBJECT
  private slots:
    void startsIdleAndDoesNotCreateAProcessForUnsealedInput() {
        WindowsLocalMediaConverter converter(configuration());
        QCOMPARE(converter.poll(0).state, LocalMediaConversionState::Idle);
        Harness h;
        QVERIFY(h.workspace.create());
        QVERIFY(!converter.start(h.workspace, 0));
    }
    void convertsAndFullyValidatesSyntheticLocalAudio_data() {
        QTest::addColumn<QString>("input");
        QTest::newRow("M4A AAC audio in MP4") << QStringLiteral("m4a-aac-lc.m4a");
        QTest::newRow("MP3 with Unicode metadata") << QStringLiteral("mp3-unicode-tags.mp3");
    }
    void convertsAndFullyValidatesSyntheticLocalAudio() {
        QFETCH(QString, input);
        Harness h;
        QVERIFY(h.prepare(fixture(input)));
        WindowsLocalMediaConverter converter(configuration());
        QVERIFY(converter.start(h.workspace, 0));
        const auto result = finish(converter);
        QCOMPARE(result.state, LocalMediaConversionState::Validated);
        QCOMPARE(result.error, DownloadFailure::None);
        QVERIFY(result.processed_us > 0);
        QVERIFY(result.duration_us.has_value());
        QVERIFY(!h.workspace.outputPath().isEmpty());
        QVERIFY(QDir(h.publication).entryList(QDir::Files).isEmpty());
        const auto published = h.workspace.validateAndPublish(
            [&](const QString&) { return converter.poll(15000).state == LocalMediaConversionState::Validated; });
        QVERIFY(!published.isEmpty());
        QVERIFY(published.endsWith(".mp3"));
        QVERIFY(QFileInfo(published).size() > 0);
        QCOMPARE(QDir(h.publication).entryList(QDir::Files).size(), 1);
    }
    void invalidInputCannotCreateAnMp3OrReportValidation() {
        Harness h;
        QVERIFY(h.prepare("this is not an audio container"));
        WindowsLocalMediaConverter converter(configuration());
        QVERIFY(converter.start(h.workspace, 0));
        const auto result = finish(converter);
        QCOMPARE(result.state, LocalMediaConversionState::Failed);
        QCOMPARE(result.error, DownloadFailure::UnsupportedMedia);
        QVERIFY(h.workspace.outputPath().isEmpty());
        QVERIFY(QDir(h.publication).entryList(QDir::Files).isEmpty());
    }
    void mp4WithoutAnAudioHandlerReportsNoAudioWithoutOutput() {
        auto input = fixture("m4a-aac-lc.m4a");
        const auto handler = input.indexOf("soun");
        QVERIFY(handler > 0);
        // A derived trusted local container, not a replacement for the corpus:
        // change its one handler to video so selection a:0 is empty.
        input.replace(handler, 4, "vide");
        Harness h;
        QVERIFY(h.prepare(input));
        WindowsLocalMediaConverter converter(configuration());
        QVERIFY(converter.start(h.workspace, 0));
        const auto result = finish(converter);
        QCOMPARE(result.state, LocalMediaConversionState::Failed);
        QCOMPARE(result.error, DownloadFailure::NoAudio);
        QVERIFY(h.workspace.outputPath().isEmpty());
        QCOMPARE(h.workspace.outputSink()->size(), 0);
    }
    void backwardClockFailsAndDoesNotPublish() {
        Harness h;
        QVERIFY(h.prepare(fixture("m4a-aac-lc.m4a")));
        WindowsLocalMediaConverter converter(configuration());
        QVERIFY(converter.start(h.workspace, 0));
        converter.poll(-1);
        const auto result = finish(converter);
        QCOMPARE(result.state, LocalMediaConversionState::Failed);
        QCOMPARE(result.error, DownloadFailure::InvalidResult);
        QVERIFY(QDir(h.publication).entryList(QDir::Files).isEmpty());
    }
    void cancelStopsTheOwnedProcessBeforeAcknowledging() {
        Harness h;
        QVERIFY(h.prepare(fixture("m4a-aac-lc.m4a")));
        WindowsLocalMediaConverter converter(configuration());
        QVERIFY(converter.start(h.workspace, 0));
        converter.cancel();
        const auto result = finish(converter);
        QCOMPARE(result.state, LocalMediaConversionState::Cancelled);
        QVERIFY(h.workspace.outputPath().isEmpty());
        QVERIFY(QDir(h.publication).entryList(QDir::Files).isEmpty());
    }
    void cancellationDuringDelegatedValidationWaitsForTheDecoderToExit() {
        Harness h;
        QVERIFY(h.prepare(fixture("m4a-aac-lc.m4a")));
        const auto helper = h.directory.filePath("conversion-validation-hang.exe");
        QVERIFY(QFile::copy(QString::fromUtf8(RC_MUSIC_TEST_CONVERTER_HELPER_PATH), helper));
        auto config = configuration();
        config.ffmpeg_executable = helper;
        WindowsLocalMediaConverter converter(config);
        QVERIFY(converter.start(h.workspace, 0));
        QElapsedTimer clock;
        clock.start();
        QString marker;
        LocalMediaConversionUpdate result;
        do {
            result = converter.poll(clock.elapsed());
            if (!h.workspace.outputPath().isEmpty())
                marker = QFileInfo(h.workspace.outputPath()).absolutePath() + "/validation-started";
            QTest::qWait(5);
        } while ((marker.isEmpty() || !QFile::exists(marker)) && clock.elapsed() < 5000);
        QVERIFY(!marker.isEmpty());
        QVERIFY(QFile::exists(marker));
        QCOMPARE(result.state, LocalMediaConversionState::Validating);
        QFile started(marker);
        QVERIFY(started.open(QIODevice::ReadOnly));
        bool parsed = false;
        const auto pid = started.readAll().toULong(&parsed);
        QVERIFY(parsed && pid > 0);
        const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
        QVERIFY(process != nullptr);
        const auto close_process = qScopeGuard([process] { CloseHandle(process); });
        QCOMPARE(WaitForSingleObject(process, 0), DWORD{WAIT_TIMEOUT});
        converter.cancel();
        do {
            result = converter.poll(clock.elapsed());
            QTest::qWait(5);
        } while (result.state == LocalMediaConversionState::Stopping && clock.elapsed() < 10000);
        QCOMPARE(result.state, LocalMediaConversionState::Cancelled);
        QCOMPARE(result.error, DownloadFailure::None);
        QCOMPARE(WaitForSingleObject(process, 0), DWORD{WAIT_OBJECT_0});
        QVERIFY(!h.workspace.outputPath().isEmpty());
        QVERIFY(QDir(h.publication).entryList(QDir::Files).isEmpty());
    }
    void aggregateDiskQuotaStopsConversionWithoutPublication() {
        const auto input = fixture("m4a-aac-lc.m4a");
        Harness h(static_cast<quint64>(input.size()) + 128);
        QVERIFY(h.prepare(input));
        WindowsLocalMediaConverter converter(configuration());
        QVERIFY(converter.start(h.workspace, 0));
        const auto result = finish(converter);
        QCOMPARE(result.state, LocalMediaConversionState::Failed);
        QCOMPARE(result.error, DownloadFailure::LimitExceeded);
        QVERIFY(h.workspace.outputSink()->size() <= 128);
        QVERIFY(QDir(h.publication).entryList(QDir::Files).isEmpty());
    }
    void deadlineDoesNotResetBetweenProbeConversionAndValidation() {
        Harness h;
        QVERIFY(h.prepare(fixture("m4a-aac-lc.m4a")));
        auto config = configuration();
        config.total_timeout_ms = 5;
        WindowsLocalMediaConverter converter(config);
        QVERIFY(converter.start(h.workspace, 0));
        converter.poll(5);
        LocalMediaConversionUpdate result;
        QElapsedTimer clock;
        clock.start();
        do {
            result = converter.poll(5);
            QTest::qWait(5);
        } while (result.state == LocalMediaConversionState::Stopping && clock.elapsed() < 5000);
        QCOMPARE(result.state, LocalMediaConversionState::Failed);
        QCOMPARE(result.error, DownloadFailure::Timeout);
    }
    void validMp3AndSuccessfulConversionDoNotWaiveFullDecode() {
        Harness h;
        QVERIFY(h.prepare(fixture("m4a-aac-lc.m4a")));
        auto config = configuration();
        config.ffmpeg_executable = QString::fromUtf8(RC_MUSIC_TEST_CONVERTER_HELPER_PATH);
        WindowsLocalMediaConverter converter(config);
        QVERIFY(converter.start(h.workspace, 0));
        const auto result = finish(converter);
        QCOMPARE(result.state, LocalMediaConversionState::Failed);
        QCOMPARE(result.error, DownloadFailure::Conversion);
        QVERIFY(!h.workspace.outputPath().isEmpty()); // Valid MP3 probe succeeded, decoder refused.
        QVERIFY(QDir(h.publication).entryList(QDir::Files).isEmpty());
    }
    void totalDeadlineIsSharedAfterInputProbeFinishes() {
        Harness h;
        QVERIFY(h.prepare(fixture("m4a-aac-lc.m4a")));
        const auto helper = h.directory.filePath("conversion-hang.exe");
        QVERIFY(QFile::copy(QString::fromUtf8(RC_MUSIC_TEST_CONVERTER_HELPER_PATH), helper));
        auto config = configuration();
        config.ffmpeg_executable = helper;
        config.total_timeout_ms = 100;
        WindowsLocalMediaConverter converter(config);
        QVERIFY(converter.start(h.workspace, 0));
        LocalMediaConversionUpdate result;
        QElapsedTimer clock;
        clock.start();
        do {
            result = converter.poll(75);
            QTest::qWait(5);
        } while (result.state == LocalMediaConversionState::Inspecting && clock.elapsed() < 5000);
        QCOMPARE(result.state, LocalMediaConversionState::Converting);
        do {
            result = converter.poll(100);
            QTest::qWait(5);
        } while (result.state == LocalMediaConversionState::Stopping && clock.elapsed() < 5000);
        QCOMPARE(result.state, LocalMediaConversionState::Failed);
        QCOMPARE(result.error, DownloadFailure::Timeout);
        QVERIFY(QDir(h.publication).entryList(QDir::Files).isEmpty());
    }
};

QTEST_GUILESS_MAIN(WindowsLocalMediaConverterTests)
#include "WindowsLocalMediaConverterTests.moc"
