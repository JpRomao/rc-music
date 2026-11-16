#include "YoutubeLinks.hpp"
#include "YoutubeAudioCodec.hpp"

#include <QRegularExpression>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <array>
#include <optional>

namespace music_player::m0 {
namespace {
constexpr std::array youtube_hosts{QLatin1StringView("youtube.com"), QLatin1StringView("www.youtube.com"),
                                   QLatin1StringView("m.youtube.com"), QLatin1StringView("music.youtube.com")};
constexpr QLatin1StringView short_host{"youtu.be"};
constexpr std::array letras_hosts{QLatin1StringView("letras.mus.br"), QLatin1StringView("www.letras.mus.br")};

bool validId(const QString& value) {
    return value.size() == 11 && std::all_of(value.begin(), value.end(), [](const QChar unit) {
               const auto code = unit.unicode();
               return (code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9') ||
                      code == '_' || code == '-';
           });
}
template <std::size_t Count> bool listed(const std::array<QLatin1StringView, Count>& hosts, const QString& host) {
    return std::find(hosts.begin(), hosts.end(), host) != hosts.end();
}
bool knownHost(const QString& host) {
    return host == short_host || listed(youtube_hosts, host) || listed(letras_hosts, host);
}
// Strips what usually wraps a pasted address: <...>, (...) and a trailing comma
// or semicolon. A scheme-less address is accepted only for the known hosts.
std::optional<QUrl> webAddress(QString token) {
    while (!token.isEmpty() && (token.front() == QLatin1Char('<') || token.front() == QLatin1Char('(')))
        token.remove(0, 1);
    static const QString closing{QStringLiteral(">),;")};
    while (!token.isEmpty() && closing.contains(token.back()))
        token.chop(1);
    if (!token.contains(QStringLiteral("://"))) {
        if (!knownHost(token.section(QLatin1Char('/'), 0, 0).toLower()))
            return std::nullopt;
        token.prepend(QStringLiteral("https://"));
    }
    const QUrl url(token, QUrl::StrictMode);
    if (!url.isValid() || (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http")) ||
        !url.userInfo().isEmpty() || (url.port(-1) != -1 && url.port() != 443 && url.port() != 80))
        return std::nullopt;
    return url;
}
std::optional<std::string> youtubeId(const QUrl& url) {
    const auto host = url.host().toLower();
    const auto segments = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QString id;
    if (host == short_host) {
        if (segments.size() == 1)
            id = segments.front();
    } else if (listed(youtube_hosts, host)) {
        if (segments == QStringList{QStringLiteral("watch")})
            id = QUrlQuery(url).queryItemValue(QStringLiteral("v"));
        else if (segments.size() == 2 &&
                 (segments.front() == QLatin1String("shorts") || segments.front() == QLatin1String("live") ||
                  segments.front() == QLatin1String("embed")))
            id = segments.back();
    }
    if (!validId(id))
        return std::nullopt;
    return id.toStdString();
}
// Canonical form: https, www host, path only (query and fragment dropped).
std::optional<std::string> letrasPage(const QUrl& url) {
    if (!listed(letras_hosts, url.host().toLower()))
        return std::nullopt;
    const auto page = QStringLiteral("https://www.letras.mus.br") + url.path(QUrl::FullyEncoded);
    if (!validYoutubeLyricsPage(page))
        return std::nullopt;
    return page.toStdString();
}
} // namespace

PastedYoutubeLinks parsePastedYoutubeLinks(const QString& text) {
    PastedYoutubeLinks parsed;
    if (text.size() > pasted_youtube_links_max_chars) {
        parsed.issues.push_back({0, PastedLinkIssueKind::TextTooLong});
        return parsed;
    }
    static const QRegularExpression spaces{QStringLiteral("\\s+")};
    std::optional<std::size_t> last_link;
    int last_link_line = 0, previous_line = 0;
    const auto lines = text.split(QLatin1Char('\n'));
    for (qsizetype index = 0; index < lines.size(); ++index) {
        const int line = static_cast<int>(index) + 1;
        const auto tokens = lines.at(index).split(spaces, Qt::SkipEmptyParts);
        if (tokens.isEmpty())
            continue;
        bool recognized = false, duplicate = false, too_many = false, lonely_letras = false;
        for (const auto& token : tokens) {
            const auto url = webAddress(token);
            if (!url)
                continue;
            if (const auto id = youtubeId(*url)) {
                recognized = true;
                const auto known = std::find_if(parsed.links.begin(), parsed.links.end(),
                                                [&id](const PastedYoutubeLink& link) { return link.video_id == *id; });
                if (known != parsed.links.end()) {
                    duplicate = true;
                    last_link.reset();
                } else if (parsed.links.size() >= pasted_youtube_links_max) {
                    too_many = true;
                    last_link.reset();
                } else {
                    parsed.links.push_back({*id, {}});
                    last_link = parsed.links.size() - 1;
                    last_link_line = line;
                }
            } else if (const auto page = letrasPage(*url)) {
                recognized = true;
                if (duplicate || too_many)
                    continue; // Belongs to a link that was already reported.
                if (last_link && parsed.links[*last_link].lyrics_page.empty() &&
                    (last_link_line == line || last_link_line == previous_line))
                    parsed.links[*last_link].lyrics_page = *page;
                else
                    lonely_letras = true;
            }
        }
        if (!recognized)
            parsed.issues.push_back({line, PastedLinkIssueKind::NotALink});
        if (duplicate)
            parsed.issues.push_back({line, PastedLinkIssueKind::Duplicate});
        if (too_many)
            parsed.issues.push_back({line, PastedLinkIssueKind::TooMany});
        if (lonely_letras)
            parsed.issues.push_back({line, PastedLinkIssueKind::LetrasWithoutYoutube});
        previous_line = line;
    }
    return parsed;
}
} // namespace music_player::m0
