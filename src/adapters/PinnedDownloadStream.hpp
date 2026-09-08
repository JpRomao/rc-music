#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QSslConfiguration>
#include <QString>

#include <memory>

namespace music_player::m0 {

inline constexpr qint64 pinned_download_stream_buffer_bytes = 64 * 1024;

// Trusted adapter-to-adapter input, after public-address and URL policy checks.
// Tests may pin loopback here; the production policy must never admit it.
struct PinnedDownloadEndpoint final {
    QHostAddress address{};
    QString host{};              // Logical ASCII host, without brackets/port; TLS omits only a terminal DNS root dot.
    QByteArray request_target{}; // Original encoded path/query, not reconstructed with QUrl.
    quint16 port{443};
    bool tls{true};
};

enum class PinnedDownloadStreamState { Idle, Connecting, Receiving, Finished, Failed, Cancelled };
enum class PinnedDownloadStreamError { None, InvalidEndpoint, Connection, Tls };

struct PinnedDownloadStreamUpdate final {
    PinnedDownloadStreamState state{PinnedDownloadStreamState::Idle};
    PinnedDownloadStreamError error{PinnedDownloadStreamError::None};
    qint64 available_bytes{0};
    // TLS requires an explicitly observed close_notify. A backend that cannot
    // expose it leaves false; never accept close-delimited HTTPS on that basis.
    bool authenticated_eof{false};
};

// Event-loop-owned byte transport using asynchronous sockets. No DNS lookup, HTTP parsing,
// credentials, redirects or deadlines; the owning downloader provides those policies.
// Qt's receive high-water and the verified OS socket receive buffer leave room
// for EOF/TLS records under the 64 KiB readable-data limit. This is not a bound
// on every Qt/TLS/native allocation; a violated readable limit fails closed.
// No blocking wait is used here, but native Schannel certificate validation can
// still block or perform auxiliary retrieval; its deadline/egress gate remains
// open in ADR-0009. Event-driven I/O does not guarantee its immediate interruption.
class PinnedDownloadStream final : public QObject {
  public:
    explicit PinnedDownloadStream(QObject* parent = nullptr);
    ~PinnedDownloadStream() override;

    [[nodiscard]] bool open(const PinnedDownloadEndpoint& endpoint);
    [[nodiscard]] PinnedDownloadStreamUpdate poll() const;
    [[nodiscard]] QByteArray read(qint64 max_bytes);
    void cancel();

    // Per-instance trusted configuration only (for example, a synthetic test CA).
    // Never relax peer verification or TLS 1.2+, even if requested by this configuration.
    // Refused while a connection is active. Does not alter OS/global trust.
    [[nodiscard]] bool setTlsConfiguration(const QSslConfiguration& configuration);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace music_player::m0
