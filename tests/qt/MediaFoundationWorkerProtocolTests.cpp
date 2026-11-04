#include "adapters/MediaFoundationWorkerProtocol.hpp"

#include <QProcess>
#include <QTest>

#include <array>
#include <limits>
#include <string_view>

using namespace music_player::m0;

namespace {
MediaFoundationWorkerRequest request() {
    return {.mode = MediaFoundationWorkerMode::Convert,
            .input_handle = 0x0d0e0f10,
            .max_input_bytes = 0x01020304,
            .max_output_bytes = 0x05060708,
            .max_duration_us = 0x090a0b0c};
}
void append(std::vector<std::uint8_t>& bytes, const std::uint64_t value, const unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}
std::vector<std::uint8_t> wire(const MediaFoundationWorkerRequest& value) {
    std::vector<std::uint8_t> bytes{'R', 'C', 'M', 'M', 'F', 'W', 'R', 'Q', 2, static_cast<std::uint8_t>(value.mode)};
    append(bytes, value.max_input_bytes, 8);
    append(bytes, value.max_output_bytes, 8);
    append(bytes, value.max_duration_us, 8);
    append(bytes, value.input_handle, 8);
    return bytes;
}
std::vector<std::uint8_t> legacyPathWire() {
    const auto value = request();
    const std::u16string_view path = u"C:\\Synthetic folder\\áudio.mp4";
    std::vector<std::uint8_t> bytes{'R', 'C', 'M', 'M', 'F', 'W', 'R', 'Q', 1, static_cast<std::uint8_t>(value.mode)};
    append(bytes, value.max_input_bytes, 8);
    append(bytes, value.max_output_bytes, 8);
    append(bytes, value.max_duration_us, 8);
    append(bytes, path.size(), 4);
    for (const auto unit : path)
        append(bytes, unit, 2);
    return bytes;
}
bool same(const MediaFoundationWorkerRequest& left, const MediaFoundationWorkerRequest& right) {
    return left.mode == right.mode && left.input_handle == right.input_handle &&
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
        QCOMPARE(expected.size(), std::size_t{42});
        QVERIFY(encodeMediaFoundationWorkerRequest(value) == expected);
        const auto decoded = decodeMediaFoundationWorkerRequest(expected);
        QVERIFY(decoded.has_value());
        QVERIFY(same(*decoded, value));
    }

    void bothModesPreserveAllHandleAndLimitBits() {
        for (const auto mode : {MediaFoundationWorkerMode::Convert, MediaFoundationWorkerMode::Validate}) {
            auto value = request();
            value.mode = mode;
            value.input_handle = static_cast<std::uint64_t>(std::numeric_limits<std::intptr_t>::max()) - 3;
            const auto encoded = encodeMediaFoundationWorkerRequest(value);
            QVERIFY(!encoded.empty());
            const auto decoded = decodeMediaFoundationWorkerRequest(encoded);
            QVERIFY(decoded.has_value());
            QVERIFY(same(value, *decoded));
        }
    }

    void acceptsLargestPositiveHandleAndExactResourceLimits() {
        auto value = request();
        value.input_handle = static_cast<std::uint64_t>(std::numeric_limits<std::intptr_t>::max());
        value.max_input_bytes = value.max_output_bytes = 1024ULL * 1024 * 1024;
        value.max_duration_us = 24ULL * 60 * 60 * 1'000'000;
        QVERIFY(validMediaFoundationWorkerRequest(value));
        const auto encoded = encodeMediaFoundationWorkerRequest(value);
        QCOMPARE(encoded.size(), media_foundation_worker_max_request_bytes);
        const auto decoded = decodeMediaFoundationWorkerRequest(encoded);
        QVERIFY(decoded.has_value());
        QVERIFY(same(value, *decoded));
    }

    void rejectsNullPseudoAndOutOfWidthHandles_data() {
        QTest::addColumn<qulonglong>("handle");
        QTest::newRow("null") << qulonglong{0};
        for (int value = -1; value >= -6; --value)
            QTest::newRow(qPrintable(QStringLiteral("pseudo %1").arg(value)))
                << static_cast<qulonglong>(static_cast<std::int64_t>(value));
        QTest::newRow("sign bit") << static_cast<qulonglong>(
            static_cast<std::uint64_t>(std::numeric_limits<std::intptr_t>::max()) + 1);
        if constexpr (sizeof(std::uintptr_t) < sizeof(std::uint64_t))
            QTest::newRow("wider than process handle") << (qulonglong{1} << 32);
    }

    void rejectsNullPseudoAndOutOfWidthHandles() {
        QFETCH(qulonglong, handle);
        auto value = request();
        value.input_handle = handle;
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
        auto bad_handle = bytes;
        for (std::size_t i = 34; i < 42; ++i)
            bad_handle[i] = 0xff;
        QVERIFY(!decodeMediaFoundationWorkerRequest(bad_handle).has_value());
    }

    void thePathProtocolIsRejectedEvenWhenItsFrameHasTheNewLength() {
        QVERIFY(!decodeMediaFoundationWorkerRequest(legacyPathWire()).has_value());
        auto old_version = wire(request());
        old_version[8] = 1;
        QCOMPARE(old_version.size(), media_foundation_worker_max_request_bytes);
        QVERIFY(!decodeMediaFoundationWorkerRequest(old_version).has_value());
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
        const auto legacy = legacyPathWire();
        QTest::newRow("legacy path request")
            << QByteArray(reinterpret_cast<const char*>(legacy.data()), static_cast<qsizetype>(legacy.size()));
        auto legacy_same_size = bytes;
        legacy_same_size[8] = 1;
        QTest::newRow("legacy version fixed frame") << legacy_same_size;
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
