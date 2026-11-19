#pragma once

#include "m0/domain/ListenerProfile.hpp"
#include "m0/domain/ListeningRoom.hpp"
#include "m0/domain/ListeningRoomLimits.hpp"
#include "m0/domain/RoomInviteLink.hpp"
#include "m0/domain/RoomMessages.hpp"
#include "m0/domain/RoomRoster.hpp"
#include "m0/domain/SessionSnapshot.hpp"
#include "m0/ports/RoomFiles.hpp"
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
    // Runs task once after delay_ms on the host's thread (QTimer::singleShot in the app).
    std::function<void(std::int64_t delay_ms, std::function<void()> task)> schedule;
    // Streams the files guests ask for (S8.2); nullptr: file requests are ignored.
    RoomFileOutbox* files{nullptr};
};

// Upper bound of the encoded size of a RoomStateMessage (RoomProtocolCodec JSON): every text byte
// counted as its worst JSON escape. The host keeps every state it sends within
// maximum_room_control_message_bytes by this estimate.
[[nodiscard]] std::size_t estimatedRoomStateBytes(const RoomStateMessage& message);

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
//   - A link that sends no hello within room_hello_timeout_ms is closed.
//   - One live link per identity: a newer link that says hello replaces the older one (closed
//     locally). When the link of an ADMITTED member drops, the member is held for
//     room_reconnect_window_ms: same seat, no version bump; a new link with the same fingerprint that
//     says hello inside the window resumes it (JoinResult{Admitted} + the current state, version
//     unchanged). After the window the member leaves (version bumps). A pending person who drops
//     leaves at once. A RoomBye from a guest (it left) is an immediate leave: the link is closed,
//     there is no reconnect hold, and the version bumps.
//   - kick(fp, block) sends RoomBye{Kicked} and closes; blocking someone present is always
//     kick(fp, true), never a bare roster change.
//
// Intents: every RoomIntentMessage goes through ListeningRoom::authorize against the current
// snapshot (requiredPermission alone is never the gate). NotAdmitted is dropped silently (pending
// people get nothing); any other rejection answers RoomIntentRejected{request_id, reason} and changes
// nothing (no refresh, no broadcast: a refused guest cannot make the host send states). Authorized
// intents call the player: Next/Previous/Seek/Pause/Resume, RemoveItem and MoveItem (destination is
// an index of the room queue). Pause/Resume already in that state succeed without a command; a
// command the player refuses answers InvalidArgument.
//   - AddItems with catalog ids and AddPlaylist answer UnknownItem until the room catalog exists
//     (S11.2).
//   - AddItems with an uploaded item answers InvalidArgument until the guest-file transfer exists
//     (S8/S12). That transfer will call addGuestTrack(), which overwrites the item's id,
//     owner_fingerprint (the link fingerprint) and added_by before using it.
// There is no guest intent for shuffle, repeat or the end of the queue: they stay with the host's
// player and reach the room only as the queue order (the play order, see RoomHostPlayer) and current
// item.
//
// State: snapshot() is the host's view; its version grows by one each time the members, the room
// queue or the playback change. Guests get guestView = the same snapshot with ADMITTED members only
// (pending people stay visible to the host, via snapshot() and onJoinRequest), and only when that view
// changed: RoomStateMessage goes to every admitted member with ITS OWN effective permissions
// (RoomRoster::effectivePermissions); a guest may see version gaps.
//   - Room queue: from the current item forward (the play order), item id = the queue occurrence id.
//     HostTrack items carry the catalog's hash and summary, owner = the host fingerprint (lowercase
//     hex) and added_by = the host nickname; an item appears once the catalog described it and a file
//     the catalog cannot read stays out. Title, artist and album are cut to
//     maximum_room_summary_text_bytes at a code point boundary (also for guest items). The window holds
//     at most maximum_room_queue_items items and only as many as keep estimatedRoomStateBytes of the
//     guests' state within maximum_room_control_message_bytes, so every state the host sends is
//     encodable. A message a link still refuses is skipped: that member keeps its older version.
//   - Playback: current_item, playing and the anchor (host_clock_ms(), position). Item changes,
//     play/pause and seek intents always re-anchor; otherwise the anchor stays while the player is
//     within room_anchor_tolerance_ms of expectedPositionMs(anchor, now), so position jitter neither
//     bumps the version nor broadcasts. (A seek made in the host's own UI that moves less than the
//     tolerance is indistinguishable from jitter and keeps the anchor.)
//   - When the current item changes, its lyrics (if any) go once to every admitted member as RoomLyrics,
//     and to a member admitted mid-song. Lyrics above maximum_room_lyrics_bytes are never sent.
//
// Files (S8.2): transfers are request-driven. The file window is the first 1 + room_prefetch_items
// items of the room queue (the current item, then the next ones in queue order). An admitted member's
// RoomFileRequest{sha, offset} is served only when sha belongs to a window item the host has a file for
// (a host track, or a guest item in the host's cache); anything else is dropped. Requests are collected
// and served on the next scheduler turn in window order (the current item first), each with a new
// transfer_id, through RoomFileOutbox::send from the request's offset (resume). A second request for a
// sha replaces the first. When the window changes, transfers whose item left it are cancelled; a link
// that goes is forgotten by the outbox.
//
// Sync difference (S9.2): an admitted member's RoomDriftReport{drift_ms} replaces its last value
// (memberDrifts(), keyed by fingerprint, nothing persisted). Reports from pending or unknown links and
// values beyond +-room_drift_report_limit_ms are ignored. A member's value goes away when its link
// drops (even while its seat is held), when it leaves or is kicked, and when the room closes.
// onDriftsChanged is called when the map changes. It never touches the room state or its version.
//
// Guest items (GuestTrack) never reach the session: the session autosave must store
// withoutGuestItems(snapshot) while a room is open, and close(), revoke() and the destructor remove
// them from the player's queue.
//
// Endpoint failure: onEndpointFailed is called and links already connected keep working; the UI
// decides whether to close the room.
class ListeningRoomHost final {
  public:
    explicit ListeningRoomHost(ListeningRoomHostDependencies dependencies);
    ~ListeningRoomHost();
    ListeningRoomHost(const ListeningRoomHost&) = delete;
    ListeningRoomHost& operator=(const ListeningRoomHost&) = delete;

    // until_revoked with a persisted invite reopens it with ITS settings (max_listeners, default role),
    // ignoring the options; without one, it creates and persists a new invite with the options.
    // Opening without until_revoked leaves a persisted invite untouched. Already open: returns the
    // current invite.
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
    // The invite kept for "until revoked" rooms, with its settings (for the UI to show).
    [[nodiscard]] std::optional<PersistedInvite> persistedInvite() const;

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
    std::function<void()> onDriftsChanged;
    // The latest reported sync difference (ms, positive = the guest is ahead) of each admitted member.
    [[nodiscard]] const std::map<std::string, std::int64_t, std::less<>>& memberDrifts() const noexcept;

  private:
    struct Alive;
    struct Connection;

    void attach(const std::shared_ptr<RoomLink>& link);
    void onIncoming(std::uint64_t generation, const std::shared_ptr<RoomLink>& link);
    void onEndpointFailure(std::uint64_t generation, RoomLinkFailure failure);
    void onLinkControl(const std::shared_ptr<RoomLink>& link, RoomControlMessage message);
    void onLinkClosed(const std::shared_ptr<RoomLink>& link);
    void onHelloTimeout(std::uint64_t generation, const std::shared_ptr<RoomLink>& link);
    void onReconnectExpired(std::uint64_t generation, const std::string& fingerprint, std::uint64_t token);
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
    struct WindowFile;
    [[nodiscard]] std::vector<WindowFile> fileWindow() const;
    void onFileRequest(Connection& connection, const RoomFileRequest& request);
    void serveFiles();
    void cancelFilesOutsideWindow();
    void forgetFiles(const RoomLink& link);
    void drop(const std::shared_ptr<RoomLink>& link);
    void forgetDrift(std::string_view fingerprint);
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
    std::function<void(std::int64_t, std::function<void()>)> schedule_;
    RoomFileOutbox* files_;

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
    // The cached file of each guest item, to serve it like a host track.
    std::map<std::uint64_t, std::filesystem::path> guest_files_;
    std::uint32_t next_transfer_id_{0};
    bool serve_scheduled_{false};
    // Admitted members whose link dropped, held for room_reconnect_window_ms (value: hold token).
    std::map<std::string, std::uint64_t, std::less<>> held_;
    std::uint64_t next_hold_{0};
    std::map<std::string, std::int64_t, std::less<>> drifts_;
    // The last state guests received (admitted members only), to broadcast only what they would see.
    RoomSnapshot sent_view_;
    bool reanchor_{false};
    bool refreshing_{false};
    bool refresh_again_{false};
    std::shared_ptr<Alive> alive_;
};

} // namespace music_player::m0
