#include "NativeDownloadCodec.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringDecoder>
#include <QUrl>

#include <algorithm>
#include <array>
#include <string_view>

namespace music_player::m0 {
namespace {

constexpr qsizetype max_payload_bytes = 256 * 1024;
constexpr qsizetype max_url_units = 8192;
constexpr qsizetype max_title_units = 200;
constexpr qsizetype max_items = 20;

bool isHex(char value) {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
}

bool validRequestId(std::string_view id) {
    if (id.size() != 36)
        return false;
    for (std::size_t index = 0; index < id.size(); ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (id[index] != '-')
                return false;
        } else if (!isHex(id[index])) {
            return false;
        }
    }
    return id[14] == '4' &&
           (id[19] == '8' || id[19] == '9' || id[19] == 'a' || id[19] == 'A' || id[19] == 'b' || id[19] == 'B');
}

bool jsonWhitespace(char value) { return value == ' ' || value == '\t' || value == '\r' || value == '\n'; }

bool readEscapedUnit(const QByteArray& payload, qsizetype& position, unsigned& unit) {
    if (payload.size() - position < 4)
        return false;
    unit = 0;
    for (int digit = 0; digit < 4; ++digit) {
        const char value = payload[position++];
        if (!isHex(value))
            return false;
        const auto number = value <= '9' ? value - '0' : ((value | 0x20) - 'a' + 10);
        unit = unit * 16 + static_cast<unsigned>(number);
    }
    return true;
}

// Qt's JSON parser accepts some unknown escapes and unpaired escaped surrogates.
// Validate string lexemes before letting it construct values or collapse object keys.
bool skipStrictString(const QByteArray& payload, qsizetype& position) {
    ++position;
    // Qt's per-string UTF-8 conversion can strip a leading BOM, including in member names.
    // No protocol string may begin with one; an interior BOM in a title remains literal data.
    if (payload.size() - position >= 3 && static_cast<unsigned char>(payload[position]) == 0xef &&
        static_cast<unsigned char>(payload[position + 1]) == 0xbb &&
        static_cast<unsigned char>(payload[position + 2]) == 0xbf)
        return false;
    while (position < payload.size()) {
        const unsigned char value = static_cast<unsigned char>(payload[position++]);
        if (value == '"')
            return true;
        if (value < 0x20)
            return false;
        if (value != '\\')
            continue;
        if (position == payload.size())
            return false;
        const char escape = payload[position++];
        if (escape == '"' || escape == '\\' || escape == '/' || escape == 'b' || escape == 'f' || escape == 'n' ||
            escape == 'r' || escape == 't')
            continue;
        if (escape != 'u')
            return false;
        unsigned unit = 0;
        if (!readEscapedUnit(payload, position, unit) || (unit >= 0xdc00 && unit <= 0xdfff))
            return false;
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (payload.size() - position < 6 || payload[position] != '\\' || payload[position + 1] != 'u')
                return false;
            position += 2;
            if (!readEscapedUnit(payload, position, unit) || unit < 0xdc00 || unit > 0xdfff)
                return false;
        }
    }
    return false;
}

bool jsonDigit(char value) { return value >= '0' && value <= '9'; }

bool skipStrictNumber(const QByteArray& payload, qsizetype& position) {
    if (payload[position] == '-')
        ++position;
    if (position == payload.size() || !jsonDigit(payload[position]))
        return false;
    if (payload[position] == '0') {
        ++position;
        if (position < payload.size() && jsonDigit(payload[position]))
            return false;
    } else {
        while (position < payload.size() && jsonDigit(payload[position]))
            ++position;
    }
    if (position < payload.size() && payload[position] == '.') {
        ++position;
        if (position == payload.size() || !jsonDigit(payload[position]))
            return false;
        while (position < payload.size() && jsonDigit(payload[position]))
            ++position;
    }
    if (position < payload.size() && (payload[position] == 'e' || payload[position] == 'E')) {
        ++position;
        if (position < payload.size() && (payload[position] == '+' || payload[position] == '-'))
            ++position;
        if (position == payload.size() || !jsonDigit(payload[position]))
            return false;
        while (position < payload.size() && jsonDigit(payload[position]))
            ++position;
    }
    return position == payload.size() || jsonWhitespace(payload[position]) || payload[position] == ',' ||
           payload[position] == ']' || payload[position] == '}';
}

bool strictJsonPreflight(const QByteArray& payload) {
    QStringDecoder decoder{QStringDecoder::Utf8};
    const QString decoded = decoder(payload);
    if (decoder.hasError() || decoded.toUtf8() != payload)
        return false;

    struct Scope final {
        char opening;
        QSet<QString> keys;
    };
    std::vector<Scope> scopes;
    scopes.reserve(4);
    for (qsizetype position = 0; position < payload.size();) {
        const char value = payload[position];
        if (value == '{' || value == '[') {
            if (scopes.size() == 4)
                return false;
            scopes.push_back({value, {}});
            ++position;
        } else if (value == '}' || value == ']') {
            if (scopes.empty() || (value == '}' ? scopes.back().opening != '{' : scopes.back().opening != '['))
                return false;
            scopes.pop_back();
            ++position;
        } else if (value == '"') {
            const qsizetype start = position;
            if (!skipStrictString(payload, position))
                return false;
            qsizetype next = position;
            while (next < payload.size() && jsonWhitespace(payload[next]))
                ++next;
            if (next < payload.size() && payload[next] == ':') {
                if (scopes.empty() || scopes.back().opening != '{')
                    return false;
                // Only decode a lexically verified key; QJson still owns JSON grammar validation.
                const auto key_doc = QJsonDocument::fromJson('[' + payload.mid(start, position - start) + ']');
                if (!key_doc.isArray() || key_doc.array().size() != 1 || !key_doc.array().first().isString())
                    return false;
                const auto key = key_doc.array().first().toString();
                if (scopes.back().keys.contains(key))
                    return false;
                scopes.back().keys.insert(key);
            }
        } else if (jsonDigit(value) || value == '-') {
            if (!skipStrictNumber(payload, position))
                return false;
        } else if (value == '+' || value == '.') {
            return false;
        } else {
            ++position;
        }
    }
    return scopes.empty();
}

template <std::size_t Count> bool exactKeys(const QJsonObject& object, const std::array<QString, Count>& keys) {
    return object.size() == static_cast<qsizetype>(Count) &&
           std::all_of(keys.begin(), keys.end(), [&object](const auto& key) { return object.contains(key); });
}

bool javascriptWhitespace(QChar value) {
    const auto unit = value.unicode();
    return (unit >= 0x09 && unit <= 0x0d) || unit == 0x20 || unit == 0xa0 || unit == 0x1680 ||
           (unit >= 0x2000 && unit <= 0x200a) || unit == 0x2028 || unit == 0x2029 || unit == 0x202f || unit == 0x205f ||
           unit == 0x3000 || unit == 0xfeff;
}

bool validTitle(const QString& title) {
    if (title.isEmpty() || javascriptWhitespace(title.front()) || javascriptWhitespace(title.back()))
        return false;
    for (const QChar value : title) {
        const auto unit = value.unicode();
        if (unit <= 0x1f || (unit >= 0x7f && unit <= 0x9f) || (unit >= 0x202a && unit <= 0x202e) ||
            (unit >= 0x2066 && unit <= 0x2069))
            return false;
    }
    // Surrogate pairing has already been enforced by the UTF-8/string preflight.
    return true;
}

bool asciiAlphanumeric(QChar value) {
    const auto unit = value.unicode();
    return (unit >= 'a' && unit <= 'z') || (unit >= 'A' && unit <= 'Z') || (unit >= '0' && unit <= '9');
}

bool allowedMediaUrl(const QString& raw) {
    for (qsizetype index = 0; index < raw.size(); ++index) {
        const auto unit = raw[index].unicode();
        if (unit <= 0x20 || unit >= 0x7f || unit == '\\' || unit == '#' || unit == '"' || unit == '<' || unit == '>')
            return false;
        if (unit == '%' &&
            (raw.size() - index < 3 || !isHex(raw[index + 1].toLatin1()) || !isHex(raw[index + 2].toLatin1())))
            return false;
    }
    const bool https = raw.startsWith(QStringLiteral("https://"));
    if (!https && !raw.startsWith(QStringLiteral("http://")))
        return false;
    const qsizetype authority_start = https ? 8 : 7;
    qsizetype authority_end = authority_start;
    while (authority_end < raw.size() && raw[authority_end] != QLatin1Char('/') &&
           raw[authority_end] != QLatin1Char('?'))
        ++authority_end;
    if (authority_end == raw.size() || raw[authority_end] != QLatin1Char('/'))
        return false;
    QString host = raw.mid(authority_start, authority_end - authority_start);
    if (host.contains(QLatin1Char('@')) || host.contains(QLatin1Char('[')) || host.contains(QLatin1Char(']')))
        return false;
    // Browser-normalized default ports are omitted too; preserve one wire representation.
    if (host.contains(QLatin1Char(':')))
        return false;
    if (host != host.toLower())
        return false;
    if (host.endsWith(QLatin1Char('.')))
        host.chop(1);
    const auto labels = host.split(QLatin1Char('.'));
    if (labels.size() < 2 || host.size() > 253)
        return false;
    for (const auto& label : labels) {
        if (label.isEmpty() || label.size() > 63 || !asciiAlphanumeric(label.front()) ||
            !asciiAlphanumeric(label.back()) || !std::all_of(label.begin(), label.end(), [](QChar value) {
                return asciiAlphanumeric(value) || value == QLatin1Char('-');
            }))
            return false;
    }
    // WHATWG parses numeric final labels as IPv4, including shortened/octal/hex spellings.
    const auto& last = labels.back();
    if (std::all_of(last.begin(), last.end(),
                    [](QChar value) { return value >= QLatin1Char('0') && value <= QLatin1Char('9'); }) ||
        (last.startsWith(QStringLiteral("0x")) && last.size() > 2 &&
         std::all_of(last.begin() + 2, last.end(), [](QChar value) { return isHex(value.toLatin1()); })))
        return false;
    static const QStringList blocked{QStringLiteral("youtube.com"),
                                     QStringLiteral("youtu.be"),
                                     QStringLiteral("youtube-nocookie.com"),
                                     QStringLiteral("googlevideo.com"),
                                     QStringLiteral("youtube.googleapis.com"),
                                     QStringLiteral("youtubei.googleapis.com"),
                                     QStringLiteral("youtubekids.com"),
                                     QStringLiteral("localhost"),
                                     QStringLiteral("local"),
                                     QStringLiteral("internal"),
                                     QStringLiteral("lan"),
                                     QStringLiteral("home"),
                                     QStringLiteral("corp"),
                                     QStringLiteral("home.arpa"),
                                     QStringLiteral("in-addr.arpa"),
                                     QStringLiteral("ip6.arpa")};
    for (const auto& domain : blocked) {
        if (host == domain || host.endsWith(QLatin1Char('.') + domain))
            return false;
    }
    const auto query_start = raw.indexOf(QLatin1Char('?'), authority_end);
    const auto path = raw.mid(authority_end, query_start < 0 ? -1 : query_start - authority_end);
    if (path.contains(QLatin1Char('`')) || path.contains(QLatin1Char('{')) || path.contains(QLatin1Char('}')) ||
        (query_start >= 0 && raw.mid(query_start + 1).contains(QLatin1Char('\''))))
        return false;
    for (auto segment : path.split(QLatin1Char('/'))) {
        segment.replace(QStringLiteral("%2e"), QStringLiteral("."), Qt::CaseInsensitive);
        if (segment == QStringLiteral(".") || segment == QStringLiteral(".."))
            return false;
    }
    const QUrl parsed{raw, QUrl::StrictMode};
    return parsed.isValid() && !parsed.isRelative() && !parsed.host().isEmpty();
}

QString errorCode(DownloadSubmissionError error) {
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

bool validJobId(const std::string& id) {
    return !id.empty() && id.size() <= 80 && std::all_of(id.begin(), id.end(), [](char value) {
        return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') ||
               value == '_' || value == '-';
    });
}

} // namespace

NativeDownloadDecodingResult decodeNativeDownloadRequest(const QByteArray& payload) {
    NativeDownloadDecodingResult result;
    if (payload.size() > max_payload_bytes) {
        result.error = DownloadSubmissionError::LimitExceeded;
        return result;
    }
    if (!strictJsonPreflight(payload))
        return result;
    const auto document = QJsonDocument::fromJson(payload);
    if (!document.isObject())
        return result;
    const auto object = document.object();
    const auto id_value = object.value(QStringLiteral("requestId"));
    if (!id_value.isString() || !validRequestId(id_value.toString().toStdString()))
        return result;
    result.request.request_id = id_value.toString().toStdString();
    if (!exactKeys(object, std::array{QStringLiteral("schemaVersion"), QStringLiteral("type"),
                                      QStringLiteral("requestId"), QStringLiteral("items")}) ||
        !object.value(QStringLiteral("schemaVersion")).isDouble() ||
        object.value(QStringLiteral("schemaVersion")).toDouble() != 1.0 ||
        object.value(QStringLiteral("type")).toString() != QStringLiteral("download.request") ||
        !object.value(QStringLiteral("items")).isArray())
        return result;
    const auto items = object.value(QStringLiteral("items")).toArray();
    if (items.isEmpty())
        return result;
    if (items.size() > max_items) {
        result.error = DownloadSubmissionError::LimitExceeded;
        return result;
    }
    std::vector<DownloadItem> decoded_items;
    QSet<QString> seen;
    for (const auto& value : items) {
        if (!value.isObject())
            return result;
        const auto entry = value.toObject();
        if (!exactKeys(entry, std::array{QStringLiteral("url"), QStringLiteral("title"), QStringLiteral("kind")}) ||
            !entry.value(QStringLiteral("url")).isString() || !entry.value(QStringLiteral("title")).isString() ||
            !entry.value(QStringLiteral("kind")).isString())
            return result;
        const auto url = entry.value(QStringLiteral("url")).toString();
        const auto title = entry.value(QStringLiteral("title")).toString();
        const auto kind = entry.value(QStringLiteral("kind")).toString();
        if (url.size() > max_url_units || title.size() > max_title_units) {
            result.error = DownloadSubmissionError::LimitExceeded;
            return result;
        }
        if (!validTitle(title) || (kind != QStringLiteral("audio") && kind != QStringLiteral("video")) ||
            seen.contains(url))
            return result;
        if (!allowedMediaUrl(url)) {
            result.error = DownloadSubmissionError::UnsupportedSource;
            return result;
        }
        seen.insert(url);
        decoded_items.push_back(
            {url.toStdString(), title.toStdString(),
             kind == QStringLiteral("audio") ? DownloadMediaKind::Audio : DownloadMediaKind::Video});
    }
    result.request.items = std::move(decoded_items);
    result.error = DownloadSubmissionError::None;
    return result;
}

QByteArray encodeNativeDownloadResponse(const std::string& request_id, const DownloadSubmissionResult& result) {
    if (!validRequestId(request_id))
        return {};
    QJsonObject response{{QStringLiteral("schemaVersion"), 1},
                         {QStringLiteral("type"), QStringLiteral("download.response")},
                         {QStringLiteral("requestId"), QString::fromStdString(request_id)}};
    if (result.error == DownloadSubmissionError::None) {
        if (result.job_ids.empty() || result.job_ids.size() > static_cast<std::size_t>(max_items))
            return {};
        QJsonArray jobs;
        QSet<QString> seen;
        for (const auto& id : result.job_ids) {
            if (!validJobId(id))
                return {};
            const auto value = QString::fromStdString(id);
            if (seen.contains(value))
                return {};
            seen.insert(value);
            jobs.push_back(value);
        }
        response.insert(QStringLiteral("status"), QStringLiteral("accepted"));
        response.insert(QStringLiteral("jobIds"), jobs);
    } else {
        const auto code = errorCode(result.error);
        if (code.isEmpty() || !result.job_ids.empty())
            return {};
        response.insert(QStringLiteral("status"), QStringLiteral("rejected"));
        response.insert(QStringLiteral("code"), code);
    }
    return QJsonDocument{response}.toJson(QJsonDocument::Compact);
}

} // namespace music_player::m0
