#include "DownloadHttpResponse.hpp"

#include <QTest>

#include <limits>

using music_player::m0::download_http_max_chunk_framing_bytes;
using music_player::m0::download_http_max_chunk_line_bytes;
using music_player::m0::download_http_max_header_bytes;
using music_player::m0::download_http_max_header_fields;
using music_player::m0::DownloadHttpError;
using music_player::m0::DownloadHttpResponse;

namespace {

const QByteArray fixed_response = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Type: audio/mpeg\r\n\r\nabcde";
const QByteArray chunked_headers = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";

} // namespace

class DownloadHttpResponseTests final : public QObject {
    Q_OBJECT

  private slots:
    void fixedLengthProvidesMetadataBeforeBodyAndCompletes() {
        DownloadHttpResponse decoder(5);
        const auto result = decoder.feed(fixed_response);
        QVERIFY(result.metadataAvailable);
        QVERIFY(result.finished);
        QVERIFY(!result.failed);
        QCOMPARE(result.body, QByteArray("abcde"));
        QCOMPARE(decoder.statusCode(), 200);
        QCOMPARE(decoder.header("content-type"), QByteArray("audio/mpeg"));
        QCOMPARE(decoder.header("CONTENT-TYPE"), QByteArray("audio/mpeg"));
        QVERIFY(decoder.header("not-present").isEmpty());
        QVERIFY(decoder.contentLength().has_value());
        QCOMPARE(*decoder.contentLength(), quint64(5));
        QVERIFY(decoder.finished());
        QVERIFY(!decoder.failed());
        QVERIFY(decoder.finishEof().finished);
    }

    void everySplitPreservesBinaryBodyWithoutWaitingForAnotherFeed() {
        const QByteArray body = QByteArray::fromHex("000d0aff01");
        auto response = fixed_response;
        response.chop(5);
        response += body;
        for (qsizetype split = 0; split <= response.size(); ++split) {
            DownloadHttpResponse decoder(5);
            const auto first = decoder.feed(QByteArrayView(response).first(split));
            QVERIFY(!first.failed);
            QVERIFY(first.body.size() <= split);
            if (!first.body.isEmpty())
                QVERIFY(first.metadataAvailable);
            const auto last = decoder.feed(QByteArrayView(response).sliced(split));
            QVERIFY(!last.failed);
            QVERIFY(last.finished);
            QVERIFY(last.body.size() <= response.size() - split);
            QCOMPARE(first.body + last.body, body);
        }
    }

    void chunkedStreamsOneByteAtATimeAndIgnoresSafeExtensionsAndTrailers() {
        const auto response =
            chunked_headers + "2;name=value;quoted=\"a\\\"b\"\r\nab\r\n3\r\ncde\r\n0\r\nX-Checksum: abc\r\n\r\n";
        DownloadHttpResponse decoder(5);
        QByteArray body;
        for (qsizetype offset = 0; offset < response.size(); ++offset) {
            const auto part = decoder.feed(QByteArrayView(response).sliced(offset, 1));
            QVERIFY(!part.failed);
            QVERIFY(part.body.size() <= 1);
            if (!part.body.isEmpty())
                QVERIFY(part.metadataAvailable);
            body += part.body;
            QCOMPARE(part.finished, offset == response.size() - 1);
        }
        QCOMPARE(body, QByteArray("abcde"));
        QVERIFY(!decoder.contentLength().has_value());
        QVERIFY(decoder.header("x-checksum").isEmpty());
    }

    void closeDelimitedCompletesOnlyOnOrderlyEof() {
        DownloadHttpResponse decoder(5);
        const auto first = decoder.feed("HTTP/1.0 200 OK\r\nContent-Encoding: identity\r\n\r\nab");
        QVERIFY(first.metadataAvailable);
        QVERIFY(!first.finished);
        QVERIFY(!first.failed);
        QCOMPARE(first.body, QByteArray("ab"));
        const auto last = decoder.feed("cde");
        QCOMPARE(last.body, QByteArray("cde"));
        QVERIFY(!last.finished);
        QVERIFY(decoder.finishEof().finished);
        QVERIFY(!decoder.contentLength().has_value());
    }

    void informationalResponsesDoNotExposeIntermediateHeaders() {
        DownloadHttpResponse decoder(5);
        const auto provisional = decoder.feed("HTTP/1.1 103 Early Hints\r\nLink: secret\r\n\r\n");
        QVERIFY(!provisional.metadataAvailable);
        QVERIFY(provisional.body.isEmpty());
        QCOMPARE(decoder.statusCode(), 0);
        QVERIFY(decoder.header("link").isEmpty());
        const auto final = decoder.feed(fixed_response);
        QCOMPARE(final.body, QByteArray("abcde"));
        QVERIFY(final.finished);
        QVERIFY(decoder.header("link").isEmpty());
    }

    void preservesSafeMetadataWithoutApplyingMediaDecodingToRedirectBodies() {
        DownloadHttpResponse metadata(1);
        const auto result = metadata.feed("HTTP/1.1 200 \r\nContent-Length:\t1 \t\r\nContent-Encoding: IDENTITY\r\n"
                                          "X-Test: one\r\nx-test: two\r\n\r\na");
        QVERIFY(result.finished);
        QVERIFY(!result.failed);
        QCOMPARE(result.body, QByteArray("a"));
        QCOMPARE(metadata.header("x-test"), QByteArray("one, two"));

        DownloadHttpResponse redirect(1);
        const auto ignored = redirect.feed("HTTP/1.1 302 Found\r\nContent-Encoding: gzip\r\n"
                                           "Content-Length: 999999999\r\nLocation: /next\r\n\r\nnot media");
        QVERIFY(ignored.metadataAvailable);
        QVERIFY(ignored.finished);
        QVERIFY(!ignored.failed);
        QVERIFY(ignored.body.isEmpty());
    }

    void partialContentEmitsBodyAndKeepsContentRange() {
        DownloadHttpResponse decoder(16);
        const auto result = decoder.feed(
            "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 4-7/10\r\nContent-Length: 4\r\n\r\nEFGH");
        QVERIFY(result.metadataAvailable);
        QVERIFY(result.finished);
        QVERIFY(!result.failed);
        QCOMPARE(result.body, QByteArray("EFGH"));
        QCOMPARE(decoder.statusCode(), 206);
        QCOMPARE(decoder.header("content-range"), QByteArray("bytes 4-7/10"));
        QVERIFY(decoder.feed("X").failed);
    }

    void nonSuccessBodyNeverBecomesMedia_data() {
        QTest::addColumn<int>("code");
        for (int code : {204, 301, 302, 303, 304, 307, 308, 401, 403, 404, 416, 500})
            QTest::newRow(qPrintable(QString::number(code))) << code;
    }

    void nonSuccessBodyNeverBecomesMedia() {
        QFETCH(int, code);
        DownloadHttpResponse decoder(5);
        const auto result = decoder.feed("HTTP/1.1 " + QByteArray::number(code) +
                                         " Other\r\nLocation: /a%2fb?sig=x%2Fy\r\n\r\nprivate error body");
        QVERIFY(result.metadataAvailable);
        QVERIFY(result.finished);
        QVERIFY(!result.failed);
        QVERIFY(result.body.isEmpty());
        QCOMPARE(decoder.statusCode(), code);
        QCOMPARE(decoder.header("location"), QByteArray("/a%2fb?sig=x%2Fy"));
    }

    void rejectsMalformedOrAmbiguousHeaders_data() {
        QTest::addColumn<QByteArray>("response");
        QTest::newRow("unsupported version") << QByteArray("HTTP/2.0 200 OK\r\n\r\n");
        QTest::newRow("chunked HTTP 1.0") << QByteArray("HTTP/1.0 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
        QTest::newRow("invalid status digits") << QByteArray("HTTP/1.1 2x0 OK\r\n\r\n");
        QTest::newRow("invalid status range") << QByteArray("HTTP/1.1 600 Invalid\r\n\r\n");
        QTest::newRow("missing reason separator") << QByteArray("HTTP/1.1 200\r\n\r\n");
        QTest::newRow("invalid reason control") << QByteArray("HTTP/1.1 200 O\x01K\r\n\r\n");
        QTest::newRow("bare LF") << QByteArray("HTTP/1.1 200 OK\n\n");
        QTest::newRow("bare CR") << QByteArray("HTTP/1.1 200 OK\rX\r\n\r\n");
        QTest::newRow("leading whitespace") << QByteArray("HTTP/1.1 200 OK\r\n Content-Length: 0\r\n\r\n");
        QTest::newRow("whitespace before colon") << QByteArray("HTTP/1.1 200 OK\r\nContent-Length : 0\r\n\r\n");
        QTest::newRow("invalid field name") << QByteArray("HTTP/1.1 200 OK\r\nX(Name): z\r\n\r\n");
        QTest::newRow("obs fold") << QByteArray("HTTP/1.1 200 OK\r\nX: a\r\n b\r\n\r\n");
        QTest::newRow("NUL field") << (QByteArray("HTTP/1.1 200 OK\r\nX: a") + '\0' + "b\r\n\r\n");
        QTest::newRow("DEL field") << QByteArray("HTTP/1.1 200 OK\r\nX: \x7f\r\n\r\n");
        QTest::newRow("same duplicate length")
            << QByteArray("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\nabcde");
        QTest::newRow("conflicting length")
            << QByteArray("HTTP/1.1 200 OK\r\nContent-Length: 5\r\ncontent-length: 3\r\n\r\nabcde");
        QTest::newRow("length list") << QByteArray("HTTP/1.1 200 OK\r\nContent-Length: 5, 5\r\n\r\nabcde");
        QTest::newRow("negative length") << QByteArray("HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n");
        QTest::newRow("plus length") << QByteArray("HTTP/1.1 200 OK\r\nContent-Length: +1\r\n\r\na");
        QTest::newRow("overflow length") << QByteArray(
            "HTTP/1.1 200 OK\r\nContent-Length: 18446744073709551616\r\n\r\n");
        QTest::newRow("empty length") << QByteArray("HTTP/1.1 200 OK\r\nContent-Length:\r\n\r\n");
        QTest::newRow("TE plus CL") << QByteArray(
            "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 0\r\n\r\n");
        QTest::newRow("TE duplicate") << QByteArray(
            "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nTransfer-Encoding: chunked\r\n\r\n");
        QTest::newRow("TE list") << QByteArray("HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n");
        QTest::newRow("unsupported TE") << QByteArray("HTTP/1.1 200 OK\r\nTransfer-Encoding: identity\r\n\r\n");
        QTest::newRow("compressed content") << QByteArray("HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\n");
        QTest::newRow("empty content encoding") << QByteArray("HTTP/1.1 200 OK\r\nContent-Encoding:\r\n\r\n");
        QTest::newRow("duplicate encoding")
            << QByteArray("HTTP/1.1 200 OK\r\nContent-Encoding: identity\r\nContent-Encoding: identity\r\n\r\n");
        QTest::newRow("duplicate location") << QByteArray("HTTP/1.1 302 Found\r\nLocation: /a\r\nLocation: /b\r\n\r\n");
        QTest::newRow("upgrade") << QByteArray("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n\r\n");
        QTest::newRow("upgrade header on success") << QByteArray("HTTP/1.1 200 OK\r\nUpgrade: websocket\r\n\r\n");
        QTest::newRow("informational with length") << QByteArray("HTTP/1.1 103 Hints\r\nContent-Length: 1\r\n\r\n");
        QTest::newRow("five informational responses") << QByteArray("HTTP/1.1 100 Continue\r\n\r\n").repeated(5);
    }

    void rejectsMalformedOrAmbiguousHeaders() {
        QFETCH(QByteArray, response);
        DownloadHttpResponse decoder(20);
        const auto result = decoder.feed(response);
        QVERIFY(result.failed);
        QVERIFY(result.body.isEmpty());
        QVERIFY(!result.finished);
        QVERIFY(decoder.failed());
        QVERIFY(decoder.feed(fixed_response).failed);
        QVERIFY(decoder.finishEof().failed);
    }

    void rejectsInvalidChunks_data() {
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("invalid hex") << QByteArray("g\r\n");
        QTest::newRow("negative size") << QByteArray("-1\r\n");
        QTest::newRow("plus size") << QByteArray("+1\r\n");
        QTest::newRow("empty size") << QByteArray("\r\n");
        QTest::newRow("hex overflow") << QByteArray("10000000000000000\r\n");
        QTest::newRow("missing body CRLF") << QByteArray("1\r\naXX");
        QTest::newRow("empty extension name") << QByteArray("1;\r\na\r\n0\r\n\r\n");
        QTest::newRow("invalid extension value") << QByteArray("1;x=()\r\na\r\n0\r\n\r\n");
        QTest::newRow("unclosed quoted extension") << QByteArray("1;x=\"a\r\na\r\n0\r\n\r\n");
        QTest::newRow("trailer length") << QByteArray("0\r\nContent-Length: 0\r\n\r\n");
        QTest::newRow("trailer transfer encoding") << QByteArray("0\r\nTransfer-Encoding: chunked\r\n\r\n");
        QTest::newRow("trailer location") << QByteArray("0\r\nLocation: /other\r\n\r\n");
        QTest::newRow("trailer content encoding") << QByteArray("0\r\nContent-Encoding: gzip\r\n\r\n");
        QTest::newRow("trailer folded") << QByteArray("0\r\nX: a\r\n folded\r\n\r\n");
        QTest::newRow("trailing bytes") << QByteArray("0\r\n\r\nx");
    }

    void rejectsInvalidChunks() {
        QFETCH(QByteArray, body);
        DownloadHttpResponse decoder(20);
        const auto result = decoder.feed(chunked_headers + body);
        QVERIFY(result.failed);
        QVERIFY(result.body.isEmpty());
    }

    void truncatedInputCannotComplete_data() {
        QTest::addColumn<QByteArray>("response");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("status") << QByteArray("HTTP/1.1 200");
        QTest::newRow("headers") << QByteArray("HTTP/1.1 200 OK\r\n");
        QTest::newRow("fixed body") << fixed_response.first(fixed_response.size() - 1);
        QTest::newRow("chunk header") << (chunked_headers + "1");
        QTest::newRow("chunk body") << (chunked_headers + "2\r\na");
        QTest::newRow("chunk CRLF") << (chunked_headers + "1\r\na\r");
        QTest::newRow("last chunk") << (chunked_headers + "1\r\na\r\n");
        QTest::newRow("trailer") << (chunked_headers + "0\r\nX: a\r\n");
    }

    void truncatedInputCannotComplete() {
        QFETCH(QByteArray, response);
        DownloadHttpResponse decoder(5);
        QVERIFY(!decoder.feed(response).finished);
        const auto end = decoder.finishEof();
        QVERIFY(end.failed);
        QVERIFY(!end.finished);
        QVERIFY(end.body.isEmpty());
    }

    void absoluteBodyQuotaAppliesToEveryFramingAndDeclaredLengths() {
        DownloadHttpResponse fixed(4);
        const auto known = fixed.feed(fixed_response);
        QVERIFY(known.failed);
        QVERIFY(known.body.isEmpty());
        DownloadHttpResponse close(4);
        QVERIFY(!close.feed("HTTP/1.1 200 OK\r\n\r\nabc").failed);
        const auto overflow = close.feed("de");
        QVERIFY(overflow.failed);
        QVERIFY(overflow.body.isEmpty());
        DownloadHttpResponse chunks(4);
        QVERIFY(!chunks.feed(chunked_headers + "3\r\nabc\r\n").failed);
        QVERIFY(chunks.feed("2\r\n").failed);
        DownloadHttpResponse zero(0);
        QVERIFY(zero.feed("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n").finished);
    }

    void errorsDistinguishAbsoluteQuotasFromMalformedMedia_data() {
        QTest::addColumn<QByteArray>("response");
        QTest::addColumn<DownloadHttpError>("error");
        QTest::newRow("declared body quota") << fixed_response << DownloadHttpError::LimitExceeded;
        QTest::newRow("chunk body quota")
            << (chunked_headers + "5\r\nabcde\r\n0\r\n\r\n") << DownloadHttpError::LimitExceeded;
        QTest::newRow("close body quota")
            << QByteArray("HTTP/1.1 200 OK\r\n\r\nabcde") << DownloadHttpError::LimitExceeded;
        QTest::newRow("header quota") << QByteArray(download_http_max_header_bytes + 1, 'x')
                                      << DownloadHttpError::LimitExceeded;
        QTest::newRow("chunk line quota")
            << (chunked_headers + "0;x=" + QByteArray(download_http_max_chunk_line_bytes, 'a'))
            << DownloadHttpError::LimitExceeded;
        QTest::newRow("invalid status") << QByteArray("HTTP/1.1 invalid\r\n") << DownloadHttpError::Malformed;
        QTest::newRow("invalid chunks") << (chunked_headers + "g\r\n") << DownloadHttpError::Malformed;
    }

    void errorsDistinguishAbsoluteQuotasFromMalformedMedia() {
        QFETCH(QByteArray, response);
        QFETCH(DownloadHttpError, error);
        DownloadHttpResponse decoder(4);
        QCOMPARE(decoder.error(), DownloadHttpError::None);
        QVERIFY(decoder.feed(response).failed);
        QCOMPARE(decoder.error(), error);
        QVERIFY(decoder.finishEof().failed);
        QCOMPARE(decoder.error(), error);
    }

    void extraBytesAfterFixedLengthAreNotAnotherResponse() {
        DownloadHttpResponse decoder(20);
        const auto result = decoder.feed(fixed_response + 'x');
        QVERIFY(result.failed);
        QVERIFY(result.body.isEmpty());
        DownloadHttpResponse separate(20);
        QVERIFY(separate.feed(fixed_response).finished);
        QVERIFY(separate.feed("x").failed);
    }

    void aggregateHeadersAndTrailersHaveAnInclusiveByteBudget() {
        const QByteArray prefix = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nX: ";
        const QByteArray suffix = "\r\n\r\n";
        const auto fill = download_http_max_header_bytes - prefix.size() - suffix.size();
        DownloadHttpResponse atLimit(1);
        QVERIFY(atLimit.feed(prefix + QByteArray(fill, 'a') + suffix).finished);
        DownloadHttpResponse excessive(1);
        QVERIFY(excessive.feed(prefix + QByteArray(fill + 1, 'a') + suffix).failed);
        DownloadHttpResponse unterminated(1);
        QVERIFY(unterminated.feed(QByteArray(download_http_max_header_bytes + 1, 'x')).failed);
        DownloadHttpResponse trailers(1);
        QVERIFY(trailers.feed(chunked_headers + "0\r\nX: " + QByteArray(download_http_max_header_bytes, 'a')).failed);
    }

    void aggregateFieldCountIncludesIgnoredInformationalAndTrailerFields() {
        QByteArray fields;
        for (qsizetype i = 0; i < download_http_max_header_fields; ++i)
            fields += "X-" + QByteArray::number(i) + ": a\r\n";
        DownloadHttpResponse exact(1);
        QVERIFY(!exact.feed("HTTP/1.1 200 OK\r\n" + fields + "\r\n").failed);
        QVERIFY(exact.finishEof().finished);
        DownloadHttpResponse extra(1);
        QVERIFY(extra.feed("HTTP/1.1 200 OK\r\n" + fields + "X: a\r\n\r\n").failed);
        DownloadHttpResponse informational(1);
        QVERIFY(informational.feed("HTTP/1.1 103 Hints\r\n" + fields + "\r\n" + fixed_response).failed);
        DownloadHttpResponse trailer(1);
        QVERIFY(trailer.feed(chunked_headers + "0\r\n" + fields + "\r\n").failed);
    }

    void chunkExtensionsAndAggregateFramingAreBounded() {
        DownloadHttpResponse line(1);
        QVERIFY(line.feed(chunked_headers + "0;x=" + QByteArray(download_http_max_chunk_line_bytes, 'a')).failed);
        DownloadHttpResponse framing(download_http_max_chunk_framing_bytes);
        QVERIFY(!framing.feed(chunked_headers).failed);
        const QByteArray chunk = "1\r\na\r\n";
        // This adversarial shape spends five framing bytes per body byte.
        const auto over = chunk.repeated(static_cast<qsizetype>(download_http_max_chunk_framing_bytes / 5 + 1));
        const auto result = framing.feed(over);
        QVERIFY(result.failed);
        QVERIFY(result.body.isEmpty());
    }
};

QTEST_APPLESS_MAIN(DownloadHttpResponseTests)
#include "DownloadHttpResponseTests.moc"
