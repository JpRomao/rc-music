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
};

QTEST_GUILESS_MAIN(NativeDownloadCodecTests)

#include "NativeDownloadCodecTests.moc"
