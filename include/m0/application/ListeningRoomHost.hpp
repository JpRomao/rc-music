#pragma once

#include "m0/domain/ListenerProfile.hpp"
#include "m0/domain/ListeningRoom.hpp"
#include "m0/domain/ListeningRoomLimits.hpp"
#include "m0/domain/RoomInviteLink.hpp"
#include "m0/domain/RoomMessages.hpp"
#include "m0/domain/RoomRoster.hpp"
#include "m0/domain/SessionSnapshot.hpp"
#include "m0/ports/RoomHostPlayer.hpp"
#include "m0/ports/RoomRosterStore.hpp"
#include "m0/ports/RoomTrackCatalog.hpp"
#include "m0/ports/RoomTransport.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace music_player::m0 {

struct RoomOpenOptions final {
    std::size_t max_listeners{default_room_listeners};
    bool until_revoked{false};
    std::uint64_t default_role_id{1};
};

struct ListeningRoomHostDependencies final {
    RoomRosterStore& roster_store;
    RoomHostPlayer& player;
    RoomHostEndpoint& endpoint;
    RoomTrackCatalog& catalog;
    std::function<std::int64_t()> host_clock_ms;
    // Fills room ids and invite secrets. Production passes a CSPRNG.
    std::function<void(std::span<std::uint8_t>)> random_bytes;
    // SHA-256 of the host's own DTLS certificate (RoomIdentity::fingerprint).
    std::array<std::uint8_t, 32> host_fingerprint{};
    ListenerProfile host_profile{};
};

// Host side of the listening room (ADR-0022, S7.1): ties ListeningRoom/RoomRoster, the roster store,
// the local player (RoomHostPlayer, see that port for why it is not PlayerController directly), the
// track catalog and the transport together. Qt-free; every call and callback runs on one thread.
//
// Network: nothing is opened by construction, not even with a persisted invite; only open() calls
// RoomHostEndpoint::open, and close()/revoke() call closeAll().
//
// Links and members:
//   - A member's identity is link->peerFingerprint() (DTLS-verified), never a value from a message.
//     A link whose peer fingerprint is empty or equals the host's own is closed: the host is never
//     passed to ListeningRoom::requestJoin. The host appears as the first entry of snapshot().members
//     (state Admitted) so guests can show its nickname and avatar.
//   - The first message on a link must be RoomHello; anything else first closes that link. A hello
//     with another protocol_version gets RoomBye{VersionMismatch} and the link is closed; a hello
//     whose nickname is not canonical or whose avatar is invalid closes the link.
//   - Admitted -> JoinResult{Admitted} then the state (and the current item's lyrics). Pending ->
//     JoinResult{Pending} only, plus onJoinRequest; nothing else is sent to a pending member, and
//     anything it sends after the hello is ignored. Full/Blocked -> that JoinResult, then close.
//     reject() answers JoinResult{RejectedClosed} and closes.
//   - One live link per identity: a newer link that says hello replaces the older one (closed
//     locally). A link dropped by the network makes its member leave the room; a known person who
//     comes back is admitted again by the roster.
//   - kick(fp, block) sends RoomBye{Kicked} and closes; blocking someone present is always
//     kick(fp, true), never a bare roster change.
//
// Intents: every RoomIntentMessage goes through ListeningRoom::authorize against the current
// snapshot (requiredPermission alone is never the gate). NotAdmitted is dropped silently (pending
// people get nothing); any other rejection answers RoomIntentRejected{request_id, reason}. Authorized
// intents call the player: Next/Previous/Seek/Pause/Resume, RemoveItem and MoveItem (destination is
// an index of the room queue). Pause/Resume already in that state succeed without a command; a
// command the player refuses answers InvalidArgument.
//   - AddItems with catalog ids and AddPlaylist answer UnknownItem until the room catalog exists
//     (S11.2).
//   - AddItems with an uploaded item answers InvalidArgument until the guest-file transfer exists
//     (S8/S12). That transfer will call addGuestTrack(), which overwrites the item's id,
//     owner_fingerprint (the link fingerprint) and added_by before using it.
// There is no guest intent for shuffle, repeat or the end of the queue: they stay with the host's
// player and reach the room only as the queue order and current item.
//
// State: snapshot().version grows by one each time the members, the room queue or the playback
// change, and only then RoomStateMessage goes to every admitted member with ITS OWN effective
// permissions (RoomRoster::effectivePermissions). The room queue maps the player's queue: item id =
// the queue occurrence id; HostTrack items carry the catalog's hash and summary, owner = the host
// fingerprint (lowercase hex) and added_by = the host nickname; an item appears once the catalog
// described it (current item first) and a file the catalog cannot read stays out of the room. At
// most maximum_room_queue_items items, a window that starts at the current item when the local
// queue is longer. Playback: current_item, playing, and the anchor (host_clock_ms(), position) taken
// whenever the playback changes in a way the previous anchor does not predict (pause, seek, new
// item). When the current item changes, its lyrics (if any) go to every admitted member as
// RoomLyrics. A message the link refuses (the adapter drops what encodeRoomControl rejects, e.g. an
// oversized state) is skipped: that member keeps its older version until a later state fits.
//
// Guest items (GuestTrack) never reach the session: the session autosave must store
// withoutGuestItems(snapshot) while a room is open, and close()/revoke() remove them from the queue.
//
// Endpoint failure: onEndpointFailed is called and links already connected keep working; the UI
// decides whether to close the room.
class ListeningRoomHost final {
  public:
    explicit ListeningRoomHost(ListeningRoomHostDependencies dependencies);
    ~ListeningRoomHost();
    ListeningRoomHost(const ListeningRoomHost&) = delete;
    ListeningRoomHost& operator=(const ListeningRoomHost&) = delete;

    // Reuses the persisted invite when until_revoked and present; until_revoked persists the invite
    // (room id, secret, max_listeners, default role). Opening without until_revoked leaves a persisted
    // invite untouched. Already open: returns the current invite.
    [[nodiscard]] std::optional<RoomInviteLink> open(const RoomOpenOptions& options);
    // "Encerrar sala": RoomBye{Closed} to everyone, keeps the persisted invite.
    void close();
    // Closes with RoomBye{Revoked}, forgets the persisted invite and drops everyone.
    void revoke();
    [[nodiscard]] bool approve(std::string_view fingerprint);
    [[nodiscard]] bool reject(std::string_view fingerprint);
    [[nodiscard]] bool kick(std::string_view fingerprint, bool block);
    [[nodiscard]] RoomSnapshot snapshot() const;
    [[nodiscard]] bool isOpen() const noexcept;

    // For the guest-file transfer (S8/S12): a guest's file already in the host's room cache joins the
    // queue as a GuestTrack. Overwrites id, origin, owner_fingerprint (= owner_fingerprint) and
    // added_by (the owner's nickname). nullopt when the room is closed, the owner is not admitted,
    // the room queue is full or the player refuses the file.
    [[nodiscard]] std::optional<std::uint64_t>
    addGuestTrack(std::string_view owner_fingerprint, const std::filesystem::path& cached_file, RoomItem described);
    // What session autosave may store while a room is open: the queue without guest items.
    [[nodiscard]] SessionSnapshot withoutGuestItems(SessionSnapshot session) const;

    std::function<void(const RoomSnapshot&)> onChanged;
    std::function<void(const RoomMember&)> onJoinRequest;
    std::function<void(RoomLinkFailure)> onEndpointFailed;

  private:
    struct Alive;
    struct Connection;

    void attach(const std::shared_ptr<RoomLink>& link);
    void onIncoming(std::uint64_t generation, const std::shared_ptr<RoomLink>& link);
    void onEndpointFailure(std::uint64_t generation, RoomLinkFailure failure);
    void onLinkControl(const std::shared_ptr<RoomLink>& link, RoomControlMessage message);
    void onLinkClosed(const std::shared_ptr<RoomLink>& link);
    void onHello(const std::shared_ptr<RoomLink>& link, const RoomHello& hello);
    void onIntent(const std::shared_ptr<RoomLink>& link, const RoomIntentMessage& message);
    [[nodiscard]] std::optional<IntentRejection> apply(const RoomIntent& intent);
    void onDescribed(std::uint64_t generation, const std::filesystem::path& source,
                     std::optional<RoomTrackDescription> description);
    void requestDescription(const std::filesystem::path& source);
    [[nodiscard]] std::optional<RoomItem> roomItem(const LocalFileQueueSnapshotItem& entry) const;
    bool refresh();
    bool rebuild();
    void admitted(const std::shared_ptr<RoomLink>& link);
    void sendState(const Connection& connection) const;
    void sendLyrics(Connection& connection) const;
    void drop(const std::shared_ptr<RoomLink>& link);
    void end(RoomByeReason reason);
    void saveRoster();
    [[nodiscard]] Connection* find(const RoomLink* link);
    [[nodiscard]] Connection* findMember(std::string_view fingerprint);
    [[nodiscard]] std::optional<MemberState> memberState(std::string_view fingerprint) const;
    [[nodiscard]] RoomMember hostMember() const;

    RoomRosterStore& roster_store_;
    RoomHostPlayer& player_;
    RoomHostEndpoint& endpoint_;
    RoomTrackCatalog& catalog_;
    std::function<std::int64_t()> host_clock_ms_;
    std::function<void(std::span<std::uint8_t>)> random_bytes_;
    std::array<std::uint8_t, 32> host_fingerprint_;
    std::string host_fingerprint_hex_;
    ListenerProfile host_profile_;

    RoomRoster roster_;
    bool roster_writable_{false};
    std::optional<ListeningRoom> room_;
    std::optional<RoomInviteLink> invite_;
    std::uint64_t generation_{0};
    std::vector<Connection> connections_;
    RoomSnapshot current_;
    std::map<std::filesystem::path, RoomTrackDescription> descriptions_;
    std::set<std::filesystem::path> describing_;
    std::set<std::filesystem::path> unreadable_;
    std::map<std::uint64_t, RoomItem> guest_items_;
    bool refreshing_{false};
    bool refresh_again_{false};
    std::shared_ptr<Alive> alive_;
};

} // namespace music_player::m0
