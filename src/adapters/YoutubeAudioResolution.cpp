#include "YoutubeAudioResolution.hpp"
#include "DownloadUserAgent.hpp"
#include "StrictJson.hpp"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace music_player::m0 {
namespace {
constexpr qsizetype maximum_payload_bytes = 1024 * 1024;
constexpr qsizetype maximum_url_units = 8192;
constexpr quint64 maximum_audio_bytes = 512 * 1024 * 1024;
constexpr double maximum_duration_seconds = 24 * 60 * 60;
bool asciiAlphanumeric(const QChar value) {
    const auto unit = value.unicode();
    return (unit >= 'a' && unit <= 'z') || (unit >= 'A' && unit <= 'Z') || (unit >= '0' && unit <= '9');
}
bool asciiHex(const QChar value) {
    const auto unit = value.unicode();
    return (unit >= 'a' && unit <= 'f') || (unit >= 'A' && unit <= 'F') || (unit >= '0' && unit <= '9');
}
bool validId(const QString& value) {
    return value.size() == 11 && std::all_of(value.begin(), value.end(), [](const QChar unit) {
               return asciiAlphanumeric(unit) || unit == QLatin1Char('_') || unit == QLatin1Char('-');
           });
}
bool forbiddenMetadataUnit(const QChar value) {
    const auto unit = value.unicode();
    return unit <= 0x1f || (unit >= 0x7f && unit <= 0x9f) || (unit >= 0x2028 && unit <= 0x202e) ||
           (unit >= 0x2066 && unit <= 0x2069);
}
bool validPrivateNodePath(const QString& path) {
    if (path.isEmpty() || path.size() > 32'768 || path.startsWith(QLatin1String("//")) ||
        path.startsWith(QLatin1String("\\\\")) || path.startsWith(QLatin1Char(':')) || !QDir::isAbsolutePath(path) ||
        std::any_of(path.begin(), path.end(), forbiddenMetadataUnit) || QString::fromUtf8(path.toUtf8()) != path)
        return false;
#ifdef Q_OS_WIN
    if (path.size() < 3 ||
        !((path.front() >= QLatin1Char('a') && path.front() <= QLatin1Char('z')) ||
          (path.front() >= QLatin1Char('A') && path.front() <= QLatin1Char('Z'))) ||
        path[1] != QLatin1Char(':') || (path[2] != QLatin1Char('/') && path[2] != QLatin1Char('\\')))
        return false;
#endif
    return true;
}
bool validHost(const QString& host) {
    if (host.size() > 253 || !host.endsWith(QLatin1String(".googlevideo.com"), Qt::CaseInsensitive))
        return false;
    const auto labels = host.split(QLatin1Char('.'));
    return std::all_of(labels.begin(), labels.end(), [](const QString& label) {
        return !label.isEmpty() && label.size() <= 63 && asciiAlphanumeric(label.front()) &&
               asciiAlphanumeric(label.back()) && std::all_of(label.begin(), label.end(), [](const QChar unit) {
                   return asciiAlphanumeric(unit) || unit == QLatin1Char('-');
               });
    });
}
YoutubeAudioResolutionResult failure(const DownloadFailure error) { return {.error = error}; }
DownloadFailure metadata(const QJsonObject& object, const QString& key, QString& destination) {
    const auto value = object.value(key);
    if (value.isUndefined())
        return DownloadFailure::None;
    if (!value.isString())
        return DownloadFailure::InvalidResult;
    const auto text = value.toString();
    if (text.size() > 512 || text.toUtf8().size() > 512)
        return DownloadFailure::LimitExceeded;
    if (std::any_of(text.begin(), text.end(), forbiddenMetadataUnit))
        return DownloadFailure::InvalidResult;
    destination = text;
    return DownloadFailure::None;
}
DownloadFailure headers(const QJsonObject& object, QByteArray& user_agent_hint) {
    const auto value = object.value(QStringLiteral("http_headers"));
    if (value.isUndefined())
        return DownloadFailure::None;
    if (!value.isObject())
        return DownloadFailure::InvalidResult;
    const auto dictionary = value.toObject();
    constexpr std::array<const char*, 4> names{"user-agent", "accept", "accept-language", "sec-fetch-mode"};
    QSet<QString> seen;
    QByteArray candidate;
    for (auto entry = dictionary.begin(); entry != dictionary.end(); ++entry) {
        const auto key = entry.key().toLower();
        if (seen.contains(key) ||
            std::none_of(names.begin(), names.end(), [&](const char* name) { return key == QLatin1String(name); }) ||
            !entry.value().isString())
            return DownloadFailure::Restricted;
        seen.insert(key);
        const auto text = entry.value().toString();
        if (text.size() > download_max_user_agent_bytes || std::any_of(text.begin(), text.end(), [](const QChar unit) {
                return unit.unicode() < 0x20 || unit.unicode() > 0x7e;
            }))
            return DownloadFailure::Restricted;
        if (key == QLatin1String("user-agent"))
            candidate = text.toLatin1();
    }
    if (!validDownloadUserAgent(candidate))
        return DownloadFailure::Restricted;
    // Preserve only this public hint; trusted composition still must opt in.
    // Other inspected defaults never become request-header authority.
    user_agent_hint = std::move(candidate);
    return DownloadFailure::None;
}
DownloadFailure byteCount(const QJsonValue& value, std::optional<quint64>& destination) {
    if (value.isUndefined() || value.isNull())
        return DownloadFailure::None;
    if (!value.isDouble())
        return DownloadFailure::InvalidResult;
    const auto number = value.toDouble();
    if (!std::isfinite(number) || number <= 0 || std::trunc(number) != number)
        return DownloadFailure::InvalidResult;
    if (number > static_cast<double>(maximum_audio_bytes))
        return DownloadFailure::LimitExceeded;
    destination = static_cast<quint64>(number);
    return DownloadFailure::None;
}
} // namespace

std::optional<qint64> youtubeAudioStreamExpiry(QString raw) {
    if (raw.isEmpty() || raw.size() > maximum_url_units ||
        !raw.startsWith(QLatin1String("https://"), Qt::CaseInsensitive))
        return {};
    for (qsizetype index = 0; index < raw.size(); ++index) {
        const auto unit = raw[index].unicode();
        if (unit <= 0x20 || unit >= 0x7f || unit == '\\' || unit == '#' || unit == '"' || unit == '<' || unit == '>')
            return {};
        if (unit == '%' && (index + 2 >= raw.size() || !asciiHex(raw[index + 1]) || !asciiHex(raw[index + 2])))
            return {};
    }
    const auto path_start = raw.indexOf(QLatin1Char('/'), 8);
    if (path_start < 0)
        return {};
    auto authority = raw.mid(8, path_start - 8);
    if (authority.contains(QLatin1Char('@')))
        return {};
    if (authority.endsWith(QLatin1String(":443")))
        authority.chop(4);
    if (!validHost(authority))
        return {};
    const auto query_start = raw.indexOf(QLatin1Char('?'), path_start);
    if (query_start < 0 || raw.mid(path_start, query_start - path_start) != QLatin1String("/videoplayback"))
        return {};
    const QUrl parsed(raw, QUrl::StrictMode);
    if (!parsed.isValid() || parsed.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) != 0 ||
        !parsed.userInfo().isEmpty() || parsed.hasFragment() || parsed.port(443) != 443)
        return {};
    std::optional<qint64> expiry;
    const auto parameters = raw.mid(query_start + 1).split(QLatin1Char('&'));
    for (const auto& parameter : parameters) {
        const auto separator = parameter.indexOf(QLatin1Char('='));
        const auto key = separator < 0 ? parameter : parameter.left(separator);
        const auto decoded_key = QByteArray::fromPercentEncoding(key.toLatin1()).toLower();
        if (decoded_key != QByteArrayLiteral("expire"))
            continue;
        if (key != QLatin1String("expire") || expiry || separator < 0)
            return {};
        const auto number = parameter.mid(separator + 1);
        if (number.isEmpty() || !std::all_of(number.begin(), number.end(), [](const QChar unit) {
                return unit >= QLatin1Char('0') && unit <= QLatin1Char('9');
            }))
            return {};
        bool ok{};
        const auto seconds = number.toLongLong(&ok);
        if (!ok || seconds <= 0)
            return {};
        expiry = seconds;
    }
    return expiry;
}
bool validYoutubeAudioStreamUrl(QString url) { return youtubeAudioStreamExpiry(std::move(url)).has_value(); }

YoutubeAudioResolutionResult decodeYoutubeAudioResolution(QByteArray payload, QString expected_video_id) {
    if (payload.size() > maximum_payload_bytes)
        return failure(DownloadFailure::LimitExceeded);
    if (!validId(expected_video_id) || !detail::strictJsonPreflight(payload, 16))
        return failure(DownloadFailure::InvalidResult);
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(payload, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        return failure(DownloadFailure::InvalidResult);
    const auto object = document.object();
    const auto id = object.value(QStringLiteral("id"));
    const auto extractor = object.value(QStringLiteral("extractor_key"));
    if (!id.isString() || id.toString() != expected_video_id || !extractor.isString() ||
        extractor.toString() != QLatin1String("Youtube"))
        return failure(DownloadFailure::InvalidResult);
    const auto type = object.value(QStringLiteral("_type"));
    if (!type.isUndefined()) {
        if (!type.isString())
            return failure(DownloadFailure::InvalidResult);
        if (type.toString() != QLatin1String("video"))
            return failure(DownloadFailure::UnsupportedMedia);
    }
    for (const auto* field : {"entries", "requested_formats", "fragments", "url_transparent"})
        if (object.contains(QLatin1String(field)))
            return failure(DownloadFailure::UnsupportedMedia);
    for (const auto* field : {"is_live", "was_live", "has_drm"}) {
        const auto value = object.value(QLatin1String(field));
        if (!value.isUndefined() && !value.isNull() && !value.isBool())
            return failure(DownloadFailure::InvalidResult);
    }
    if (object.value(QStringLiteral("has_drm")).toBool())
        return failure(DownloadFailure::Restricted);
    if (object.value(QStringLiteral("is_live")).toBool())
        return failure(DownloadFailure::UnsupportedMedia);
    const auto live_status = object.value(QStringLiteral("live_status"));
    if (!live_status.isUndefined() && !live_status.isNull()) {
        if (!live_status.isString())
            return failure(DownloadFailure::InvalidResult);
        const auto status = live_status.toString();
        if (status != QLatin1String("not_live") && status != QLatin1String("was_live"))
            return failure(DownloadFailure::UnsupportedMedia);
        if (status == QLatin1String("was_live") && !object.value(QStringLiteral("is_live")).isBool())
            return failure(DownloadFailure::InvalidResult);
    }
    const auto availability = object.value(QStringLiteral("availability"));
    if (!availability.isUndefined() && !availability.isNull()) {
        if (!availability.isString())
            return failure(DownloadFailure::InvalidResult);
        if (availability.toString() != QLatin1String("public") && availability.toString() != QLatin1String("unlisted"))
            return failure(DownloadFailure::Restricted);
    }
    if (object.value(QStringLiteral("ext")).toString() != QLatin1String("m4a") ||
        (object.value(QStringLiteral("acodec")).toString() != QLatin1String("mp4a.40.2") &&
         object.value(QStringLiteral("acodec")).toString() != QLatin1String("aac")) ||
        object.value(QStringLiteral("vcodec")).toString() != QLatin1String("none") ||
        object.value(QStringLiteral("protocol")).toString() != QLatin1String("https"))
        return failure(DownloadFailure::UnsupportedMedia);
    QByteArray user_agent_hint;
    const auto header_status = headers(object, user_agent_hint);
    if (header_status != DownloadFailure::None)
        return failure(header_status);
    const auto duration = object.value(QStringLiteral("duration"));
    if (!duration.isDouble() || !std::isfinite(duration.toDouble()) || duration.toDouble() <= 0)
        return failure(DownloadFailure::InvalidResult);
    if (duration.toDouble() > maximum_duration_seconds)
        return failure(DownloadFailure::LimitExceeded);
    const auto duration_ms = static_cast<quint64>(std::llround(duration.toDouble() * 1000));
    if (duration_ms == 0)
        return failure(DownloadFailure::InvalidResult);
    YoutubeResolvedAudioData audio;
    audio.video_id = std::move(expected_video_id);
    audio.duration_ms = duration_ms;
    auto status = byteCount(object.value(QStringLiteral("filesize")), audio.expected_bytes);
    if (status != DownloadFailure::None)
        return failure(status);
    std::optional<quint64> approximation;
    status = byteCount(object.value(QStringLiteral("filesize_approx")), approximation);
    if (status != DownloadFailure::None)
        return failure(status);
    for (const auto& field :
         {std::pair{QStringLiteral("title"), &audio.title}, std::pair{QStringLiteral("artist"), &audio.artist}}) {
        status = metadata(object, field.first, *field.second);
        if (status != DownloadFailure::None)
            return failure(status);
    }
    const auto url = object.value(QStringLiteral("url"));
    if (!url.isString())
        return failure(DownloadFailure::InvalidResult);
    audio.stream_url = url.toString();
    const auto expiry = youtubeAudioStreamExpiry(audio.stream_url);
    if (!expiry)
        return failure(DownloadFailure::InvalidResult);
    audio.expires_at_seconds = *expiry;
    audio.user_agent_hint = std::move(user_agent_hint);
    return {.error = DownloadFailure::None, .audio = std::move(audio)};
}

QStringList youtubeAudioResolverArguments(QString video_id, QString private_node_absolute) {
    if (!validId(video_id) || !validPrivateNodePath(private_node_absolute))
        return {};
    return {QStringLiteral("--ignore-config"),
            QStringLiteral("--no-config-locations"),
            QStringLiteral("--no-plugin-dirs"),
            QStringLiteral("--no-cookies"),
            QStringLiteral("--no-cookies-from-browser"),
            QStringLiteral("--no-cache-dir"),
            QStringLiteral("--no-remote-components"),
            QStringLiteral("--no-js-runtimes"),
            QStringLiteral("--js-runtimes"),
            QStringLiteral("node:") + private_node_absolute,
            QStringLiteral("--no-update"),
            QStringLiteral("--no-exec"),
            QStringLiteral("--no-mark-watched"),
            QStringLiteral("--no-playlist"),
            QStringLiteral("--no-audio-multistreams"),
            QStringLiteral("--no-video-multistreams"),
            QStringLiteral("--no-check-formats"),
            QStringLiteral("--simulate"),
            QStringLiteral("--print"),
            QStringLiteral(
                "%(.{_type,id,extractor_key,url,title,artist,duration,filesize,filesize_approx,ext,acodec,vcodec,"
                "protocol,availability,is_live,was_live,live_status,has_drm,http_headers,entries,requested_formats,"
                "fragments})j"),
            QStringLiteral("--no-progress"),
            QStringLiteral("--no-colors"),
            QStringLiteral("--use-extractors"),
            QStringLiteral("Youtube"),
            QStringLiteral("--socket-timeout"),
            QStringLiteral("10"),
            QStringLiteral("--extractor-retries"),
            QStringLiteral("0"),
            QStringLiteral("--retries"),
            QStringLiteral("0"),
            QStringLiteral("--proxy"),
            QString{},
            QStringLiteral("--format"),
            QStringLiteral("bestaudio[ext=m4a][vcodec=none]"
                           "[acodec~='^(mp4a[.]40[.]2|aac)$'][protocol=https]"),
            QStringLiteral("--format-sort"),
            QStringLiteral("lang,quality,source,channels,abr,br,asr"),
            QStringLiteral("--"),
            QStringLiteral("https://www.youtube.com/watch?v=") + video_id};
}
ResolvedYoutubeAudioCapability::ResolvedYoutubeAudioCapability(std::string job_id, YoutubeResolvedAudioData data)
    : job_id_(std::move(job_id)), data_(std::move(data)) {}
} // namespace music_player::m0
