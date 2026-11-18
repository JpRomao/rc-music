#pragma once

#include "m0/domain/RoomInviteLink.hpp"
#include "m0/domain/RoomMessages.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace music_player::m0 {

// Transport port of the listening room (ADR-0022). Qt-free: the libdatachannel adapter (S6.3)
// implements it inside rc_music_listening_room; tests/support/FakeRoomTransport.hpp implements it
// in memory. Adapters deliver every callback on the thread that owns the endpoint/connector.

enum class RoomLinkFailure : std::uint8_t {
    SignalingUnavailable,
    HostOffline,
    Unreachable,
    Refused,
    VersionMismatch,
    Closed
};

// One admitted or pending peer connection. The adapter runs the S4 codec (RoomProtocolCodec) and
// silently drops input that does not decode; only valid messages reach onControl/onMedia.
class RoomLink {
  public:
    virtual ~RoomLink() = default;
    // Lowercase hex SHA-256 of the remote DTLS certificate (the peer's room identity).
    [[nodiscard]] virtual std::string peerFingerprint() const = 0;
    // false when the link is closed or the message cannot be queued.
    virtual bool sendControl(const RoomControlMessage& message) = 0;
    virtual bool sendMedia(const RoomFileChunk& chunk) = 0;
    // Bytes queued on the media channel and not yet handed to the network (backpressure).
    [[nodiscard]] virtual std::size_t mediaBufferedBytes() const = 0;
    // Closing locally does not call onClosed on this side.
    virtual void close() = 0;

    std::function<void(RoomControlMessage)> onControl;
    std::function<void(RoomFileChunk)> onMedia;
    std::function<void(RoomLinkFailure)> onClosed;
};

class RoomHostEndpoint {
  public:
    virtual ~RoomHostEndpoint() = default;
    // Registers the room with the signaling relay and reports every incoming guest link.
    // on_failure ends the endpoint (for example SignalingUnavailable); links already handed
    // out keep their own onClosed.
    virtual void open(const RoomInviteLink& invite, std::function<void(std::shared_ptr<RoomLink>)> on_incoming,
                      std::function<void(RoomLinkFailure)> on_failure) = 0;
    virtual void closeAll() = 0;
};

class RoomGuestConnector {
  public:
    virtual ~RoomGuestConnector() = default;
    // Exactly one of the callbacks is called once, unless cancel() comes first. The link is
    // connected only when the host's certificate matches invite.host_fingerprint.
    virtual void connect(const RoomInviteLink& invite, std::function<void(std::shared_ptr<RoomLink>)> on_connected,
                         std::function<void(RoomLinkFailure)> on_failure) = 0;
    virtual void cancel() = 0;
};

} // namespace music_player::m0
