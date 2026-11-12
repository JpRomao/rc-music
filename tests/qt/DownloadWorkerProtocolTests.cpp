#include "DownloadWorkerProtocol.hpp"

#include <QCryptographicHash>
#include <QTest>
#include <QtEndian>

using namespace music_player::m0;

namespace {
DownloadWorkerRequest request(DownloadWorkerMode mode = DownloadWorkerMode::Resolve) {
    DownloadWorkerRequest value;
    value.mode = mode;
    value.runtime = {QStringLiteral("C:/RC Music 日本/Qt6Core.dll"),
                     QByteArray(64, 'a'),
                     QStringLiteral("C:/RC Music 日本/Qt6Network.dll"),
                     QByteArray(64, 'b'),
                     QStringLiteral("C:/RC Music 日本/qrcmusicschanneloffline.dll"),
                     QByteArray(64, 'c')};
    value.host = QStringLiteral("media.example.test");
    value.endpoint = {QHostAddress{QStringLiteral("93.184.215.14")}, QStringLiteral("media.example.test"),
                      QByteArray{"/file%41%2f.mp4?sig=1+2&x=%20"}, 443, true};
    return value;
}
DownloadWorkerFrame terminal(bool eof = false, DownloadWorkerError error = DownloadWorkerError::None) {
    DownloadWorkerFrame value;
    value.authenticated_eof = eof;
    value.error = error;
    return value;
}
QByteArray framedBody(const QByteArray& packet) { return packet.mid(4); }
QByteArray versionTwoStream(const QByteArray& agent) {
    auto legacy = encodeDownloadWorkerRequest(request(DownloadWorkerMode::Stream));
    legacy[4] = char{2};
    QByteArray field(4, char{0});
    qToLittleEndian<quint32>(static_cast<quint32>(agent.size()), field.data());
    field += agent;
    legacy.insert(legacy.size() - 3, field);
    return legacy;
}
} // namespace

class DownloadWorkerProtocolTests final : public QObject {
    Q_OBJECT
  private slots:
    void userAgentStreamUsesStrictVersionTwo() {
        auto original = request(DownloadWorkerMode::Stream);
        original.endpoint.user_agent = "RCMusic Synthetic Agent/1";
        const auto encoded = encodeDownloadWorkerRequest(original);
        QVERIFY(!encoded.isEmpty());
        QCOMPARE(encoded.first(5), QByteArray("RCMD", 4) + char{2});
        QCOMPARE(encoded, versionTwoStream(original.endpoint.user_agent));
        const auto decoded = decodeDownloadWorkerRequest(encoded);
        QVERIFY(decoded);
        QCOMPARE(decoded->endpoint.user_agent, original.endpoint.user_agent);
        QCOMPARE(decoded->endpoint.request_target, original.endpoint.request_target);
        QCOMPARE(decoded->endpoint.host, original.endpoint.host);
        QCOMPARE(decoded->endpoint.address, original.endpoint.address);
        QCOMPARE(decoded->endpoint.port, original.endpoint.port);
        QCOMPARE(decoded->endpoint.tls, original.endpoint.tls);
        QCOMPARE(decoded->runtime.core_path, original.runtime.core_path);
    }
    void minimalRequestsKeepLegacyVersionOne() {
        for (const auto mode : {DownloadWorkerMode::Resolve, DownloadWorkerMode::Stream}) {
            const auto original = request(mode);
            const auto encoded = encodeDownloadWorkerRequest(original);
            QVERIFY(!encoded.isEmpty());
            QCOMPARE(encoded.first(5), QByteArray("RCMD", 4) + char{1});
            // Independent pre-hint wire golden, not a second encoder round-trip.
            const auto digest = QCryptographicHash::hash(encoded, QCryptographicHash::Sha256).toHex();
            if (mode == DownloadWorkerMode::Resolve)
                QCOMPARE(digest, QByteArray("ead33e3c1d4a924108bf2516c58fecc4471347acdc69cd1b8e436c4716fa4928"));
            else
                QCOMPARE(digest, QByteArray("6fb18fa1d75f0de8dffda4757c6c178cc9495235ea9dfbf47c4864e83e20b029"));
            const auto decoded = decodeDownloadWorkerRequest(encoded);
            QVERIFY(decoded);
            QVERIFY(decoded->endpoint.user_agent.isEmpty());
            QCOMPARE(encodeDownloadWorkerRequest(*decoded), encoded);
            QVERIFY(!decodeDownloadWorkerRequest(encoded + char{0}));
        }
    }
    void userAgentVersionTwoRejectsMalformedRequests() {
        const auto valid_v2 = versionTwoStream("RCMusic Synthetic Agent/1");
        const auto decoded = decodeDownloadWorkerRequest(valid_v2);
        QVERIFY(decoded);
        for (qsizetype size = 0; size < valid_v2.size(); ++size)
            QVERIFY(!decodeDownloadWorkerRequest(valid_v2.first(size)));
        QVERIFY(!decodeDownloadWorkerRequest(valid_v2 + char{0}));
        QVERIFY(!decodeDownloadWorkerRequest(versionTwoStream({})));
        auto dns_v2 = encodeDownloadWorkerRequest(request());
        dns_v2[4] = char{2};
        QVERIFY(!decodeDownloadWorkerRequest(dns_v2));
        auto disguised_v1 = valid_v2;
        disguised_v1[4] = char{1};
        QVERIFY(!decodeDownloadWorkerRequest(disguised_v1));
        const QList<QByteArray> invalid{QByteArray("agent") + char{13} + "Cookie: no",
                                        QByteArray("agent") + char{10} + "Authorization: no",
                                        QByteArray("agent") + char{0},
                                        QByteArray(1, char{127}),
                                        QByteArray::fromHex("c3a9"),
                                        QByteArray(513, 'a')};
        for (const auto& agent : invalid) {
            auto value = request(DownloadWorkerMode::Stream);
            value.endpoint.user_agent = agent;
            QVERIFY(encodeDownloadWorkerRequest(value).isEmpty());
            QVERIFY(!decodeDownloadWorkerRequest(versionTwoStream(agent)));
        }
        auto dns = request();
        dns.endpoint.user_agent = "RCMusic Synthetic Agent/1";
        QVERIFY(encodeDownloadWorkerRequest(dns).isEmpty());
        auto maximum = request(DownloadWorkerMode::Stream);
        maximum.endpoint.user_agent = QByteArray(512, 'a');
        const auto maximum_decoded = decodeDownloadWorkerRequest(encodeDownloadWorkerRequest(maximum));
        QVERIFY(maximum_decoded);
        QCOMPARE(maximum_decoded->endpoint.user_agent, maximum.endpoint.user_agent);
    }
    void privateRequestPreservesUnicodeAndSignedBytes_data() {
        QTest::addColumn<bool>("stream");
        QTest::newRow("dns") << false;
        QTest::newRow("stream") << true;
    }
    void privateRequestPreservesUnicodeAndSignedBytes() {
        QFETCH(bool, stream);
        const auto original = request(stream ? DownloadWorkerMode::Stream : DownloadWorkerMode::Resolve);
        const auto encoded = encodeDownloadWorkerRequest(original);
        QVERIFY(!encoded.isEmpty());
        QVERIFY(encoded.size() <= download_worker_max_request_bytes);
        const auto decoded = decodeDownloadWorkerRequest(encoded);
        QVERIFY(decoded.has_value());
        QCOMPARE(decoded->mode, original.mode);
        QCOMPARE(decoded->runtime.core_path, original.runtime.core_path);
        QCOMPARE(decoded->runtime.network_sha256, original.runtime.network_sha256);
        QCOMPARE(decoded->runtime.plugin_path, original.runtime.plugin_path);
        if (stream) {
            QCOMPARE(decoded->endpoint.address, original.endpoint.address);
            QCOMPARE(decoded->endpoint.request_target, original.endpoint.request_target);
            QCOMPARE(decoded->endpoint.port, quint16{443});
            QVERIFY(decoded->endpoint.tls);
        } else
            QCOMPARE(decoded->host, original.host);
    }
    void requestRejectsTruncationTrailingBytesUnknownModesAndOversize() {
        const auto encoded = encodeDownloadWorkerRequest(request());
        QVERIFY(!encoded.isEmpty());
        for (qsizetype size = 0; size < encoded.size(); ++size)
            QVERIFY(!decodeDownloadWorkerRequest(encoded.left(size)));
        QVERIFY(!decodeDownloadWorkerRequest(encoded + '\0'));
        auto changed = encoded;
        changed[5] = char{3};
        QVERIFY(!decodeDownloadWorkerRequest(changed));
        changed = encoded;
        changed[4] = char{2};
        QVERIFY(!decodeDownloadWorkerRequest(changed));
        QVERIFY(!decodeDownloadWorkerRequest(QByteArray(download_worker_max_request_bytes + 1, 'a')));
    }
    void requestRejectsInvalidPinsUnicodeAndControlInput() {
        auto value = request();
        value.runtime.core_sha256 = QByteArray(64, 'g');
        QVERIFY(encodeDownloadWorkerRequest(value).isEmpty());
        value = request();
        value.runtime.plugin_path += QChar{u'\0'};
        QVERIFY(encodeDownloadWorkerRequest(value).isEmpty());
        value = request();
        value.runtime.network_path += QChar{0xd800};
        QVERIFY(encodeDownloadWorkerRequest(value).isEmpty());
        value = request();
        value.host = QStringLiteral("host\r\ncommand");
        QVERIFY(encodeDownloadWorkerRequest(value).isEmpty());
        value = request(DownloadWorkerMode::Stream);
        value.endpoint.port = 0;
        QVERIFY(encodeDownloadWorkerRequest(value).isEmpty());
    }
    void resolveReplyRetainsAllAddressesAndBoundsCount() {
        DownloadWorkerFrame value;
        value.kind = DownloadWorkerFrameKind::Addresses;
        value.addresses = {QHostAddress{"93.184.215.14"}, QHostAddress{"2606:4700::1111"}};
        const auto encoded = encodeDownloadWorkerFrame(value);
        QVERIFY(!encoded.isEmpty());
        const auto decoded = decodeDownloadWorkerFrame(framedBody(encoded));
        QVERIFY(decoded.has_value());
        QCOMPARE(decoded->addresses, value.addresses);
        value.addresses.clear();
        QVERIFY(encodeDownloadWorkerFrame(value).isEmpty());
        for (int index = 0; index < 32; ++index)
            value.addresses.push_back(QHostAddress{quint32(0x01000000 + index)});
        QVERIFY(!encodeDownloadWorkerFrame(value).isEmpty());
        value.addresses.push_back(QHostAddress{"1.1.1.1"});
        QVERIFY(encodeDownloadWorkerFrame(value).isEmpty());
    }
    void dataFramesAreBoundedAndPreserveBinaryBytes() {
        DownloadWorkerFrame value;
        value.kind = DownloadWorkerFrameKind::Data;
        value.data = QByteArray(download_worker_max_data_bytes, '\0');
        value.data[8] = static_cast<char>(0xff);
        const auto packet = encodeDownloadWorkerFrame(value);
        QCOMPARE(packet.size(), download_worker_max_frame_bytes + 4);
        const auto decoded = decodeDownloadWorkerFrame(framedBody(packet));
        QVERIFY(decoded.has_value());
        QCOMPARE(decoded->data, value.data);
        value.data += 'x';
        QVERIFY(encodeDownloadWorkerFrame(value).isEmpty());
        value.data.clear();
        QVERIFY(encodeDownloadWorkerFrame(value).isEmpty());
    }
    void terminalEofIsAnExplicitBitNotAnExitInference() {
        for (const bool eof : {false, true}) {
            const auto decoded = decodeDownloadWorkerFrame(framedBody(encodeDownloadWorkerFrame(terminal(eof))));
            QVERIFY(decoded.has_value());
            QCOMPARE(decoded->authenticated_eof, eof);
            QCOMPARE(decoded->error, DownloadWorkerError::None);
        }
        const auto decoded =
            decodeDownloadWorkerFrame(framedBody(encodeDownloadWorkerFrame(terminal(false, DownloadWorkerError::Tls))));
        QVERIFY(decoded.has_value());
        QCOMPARE(decoded->error, DownloadWorkerError::Tls);
        QVERIFY(encodeDownloadWorkerFrame(terminal(true, DownloadWorkerError::Tls)).isEmpty());
        auto malformed = framedBody(encodeDownloadWorkerFrame(terminal()));
        malformed.back() = char{2};
        QVERIFY(!decodeDownloadWorkerFrame(malformed));
        malformed[1] = char{100};
        QVERIFY(!decodeDownloadWorkerFrame(malformed));
    }
    void frameDecoderAppliesBackpressureAtEveryFrameAndRejectsOversizeEarly() {
        DownloadWorkerFrame value;
        value.kind = DownloadWorkerFrameKind::Data;
        value.data = QByteArray(download_worker_max_data_bytes, 'x');
        const auto first = encodeDownloadWorkerFrame(value);
        const auto end = encodeDownloadWorkerFrame(terminal());
        QVERIFY(!first.isEmpty());
        DownloadWorkerFrameDecoder decoder;
        QCOMPARE(decoder.append(QByteArrayView{first + end}), first.size());
        QVERIFY(decoder.ready());
        QCOMPARE(decoder.append(QByteArrayView{end}), qsizetype{0});
        QCOMPARE(decoder.take()->data, value.data);
        QCOMPARE(decoder.append(QByteArrayView{end}), end.size());
        QVERIFY(decoder.take().has_value());
        QVERIFY(!decoder.incomplete());
        QByteArray oversized(4, '\0');
        qToLittleEndian<quint32>(quint32(download_worker_max_frame_bytes + 1), oversized.data());
        QCOMPARE(decoder.append(QByteArrayView{oversized}), qsizetype{4});
        QVERIFY(decoder.failed());
        QVERIFY(!decoder.take());
    }
    void frameDecoderSupportsEveryPartialBoundaryAndRejectsMalformedBody() {
        const auto packet = encodeDownloadWorkerFrame(terminal(true));
        QVERIFY(!packet.isEmpty());
        for (qsizetype boundary = 0; boundary <= packet.size(); ++boundary) {
            DownloadWorkerFrameDecoder decoder;
            QCOMPARE(decoder.append(QByteArrayView{packet.constData(), boundary}), boundary);
            if (boundary < packet.size()) {
                QVERIFY(!decoder.ready());
                QCOMPARE(decoder.incomplete(), boundary > 0);
                QCOMPARE(decoder.append(QByteArrayView{packet.constData() + boundary, packet.size() - boundary}),
                         packet.size() - boundary);
            }
            QVERIFY(decoder.ready());
            QVERIFY(decoder.take()->authenticated_eof);
        }
        auto malformed = packet;
        malformed[4] = char{100};
        DownloadWorkerFrameDecoder decoder;
        QCOMPARE(decoder.append(QByteArrayView{malformed}), malformed.size());
        QVERIFY(!decoder.take());
        QVERIFY(decoder.failed());
    }
};
QTEST_GUILESS_MAIN(DownloadWorkerProtocolTests)
#include "DownloadWorkerProtocolTests.moc"
