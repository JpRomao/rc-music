#pragma once

#include "adapters/WindowsRoomIdentity.hpp"
#include "m0/ports/RoomTransport.hpp"

#include <QString>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace music_player::m0 {

// The listening room transport on libdatachannel (ADR-0022, S6.3). This header is rtc-free (pimpl):
// only DataChannelRoomTransport.cpp, inside rc_music_listening_room, includes libdatachannel.
//
// Signaling (S5 relay, see docs/room-signaling.md): the guest opens GET /rooms/<room_id>/guest, the
// host GET /rooms/<room_id>/host with X-Room-Host-Token = roomHostToken(identity, room_id) (an empty
// token fails with Unreachable and never reaches the network). Each relay message carries
// data = base64(seal(JSON)) with the invite's WindowsRoomEnvelopeCipher, so the relay sees no SDP,
// candidate, fingerprint or address. The JSON is
//   {"room_protocol_version":<u32>,"kind":"offer"|"answer"|"reject","sdp":<text>}
// ("reject" has no sdp). No ICE trickle: each side waits for ICE gathering to complete and sends ONE
// sealed description (the relay drops above 20 messages/s and 16 KiB per message). A side that
// receives another room_protocol_version fails with VersionMismatch (the host answers it with a
// "reject" carrying its own version and drops the offer).
//
// The guest offers and creates the data channels "control" and "media" (both reliable and
// ordered). Before setRemoteDescription it requires every a=fingerprint of the host's answer to be
// sha-256 of invite.host_fingerprint, otherwise it fails with Refused; DTLS then makes libdatachannel
// check the host certificate against that SDP fingerprint, and the guest checks the DTLS fingerprint
// once more before reporting the link. peerFingerprint() is lowercase hex without colons.
//
// Failures: relay unreachable, an unexpected HTTP status or the relay dropping the socket ->
// SignalingUnavailable; {"event":"host-offline"} before the link -> HostOffline; 409 for the host
// (another host token owns the room), close 4001 (replaced by the same token elsewhere), 429 for a
// guest (room full) or a wrong host fingerprint -> Refused; local identity unusable (no host token,
// PEM paths Mbed TLS cannot open, bad ICE server text), ICE/DTLS failure or no answer within
// room_connect_timeout_ms -> Unreachable.
//
// Identity: the PEM files go to libdatachannel by path, and Mbed TLS opens them with ANSI fopen. A
// path is passed as is when it is ASCII, else as its 8.3 short form when that is ASCII, else in the
// ANSI code page when it converts exactly; otherwise the operation fails with Unreachable.
//
// Threads and lifetime: construct, use and destroy every object here on one thread that runs a Qt
// event loop; every callback (including RoomLink's) arrives on that thread through
// QMetaObject::invokeMethod(..., Qt::QueuedConnection). libdatachannel callbacks never hold a strong
// reference to an endpoint, connector or link: they only post to a mailbox that the owner disarms
// before tearing down, so a callback can never touch a destroyed object, and closing or destroying
// anything from inside a callback is safe. libdatachannel logging is off (rtc::InitLogger(None)):
// nothing here logs SDP, candidates, addresses or secrets.
[[nodiscard]] std::vector<std::string> defaultRoomIceServers(); // {"stun:stun.cloudflare.com:3478"}

// The narrow path Mbed TLS's fopen can open for `path` (the fallbacks under "Identity" above), or
// nullopt when there is none.
[[nodiscard]] std::optional<std::string> mbedTlsFilePath(const QString& path);

class DataChannelRoomHostEndpoint final : public RoomHostEndpoint {
  public:
    // signaling_origin: "https://<worker>" (or "http://127.0.0.1:<port>" for a loopback relay).
    DataChannelRoomHostEndpoint(QString signaling_origin, RoomIdentity identity,
                                std::vector<std::string> ice_servers = defaultRoomIceServers());
    ~DataChannelRoomHostEndpoint() override;
    DataChannelRoomHostEndpoint(const DataChannelRoomHostEndpoint&) = delete;
    DataChannelRoomHostEndpoint& operator=(const DataChannelRoomHostEndpoint&) = delete;

    // A second open() first does closeAll(). Links already handed out are closed by closeAll() and
    // by the destructor (locally: their onClosed is not called).
    void open(const RoomInviteLink& invite, std::function<void(std::shared_ptr<RoomLink>)> on_incoming,
              std::function<void(RoomLinkFailure)> on_failure) override;
    void closeAll() override;

    struct State;

  private:
    std::shared_ptr<State> state_;
};

class DataChannelRoomGuestConnector final : public RoomGuestConnector {
  public:
    DataChannelRoomGuestConnector(QString signaling_origin, RoomIdentity identity,
                                  std::vector<std::string> ice_servers = defaultRoomIceServers());
    ~DataChannelRoomGuestConnector() override;
    DataChannelRoomGuestConnector(const DataChannelRoomGuestConnector&) = delete;
    DataChannelRoomGuestConnector& operator=(const DataChannelRoomGuestConnector&) = delete;

    // A second connect() first cancels the previous attempt. The link handed to on_connected is
    // independent of the connector: destroying the connector afterwards does not close it.
    void connect(const RoomInviteLink& invite, std::function<void(std::shared_ptr<RoomLink>)> on_connected,
                 std::function<void(RoomLinkFailure)> on_failure) override;
    void cancel() override;

    struct State;

  private:
    std::shared_ptr<State> state_;
};

// How long a guest waits for the host's answer and the link, and the host for a guest's link.
inline constexpr int room_connect_timeout_ms = 30000;

} // namespace music_player::m0
