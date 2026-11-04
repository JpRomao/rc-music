#include "../windows/sandbox/AppContainerRegistryLocationProtocol.hpp"

#include <QTest>

#include <algorithm>
#include <array>
#include <limits>

namespace protocol = music_player::sandbox_registry_location;
namespace {
protocol::Request request() {
    protocol::Request value;
    value.package_sid[0] = 1;
    value.package_sid[1] = 8;
    value.package_sid[7] = 15;
    value.package_sid[8] = 2;
    for (std::size_t index = 12; index < value.package_sid.size(); ++index)
        value.package_sid[index] = static_cast<std::uint8_t>(index * 7);
    for (std::size_t index = 0; index < value.nonce.size(); ++index)
        value.nonce[index] = static_cast<std::uint8_t>(255 - index);
    return value;
}
protocol::Reply captured() {
    protocol::Reply value;
    value.outcome = protocol::Outcome::CapturedName;
    value.nonce = request().nonce;
    value.session_package_sid = request().package_sid;
    value.token_errors = {0, 0, 0, 1008, std::nullopt};
    value.registry_location_hresult = 0;
    value.name_size_status = 0xc0000023;
    value.name_size_win32 = 122;
    value.name_read_status = 0;
    value.name_read_win32 = 0;
    value.key_close_error = 0;
    value.native_name = u"\\REGISTRY\\USER\\locator-\U0001f3b5";
    value.name_size_required = value.name_read_required = static_cast<std::uint32_t>(4 + 2 * value.native_name.size());
    return value;
}
protocol::Reply reply(const int kind) {
    auto value = captured();
    if (kind == 0) {
        value = {};
        value.nonce = request().nonce;
        value.token_errors.token_query_error = 5;
        value.token_errors.failed_information_class = 29;
    } else if (kind == 1) {
        value.outcome = protocol::Outcome::NativeApiError;
        value.registry_location_hresult = 0x80070002;
        value.name_size_status.reset();
        value.name_size_win32.reset();
        value.name_size_required.reset();
        value.name_read_status.reset();
        value.name_read_win32.reset();
        value.name_read_required.reset();
        value.key_close_error.reset();
        value.native_name.clear();
    } else if (kind == 3) {
        value.outcome = protocol::Outcome::CloseUnknown;
        value.key_close_error = 6;
    }
    return value;
}
void append(std::vector<std::uint8_t>& bytes, const std::uint32_t value, const unsigned count) {
    for (unsigned index = 0; index < count; ++index)
        bytes.push_back(static_cast<std::uint8_t>(value >> (index * 8)));
}
void replace(std::vector<std::uint8_t>& bytes, const std::size_t at, const std::uint32_t value, const unsigned count) {
    for (unsigned index = 0; index < count; ++index)
        bytes[at + index] = static_cast<std::uint8_t>(value >> (index * 8));
}
std::vector<std::uint8_t> wire(const protocol::Request& value) {
    std::vector<std::uint8_t> bytes{'R', 'C', 'M', 'R', 'L', 'Q', '0', '1', 1, 0, 0, 0};
    bytes.insert(bytes.end(), value.package_sid.begin(), value.package_sid.end());
    bytes.insert(bytes.end(), value.nonce.begin(), value.nonce.end());
    return bytes;
}
std::vector<std::uint8_t> wire(const protocol::Reply& value) {
    const auto& token = value.token_errors;
    const std::array<std::optional<std::uint32_t>, 13> fields{
        token.token_query_error,  token.failed_information_class,  token.process_token_close, token.thread_token_open,
        token.thread_token_close, value.registry_location_hresult, value.name_size_status,    value.name_size_win32,
        value.name_size_required, value.name_read_status,          value.name_read_win32,     value.name_read_required,
        value.key_close_error};
    std::uint32_t mask = value.session_package_sid ? 1 : 0;
    for (std::size_t index = 0; index < fields.size(); ++index)
        if (fields[index])
            mask |= std::uint32_t{1} << (index + 1);
    std::vector<std::uint8_t> bytes{'R', 'C', 'M', 'R', 'L', 'P', '0', '1', 1, 0};
    append(bytes, static_cast<std::uint8_t>(value.outcome), 1);
    append(bytes, value.uncertain_output ? 1 : 0, 1);
    append(bytes, mask, 4);
    bytes.insert(bytes.end(), value.nonce.begin(), value.nonce.end());
    const auto sid = value.session_package_sid.value_or(protocol::ProfileSid{});
    bytes.insert(bytes.end(), sid.begin(), sid.end());
    for (const auto field : fields)
        append(bytes, field.value_or(0), 4);
    append(bytes, static_cast<std::uint32_t>(value.native_name.size()), 4);
    for (const auto unit : value.native_name)
        append(bytes, unit, 2);
    return bytes;
}
bool refused(const protocol::Reply& value) {
    return protocol::encodeReply(value).empty() && !protocol::decodeReply(wire(value));
}
} // namespace

class AppContainerRegistryLocationProtocolTests final : public QObject {
    Q_OBJECT
  private slots:
    void requestHasOnlyCanonicalSidAndOpaqueNonce() {
        const auto value = request();
        const auto expected = wire(value);
        QCOMPARE(expected.size(), std::size_t{84});
        QCOMPARE(protocol::request_frame_bytes, expected.size());
        QVERIFY(protocol::encodeRequest(value) == expected);
        const auto decoded = protocol::decodeRequest(expected);
        QVERIFY(decoded && *decoded == value);
    }
    void zeroNonceAndOpaqueSidSuffixAreNotPolicyEvidence() {
        auto value = request();
        value.nonce.fill(0);
        std::fill(value.package_sid.begin() + 12, value.package_sid.end(), 0xff);
        const auto decoded = protocol::decodeRequest(protocol::encodeRequest(value));
        QVERIFY(decoded && *decoded == value);
    }
    void invalidCanonicalSid_data() {
        QTest::addColumn<int>("offset");
        for (int index = 0; index < 12; ++index)
            QTest::newRow(qPrintable(QString::number(index))) << index;
    }
    void invalidCanonicalSid() {
        QFETCH(int, offset);
        auto value = request();
        value.package_sid[static_cast<std::size_t>(offset)] ^= 1;
        QVERIFY(protocol::encodeRequest(value).empty());
        QVERIFY(!protocol::decodeRequest(wire(value)));
        auto response = captured();
        response.session_package_sid = value.package_sid;
        QVERIFY(refused(response));
    }
    void requestRejectsEveryTruncationExtensionAndEnvelopeMutation() {
        const auto bytes = wire(request());
        for (std::size_t size = 0; size < bytes.size(); ++size)
            QVERIFY(!protocol::decodeRequest(std::span{bytes}.first(size)));
        for (const auto offset :
             {std::size_t{0}, std::size_t{7}, std::size_t{8}, std::size_t{9}, std::size_t{10}, std::size_t{11}}) {
            auto changed = bytes;
            changed[offset] ^= 1;
            QVERIFY(!protocol::decodeRequest(changed));
        }
        auto changed = bytes;
        changed.push_back(0);
        QVERIFY(!protocol::decodeRequest(changed));
    }
    void boundedOutcomesMatchIndependentWire_data() {
        QTest::addColumn<int>("kind");
        for (int index = 0; index < 4; ++index)
            QTest::newRow(qPrintable(QString::number(index))) << index;
    }
    void boundedOutcomesMatchIndependentWire() {
        QFETCH(int, kind);
        const auto value = reply(kind);
        const auto expected = wire(value);
        QCOMPARE(expected.size(), std::size_t{144} + 2 * value.native_name.size());
        QCOMPARE(protocol::reply_header_bytes, std::size_t{144});
        QVERIFY(protocol::encodeReply(value) == expected);
        const auto decoded = protocol::decodeReply(expected);
        QVERIFY(decoded && *decoded == value);
    }
    void bothNameQueryStagesPreserveEveryRawBit() {
        auto value = reply(1);
        value.registry_location_hresult = 0;
        value.name_size_status = 0x80000005;
        value.name_size_win32 = 234;
        value.name_size_required = 0xffffffff;
        value.name_read_status = 0xc0000022;
        value.name_read_win32 = 5;
        value.name_read_required = 0xfffffffe;
        value.key_close_error = 0;
        const auto decoded = protocol::decodeReply(protocol::encodeReply(value));
        QVERIFY(decoded && *decoded == value);
        value = reply(0);
        value.token_errors = {0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};
        const auto token_decoded = protocol::decodeReply(protocol::encodeReply(value));
        QVERIFY(token_decoded && *token_decoded == value);
    }
    void unknownOutputRetainsASeparateCloseFactAndMayHaveNoName() {
        for (const auto close :
             {std::optional<std::uint32_t>{}, std::optional<std::uint32_t>{0}, std::optional<std::uint32_t>{6}}) {
            auto value = reply(1);
            value.outcome = protocol::Outcome::CloseUnknown;
            value.uncertain_output = true;
            value.key_close_error = close;
            const auto decoded = protocol::decodeReply(protocol::encodeReply(value));
            QVERIFY(decoded && *decoded == value);
        }
    }
    void thrownLocationPreservesAbsentHresultAndUnknownOutput() {
        auto value = reply(1);
        value.outcome = protocol::Outcome::CloseUnknown;
        value.uncertain_output = true;
        value.registry_location_hresult.reset();
        const auto decoded = protocol::decodeReply(protocol::encodeReply(value));
        QVERIFY(decoded && *decoded == value);
        value.uncertain_output = false;
        QVERIFY(refused(value));
    }
    void thrownQueryStagesKeepOnlyTheirObservedReturns() {
        auto value = reply(1);
        value.registry_location_hresult = 0;
        value.key_close_error = 0;
        const auto size_threw = protocol::decodeReply(protocol::encodeReply(value));
        QVERIFY(size_threw && *size_threw == value);
        value.name_size_status = 0xc0000023;
        value.name_size_win32 = 122;
        value.name_size_required = 112;
        const auto read_threw = protocol::decodeReply(protocol::encodeReply(value));
        QVERIFY(read_threw && *read_threw == value);
        value.outcome = protocol::Outcome::CloseUnknown;
        value.key_close_error.reset();
        const auto close_threw = protocol::decodeReply(protocol::encodeReply(value));
        QVERIFY(close_threw && *close_threw == value);
    }
    void nativeNameInclusiveLimitAndSurrogatePairRoundTrip() {
        auto value = captured();
        value.native_name.assign(32767, u'x');
        value.native_name[32765] = char16_t{0xd83c};
        value.native_name[32766] = char16_t{0xdfb5};
        value.name_size_required = value.name_read_required = 65538;
        const auto bytes = protocol::encodeReply(value);
        QCOMPARE(bytes.size(), std::size_t{144 + 65534});
        QVERIFY(bytes.size() <= protocol::maximum_frame_bytes);
        const auto decoded = protocol::decodeReply(bytes);
        QVERIFY(decoded && *decoded == value);
    }
    void capturedNameMayShrinkWithinBothRecognizedSizeStatuses() {
        for (const auto status : {std::uint32_t{0xc0000023}, std::uint32_t{0x80000005}})
            for (const auto size : {std::uint32_t{6}, std::uint32_t{65538}}) {
                auto value = captured();
                value.native_name = u"x";
                value.name_size_status = status;
                value.name_size_required = size;
                value.name_read_required = 6;
                const auto decoded = protocol::decodeReply(protocol::encodeReply(value));
                QVERIFY(decoded && *decoded == value);
            }
    }
    void capturedNameRejectsContradictoryNativeQuerySizes_data() {
        QTest::addColumn<int>("mutation");
        for (int index = 0; index < 10; ++index)
            QTest::newRow(qPrintable(QString::number(index))) << index;
    }
    void capturedNameRejectsContradictoryNativeQuerySizes() {
        QFETCH(int, mutation);
        auto value = captured();
        const auto actual = static_cast<std::uint32_t>(4 + 2 * value.native_name.size());
        switch (mutation) {
        case 0:
            value.name_size_status = 0;
            break;
        case 1:
            value.name_size_status = 0xffffffff;
            break;
        case 2:
            value.name_size_status = 0xc0000022;
            break;
        case 3:
            value.name_size_required = 5;
            break;
        case 4:
            value.name_size_required = 65539;
            break;
        case 5:
            value.name_read_required = actual + 2;
            value.name_size_required = actual + 2;
            break;
        case 6:
            value.name_read_required = actual - 2;
            break;
        case 7:
            value.name_size_required = actual - 2;
            break;
        case 8:
            value.name_read_required = 0;
            break;
        case 9:
            value.name_read_required = 0xffffffff;
            break;
        }
        QVERIFY(refused(value));
        value.outcome = protocol::Outcome::CloseUnknown;
        value.key_close_error = 6;
        QVERIFY(refused(value));
    }
    void invalidReplyContract_data() {
        QTest::addColumn<int>("mutation");
        for (int index = 0; index < 32; ++index)
            QTest::newRow(qPrintable(QString::number(index))) << index;
    }
    void invalidReplyContract() {
        QFETCH(int, mutation);
        auto value = captured();
        switch (mutation) {
        case 0:
            value.outcome = static_cast<protocol::Outcome>(0);
            break;
        case 1:
            value.outcome = static_cast<protocol::Outcome>(5);
            break;
        case 2:
            value.session_package_sid.reset();
            break;
        case 3:
            value.native_name.clear();
            break;
        case 4:
            value.native_name.assign(32768, u'x');
            break;
        case 5:
            value.native_name = std::u16string{u'a', char16_t{0}, u'b'};
            break;
        case 6:
            value.native_name = std::u16string{char16_t{0xd800}};
            break;
        case 7:
            value.native_name = std::u16string{char16_t{0xdc00}};
            break;
        case 8:
            value.native_name = std::u16string{char16_t{0xd800}, u'a'};
            break;
        case 9:
            value.native_name = std::u16string{char16_t{0xdc00}, char16_t{0xd800}};
            break;
        case 10:
            value.registry_location_hresult = 1;
            break;
        case 11:
            value.registry_location_hresult.reset();
            break;
        case 12:
            value.name_read_status = 0x80000005;
            break;
        case 13:
            value.name_read_win32 = 5;
            break;
        case 14:
            value.key_close_error = 6;
            break;
        case 15:
            value.key_close_error.reset();
            break;
        case 16:
            value.uncertain_output = true;
            break;
        case 17:
            value.token_errors.token_query_error = 5;
            break;
        case 18:
            value.token_errors.token_query_error.reset();
            break;
        case 19:
            value.token_errors.failed_information_class = 29;
            break;
        case 20:
            value.token_errors.process_token_close = 6;
            break;
        case 21:
            value.token_errors.thread_token_open = 0;
            break;
        case 22:
            value.token_errors.thread_token_close = 0;
            break;
        case 23:
            value.outcome = protocol::Outcome::RefusedToken;
            break;
        case 24:
            value.outcome = protocol::Outcome::NativeApiError;
            break;
        case 25:
            value.outcome = protocol::Outcome::CloseUnknown;
            break;
        case 26:
            value.name_size_status.reset();
            break;
        case 27:
            value.name_size_win32.reset();
            break;
        case 28:
            value.name_size_required.reset();
            break;
        case 29:
            value.name_read_status.reset();
            break;
        case 30:
            value.name_read_win32.reset();
            break;
        case 31:
            value.name_read_required.reset();
            break;
        }
        QVERIFY(refused(value));
    }
    void apiOutcomesRefuseTokenContradictionsEvenWithoutAName() {
        for (const auto kind : {1, 3}) {
            auto value = reply(kind);
            value.native_name.clear();
            value.token_errors.process_token_close.reset();
            QVERIFY(refused(value));
            value = reply(kind);
            value.token_errors.failed_information_class.reset();
            QVERIFY(refused(value));
            value = reply(kind);
            value.token_errors.thread_token_open.reset();
            QVERIFY(refused(value));
        }
    }
    void replyRejectsEveryTruncationAndAdditionalData() {
        const auto bytes = wire(captured());
        for (std::size_t size = 0; size < bytes.size(); ++size)
            QVERIFY(!protocol::decodeReply(std::span{bytes}.first(size)));
        auto changed = bytes;
        changed.push_back(0);
        QVERIFY(!protocol::decodeReply(changed));
        changed.resize(protocol::maximum_frame_bytes + 1);
        QVERIFY(!protocol::decodeReply(changed));
    }
    void replyRejectsEnvelopeUnknownPresenceAndDirtyAbsentSlots() {
        const auto bytes = wire(captured());
        for (const auto offset : {std::size_t{0}, std::size_t{7}, std::size_t{8}, std::size_t{9}}) {
            auto changed = bytes;
            changed[offset] ^= 1;
            QVERIFY(!protocol::decodeReply(changed));
        }
        auto changed = bytes;
        changed[11] = 2;
        QVERIFY(!protocol::decodeReply(changed));
        changed = bytes;
        changed[15] |= 0x80;
        QVERIFY(!protocol::decodeReply(changed));
        changed = wire(reply(0));
        changed[48] = 1;
        QVERIFY(!protocol::decodeReply(changed));
        changed = bytes;
        changed[104] = 1; // Absent thread_token_close slot must be zero.
        QVERIFY(!protocol::decodeReply(changed));
    }
    void forgedNameCountsAndMalformedUtf16AreRejected() {
        const auto bytes = wire(captured());
        for (const auto count : {std::uint32_t{0}, std::uint32_t{32768}, std::uint32_t{0xffffffff}}) {
            auto changed = bytes;
            replace(changed, 140, count, 4);
            QVERIFY(!protocol::decodeReply(changed));
        }
        auto changed = bytes;
        replace(changed, 144, 0, 2);
        QVERIFY(!protocol::decodeReply(changed));
        changed = bytes;
        replace(changed, changed.size() - 2, 0xd800, 2);
        QVERIFY(!protocol::decodeReply(changed));
    }
    void matchesOnlyTheExactSidAndNonceAndDoesNotInventMissingIdentity() {
        auto value = captured();
        const auto expected = request();
        QVERIFY(protocol::matchesRequest(expected, value));
        value.nonce[0] ^= 1;
        QVERIFY(!protocol::matchesRequest(expected, value));
        value = captured();
        (*value.session_package_sid)[12] ^= 1;
        QVERIFY(!protocol::matchesRequest(expected, value));
        value = reply(0);
        QVERIFY(!protocol::matchesRequest(expected, value));
        value.session_package_sid = expected.package_sid;
        QVERIFY(protocol::matchesRequest(expected, value));
        (*value.session_package_sid)[12] ^= 1;
        QVERIFY(!protocol::matchesRequest(expected, value));
        const auto decoded = protocol::decodeReply(wire(value));
        QVERIFY(decoded && *decoded == value);
        value = captured();
        value.registry_location_hresult = 1;
        QVERIFY(!protocol::matchesRequest(expected, value));
    }
};

QTEST_GUILESS_MAIN(AppContainerRegistryLocationProtocolTests)
#include "AppContainerRegistryLocationProtocolTests.moc"
