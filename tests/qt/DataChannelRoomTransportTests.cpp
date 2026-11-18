#include "../support/LoopbackSignaling.hpp"
#include "adapters/DataChannelRoomTransport.hpp"
#include "adapters/RoomProtocolCodec.hpp"
#include "adapters/WindowsRoomEnvelopeCipher.hpp"
#include "adapters/WindowsRoomIdentity.hpp"
#include "adapters/WindowsRoomSignalingSocket.hpp"
#include "m0/domain/ListeningRoomLimits.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>
#include <memory>
#include <optional>

using namespace music_player::m0;
using music_player::m0::testing::LoopbackSignaling;

namespace {
constexpr int connect_ms = 20000;
const std::vector<std::string> local_only{}; // host candidates only: no STUN, no internet

RoomIdentity identityIn(const QString& directory) {
    auto identity = loadOrCreateRoomIdentity(directory);
    if (!identity)
        qFatal("cannot create a test identity");
    return *identity;
}

RoomInviteLink inviteFor(const RoomIdentity& host) {
    RoomInviteLink invite;
    QRandomGenerator::system()->generate(invite.room_id.begin(), invite.room_id.end());
    QRandomGenerator::system()->generate(invite.secret.begin(), invite.secret.end());
    invite.host_fingerprint = host.fingerprint;
    return invite;
}

std::string hex(const std::array<std::uint8_t, 32>& bytes) {
    return QByteArray(reinterpret_cast<const char*>(bytes.data()), 32).toHex().toStdString();
}

QString roomPath(const RoomInviteLink& invite, const char* role) {
    const auto id = QByteArray(reinterpret_cast<const char*>(invite.room_id.data()), 16)
                        .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return QStringLiteral("/rooms/%1/%2").arg(QString::fromLatin1(id), QString::fromLatin1(role));
}

QByteArray sealedEnvelope(const RoomInviteLink& invite, const QJsonValue& to, const QJsonObject& payload) {
    const WindowsRoomEnvelopeCipher cipher(invite.secret, invite.room_id);
    const auto sealed = cipher.seal(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    return QJsonDocument(QJsonObject{{QStringLiteral("to"), to},
                                     {QStringLiteral("data"), QString::fromLatin1(sealed.toBase64())}})
        .toJson(QJsonDocument::Compact);
}

// The payload of a relayed {from|to, data} message, opened with the room's cipher.
std::optional<QJsonObject> openedPayload(const RoomInviteLink& invite, const QByteArray& message) {
    const auto object = QJsonDocument::fromJson(message).object();
    const auto data = QByteArray::fromBase64(object.value(QStringLiteral("data")).toString().toLatin1());
    const WindowsRoomEnvelopeCipher cipher(invite.secret, invite.room_id);
    const auto plain = cipher.open(data);
    if (!plain)
        return std::nullopt;
    return QJsonDocument::fromJson(*plain).object();
}

// A raw relay client (the product's WinHTTP socket) for scripting one side by hand.
struct RawClient {
    QObject context;
    int status{-1};
    QList<QByteArray> texts;
    std::unique_ptr<WindowsRoomSignalingSocket> socket;
    RawClient(const QString& origin, const QString& path, const QByteArray& token) {
        socket = std::make_unique<WindowsRoomSignalingSocket>(
            &context, *parseRoomSignalingOrigin(origin), path, token, [this](int value) { status = value; },
            [this](QByteArray text) { texts.push_back(text); }, [](int) {});
    }
    ~RawClient() { socket.reset(); }
};

struct Outcome {
    std::shared_ptr<RoomLink> link;
    std::optional<RoomLinkFailure> failure;
    int links{};
};
} // namespace

class DataChannelRoomTransportTests final : public QObject {
    Q_OBJECT

    QTemporaryDir host_dir_;
    QTemporaryDir guest_dir_;
    QTemporaryDir other_dir_;
    RoomIdentity host_id_;
    RoomIdentity guest_id_;
    RoomIdentity other_id_;

    void openHost(DataChannelRoomHostEndpoint& host, const RoomInviteLink& invite, Outcome& outcome,
                  LoopbackSignaling& relay) {
        const auto before = relay.host_tokens.size();
        host.open(
            invite,
            [&outcome](std::shared_ptr<RoomLink> link) {
                ++outcome.links;
                outcome.link = std::move(link);
            },
            [&outcome](RoomLinkFailure failure) { outcome.failure = failure; });
        QTRY_COMPARE_WITH_TIMEOUT(relay.host_tokens.size(), before + 1, connect_ms);
    }
    static void connectGuest(DataChannelRoomGuestConnector& guest, const RoomInviteLink& invite, Outcome& outcome) {
        guest.connect(
            invite,
            [&outcome](std::shared_ptr<RoomLink> link) {
                ++outcome.links;
                outcome.link = std::move(link);
            },
            [&outcome](RoomLinkFailure failure) { outcome.failure = failure; });
    }

  private slots:
    void initTestCase() {
        QVERIFY(host_dir_.isValid() && guest_dir_.isValid() && other_dir_.isValid());
        host_id_ = identityIn(host_dir_.path());
        guest_id_ = identityIn(guest_dir_.path());
        other_id_ = identityIn(other_dir_.path());
    }

    void hostAndGuestConnectAndBothChannelsCarryBothWays() {
        LoopbackSignaling relay;
        const auto invite = inviteFor(host_id_);
        DataChannelRoomHostEndpoint host(relay.origin(), host_id_, local_only);
        DataChannelRoomGuestConnector guest(relay.origin(), guest_id_, local_only);
        Outcome at_host;
        Outcome at_guest;
        openHost(host, invite, at_host, relay);
        connectGuest(guest, invite, at_guest);
        QTRY_VERIFY_WITH_TIMEOUT(at_host.link && at_guest.link, connect_ms);
        QVERIFY(!at_host.failure && !at_guest.failure);
        QCOMPARE(at_host.links, 1);
        QCOMPARE(at_guest.links, 1);

        std::vector<RoomControlMessage> host_controls;
        std::vector<RoomControlMessage> guest_controls;
        std::vector<RoomFileChunk> host_media;
        std::vector<RoomFileChunk> guest_media;
        at_host.link->onControl = [&](RoomControlMessage message) { host_controls.push_back(std::move(message)); };
        at_host.link->onMedia = [&](RoomFileChunk chunk) { host_media.push_back(std::move(chunk)); };
        at_guest.link->onControl = [&](RoomControlMessage message) { guest_controls.push_back(std::move(message)); };
        at_guest.link->onMedia = [&](RoomFileChunk chunk) { guest_media.push_back(std::move(chunk)); };

        const RoomFileChunk big{7, 65536, std::vector<std::uint8_t>(room_file_chunk_bytes, 0x5a)};
        const RoomFileChunk small{8, 0, {1, 2, 3}};
        QVERIFY(at_guest.link->sendControl(RoomClockPing{1234}));
        QVERIFY(at_host.link->sendControl(RoomClockPong{1234, 99}));
        QVERIFY(at_host.link->sendMedia(big));
        QVERIFY(at_guest.link->sendMedia(small));
        QTRY_VERIFY_WITH_TIMEOUT(host_controls.size() == 1 && guest_controls.size() == 1 && host_media.size() == 1 &&
                                     guest_media.size() == 1,
                                 connect_ms);
        QVERIFY((host_controls[0] == RoomControlMessage{RoomClockPing{1234}}));
        QVERIFY((guest_controls[0] == RoomControlMessage{RoomClockPong{1234, 99}}));
        QVERIFY(guest_media[0] == big);
        QVERIFY(host_media[0] == small);
        QTRY_COMPARE_WITH_TIMEOUT(at_host.link->mediaBufferedBytes(), std::size_t{0}, connect_ms);
        QVERIFY(!at_host.failure && !at_guest.failure);
    }

    void eachPeerFingerprintIsTheOtherIdentity() {
        LoopbackSignaling relay;
        const auto invite = inviteFor(host_id_);
        DataChannelRoomHostEndpoint host(relay.origin(), host_id_, local_only);
        DataChannelRoomGuestConnector guest(relay.origin(), guest_id_, local_only);
        Outcome at_host;
        Outcome at_guest;
        openHost(host, invite, at_host, relay);
        connectGuest(guest, invite, at_guest);
        QTRY_VERIFY_WITH_TIMEOUT(at_host.link && at_guest.link, connect_ms);
        QCOMPARE(at_host.link->peerFingerprint(), hex(guest_id_.fingerprint));
        QCOMPARE(at_guest.link->peerFingerprint(), hex(host_id_.fingerprint));
    }

    void anotherHostFingerprintInTheLinkIsRefused() {
        LoopbackSignaling relay;
        auto invite = inviteFor(host_id_);
        DataChannelRoomHostEndpoint host(relay.origin(), host_id_, local_only);
        Outcome at_host;
        openHost(host, invite, at_host, relay);
        invite.host_fingerprint = other_id_.fingerprint; // the link names another host
        DataChannelRoomGuestConnector guest(relay.origin(), guest_id_, local_only);
        Outcome at_guest;
        connectGuest(guest, invite, at_guest);
        QTRY_VERIFY_WITH_TIMEOUT(at_guest.failure.has_value(), connect_ms);
        QCOMPARE(*at_guest.failure, RoomLinkFailure::Refused);
        QTest::qWait(1000);
        QVERIFY(!at_guest.link);
        QVERIFY(!at_host.link);
        QVERIFY(!at_host.failure);
    }

    // The gate runs on the answer itself, before setRemoteDescription: this answer has no
    // candidates, so without the gate nothing would fail until the connect timeout.
    void anAnswerWithAnotherFingerprintIsRefusedBeforeDtls() {
        LoopbackSignaling relay;
        const auto invite = inviteFor(host_id_);
        RawClient raw_host(relay.origin(), roomPath(invite, "host"), roomHostToken(host_id_, invite.room_id));
        QTRY_COMPARE_WITH_TIMEOUT(raw_host.status, 101, connect_ms);
        DataChannelRoomGuestConnector guest(relay.origin(), guest_id_, local_only);
        Outcome at_guest;
        connectGuest(guest, invite, at_guest);
        QTRY_COMPARE_WITH_TIMEOUT(raw_host.texts.size(), 1, connect_ms);
        const auto offer = openedPayload(invite, raw_host.texts[0]);
        QVERIFY(offer.has_value());
        const auto sdp_line_end = QString(QChar(13)) + QChar(10); // SDP lines end with CRLF
        QStringList answer;
        for (const auto& line : offer->value(QStringLiteral("sdp")).toString().split(sdp_line_end)) {
            if (line.startsWith(QStringLiteral("a=candidate")) ||
                line.startsWith(QStringLiteral("a=end-of-candidates")))
                continue;
            if (line.startsWith(QStringLiteral("a=fingerprint:")))
                answer << QStringLiteral("a=fingerprint:sha-256 ") +
                              QString::fromLatin1(QByteArray::fromStdString(hex(other_id_.fingerprint)).toUpper());
            else
                answer << QString(line).replace(QStringLiteral("a=setup:actpass"), QStringLiteral("a=setup:active"));
        }
        const auto from = QJsonDocument::fromJson(raw_host.texts[0]).object().value(QStringLiteral("from"));
        QVERIFY(raw_host.socket->sendText(sealedEnvelope(
            invite, from,
            QJsonObject{{QStringLiteral("room_protocol_version"), static_cast<qint64>(room_protocol_version)},
                        {QStringLiteral("kind"), QStringLiteral("answer")},
                        {QStringLiteral("sdp"), answer.join(sdp_line_end)}})));
        QTRY_VERIFY_WITH_TIMEOUT(at_guest.failure.has_value(), 3000);
        QCOMPARE(*at_guest.failure, RoomLinkFailure::Refused);
        QVERIFY(!at_guest.link);
    }

    void anAbsentHostIsHostOffline() {
        LoopbackSignaling relay;
        DataChannelRoomGuestConnector guest(relay.origin(), guest_id_, local_only);
        Outcome at_guest;
        connectGuest(guest, inviteFor(host_id_), at_guest);
        QTRY_VERIFY_WITH_TIMEOUT(at_guest.failure.has_value(), connect_ms);
        QCOMPARE(*at_guest.failure, RoomLinkFailure::HostOffline);
        QVERIFY(!at_guest.link);
    }

    void aStoppedServerIsSignalingUnavailable() {
        LoopbackSignaling relay;
        relay.stop();
        const auto invite = inviteFor(host_id_);
        DataChannelRoomHostEndpoint host(relay.origin(), host_id_, local_only);
        DataChannelRoomGuestConnector guest(relay.origin(), guest_id_, local_only);
        Outcome at_host;
        Outcome at_guest;
        host.open(
            invite, [&](std::shared_ptr<RoomLink>) { ++at_host.links; },
            [&](RoomLinkFailure failure) { at_host.failure = failure; });
        connectGuest(guest, invite, at_guest);
        QTRY_VERIFY_WITH_TIMEOUT(at_host.failure && at_guest.failure, connect_ms);
        QCOMPARE(*at_host.failure, RoomLinkFailure::SignalingUnavailable);
        QCOMPARE(*at_guest.failure, RoomLinkFailure::SignalingUnavailable);
    }

    void aClosingHostReachesTheGuestWithinTenSeconds() {
        LoopbackSignaling relay;
        const auto invite = inviteFor(host_id_);
        DataChannelRoomHostEndpoint host(relay.origin(), host_id_, local_only);
        DataChannelRoomGuestConnector guest(relay.origin(), guest_id_, local_only);
        Outcome at_host;
        Outcome at_guest;
        openHost(host, invite, at_host, relay);
        connectGuest(guest, invite, at_guest);
        QTRY_VERIFY_WITH_TIMEOUT(at_host.link && at_guest.link, connect_ms);
        std::optional<RoomLinkFailure> guest_closed;
        int host_closed{};
        at_guest.link->onClosed = [&](RoomLinkFailure failure) { guest_closed = failure; };
        at_host.link->onClosed = [&](RoomLinkFailure) { ++host_closed; };

        QElapsedTimer elapsed;
        elapsed.start();
        host.closeAll();
        QTRY_VERIFY_WITH_TIMEOUT(guest_closed.has_value(), 10000);
        QVERIFY2(elapsed.elapsed() < 10000, qPrintable(QString::number(elapsed.elapsed())));
        QCOMPARE(*guest_closed, RoomLinkFailure::Closed);
        QCOMPARE(host_closed, 0); // a local close does not call onClosed
        QVERIFY(!at_guest.link->sendControl(RoomClockPing{1}));
        QVERIFY(!at_host.link->sendControl(RoomClockPing{1}));
    }

    // The relay socket waits for guests without a deadline: WinHTTP's 10 s timeouts for the
    // handshake must not end an idle host.
    void anIdleHostStillAdmitsAGuestAfterTheHandshakeTimeout() {
        LoopbackSignaling relay;
        const auto invite = inviteFor(host_id_);
        DataChannelRoomHostEndpoint host(relay.origin(), host_id_, local_only);
        Outcome at_host;
        openHost(host, invite, at_host, relay);
        QTest::qWait(11000);
        QVERIFY(!at_host.failure);
        DataChannelRoomGuestConnector guest(relay.origin(), guest_id_, local_only);
        Outcome at_guest;
        connectGuest(guest, invite, at_guest);
        QTRY_VERIFY_WITH_TIMEOUT((at_host.link && at_guest.link) || at_host.failure || at_guest.failure, connect_ms);
        QVERIFY(!at_host.failure && !at_guest.failure);
    }

    void theRelayOnlySeesSealedEnvelopes() {
        LoopbackSignaling relay;
        const auto invite = inviteFor(host_id_);
        DataChannelRoomHostEndpoint host(relay.origin(), host_id_, local_only);
        DataChannelRoomGuestConnector guest(relay.origin(), guest_id_, local_only);
        Outcome at_host;
        Outcome at_guest;
        openHost(host, invite, at_host, relay);
        connectGuest(guest, invite, at_guest);
        QTRY_VERIFY_WITH_TIMEOUT(at_host.link && at_guest.link, connect_ms);

        QCOMPARE(relay.received.size(), 2); // one offer and one answer, no trickle
        const auto host_colon =
            QByteArray::fromHex(QByteArray::fromStdString(hex(host_id_.fingerprint))).toHex(':').toUpper();
        QStringList kinds;
        for (const auto& message : relay.received) {
            QVERIFY(message.size() <= 16 * 1024);
            QVERIFY2(!message.contains("a=fingerprint") && !message.contains("candidate") &&
                         !message.contains("sha-256") && !message.contains(host_colon) &&
                         !message.contains(QByteArray::fromStdString(hex(host_id_.fingerprint))),
                     message.constData());
            const auto object = QJsonDocument::fromJson(message).object();
            QCOMPARE(object.keys(), (QStringList{QStringLiteral("data"), QStringLiteral("to")}));
            const auto payload = openedPayload(invite, message);
            QVERIFY(payload.has_value());
            QCOMPARE(payload->value(QStringLiteral("room_protocol_version")).toInteger(),
                     static_cast<qint64>(room_protocol_version));
            QVERIFY(payload->value(QStringLiteral("sdp")).toString().contains(QStringLiteral("a=fingerprint")));
            kinds.push_back(payload->value(QStringLiteral("kind")).toString());
        }
        QCOMPARE(kinds, (QStringList{QStringLiteral("offer"), QStringLiteral("answer")}));
    }

    void aHostWithAnotherProtocolVersionIsVersionMismatch() {
        LoopbackSignaling relay;
        const auto invite = inviteFor(host_id_);
        RawClient raw_host(relay.origin(), roomPath(invite, "host"), roomHostToken(host_id_, invite.room_id));
        QTRY_COMPARE_WITH_TIMEOUT(raw_host.status, 101, connect_ms);
        DataChannelRoomGuestConnector guest(relay.origin(), guest_id_, local_only);
        Outcome at_guest;
        connectGuest(guest, invite, at_guest);
        QTRY_COMPARE_WITH_TIMEOUT(raw_host.texts.size(), 1, connect_ms);
        const auto offer = openedPayload(invite, raw_host.texts[0]);
        QVERIFY(offer.has_value());
        QCOMPARE(offer->value(QStringLiteral("kind")).toString(), QStringLiteral("offer"));
        const auto from = QJsonDocument::fromJson(raw_host.texts[0]).object().value(QStringLiteral("from"));
        QVERIFY(raw_host.socket->sendText(sealedEnvelope(
            invite, from,
            QJsonObject{{QStringLiteral("room_protocol_version"), static_cast<qint64>(room_protocol_version + 1)},
                        {QStringLiteral("kind"), QStringLiteral("answer")},
                        {QStringLiteral("sdp"), QStringLiteral("v=0")}})));
        QTRY_VERIFY_WITH_TIMEOUT(at_guest.failure.has_value(), connect_ms);
        QCOMPARE(*at_guest.failure, RoomLinkFailure::VersionMismatch);
        QVERIFY(!at_guest.link);
    }

    void aGuestWithAnotherProtocolVersionIsRejected() {
        LoopbackSignaling relay;
        const auto invite = inviteFor(host_id_);
        DataChannelRoomHostEndpoint host(relay.origin(), host_id_, local_only);
        Outcome at_host;
        openHost(host, invite, at_host, relay);
        RawClient raw_guest(relay.origin(), roomPath(invite, "guest"), {});
        QTRY_COMPARE_WITH_TIMEOUT(raw_guest.status, 101, connect_ms);
        QVERIFY(raw_guest.socket->sendText(sealedEnvelope(
            invite, QStringLiteral("host"),
            QJsonObject{{QStringLiteral("room_protocol_version"), static_cast<qint64>(room_protocol_version + 1)},
                        {QStringLiteral("kind"), QStringLiteral("offer")},
                        {QStringLiteral("sdp"), QStringLiteral("v=0")}})));
        QTRY_COMPARE_WITH_TIMEOUT(raw_guest.texts.size(), 1, connect_ms);
        const auto reply = openedPayload(invite, raw_guest.texts[0]);
        QVERIFY(reply.has_value());
        QCOMPARE(reply->value(QStringLiteral("kind")).toString(), QStringLiteral("reject"));
        QCOMPARE(reply->value(QStringLiteral("room_protocol_version")).toInteger(),
                 static_cast<qint64>(room_protocol_version));
        QTest::qWait(500);
        QVERIFY(!at_host.link);
        QVERIFY(!at_host.failure);
    }

    void identitiesInANonAsciiDirectoryConnect() {
        QTemporaryDir host_dir{QDir::tempPath() + QStringLiteral("/rc-music-room-host-espaço-音-XXXXXX")};
        QTemporaryDir guest_dir{QDir::tempPath() + QStringLiteral("/rc-music-room-guest-espaço-音-XXXXXX")};
        QVERIFY(host_dir.isValid() && guest_dir.isValid());
        const auto host_id = identityIn(host_dir.path());
        const auto guest_id = identityIn(guest_dir.path());
        QVERIFY(host_id.key_file.contains(QStringLiteral("音")));
        LoopbackSignaling relay;
        const auto invite = inviteFor(host_id);
        DataChannelRoomHostEndpoint host(relay.origin(), host_id, local_only);
        DataChannelRoomGuestConnector guest(relay.origin(), guest_id, local_only);
        Outcome at_host;
        Outcome at_guest;
        openHost(host, invite, at_host, relay);
        connectGuest(guest, invite, at_guest);
        QTRY_VERIFY_WITH_TIMEOUT((at_host.link && at_guest.link) || at_host.failure || at_guest.failure, connect_ms);
        QVERIFY(!at_host.failure && !at_guest.failure);
        QCOMPARE(at_guest.link->peerFingerprint(), hex(host_id.fingerprint));
        QCOMPARE(at_host.link->peerFingerprint(), hex(guest_id.fingerprint));
    }

    void aNonAsciiPemPathBecomesANarrowPathFopenOpens() {
        QTemporaryDir dir{QDir::tempPath() + QStringLiteral("/rc-music-room-pem-espaço-音-XXXXXX")};
        QVERIFY(dir.isValid());
        const auto path = dir.filePath(QStringLiteral("room-identity-cert.pem"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("pem"), qint64{3});
        file.close();
        const auto narrow = mbedTlsFilePath(path);
        QVERIFY(narrow.has_value());
        for (const auto c : *narrow)
            QVERIFY(c >= 0x20 && c <= 0x7e); // the 8.3 form: no code page can garble it
        std::FILE* opened{};
        QCOMPARE(fopen_s(&opened, narrow->c_str(), "rb"), 0); // what Mbed TLS does
        char bytes[4]{};
        QCOMPARE(std::fread(bytes, 1, 3, opened), std::size_t{3});
        std::fclose(opened);
        QCOMPARE(QByteArray(bytes), QByteArray("pem"));
        QVERIFY(!mbedTlsFilePath(QString{}).has_value());
    }

    void anEmptyHostTokenNeverReachesTheRelay() {
        LoopbackSignaling relay;
        DataChannelRoomHostEndpoint host(relay.origin(), RoomIdentity{}, local_only);
        Outcome at_host;
        host.open(
            inviteFor(host_id_), [&](std::shared_ptr<RoomLink>) { ++at_host.links; },
            [&](RoomLinkFailure failure) { at_host.failure = failure; });
        QTRY_VERIFY_WITH_TIMEOUT(at_host.failure.has_value(), connect_ms);
        QCOMPARE(*at_host.failure, RoomLinkFailure::Unreachable);
        QTest::qWait(300);
        QVERIFY(relay.host_tokens.isEmpty());
    }

    void aSecondHostWithAnotherTokenIsRefused() {
        LoopbackSignaling relay;
        const auto invite = inviteFor(host_id_);
        DataChannelRoomHostEndpoint first(relay.origin(), host_id_, local_only);
        DataChannelRoomHostEndpoint second(relay.origin(), other_id_, local_only);
        Outcome at_first;
        Outcome at_second;
        openHost(first, invite, at_first, relay);
        second.open(
            invite, [&](std::shared_ptr<RoomLink>) { ++at_second.links; },
            [&](RoomLinkFailure failure) { at_second.failure = failure; });
        QTRY_VERIFY_WITH_TIMEOUT(at_second.failure.has_value(), connect_ms);
        QCOMPARE(*at_second.failure, RoomLinkFailure::Refused); // 409: the room belongs to another token
        QVERIFY(!at_first.failure);
    }

    void closingAndDestroyingFromInsideCallbacksIsSafe() {
        LoopbackSignaling relay;
        const auto invite = inviteFor(host_id_);
        DataChannelRoomHostEndpoint host(relay.origin(), host_id_, local_only);
        auto guest = std::make_unique<DataChannelRoomGuestConnector>(relay.origin(), guest_id_, local_only);
        Outcome at_host;
        std::shared_ptr<RoomLink> guest_link;
        std::optional<RoomLinkFailure> guest_failure;
        openHost(host, invite, at_host, relay);
        guest->connect(
            invite,
            [&](std::shared_ptr<RoomLink> link) {
                guest_link = std::move(link);
                guest.reset(); // destroy the connector inside its own callback
            },
            [&](RoomLinkFailure failure) { guest_failure = failure; });
        QTRY_VERIFY_WITH_TIMEOUT(at_host.link && guest_link, connect_ms);
        QVERIFY(!guest);
        std::optional<RoomLinkFailure> guest_closed;
        guest_link->onClosed = [&](RoomLinkFailure failure) { guest_closed = failure; };
        int host_controls{};
        at_host.link->onControl = [&](RoomControlMessage) {
            ++host_controls;
            at_host.link->close();
            at_host.link.reset(); // drop the last reference inside the link's own callback
        };
        QVERIFY(guest_link->sendControl(RoomClockPing{1}));
        QVERIFY(guest_link->sendControl(RoomClockPing{2}));
        QTRY_VERIFY_WITH_TIMEOUT(guest_closed.has_value(), 10000);
        QCOMPARE(*guest_closed, RoomLinkFailure::Closed);
        QCOMPARE(host_controls, 1);
        QVERIFY(!guest_failure);
    }
};

QTEST_GUILESS_MAIN(DataChannelRoomTransportTests)
#include "DataChannelRoomTransportTests.moc"
