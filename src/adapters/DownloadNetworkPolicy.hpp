#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QString>

#include <optional>

namespace music_player::m0 {
class ResolvedYoutubeAudioCapability;

struct DownloadNetworkTarget final {
    QString url;
    QString host;
    QByteArray host_header;
    QByteArray request_target;
    bool https{false};
};

// Canonical URL syntax is shared with native admission. No DNS or network I/O.
// Encoded path/query and the Host field retain their exact accepted bytes.
[[nodiscard]] std::optional<DownloadNetworkTarget> parseDownloadNetworkTarget(const QString& raw);
// Typed resolver authority only. Initial bytes come exclusively from the
// immutable capability; the caller supplies/validates its trusted clock.
[[nodiscard]] std::optional<DownloadNetworkTarget>
parseResolvedYoutubeNetworkTarget(const ResolvedYoutubeAudioCapability& capability);
[[nodiscard]] std::optional<DownloadNetworkTarget>
resolveYoutubeAudioRedirect(const DownloadNetworkTarget& previous, const QByteArray& location,
                            const ResolvedYoutubeAudioCapability& capability);
// Resolve HTTP Location without decoding or recasing signed escapes. Every result
// re-enters the common policy; HTTPS downgrade and fragments are rejected.
[[nodiscard]] std::optional<DownloadNetworkTarget> resolveDownloadRedirect(const DownloadNetworkTarget& previous,
                                                                           const QByteArray& location);
// Conservative destination policy, not a claim of routability. IPv4-mapped,
// translation, special-purpose, private, scoped and multicast addresses fail closed.
[[nodiscard]] bool isPublicDownloadAddress(const QHostAddress& address);
// Validate ALL answers before selecting any. Reject empty or more than 32 answers
// before deduplication; return unique permitted answers in their original order.
[[nodiscard]] std::optional<QList<QHostAddress>> approveDownloadAddresses(const QList<QHostAddress>& addresses);

} // namespace music_player::m0
