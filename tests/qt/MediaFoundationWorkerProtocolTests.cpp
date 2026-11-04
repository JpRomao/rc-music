#include "adapters/MediaFoundationWorkerProtocol.hpp"

#include <QProcess>
#include <QTest>

#include <array>
#include <limits>

using namespace music_player::m0;

namespace {
MediaFoundationWorkerRequest request() {
    return {.mode = MediaFoundationWorkerMode::Convert,
            .input_path = u"C:\\Synthetic folder\\áudio.mp4",
            .max_input_bytes = 0x01020304,
            .max_output_bytes = 0x05060708,
            .max_duration_us = 0x090a0b0c};
}
void append(std::vector<std::uint8_t>& bytes, const std::uint64_t value, const unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}
std::vector<std::uint8_t> wire(const MediaFoundationWorkerRequest& value) {
    std::vector<std::uint8_t> bytes{'R', 'C', 'M', 'M', 'F', 'W', 'R', 'Q', 1, static_cast<std::uint8_t>(value.mode)};
    append(bytes, value.max_input_bytes, 8);
    append(bytes, value.max_output_bytes, 8);
    append(bytes, value.max_duration_us, 8);
    append(bytes, value.input_path.size(), 4);
    for (const auto unit : value.input_path)
        append(bytes, unit, 2);
    return bytes;
}
bool same(const MediaFoundationWorkerRequest& left, const MediaFoundationWorkerRequest& right) {
    return left.mode == right.mode && left.input_path == right.input_path &&
           left.max_input_bytes == right.max_input_bytes && left.max_output_bytes == right.max_output_bytes &&
           left.max_duration_us == right.max_duration_us;
}
struct WorkerOutcome final {
    bool started{false};
    bool finished{false};
    QProcess::ExitStatus status{QProcess::CrashExit};
    int code{-1};
    QByteArray output;
    QByteArray errors;
};
WorkerOutcome runWorker(const QByteArray& bytes, const QStringList& arguments = {}) {
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(QString::fromUtf8(RC_MUSIC_TEST_MF_WORKER_PATH), arguments);
    WorkerOutcome result;
    result.started = process.waitForStarted(2'000);
    if (result.started) {
        if (!bytes.isEmpty())
            static_cast<void>(process.write(bytes));
        process.closeWriteChannel();
        result.finished = process.waitForFinished(5'000);
        if (!result.finished) {
            process.kill();
            static_cast<void>(process.waitForFinished(2'000));
        }
        result.status = process.exitStatus();
        result.code = process.exitCode();
        result.output = process.readAllStandardOutput();
        result.errors = process.readAllStandardError();
    }
    return result;
}
} // namespace

class MediaFoundationWorkerProtocolTests final : public QObject {
    Q_OBJECT
  private slots:
    void validRequestEncodesAnExactIndependentLittleEndianVector() {
        const auto value = request();
        QVERIFY(validMediaFoundationWorkerRequest(value));
        const auto expected = wire(value);
        QCOMPARE(expected.size(), std::size_t{38} + value.input_path.size() * 2);
        QVERIFY(encodeMediaFoundationWorkerRequest(value) == expected);
        const auto decoded = decodeMediaFoundationWorkerRequest(expected);
        QVERIFY(decoded.has_value());
        QVERIFY(same(*decoded, value));
    }

    void bothModesPreserveAllFieldsAndSafeUtf16Pairs() {
        for (const auto mode : {MediaFoundationWorkerMode::Convert, MediaFoundationWorkerMode::Validate}) {
            auto value = request();
            value.mode = mode;
            value.input_path = u"D:/Synthetic/\U0001f3b5/source.mp3";
            const auto encoded = encodeMediaFoundationWorkerRequest(value);
            QVERIFY(!encoded.empty());
            const auto decoded = decodeMediaFoundationWorkerRequest(encoded);
            QVERIFY(decoded.has_value());
            QVERIFY(same(value, *decoded));
        }
    }

    void acceptsExactMaximumPathAndResourceLimits() {
        auto value = request();
        value.input_path = u"C:\\" + std::u16string(8'189, u'a');
        QCOMPARE(value.input_path.size(), std::size_t{8'192});
        value.max_input_bytes = value.max_output_bytes = 1024ULL * 1024 * 1024;
        value.max_duration_us = 24ULL * 60 * 60 * 1'000'000;
        QVERIFY(validMediaFoundationWorkerRequest(value));
        const auto encoded = encodeMediaFoundationWorkerRequest(value);
        QVERIFY(encoded.size() < media_foundation_worker_max_request_bytes);
        const auto decoded = decodeMediaFoundationWorkerRequest(encoded);
        QVERIFY(decoded.has_value());
        QVERIFY(same(value, *decoded));
    }

    void rejectsUnsafePaths_data() {
        QTest::addColumn<QString>("path");
        QTest::newRow("empty") << QString{};
        QTest::newRow("relative") << QStringLiteral("source.mp4");
        QTest::newRow("drive-relative") << QStringLiteral("C:source.mp4");
        QTest::newRow("UNC") << QStringLiteral("\\\\server\\share\\source.mp4");
        QTest::newRow("device") << QStringLiteral("\\\\?\\C:\\source.mp4");
        QTest::newRow("device-dot") << QStringLiteral("\\\\.\\C:\\source.mp4");
        QTest::newRow("ADS") << QStringLiteral("C:\\source.mp4:stream");
        QTest::newRow("parent-component") << QStringLiteral("C:\\folder\\..\\source.mp4");
        QTest::newRow("dot-component") << QStringLiteral("C:\\folder\\.\\source.mp4");
        QTest::newRow("empty-component") << QStringLiteral("C:\\folder\\\\source.mp4");
        QTest::newRow("trailing-separator") << QStringLiteral("C:\\folder\\");
        QTest::newRow("trailing-dot") << QStringLiteral("C:\\folder\\source.mp4.");
        QTest::newRow("trailing-space") << QStringLiteral("C:\\folder\\source.mp4 ");
        QTest::newRow("wildcard") << QStringLiteral("C:\\folder\\*.mp4");
        QTest::newRow("DOS-device") << QStringLiteral("C:\\folder\\NUL.mp4");
        QTest::newRow("NUL") << QStringLiteral("C:\\folder\\a") + QChar{0} + QStringLiteral(".mp4");
        QTest::newRow("high-surrogate") << QStringLiteral("C:\\folder\\a") + QChar{0xd800};
        QTest::newRow("low-surrogate") << QStringLiteral("C:\\folder\\a") + QChar{0xdc00};
        QTest::newRow("too-long") << QStringLiteral("C:\\") + QString(8'190, QLatin1Char('a'));
    }

    void rejectsUnsafePaths() {
        QFETCH(QString, path);
        auto value = request();
        value.input_path = path.toStdU16String();
        QVERIFY(!validMediaFoundationWorkerRequest(value));
        QVERIFY(encodeMediaFoundationWorkerRequest(value).empty());
        QVERIFY(!decodeMediaFoundationWorkerRequest(wire(value)).has_value());
    }

    void refusesZeroOversizedAndOverflowedLimits() {
        for (const auto invalid :
             std::array<std::uint64_t, 3>{0, 1024ULL * 1024 * 1024 + 1, std::numeric_limits<std::uint64_t>::max()}) {
            for (const bool output : {false, true}) {
                auto value = request();
                (output ? value.max_output_bytes : value.max_input_bytes) = invalid;
                QVERIFY(!validMediaFoundationWorkerRequest(value));
                QVERIFY(encodeMediaFoundationWorkerRequest(value).empty());
                QVERIFY(!decodeMediaFoundationWorkerRequest(wire(value)).has_value());
            }
        }
        for (const auto invalid : std::array<std::uint64_t, 3>{0, 24ULL * 60 * 60 * 1'000'000 + 1,
                                                               std::numeric_limits<std::uint64_t>::max()}) {
            auto value = request();
            value.max_duration_us = invalid;
            QVERIFY(!validMediaFoundationWorkerRequest(value));
            QVERIFY(encodeMediaFoundationWorkerRequest(value).empty());
            QVERIFY(!decodeMediaFoundationWorkerRequest(wire(value)).has_value());
        }
    }

    void unknownModesAndWireVersionsAreNeverAccepted() {
        for (const auto mode : {0, 3, 255}) {
            auto value = request();
            value.mode = static_cast<MediaFoundationWorkerMode>(mode);
            QVERIFY(!validMediaFoundationWorkerRequest(value));
            QVERIFY(encodeMediaFoundationWorkerRequest(value).empty());
            QVERIFY(!decodeMediaFoundationWorkerRequest(wire(value)).has_value());
        }
        for (const std::size_t offset : {std::size_t{0}, std::size_t{7}, std::size_t{8}}) {
            auto bytes = wire(request());
            bytes[offset] ^= 0x80;
            QVERIFY(!decodeMediaFoundationWorkerRequest(bytes).has_value());
        }
    }

    void truncatedTrailingAndOversizedFramesFailClosed() {
        const auto bytes = wire(request());
        for (std::size_t length = 0; length < bytes.size(); ++length)
            QVERIFY(!decodeMediaFoundationWorkerRequest(std::span{bytes}.first(length)).has_value());
        auto trailing = bytes;
        trailing.push_back(0);
        QVERIFY(!decodeMediaFoundationWorkerRequest(trailing).has_value());
        trailing = bytes;
        trailing.insert(trailing.end(), bytes.begin(), bytes.end());
        QVERIFY(!decodeMediaFoundationWorkerRequest(trailing).has_value());
        QVERIFY(!decodeMediaFoundationWorkerRequest(
                     std::vector<std::uint8_t>(media_foundation_worker_max_request_bytes + 1, 0))
                     .has_value());
        auto bad_length = bytes;
        for (std::size_t i = 34; i < 38; ++i)
            bad_length[i] = 0xff;
        QVERIFY(!decodeMediaFoundationWorkerRequest(bad_length).has_value());
    }

    void workerRejectsArgumentsBeforeInterpretingItsRequest() {
        const auto encoded = wire(request());
        const auto bytes =
            QByteArray(reinterpret_cast<const char*>(encoded.data()), static_cast<qsizetype>(encoded.size()));
        for (const auto& arguments :
             {QStringList{QStringLiteral("--validate")}, QStringList{QStringLiteral("C:\\untrusted.mp4")}}) {
            const auto outcome = runWorker(bytes, arguments);
            QVERIFY(outcome.started);
            QVERIFY(outcome.finished);
            QCOMPARE(outcome.status, QProcess::NormalExit);
            QCOMPARE(outcome.code, static_cast<int>(MediaFoundationWorkerExit::InvalidRequest));
            QVERIFY(outcome.output.isEmpty());
            QVERIFY(outcome.errors.isEmpty());
        }
    }

    void workerRejectsMalformedOrNonEofRequestsSilently_data() {
        QTest::addColumn<QByteArray>("bytes");
        const auto valid = wire(request());
        const auto bytes =
            QByteArray(reinterpret_cast<const char*>(valid.data()), static_cast<qsizetype>(valid.size()));
        QTest::newRow("empty") << QByteArray{};
        QTest::newRow("bad-magic") << QByteArray{"not-a-request"};
        QTest::newRow("truncated") << bytes.left(bytes.size() - 1);
        QTest::newRow("trailing") << bytes + 'x';
        QTest::newRow("double-request") << bytes + bytes;
        QTest::newRow("over-budget") << QByteArray(
            static_cast<qsizetype>(media_foundation_worker_max_request_bytes + 1), 'x');
    }

    void workerRejectsMalformedOrNonEofRequestsSilently() {
        QFETCH(QByteArray, bytes);
        const auto outcome = runWorker(bytes);
        QVERIFY(outcome.started);
        QVERIFY(outcome.finished);
        QCOMPARE(outcome.status, QProcess::NormalExit);
        QCOMPARE(outcome.code, static_cast<int>(MediaFoundationWorkerExit::InvalidRequest));
        QVERIFY(outcome.output.isEmpty());
        QVERIFY(outcome.errors.isEmpty());
    }
};

QTEST_GUILESS_MAIN(MediaFoundationWorkerProtocolTests)
#include "MediaFoundationWorkerProtocolTests.moc"
