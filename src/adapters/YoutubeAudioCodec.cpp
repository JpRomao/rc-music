#include "YoutubeAudioCodec.hpp"
#include "StrictJson.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <array>
#include <utility>

namespace music_player::m0 {
namespace {
bool asciiHex(const char value) {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
}
bool validRequestId(const std::string_view id) {
    if (id.size() != 36)
        return false;
    for (std::size_t index = 0; index < id.size(); ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (id[index] != '-')
                return false;
        } else if (!asciiHex(id[index]))
            return false;
    }
    return id[14] == '4' &&
           (id[19] == '8' || id[19] == '9' || id[19] == 'a' || id[19] == 'A' || id[19] == 'b' || id[19] == 'B');
}
bool opaqueAscii(const char value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') ||
           value == '_' || value == '-';
}
bool validVideoId(const std::string_view value) {
    return value.size() == 11 && std::all_of(value.begin(), value.end(), opaqueAscii);
}
bool validJobId(const std::string_view value) {
    return !value.empty() && value.size() <= 80 && std::all_of(value.begin(), value.end(), opaqueAscii);
}
template <std::size_t Count> bool exactKeys(const QJsonObject& object, const std::array<QString, Count>& keys) {
    return object.size() == static_cast<qsizetype>(Count) &&
           std::all_of(keys.begin(), keys.end(), [&object](const auto& key) { return object.contains(key); });
}
bool boundaryWhitespace(const QChar value) {
    const auto unit = value.unicode();
    return (unit >= 0x09 && unit <= 0x0d) || unit == 0x20 || unit == 0xa0 || unit == 0x1680 ||
           (unit >= 0x2000 && unit <= 0x200a) || unit == 0x2028 || unit == 0x2029 || unit == 0x202f || unit == 0x205f ||
           unit == 0x3000 || unit == 0xfeff;
}
bool forbiddenTextUnit(const QChar value) {
    const auto unit = value.unicode();
    return unit <= 0x1f || (unit >= 0x7f && unit <= 0x9f) || (unit >= 0x2028 && unit <= 0x202e) ||
           (unit >= 0x2066 && unit <= 0x2069);
}
bool validMetadata(const QString& value, const bool optional) {
    if (value.isEmpty())
        return optional;
    return !boundaryWhitespace(value.front()) && !boundaryWhitespace(value.back()) &&
           std::none_of(value.begin(), value.end(), forbiddenTextUnit);
}
bool validLyricsPage(const QString& raw) {
    if (raw.isEmpty())
        return true;
    if (!raw.startsWith(QLatin1String("https://"), Qt::CaseInsensitive) || raw.contains(QLatin1Char('\\')) ||
        raw.contains(QLatin1Char('?')) || raw.contains(QLatin1Char('#')) ||
        std::any_of(raw.begin(), raw.end(),
                    [](const QChar unit) { return forbiddenTextUnit(unit) || boundaryWhitespace(unit); }))
        return false;
    const auto path_start = raw.indexOf(QLatin1Char('/'), 8);
    if (path_start < 0 || raw.mid(8, path_start - 8).contains(QLatin1Char('@')))
        return false;
    // Check separators before URL parsing can change path boundaries.
    for (qsizetype index = 0; index < raw.size(); ++index) {
        if (raw[index] != QLatin1Char('%'))
            continue;
        if (index + 2 >= raw.size() || !asciiHex(raw[index + 1].toLatin1()) || !asciiHex(raw[index + 2].toLatin1()))
            return false;
        const auto token = raw.mid(index + 1, 2).toLower();
        if (token == QLatin1String("2f") || token == QLatin1String("5c") || token == QLatin1String("25"))
            return false;
        index += 2;
    }
    const QUrl url(raw, QUrl::StrictMode);
    if (!url.isValid() || url.isRelative() || url.scheme() != QLatin1String("https") ||
        (url.host() != QLatin1String("letras.mus.br") && url.host() != QLatin1String("www.letras.mus.br")) ||
        !url.userName().isEmpty() || !url.password().isEmpty() || (url.port(-1) != -1 && url.port(-1) != 443) ||
        url.hasQuery() || url.hasFragment())
        return false;
    const auto path = url.path(QUrl::FullyDecoded);
    if (!path.startsWith(QLatin1Char('/')) || path.size() == 1)
        return false;
    auto segments = path.mid(1).split(QLatin1Char('/'), Qt::KeepEmptyParts);
    if (segments.back().isEmpty())
        segments.removeLast();
    if (segments.isEmpty())
        return false;
    for (const auto& segment : segments) {
        if (segment.isEmpty() || segment == QLatin1String(".") || segment == QLatin1String(".."))
            return false;
        for (const auto scalar : segment.toUcs4())
            if (!QChar::isLetterOrNumber(scalar) && scalar != U'_' && scalar != U'-' && scalar != U'.' &&
                scalar != U'~')
                return false;
    }
    return true;
}
QString errorCode(const DownloadSubmissionError error) {
    switch (error) {
    case DownloadSubmissionError::InvalidRequest:
        return QStringLiteral("invalid_request");
    case DownloadSubmissionError::UnsupportedSource:
        return QStringLiteral("unsupported_source");
    case DownloadSubmissionError::Busy:
        return QStringLiteral("busy");
    case DownloadSubmissionError::NotAvailable:
        return QStringLiteral("not_available");
    case DownloadSubmissionError::LimitExceeded:
        return QStringLiteral("limit_exceeded");
    default:
        return {};
    }
}
} // namespace

YoutubeAudioDecodingResult decodeYoutubeAudioRequest(const QByteArray& payload) {
    YoutubeAudioDecodingResult result;
    if (payload.size() > static_cast<qsizetype>(youtube_audio_max_payload_bytes)) {
        result.error = DownloadSubmissionError::LimitExceeded;
        return result;
    }
    if (!detail::strictJsonPreflight(payload))
        return result;
    const auto document = QJsonDocument::fromJson(payload);
    if (!document.isObject())
        return result;
    const auto object = document.object();
    const auto id = object.value(QStringLiteral("requestId"));
    if (!id.isString() || !validRequestId(id.toString().toStdString()))
        return result;
    result.request.request_id = id.toString().toStdString();
    if (!exactKeys(object, std::array{QStringLiteral("schemaVersion"), QStringLiteral("type"),
                                      QStringLiteral("requestId"), QStringLiteral("items")}) ||
        !object.value(QStringLiteral("schemaVersion")).isDouble() ||
        object.value(QStringLiteral("schemaVersion")).toDouble() != 1.0 ||
        !object.value(QStringLiteral("type")).isString() ||
        object.value(QStringLiteral("type")).toString() != QStringLiteral("youtube.audio.request") ||
        !object.value(QStringLiteral("items")).isArray())
        return result;
    const auto items = object.value(QStringLiteral("items")).toArray();
    if (items.isEmpty())
        return result;
    if (static_cast<std::size_t>(items.size()) > youtube_audio_max_items) {
        result.error = DownloadSubmissionError::LimitExceeded;
        return result;
    }
    QSet<QString> seen;
    std::vector<YoutubeAudioItem> decoded;
    decoded.reserve(static_cast<std::size_t>(items.size()));
    for (const auto& value : items) {
        if (!value.isObject())
            return result;
        auto item = value.toObject();
        const auto name = item.take(QStringLiteral("fileName"));
        const auto watch_url = item.take(QStringLiteral("videoUrl"));
        if (!name.isUndefined() && (!name.isString() || name.toString().isEmpty()))
            return result;
        if (!exactKeys(item, std::array{QStringLiteral("videoId"), QStringLiteral("title"), QStringLiteral("artist"),
                                        QStringLiteral("lyricsPage"), QStringLiteral("fetchLyrics")}) ||
            !item.value(QStringLiteral("videoId")).isString() || !item.value(QStringLiteral("title")).isString() ||
            !item.value(QStringLiteral("artist")).isString() || !item.value(QStringLiteral("lyricsPage")).isString() ||
            !item.value(QStringLiteral("fetchLyrics")).isBool())
            return result;
        const auto video_id = item.value(QStringLiteral("videoId")).toString();
        const auto title = item.value(QStringLiteral("title")).toString();
        const auto artist = item.value(QStringLiteral("artist")).toString();
        const auto lyrics_page = item.value(QStringLiteral("lyricsPage")).toString();
        const auto file_name = name.toString();
        if (title.toUtf8().size() > 512 || artist.toUtf8().size() > 512 || lyrics_page.toUtf8().size() > 2048 ||
            file_name.toUtf8().size() > 512) {
            result.error = DownloadSubmissionError::LimitExceeded;
            return result;
        }
        if (!validVideoId(video_id.toStdString()) || seen.contains(video_id) || !validMetadata(title, false) ||
            !validMetadata(artist, true) || !validMetadata(file_name, true))
            return result;
        // The popup may carry the canonical address explicitly. Keep the typed
        // identity and internal host-to-owner framing based on the opaque ID.
        if (!watch_url.isUndefined() &&
            (!watch_url.isString() || watch_url.toString() != canonicalYoutubeWatchUrl(video_id.toStdString())))
            return result;
        if (!validLyricsPage(lyrics_page)) {
            result.error = DownloadSubmissionError::UnsupportedSource;
            return result;
        }
        seen.insert(video_id);
        decoded.push_back({video_id.toStdString(), title.toStdString(), artist.toStdString(), lyrics_page.toStdString(),
                           item.value(QStringLiteral("fetchLyrics")).toBool(), file_name.toStdString()});
    }
    result.request.items = std::move(decoded);
    result.error = DownloadSubmissionError::None;
    return result;
}
QByteArray encodeYoutubeAudioRequest(const YoutubeAudioRequest& request) {
    if (!validRequestId(request.request_id) || request.items.empty() || request.items.size() > youtube_audio_max_items)
        return {};
    QJsonArray items;
    for (const auto& item : request.items) {
        if (!validVideoId(item.video_id) || item.title.size() > 512 || item.artist.size() > 512 ||
            item.lyrics_page.size() > 2048 || item.file_name.size() > 512)
            return {};
        QJsonObject encoded{{QStringLiteral("videoId"), QString::fromStdString(item.video_id)},
                            {QStringLiteral("title"), QString::fromStdString(item.title)},
                            {QStringLiteral("artist"), QString::fromStdString(item.artist)},
                            {QStringLiteral("lyricsPage"), QString::fromStdString(item.lyrics_page)},
                            {QStringLiteral("fetchLyrics"), item.fetch_lyrics}};
        if (!item.file_name.empty())
            encoded.insert(QStringLiteral("fileName"), QString::fromStdString(item.file_name));
        items.push_back(encoded);
    }
    const auto bytes =
        QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                                  {QStringLiteral("type"), QStringLiteral("youtube.audio.request")},
                                  {QStringLiteral("requestId"), QString::fromStdString(request.request_id)},
                                  {QStringLiteral("items"), items}})
            .toJson(QJsonDocument::Compact);
    const auto decoded = decodeYoutubeAudioRequest(bytes);
    return decoded.error == DownloadSubmissionError::None && decoded.request == request ? bytes : QByteArray{};
}
QByteArray encodeYoutubeAudioResponse(const std::string& request_id, const DownloadSubmissionResult& result) {
    if (!validRequestId(request_id))
        return {};
    QJsonObject object{{QStringLiteral("schemaVersion"), 1},
                       {QStringLiteral("type"), QStringLiteral("youtube.audio.response")},
                       {QStringLiteral("requestId"), QString::fromStdString(request_id)}};
    if (result.error == DownloadSubmissionError::None) {
        if (result.job_ids.empty() || result.job_ids.size() > youtube_audio_max_items)
            return {};
        QJsonArray jobs;
        QSet<QString> seen;
        for (const auto& id : result.job_ids) {
            if (!validJobId(id))
                return {};
            const auto text = QString::fromStdString(id);
            if (seen.contains(text))
                return {};
            seen.insert(text);
            jobs.push_back(text);
        }
        object.insert(QStringLiteral("status"), QStringLiteral("accepted"));
        object.insert(QStringLiteral("jobIds"), jobs);
    } else {
        const auto code = errorCode(result.error);
        if (code.isEmpty() || !result.job_ids.empty())
            return {};
        object.insert(QStringLiteral("status"), QStringLiteral("rejected"));
        object.insert(QStringLiteral("code"), code);
    }
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
std::optional<DownloadSubmissionResult> decodeYoutubeAudioResponse(const QByteArray& payload,
                                                                   const std::string_view expected_request_id,
                                                                   const std::size_t expected_item_count) {
    if (!validRequestId(expected_request_id) || expected_item_count == 0 ||
        expected_item_count > youtube_audio_max_items ||
        payload.size() > static_cast<qsizetype>(youtube_audio_max_payload_bytes) ||
        !detail::strictJsonPreflight(payload))
        return {};
    const auto document = QJsonDocument::fromJson(payload);
    if (!document.isObject())
        return {};
    const auto object = document.object();
    if (!object.value(QStringLiteral("schemaVersion")).isDouble() ||
        object.value(QStringLiteral("schemaVersion")).toDouble() != 1.0 ||
        !object.value(QStringLiteral("type")).isString() ||
        object.value(QStringLiteral("type")).toString() != QStringLiteral("youtube.audio.response") ||
        !object.value(QStringLiteral("requestId")).isString() ||
        object.value(QStringLiteral("requestId")).toString().toStdString() != expected_request_id ||
        !object.value(QStringLiteral("status")).isString())
        return {};
    const auto status = object.value(QStringLiteral("status")).toString();
    if (status == QStringLiteral("accepted")) {
        if (!exactKeys(object,
                       std::array{QStringLiteral("schemaVersion"), QStringLiteral("type"), QStringLiteral("requestId"),
                                  QStringLiteral("status"), QStringLiteral("jobIds")}) ||
            !object.value(QStringLiteral("jobIds")).isArray())
            return {};
        const auto jobs = object.value(QStringLiteral("jobIds")).toArray();
        if (static_cast<std::size_t>(jobs.size()) != expected_item_count)
            return {};
        DownloadSubmissionResult result{.error = DownloadSubmissionError::None};
        QSet<QString> seen;
        for (const auto& value : jobs) {
            if (!value.isString())
                return {};
            const auto text = value.toString();
            if (!validJobId(text.toStdString()) || seen.contains(text))
                return {};
            seen.insert(text);
            result.job_ids.push_back(text.toStdString());
        }
        return result;
    }
    if (status != QStringLiteral("rejected") ||
        !exactKeys(object, std::array{QStringLiteral("schemaVersion"), QStringLiteral("type"),
                                      QStringLiteral("requestId"), QStringLiteral("status"), QStringLiteral("code")}) ||
        !object.value(QStringLiteral("code")).isString())
        return {};
    const auto code = object.value(QStringLiteral("code")).toString();
    for (const auto error :
         {DownloadSubmissionError::InvalidRequest, DownloadSubmissionError::UnsupportedSource,
          DownloadSubmissionError::Busy, DownloadSubmissionError::NotAvailable, DownloadSubmissionError::LimitExceeded})
        if (code == errorCode(error))
            return DownloadSubmissionResult{.error = error};
    return {};
}
bool validYoutubeLyricsPage(const QString& page) { return !page.isEmpty() && validLyricsPage(page); }
QString canonicalYoutubeWatchUrl(const std::string_view video_id) {
    return validVideoId(video_id) ? QStringLiteral("https://www.youtube.com/watch?v=") +
                                        QString::fromLatin1(video_id.data(), static_cast<qsizetype>(video_id.size()))
                                  : QString{};
}
} // namespace music_player::m0
