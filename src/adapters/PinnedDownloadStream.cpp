#include "PinnedDownloadStream.hpp"

#include <QNetworkProxy>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpSocket>
#include <QVariant>

#include <algorithm>
#include <optional>

namespace music_player::m0 {
namespace {

// Qt has both plaintext and ciphertext queues. A complete TLS plaintext record
// can overshoot its high-water by up to 16 KiB (compression is disabled). Keep
// both normal queues small and reserve one record above the closing high-water
// to avoid the demonstrated EOF overshoot with an unaligned consumer.
// This does not bound Schannel's hidden intermediate ciphertext: Qt can append
// another input chunk before consuming retained extra bytes. The actual readable
// limit still fails closed, and hidden TLS/native allocation remains a separate
// security gate rather than an arithmetic claim of a total-retention bound.
constexpr qint64 socket_high_water_bytes = 4 * 1024;
constexpr qint64 native_receive_bytes = 4 * 1024;
constexpr qint64 maximum_native_receive_bytes = 8 * 1024;
constexpr qint64 closing_high_water_bytes = 48 * 1024;

[[nodiscard]] bool asciiAlphanumeric(ushort value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9');
}

[[nodiscard]] bool validHost(const QString& host) {
    if (host.isEmpty() || host.size() > 254)
        return false;
    for (const auto character : host) {
        const auto value = character.unicode();
        if (!asciiAlphanumeric(value) && value != '-' && value != '.' && value != ':')
            return false;
    }
    if (host.contains(':')) {
        const QHostAddress address(host);
        return address.protocol() == QAbstractSocket::IPv6Protocol && address.scopeId().isEmpty();
    }
    auto dns = host;
    if (dns.endsWith('.'))
        dns.chop(1);
    if (dns.size() > 253)
        return false;
    for (const auto& label : dns.split('.')) {
        if (label.isEmpty() || label.size() > 63 || !asciiAlphanumeric(label.front().unicode()) ||
            !asciiAlphanumeric(label.back().unicode()))
            return false;
    }
    return true;
}

[[nodiscard]] bool hexDigit(char value) {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
}

[[nodiscard]] bool validTarget(const QByteArray& target) {
    if (target.isEmpty() || target.size() > 8192 || target.front() != '/')
        return false;
    for (qsizetype index = 0; index < target.size(); ++index) {
        const auto value = static_cast<unsigned char>(target.at(index));
        // Same raw ASCII exclusion set as native URL admission. In particular,
        // signed '^'/'|' and query '`'/'{'/'}' must not be silently re-encoded.
        if (value <= 0x20 || value >= 0x7f || value == '\\' || value == '#' || value == '"' || value == '<' ||
            value == '>')
            return false;
        if (value == '%' &&
            (index + 2 >= target.size() || !hexDigit(target.at(index + 1)) || !hexDigit(target.at(index + 2))))
            return false;
    }
    return true;
}

[[nodiscard]] bool validEndpoint(const PinnedDownloadEndpoint& endpoint) {
    return !endpoint.address.isNull() && endpoint.address != QHostAddress::AnyIPv4 &&
           endpoint.address != QHostAddress::AnyIPv6 && endpoint.address.scopeId().isEmpty() && endpoint.port != 0 &&
           validHost(endpoint.host) && validTarget(endpoint.request_target);
}

[[nodiscard]] QByteArray wireRequest(const PinnedDownloadEndpoint& endpoint) {
    auto host = endpoint.host.toLatin1();
    if (host.contains(':'))
        host = '[' + host + ']';
    if (endpoint.port != (endpoint.tls ? 443 : 80))
        host += ':' + QByteArray::number(endpoint.port);
    return "GET " + endpoint.request_target + " HTTP/1.1\r\nHost: " + host +
           "\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n";
}

} // namespace

struct PinnedDownloadStream::Impl final {
    explicit Impl(PinnedDownloadStream* value) : owner(value) {}
    ~Impl() { resetSocket(); }

    PinnedDownloadStream* owner;
    std::unique_ptr<QTcpSocket> socket;
    std::optional<QSslConfiguration> tls_configuration;
    PinnedDownloadEndpoint endpoint;
    QByteArray request;
    PinnedDownloadStreamState state{PinnedDownloadStreamState::Idle};
    PinnedDownloadStreamError error{PinnedDownloadStreamError::None};
    bool close_notify{false};
    bool authenticated_eof{false};

    [[nodiscard]] bool active() const {
        return state == PinnedDownloadStreamState::Connecting || state == PinnedDownloadStreamState::Receiving;
    }

    [[nodiscard]] bool readable() const {
        return state == PinnedDownloadStreamState::Receiving || state == PinnedDownloadStreamState::Finished;
    }

    void resetSocket() {
        if (!socket)
            return;
        QObject::disconnect(socket.get(), nullptr, owner, nullptr);
        socket->abort();
        socket.reset();
    }

    void fail(PinnedDownloadStreamError failure) {
        if (!active() && state != PinnedDownloadStreamState::Finished)
            return;
        state = PinnedDownloadStreamState::Failed;
        error = failure;
        authenticated_eof = false;
        request.clear();
        // Do not destroy a socket while handling one of its own direct signals.
        socket->abort();
    }

    [[nodiscard]] bool pinnedPeer() const {
        return socket->peerAddress().isEqual(endpoint.address, QHostAddress::ConvertV4MappedToIPv4);
    }

    void enforceReadBound() {
        if (socket && socket->bytesAvailable() > pinned_download_stream_buffer_bytes)
            fail(PinnedDownloadStreamError::Connection);
    }

    void sendRequest() {
        if (state != PinnedDownloadStreamState::Connecting)
            return;
        if (!pinnedPeer()) {
            fail(PinnedDownloadStreamError::Connection);
            return;
        }
        if (endpoint.tls) {
            const auto* tls = static_cast<QSslSocket*>(socket.get());
            const auto negotiated = tls->sslConfiguration().nextNegotiatedProtocol();
            if (!tls->isEncrypted() || (!negotiated.isEmpty() && negotiated != "http/1.1")) {
                fail(PinnedDownloadStreamError::Tls);
                return;
            }
        }
        const auto accepted = socket->write(request);
        if (accepted != request.size()) {
            fail(PinnedDownloadStreamError::Connection);
            return;
        }
        request.clear();
        if (active())
            state = PinnedDownloadStreamState::Receiving;
    }

    void connected() {
        if (state != PinnedDownloadStreamState::Connecting)
            return;
        if (!pinnedPeer()) {
            fail(PinnedDownloadStreamError::Connection);
            return;
        }
        if (endpoint.tls)
            static_cast<QSslSocket*>(socket.get())->startClientEncryption();
        else
            sendRequest();
    }

    void disconnected() {
        if (state == PinnedDownloadStreamState::Receiving) {
            state = PinnedDownloadStreamState::Finished;
            authenticated_eof = !endpoint.tls || close_notify;
        } else if (state == PinnedDownloadStreamState::Connecting) {
            fail(endpoint.tls ? PinnedDownloadStreamError::Tls : PinnedDownloadStreamError::Connection);
        }
    }

    void socketError(QAbstractSocket::SocketError failure) {
        if (!active())
            return;
        // Qt reports an ordinary peer close as an error before disconnected.
        // Leave its buffered data untouched; the HTTP decoder still must prove framing.
        if (failure == QAbstractSocket::RemoteHostClosedError && state == PinnedDownloadStreamState::Receiving)
            return;
        const bool tls_failure = failure == QAbstractSocket::SslHandshakeFailedError ||
                                 failure == QAbstractSocket::SslInternalError ||
                                 (endpoint.tls && failure == QAbstractSocket::RemoteHostClosedError);
        fail(tls_failure ? PinnedDownloadStreamError::Tls : PinnedDownloadStreamError::Connection);
    }

    void createSocket() {
        if (endpoint.tls) {
            auto tls = std::make_unique<QSslSocket>(owner);
            auto configuration = tls_configuration.value_or(QSslConfiguration::defaultConfiguration());
            configuration.setPeerVerifyMode(QSslSocket::VerifyPeer);
            configuration.setProtocol(QSsl::TlsV1_2OrLater);
            configuration.setAllowedNextProtocols({QByteArrayLiteral("http/1.1")});
            configuration.setLocalCertificateChain({});
            configuration.setPrivateKey(QSslKey{});
            configuration.setSslOption(QSsl::SslOptionDisableServerNameIndication, false);
            configuration.setSslOption(QSsl::SslOptionDisableCompression, true);
            configuration.setSslOption(QSsl::SslOptionDisableLegacyRenegotiation, true);
            configuration.setSslOption(QSsl::SslOptionDisableSessionPersistence, true);
            configuration.setSslOption(QSsl::SslOptionDisableSessionSharing, true);
            configuration.setSslOption(QSsl::SslOptionDisableSessionTickets, true);
            tls->setSslConfiguration(configuration);
            auto peer_name = endpoint.host;
            if (peer_name.endsWith('.'))
                peer_name.chop(1); // SNI uses the DNS name without its terminal root label.
            tls->setPeerVerifyName(peer_name);
            QObject::connect(tls.get(), &QSslSocket::encrypted, owner, [this] { sendRequest(); });
            QObject::connect(tls.get(), &QSslSocket::sslErrors, owner,
                             [this](const QList<QSslError>&) { fail(PinnedDownloadStreamError::Tls); });
            QObject::connect(tls.get(), &QSslSocket::alertReceived, owner,
                             [this](QSsl::AlertLevel level, QSsl::AlertType type, const QString&) {
                                 if (!active())
                                     return;
                                 if (type == QSsl::AlertType::CloseNotify)
                                     close_notify = true;
                                 else if (level == QSsl::AlertLevel::Fatal)
                                     fail(PinnedDownloadStreamError::Tls);
                             });
            socket = std::move(tls);
        } else {
            socket = std::make_unique<QTcpSocket>(owner);
        }
        socket->setProxy(QNetworkProxy::NoProxy);
        socket->setReadBufferSize(socket_high_water_bytes);
        QObject::connect(socket.get(), &QTcpSocket::connected, owner, [this] { connected(); });
        QObject::connect(socket.get(), &QTcpSocket::disconnected, owner, [this] { disconnected(); });
        QObject::connect(socket.get(), &QTcpSocket::errorOccurred, owner,
                         [this](QAbstractSocket::SocketError failure) { socketError(failure); });
        QObject::connect(socket.get(), &QTcpSocket::readyRead, owner, [this] { enforceReadBound(); });
        QObject::connect(socket.get(), &QTcpSocket::stateChanged, owner, [this](QAbstractSocket::SocketState value) {
            if (endpoint.tls && readable() && value == QAbstractSocket::ClosingState) {
                // Schannel cannot decrypt retained ciphertext after
                // its TCP descriptor becomes invalid. Use the reserved
                // EOF headroom while that descriptor is still valid.
                socket->setReadBufferSize(closing_high_water_bytes);
                socket->flush();
                enforceReadBound();
            }
        });
    }

    [[nodiscard]] bool boundNativeReceiveBuffer() {
        // Bind an ephemeral outbound socket so SO_RCVBUF is fixed before the
        // connection (including unsolicited peer data or TLS) can begin.
        const QHostAddress any(endpoint.address.protocol() == QAbstractSocket::IPv6Protocol ? QHostAddress::AnyIPv6
                                                                                            : QHostAddress::AnyIPv4);
        if (!socket->bind(any, 0)) {
            fail(PinnedDownloadStreamError::Connection);
            return false;
        }
        socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, native_receive_bytes);
        bool valid = false;
        const auto actual = socket->socketOption(QAbstractSocket::ReceiveBufferSizeSocketOption).toLongLong(&valid);
        if (!valid || actual <= 0 || actual > maximum_native_receive_bytes) {
            fail(PinnedDownloadStreamError::Connection);
            return false;
        }
        return true;
    }
};

PinnedDownloadStream::PinnedDownloadStream(QObject* parent) : QObject(parent), impl_(std::make_unique<Impl>(this)) {}
PinnedDownloadStream::~PinnedDownloadStream() = default;

bool PinnedDownloadStream::open(const PinnedDownloadEndpoint& endpoint) {
    if (impl_->active())
        return false;
    impl_->resetSocket();
    impl_->authenticated_eof = false;
    impl_->close_notify = false;
    impl_->request.clear();
    if (!validEndpoint(endpoint)) {
        impl_->state = PinnedDownloadStreamState::Failed;
        impl_->error = PinnedDownloadStreamError::InvalidEndpoint;
        return false;
    }
    impl_->endpoint = endpoint;
    impl_->request = wireRequest(endpoint);
    impl_->error = PinnedDownloadStreamError::None;
    impl_->state = PinnedDownloadStreamState::Connecting;
    impl_->createSocket();
    if (!impl_->boundNativeReceiveBuffer())
        return false;
    impl_->socket->connectToHost(endpoint.address, endpoint.port);
    return true;
}

PinnedDownloadStreamUpdate PinnedDownloadStream::poll() const {
    impl_->enforceReadBound();
    return {impl_->state, impl_->error, impl_->readable() && impl_->socket ? impl_->socket->bytesAvailable() : 0,
            impl_->authenticated_eof};
}

QByteArray PinnedDownloadStream::read(qint64 max_bytes) {
    impl_->enforceReadBound();
    if (!impl_->readable() || !impl_->socket || max_bytes <= 0)
        return {};
    return impl_->socket->read(std::min(max_bytes, pinned_download_stream_buffer_bytes));
}

void PinnedDownloadStream::cancel() {
    if (impl_->state != PinnedDownloadStreamState::Idle && !impl_->active())
        return;
    impl_->state = PinnedDownloadStreamState::Cancelled;
    impl_->error = PinnedDownloadStreamError::None;
    impl_->authenticated_eof = false;
    impl_->request.clear();
    if (impl_->socket)
        impl_->socket->abort();
}

bool PinnedDownloadStream::setTlsConfiguration(const QSslConfiguration& configuration) {
    if (impl_->active())
        return false;
    impl_->tls_configuration = configuration;
    return true;
}

} // namespace music_player::m0
