#include "BrowserNavigationPolicy.hpp"

#include <QUrl>

namespace music_player::m0 {
namespace {
int hexValue(const QChar character) {
    const auto unit = character.unicode();
    if (unit >= u'0' && unit <= u'9')
        return unit - u'0';
    if (unit >= u'a' && unit <= u'f')
        return unit - u'a' + 10;
    if (unit >= u'A' && unit <= u'F')
        return unit - u'A' + 10;
    return -1;
}

bool explicitPortValid(const QString& authority) {
    QString port;
    if (authority.startsWith(QLatin1Char('['))) {
        const auto closing = authority.indexOf(QLatin1Char(']'));
        if (closing < 0)
            return false;
        const auto suffix = authority.mid(closing + 1);
        if (suffix.isEmpty())
            return true;
        if (!suffix.startsWith(QLatin1Char(':')))
            return false;
        port = suffix.mid(1);
    } else {
        const auto separator = authority.indexOf(QLatin1Char(':'));
        if (separator < 0)
            return true;
        port = authority.mid(separator + 1);
    }
    if (port.isEmpty())
        return false;
    for (const auto character : port)
        if (character < QLatin1Char('0') || character > QLatin1Char('9'))
            return false;
    bool converted = false;
    const auto number = port.toUInt(&converted);
    return converted && number >= 1 && number <= 65'535;
}
} // namespace

bool browserNavigationAllowed(const QString& url, const bool internal_blank) {
    if (internal_blank && url == QStringLiteral("about:blank"))
        return true;
    if (!url.isValidUtf16() || !url.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))
        return false;
    for (qsizetype i = 0; i < url.size(); ++i) {
        const auto character = url[i];
        if (character.isSpace() || character.category() == QChar::Other_Control || character == QLatin1Char('\\'))
            return false;
        if (character == QLatin1Char('%')) {
            if (url.size() - i < 3)
                return false;
            const auto high = hexValue(url[i + 1]);
            const auto low = hexValue(url[i + 2]);
            if (high < 0 || low < 0)
                return false;
            const auto decoded = high * 16 + low;
            if (decoded < 0x20 || decoded == 0x7f)
                return false;
            i += 2;
        }
    }
    qsizetype authority_end = 8;
    while (authority_end < url.size() && url[authority_end] != QLatin1Char('/') &&
           url[authority_end] != QLatin1Char('?') && url[authority_end] != QLatin1Char('#'))
        ++authority_end;
    const auto authority = url.mid(8, authority_end - 8);
    // Reject even an empty user-info separator and encoded authority text;
    // neither may acquire a different meaning in the browser's URL parser.
    if (authority.isEmpty() || authority.contains(QLatin1Char('@')) || authority.contains(QLatin1Char('%')) ||
        !explicitPortValid(authority))
        return false;
    const QUrl parsed{url, QUrl::StrictMode};
    return parsed.isValid() && !parsed.host().isEmpty() &&
           parsed.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0 && parsed.userInfo().isEmpty();
}
} // namespace music_player::m0
