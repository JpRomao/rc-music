#include "DownloadNetworkPolicy.hpp"
#include "NativeDownloadCodec.hpp"

#include <algorithm>
#include <array>

namespace music_player::m0 {
namespace {

constexpr qsizetype max_url_units = 8192;
constexpr qsizetype max_dns_answers = 32;

using Subnet = std::pair<QHostAddress, int>;

template <std::size_t Size> std::array<Subnet, Size> subnets(const std::array<const char*, Size>& entries) {
    std::array<Subnet, Size> result;
    for (std::size_t index = 0; index < Size; ++index)
        result[index] = QHostAddress::parseSubnet(QString::fromLatin1(entries[index]));
    return result;
}

template <std::size_t Size> bool contains(const std::array<Subnet, Size>& ranges, const QHostAddress& address) {
    return std::any_of(ranges.begin(), ranges.end(),
                       [&address](const Subnet& range) { return address.isInSubnet(range); });
}

bool validLocationBytes(const QByteArray& location) {
    return !location.isEmpty() && location.size() <= max_url_units &&
           std::all_of(location.begin(), location.end(), [](unsigned char value) {
               return value > 0x20 && value < 0x7f && value != '\\' && value != '#' && value != '"' && value != '<' &&
                      value != '>';
           });
}

// RFC 3986 path merging with literal dot segments only. Encoded dot aliases are
// refused BEFORE removal, so a later /.. cannot conceal a forbidden segment.
std::optional<QString> resolvePath(const QString& path) {
    const auto segments = path.split(QLatin1Char('/'));
    QStringList output;
    for (qsizetype index = 1; index < segments.size(); ++index) {
        const auto& segment = segments[index];
        auto decoded_dots = segment;
        decoded_dots.replace(QStringLiteral("%2e"), QStringLiteral("."), Qt::CaseInsensitive);
        if (decoded_dots != segment && (decoded_dots == QStringLiteral(".") || decoded_dots == QStringLiteral("..")))
            return std::nullopt;
        if (segment == QStringLiteral(".")) {
            if (index + 1 == segments.size())
                output.push_back({});
        } else if (segment == QStringLiteral("..")) {
            if (!output.isEmpty())
                output.pop_back();
            if (index + 1 == segments.size())
                output.push_back({});
        } else {
            output.push_back(segment);
        }
    }
    return QLatin1Char('/') + output.join(QLatin1Char('/'));
}

} // namespace

std::optional<DownloadNetworkTarget> parseDownloadNetworkTarget(const QString& raw) {
    if (raw.size() > max_url_units || !isAllowedDirectMediaUrl(raw))
        return std::nullopt;
    const bool https = raw.startsWith(QStringLiteral("https://"));
    const qsizetype authority_start = https ? 8 : 7;
    const qsizetype authority_end = raw.indexOf(QLatin1Char('/'), authority_start);
    const auto host = raw.mid(authority_start, authority_end - authority_start);
    return DownloadNetworkTarget{raw, host, host.toLatin1(), raw.mid(authority_end).toLatin1(), https};
}

std::optional<DownloadNetworkTarget> resolveDownloadRedirect(const DownloadNetworkTarget& previous,
                                                             const QByteArray& location) {
    const auto base = parseDownloadNetworkTarget(previous.url);
    if (!base || !validLocationBytes(location))
        return std::nullopt;

    const auto reference = QString::fromLatin1(location);
    const auto base_origin = base->url.left(base->https ? 8 + base->host.size() : 7 + base->host.size());
    const auto base_query = base->url.indexOf(QLatin1Char('?'));
    const auto base_without_query = base_query < 0 ? base->url : base->url.left(base_query);
    QString candidate;
    if (reference.startsWith(QStringLiteral("//"))) {
        candidate = (base->https ? QStringLiteral("https:") : QStringLiteral("http:")) + reference;
    } else {
        const auto colon = reference.indexOf(QLatin1Char(':'));
        const auto slash = reference.indexOf(QLatin1Char('/'));
        const auto query = reference.indexOf(QLatin1Char('?'));
        const bool has_scheme = colon >= 0 && (slash < 0 || colon < slash) && (query < 0 || colon < query);
        if (has_scheme)
            candidate = reference;
        else if (reference.startsWith(QLatin1Char('/')))
            candidate = base_origin + reference;
        else if (reference.startsWith(QLatin1Char('?')))
            candidate = base_without_query + reference;
        else
            candidate = base_without_query.left(base_without_query.lastIndexOf(QLatin1Char('/')) + 1) + reference;
    }

    const bool https = candidate.startsWith(QStringLiteral("https://"));
    if ((!https && !candidate.startsWith(QStringLiteral("http://"))) || (base->https && !https))
        return std::nullopt;
    const auto query_start = candidate.indexOf(QLatin1Char('?'));
    const auto without_query = query_start < 0 ? candidate : candidate.left(query_start);
    const auto query = query_start < 0 ? QString{} : candidate.mid(query_start);
    const auto authority_end = without_query.indexOf(QLatin1Char('/'), https ? 8 : 7);
    // HTTP Location can omit an empty path; the canonical request-target is then '/'.
    const auto origin = authority_end < 0 ? without_query : without_query.left(authority_end);
    const auto path = resolvePath(authority_end < 0 ? QStringLiteral("/") : without_query.mid(authority_end));
    if (!path)
        return std::nullopt;
    return parseDownloadNetworkTarget(origin + *path + query);
}

bool isPublicDownloadAddress(const QHostAddress& address) {
    if (address.isNull() || !address.scopeId().isEmpty())
        return false;

    // All IANA IPv4 special-purpose ranges are excluded, even globally reachable
    // infrastructure exceptions. Multicast is excluded separately by its /4.
    // https://www.iana.org/assignments/iana-ipv4-special-registry/ (2025-10-09)
    if (address.protocol() == QAbstractSocket::IPv4Protocol) {
        static const auto blocked = subnets(std::array{
            "0.0.0.0/8", "10.0.0.0/8", "100.64.0.0/10", "127.0.0.0/8", "169.254.0.0/16", "172.16.0.0/12",
            "192.0.0.0/24", "192.0.2.0/24", "192.31.196.0/24", "192.52.193.0/24", "192.88.99.0/24", "192.168.0.0/16",
            "192.175.48.0/24", "198.18.0.0/15", "198.51.100.0/24", "203.0.113.0/24", "224.0.0.0/4", "240.0.0.0/4"});
        return !contains(blocked, address);
    }
    if (address.protocol() != QAbstractSocket::IPv6Protocol)
        return false;

    // Reviewed RIR allocations only: unlisted 2000::/3 is reserved, not public.
    // This also excludes mapped/compatible IPv4, NAT64, Teredo, 6to4 and scopes.
    // https://www.iana.org/assignments/ipv6-unicast-address-assignments/ (2025-10-10)
    static const auto allocated = subnets(std::array{
        "2001:200::/23",  "2001:400::/23",  "2001:600::/23",  "2001:800::/22",  "2001:c00::/23",  "2001:e00::/23",
        "2001:1200::/23", "2001:1400::/22", "2001:1800::/23", "2001:1a00::/23", "2001:1c00::/22", "2001:2000::/19",
        "2001:4000::/23", "2001:4200::/23", "2001:4400::/23", "2001:4600::/23", "2001:4800::/23", "2001:4a00::/23",
        "2001:4c00::/23", "2001:5000::/20", "2001:8000::/19", "2001:a000::/20", "2001:b000::/20", "2003::/18",
        "2400::/12",      "2410::/12",      "2600::/12",      "2610::/23",      "2620::/23",      "2630::/12",
        "2800::/12",      "2a00::/12",      "2a10::/12",      "2c00::/12"});
    // Special-purpose subranges inside those allocations are still forbidden.
    // https://www.iana.org/assignments/iana-ipv6-special-registry/ (2025-10-09)
    static const auto blocked = subnets(std::array{"2001:db8::/32", "2620:4f:8000::/48"});
    return contains(allocated, address) && !contains(blocked, address);
}

std::optional<QList<QHostAddress>> approveDownloadAddresses(const QList<QHostAddress>& addresses) {
    if (addresses.isEmpty() || addresses.size() > max_dns_answers ||
        !std::all_of(addresses.begin(), addresses.end(), isPublicDownloadAddress))
        return std::nullopt;
    QList<QHostAddress> approved;
    approved.reserve(addresses.size());
    for (const auto& address : addresses) {
        if (!approved.contains(address))
            approved.push_back(address);
    }
    return approved;
}

} // namespace music_player::m0
