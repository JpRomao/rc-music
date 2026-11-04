#include "../windows/sandbox/AppContainerProbeProtocol.hpp"

#include <QTest>

#include <algorithm>

namespace protocol = music_player::sandbox_probe;
namespace {
protocol::Request request() {
    protocol::Request value;
    value.sid = std::vector<std::uint8_t>(40, 1);
    value.input_handle = 0x01020304;
    for (std::size_t i = 0; i < value.challenge.size(); ++i)
        value.challenge[i] = static_cast<std::uint8_t>(i);
    value.sentinel = L"C:/private/sentinel.bin";
    value.runtime = L"C:/private/runtime";
    value.port = 0x1234;
    value.udp_port = 0x5678;
    return value;
}
void append(std::vector<std::uint8_t>& bytes, const std::uint64_t value, const unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}
std::vector<std::uint8_t> wire(const protocol::Request& value) {
    std::vector<std::uint8_t> bytes{'R', 'C', 'M', 'S', 'B', 'X', '0', '1', 2, 0};
    append(bytes, value.sid.size(), 2);
    append(bytes, value.input_handle, 8);
    append(bytes, value.sentinel.size(), 2);
    append(bytes, value.runtime.size(), 2);
    append(bytes, value.port, 2);
    append(bytes, value.udp_port, 2);
    bytes.insert(bytes.end(), value.challenge.begin(), value.challenge.end());
    bytes.insert(bytes.end(), value.sid.begin(), value.sid.end());
    for (const auto& path : {value.sentinel, value.runtime})
        for (const auto unit : path)
            append(bytes, static_cast<std::uint16_t>(unit), 2);
    return bytes;
}
protocol::Report report() {
    protocol::Report value{.passed = protocol::all_checks, .capability_count = 0, .integrity_rid = 4096};
    for (std::size_t i = 0; i < value.errors.size(); ++i)
        value.errors[i] = static_cast<std::uint32_t>(i + 1);
    value.errors[5] = 1816;
    value.errors[6] = 10060;
    value.udp_error = 10013;
    return value;
}
std::vector<std::uint8_t> wire(const protocol::Report& value) {
    std::vector<std::uint8_t> bytes{'R', 'C', 'M', 'S', 'B', 'R', '0', '1', 2, 0};
    append(bytes, value.passed, 4);
    append(bytes, value.capability_count, 4);
    append(bytes, value.integrity_rid, 4);
    for (const auto error : value.errors)
        append(bytes, error, 4);
    append(bytes, value.udp_error, 4);
    return bytes;
}
} // namespace

class AppContainerProbeProtocolTests final : public QObject {
    Q_OBJECT
  private slots:
    void requestPreservesBothPortsAndMatchesIndependentVersionTwoBytes() {
        const auto expected = request();
        const auto bytes = wire(expected);
        QCOMPARE(bytes[24], std::uint8_t{0x34});
        QCOMPARE(bytes[25], std::uint8_t{0x12});
        QCOMPARE(bytes[26], std::uint8_t{0x78});
        QCOMPARE(bytes[27], std::uint8_t{0x56});
        QVERIFY(protocol::encode(expected) == bytes);
        const auto decoded = protocol::decodeRequest(bytes);
        QVERIFY(decoded && *decoded == expected);
    }
    void requestRoundTripRetainsTheUdpPort() {
        const auto expected = request();
        const auto decoded = protocol::decodeRequest(protocol::encode(expected));
        QVERIFY(decoded && *decoded == expected);
    }
    void inclusiveSidPathHandleAndPortLimitsRoundTrip() {
        for (const auto sid_size : {std::size_t{8}, std::size_t{68}}) {
            auto value = request();
            value.sid.assign(sid_size, 0xff);
            value.input_handle = static_cast<std::uint64_t>(std::numeric_limits<std::intptr_t>::max());
            value.sentinel = L"C:/" + std::wstring(2045, L's');
            value.runtime = L"D:\\" + std::wstring(2045, L'r');
            value.port = value.udp_port = std::numeric_limits<std::uint16_t>::max();
            const auto bytes = protocol::encode(value);
            QCOMPARE(bytes.size(), std::size_t{60} + sid_size + 2 * (value.sentinel.size() + value.runtime.size()));
            QVERIFY(bytes.size() <= protocol::maximum_request_bytes);
            const auto decoded = protocol::decodeRequest(bytes);
            QVERIFY(decoded && *decoded == value);
        }
        auto value = request();
        value.port = value.udp_port = 1;
        const auto decoded = protocol::decodeRequest(protocol::encode(value));
        QVERIFY(decoded && *decoded == value);
    }
    void invalidRequestFields_data() {
        QTest::addColumn<int>("mutation");
        for (int i = 0; i < 12; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void invalidRequestFields() {
        QFETCH(int, mutation);
        auto value = request();
        switch (mutation) {
        case 0:
            value.udp_port = 0;
            break;
        case 1:
            value.port = 0;
            break;
        case 2:
            value.sid.resize(7);
            break;
        case 3:
            value.sid.resize(69);
            break;
        case 4:
            value.input_handle = 0;
            break;
        case 5:
            value.input_handle = std::numeric_limits<std::uint64_t>::max();
            break;
        case 6:
            value.sentinel = L"relative";
            break;
        case 7:
            value.runtime = L"\\\\server\\share";
            break;
        case 8:
            value.sentinel = L"C:/" + std::wstring(2046, L's');
            break;
        case 9:
            value.runtime = L"C:/" + std::wstring(2046, L'r');
            break;
        case 10:
            value.sentinel[3] = L'\0';
            break;
        case 11:
            value.runtime[3] = L'\n';
            break;
        }
        QVERIFY(protocol::encode(value).empty());
        QVERIFY(!protocol::decodeRequest(wire(value)));
    }
    void everyRequestTruncationAndAdditionalBytesAreRejected() {
        const auto bytes = wire(request());
        for (std::size_t count = 0; count < bytes.size(); ++count)
            QVERIFY(!protocol::decodeRequest(std::span<const std::uint8_t>{bytes.data(), count}));
        auto extra = bytes;
        extra.push_back(0);
        QVERIFY(!protocol::decodeRequest(extra));
        QVERIFY(!protocol::decodeRequest(std::vector<std::uint8_t>(protocol::maximum_request_bytes + 1, 0)));
    }
    void legacyFutureWrongMagicAndMismatchedLengthsAreRejected() {
        const auto original = wire(request());
        for (const auto version : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{3}, std::uint8_t{255}}) {
            auto bytes = original;
            bytes[8] = version;
            if (version == 1)
                bytes[26] = bytes[27] = 0; // Exact legacy reserved slot, otherwise a valid v1 request.
            QVERIFY(!protocol::decodeRequest(bytes));
        }
        for (const auto offset : {std::size_t{0}, std::size_t{10}, std::size_t{20}, std::size_t{22}}) {
            auto bytes = original;
            ++bytes[offset];
            QVERIFY(!protocol::decodeRequest(bytes));
        }
    }
    void reportAddsUdpWithoutLosingTheRawJobOrTcpErrors() {
        const auto expected = report();
        const auto bytes = wire(expected);
        QCOMPARE(bytes.size(), std::size_t{70});
        QVERIFY(protocol::encode(expected) == bytes);
        const auto decoded = protocol::decodeReport(bytes);
        QVERIFY(decoded && *decoded == expected);
        QCOMPARE(decoded->errors[5], std::uint32_t{1816});
        QCOMPARE(decoded->errors[6], std::uint32_t{10060});
        QCOMPARE(decoded->udp_error, std::uint32_t{10013});
    }
    void reportRoundTripRetainsAllUdpErrorBits() {
        for (const auto error : {std::uint32_t{0}, std::uint32_t{10013}, std::uint32_t{10060},
                                 std::numeric_limits<std::uint32_t>::max()}) {
            auto expected = report();
            expected.udp_error = error;
            const auto decoded = protocol::decodeReport(protocol::encode(expected));
            QVERIFY(decoded && *decoded == expected);
        }
    }
    void reportRejectsEveryTruncationTrailingDataVersionsAndUnknownCheckBits() {
        const auto original = wire(report());
        for (std::size_t count = 0; count < original.size(); ++count)
            QVERIFY(!protocol::decodeReport(std::span<const std::uint8_t>{original.data(), count}));
        auto extra = original;
        extra.push_back(0);
        QVERIFY(!protocol::decodeReport(extra));
        for (const auto version : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{3}}) {
            auto bytes = original;
            bytes[8] = version;
            if (version == 1)
                bytes.resize(66); // Exact v1 report rather than a malformed length alone.
            QVERIFY(!protocol::decodeReport(bytes));
        }
        auto bytes = original;
        bytes[0] ^= 1;
        QVERIFY(!protocol::decodeReport(bytes));
        bytes = original;
        bytes[13] |= 0x80;
        QVERIFY(!protocol::decodeReport(bytes));
    }
    void reportDoesNotPromoteTcpOrJobChecksFromUdpAccessDenied() {
        auto value = report();
        value.passed = protocol::all_checks & ~(protocol::Check::LoopbackDenied | protocol::Check::JobChildDenied);
        const auto decoded = protocol::decodeReport(protocol::encode(value));
        QVERIFY(decoded);
        QCOMPARE(decoded->passed, std::uint32_t{29695});
        QCOMPARE(decoded->errors[5], std::uint32_t{1816});
        QCOMPARE(decoded->errors[6], std::uint32_t{10060});
        QCOMPARE(decoded->udp_error, std::uint32_t{10013});
    }
    void holdModeNonceIsExactlyThirtyTwoLowerHexCharacters_data() {
        QTest::addColumn<QString>("nonce");
        QTest::addColumn<bool>("valid");
        QTest::newRow("zero") << QString(32, QLatin1Char('0')) << true;
        QTest::newRow("alphabet") << QStringLiteral("0123456789abcdef0123456789abcdef") << true;
        QTest::newRow("short") << QString(31, QLatin1Char('a')) << false;
        QTest::newRow("long") << QString(33, QLatin1Char('a')) << false;
        QTest::newRow("upper") << QString(32, QLatin1Char('A')) << false;
        QTest::newRow("nonhex") << QString(32, QLatin1Char('g')) << false;
        QTest::newRow("control") << (QString(31, QLatin1Char('a')) + QChar{u'\0'}) << false;
        QTest::newRow("unicode") << QString(32, QChar{u'\u00e9'}) << false;
        QTest::newRow("empty") << QString{} << false;
    }
    void holdModeNonceIsExactlyThirtyTwoLowerHexCharacters() {
        QFETCH(QString, nonce);
        QFETCH(bool, valid);
        QCOMPARE(protocol::validHoldNonce(nonce.toStdWString()), valid);
    }
};
QTEST_GUILESS_MAIN(AppContainerProbeProtocolTests)
#include "AppContainerProbeProtocolTests.moc"
