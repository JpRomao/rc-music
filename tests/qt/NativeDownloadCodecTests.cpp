#include "NativeDownloadCodec.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace music_player::m0;

namespace {

const QString request_id = QStringLiteral("01234567-89ab-4cde-8fab-0123456789ab");

QJsonObject item(const QString& url = QStringLiteral("https://media.example.test/audio.mp3"),
                 const QString& title = QStringLiteral("Áudio"), const QString& kind = QStringLiteral("audio")) {
    return {{QStringLiteral("url"), url}, {QStringLiteral("title"), title}, {QStringLiteral("kind"), kind}};
}

QJsonObject request(const QJsonArray& items = QJsonArray{item()}) {
    return {{QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("type"), QStringLiteral("download.request")},
            {QStringLiteral("requestId"), request_id},
            {QStringLiteral("items"), items}};
}

QByteArray bytes(const QJsonObject& value) { return QJsonDocument{value}.toJson(QJsonDocument::Compact); }

QJsonObject acceptedResponse(const QJsonArray& ids = {QStringLiteral("job_1"), QStringLiteral("Job-2")}) {
    return {{QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("type"), QStringLiteral("download.response")},
            {QStringLiteral("requestId"), request_id},
            {QStringLiteral("status"), QStringLiteral("accepted")},
            {QStringLiteral("jobIds"), ids}};
}

QJsonObject rejectedResponse(const QString& code = QStringLiteral("busy")) {
    auto response = acceptedResponse();
    response.insert(QStringLiteral("status"), QStringLiteral("rejected"));
    response.remove(QStringLiteral("jobIds"));
    response.insert(QStringLiteral("code"), code);
    return response;
}

} // namespace

class NativeDownloadCodecTests final : public QObject {
    Q_OBJECT

  private slots:
    void agreesWithSharedBrowserProtocolVectors_data() {
        QTest::addColumn<QByteArray>("payload");
        QTest::addColumn<bool>("accepted");
        QFile file{QString::fromUtf8(RC_MUSIC_NATIVE_PROTOCOL_VECTORS_PATH)};
        QVERIFY2(file.open(QIODevice::ReadOnly), "Shared protocol corpus must be readable");
        const auto cases = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("cases")).toArray();
        QVERIFY(!cases.isEmpty());
        for (const auto& value : cases) {
            const auto entry = value.toObject();
            QVERIFY(entry.value(QStringLiteral("name")).isString());
            QVERIFY(entry.value(QStringLiteral("accepted")).isBool());
            QVERIFY(entry.value(QStringLiteral("request")).isObject());
            QTest::newRow(qPrintable(entry.value(QStringLiteral("name")).toString()))
                << bytes(entry.value(QStringLiteral("request")).toObject())
                << entry.value(QStringLiteral("accepted")).toBool();
        }
    }

    void agreesWithSharedBrowserProtocolVectors() {
        QFETCH(QByteArray, payload);
        QFETCH(bool, accepted);
        const auto decoded = decodeNativeDownloadRequest(payload);
        QCOMPARE(decoded.error == DownloadSubmissionError::None, accepted);
        if (!accepted) {
            QVERIFY(decoded.request.items.empty());
            return;
        }
        const auto sent = QJsonDocument::fromJson(payload).object().value(QStringLiteral("items")).toArray();
        QCOMPARE(decoded.request.items.size(), static_cast<std::size_t>(sent.size()));
        for (qsizetype index = 0; index < sent.size(); ++index) {
            const auto original = sent[index].toObject();
            const auto& actual = decoded.request.items[static_cast<std::size_t>(index)];
            QCOMPARE(actual.url, original.value(QStringLiteral("url")).toString().toStdString());
            QCOMPARE(actual.title, original.value(QStringLiteral("title")).toString().toStdString());
        }
    }

    void rejectsNonJsonNumericSpellings_data() {
        QTest::addColumn<QByteArray>("token");
        QTest::newRow("trailing decimal point") << QByteArray{"1."};
        QTest::newRow("fraction without digit") << QByteArray{"1.e0"};
        QTest::newRow("leading decimal point") << QByteArray{".1e1"};
        QTest::newRow("leading plus") << QByteArray{"+1"};
        QTest::newRow("leading zero") << QByteArray{"01"};
    }

    void rejectsNonJsonNumericSpellings() {
        QFETCH(QByteArray, token);
        auto payload = bytes(request());
        payload.replace("\"schemaVersion\":1", "\"schemaVersion\":" + token);
        QCOMPARE(decodeNativeDownloadRequest(payload).error, DownloadSubmissionError::InvalidRequest);
    }

    void acceptsEquivalentJsonNumberSpellings() {
        for (const QByteArray& token :
             {QByteArray{"1.0"}, QByteArray{"1e0"}, QByteArray{"1E+0"}, QByteArray{"1.00e-0"}}) {
            auto payload = bytes(request());
            payload.replace("\"schemaVersion\":1", "\"schemaVersion\":" + token);
            QCOMPARE(decodeNativeDownloadRequest(payload).error, DownloadSubmissionError::None);
        }
    }

    void refusesNoncanonicalBrowserWireUrls_data() {
        QTest::addColumn<QString>("url");
        QTest::newRow("raw Unicode path") << QStringLiteral("https://media.example.test/música.mp3");
        QTest::newRow("uppercase host") << QStringLiteral("https://MEDIA.example.test/a.mp3");
        QTest::newRow("missing slash") << QStringLiteral("https://media.example.test");
        QTest::newRow("query with missing slash") << QStringLiteral("https://media.example.test?audio.mp3");
        QTest::newRow("dot segment") << QStringLiteral("https://media.example.test/a/../b.mp3");
        QTest::newRow("encoded dot segment") << QStringLiteral("https://media.example.test/%2e/a.mp3");
        QTest::newRow("mixed encoded dot segment") << QStringLiteral("https://media.example.test/a/.%2E/b.mp3");
        QTest::newRow("final encoded double dot") << QStringLiteral("https://media.example.test/a/%2e%2e");
        QTest::newRow("default HTTPS port") << QStringLiteral("https://media.example.test:443/a.mp3");
        QTest::newRow("default HTTP port") << QStringLiteral("http://media.example.test:80/a.mp3");
        QTest::newRow("unescaped path braces") << QStringLiteral("https://media.example.test/a{b}.mp3");
        QTest::newRow("unescaped path backtick") << QStringLiteral("https://media.example.test/a`b.mp3");
        QTest::newRow("unescaped query apostrophe") << QStringLiteral("https://media.example.test/a?x='y'");
        QTest::newRow("oversized DNS label")
            << QStringLiteral("https://") + QString(64, QLatin1Char('a')) + QStringLiteral(".test/a");
    }

    void refusesNoncanonicalBrowserWireUrls() {
        QFETCH(QString, url);
        QCOMPARE(decodeNativeDownloadRequest(bytes(request({item(url)}))).error,
                 DownloadSubmissionError::UnsupportedSource);
    }

    void decodesOrderedAudioAndVideoWithoutChangingSignedUrls() {
        const QString signed_url = QStringLiteral("https://media.example.test/a%2fb.mp4?sig=A%2fb%2FB&x=1+2&x=3");
        const auto decoded = decodeNativeDownloadRequest(
            bytes(request({item(), item(signed_url, QStringLiteral("Vídeo 日本"), QStringLiteral("video"))})));
        QCOMPARE(decoded.error, DownloadSubmissionError::None);
        QCOMPARE(decoded.request.request_id, request_id.toStdString());
        QCOMPARE(decoded.request.items.size(), std::size_t{2});
        QCOMPARE(decoded.request.items[0].kind, DownloadMediaKind::Audio);
        QCOMPARE(decoded.request.items[1].kind, DownloadMediaKind::Video);
        QCOMPARE(decoded.request.items[1].url, signed_url.toStdString());
        QCOMPARE(decoded.request.items[1].title, QStringLiteral("Vídeo 日本").toStdString());
    }

    void rejectsMalformedSchemas_data() {
        QTest::addColumn<QByteArray>("payload");
        QTest::newRow("empty") << QByteArray{};
        QTest::newRow("broken JSON") << QByteArray{"{"};
        QTest::newRow("array root") << QByteArray{"[]"};
        QTest::newRow("trailing data") << bytes(request()) + QByteArray{"{}"};
        auto invalid_escape = bytes(request());
        invalid_escape.replace("Áudio", "bad\\q");
        QTest::newRow("invalid escape") << invalid_escape;
        invalid_escape = bytes(request());
        invalid_escape.replace("Áudio", "bad\\v");
        QTest::newRow("invalid vertical escape") << invalid_escape;
        auto changed = request();
        changed.insert(QStringLiteral("schemaVersion"), true);
        QTest::newRow("boolean version") << bytes(changed);
        changed.insert(QStringLiteral("schemaVersion"), 1.1);
        QTest::newRow("fractional version") << bytes(changed);
        changed.insert(QStringLiteral("schemaVersion"), 4294967297.0);
        QTest::newRow("overflowing version") << bytes(changed);
        changed = request();
        changed.insert(QStringLiteral("type"), QStringLiteral("openSources"));
        QTest::newRow("other command") << bytes(changed);
        changed = request();
        changed.insert(QStringLiteral("command"), QStringLiteral("anything"));
        QTest::newRow("extra root field") << bytes(changed);
        changed = request();
        changed.remove(QStringLiteral("type"));
        QTest::newRow("missing field") << bytes(changed);
        auto changed_item = item();
        changed_item.insert(QStringLiteral("headers"), QJsonObject{});
        QTest::newRow("extra item field") << bytes(request({changed_item}));
        changed_item = item();
        changed_item.insert(QStringLiteral("kind"), QStringLiteral("command"));
        QTest::newRow("unknown kind") << bytes(request({changed_item}));
        changed_item.insert(QStringLiteral("kind"), true);
        QTest::newRow("boolean kind") << bytes(request({changed_item}));
        QTest::newRow("nonobject item") << bytes(request({1}));
        changed = request();
        changed.insert(QStringLiteral("items"), QJsonObject{});
        QTest::newRow("nonarray items") << bytes(changed);
        QTest::newRow("duplicate URL") << bytes(request({item(), item()}));
    }

    void rejectsMalformedSchemas() {
        QFETCH(QByteArray, payload);
        const auto decoded = decodeNativeDownloadRequest(payload);
        QCOMPARE(decoded.error, DownloadSubmissionError::InvalidRequest);
        QVERIFY(decoded.request.items.empty());
    }

    void rejectsDuplicateMembersEvenEscapedAliases_data() {
        QTest::addColumn<QByteArray>("payload");
        auto duplicate_root = bytes(request());
        duplicate_root.insert(1, "\"schemaVersion\":1,");
        QTest::newRow("identical root value") << duplicate_root;
        auto escaped_root = bytes(request());
        escaped_root.insert(1, "\"schema\\u0056ersion\":2,");
        QTest::newRow("escaped root alias") << escaped_root;
        auto duplicate_item = bytes(request());
        duplicate_item.replace("\"url\":", "\"url\":\"https://evil.example.test/a\",\"url\":");
        QTest::newRow("item duplicate") << duplicate_item;
        auto escaped_item = bytes(request());
        escaped_item.replace("\"title\":", "\"ti\\u0074le\":\"first\",\"title\":");
        QTest::newRow("escaped item alias") << escaped_item;
    }

    void rejectsDuplicateMembersEvenEscapedAliases() {
        QFETCH(QByteArray, payload);
        const auto decoded = decodeNativeDownloadRequest(payload);
        QCOMPARE(decoded.error, DownloadSubmissionError::InvalidRequest);
        QVERIFY(decoded.request.items.empty());
    }

    void acceptsEscapedKeysAndStringPunctuationWithoutFalseDuplicates() {
        auto payload = bytes(request(
            {item(QStringLiteral("https://media.example.test/a"), QStringLiteral("{\"url\":\"x\",\"url\":\"x\"}\\"))}));
        payload.replace("\"schemaVersion\":", "\"schema\\u0056ersion\":");
        const auto decoded = decodeNativeDownloadRequest(payload);
        QCOMPARE(decoded.error, DownloadSubmissionError::None);
        QCOMPARE(decoded.request.items.front().title, std::string{"{\"url\":\"x\",\"url\":\"x\"}\\"});
    }

    void preservesSupportedUrlRepresentations_data() {
        QTest::addColumn<QString>("url");
        QTest::newRow("encoded slash tilde query punctuation")
            << QStringLiteral("https://media.example.test/a%2fb%7E?q[]=1+2&q[]=3&sig=AZ%2Faz%2f");
        QTest::newRow("trailing host dot") << QStringLiteral("https://media.example.test./a.mp3");
        QTest::newRow("punycode host and Unicode path encoding")
            << QStringLiteral("https://xn--msica-7ua.example.test/%E6%97%A5%E6%9C%AC.mp3");
        QTest::newRow("domain is not a blocked subdomain") << QStringLiteral("http://notyoutube.com/a.mp3");
    }

    void preservesSupportedUrlRepresentations() {
        QFETCH(QString, url);
        const auto decoded = decodeNativeDownloadRequest(bytes(request({item(url)})));
        QCOMPARE(decoded.error, DownloadSubmissionError::None);
        QCOMPARE(decoded.request.items.front().url, url.toStdString());
    }

    void checksRequestIdentifiers_data() {
        QTest::addColumn<QString>("id");
        QTest::newRow("missing") << QString{};
        QTest::newRow("wrong version") << QStringLiteral("01234567-89ab-3cde-8fab-0123456789ab");
        QTest::newRow("wrong variant") << QStringLiteral("01234567-89ab-4cde-7fab-0123456789ab");
        QTest::newRow("padding") << request_id + QLatin1Char(' ');
        QTest::newRow("not hex") << QStringLiteral("01234567-89ab-4cde-8fab-0123456789az");
    }

    void checksRequestIdentifiers() {
        QFETCH(QString, id);
        auto changed = request();
        changed.insert(QStringLiteral("requestId"), id);
        const auto decoded = decodeNativeDownloadRequest(bytes(changed));
        QCOMPARE(decoded.error, DownloadSubmissionError::InvalidRequest);
        QVERIFY(decoded.request.request_id.empty());
        QVERIFY(encodeNativeDownloadResponse(id.toStdString(), {}).isEmpty());
    }

    void retainsValidRequestIdForAnAtomicRejection() {
        const auto decoded =
            decodeNativeDownloadRequest(bytes(request({item(), item(QStringLiteral("file:///C:/song.mp3"))})));
        QCOMPARE(decoded.error, DownloadSubmissionError::UnsupportedSource);
        QCOMPARE(decoded.request.request_id, request_id.toStdString());
        QVERIFY(decoded.request.items.empty());
        auto upper = request();
        upper.insert(QStringLiteral("requestId"), request_id.toUpper());
        QCOMPARE(decodeNativeDownloadRequest(bytes(upper)).request.request_id, request_id.toUpper().toStdString());
    }

    void refusesUnsupportedUrls_data() {
        QTest::addColumn<QString>("url");
        const QStringList rejected{QStringLiteral("file:///C:/song.mp3"),
                                   QStringLiteral("blob:https://media.example.test/id"),
                                   QStringLiteral("//media.example.test/a.mp3"),
                                   QStringLiteral("https://user:secret@media.example.test/a"),
                                   QStringLiteral("https://media.example.test/a#fragment"),
                                   QStringLiteral("http://127.0.0.1/a"),
                                   QStringLiteral("http://2130706433/a"),
                                   QStringLiteral("http://0x7f.1/a"),
                                   QStringLiteral("http://[::1]/a"),
                                   QStringLiteral("https://localhost/a"),
                                   QStringLiteral("https://music.local/a"),
                                   QStringLiteral("https://music.internal/a"),
                                   QStringLiteral("https://music.home.arpa/a"),
                                   QStringLiteral("https://single/a"),
                                   QStringLiteral("https://media.example.test:444/a"),
                                   QStringLiteral("https://youtube.com/a"),
                                   QStringLiteral("https://m.youtube.com./a"),
                                   QStringLiteral("https://youtu.be/a"),
                                   QStringLiteral("https://r1.googlevideo.com/a"),
                                   QStringLiteral("https://youtube-nocookie.com/a"),
                                   QStringLiteral("https://youtubei.googleapis.com/a"),
                                   QStringLiteral("https://youtube.googleapis.com/a"),
                                   QStringLiteral("https://youtubekids.com/a"),
                                   QStringLiteral("https://media.example.test/has space"),
                                   QStringLiteral("https://media.example.test/a\\b"),
                                   QStringLiteral("https://media.example.test/%"),
                                   QStringLiteral("https://media.example.test/%zz"),
                                   QStringLiteral("https://media.example.test/a") + QChar{0},
                                   QStringLiteral("https://media.example.test/a") + QChar{0x85}};
        for (qsizetype index = 0; index < rejected.size(); ++index)
            QTest::newRow(qPrintable(QStringLiteral("URL-%1").arg(index))) << rejected[index];
    }

    void refusesUnsupportedUrls() {
        QFETCH(QString, url);
        const auto decoded = decodeNativeDownloadRequest(bytes(request({item(url)})));
        QCOMPARE(decoded.error, DownloadSubmissionError::UnsupportedSource);
        QVERIFY(decoded.request.items.empty());
    }

    void rejectsUnsafeOrUnnormalizedTitles_data() {
        QTest::addColumn<QString>("title");
        QTest::newRow("empty") << QString{};
        QTest::newRow("padding") << QStringLiteral(" Audio ");
        QTest::newRow("JavaScript whitespace") << QChar{0xfeff} + QStringLiteral("Audio");
        QTest::newRow("newline") << QStringLiteral("A\nB");
        QTest::newRow("control") << QStringLiteral("A") + QChar{0x85};
        QTest::newRow("bidi override") << QStringLiteral("A") + QChar{0x202e};
        QTest::newRow("bidi isolate") << QStringLiteral("A") + QChar{0x2068};
        QTest::newRow("lone high surrogate") << QStringLiteral("A") + QChar{0xd800};
        QTest::newRow("lone low surrogate") << QStringLiteral("A") + QChar{0xdc00};
        QTest::newRow("too long") << QString(201, QLatin1Char('a'));
    }

    void rejectsUnsafeOrUnnormalizedTitles() {
        QFETCH(QString, title);
        const auto decoded =
            decodeNativeDownloadRequest(bytes(request({item(QStringLiteral("https://media.example.test/a"), title)})));
        QVERIFY(decoded.error != DownloadSubmissionError::None);
        QVERIFY(decoded.request.items.empty());
    }

    void acceptsUtf16TitleBoundaryAndLiteralHostileText() {
        QString title = QStringLiteral("<script>' &; $() ");
        while (title.size() < 198)
            title += QLatin1Char('a');
        title += QString::fromUcs4(U"🎵");
        QCOMPARE(title.size(), qsizetype{200});
        const auto decoded =
            decodeNativeDownloadRequest(bytes(request({item(QStringLiteral("https://media.example.test/a"), title)})));
        QCOMPARE(decoded.error, DownloadSubmissionError::None);
        QCOMPARE(decoded.request.items.front().title, title.toStdString());
    }

    void enforcesBatchUrlAndPayloadBounds() {
        QJsonArray items;
        for (int index = 0; index < 20; ++index)
            items.push_back(item(QStringLiteral("https://media.example.test/%1").arg(index)));
        QCOMPARE(decodeNativeDownloadRequest(bytes(request(items))).error, DownloadSubmissionError::None);
        items.push_back(item(QStringLiteral("https://media.example.test/21")));
        QCOMPARE(decodeNativeDownloadRequest(bytes(request(items))).error, DownloadSubmissionError::LimitExceeded);
        QCOMPARE(decodeNativeDownloadRequest(bytes(request({}))).error, DownloadSubmissionError::InvalidRequest);
        QString url = QStringLiteral("https://media.example.test/");
        url += QString(8192 - url.size(), QLatin1Char('a'));
        QCOMPARE(decodeNativeDownloadRequest(bytes(request({item(url)}))).error, DownloadSubmissionError::None);
        QCOMPARE(decodeNativeDownloadRequest(bytes(request({item(url + QLatin1Char('b'))}))).error,
                 DownloadSubmissionError::LimitExceeded);
        auto payload = bytes(request());
        payload.append(QByteArray(256 * 1024 - payload.size(), ' '));
        QCOMPARE(decodeNativeDownloadRequest(payload).error, DownloadSubmissionError::None);
        payload.append(' ');
        QCOMPARE(decodeNativeDownloadRequest(payload).error, DownloadSubmissionError::LimitExceeded);
    }

    void rejectsInvalidUtf8AndExcessiveNesting() {
        auto payload = bytes(request());
        payload.replace("Áudio", QByteArray::fromHex("c328"));
        QCOMPARE(decodeNativeDownloadRequest(payload).error, DownloadSubmissionError::InvalidRequest);
        payload = bytes(request());
        payload.replace("Áudio", QByteArray::fromHex("eda080"));
        QCOMPARE(decodeNativeDownloadRequest(payload).error, DownloadSubmissionError::InvalidRequest);
        payload = QByteArray(5000, '[') + QByteArray(5000, ']');
        QCOMPARE(decodeNativeDownloadRequest(payload).error, DownloadSubmissionError::InvalidRequest);
    }

    void refusesBomAliasesWhilePreservingInteriorTitleCharacters() {
        const auto bom = QByteArray::fromHex("efbbbf");
        auto payload = bytes(request());
        payload.replace("\"requestId\":", '"' + bom + "requestId\":");
        QCOMPARE(decodeNativeDownloadRequest(payload).error, DownloadSubmissionError::InvalidRequest);
        payload = bytes(request());
        payload.replace(request_id.toUtf8(), bom + request_id.toUtf8());
        QCOMPARE(decodeNativeDownloadRequest(payload).error, DownloadSubmissionError::InvalidRequest);
        payload = bom + bytes(request());
        QCOMPARE(decodeNativeDownloadRequest(payload).error, DownloadSubmissionError::InvalidRequest);
        for (const auto& prefix : {QStringLiteral("A"), QStringLiteral("A\"")}) {
            const auto title = prefix + QChar{0xfeff} + QStringLiteral("B");
            payload = bytes(request({item(QStringLiteral("https://media.example.test/a"), title)}));
            const auto literal = decodeNativeDownloadRequest(payload);
            QCOMPARE(literal.error, DownloadSubmissionError::None);
            QCOMPARE(literal.request.items.front().title, title.toStdString());
            payload.replace(bom, "\\ufeff");
            const auto escaped = decodeNativeDownloadRequest(payload);
            QCOMPARE(escaped.error, DownloadSubmissionError::None);
            QCOMPARE(escaped.request.items.front().title, title.toStdString());
        }
    }

    void serializesAcceptedAndEveryTypedRejection() {
        const auto accepted =
            QJsonDocument::fromJson(encodeNativeDownloadResponse(request_id.toStdString(),
                                                                 {DownloadSubmissionError::None, {"job_1", "Job-2"}}))
                .object();
        QCOMPARE(accepted.size(), qsizetype{5});
        QCOMPARE(accepted.value(QStringLiteral("schemaVersion")).toInt(), 1);
        QCOMPARE(accepted.value(QStringLiteral("type")).toString(), QStringLiteral("download.response"));
        QCOMPARE(accepted.value(QStringLiteral("requestId")).toString(), request_id);
        QCOMPARE(accepted.value(QStringLiteral("status")).toString(), QStringLiteral("accepted"));
        QCOMPARE(accepted.value(QStringLiteral("jobIds")).toArray(),
                 (QJsonArray{QStringLiteral("job_1"), QStringLiteral("Job-2")}));
        const std::pair<DownloadSubmissionError, QString> errors[]{
            {DownloadSubmissionError::InvalidRequest, QStringLiteral("invalid_request")},
            {DownloadSubmissionError::UnsupportedSource, QStringLiteral("unsupported_source")},
            {DownloadSubmissionError::Busy, QStringLiteral("busy")},
            {DownloadSubmissionError::NotAvailable, QStringLiteral("not_available")},
            {DownloadSubmissionError::LimitExceeded, QStringLiteral("limit_exceeded")}};
        for (const auto& [error, code] : errors) {
            const auto rejected =
                QJsonDocument::fromJson(encodeNativeDownloadResponse(request_id.toStdString(), {error, {}})).object();
            QCOMPARE(rejected.size(), qsizetype{5});
            QCOMPARE(rejected.value(QStringLiteral("status")).toString(), QStringLiteral("rejected"));
            QCOMPARE(rejected.value(QStringLiteral("code")).toString(), code);
            QVERIFY(!rejected.contains(QStringLiteral("jobIds")));
        }
    }

    void refusesInvalidSubmissionResults() {
        const std::vector<DownloadSubmissionResult> invalid{{DownloadSubmissionError::None, {}},
                                                            {DownloadSubmissionError::None, {"same", "same"}},
                                                            {DownloadSubmissionError::None, {""}},
                                                            {DownloadSubmissionError::None, {"not allowed"}},
                                                            {DownloadSubmissionError::None, {std::string(81, 'a')}},
                                                            {DownloadSubmissionError::None, {"áudio"}},
                                                            {DownloadSubmissionError::Busy, {"job"}},
                                                            {static_cast<DownloadSubmissionError>(999), {}}};
        for (const auto& result : invalid)
            QVERIFY(encodeNativeDownloadResponse(request_id.toStdString(), result).isEmpty());
        QVERIFY(!encodeNativeDownloadResponse(request_id.toStdString(),
                                              {DownloadSubmissionError::None, {std::string(80, 'a')}})
                     .isEmpty());
    }

    void serializesTypedRequestWithoutChangingStrings() {
        const DownloadRequest original{
            request_id.toUpper().toStdString(),
            {{"https://media.example.test/a%2fb%2FB?sig=A%2fb%2FB&x=1+2&x=3", "Áudio 日本", DownloadMediaKind::Audio},
             {"http://other.example.test/video.mp4?escaped=%26%3D&x=two", "Vídeo 🎵", DownloadMediaKind::Video}}};
        const auto preserved = original;
        const auto payload = encodeNativeDownloadRequest(original);
        QVERIFY(!payload.isEmpty());
        QVERIFY(payload.size() <= 256 * 1024);
        const auto decoded = decodeNativeDownloadRequest(payload);
        QCOMPARE(decoded.error, DownloadSubmissionError::None);
        QVERIFY(decoded.request == original);
        QVERIFY(original == preserved);
    }

    void typedRequestEncoderAcceptsInclusiveBounds() {
        DownloadRequest original{request_id.toStdString(), {}};
        for (int index = 0; index < 20; ++index)
            original.items.push_back({"https://media.example.test/" + std::to_string(index), std::string(200, 'a'),
                                      DownloadMediaKind::Audio});
        const auto payload = encodeNativeDownloadRequest(original);
        QVERIFY(!payload.isEmpty());
        QVERIFY(decodeNativeDownloadRequest(payload).request == original);
        original.items = {
            {"https://media.example.test/" + std::string(8192 - 27, 'a'), "Title", DownloadMediaKind::Video}};
        QCOMPARE(original.items.front().url.size(), std::size_t{8192});
        QVERIFY(!encodeNativeDownloadRequest(original).isEmpty());
        original.items.front().title = (QString(198, QLatin1Char('a')) + QString::fromUcs4(U"🎵")).toStdString();
        QVERIFY(!encodeNativeDownloadRequest(original).isEmpty());
        original.items.front().title.insert(0, "a");
        QVERIFY(encodeNativeDownloadRequest(original).isEmpty());
    }

    void principalEncoderRefusesYoutubeMetadataEvenOnAnOtherwiseDirectRequest() {
        DownloadRequest original{request_id.toStdString(), {{"https://media.example.test/audio.mp3", "Audio"}}};
        QVERIFY(!encodeNativeDownloadRequest(original).isEmpty());
        original.items.front().youtube = YoutubeAudioItem{.video_id = "AbCdEf_12-3",
                                                          .title = "Audio",
                                                          .artist = "Artist",
                                                          .lyrics_page = "https://letras.mus.br/artist/audio/",
                                                          .fetch_lyrics = true};
        const auto preserved = original;
        QVERIFY(encodeNativeDownloadRequest(original).isEmpty());
        QVERIFY(original == preserved);
        original.items.front().youtube = YoutubeAudioItem{};
        QVERIFY(encodeNativeDownloadRequest(original).isEmpty());
        original.items.front().youtube.reset();
        QVERIFY(!encodeNativeDownloadRequest(original).isEmpty());
    }

    void principalDecoderNeverPopulatesOrAcceptsYoutubeMetadata() {
        const auto decoded = decodeNativeDownloadRequest(bytes(request()));
        QCOMPARE(decoded.error, DownloadSubmissionError::None);
        QCOMPARE(decoded.request.items.size(), std::size_t{1});
        QVERIFY(!decoded.request.items.front().youtube);
        auto changed = item();
        changed.insert(QStringLiteral("youtube"),
                       QJsonObject{{QStringLiteral("videoId"), QStringLiteral("AbCdEf_12-3")},
                                   {QStringLiteral("fetchLyrics"), true}});
        const auto injected = decodeNativeDownloadRequest(bytes(request({changed})));
        QCOMPARE(injected.error, DownloadSubmissionError::InvalidRequest);
        QVERIFY(injected.request.items.empty());
        const auto youtube = decodeNativeDownloadRequest(
            bytes(request({item(QStringLiteral("https://www.youtube.com/watch?v=AbCdEf_12-3"))})));
        QCOMPARE(youtube.error, DownloadSubmissionError::UnsupportedSource);
        QVERIFY(youtube.request.items.empty());
    }

    void typedRequestEncoderRejectsInvalidDtos() {
        const DownloadRequest valid{request_id.toStdString(), {{"https://media.example.test/audio.mp3", "Audio"}}};
        std::vector<DownloadRequest> invalid;
        auto changed = valid;
        changed.request_id.clear();
        invalid.push_back(changed);
        changed = valid;
        changed.request_id = std::string(500'000, 'a');
        invalid.push_back(changed);
        changed = valid;
        changed.items.clear();
        invalid.push_back(changed);
        changed = valid;
        changed.items.assign(21, valid.items.front());
        invalid.push_back(changed);
        changed = valid;
        changed.items.push_back(valid.items.front());
        invalid.push_back(changed);
        for (const std::string& url :
             {std::string{"file:///C:/audio.mp3"}, std::string{"https://youtube.com/audio.mp3"},
              std::string{"https://MEDIA.example.test/audio.mp3"}, std::string(8193, 'a'),
              std::string{"https://media.example.test/"} + QByteArray::fromHex("ff").toStdString()}) {
            changed = valid;
            changed.items.front().url = url;
            invalid.push_back(changed);
        }
        for (const std::string& title :
             {std::string{}, std::string{" Audio "}, std::string{"bad\ntext"}, std::string(201, 'a'),
              QByteArray::fromHex("ff").toStdString(), std::string{"A\0B", 3}, std::string(100'000, 'a')}) {
            changed = valid;
            changed.items.front().title = title;
            invalid.push_back(changed);
        }
        changed = valid;
        changed.items.front().kind = static_cast<DownloadMediaKind>(999);
        invalid.push_back(changed);
        for (const auto& candidate : invalid)
            QVERIFY(encodeNativeDownloadRequest(candidate).isEmpty());
    }

    void decodesCorrelatedAcceptedResponse() {
        const auto result = decodeNativeDownloadResponse(bytes(acceptedResponse()), request_id.toStdString(), 2);
        QVERIFY(result);
        QCOMPARE(result->error, DownloadSubmissionError::None);
        QCOMPARE(result->job_ids, (std::vector<std::string>{"job_1", "Job-2"}));
        auto upper = acceptedResponse();
        upper.insert(QStringLiteral("requestId"), request_id.toUpper());
        QVERIFY(decodeNativeDownloadResponse(bytes(upper), request_id.toUpper().toStdString(), 2));
        QVERIFY(!decodeNativeDownloadResponse(bytes(upper), request_id.toStdString(), 2));
    }

    void decodesEveryTypedRejection_data() {
        QTest::addColumn<QString>("code");
        QTest::addColumn<DownloadSubmissionError>("error");
        QTest::newRow("invalid_request") << QStringLiteral("invalid_request")
                                         << DownloadSubmissionError::InvalidRequest;
        QTest::newRow("unsupported_source")
            << QStringLiteral("unsupported_source") << DownloadSubmissionError::UnsupportedSource;
        QTest::newRow("busy") << QStringLiteral("busy") << DownloadSubmissionError::Busy;
        QTest::newRow("not_available") << QStringLiteral("not_available") << DownloadSubmissionError::NotAvailable;
        QTest::newRow("limit_exceeded") << QStringLiteral("limit_exceeded") << DownloadSubmissionError::LimitExceeded;
    }

    void decodesEveryTypedRejection() {
        QFETCH(QString, code);
        QFETCH(DownloadSubmissionError, error);
        const auto result = decodeNativeDownloadResponse(bytes(rejectedResponse(code)), request_id.toStdString(), 2);
        QVERIFY(result);
        QCOMPARE(result->error, error);
        QVERIFY(result->job_ids.empty());
    }

    void rejectsMalformedOrAmbiguousResponses_data() {
        QTest::addColumn<QByteArray>("payload");
        QTest::newRow("empty response") << QByteArray{};
        QTest::newRow("broken response") << QByteArray{"{"};
        QTest::newRow("array response") << QByteArray{"[]"};
        QTest::newRow("trailing JSON") << bytes(acceptedResponse()) + QByteArray{"{}"};
        auto changed = acceptedResponse();
        changed.insert(QStringLiteral("schemaVersion"), true);
        QTest::newRow("boolean version") << bytes(changed);
        changed.insert(QStringLiteral("schemaVersion"), 1.1);
        QTest::newRow("fractional version") << bytes(changed);
        changed.insert(QStringLiteral("schemaVersion"), 2);
        QTest::newRow("future version") << bytes(changed);
        changed = acceptedResponse();
        changed.insert(QStringLiteral("type"), QStringLiteral("download.request"));
        QTest::newRow("request masquerades as reply") << bytes(changed);
        changed = acceptedResponse();
        changed.insert(QStringLiteral("requestId"), QStringLiteral("01234567-89ab-4cde-8fab-0123456789ac"));
        QTest::newRow("uncorrelated request") << bytes(changed);
        changed.insert(QStringLiteral("requestId"), true);
        QTest::newRow("boolean request identity") << bytes(changed);
        changed.insert(QStringLiteral("requestId"), QStringLiteral("01234567-89ab-3cde-8fab-0123456789ab"));
        QTest::newRow("invalid UUID version") << bytes(changed);
        changed = acceptedResponse();
        changed.insert(QStringLiteral("status"), true);
        QTest::newRow("boolean status") << bytes(changed);
        changed.insert(QStringLiteral("status"), QStringLiteral("pending"));
        QTest::newRow("pending forbidden on wire") << bytes(changed);
        changed = acceptedResponse();
        changed.insert(QStringLiteral("code"), QStringLiteral("busy"));
        QTest::newRow("accepted with rejection code") << bytes(changed);
        changed = acceptedResponse();
        changed.insert(QStringLiteral("message"), QStringLiteral("anything"));
        QTest::newRow("free text extra field") << bytes(changed);
        changed = acceptedResponse();
        changed.remove(QStringLiteral("jobIds"));
        QTest::newRow("accepted missing jobs") << bytes(changed);
        changed = acceptedResponse();
        changed.insert(QStringLiteral("jobIds"), QStringLiteral("job_1"));
        QTest::newRow("nonarray jobs") << bytes(changed);
        QTest::newRow("empty jobs") << bytes(acceptedResponse({}));
        QTest::newRow("duplicate jobs") << bytes(acceptedResponse({QStringLiteral("same"), QStringLiteral("same")}));
        QTest::newRow("wrong jobs count") << bytes(acceptedResponse({QStringLiteral("job_1")}));
        QTest::newRow("nonstring job") << bytes(acceptedResponse({QStringLiteral("job_1"), 7}));
        QTest::newRow("object job") << bytes(acceptedResponse({QStringLiteral("job_1"), QJsonObject{}}));
        QTest::newRow("empty job") << bytes(acceptedResponse({QStringLiteral("job_1"), QString{}}));
        QTest::newRow("long job") << bytes(acceptedResponse({QStringLiteral("job_1"), QString(81, QLatin1Char('a'))}));
        QTest::newRow("Unicode job") << bytes(acceptedResponse({QStringLiteral("job_1"), QStringLiteral("áudio")}));
        QTest::newRow("control job") << bytes(acceptedResponse({QStringLiteral("job_1"), QStringLiteral("bad\njob")}));
        QTest::newRow("job path") << bytes(acceptedResponse({QStringLiteral("job_1"), QStringLiteral("C:/audio.mp3")}));
        changed = rejectedResponse();
        changed.insert(QStringLiteral("jobIds"), QJsonArray{});
        QTest::newRow("rejected with jobs") << bytes(changed);
        changed = rejectedResponse();
        changed.remove(QStringLiteral("code"));
        QTest::newRow("missing rejection code") << bytes(changed);
        changed = rejectedResponse();
        changed.insert(QStringLiteral("code"), 7);
        QTest::newRow("numeric rejection code") << bytes(changed);
        QTest::newRow("unknown rejection code") << bytes(rejectedResponse(QStringLiteral("all_ok")));
        QTest::newRow("uppercase rejection code") << bytes(rejectedResponse(QStringLiteral("BUSY")));
        auto payload = bytes(acceptedResponse());
        payload.insert(1, "\"status\":\"accepted\",");
        QTest::newRow("duplicate member") << payload;
        payload = bytes(acceptedResponse());
        payload.insert(1, "\"sta\\u0074us\":\"accepted\",");
        QTest::newRow("escaped duplicate alias") << payload;
        payload = bytes(acceptedResponse());
        payload.replace("Job-2", "bad\\q");
        QTest::newRow("unknown JSON escape") << payload;
        payload = bytes(acceptedResponse());
        payload.replace("Job-2", "\\ud800");
        QTest::newRow("lone surrogate") << payload;
        payload = bytes(acceptedResponse());
        payload.replace("Job-2", QByteArray::fromHex("c328"));
        QTest::newRow("invalid UTF8") << payload;
        const auto bom = QByteArray::fromHex("efbbbf");
        QTest::newRow("payload BOM") << bom + bytes(acceptedResponse());
        payload = bytes(acceptedResponse());
        payload.replace("\"requestId\":", '"' + bom + "requestId\":");
        QTest::newRow("BOM key alias") << payload;
        QTest::newRow("deep JSON") << QByteArray(5000, '[') + QByteArray(5000, ']');
        payload = bytes(acceptedResponse());
        payload.replace("\"schemaVersion\":1", "\"schemaVersion\":1.");
        QTest::newRow("nonJSON numeric spelling") << payload;
    }

    void rejectsMalformedOrAmbiguousResponses() {
        QFETCH(QByteArray, payload);
        QVERIFY(!decodeNativeDownloadResponse(payload, request_id.toStdString(), 2));
    }

    void responseDecoderEnforcesExpectedContextAndInclusiveBounds() {
        QJsonArray ids;
        for (int index = 0; index < 20; ++index)
            ids.push_back(QString::number(index) + QString(78, QLatin1Char('a')));
        ids[0] = QString(80, QLatin1Char('a'));
        QVERIFY(decodeNativeDownloadResponse(bytes(acceptedResponse(ids)), request_id.toStdString(), 20));
        ids.push_back(QStringLiteral("extra"));
        QVERIFY(!decodeNativeDownloadResponse(bytes(acceptedResponse(ids)), request_id.toStdString(), 21));
        QVERIFY(!decodeNativeDownloadResponse(bytes(acceptedResponse()), std::string_view{}, 2));
        QVERIFY(!decodeNativeDownloadResponse(bytes(acceptedResponse()), "not-a-UUID", 2));
        QVERIFY(!decodeNativeDownloadResponse(bytes(acceptedResponse()), request_id.toStdString(), 0));
        QVERIFY(!decodeNativeDownloadResponse(bytes(rejectedResponse()), request_id.toStdString(), 0));
        QVERIFY(!decodeNativeDownloadResponse(bytes(rejectedResponse()), request_id.toStdString(), 21));
        auto payload = bytes(acceptedResponse());
        payload.append(QByteArray(256 * 1024 - payload.size(), ' '));
        QVERIFY(decodeNativeDownloadResponse(payload, request_id.toStdString(), 2));
        payload.append(' ');
        QVERIFY(!decodeNativeDownloadResponse(payload, request_id.toStdString(), 2));
        payload = bytes(acceptedResponse());
        payload.replace("\"schemaVersion\":1", "\"schemaVersion\":1.0");
        QVERIFY(decodeNativeDownloadResponse(payload, request_id.toStdString(), 2));
    }
};

QTEST_GUILESS_MAIN(NativeDownloadCodecTests)

#include "NativeDownloadCodecTests.moc"
