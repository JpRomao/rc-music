#include "adapters/YoutubeAudioResolution.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <limits>
#include <type_traits>

using namespace music_player::m0;
namespace {
const QString video_id = QStringLiteral("aB_0123-xYz");
const QString stream =
    QStringLiteral("https://r1.googlevideo.com/videoplayback?expire=2000000000&sig=A%2fb%2F&x=1+2&x=3");
QJsonObject fixture() {
    return {{"_type", "video"},
            {"id", video_id},
            {"extractor_key", "Youtube"},
            {"ext", "m4a"},
            {"acodec", "mp4a.40.2"},
            {"vcodec", "none"},
            {"protocol", "https"},
            {"duration", 123.456},
            {"filesize", 1024},
            {"availability", "public"},
            {"is_live", false},
            {"live_status", "not_live"},
            {"has_drm", false},
            {"url", stream},
            {"title", QStringLiteral("歌のテスト / Canção")},
            {"artist", QStringLiteral("作者")},
            {"http_headers", QJsonObject{}}};
}
QByteArray bytes(const QJsonObject& object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); }
} // namespace

static_assert(!std::is_default_constructible_v<ResolvedYoutubeAudioCapability>);
static_assert(!std::is_constructible_v<ResolvedYoutubeAudioCapability, std::string, YoutubeResolvedAudioData>);
static_assert(!std::is_copy_assignable_v<ResolvedYoutubeAudioCapability>);
static_assert(!std::is_move_assignable_v<ResolvedYoutubeAudioCapability>);
static_assert(std::is_same_v<decltype(std::declval<const ResolvedYoutubeAudioCapability&>().data()),
                             const YoutubeResolvedAudioData&>);

class YoutubeAudioResolutionTests final : public QObject {
    Q_OBJECT
  private slots:
    void selectsOneAacStreamAndPreservesSignedBytesAndUnicodeWithoutMintingAuthority() {
        const auto payload = bytes(fixture());
        const auto original = payload;
        const auto result = decodeYoutubeAudioResolution(payload, video_id);
        QCOMPARE(result.error, DownloadFailure::None);
        QVERIFY(result.audio);
        QCOMPARE(result.audio->video_id, video_id);
        QCOMPARE(result.audio->stream_url, stream);
        QCOMPARE(result.audio->duration_ms, quint64{123456});
        QCOMPARE(result.audio->expected_bytes, std::optional<quint64>{1024});
        QCOMPARE(result.audio->expires_at_seconds, qint64{2'000'000'000});
        QCOMPARE(result.audio->title, QStringLiteral("歌のテスト / Canção"));
        QCOMPARE(result.audio->artist, QStringLiteral("作者"));
        QCOMPARE(payload, original);
    }
    void acceptsOptionalMetadataAndApproximateSizeWithoutTurningTheEstimateIntoAnExactLength() {
        auto object = fixture();
        for (const auto* field : {"_type", "title", "artist", "filesize", "http_headers", "availability", "is_live",
                                  "live_status", "has_drm"})
            object.remove(QLatin1String(field));
        object.insert("filesize_approx", 2048);
        object.insert("acodec", "aac");
        object.insert("extra_metadata", QJsonObject{{"nested", QJsonArray{"ignored", 7}}});
        const auto result = decodeYoutubeAudioResolution(bytes(object), video_id);
        QCOMPARE(result.error, DownloadFailure::None);
        QVERIFY(result.audio);
        QVERIFY(!result.audio->expected_bytes);
        QVERIFY(result.audio->title.isEmpty());
        QVERIFY(result.audio->artist.isEmpty());
    }
    void capturesOnlyValidatedPublicUserAgentHint() {
        auto object = fixture();
        object.insert("http_headers", QJsonObject{{"uSeR-aGeNt", "RCMusic Synthetic Agent/1"},
                                                  {"Accept", "*/*"},
                                                  {"Accept-Language", "en"},
                                                  {"Sec-Fetch-Mode", "navigate"}});
        const auto result = decodeYoutubeAudioResolution(bytes(object), video_id);
        QCOMPARE(result.error, DownloadFailure::None);
        QVERIFY(result.audio);
        QCOMPARE(result.audio->user_agent_hint, QByteArray("RCMusic Synthetic Agent/1"));
        QCOMPARE(result.audio->stream_url, stream);
        object.remove("http_headers");
        const auto absent = decodeYoutubeAudioResolution(bytes(object), video_id);
        QVERIFY(absent.audio);
        QVERIFY(absent.audio->user_agent_hint.isEmpty());
        object.insert("http_headers", QJsonObject{{"Accept", "*/*"}});
        const auto other_hint = decodeYoutubeAudioResolution(bytes(object), video_id);
        QVERIFY(other_hint.audio);
        QVERIFY(other_hint.audio->user_agent_hint.isEmpty());
    }
    void approvedDefaultHeaderMetadataDoesNotBecomeArbitraryRequestHeaders() {
        auto object = fixture();
        object.insert("http_headers", QJsonObject{{"User-Agent", "Mozilla/5.0 Synthetic Test"},
                                                  {"Accept", "text/html,application/xhtml+xml,*/*;q=0.8"},
                                                  {"Accept-Language", "en-us,en;q=0.5"},
                                                  {"Sec-Fetch-Mode", "navigate"}});
        const auto result = decodeYoutubeAudioResolution(bytes(object), video_id);
        QCOMPARE(result.error, DownloadFailure::None);
        QVERIFY(result.audio);
        QCOMPARE(result.audio->stream_url, stream);
    }
    void inputLimitsAndIdentityCannotBeRepairedOrPartiallyAccepted_data() {
        QTest::addColumn<QByteArray>("payload");
        QTest::addColumn<QString>("expected_id");
        QTest::addColumn<DownloadFailure>("failure");
        const auto good = bytes(fixture());
        QTest::newRow("bad JSON") << QByteArray("not JSON") << video_id << DownloadFailure::InvalidResult;
        QTest::newRow("oversized") << QByteArray(1024 * 1024 + 1, ' ') << video_id << DownloadFailure::LimitExceeded;
        QTest::newRow("unexpected video") << good << QStringLiteral("zzzzzzzzzzz") << DownloadFailure::InvalidResult;
        QTest::newRow("invalid expected opaque ID")
            << good << QStringLiteral("https://evil") << DownloadFailure::InvalidResult;
        auto duplicate = good;
        duplicate.insert(1, "\"id\":\"different00\",");
        QTest::newRow("duplicate key") << duplicate << video_id << DownloadFailure::InvalidResult;
        auto malformed = good;
        malformed.replace("Youtube", QByteArray("Y\xc0\xaf", 3));
        QTest::newRow("invalid raw UTF8") << malformed << video_id << DownloadFailure::InvalidResult;
        auto surrogate = good;
        surrogate.replace("Youtube", "Y\\ud800");
        QTest::newRow("unpaired surrogate") << surrogate << video_id << DownloadFailure::InvalidResult;
        QTest::newRow("two JSON documents") << good + good << video_id << DownloadFailure::InvalidResult;
        auto nested = fixture();
        QJsonValue value(1);
        for (int depth = 0; depth < 16; ++depth)
            value = QJsonObject{{"level", value}};
        nested.insert("unknown", value);
        QTest::newRow("nesting over sixteen") << bytes(nested) << video_id << DownloadFailure::InvalidResult;
    }
    void inputLimitsAndIdentityCannotBeRepairedOrPartiallyAccepted() {
        QFETCH(QByteArray, payload);
        QFETCH(QString, expected_id);
        QFETCH(DownloadFailure, failure);
        const auto result = decodeYoutubeAudioResolution(payload, expected_id);
        QCOMPARE(result.error, failure);
        QVERIFY(!result.audio);
    }
    void rejectsUnsupportedAndRestrictedResolutionShapes_data() {
        QTest::addColumn<QString>("field");
        QTest::addColumn<QJsonValue>("value");
        QTest::addColumn<DownloadFailure>("failure");
        QTest::newRow("transparent redirect")
            << QString("_type") << QJsonValue("url_transparent") << DownloadFailure::UnsupportedMedia;
        QTest::newRow("playlist") << QString("_type") << QJsonValue("playlist") << DownloadFailure::UnsupportedMedia;
        QTest::newRow("playlist entries")
            << QString("entries") << QJsonValue(QJsonArray{}) << DownloadFailure::UnsupportedMedia;
        QTest::newRow("merged streams") << QString("requested_formats") << QJsonValue(QJsonArray{})
                                        << DownloadFailure::UnsupportedMedia;
        QTest::newRow("fragment stream") << QString("fragments") << QJsonValue(QJsonArray{})
                                         << DownloadFailure::UnsupportedMedia;
        QTest::newRow("wrong extractor") << QString("extractor_key") << QJsonValue("Generic")
                                         << DownloadFailure::InvalidResult;
        QTest::newRow("wrong id") << QString("id") << QJsonValue("zzzzzzzzzzz") << DownloadFailure::InvalidResult;
        QTest::newRow("webm") << QString("ext") << QJsonValue("webm") << DownloadFailure::UnsupportedMedia;
        QTest::newRow("opus") << QString("acodec") << QJsonValue("opus") << DownloadFailure::UnsupportedMedia;
        QTest::newRow("unqualified AAC variant")
            << QString("acodec") << QJsonValue("mp4a.40.5") << DownloadFailure::UnsupportedMedia;
        QTest::newRow("video track") << QString("vcodec") << QJsonValue("avc1.4d401e")
                                     << DownloadFailure::UnsupportedMedia;
        QTest::newRow("DASH fragments") << QString("protocol") << QJsonValue("http_dash_segments")
                                        << DownloadFailure::UnsupportedMedia;
        QTest::newRow("live") << QString("is_live") << QJsonValue(true) << DownloadFailure::UnsupportedMedia;
        QTest::newRow("upcoming") << QString("live_status") << QJsonValue("is_upcoming")
                                  << DownloadFailure::UnsupportedMedia;
        QTest::newRow("postlive") << QString("live_status") << QJsonValue("post_live")
                                  << DownloadFailure::UnsupportedMedia;
        QTest::newRow("string false") << QString("is_live") << QJsonValue("false") << DownloadFailure::InvalidResult;
        QTest::newRow("DRM") << QString("has_drm") << QJsonValue(true) << DownloadFailure::Restricted;
        for (const auto* restricted : {"private", "premium_only", "subscriber_only", "needs_auth", "unknown"})
            QTest::newRow(restricted) << QString("availability") << QJsonValue(QLatin1String(restricted))
                                      << DownloadFailure::Restricted;
        QTest::newRow("negative duration") << QString("duration") << QJsonValue(-1) << DownloadFailure::InvalidResult;
        QTest::newRow("zero duration") << QString("duration") << QJsonValue(0) << DownloadFailure::InvalidResult;
        QTest::newRow("duration missing")
            << QString("duration") << QJsonValue(QJsonValue::Undefined) << DownloadFailure::InvalidResult;
        QTest::newRow("duration string") << QString("duration") << QJsonValue("10") << DownloadFailure::InvalidResult;
        QTest::newRow("over 24 hours") << QString("duration") << QJsonValue(86400.001)
                                       << DownloadFailure::LimitExceeded;
        QTest::newRow("zero bytes") << QString("filesize") << QJsonValue(0) << DownloadFailure::InvalidResult;
        QTest::newRow("fractional bytes")
            << QString("filesize") << QJsonValue(1024.5) << DownloadFailure::InvalidResult;
        QTest::newRow("over 512 MiB") << QString("filesize") << QJsonValue(512.0 * 1024 * 1024 + 1)
                                      << DownloadFailure::LimitExceeded;
        QTest::newRow("oversized approximation")
            << QString("filesize_approx") << QJsonValue(512.0 * 1024 * 1024 + 1) << DownloadFailure::LimitExceeded;
        QTest::newRow("metadata object") << QString("artist") << QJsonValue(QJsonObject{})
                                         << DownloadFailure::InvalidResult;
        QTest::newRow("metadata control") << QString("title") << QJsonValue("A\nB") << DownloadFailure::InvalidResult;
        QTest::newRow("metadata byte limit")
            << QString("artist") << QJsonValue(QString(513, 'a')) << DownloadFailure::LimitExceeded;
    }
    void rejectsUnsupportedAndRestrictedResolutionShapes() {
        QFETCH(QString, field);
        QFETCH(QJsonValue, value);
        QFETCH(DownloadFailure, failure);
        auto object = fixture();
        object.insert(field, value);
        const auto result = decodeYoutubeAudioResolution(bytes(object), video_id);
        QCOMPARE(result.error, failure);
        QVERIFY(!result.audio);
    }
    void rejectsArbitraryHttpHeaders_data() {
        QTest::addColumn<QJsonObject>("headers");
        for (const auto* key : {"Cookie", "Authorization", "Referer", "Origin", "Range", "X-Arbitrary"})
            QTest::newRow(key) << QJsonObject{{QLatin1String(key), "value"}};
        QTest::newRow("control injection") << QJsonObject{{"User-Agent", "Agent\r\nInjected: value"}};
        QTest::newRow("oversized value") << QJsonObject{{"Accept", QString(513, 'a')}};
        QTest::newRow("non ASCII") << QJsonObject{{"Accept-Language", QStringLiteral("日本語")}};
        QTest::newRow("non string") << QJsonObject{{"Accept", 7}};
        QTest::newRow("case alias duplicate") << QJsonObject{{"Accept", "*/*"}, {"accept", "*/*"}};
    }
    void rejectsArbitraryHttpHeaders() {
        QFETCH(QJsonObject, headers);
        auto object = fixture();
        object.insert("http_headers", headers);
        const auto result = decodeYoutubeAudioResolution(bytes(object), video_id);
        QCOMPARE(result.error, DownloadFailure::Restricted);
        QVERIFY(!result.audio);
    }
    void validatesOnlyConstrainedGooglevideoUrlsAndNeverRewritesSignatures_data() {
        QTest::addColumn<QString>("url");
        QTest::addColumn<bool>("valid");
        QTest::newRow("signed URL") << stream << true;
        QTest::newRow("default port") << QString("https://r1.googlevideo.com:443/videoplayback?expire=1&sig=%2f%2F")
                                      << true;
        QTest::newRow("expired but syntactically valid")
            << QString("https://r1.googlevideo.com/videoplayback?expire=1") << true;
        QTest::newRow("maximum expiration")
            << QString("https://r1.googlevideo.com/videoplayback?expire=9223372036854775807") << true;
        for (const auto& bad : {"http://r1.googlevideo.com/videoplayback?expire=1",
                                "https://googlevideo.com/videoplayback?expire=1",
                                "https://evilgooglevideo.com/videoplayback?expire=1",
                                "https://r1.googlevideo.com.evil.test/videoplayback?expire=1",
                                "https://user@r1.googlevideo.com/videoplayback?expire=1",
                                "https://@r1.googlevideo.com/videoplayback?expire=1",
                                "https://r1.googlevideo.com:444/videoplayback?expire=1",
                                "https://r1.googlevideo.com./videoplayback?expire=1",
                                "https://r1..googlevideo.com/videoplayback?expire=1",
                                "https://r1.googlevideo.com/other?expire=1",
                                "https://r1.googlevideo.com/a/../videoplayback?expire=1",
                                "https://r1.googlevideo.com/%76ideoplayback?expire=1",
                                "https://r1.googlevideo.com/videoplayback/?expire=1",
                                "https://r1.googlevideo.com/videoplayback?expire=1#fragment",
                                "https://r1.googlevideo.com/videoplayback?expire=1#",
                                "https://r1.googlevideo.com/videoplayback?sig=x",
                                "https://r1.googlevideo.com/videoplayback?expire=1&expire=2",
                                "https://r1.googlevideo.com/videoplayback?expire=0",
                                "https://r1.googlevideo.com/videoplayback?expire=-1",
                                "https://r1.googlevideo.com/videoplayback?expire=1.5",
                                "https://r1.googlevideo.com/videoplayback?expire=9223372036854775808",
                                "https://r1.googlevideo.com/videoplayback?expire=%31",
                                "https://r1.googlevideo.com/videoplayback?expire=1&%65xpire=2",
                                "https://r1.googlevideo.com/videoplayback?expire=1&sig=%zz"})
            QTest::newRow(bad) << QString::fromLatin1(bad) << false;
        QTest::newRow("non ASCII") << QStringLiteral("https://r1.googlevideo.com/videoplayback?expire=1&x=日本語")
                                   << false;
        QTest::newRow("NUL") << stream + QChar{0} << false;
        QTest::newRow("oversized URL") << stream + QString(8192, 'a') << false;
    }
    void validatesOnlyConstrainedGooglevideoUrlsAndNeverRewritesSignatures() {
        QFETCH(QString, url);
        QFETCH(bool, valid);
        QCOMPARE(validYoutubeAudioStreamUrl(url), valid);
        const auto expiration = youtubeAudioStreamExpiry(url);
        QCOMPARE(expiration.has_value(), valid);
        auto object = fixture();
        object.insert("url", url);
        const auto result = decodeYoutubeAudioResolution(bytes(object), video_id);
        QCOMPARE(result.audio.has_value(), valid);
        if (valid) {
            QCOMPARE(result.audio->stream_url, url);
            QCOMPARE(result.audio->expires_at_seconds, *expiration);
            QVERIFY(*expiration > 0);
        } else
            QCOMPARE(result.error, DownloadFailure::InvalidResult);
    }
    void durationAndSizeBoundariesAreRepresentable() {
        auto object = fixture();
        object.insert("duration", 86400);
        object.insert("filesize", 512 * 1024 * 1024);
        auto result = decodeYoutubeAudioResolution(bytes(object), video_id);
        QCOMPARE(result.error, DownloadFailure::None);
        QVERIFY(result.audio);
        QCOMPARE(result.audio->duration_ms, quint64{86'400'000});
        QCOMPARE(result.audio->expected_bytes, std::optional<quint64>{512 * 1024 * 1024});
        object.insert("duration", 0.001);
        result = decodeYoutubeAudioResolution(bytes(object), video_id);
        QVERIFY(result.audio);
        QCOMPARE(result.audio->duration_ms, quint64{1});
    }
    void completedFiniteArchivesAreAcceptedButActiveAndProcessingLivesAreNot() {
        auto object = fixture();
        object.insert("live_status", "was_live");
        object.insert("was_live", true);
        auto result = decodeYoutubeAudioResolution(bytes(object), video_id);
        QCOMPARE(result.error, DownloadFailure::None);
        QVERIFY(result.audio);
        object.insert("is_live", true);
        result = decodeYoutubeAudioResolution(bytes(object), video_id);
        QCOMPARE(result.error, DownloadFailure::UnsupportedMedia);
        QVERIFY(!result.audio);
    }
    void resolverArgumentsAreFixedAndDoNotReadUserConfigurationOrWriteMedia() {
        const auto node = QStringLiteral("C:/RC Music 私有 & literal/node.exe");
        const auto arguments = youtubeAudioResolverArguments(video_id, node);
        QVERIFY(!arguments.isEmpty());
        for (const auto* flag : {"--ignore-config",
                                 "--no-config-locations",
                                 "--no-plugin-dirs",
                                 "--no-cookies",
                                 "--no-cookies-from-browser",
                                 "--no-cache-dir",
                                 "--no-remote-components",
                                 "--no-js-runtimes",
                                 "--js-runtimes",
                                 "--no-update",
                                 "--no-exec",
                                 "--no-mark-watched",
                                 "--no-playlist",
                                 "--no-audio-multistreams",
                                 "--no-video-multistreams",
                                 "--no-check-formats",
                                 "--simulate",
                                 "--print",
                                 "--no-progress",
                                 "--no-colors",
                                 "--use-extractors",
                                 "--socket-timeout",
                                 "--extractor-retries",
                                 "--retries",
                                 "--proxy"})
            QVERIFY2(arguments.contains(QLatin1String(flag)), flag);
        const auto js_index = arguments.indexOf("--js-runtimes");
        QVERIFY(js_index + 1 < arguments.size());
        QCOMPARE(arguments[js_index + 1], "node:" + node);
        QCOMPARE(arguments[arguments.size() - 2], QStringLiteral("--"));
        QCOMPARE(arguments.back(), "https://www.youtube.com/watch?v=" + video_id);
        const auto format_index = arguments.indexOf("--format");
        QVERIFY(format_index >= 0 && format_index + 1 < arguments.size());
        QVERIFY(arguments[format_index + 1].contains("m4a"));
        QVERIFY(arguments[format_index + 1].contains("https"));
        const auto sort_index = arguments.indexOf("--format-sort");
        QVERIFY(sort_index >= 0 && sort_index + 1 < arguments.size());
        QCOMPARE(arguments[sort_index + 1], QStringLiteral("lang,quality,source,channels,abr,br,asr"));
        const auto print_index = arguments.indexOf("--print");
        QVERIFY(print_index >= 0 && print_index + 1 < arguments.size());
        const auto projection = arguments[print_index + 1];
        QVERIFY(projection.startsWith("%(.{"));
        QVERIFY(projection.endsWith("})j"));
        for (const auto* field : {"id", "extractor_key", "url", "has_drm", "is_live", "live_status", "availability",
                                  "entries", "requested_formats", "fragments", "http_headers"})
            QVERIFY(projection.contains(QLatin1String(field)));
        QVERIFY(!projection.contains(",formats,"));
        QVERIFY(!projection.contains("automatic_captions"));
        QVERIFY(!projection.contains("description"));
        QVERIFY(!arguments.contains("--dump-single-json"));
        QCOMPARE(arguments[arguments.indexOf("--proxy") + 1], QString{});
        QCOMPARE(arguments[arguments.indexOf("--use-extractors") + 1], QStringLiteral("Youtube"));
        QCOMPARE(arguments[arguments.indexOf("--socket-timeout") + 1], QStringLiteral("10"));
        QCOMPARE(arguments[arguments.indexOf("--extractor-retries") + 1], QStringLiteral("0"));
        QCOMPARE(arguments[arguments.indexOf("--retries") + 1], QStringLiteral("0"));
        for (const auto* forbidden :
             {"--exec", "--cookies", "--cookies-from-browser", "--netrc", "--no-check-certificate", "--extract-audio",
              "--audio-format", "--user-agent", "--add-headers"})
            QVERIFY(!arguments.contains(QLatin1String(forbidden)));
        QVERIFY(youtubeAudioResolverArguments("bad;id", node).isEmpty());
        QVERIFY(youtubeAudioResolverArguments(video_id, "node.exe").isEmpty());
        QVERIFY(youtubeAudioResolverArguments(video_id, "//server/node.exe").isEmpty());
        QVERIFY(youtubeAudioResolverArguments(video_id, node + QChar{0}).isEmpty());
    }
};
QTEST_GUILESS_MAIN(YoutubeAudioResolutionTests)
#include "YoutubeAudioResolutionTests.moc"
