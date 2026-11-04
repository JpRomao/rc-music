#include "NativeDownloadCodec.hpp"
#include "StrictJson.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

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
    if (!detail::strictJsonPreflight(payload))
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
        if (!isAllowedDirectMediaUrl(url)) {
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

QByteArray encodeNativeDownloadRequest(const DownloadRequest& request) {
    if (!validRequestId(request.request_id) || request.items.empty() ||
        request.items.size() > static_cast<std::size_t>(max_items))
        return {};
    QJsonArray items;
    for (const auto& item : request.items) {
        // Bound construction before Qt allocates decoded strings. URLs are
        // ASCII by policy; four UTF-8 bytes per UTF-16 unit is a safe title cap.
        if (item.url.size() > static_cast<std::size_t>(max_url_units) ||
            item.title.size() > static_cast<std::size_t>(4 * max_title_units))
            return {};
        QString kind;
        switch (item.kind) {
        case DownloadMediaKind::Audio:
            kind = QStringLiteral("audio");
            break;
        case DownloadMediaKind::Video:
            kind = QStringLiteral("video");
            break;
        default:
            return {};
        }
        items.push_back(QJsonObject{{QStringLiteral("url"), QString::fromStdString(item.url)},
                                    {QStringLiteral("title"), QString::fromStdString(item.title)},
                                    {QStringLiteral("kind"), kind}});
    }
    const QJsonObject object{{QStringLiteral("schemaVersion"), 1},
                             {QStringLiteral("type"), QStringLiteral("download.request")},
                             {QStringLiteral("requestId"), QString::fromStdString(request.request_id)},
                             {QStringLiteral("items"), items}};
    const auto payload = QJsonDocument{object}.toJson(QJsonDocument::Compact);
    const auto decoded = decodeNativeDownloadRequest(payload);
    // The one strict policy remains authoritative; invalid UTF-8 replacement
    // or any other coercion cannot turn a malformed DTO into a different batch.
    return decoded.error == DownloadSubmissionError::None && decoded.request == request ? payload : QByteArray{};
}

std::optional<DownloadSubmissionResult> decodeNativeDownloadResponse(const QByteArray& payload,
                                                                     const std::string_view expected_request_id,
                                                                     const std::size_t expected_item_count) {
    if (!validRequestId(expected_request_id) || expected_item_count == 0 ||
        expected_item_count > static_cast<std::size_t>(max_items) || payload.size() > max_payload_bytes ||
        !detail::strictJsonPreflight(payload))
        return std::nullopt;
    const auto document = QJsonDocument::fromJson(payload);
    if (!document.isObject())
        return std::nullopt;
    const auto object = document.object();
    const auto id = object.value(QStringLiteral("requestId"));
    const auto status = object.value(QStringLiteral("status"));
    if (!object.value(QStringLiteral("schemaVersion")).isDouble() ||
        object.value(QStringLiteral("schemaVersion")).toDouble() != 1.0 ||
        !object.value(QStringLiteral("type")).isString() ||
        object.value(QStringLiteral("type")).toString() != QStringLiteral("download.response") || !id.isString() ||
        id.toString().size() != 36 || id.toString().toStdString() != expected_request_id || !status.isString())
        return std::nullopt;
    if (status.toString() == QStringLiteral("accepted")) {
        if (!exactKeys(object,
                       std::array{QStringLiteral("schemaVersion"), QStringLiteral("type"), QStringLiteral("requestId"),
                                  QStringLiteral("status"), QStringLiteral("jobIds")}) ||
            !object.value(QStringLiteral("jobIds")).isArray())
            return std::nullopt;
        const auto ids = object.value(QStringLiteral("jobIds")).toArray();
        if (static_cast<std::size_t>(ids.size()) != expected_item_count)
            return std::nullopt;
        DownloadSubmissionResult result{.error = DownloadSubmissionError::None};
        result.job_ids.reserve(expected_item_count);
        QSet<QString> seen;
        for (const auto& value : ids) {
            if (!value.isString())
                return std::nullopt;
            const auto id_string = value.toString();
            if (id_string.isEmpty() || id_string.size() > 80)
                return std::nullopt;
            auto job_id = id_string.toStdString();
            if (!validJobId(job_id) || seen.contains(id_string))
                return std::nullopt;
            seen.insert(id_string);
            result.job_ids.push_back(std::move(job_id));
        }
        return result;
    }
    if (status.toString() != QStringLiteral("rejected") ||
        !exactKeys(object, std::array{QStringLiteral("schemaVersion"), QStringLiteral("type"),
                                      QStringLiteral("requestId"), QStringLiteral("status"), QStringLiteral("code")}) ||
        !object.value(QStringLiteral("code")).isString())
        return std::nullopt;
    const auto code = object.value(QStringLiteral("code")).toString();
    for (const auto error : {DownloadSubmissionError::InvalidRequest, DownloadSubmissionError::UnsupportedSource,
                             DownloadSubmissionError::Busy, DownloadSubmissionError::NotAvailable,
                             DownloadSubmissionError::LimitExceeded}) {
        if (code == errorCode(error))
            return DownloadSubmissionResult{.error = error};
    }
    return std::nullopt;
}

} // namespace music_player::m0
