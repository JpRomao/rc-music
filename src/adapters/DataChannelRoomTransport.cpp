#include "adapters/DataChannelRoomTransport.hpp"

#include "adapters/RoomProtocolCodec.hpp"
#include "adapters/WindowsRoomEnvelopeCipher.hpp"
#include "adapters/WindowsRoomSignalingSocket.hpp"
#include "m0/domain/ListeningRoomLimits.hpp"

#include <rtc/rtc.hpp>

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaObject>
#include <QObject>
#include <QTimer>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <variant>

namespace music_player::m0 {
namespace {

constexpr qsizetype maximum_relay_message_bytes = WindowsRoomSignalingSocket::maximum_message_bytes;
const QString version_key = QStringLiteral("room_protocol_version");
const QString kind_key = QStringLiteral("kind");
const QString sdp_key = QStringLiteral("sdp");

void initializeRtc() {
    static std::once_flag once;
    // No libdatachannel log output at all: its logs would carry SDP, candidates and addresses.
    std::call_once(once, [] { rtc::InitLogger(rtc::LogLevel::None); });
}

// Posts work to the owner's thread. libdatachannel callbacks hold only a shared_ptr<Mailbox>;
// the owner disarms it before tearing down, so nothing is posted to a deleted object.
class Mailbox final {
  public:
    explicit Mailbox(QObject* target) : target_(target) {}
    template <typename Function> void post(Function function) {
        std::lock_guard lock(mutex_);
        if (target_ != nullptr)
            QMetaObject::invokeMethod(target_, std::move(function), Qt::QueuedConnection);
    }
    void disarm() {
        std::lock_guard lock(mutex_);
        target_ = nullptr;
    }

  private:
    std::mutex mutex_;
    QObject* target_;
};

// The QObject that receives an owner's queued calls, timers and socket handlers. It is deleted
// with deleteLater (never inside one of its own queued calls); calls already queued then find
// their owner gone through a weak_ptr and do nothing.
class Anchor final {
  public:
    Anchor() : object_(new QObject), mailbox_(std::make_shared<Mailbox>(object_)) {}
    ~Anchor() {
        mailbox_->disarm();
        object_->deleteLater();
    }
    Anchor(const Anchor&) = delete;
    Anchor& operator=(const Anchor&) = delete;
    [[nodiscard]] QObject* object() const { return object_; }
    [[nodiscard]] const std::shared_ptr<Mailbox>& mailbox() const { return mailbox_; }

  private:
    QObject* object_;
    std::shared_ptr<Mailbox> mailbox_;
};

std::optional<std::string> printableAscii(const QString& text) {
    for (const auto c : text)
        if (c.unicode() < 0x20 || c.unicode() > 0x7E)
            return std::nullopt;
    return text.toStdString();
}

std::optional<rtc::Configuration> peerConfiguration(const RoomIdentity& identity,
                                                    const std::vector<std::string>& ice_servers) {
    const auto certificate = mbedTlsFilePath(identity.certificate_file);
    const auto key = mbedTlsFilePath(identity.key_file);
    if (!certificate || !key || identity.key_passphrase.empty())
        return std::nullopt;
    rtc::Configuration configuration;
    try {
        for (const auto& server : ice_servers)
            configuration.iceServers.emplace_back(server);
    } catch (...) {
        return std::nullopt;
    }
    configuration.certificatePemFile = *certificate;
    configuration.keyPemFile = *key;
    configuration.keyPemPass = identity.key_passphrase;
    configuration.maxMessageSize = std::max(maximum_room_control_message_bytes, room_file_chunk_bytes + 12);
    return configuration;
}

std::string lowerHex(const std::array<std::uint8_t, 32>& bytes) {
    return QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()))
        .toHex()
        .toStdString();
}

// "AB:CD:..." -> "abcd...".
std::string normalizedFingerprint(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const auto c : value)
        if (c != ':')
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

// Every a=fingerprint line of the SDP must be sha-256 of `expected`; there must be at least one.
bool sdpFingerprintsAre(const std::string& sdp, const std::string& expected) {
    bool any = false;
    const QString text = QString::fromStdString(sdp);
    for (auto line : text.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (!line.startsWith(QStringLiteral("a=fingerprint:")))
            continue;
        const auto parts = line.mid(14).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() != 2 || parts[0].compare(QStringLiteral("sha-256"), Qt::CaseInsensitive) != 0 ||
            normalizedFingerprint(parts[1].toStdString()) != expected)
            return false;
        any = true;
    }
    return any;
}

std::optional<std::string> remoteFingerprintOf(rtc::PeerConnection& peer) {
    try {
        const auto fingerprint = peer.remoteFingerprint();
        if (fingerprint.algorithm != rtc::CertificateFingerprint::Algorithm::Sha256 || fingerprint.value.empty())
            return std::nullopt;
        auto normalized = normalizedFingerprint(fingerprint.value);
        if (normalized.size() != 64)
            return std::nullopt;
        return normalized;
    } catch (...) {
        return std::nullopt;
    }
}

struct SignalingPayload {
    qint64 version{};
    QString kind;
    std::string sdp;
};

QByteArray payloadText(const QString& kind, const std::string& sdp) {
    QJsonObject object{{version_key, static_cast<qint64>(room_protocol_version)}, {kind_key, kind}};
    if (!sdp.empty())
        object.insert(sdp_key, QString::fromStdString(sdp));
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

// Strict: exact keys per kind. A payload with another version is returned as soon as the version
// is read, whatever else it carries (a future format may differ).
std::optional<SignalingPayload> parsePayload(const QByteArray& text) {
    QJsonParseError error{};
    const auto document = QJsonDocument::fromJson(text, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    const auto object = document.object();
    const auto version = object.value(version_key);
    if (!version.isDouble())
        return std::nullopt;
    const auto number = version.toDouble();
    if (number < 0 || number > 4294967295.0 || number != static_cast<double>(static_cast<qint64>(number)))
        return std::nullopt;
    SignalingPayload payload;
    payload.version = static_cast<qint64>(number);
    if (payload.version != static_cast<qint64>(room_protocol_version))
        return payload;
    payload.kind = object.value(kind_key).toString();
    if (payload.kind == QStringLiteral("reject"))
        return object.size() == 2 ? std::optional(payload) : std::nullopt;
    if ((payload.kind != QStringLiteral("offer") && payload.kind != QStringLiteral("answer")) || object.size() != 3 ||
        !object.value(sdp_key).isString())
        return std::nullopt;
    payload.sdp = object.value(sdp_key).toString().toStdString();
    if (payload.sdp.empty())
        return std::nullopt;
    return payload;
}

// The relay message {"to": <to>, "data": base64(seal(payload))}; empty when it cannot be sealed or
// would exceed the relay's 16 KiB.
QByteArray relayEnvelope(const WindowsRoomEnvelopeCipher& cipher, const QJsonValue& to, const QByteArray& payload) {
    const auto sealed = cipher.seal(payload);
    if (sealed.isEmpty())
        return {};
    auto envelope = QJsonDocument(QJsonObject{{QStringLiteral("to"), to},
                                              {QStringLiteral("data"), QString::fromLatin1(sealed.toBase64())}})
                        .toJson(QJsonDocument::Compact);
    return envelope.size() <= maximum_relay_message_bytes ? envelope : QByteArray{};
}

struct RelayMessage {
    QJsonValue from;
    QString event;
    QJsonValue peer;
    std::optional<SignalingPayload> payload; // the opened data, when it opens and parses
};

std::optional<RelayMessage> parseRelayMessage(const WindowsRoomEnvelopeCipher& cipher, const QByteArray& text) {
    QJsonParseError error{};
    const auto document = QJsonDocument::fromJson(text, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    const auto object = document.object();
    RelayMessage message;
    if (object.contains(QStringLiteral("event"))) {
        message.event = object.value(QStringLiteral("event")).toString();
        message.peer = object.value(QStringLiteral("peer"));
        return message;
    }
    if (object.size() != 2 || !object.contains(QStringLiteral("from")) ||
        !object.value(QStringLiteral("data")).isString())
        return std::nullopt;
    message.from = object.value(QStringLiteral("from"));
    const auto sealed = QByteArray::fromBase64(object.value(QStringLiteral("data")).toString().toLatin1(),
                                               QByteArray::AbortOnBase64DecodingErrors);
    if (sealed.isEmpty())
        return std::nullopt;
    if (const auto plain = cipher.open(sealed))
        message.payload = parsePayload(*plain);
    return message;
}

std::optional<std::uint32_t> peerNumber(const QJsonValue& value) {
    if (!value.isDouble())
        return std::nullopt;
    const auto number = value.toDouble();
    if (number < 0 || number > 4294967295.0 || number != static_cast<double>(static_cast<std::uint32_t>(number)))
        return std::nullopt;
    return static_cast<std::uint32_t>(number);
}

QString relayPath(const RoomInviteLink& invite, const char* role) {
    const auto id = QByteArray(reinterpret_cast<const char*>(invite.room_id.data()), 16)
                        .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return QStringLiteral("/rooms/%1/%2").arg(QString::fromLatin1(id), QString::fromLatin1(role));
}

QByteArrayView bytesOf(const rtc::message_variant& data) {
    if (const auto* binary = std::get_if<rtc::binary>(&data))
        return {reinterpret_cast<const char*>(binary->data()), static_cast<qsizetype>(binary->size())};
    const auto& text = std::get<std::string>(data);
    return {text.data(), static_cast<qsizetype>(text.size())};
}

// One peer connection with its "control" and "media" channels. Before it is handed out, the
// endpoint/connector that created it watches it through the internal callbacks; afterwards it is
// on its own (its own Anchor), and only RoomLink's public callbacks are used.
class DataChannelRoomLink final : public RoomLink, public std::enable_shared_from_this<DataChannelRoomLink> {
  public:
    using Item = std::variant<RoomControlMessage, RoomFileChunk>;

    ~DataChannelRoomLink() override { teardown(); }

    // Creates the PeerConnection; false when libdatachannel refuses the configuration (for
    // example a PEM file it cannot open or a wrong passphrase).
    bool start(const rtc::Configuration& configuration) {
        initializeRtc();
        try {
            peer_ = std::make_shared<rtc::PeerConnection>(configuration);
        } catch (...) {
            return false;
        }
        const auto mailbox = anchor_.mailbox();
        const std::weak_ptr<DataChannelRoomLink> weak = weak_from_this();
        peer_->onStateChange([mailbox, weak](rtc::PeerConnection::State state) {
            mailbox->post([weak, state] {
                if (const auto self = weak.lock())
                    self->stateChanged(state);
            });
        });
        peer_->onGatheringStateChange([mailbox, weak](rtc::PeerConnection::GatheringState state) {
            if (state != rtc::PeerConnection::GatheringState::Complete)
                return;
            mailbox->post([weak] {
                if (const auto self = weak.lock(); self && !self->closed_ && self->on_gathered)
                    self->on_gathered();
            });
        });
        peer_->onDataChannel([mailbox, weak](std::shared_ptr<rtc::DataChannel> channel) {
            mailbox->post([weak, channel] {
                if (const auto self = weak.lock())
                    self->attach(channel);
                else
                    channel->close();
            });
        });
        return true;
    }

    // Guest side: creates both channels, which starts the offer and ICE gathering.
    bool createChannels() {
        try {
            attach(peer_->createDataChannel("control"));
            attach(peer_->createDataChannel("media"));
            return true;
        } catch (...) {
            return false;
        }
    }

    bool setRemote(const std::string& sdp, const char* type) {
        try {
            peer_->setRemoteDescription(rtc::Description(sdp, type));
            return true;
        } catch (...) {
            return false;
        }
    }

    [[nodiscard]] std::optional<std::string> localSdp() const {
        if (!peer_)
            return std::nullopt;
        const auto description = peer_->localDescription();
        if (!description)
            return std::nullopt;
        return std::string(*description);
    }

    [[nodiscard]] std::optional<std::string> verifiedRemoteFingerprint() const {
        return peer_ ? remoteFingerprintOf(*peer_) : std::nullopt;
    }

    // Hands the link out: from now on its public callbacks receive what arrived meanwhile.
    void handOut(std::string fingerprint) {
        fingerprint_ = std::move(fingerprint);
        on_gathered = nullptr;
        on_ready = nullptr;
        on_pending_failure = nullptr;
        handed_ = true;
    }
    void startDelivery() {
        const auto self = shared_from_this();
        while (!closed_ && !pending_.empty()) {
            auto item = std::move(pending_.front());
            pending_.pop_front();
            dispatch(std::move(item));
        }
    }

    [[nodiscard]] std::string peerFingerprint() const override { return fingerprint_; }

    bool sendControl(const RoomControlMessage& message) override {
        const auto bytes = encodeRoomControl(message);
        return !bytes.isEmpty() && send(control_, bytes);
    }
    bool sendMedia(const RoomFileChunk& chunk) override {
        const auto bytes = encodeFileChunk(chunk);
        return !bytes.isEmpty() && send(media_, bytes);
    }
    [[nodiscard]] std::size_t mediaBufferedBytes() const override {
        try {
            return media_ && !closed_ ? media_->bufferedAmount() : 0;
        } catch (...) {
            return 0;
        }
    }
    void close() override {
        if (closed_)
            return;
        teardown();
    }

    // Internal callbacks of the endpoint/connector, used only before handOut.
    std::function<void()> on_gathered;
    std::function<void()> on_ready;
    std::function<void(RoomLinkFailure)> on_pending_failure;

  private:
    bool send(const std::shared_ptr<rtc::DataChannel>& channel, const QByteArray& bytes) {
        if (closed_ || !channel)
            return false;
        try {
            if (!channel->isOpen())
                return false;
            channel->send(reinterpret_cast<const std::byte*>(bytes.constData()),
                          static_cast<std::size_t>(bytes.size()));
            return true; // send() returns false when the message was only buffered: still queued
        } catch (...) {
            return false;
        }
    }

    void attach(const std::shared_ptr<rtc::DataChannel>& channel) {
        if (closed_ || !channel)
            return;
        const auto label = channel->label();
        const bool is_control = label == "control" && !control_;
        const bool is_media = label == "media" && !media_;
        if (!is_control && !is_media) {
            channel->close();
            return;
        }
        (is_control ? control_ : media_) = channel;
        const auto mailbox = anchor_.mailbox();
        const std::weak_ptr<DataChannelRoomLink> weak = weak_from_this();
        channel->onOpen([mailbox, weak] {
            mailbox->post([weak] {
                if (const auto self = weak.lock())
                    self->channelOpened();
            });
        });
        channel->onClosed([mailbox, weak] {
            mailbox->post([weak] {
                if (const auto self = weak.lock())
                    self->remoteClosed(RoomLinkFailure::Closed);
            });
        });
        // Decoded on libdatachannel's thread (the codec is reentrant); only valid input is posted.
        if (is_control)
            channel->onMessage([mailbox, weak](rtc::message_variant data) {
                if (auto message = decodeRoomControl(bytesOf(data)))
                    mailbox->post([weak, message = std::move(*message)] {
                        if (const auto self = weak.lock())
                            self->received(message);
                    });
            });
        else
            channel->onMessage([mailbox, weak](rtc::message_variant data) {
                if (auto chunk = decodeFileChunk(bytesOf(data)))
                    mailbox->post([weak, chunk = std::move(*chunk)] {
                        if (const auto self = weak.lock())
                            self->received(chunk);
                    });
            });
        if (channel->isOpen())
            channelOpened();
    }

    void channelOpened() {
        if (closed_ || handed_ || !control_ || !media_ || !control_->isOpen() || !media_->isOpen())
            return;
        if (on_ready) {
            const auto self = shared_from_this();
            auto ready = on_ready;
            ready();
        }
    }

    void stateChanged(rtc::PeerConnection::State state) {
        if (state == rtc::PeerConnection::State::Failed)
            remoteClosed(RoomLinkFailure::Unreachable);
        else if (state == rtc::PeerConnection::State::Closed)
            remoteClosed(RoomLinkFailure::Closed);
    }

    void received(Item item) {
        if (closed_)
            return;
        if (!handed_ || !pending_.empty()) {
            pending_.push_back(std::move(item));
            if (handed_)
                startDelivery();
            return;
        }
        const auto self = shared_from_this();
        dispatch(std::move(item));
    }

    void dispatch(Item item) {
        if (auto* message = std::get_if<RoomControlMessage>(&item)) {
            if (auto callback = onControl)
                callback(std::move(*message));
        } else if (auto callback = onMedia) {
            callback(std::move(std::get<RoomFileChunk>(item)));
        }
    }

    void remoteClosed(RoomLinkFailure failure) {
        if (closed_)
            return;
        const auto self = shared_from_this();
        teardown();
        if (handed_) {
            if (auto callback = onClosed)
                callback(failure);
        } else if (auto callback = on_pending_failure) {
            callback(failure);
        }
    }

    // Stops every libdatachannel callback first (resetCallbacks waits for one in flight, which only
    // posts to the disarmed mailbox), then closes the channels and the connection.
    void teardown() {
        closed_ = true;
        pending_.clear();
        anchor_.mailbox()->disarm();
        for (auto* channel : {&control_, &media_}) {
            if (*channel) {
                try {
                    (*channel)->resetCallbacks();
                    (*channel)->close();
                } catch (...) {
                }
                channel->reset();
            }
        }
        if (peer_) {
            try {
                peer_->resetCallbacks();
                peer_->close();
            } catch (...) {
            }
            peer_.reset();
        }
    }

    Anchor anchor_;
    std::shared_ptr<rtc::PeerConnection> peer_;
    std::shared_ptr<rtc::DataChannel> control_;
    std::shared_ptr<rtc::DataChannel> media_;
    std::deque<Item> pending_;
    std::string fingerprint_;
    bool closed_{};
    bool handed_{};
};

// Calls `callback(failure)` later on the anchor's thread unless the generation changed.
template <typename State>
void postFailure(const std::shared_ptr<State>& state, std::function<void(RoomLinkFailure)> callback,
                 RoomLinkFailure failure) {
    const std::weak_ptr<State> weak = state;
    const auto generation = state->generation;
    state->anchor.mailbox()->post([weak, generation, callback = std::move(callback), failure] {
        const auto self = weak.lock();
        if (!self || self->generation != generation)
            return;
        self->reset();
        if (callback)
            callback(failure);
    });
}

} // namespace

std::optional<std::string> mbedTlsFilePath(const QString& path) {
    if (path.isEmpty())
        return std::nullopt;
    const auto native = QDir::toNativeSeparators(path);
    if (auto ascii = printableAscii(native))
        return ascii;
    const auto wide = native.toStdWString();
    if (const auto needed = GetShortPathNameW(wide.c_str(), nullptr, 0); needed > 0) {
        std::wstring short_path(needed, L'\0');
        const auto written = GetShortPathNameW(wide.c_str(), short_path.data(), needed);
        if (written > 0 && written < needed) {
            short_path.resize(written);
            if (auto ascii = printableAscii(QString::fromStdWString(short_path)))
                return ascii;
        }
    }
    const auto code_page = GetACP();
    const bool utf8 = code_page == CP_UTF8;
    BOOL lossy = FALSE;
    const auto length = static_cast<int>(wide.size());
    const auto size = WideCharToMultiByte(code_page, utf8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS, wide.c_str(),
                                          length, nullptr, 0, nullptr, utf8 ? nullptr : &lossy);
    if (size <= 0 || lossy)
        return std::nullopt;
    std::string ansi(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(code_page, utf8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS, wide.c_str(), length,
                        ansi.data(), size, nullptr, utf8 ? nullptr : &lossy);
    const auto back_size = MultiByteToWideChar(code_page, MB_ERR_INVALID_CHARS, ansi.data(), size, nullptr, 0);
    std::wstring back(static_cast<std::size_t>(std::max(back_size, 0)), L'\0');
    if (lossy || back_size <= 0 ||
        MultiByteToWideChar(code_page, MB_ERR_INVALID_CHARS, ansi.data(), size, back.data(), back_size) != back_size ||
        back != wide)
        return std::nullopt;
    return ansi;
}

std::vector<std::string> defaultRoomIceServers() { return {"stun:stun.cloudflare.com:3478"}; }

// ---------------------------------------------------------------------------------------- guest

struct DataChannelRoomGuestConnector::State final : std::enable_shared_from_this<State> {
    QString origin;
    std::optional<rtc::Configuration> configuration;
    Anchor anchor;
    std::uint64_t generation{};

    std::optional<WindowsRoomEnvelopeCipher> cipher;
    std::string expected_fingerprint;
    std::unique_ptr<WindowsRoomSignalingSocket> socket;
    std::shared_ptr<DataChannelRoomLink> link;
    bool socket_open{};
    bool gathered{};
    bool offer_sent{};
    bool answered{};
    std::function<void(std::shared_ptr<RoomLink>)> on_connected;
    std::function<void(RoomLinkFailure)> on_failure;

    // Ends the attempt without callbacks.
    void reset() {
        ++generation;
        socket.reset();
        link.reset();
        cipher.reset();
        socket_open = gathered = offer_sent = answered = false;
        on_connected = nullptr;
        on_failure = nullptr;
    }

    void fail(RoomLinkFailure failure) {
        const auto self = shared_from_this();
        auto callback = on_failure;
        reset();
        if (callback)
            callback(failure);
    }

    void maybeSendOffer() {
        if (!socket_open || !gathered || offer_sent || !link)
            return;
        const auto sdp = link->localSdp();
        if (!sdp) {
            fail(RoomLinkFailure::Unreachable);
            return;
        }
        const auto envelope =
            relayEnvelope(*cipher, QStringLiteral("host"), payloadText(QStringLiteral("offer"), *sdp));
        if (envelope.isEmpty() || !socket->sendText(envelope)) {
            fail(envelope.isEmpty() ? RoomLinkFailure::Unreachable : RoomLinkFailure::SignalingUnavailable);
            return;
        }
        offer_sent = true;
    }

    void relayText(const QByteArray& text) {
        const auto message = parseRelayMessage(*cipher, text);
        if (!message)
            return;
        if (message->event == QStringLiteral("host-offline")) {
            fail(RoomLinkFailure::HostOffline);
            return;
        }
        if (message->from != QJsonValue(QStringLiteral("host")) || !message->payload || answered || !offer_sent)
            return;
        const auto& payload = *message->payload;
        if (payload.version != static_cast<qint64>(room_protocol_version)) {
            fail(RoomLinkFailure::VersionMismatch);
            return;
        }
        if (payload.kind == QStringLiteral("reject")) {
            fail(RoomLinkFailure::Refused);
            return;
        }
        if (payload.kind != QStringLiteral("answer"))
            return;
        // The host's certificate must be the one the invite names, before DTLS ever starts.
        if (!sdpFingerprintsAre(payload.sdp, expected_fingerprint)) {
            fail(RoomLinkFailure::Refused);
            return;
        }
        answered = true;
        if (!link->setRemote(payload.sdp, "answer"))
            fail(RoomLinkFailure::Unreachable);
    }

    void linkReady() {
        const auto fingerprint = link->verifiedRemoteFingerprint();
        if (!fingerprint || *fingerprint != expected_fingerprint) {
            fail(RoomLinkFailure::Refused);
            return;
        }
        const auto self = shared_from_this();
        auto ready = link;
        auto callback = on_connected;
        ready->handOut(*fingerprint);
        link.reset(); // the link is the caller's now
        reset();      // closes the relay socket: the guest no longer needs it
        if (callback)
            callback(ready);
        ready->startDelivery();
    }
};

DataChannelRoomGuestConnector::DataChannelRoomGuestConnector(QString signaling_origin, RoomIdentity identity,
                                                             std::vector<std::string> ice_servers)
    : state_(std::make_shared<State>()) {
    state_->origin = std::move(signaling_origin);
    state_->configuration = peerConfiguration(identity, ice_servers);
}

DataChannelRoomGuestConnector::~DataChannelRoomGuestConnector() { state_->reset(); }

void DataChannelRoomGuestConnector::cancel() { state_->reset(); }

void DataChannelRoomGuestConnector::connect(const RoomInviteLink& invite,
                                            std::function<void(std::shared_ptr<RoomLink>)> on_connected,
                                            std::function<void(RoomLinkFailure)> on_failure) {
    const auto state = state_;
    state->reset();
    state->on_connected = std::move(on_connected);
    state->on_failure = std::move(on_failure);
    state->cipher.emplace(invite.secret, invite.room_id);
    state->expected_fingerprint = lowerHex(invite.host_fingerprint);
    const auto target = parseRoomSignalingOrigin(state->origin);
    if (!target) {
        postFailure(state, state->on_failure, RoomLinkFailure::SignalingUnavailable);
        return;
    }
    if (!state->cipher->isValid() || !state->configuration) {
        postFailure(state, state->on_failure, RoomLinkFailure::Unreachable);
        return;
    }

    const std::weak_ptr<State> weak = state;
    const auto generation = state->generation;
    auto current = [weak, generation]() -> std::shared_ptr<State> {
        auto self = weak.lock();
        return self && self->generation == generation ? self : nullptr;
    };

    state->link = std::make_shared<DataChannelRoomLink>();
    if (!state->link->start(*state->configuration)) {
        postFailure(state, state->on_failure, RoomLinkFailure::Unreachable);
        return;
    }
    state->link->on_gathered = [current] {
        if (const auto self = current()) {
            self->gathered = true;
            self->maybeSendOffer();
        }
    };
    state->link->on_ready = [current] {
        if (const auto self = current())
            self->linkReady();
    };
    state->link->on_pending_failure = [current](RoomLinkFailure) {
        if (const auto self = current())
            self->fail(RoomLinkFailure::Unreachable);
    };

    state->socket = std::make_unique<WindowsRoomSignalingSocket>(
        state->anchor.object(), *target, relayPath(invite, "guest"), QByteArray{},
        [current](int status) {
            const auto self = current();
            if (!self)
                return;
            if (status == 101) {
                self->socket_open = true;
                self->maybeSendOffer();
            } else {
                self->fail(status == 429 ? RoomLinkFailure::Refused : RoomLinkFailure::SignalingUnavailable);
            }
        },
        [current](QByteArray text) {
            if (const auto self = current())
                self->relayText(text);
        },
        [current](int) {
            if (const auto self = current())
                self->fail(RoomLinkFailure::SignalingUnavailable);
        });

    if (!state->link->createChannels()) {
        postFailure(state, state->on_failure, RoomLinkFailure::Unreachable);
        return;
    }
    QTimer::singleShot(room_connect_timeout_ms, state->anchor.object(), [current] {
        if (const auto self = current())
            self->fail(RoomLinkFailure::Unreachable);
    });
}

// ----------------------------------------------------------------------------------------- host

struct DataChannelRoomHostEndpoint::State final : std::enable_shared_from_this<State> {
    QString origin;
    RoomIdentity identity;
    std::optional<rtc::Configuration> configuration;
    Anchor anchor;
    std::uint64_t generation{};

    std::optional<WindowsRoomEnvelopeCipher> cipher;
    std::unique_ptr<WindowsRoomSignalingSocket> socket;
    std::map<std::uint32_t, std::shared_ptr<DataChannelRoomLink>> pending;
    std::vector<std::weak_ptr<DataChannelRoomLink>> handed;
    std::function<void(std::shared_ptr<RoomLink>)> on_incoming;
    std::function<void(RoomLinkFailure)> on_failure;

    // Ends the signaling and the pending negotiations; links already handed out stay.
    void reset() {
        ++generation;
        socket.reset();
        pending.clear();
        cipher.reset();
        on_incoming = nullptr;
        on_failure = nullptr;
    }

    void closeAll() {
        reset();
        auto links = std::move(handed);
        handed.clear();
        for (const auto& weak : links)
            if (const auto link = weak.lock())
                link->close();
    }

    void fail(RoomLinkFailure failure) {
        const auto self = shared_from_this();
        auto callback = on_failure;
        reset();
        if (callback)
            callback(failure);
    }

    void reply(std::uint32_t peer, const QByteArray& payload) {
        const auto envelope = relayEnvelope(*cipher, static_cast<qint64>(peer), payload);
        if (!envelope.isEmpty() && socket)
            socket->sendText(envelope);
    }

    void relayText(const QByteArray& text) {
        const auto message = parseRelayMessage(*cipher, text);
        if (!message)
            return;
        if (message->event == QStringLiteral("peer-left")) {
            if (const auto peer = peerNumber(message->peer))
                pending.erase(*peer);
            return;
        }
        const auto peer = peerNumber(message->from);
        if (!peer || !message->payload)
            return;
        const auto& payload = *message->payload;
        if (payload.version != static_cast<qint64>(room_protocol_version)) {
            reply(*peer, payloadText(QStringLiteral("reject"), {}));
            return;
        }
        if (payload.kind == QStringLiteral("offer"))
            offer(*peer, payload.sdp);
    }

    void offer(std::uint32_t peer, const std::string& sdp) {
        pending.erase(peer); // a new offer from the same peer replaces its negotiation
        if (pending.size() >= maximum_room_listeners)
            return;
        auto link = std::make_shared<DataChannelRoomLink>();
        if (!link->start(*configuration))
            return;
        const std::weak_ptr<State> weak = shared_from_this();
        const auto opened = generation;
        const auto* raw = link.get();
        auto current = [weak, opened, peer, raw]() -> std::shared_ptr<State> {
            auto self = weak.lock();
            if (!self || self->generation != opened)
                return nullptr;
            const auto found = self->pending.find(peer);
            return found != self->pending.end() && found->second.get() == raw ? self : nullptr;
        };
        link->on_gathered = [current, peer] {
            if (const auto self = current()) {
                if (const auto answer = self->pending[peer]->localSdp())
                    self->reply(peer, payloadText(QStringLiteral("answer"), *answer));
            }
        };
        link->on_ready = [current, peer] {
            if (const auto self = current())
                self->admit(peer);
        };
        link->on_pending_failure = [current, peer](RoomLinkFailure) {
            if (const auto self = current())
                self->pending.erase(peer);
        };
        pending[peer] = link;
        if (!link->setRemote(sdp, "offer")) {
            pending.erase(peer);
            return;
        }
        QTimer::singleShot(room_connect_timeout_ms, anchor.object(), [current, peer] {
            if (const auto self = current())
                self->pending.erase(peer);
        });
    }

    void admit(std::uint32_t peer) {
        const auto self = shared_from_this();
        auto link = pending[peer];
        pending.erase(peer);
        const auto fingerprint = link->verifiedRemoteFingerprint();
        if (!fingerprint)
            return;
        link->handOut(*fingerprint);
        std::erase_if(handed, [](const auto& weak) { return weak.expired(); });
        handed.push_back(link);
        if (auto callback = on_incoming)
            callback(link);
        link->startDelivery();
    }
};

DataChannelRoomHostEndpoint::DataChannelRoomHostEndpoint(QString signaling_origin, RoomIdentity identity,
                                                         std::vector<std::string> ice_servers)
    : state_(std::make_shared<State>()) {
    state_->origin = std::move(signaling_origin);
    state_->configuration = peerConfiguration(identity, ice_servers);
    state_->identity = std::move(identity);
}

DataChannelRoomHostEndpoint::~DataChannelRoomHostEndpoint() { state_->closeAll(); }

void DataChannelRoomHostEndpoint::closeAll() { state_->closeAll(); }

void DataChannelRoomHostEndpoint::open(const RoomInviteLink& invite,
                                       std::function<void(std::shared_ptr<RoomLink>)> on_incoming,
                                       std::function<void(RoomLinkFailure)> on_failure) {
    const auto state = state_;
    state->closeAll();
    state->on_incoming = std::move(on_incoming);
    state->on_failure = std::move(on_failure);
    const auto target = parseRoomSignalingOrigin(state->origin);
    if (!target) {
        postFailure(state, state->on_failure, RoomLinkFailure::SignalingUnavailable);
        return;
    }
    // Never sends an empty X-Room-Host-Token: an identity without its device secret cannot host.
    auto token = roomHostToken(state->identity, invite.room_id);
    state->cipher.emplace(invite.secret, invite.room_id);
    if (token.isEmpty() || !state->cipher->isValid() || !state->configuration) {
        postFailure(state, state->on_failure, RoomLinkFailure::Unreachable);
        return;
    }

    const std::weak_ptr<State> weak = state;
    const auto generation = state->generation;
    auto current = [weak, generation]() -> std::shared_ptr<State> {
        auto self = weak.lock();
        return self && self->generation == generation ? self : nullptr;
    };
    state->socket = std::make_unique<WindowsRoomSignalingSocket>(
        state->anchor.object(), *target, relayPath(invite, "host"), token,
        [current](int status) {
            const auto self = current();
            if (self && status != 101)
                self->fail(status == 409 ? RoomLinkFailure::Refused : RoomLinkFailure::SignalingUnavailable);
        },
        [current](QByteArray text) {
            if (const auto self = current())
                self->relayText(text);
        },
        [current](int close_status) {
            if (const auto self = current())
                self->fail(close_status == 4001 ? RoomLinkFailure::Refused : RoomLinkFailure::SignalingUnavailable);
        });
    token.fill('\0');
}

} // namespace music_player::m0
