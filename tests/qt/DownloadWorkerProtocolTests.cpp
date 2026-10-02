#include "DownloadWorkerProtocol.hpp"

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
} // namespace

class DownloadWorkerProtocolTests final : public QObject {
    Q_OBJECT
  private slots:
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
