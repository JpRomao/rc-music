#include "adapters/NativeDownloadCodec.hpp"
#include "adapters/YoutubeAudioCodec.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace music_player::m0;

namespace {
const QString request_id = QStringLiteral("01234567-89ab-4cde-8fab-0123456789ab");
QJsonObject item() {
    return {{"videoId", "aB0-_Cd1EF2"},
            {"title", QStringLiteral("歌の名前 / Canção")},
            {"artist", QStringLiteral("作者 日本語")},
            {"lyricsPage", "https://www.letras.mus.br/naruto/silhouette/"},
            {"fetchLyrics", true}};
}
QJsonObject request(const QJsonArray& items = {item()}) {
    return {{"schemaVersion", 1}, {"type", "youtube.audio.request"}, {"requestId", request_id}, {"items", items}};
}
QByteArray bytes(const QJsonObject& object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); }
QJsonObject accepted(const QJsonArray& jobs = {"job_1"}) {
    return {{"schemaVersion", 1},
            {"type", "youtube.audio.response"},
            {"requestId", request_id},
            {"status", "accepted"},
            {"jobIds", jobs}};
}
QJsonObject rejected(const QString& code) {
    auto value = accepted();
    value.remove("jobIds");
    value.insert("status", "rejected");
    value.insert("code", code);
    return value;
}
} // namespace

class YoutubeAudioCodecTests final : public QObject {
    Q_OBJECT
  private slots:
    void agreesWithTheSharedExtensionProtocolCorpus() {
        QFile file(QString::fromUtf8(RC_MUSIC_YOUTUBE_PROTOCOL_VECTORS_PATH));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto corpus = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(corpus.value("schemaVersion").toInt(), 1);
        const auto requests = corpus.value("requests").toArray();
        const auto responses = corpus.value("responses").toArray();
        QVERIFY(!requests.isEmpty() && !responses.isEmpty());
        for (const auto& entry : requests) {
            const auto row = entry.toObject();
            QVERIFY(row.value("valid").isBool() && row.value("payload").isObject());
            const auto decoded = decodeYoutubeAudioRequest(bytes(row.value("payload").toObject()));
            QVERIFY2((decoded.error == DownloadSubmissionError::None) == row.value("valid").toBool(),
                     qPrintable(row.value("name").toString()));
            if (decoded.error == DownloadSubmissionError::None)
                QCOMPARE(decodeYoutubeAudioRequest(encodeYoutubeAudioRequest(decoded.request)).request,
                         decoded.request);
            else
                QVERIFY(decoded.request.items.empty());
        }
        for (const auto& entry : responses) {
            const auto row = entry.toObject();
            QVERIFY(row.value("valid").isBool() && row.value("payload").isObject());
            const auto count = row.value("itemCount").toInt();
            QVERIFY(count > 0);
            const auto decoded = decodeYoutubeAudioResponse(bytes(row.value("payload").toObject()),
                                                            request_id.toStdString(), static_cast<std::size_t>(count));
            QVERIFY2(decoded.has_value() == row.value("valid").toBool(), qPrintable(row.value("name").toString()));
        }
    }
    void separateRequestRoundTripsUnicodeWithoutGrantingAnyMainCapability() {
        const auto payload = bytes(request());
        const auto decoded = decodeYoutubeAudioRequest(payload);
        QCOMPARE(decoded.error, DownloadSubmissionError::None);
        QCOMPARE(decoded.request.request_id, request_id.toStdString());
        QCOMPARE(decoded.request.items.size(), std::size_t{1});
        const auto& value = decoded.request.items.front();
        QCOMPARE(value.video_id, std::string{"aB0-_Cd1EF2"});
        QCOMPARE(value.title, QStringLiteral("歌の名前 / Canção").toStdString());
        QCOMPARE(value.artist, QStringLiteral("作者 日本語").toStdString());
        QCOMPARE(value.lyrics_page, std::string{"https://www.letras.mus.br/naruto/silhouette/"});
        QVERIFY(value.fetch_lyrics);
        QCOMPARE(decodeYoutubeAudioRequest(encodeYoutubeAudioRequest(decoded.request)).request, decoded.request);
        QCOMPARE(decodeNativeDownloadRequest(payload).error, DownloadSubmissionError::InvalidRequest);
    }
    void artistAndLyricsPageAreOptionalValuesButNeverMissingKeys() {
        auto value = item();
        value.insert("artist", "");
        value.insert("lyricsPage", "");
        value.insert("fetchLyrics", false);
        const auto decoded = decodeYoutubeAudioRequest(bytes(request({value})));
        QCOMPARE(decoded.error, DownloadSubmissionError::None);
        QVERIFY(decoded.request.items.front().artist.empty());
        QVERIFY(decoded.request.items.front().lyrics_page.empty());
        QVERIFY(!decoded.request.items.front().fetch_lyrics);
        value.remove("artist");
        QCOMPARE(decodeYoutubeAudioRequest(bytes(request({value}))).error, DownloadSubmissionError::InvalidRequest);
    }
    void rejectsInvalidOpaqueVideoIds_data() {
        QTest::addColumn<QString>("id");
        int index{};
        for (const auto& id : {QString{}, QString{"short"}, QString{"123456789012"}, QString{"1234567890/"},
                               QString{"1234567890%"}, QString{"1234567890?"}, QStringLiteral("日本語abcdefgh"),
                               QString{"--exec=evil"}, QString{"aB0-_Cd1EF2\n"}})
            QTest::newRow(qPrintable(QString("invalid-%1").arg(index++))) << id;
    }
    void rejectsInvalidOpaqueVideoIds() {
        QFETCH(QString, id);
        auto value = item();
        value.insert("videoId", id);
        const auto decoded = decodeYoutubeAudioRequest(bytes(request({value})));
        QCOMPARE(decoded.error, DownloadSubmissionError::InvalidRequest);
        QVERIFY(decoded.request.items.empty());
        QVERIFY(canonicalYoutubeWatchUrl(id.toStdString()).isEmpty());
    }
    void canonicalWatchUrlsOnlyUseTheFixedYoutubeEndpoint() {
        QCOMPARE(canonicalYoutubeWatchUrl("aB0-_Cd1EF2"),
                 QStringLiteral("https://www.youtube.com/watch?v=aB0-_Cd1EF2"));
        QCOMPARE(canonicalYoutubeWatchUrl("___________"),
                 QStringLiteral("https://www.youtube.com/watch?v=___________"));
    }
    void rejectsDuplicateIdsInsteadOfAdmittingAPartialBatch() {
        auto duplicate = item();
        duplicate.insert("title", "Another title");
        const auto decoded = decodeYoutubeAudioRequest(bytes(request({item(), duplicate})));
        QCOMPARE(decoded.error, DownloadSubmissionError::InvalidRequest);
        QVERIFY(decoded.request.items.empty());
    }
    void acceptsTwentyItemsButRejectsTwentyOneAndTheEmptyBatch() {
        QJsonArray values;
        for (int index = 0; index < 20; ++index) {
            auto value = item();
            value.insert("videoId", QString("000000000%1").arg(index, 2, 10, QLatin1Char('0')));
            values.push_back(value);
        }
        const auto maximum = decodeYoutubeAudioRequest(bytes(request(values)));
        QCOMPARE(maximum.error, DownloadSubmissionError::None);
        QCOMPARE(maximum.request.items.size(), std::size_t{20});
        values.push_back(item());
        QCOMPARE(decodeYoutubeAudioRequest(bytes(request(values))).error, DownloadSubmissionError::LimitExceeded);
        QCOMPARE(decodeYoutubeAudioRequest(bytes(request({}))).error, DownloadSubmissionError::InvalidRequest);
    }
    void metadataLimitsCountUtf8BytesWithoutTruncation() {
        auto value = item();
        value.insert("title", QString::fromUtf8(QByteArray(512, 't')));
        value.insert("artist", QString::fromUtf8(QByteArray(512, 'a')));
        QCOMPARE(decodeYoutubeAudioRequest(bytes(request({value}))).error, DownloadSubmissionError::None);
        value.insert("title", QString::fromUtf8(QByteArray(513, 't')));
        QCOMPARE(decodeYoutubeAudioRequest(bytes(request({value}))).error, DownloadSubmissionError::LimitExceeded);
        value.insert("title", QString(171, QChar(0x65e5))); // 513 UTF-8 bytes.
        QCOMPARE(decodeYoutubeAudioRequest(bytes(request({value}))).error, DownloadSubmissionError::LimitExceeded);
        value.insert("title", "Title");
        value.insert("artist", QString(171, QChar(0x65e5)));
        QCOMPARE(decodeYoutubeAudioRequest(bytes(request({value}))).error, DownloadSubmissionError::LimitExceeded);
    }
    void malformedMetadataIsRefusedWithoutSanitizing_data() {
        QTest::addColumn<QString>("title");
        int index{};
        for (const auto& title : {QString{}, QString{" Leading"}, QString{"Trailing "}, QString{"Two\nLines"},
                                  QString{"Two\rLines"}, QString{"A\tB"}, QString(QChar{0}),
                                  QString("A") + QChar{0x2028} + "B", QString("A") + QChar{0x202e} + "B"})
            QTest::newRow(qPrintable(QString("malformed-%1").arg(index++))) << title;
    }
    void malformedMetadataIsRefusedWithoutSanitizing() {
        QFETCH(QString, title);
        auto value = item();
        value.insert("title", title);
        QCOMPARE(decodeYoutubeAudioRequest(bytes(request({value}))).error, DownloadSubmissionError::InvalidRequest);
    }
    void invalidLyricsSourcesNeverBecomeArbitraryFetchRequests_data() {
        QTest::addColumn<QString>("url");
        for (const auto& url :
             {"http://www.letras.mus.br/a/b/", "https://example.test/a/b/", "https://letras.mus.br.evil.test/a/b/",
              "https://user@letras.mus.br/a/b/", "https://letras.mus.br:444/a/b/", "https://letras.mus.br/a/b/?x=1",
              "https://letras.mus.br/a/b/#text", "https://letras.mus.br/a/../b/", "https://letras.mus.br/a/%2e%2e/b/",
              "https://letras.mus.br/a%2Fb/", "https://letras.mus.br/a%5Cb/", "https://letras.mus.br/a//b/",
              "https://letras.mus.br/", "https://letras.mus.br/a/%00/b/", "https://letras.mus.br/a\\b/",
              "https://letras.mus.br/a b/"})
            QTest::newRow(url) << QString::fromLatin1(url);
    }
    void invalidLyricsSourcesNeverBecomeArbitraryFetchRequests() {
        QFETCH(QString, url);
        auto value = item();
        value.insert("lyricsPage", url);
        const auto decoded = decodeYoutubeAudioRequest(bytes(request({value})));
        QCOMPARE(decoded.error, DownloadSubmissionError::UnsupportedSource);
        QVERIFY(decoded.request.items.empty());
    }
    void acceptsTheTwoExactLetrasHostsAndDefaultHttpsPort() {
        for (const auto& url : {"https://letras.mus.br/a/b/", "https://WWW.LETRAS.MUS.BR:443/a/b/original.html"}) {
            auto value = item();
            value.insert("lyricsPage", url);
            QCOMPARE(decodeYoutubeAudioRequest(bytes(request({value}))).error, DownloadSubmissionError::None);
        }
    }
    void strictJsonAndExactSchemasRejectCoercions_data() {
        QTest::addColumn<QByteArray>("payload");
        auto value = request();
        value.insert("extra", true);
        QTest::newRow("extra root field") << bytes(value);
        value = request();
        value.insert("type", "download.request");
        QTest::newRow("main type") << bytes(value);
        value.insert("type", "youtube.audio.request");
        value.insert("schemaVersion", 2);
        QTest::newRow("future schema") << bytes(value);
        auto entry = item();
        entry.insert("fetchLyrics", 1);
        QTest::newRow("numeric boolean") << bytes(request({entry}));
        entry = item();
        entry.insert("url", "https://arbitrary.test/");
        QTest::newRow("arbitrary URL field") << bytes(request({entry}));
        auto duplicate = bytes(request());
        duplicate.replace("\"schemaVersion\":1", "\"schemaVersion\":1,\"schemaVersion\":1");
        QTest::newRow("duplicate key") << duplicate;
        auto invalid_utf8 = bytes(request());
        invalid_utf8.replace("aB0-_Cd1EF2", QByteArray("\xc0\xaf", 2));
        QTest::newRow("invalid UTF8") << invalid_utf8;
        for (const auto& id : {"01234567-89ab-3cde-8fab-0123456789ab", "01234567-89ab-4cde-7fab-0123456789ab",
                               "not-a-uuid", "01234567-89ab-4cde-8fab-0123456789ab\n"}) {
            value = request();
            value.insert("requestId", id);
            QTest::newRow(id) << bytes(value);
        }
    }
    void strictJsonAndExactSchemasRejectCoercions() {
        QFETCH(QByteArray, payload);
        const auto decoded = decodeYoutubeAudioRequest(payload);
        QCOMPARE(decoded.error, DownloadSubmissionError::InvalidRequest);
        QVERIFY(decoded.request.items.empty());
    }
    void oversizedDocumentsAndFieldsHaveTheirOwnLimitResult() {
        auto maximum = bytes(request());
        maximum.append(QByteArray(64 * 1024 - maximum.size(), ' '));
        QCOMPARE(decodeYoutubeAudioRequest(maximum).error, DownloadSubmissionError::None);
        QCOMPARE(decodeYoutubeAudioRequest(QByteArray(64 * 1024 + 1, ' ')).error,
                 DownloadSubmissionError::LimitExceeded);
        auto value = item();
        value.insert("lyricsPage", "https://letras.mus.br/" + QString(2048, QLatin1Char('a')));
        QCOMPARE(decodeYoutubeAudioRequest(bytes(request({value}))).error, DownloadSubmissionError::LimitExceeded);
    }
    void exactUnicodeAndUppercaseUuidBoundariesArePreserved() {
        auto value = item();
        value.insert("title", QString(256, QChar{0x03b1}));
        value.insert("artist", QString(256, QChar{0x03b1}));
        auto payload = request({value});
        payload.insert("requestId", request_id.toUpper());
        const auto decoded = decodeYoutubeAudioRequest(bytes(payload));
        QCOMPARE(decoded.error, DownloadSubmissionError::None);
        QCOMPARE(decoded.request.request_id, request_id.toUpper().toStdString());
        QCOMPARE(decoded.request.items.front().title.size(), std::size_t{512});
        QCOMPARE(decodeYoutubeAudioRequest(encodeYoutubeAudioRequest(decoded.request)).request, decoded.request);
    }
    void encodingRefusesMalformedDtoInsteadOfRepairingIt() {
        YoutubeAudioRequest value{request_id.toStdString(), {{"aB0-_Cd1EF2", "Title", "", "", false}}};
        QVERIFY(!encodeYoutubeAudioRequest(value).isEmpty());
        value.items.front().title = std::string("\xc0\xaf", 2);
        QVERIFY(encodeYoutubeAudioRequest(value).isEmpty());
        value.items.front().title = "Title";
        value.items.push_back(value.items.front());
        QVERIFY(encodeYoutubeAudioRequest(value).isEmpty());
    }
    void acceptedResponsesRoundTripTwentyCorrelatedOpaqueJobIds() {
        DownloadSubmissionResult result{.error = DownloadSubmissionError::None};
        for (int index = 0; index < 20; ++index)
            result.job_ids.push_back("job_" + std::to_string(index));
        const auto payload = encodeYoutubeAudioResponse(request_id.toStdString(), result);
        QVERIFY(!payload.isEmpty());
        QCOMPARE(QJsonDocument::fromJson(payload).object().value("type").toString(), "youtube.audio.response");
        const auto restored = decodeYoutubeAudioResponse(payload, request_id.toStdString(), 20);
        QVERIFY(restored);
        QCOMPARE(restored->error, DownloadSubmissionError::None);
        QCOMPARE(restored->job_ids, result.job_ids);
        QVERIFY(!decodeYoutubeAudioResponse(payload, request_id.toStdString(), 19));
        QVERIFY(!decodeNativeDownloadResponse(payload, request_id.toStdString(), 20));
    }
    void everyTypedRejectionRoundTripsWithoutJobIds() {
        for (const auto error : {DownloadSubmissionError::InvalidRequest, DownloadSubmissionError::UnsupportedSource,
                                 DownloadSubmissionError::Busy, DownloadSubmissionError::NotAvailable,
                                 DownloadSubmissionError::LimitExceeded}) {
            const auto payload = encodeYoutubeAudioResponse(request_id.toStdString(), {.error = error});
            QVERIFY(!payload.isEmpty());
            const auto decoded = decodeYoutubeAudioResponse(payload, request_id.toStdString(), 1);
            QVERIFY(decoded);
            QCOMPARE(decoded->error, error);
            QVERIFY(decoded->job_ids.empty());
        }
    }
    void malformedUncorrelatedOrMainResponsesAreUncertainty() {
        auto value = accepted();
        value.insert("requestId", "11234567-89ab-4cde-8fab-0123456789ab");
        QVERIFY(!decodeYoutubeAudioResponse(bytes(value), request_id.toStdString(), 1));
        value = accepted();
        value.insert("type", "download.response");
        QVERIFY(!decodeYoutubeAudioResponse(bytes(value), request_id.toStdString(), 1));
        value = accepted({"same", "same"});
        QVERIFY(!decodeYoutubeAudioResponse(bytes(value), request_id.toStdString(), 2));
        value = accepted({"bad/job"});
        QVERIFY(!decodeYoutubeAudioResponse(bytes(value), request_id.toStdString(), 1));
        value = rejected("busy");
        value.insert("jobIds", QJsonArray{"job_1"});
        QVERIFY(!decodeYoutubeAudioResponse(bytes(value), request_id.toStdString(), 1));
        QVERIFY(!decodeYoutubeAudioResponse(bytes(rejected("unknown")), request_id.toStdString(), 1));
        QVERIFY(!decodeYoutubeAudioResponse(bytes(accepted()), request_id.toStdString(), 0));
    }
    void invalidResponseDtosNeverProduceAcknowledgements() {
        QVERIFY(encodeYoutubeAudioResponse("not-a-uuid", {.error = DownloadSubmissionError::Busy}).isEmpty());
        QVERIFY(
            encodeYoutubeAudioResponse(request_id.toStdString(), {.error = DownloadSubmissionError::None}).isEmpty());
        QVERIFY(encodeYoutubeAudioResponse(request_id.toStdString(),
                                           {.error = DownloadSubmissionError::Busy, .job_ids = {"x"}})
                    .isEmpty());
        QVERIFY(encodeYoutubeAudioResponse(request_id.toStdString(),
                                           {.error = DownloadSubmissionError::None, .job_ids = {"x", "x"}})
                    .isEmpty());
    }
    void theMainCodecStillRefusesYoutubeAndItsCdnIndirectly_data() {
        QTest::addColumn<QString>("url");
        for (const auto& url :
             {"https://www.youtube.com/watch?v=aB0-_Cd1EF2", "https://youtu.be/aB0-_Cd1EF2",
              "https://www.youtube-nocookie.com/embed/aB0-_Cd1EF2", "https://r1.googlevideo.com/audio.mp4"})
            QTest::newRow(url) << QString::fromLatin1(url);
    }
    void theMainCodecStillRefusesYoutubeAndItsCdnIndirectly() {
        QFETCH(QString, url);
        const QJsonObject entry{{"url", url}, {"title", "Title"}, {"kind", "audio"}};
        auto value = request({entry});
        value.insert("type", "download.request");
        const auto decoded = decodeNativeDownloadRequest(bytes(value));
        QCOMPARE(decoded.error, DownloadSubmissionError::UnsupportedSource);
        QVERIFY(decoded.request.items.empty());
    }
};
QTEST_GUILESS_MAIN(YoutubeAudioCodecTests)
#include "YoutubeAudioCodecTests.moc"
