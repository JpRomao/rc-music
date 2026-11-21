#pragma once

#include "m0/domain/ListenerProfile.hpp"
#include "m0/domain/ListeningRoom.hpp"
#include "m0/domain/ListeningRoomLimits.hpp"
#include "m0/domain/PlaylistLibrary.hpp"
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
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace music_player::m0 {

enum class RoleRemoval : std::uint8_t { Removed, InitialRole, Refused };

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
    // S12.1, guest uploads: the host's own upload cache (a receiver limited to maximum_guest_upload_bytes,
    // emptied when the room ends) and the check of what arrives. Either nullptr: uploads answer
    // InvalidArgument.
    RoomFileInbox* uploads{nullptr};
    RoomUploadProbe* upload_probe{nullptr};
};

// Upper bound of the encoded size of a RoomStateMessage (RoomProtocolCodec JSON): every text byte
// counted as its worst JSON escape. The host keeps every state it sends within
// maximum_room_control_message_bytes by this estimate.
[[nodiscard]] std::size_t estimatedRoomStateBytes(const RoomStateMessage& message);
// The same bound for a RoomCatalog: the host keeps its catalog within maximum_room_control_message_bytes.
[[nodiscard]] std::size_t estimatedRoomCatalogBytes(const RoomCatalog& catalog);

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
//     kick(fp, true), never a bare roster change. Without block the person is forgotten (role and
//     overrides too), so coming back is a new request for approval. Someone kicked while held (link
//     down) gets RoomBye{Kicked} if they say hello again within room_reconnect_window_ms.
//
// Intents: every RoomIntentMessage goes through ListeningRoom::authorize against the current
// snapshot (requiredPermission alone is never the gate). NotAdmitted is dropped silently (pending
// people get nothing); any other rejection answers RoomIntentRejected{request_id, reason} and changes
// nothing (no refresh, no broadcast: a refused guest cannot make the host send states). Authorized
// intents call the player: Next/Previous/Seek/Pause/Resume, RemoveItem and MoveItem (destination is
// an index of the room queue). Pause/Resume already in that state succeed without a command; a
// command the player refuses answers InvalidArgument.
//   - AddItems with catalog ids and AddPlaylist (S11.2) take tracks from the room catalog (below): an id that
//     is not in the catalog guests have now (a private list, an entry that left, a closed room) answers
//     UnknownItem and adds nothing. AddPlaylist adds every catalog entry of that playlist, in order;
//     past maximum_room_queue_items in the room queue it answers QueueFull (AddItems: checked by authorize),
//     and an empty playlist answers InvalidArgument. The tracks are appended to the host's queue as host
//     tracks, all or none; while the room is open their RoomItem says added_by = the nickname of the guest
//     who asked (owner stays the host: the file is the host's).
//   - AddItems with an uploaded item (S12.1, see Guest uploads) starts an upload: only its hash and size are
//     used; nothing else in the item is trusted.
// There is no guest intent for shuffle, repeat or the end of the queue: they stay with the host's
// player and reach the room only as the queue order (the play order, see RoomHostPlayer) and current
// item.
//
// State: snapshot() is the host's view; its version grows by one each time the members, the room
// queue or the playback change, and each time the host changes roles or a person's role, overrides
// (S10.1), so every admitted member gets its new permissions in that state. Guests get guestView = the
// same snapshot with ADMITTED members only (pending people stay visible to the host, via snapshot() and
// onJoinRequest), and only when that view changed (or the roster did): RoomStateMessage goes to every
// admitted member with ITS OWN effective permissions (RoomRoster::effectivePermissions); a guest may see
// version gaps.
//   - Room queue: from the current item forward (the play order), item id = the queue occurrence id.
//     HostTrack items carry the catalog's hash and summary, owner = the host fingerprint (lowercase
//     hex) and added_by = the host nickname (or the guest who added it from the catalog, see AddItems);
//     an item appears once the catalog described it and a file
//     the catalog cannot read stays out. Title, artist and album are cut to
//     maximum_room_summary_text_bytes at a code point boundary (also for guest items). The window holds
//     at most maximum_room_queue_items items and only as many as keep estimatedRoomStateBytes of the
//     guests' state within maximum_room_control_message_bytes, so every state the host sends is
//     encodable. A message a link still refuses is skipped: that member keeps its older version.
//   - Playback: current_item, playing and the anchor (host_clock_ms(), position), from
//     RoomHostPlayer::state(), which already counts what the player was told to do, so a confirmation
//     still on its way is never broadcast as a change. Item changes, play/pause and seeks (any accepted
//     seek, from a guest or the host's own UI) always re-anchor; otherwise the anchor stays while the
//     player is within room_anchor_tolerance_ms of expectedPositionMs(anchor, now) (before a future
//     anchor: of its position), so position jitter neither bumps the version nor broadcasts.
//   - Starts lead: with at least one admitted member, a start (playing again, another item while
//     playing, a seek while playing) is anchored room_play_lead_ms ahead, at the start position, and
//     RoomHostPlayer::holdStart keeps the host's player silent (its controls still showing playing)
//     until releaseStart at that instant, so guests begin with the host. Exceptions start at once,
//     anchored now: no admitted member; an item first described while already current (the host was
//     playing it); a player that refuses the hold; and a re-anchor for drift, which is no start. Another
//     start during the wait takes a new anchor; a pause ends the wait paused; closing the room, or the
//     last admitted member leaving, releases the player at once; a player that refuses to start at the
//     anchor reads as paused.
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
// Catalog (S11.2): setPlaylists() gives the host the playlist library (any time; kept across rooms). While
// a room is open, the catalog holds only the playlists marked PlaylistVisibility::Room, in library order:
// each with its name (cut like a summary text) and, for each entry the track catalog described, the
// track's capped summary. Nothing else: no path, file name, hash or library id. Playlist and entry ids are
// the room's own (a counter from 1 per room, never derived from a path or the library), stable while
// the room is open and the entry stays. Files of private lists are never read; a list's entries appear
// once described, unreadable files stay out, and the catalog (only as many lists and entries as keep
// estimatedRoomCatalogBytes within maximum_room_control_message_bytes) asks the track catalog for a few
// files at a time and stops once it is full; as each description arrives it asks for the next file,
// without rebuilding the catalog. Every admitted member gets the catalog after its state on admission, and
// again each time it changes: right away when the library changes (a list made private or visible,
// renamed, edited), and at most once per room_catalog_refresh_ms while descriptions arrive (one scheduled
// refresh sends everything described meanwhile). Pending people never get it.
//
// Guest uploads (S12.1): the file goes up to the host first, through the same request-driven transfer with
// the roles reversed, and only a file the host checked joins the queue.
//   - An admitted member's AddItems{uploaded} needs UploadOwnTracks (authorize: MissingPermission otherwise).
//     Before any byte moves, an item that is not a GuestTrack, has no valid hash, declares size 0 or more
//     than maximum_guest_upload_bytes, or whose hash is already being uploaded, answers InvalidArgument, and
//     so does a host without the upload seams.
//   - Otherwise the host asks uploads->status() and sends the owner RoomFileRequest{sha, resume offset}. The
//     owner's RoomFileOffer must name that hash and the declared size (else the upload is refused); the host
//     accepts it under its own transfer id (guests choose theirs) and hands the owner's chunks to the cache.
//     Chunks that come before their offer are kept (at most 2 MiB per link) while a request is outstanding.
//     When some of a transfer's early chunks were dropped over that limit, its offer is accepted and the kept
//     chunks are written (S13.1: the offer can come after the whole file, so no later chunk would show the gap).
//     A failure of that accept or write counts like any other; otherwise the cache's status answer abandons the
//     transfer and asks again from the partial file. A round that did not move past the last request's offset
//     counts as a failed try (room_file_retries), so the owner is never asked for the same bytes forever.
//     The cache refuses an offer above maximum_guest_upload_bytes and a chunk past the offered size, and
//     keeps the file only when its SHA-256 matches the declared hash.
//   - The same owner declaring a hash it is already uploading replaces that upload (its partial file stays
//     for resume; the old request gets no answer). Another member declaring it answers InvalidArgument.
//     A hash already complete in the upload cache (another queued guest item uses it) is not sent again: the
//     declared size must be the cached file's (InvalidArgument otherwise) and it goes straight to the probe;
//     anyone with UploadOwnTracks may so queue it again and owns the new item.
//   - A Gap asks again at once (the partial file stays); another failure asks again at most
//     room_file_retries times, then the upload is refused.
//   - An upload that gets no byte (offer or chunk) from its owner for room_upload_stall_ms is dropped:
//     RoomIntentRejected{request_id, UnknownItem} and uploads->discard(sha). Any upload chunk of the owner
//     counts (its files go up one after the other on its link); a file being probed never stalls.
//   - A complete file goes to upload_probe (off the host's thread). Valid audio joins the queue through
//     addGuestTrack with the probe's summary, owner = the link's fingerprint and added_by = the owner's
//     nickname. Refused (not audio, a mismatched offer, retries spent, a full queue): RoomIntentRejected{the
//     intent's request_id, InvalidArgument (QueueFull for a full queue)} and uploads->discard(sha), so
//     nothing stays unless a queued guest item still uses that file.
//   - A roster change that takes UploadOwnTracks away from an owner refuses its uploads in flight
//     (MissingPermission, files discarded); the probe's answer is checked against the permission again.
//   - A guest item that has been current leaves the player's queue once another item (or none) is current:
//     it played (spec: guest songs leave "quando tocam"). The removal runs on the next scheduler turn and is
//     skipped when the item is current again by then. Previous never returns to it; repeat does not replay it.
//   - A guest item that leaves the player's queue frees its file (uploads->discard) once no other guest item
//     and no upload in flight uses that hash, so the upload cache holds only files of the queue.
//   - When the owner's link goes (it left, was kicked or dropped), its uploads in flight stop and their files
//     are discarded. Its items already in the queue stay and play for everyone: the host has the copy. They
//     leave when someone removes them or the room ends.
//   - close(), revoke() and the destructor stop every upload and purge the upload cache.
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

    // Roles and moderation (S10.1), host-only. Each successful change is saved through the roster store
    // (when the file is writable) and, while the room is open, reaches every admitted member in a new
    // state (the version bumps) carrying its effective permissions. A refused change saves and sends
    // nothing.
    [[nodiscard]] RoomRosterSnapshot roster() const;
    // The role people approved from now on get: the open room's, else the persisted invite's, else the
    // first role.
    [[nodiscard]] std::uint64_t initialRoleId() const;
    // Also updates the persisted invite when the open room is that invite (or no room is open).
    [[nodiscard]] bool setInitialRole(std::uint64_t role_id);
    [[nodiscard]] std::optional<std::uint64_t> createRole(std::string name, RoomPermissionSet permissions);
    [[nodiscard]] bool renameRole(std::uint64_t role_id, std::string name);
    [[nodiscard]] bool setRolePermissions(std::uint64_t role_id, RoomPermissionSet permissions);
    // The initial role (the open room's or the persisted invite's) is never removed: InitialRole. People
    // holding the removed role fall back to the initial role, keeping their overrides.
    [[nodiscard]] RoleRemoval removeRole(std::uint64_t role_id);
    [[nodiscard]] bool assignRole(std::string_view fingerprint, std::uint64_t role_id);
    // nullopt clears the override (the role decides again).
    [[nodiscard]] bool setOverride(std::string_view fingerprint, RoomPermission permission,
                                   std::optional<bool> allowed);
    // "Desbloquear": the person is known again and comes back in without a new approval, with their role.
    // False when not known or not blocked.
    [[nodiscard]] bool unblock(std::string_view fingerprint);
    // True while a roster change of this host is not in the file: the last save failed, or the file cannot
    // be written (unreadable, invalid or from a newer version). A later save that succeeds writes the whole
    // roster and clears it. onRosterUnsavedChanged runs each time it flips.
    [[nodiscard]] bool rosterUnsaved() const noexcept;

    // For the guest-file transfer (S8/S12): a guest's file already in the host's room cache joins the
    // queue as a GuestTrack. Overwrites id, origin, owner_fingerprint (= owner_fingerprint) and
    // added_by (the owner's nickname). nullopt when the room is closed, the owner is not admitted,
    // the room queue is full or the player refuses the file.
    [[nodiscard]] std::optional<std::uint64_t>
    addGuestTrack(std::string_view owner_fingerprint, const std::filesystem::path& cached_file, RoomItem described);
    // What session autosave may store while a room is open: the queue without guest items.
    [[nodiscard]] SessionSnapshot withoutGuestItems(SessionSnapshot session) const;

    // S11.2: the host's playlist library; only the lists visible to the room reach guests (see Catalog).
    void setPlaylists(PlaylistLibrarySnapshot library);
    // The catalog guests have now (empty while the room is closed).
    [[nodiscard]] const RoomCatalog& catalog() const noexcept;

    std::function<void(const RoomSnapshot&)> onChanged;
    std::function<void(const RoomMember&)> onJoinRequest;
    std::function<void(RoomLinkFailure)> onEndpointFailed;
    std::function<void()> onDriftsChanged;
    std::function<void()> onRosterUnsavedChanged;
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
    [[nodiscard]] std::optional<IntentRejection> apply(const RoomIntent& intent, std::string_view fingerprint);
    [[nodiscard]] std::optional<IntentRejection> enqueueAll(const std::vector<std::filesystem::path>& sources,
                                                            std::string_view fingerprint);
    void describeCatalogFiles();
    void scheduleCatalogRefresh();
    void refreshCatalog();
    [[nodiscard]] RoomCatalog buildCatalog();
    void sendCatalog(const Connection& connection) const;
    void onDescribed(std::uint64_t generation, const std::filesystem::path& source,
                     std::optional<RoomTrackDescription> description);
    void requestDescription(const std::filesystem::path& source);
    [[nodiscard]] std::optional<RoomItem> roomItem(const LocalFileQueueSnapshotItem& entry) const;
    bool refresh();
    bool rebuild();
    [[nodiscard]] bool anyAdmittedGuest() const;
    void waitForStart();
    void admitted(const std::shared_ptr<RoomLink>& link);
    void sendState(const Connection& connection) const;
    void sendLyrics(Connection& connection) const;
    struct WindowFile;
    [[nodiscard]] std::vector<WindowFile> fileWindow() const;
    void onFileRequest(Connection& connection, const RoomFileRequest& request);
    void serveFiles();
    void cancelFilesOutsideWindow();
    void forgetFiles(const RoomLink& link);
    // A guest upload in flight (S12.1).
    struct Upload final {
        std::string owner;
        std::uint64_t request_id{};
        std::uint64_t size_bytes{};
        // Callbacks of status() and probe() carry it: an answer for an upload that ended does nothing.
        std::uint64_t token{};
        // A RoomFileRequest went to the owner and its offer is awaited.
        bool requested{false};
        // The owner's transfer id and the cache's one, while the file is arriving.
        std::optional<std::uint32_t> offered{};
        std::optional<std::uint32_t> receiving{};
        int retries{0};
        // The file is complete and upload_probe is checking it: the host's own work, never a stall.
        bool probing{false};
        // S13.1: an overflowed transfer whose kept chunks are being written; still live (a failure counts as usual)
        // until the status answer ends it and asks again (onUploadResumeStatus).
        bool resuming{false};
        // The offset of the last request to the owner: an overflow round that does not move past it gained nothing.
        std::optional<std::uint64_t> asked_offset{};
    };
    [[nodiscard]] std::optional<IntentRejection> startUpload(const RoomItem& item, const std::string& fingerprint,
                                                             std::uint64_t request_id);
    void askUpload(const std::string& sha256);
    void watchUpload(const std::string& sha256, std::uint64_t token, std::uint64_t seen_activity);
    void onUploadWatch(const std::string& sha256, std::uint64_t token, std::uint64_t seen_activity);
    [[nodiscard]] bool mayUpload(std::string_view fingerprint) const;
    void freeUploadFiles(const std::vector<std::string>& hashes);
    void removePlayedGuestItem(std::uint64_t generation, std::uint64_t occurrence);
    void onUploadStatus(std::uint64_t token, const std::string& sha256, const RoomFileStatus& status);
    void onUploadResumeStatus(std::uint64_t token, const std::string& sha256, std::uint32_t receiving,
                              const RoomFileStatus& status);
    void onUploadOffer(Connection& connection, const RoomFileOffer& offer);
    void onUploadChunk(const RoomLink* link, RoomFileChunk chunk);
    void onUploadComplete(const std::string& sha256, const std::filesystem::path& file);
    void probeUpload(const std::string& sha256, const std::filesystem::path& file);
    void onUploadFailed(const std::string& sha256, std::uint32_t transfer_id, RoomFileFailure failure);
    void onUploadProbed(std::uint64_t token, const std::string& sha256, const std::filesystem::path& file,
                        const std::optional<RoomTrackSummary>& summary);
    void refuseUpload(const std::string& sha256, IntentRejection reason);
    void discardUpload(const std::string& sha256, const Upload& upload);
    void stopUploadsOf(std::string_view fingerprint);
    void stopUploads();
    void drop(const std::shared_ptr<RoomLink>& link);
    void forgetDrift(std::string_view fingerprint);
    void end(RoomByeReason reason);
    void saveRoster();
    void rosterChanged();
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
    RoomFileInbox* uploads_;
    RoomUploadProbe* upload_probe_;

    RoomRoster roster_;
    bool roster_writable_{false};
    bool roster_unsaved_{false};
    // The open room was opened from the persisted invite (until revoked).
    bool uses_persisted_invite_{false};
    std::optional<ListeningRoom> room_;
    std::optional<RoomInviteLink> invite_;
    std::uint64_t generation_{0};
    std::vector<Connection> connections_;
    RoomSnapshot current_;
    std::map<std::filesystem::path, RoomTrackDescription> descriptions_;
    std::set<std::filesystem::path> describing_;
    std::set<std::filesystem::path> unreadable_;
    std::map<std::uint64_t, RoomItem> guest_items_;
    // S11.2: the library (kept across rooms), the catalog guests have, the file of each catalog entry, and
    // the room's ids of library playlists and entries (playlist id, entry id).
    PlaylistLibrarySnapshot library_;
    RoomCatalog room_catalog_;
    std::map<std::uint64_t, std::filesystem::path> catalog_sources_;
    std::map<std::uint64_t, std::uint64_t> catalog_playlist_ids_;
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::uint64_t> catalog_entry_ids_;
    std::uint64_t next_catalog_id_{1};
    bool cataloging_{false};
    bool catalog_again_{false};
    // Catalog files still to describe, in catalog order (asked for as descriptions arrive), and whether a
    // catalog refresh is scheduled.
    std::deque<std::filesystem::path> catalog_waiting_;
    bool describing_catalog_{false};
    bool catalog_refresh_scheduled_{false};
    // Who added each catalog track a guest put in the queue (occurrence id -> nickname).
    std::map<std::uint64_t, std::string> catalog_adders_;
    // The cached file of each guest item, to serve it like a host track.
    std::map<std::uint64_t, std::filesystem::path> guest_files_;
    std::uint32_t next_transfer_id_{0};
    bool serve_scheduled_{false};
    // S12.1: guest uploads in flight, by hash; their cache transfer ids and callback tokens.
    std::map<std::string, Upload, std::less<>> uploading_;
    // The occurrence current at the last rebuild: a guest item that stops being current has played.
    std::optional<std::uint64_t> last_current_;
    std::uint32_t next_upload_transfer_id_{0};
    std::uint64_t next_upload_token_{0};
    // Admitted members whose link dropped, held for room_reconnect_window_ms (value: hold token).
    std::map<std::string, std::uint64_t, std::less<>> held_;
    std::uint64_t next_hold_{0};
    // People kicked while held, told RoomBye{Kicked} if they come back within room_reconnect_window_ms.
    std::map<std::string, std::uint64_t, std::less<>> kicked_away_;
    std::map<std::string, std::int64_t, std::less<>> drifts_;
    // The last state guests received (admitted members only), to broadcast only what they would see.
    RoomSnapshot sent_view_;
    // RoomHostPlayerState::seeks at the last rebuild: a change is a seek.
    std::uint64_t seen_seeks_{0};
    // The held start waiting for its anchor (0: none); a new wait or a re-anchor replaces it.
    std::uint64_t start_wait_{0};
    std::uint64_t next_start_wait_{0};
    bool refreshing_{false};
    bool refresh_again_{false};
    std::shared_ptr<Alive> alive_;
};

} // namespace music_player::m0
