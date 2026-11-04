#include "BrowserMediaSnapshotCodec.hpp"

#include "DirectMediaPolicy.hpp"
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
constexpr qsizetype max_payload_bytes = 1024 * 1024;
constexpr qsizetype max_candidates = 100;
constexpr qsizetype max_url_units = 8192;
constexpr qsizetype max_title_units = 200;

template <std::size_t Count> bool exactKeys(const QJsonObject& object, const std::array<QString, Count>& keys) {
    return object.size() == static_cast<qsizetype>(Count) &&
           std::all_of(keys.begin(), keys.end(), [&object](const auto& key) { return object.contains(key); });
}

bool isHex(QChar character) {
    const auto value = character.unicode();
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
}

QString cleanTitle(const QString& raw, const DownloadMediaKind kind) {
    QString value;
    value.reserve(raw.size());
    for (const auto character : raw) {
        const auto unit = character.unicode();
        if (unit <= 0x1f || (unit >= 0x7f && unit <= 0x9f) || (unit >= 0x202a && unit <= 0x202e) ||
            (unit >= 0x2066 && unit <= 0x2069))
            continue;
        value.append(character);
    }
    qsizetype first = 0;
    auto last = value.size();
    const auto whitespace = [](const QChar character) { return character.isSpace() || character.unicode() == 0xfeff; };
    while (first < last && whitespace(value[first]))
        ++first;
    while (last > first && whitespace(value[last - 1]))
        --last;
    value = value.mid(first, last - first);
    if (value.isEmpty())
        return kind == DownloadMediaKind::Audio ? QStringLiteral("Áudio") : QStringLiteral("Vídeo");
    return value;
}
} // namespace

bool browserMediaPageAllowed(const QString& page_url) {
    if (page_url.size() > max_url_units || !page_url.startsWith(QStringLiteral("https://")))
        return false;
    const auto fragment = page_url.indexOf(QLatin1Char('#'));
    if (fragment < 0)
        return isAllowedDirectMediaUrl(page_url);
    if (!isAllowedDirectMediaUrl(page_url.left(fragment)))
        return false;
    // The fragment is context data only and never enters a download request.
    // Keep it exact, but reject malformed/noncanonical URL text before Qt parses it.
    for (auto index = fragment + 1; index < page_url.size(); ++index) {
        const auto unit = page_url[index].unicode();
        if (unit <= 0x20 || unit >= 0x7f || unit == '\\' || unit == '"' || unit == '<' || unit == '>')
            return false;
        if (unit == '%' && (page_url.size() - index < 3 || !isHex(page_url[index + 1]) || !isHex(page_url[index + 2])))
            return false;
    }
    return QUrl{page_url, QUrl::StrictMode}.isValid();
}

std::optional<BrowserMediaSnapshot> decodeBrowserMediaSnapshot(const QByteArray& payload) {
    if (payload.size() > max_payload_bytes || !detail::strictJsonPreflight(payload))
        return std::nullopt;
    const auto document = QJsonDocument::fromJson(payload);
    if (!document.isObject())
        return std::nullopt;
    const auto object = document.object();
    if (!exactKeys(object,
                   std::array{QStringLiteral("pageUrl"), QStringLiteral("candidates"), QStringLiteral("truncated")}) ||
        !object.value(QStringLiteral("pageUrl")).isString() || !object.value(QStringLiteral("candidates")).isArray() ||
        !object.value(QStringLiteral("truncated")).isBool())
        return std::nullopt;
    const auto page_url = object.value(QStringLiteral("pageUrl")).toString();
    if (!browserMediaPageAllowed(page_url))
        return std::nullopt;
    const auto candidates = object.value(QStringLiteral("candidates")).toArray();
    if (candidates.size() > max_candidates)
        return std::nullopt;

    BrowserMediaSnapshot result{page_url, {}, object.value(QStringLiteral("truncated")).toBool()};
    result.candidates.reserve(static_cast<std::size_t>(candidates.size()));
    QSet<QString> seen;
    for (const auto& candidate : candidates) {
        if (!candidate.isObject())
            return std::nullopt;
        const auto entry = candidate.toObject();
        if (!exactKeys(entry, std::array{QStringLiteral("url"), QStringLiteral("title"), QStringLiteral("kind")}) ||
            !entry.value(QStringLiteral("url")).isString() || !entry.value(QStringLiteral("title")).isString() ||
            !entry.value(QStringLiteral("kind")).isString())
            return std::nullopt;
        const auto url = entry.value(QStringLiteral("url")).toString();
        const auto title = entry.value(QStringLiteral("title")).toString();
        const auto kind_text = entry.value(QStringLiteral("kind")).toString();
        if (url.size() > max_url_units || title.size() > max_title_units ||
            (kind_text != QStringLiteral("audio") && kind_text != QStringLiteral("video")))
            return std::nullopt;
        if (!isAllowedDirectMediaUrl(url))
            continue;
        if (seen.contains(url))
            return std::nullopt;
        seen.insert(url);
        const auto kind = kind_text == QStringLiteral("audio") ? DownloadMediaKind::Audio : DownloadMediaKind::Video;
        result.candidates.push_back({url.toStdString(), cleanTitle(title, kind).toStdString(), kind});
    }
    return result;
}
} // namespace music_player::m0
