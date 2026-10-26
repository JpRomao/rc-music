#include "WindowsLocalMp3Validator.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace music_player::m0;

namespace {
WindowsLocalMp3ValidatorConfig configuration() {
    return {QString::fromUtf8(RC_MUSIC_TEST_FFMPEG_PATH), QString::fromUtf8(RC_MUSIC_TEST_FFPROBE_PATH)};
}
QByteArray fixture(bool mp3 = true) {
    QFile input(QString::fromUtf8(RC_MUSIC_TEST_AUDIO_FIXTURES) + (mp3 ? "/mp3-unicode-tags.mp3" : "/m4a-aac-lc.m4a"));
    if (!input.open(QIODevice::ReadOnly))
        return {};
    const auto bytes = input.readAll();
    const auto hash = mp3 ? "65e66cf43d999e98cf37cb64003d29b374a1238e454eca138c0dc5be2b18c000"
                          : "aefe894a1a9277bf4795d421c97d4780fa342eef300819a459ca45329a614558";
    return bytes.size() == (mp3 ? 24966 : 33400) &&
                   QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() == hash
               ? bytes
               : QByteArray{};
}
bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return !bytes.isEmpty() && file.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
           file.write(bytes) == bytes.size() && file.flush();
}
QByteArray read(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
LocalMp3ValidationUpdate finish(WindowsLocalMp3Validator& validator, qint64 now_ms = -1) {
    QElapsedTimer clock;
    clock.start();
    LocalMp3ValidationUpdate result;
    do {
        result = validator.poll(now_ms < 0 ? clock.elapsed() : now_ms);
        if (result.state == LocalMp3ValidationState::Validated || result.state == LocalMp3ValidationState::Failed ||
            result.state == LocalMp3ValidationState::Cancelled)
            return result;
        QTest::qWait(5);
    } while (clock.elapsed() < 10000);
    return result;
}
} // namespace

class WindowsLocalMp3ValidatorTests final : public QObject {
    Q_OBJECT
  private slots:
    void rejectsNonlocalInputWithoutStartingOrChangingIdle() {
        for (const auto& path : {QString{}, QStringLiteral("relative.mp3"),
                                 QStringLiteral("https://example.test/x.mp3"), QStringLiteral("C:/candidate\n.mp3")}) {
            WindowsLocalMp3Validator validator(configuration());
            QCOMPARE(validator.poll(0).state, LocalMp3ValidationState::Idle);
            QVERIFY(!validator.start(path, 0));
            QCOMPARE(validator.poll(0).state, LocalMp3ValidationState::Idle);
        }
    }
    void validatesOwnedSyntheticMp3WithoutWritingOrPublishingAnything() {
        QTemporaryDir dir(QDir::tempPath() + "/RC Music validate 日本-XXXXXX");
        QVERIFY(dir.isValid());
        const auto path = dir.filePath("candidate.mp3");
        const auto bytes = fixture();
        QVERIFY(write(path, bytes));
        WindowsLocalMp3Validator validator(configuration());
        QVERIFY(validator.start(path, 0));
        QVERIFY(!validator.start(path, 0));
        const auto result = finish(validator);
        QCOMPARE(result.state, LocalMp3ValidationState::Validated);
        QCOMPARE(result.error, DownloadFailure::None);
        QCOMPARE(read(path), bytes);
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files), QStringList{"candidate.mp3"});
        validator.cancel();
        QCOMPARE(validator.poll(10000).state, LocalMp3ValidationState::Validated);
        QVERIFY(!validator.start(path, 10000));
    }
    void rejectsM4aDisguisedAsMp3WithoutConversion() {
        QTemporaryDir dir;
        const auto path = dir.filePath("candidate.mp3");
        const auto bytes = fixture(false);
        QVERIFY(write(path, bytes));
        WindowsLocalMp3Validator validator(configuration());
        QVERIFY(validator.start(path, 0));
        const auto result = finish(validator);
        QCOMPARE(result.state, LocalMp3ValidationState::Failed);
        QCOMPARE(result.error, DownloadFailure::UnsupportedMedia);
        QCOMPARE(read(path), bytes);
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files), QStringList{"candidate.mp3"});
    }
    void probeSuccessDoesNotWaiveFullDecode() {
        QTemporaryDir dir;
        const auto path = dir.filePath("candidate.mp3");
        QVERIFY(write(path, fixture()));
        auto config = configuration();
        config.ffmpeg_executable = QString::fromUtf8(RC_MUSIC_TEST_CONVERTER_HELPER_PATH);
        WindowsLocalMp3Validator validator(config);
        QVERIFY(validator.start(path, 0));
        const auto result = finish(validator);
        QCOMPARE(result.state, LocalMp3ValidationState::Failed);
        QCOMPARE(result.error, DownloadFailure::Conversion);
    }
    void probeTimeIsNotResetWhenTheFullDecoderStarts() {
        QTemporaryDir dir;
        const auto path = dir.filePath("candidate.mp3");
        const auto helper = dir.filePath("validation-hang.exe");
        QVERIFY(write(path, fixture()));
        QVERIFY(QFile::copy(QString::fromUtf8(RC_MUSIC_TEST_CONVERTER_HELPER_PATH), helper));
        auto config = configuration();
        config.ffmpeg_executable = helper;
        config.total_timeout_ms = 100;
        WindowsLocalMp3Validator validator(config);
        QVERIFY(validator.start(path, 0));
        QElapsedTimer clock;
        clock.start();
        do {
            QCOMPARE(validator.poll(75).state, LocalMp3ValidationState::Validating);
            QTest::qWait(5);
        } while (!QFile::exists(dir.filePath("validation-started")) && clock.elapsed() < 5000);
        QVERIFY(QFile::exists(dir.filePath("validation-started")));
        const auto result = finish(validator, 100);
        QCOMPARE(result.state, LocalMp3ValidationState::Failed);
        QCOMPARE(result.error, DownloadFailure::Timeout);
    }
    void cancellingIdleIsTerminalAndNeverStartsWork() {
        WindowsLocalMp3Validator validator(configuration());
        validator.cancel();
        QCOMPARE(validator.poll(0).state, LocalMp3ValidationState::Cancelled);
        QVERIFY(!validator.start(QStringLiteral("C:/candidate.mp3"), 0));
        validator.cancel();
        QCOMPARE(validator.poll(1).state, LocalMp3ValidationState::Cancelled);
    }
    void cancellationWaitsForNativeDrain() {
        QTemporaryDir dir;
        const auto path = dir.filePath("candidate.mp3");
        const auto bytes = fixture();
        QVERIFY(write(path, bytes));
        WindowsLocalMp3Validator validator(configuration());
        QVERIFY(validator.start(path, 0));
        validator.cancel();
        const auto observed = validator.poll(0).state;
        QVERIFY(observed == LocalMp3ValidationState::Stopping || observed == LocalMp3ValidationState::Cancelled);
        const auto result = finish(validator);
        QCOMPARE(result.state, LocalMp3ValidationState::Cancelled);
        QCOMPARE(result.error, DownloadFailure::None);
        QCOMPARE(read(path), bytes);
    }
    void deadlineAndBackwardClockAreTypedAndDrain_data() {
        QTest::addColumn<qint64>("now");
        QTest::addColumn<int>("error");
        QTest::newRow("deadline") << qint64{5} << static_cast<int>(DownloadFailure::Timeout);
        QTest::newRow("clock") << qint64{-1} << static_cast<int>(DownloadFailure::InvalidResult);
    }
    void deadlineAndBackwardClockAreTypedAndDrain() {
        QFETCH(qint64, now);
        QFETCH(int, error);
        QTemporaryDir dir;
        const auto path = dir.filePath("candidate.mp3");
        QVERIFY(write(path, fixture()));
        auto config = configuration();
        config.total_timeout_ms = 5;
        WindowsLocalMp3Validator validator(config);
        QVERIFY(validator.start(path, 0));
        static_cast<void>(validator.poll(now));
        LocalMp3ValidationUpdate result;
        QElapsedTimer clock;
        clock.start();
        do {
            result = validator.poll(now);
            QTest::qWait(5);
        } while (result.state == LocalMp3ValidationState::Stopping && clock.elapsed() < 5000);
        QCOMPARE(result.state, LocalMp3ValidationState::Failed);
        QCOMPARE(result.error, static_cast<DownloadFailure>(error));
    }
};
QTEST_GUILESS_MAIN(WindowsLocalMp3ValidatorTests)
#include "WindowsLocalMp3ValidatorTests.moc"
