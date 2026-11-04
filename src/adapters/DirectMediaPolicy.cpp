#include "DirectMediaPolicy.hpp"

#include <QStringList>
#include <QUrl>

#include <algorithm>

namespace music_player::m0 {
namespace {
bool isHex(char value) {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
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
} // namespace

bool isAllowedDirectMediaUrl(const QString& raw) { return raw.size() <= 8192 && allowedMediaUrl(raw); }
} // namespace music_player::m0
