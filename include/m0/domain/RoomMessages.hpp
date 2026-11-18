#pragma once

#include "m0/domain/ListenerProfile.hpp"
#include "m0/domain/ListeningRoom.hpp"
#include "m0/domain/RoomRoster.hpp"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace music_player::m0 {

// Control messages of the listening room (ADR-0022). Pure data: the wire format and its validation
// live in the adapter (src/adapters/RoomProtocolCodec.hpp).

// The avatar travels as an AvatarChoice: a bundled avatar has no hash, so a sha256 alone cannot name it.
struct RoomHello final {
    std::uint32_t protocol_version{};
    std::string nickname{};
    AvatarChoice avatar{};
    bool operator==(const RoomHello&) const = default;
};

struct RoomJoinResult final {
    JoinDecision decision{};
    bool operator==(const RoomJoinResult&) const = default;
};

struct RoomStateMessage final {
    RoomSnapshot snapshot{};
    RoomPermissionSet my_permissions{};
    bool operator==(const RoomStateMessage&) const = default;
};

struct RoomIntentMessage final {
    std::uint64_t request_id{};
    RoomIntent intent{};
    bool operator==(const RoomIntentMessage&) const = default;
};

struct RoomIntentRejected final {
    std::uint64_t request_id{};
    IntentRejection reason{};
    bool operator==(const RoomIntentRejected&) const = default;
};

struct RoomClockPing final {
    std::int64_t sent_local_ms{};
    bool operator==(const RoomClockPing&) const = default;
};

struct RoomClockPong final {
    std::int64_t sent_local_ms{};
    std::int64_t host_ms{};
    bool operator==(const RoomClockPong&) const = default;
};

struct RoomCatalogEntry final {
    std::uint64_t id{};
    RoomTrackSummary summary{};
    bool operator==(const RoomCatalogEntry&) const = default;
};

struct RoomCatalogPlaylist final {
    std::uint64_t id{};
    std::string name{};
    std::vector<RoomCatalogEntry> entries{};
    bool operator==(const RoomCatalogPlaylist&) const = default;
};

struct RoomCatalog final {
    std::vector<RoomCatalogPlaylist> playlists{};
    bool operator==(const RoomCatalog&) const = default;
};

struct RoomFileRequest final {
    std::string content_sha256{};
    std::uint64_t offset{};
    bool operator==(const RoomFileRequest&) const = default;
};

struct RoomFileOffer final {
    std::string content_sha256{};
    std::uint64_t size_bytes{};
    std::uint32_t transfer_id{};
    bool operator==(const RoomFileOffer&) const = default;
};

struct RoomLyrics final {
    std::uint64_t item_id{};
    std::string lrc_text{};
    bool operator==(const RoomLyrics&) const = default;
};

enum class RoomByeReason : std::uint8_t { Closed, Revoked, Kicked, VersionMismatch, Full };

struct RoomBye final {
    RoomByeReason reason{};
    bool operator==(const RoomBye&) const = default;
};

// Used by S9.2.
struct RoomDriftReport final {
    std::int64_t drift_ms{};
    bool operator==(const RoomDriftReport&) const = default;
};

using RoomControlMessage =
    std::variant<RoomHello, RoomJoinResult, RoomStateMessage, RoomIntentMessage, RoomIntentRejected, RoomClockPing,
                 RoomClockPong, RoomCatalog, RoomFileRequest, RoomFileOffer, RoomLyrics, RoomBye, RoomDriftReport>;

struct RoomFileChunk final {
    std::uint32_t transfer_id{};
    std::uint64_t offset{};
    std::vector<std::uint8_t> payload{};
    bool operator==(const RoomFileChunk&) const = default;
};

} // namespace music_player::m0
