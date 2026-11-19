#pragma once

#include "m0/application/RoomPlaybackFollower.hpp"
#include "m0/domain/ListenerProfile.hpp"
#include "m0/domain/ListeningRoom.hpp"
#include "m0/domain/ListeningRoomLimits.hpp"
#include "m0/domain/RoomInviteLink.hpp"
#include "m0/domain/RoomMessages.hpp"
#include "m0/domain/RoomRoster.hpp"
#include "m0/domain/SessionSnapshot.hpp"
#include "m0/ports/RoomFiles.hpp"
#include "m0/ports/RoomTransport.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace music_player::m0 {

enum class GuestRoomState : std::uint8_t { Idle, Connecting, WaitingApproval, Joined, Reconnecting, Ended };

// Why the room ended for this guest (the UI maps it to text, S7.3).
enum class GuestEndReason : std::uint8_t {
    // The first connection failed, the link dropped before admission, or the reconnect window ran out.
    LinkLost,
    RoomFull,        // JoinResult{RejectedFull} or Bye{Full}
    Blocked,         // JoinResult{RejectedBlocked}
    Rejected,        // JoinResult{RejectedClosed}: the host refused the request
    Closed,          // Bye{Closed} ("Encerrar sala"), or HostOffline while reconnecting (host gone, no Bye)
    Revoked,         // Bye{Revoked}
    Kicked,          // Bye{Kicked}
    VersionMismatch, // Bye{VersionMismatch}
};

struct GuestRoomEnd final {
    GuestEndReason reason{};
    // The value onEnded received.
    RoomLinkFailure failure{};
    bool operator==(const GuestRoomEnd&) const = default;
};

struct ListeningRoomGuestDependencies final {
    RoomGuestConnector& connector;
    // Must satisfy isValidListenerProfile (the host closes a link whose hello is not); the constructor
    // throws otherwise. Construct one guest per profile: a later profile edit is not seen.
    ListenerProfile profile{};
    // SHA-256 (64 lowercase hex) of the stored custom avatar image (<config>/avatars/custom.png), or
    // nullopt when it is missing or unreadable. The app hashes the file; may be empty (= missing).
    std::function<std::optional<std::string>()> stored_custom_avatar_sha256;
    // Seam to whatever owns the local queue (the presenter in the app). capture_session returns the
    // current local queue and position; restore_session puts back exactly that snapshot.
    std::function<SessionSnapshot()> capture_session;
    std::function<void(const SessionSnapshot&)> restore_session;
    // Runs task once after delay_ms on the guest's thread (QTimer::singleShot in the app). Same shape
    // as ListeningRoomHostDependencies::schedule.
    std::function<void(std::int64_t delay_ms, std::function<void()> task)> schedule;
    // The room cache (S8.2); nullptr: the guest asks for no files.
    RoomFileInbox* files{nullptr};
    // S9.1: the player that follows the host's time, and the follower's clock (required with it).
    // nullptr: the guest plays nothing and sends no clock pings. It must outlive the guest.
    RoomPlaybackFollower* follower{nullptr};
    std::function<std::int64_t()> local_clock_ms;
};

// Guest side of the listening room (ADR-0022, S7.2). Qt-free, no audio: it keeps the guest's view of
// the room (snapshot, own permissions) and the link to the host. Every call and callback runs on one
// thread.
//
// States: join() goes Idle/Ended -> Connecting and asks the connector for a link to the invite's host.
// On a link it sends RoomHello{room_protocol_version, nickname, avatar}; JoinResult{Pending} ->
// WaitingApproval, JoinResult{Admitted} -> Joined (from Connecting or WaitingApproval). join() in any
// other state does nothing.
//
// Avatar: a Custom avatar is sent only when stored_custom_avatar_sha256() equals the profile's hash;
// on a mismatch or a missing file the hello carries Bundled index 0 (the bundled index a Custom profile
// keeps), never a hash peers could not fetch.
//
// Snapshots: RoomStateMessage is applied only while Joined. A state whose version is <= the current one
// is ignored; gaps are normal (the host sends what changed for this guest). Exception: the first state
// on a link that was just admitted is always taken, so a guest that reconnects to a restarted host
// (versions start again) is not stuck on its old version. permissions() is the my_permissions of the
// last applied state.
//
// Intents: sendIntent returns a request_id, monotonic from 1 over the guest's life (never reset by a
// reconnect or a new join). While not Joined it sends nothing, consumes no id and returns 0; when the
// link refuses the message it returns 0 and that id is not reused.
// RoomIntentRejected{id, reason} calls onRejected(id, reason).
//
// Link loss: from Joined, a dropped link goes to Reconnecting with the same invite: a connect at once,
// and after each failure (a connector failure other than HostOffline, or a new link that drops before
// admission) another attempt after room_reconnect_retry_initial_ms, doubling up to
// room_reconnect_retry_maximum_ms. HostOffline ends the room at once: Ended(Closed) with
// onEnded(HostOffline). The relay only says it when the host's app is not in the room, so the host left
// without a Bye (its app closed or crashed), and the room ends for everyone when the host leaves.
// JoinResult{Admitted} returns to Joined (snapshot kept), Pending to WaitingApproval.
// room_reconnect_window_ms after the drop the window ends: Ended(LinkLost) with onEnded(last failure
// seen). From Connecting or WaitingApproval a drop or a connect failure ends at once (LinkLost, that
// failure).
//
// Ending: JoinResult RejectedFull/RejectedBlocked/RejectedClosed and RoomBye end the room with the
// reason in endReason(). onEnded gets: Bye{Closed} and Bye{Revoked} -> Closed, Bye{VersionMismatch} ->
// VersionMismatch, Bye{Kicked}, Bye{Full} and every rejected JoinResult -> Refused, a lost link -> its
// failure. Ending closes the link, clears snapshot() and permissions(), restores the local session (if
// one was saved), then calls onChanged and then onEnded. leave() does the same but goes to Idle and does not
// call onEnded; it first sends RoomBye{Closed} on the link (when there is one), so the host frees the
// seat at once instead of holding it for its reconnect window.
//
// Local session: on the first entry to Joined of a join() the guest calls capture_session once and
// keeps that snapshot until leave() or Ended, which call restore_session with it exactly (a reconnect
// does not capture again). While it is held, the app's session autosave must store
// sessionForAutosave(live) — the pre-room snapshot — so room playback never overwrites it on disk, and
// must not feed observeConfirmedPosition (holdsLocalSession() tells when). The destructor closes the
// link without calling any callback or restoring: call leave() first while the app keeps running.
//
// Files (S8.2), request-driven: while Joined, the file window is the first 1 + room_prefetch_items
// items of the room queue (the current item, then the next ones in queue order; items without a valid
// hash, such as YouTube ones, are skipped). For each window hash that is not known complete the guest
// asks files->status() in window order and, when the answer is not complete and the hash is still in
// the window, sends RoomFileRequest{sha, resume_offset}. A hash already in the cache is never
// requested. Only an offer for a hash it requested is accepted, or a newer offer for a hash being
// received (the host replaced that transfer: the old one is abandoned first); chunks of accepted
// transfers go to the inbox. The offer (control channel) and the chunks (media channel) are not ordered
// with each other: while a request is outstanding, chunks of a transfer not offered yet are kept (at
// most 2 MiB in all) and handed over once its offer is accepted; chunks of a transfer already over are
// dropped. When the window changes, transfers whose item left it are abandoned (and asked again if it
// comes back). A transfer that ends in a Gap is asked again at once (from the partial file); one that
// fails is asked again at most room_file_retries times while its item stays in the window. A dropped
// link abandons every transfer (the partial files stay, so the next request resumes); leave() and the end of the room
// abandon them and then purge the cache. When the current item changes, the old one's hash goes to files->markPlayed().
//
// Lyrics: RoomLyrics for the current item (at most maximum_room_lyrics_bytes) is kept until the
// current item changes; lyrics for any other item are ignored.
//
// Time (S9.1), with a follower: on admission and then every room_clock_resample_ms while that link
// stays joined, the guest sends RoomClockPing{local_clock_ms()}; each RoomClockPong becomes a
// ClockSample for the follower. Right after each ping the guest sends RoomDriftReport{follower->lastDriftMs()}
// when the follower has a valid evaluation (S9.2). Every applied state, and every current-item file that becomes local,
// calls follower->apply(playback, local file of the current item or nullopt). Ending or leaving stops
// the follower before restore_session, so the room file is gone when the local session comes back. A
// dropped link leaves the follower playing the last playback.
//
// Catalog (S11.2): the last RoomCatalog received while Joined is kept (a dropped link keeps it, the host
// sends it again on admission); onChanged runs when it changes. join(), leave() and the end of the room
// clear it. A catalog that arrives before admission is ignored.
class ListeningRoomGuest final {
  public:
    explicit ListeningRoomGuest(ListeningRoomGuestDependencies dependencies);
    ~ListeningRoomGuest();
    ListeningRoomGuest(const ListeningRoomGuest&) = delete;
    ListeningRoomGuest& operator=(const ListeningRoomGuest&) = delete;

    void join(const RoomInviteLink& invite);
    void leave();
    [[nodiscard]] std::uint64_t sendIntent(const RoomIntent& intent);
    [[nodiscard]] GuestRoomState state() const;
    [[nodiscard]] const RoomSnapshot& snapshot() const;
    [[nodiscard]] RoomPermissionSet permissions() const;
    // Set when the state is Ended.
    [[nodiscard]] std::optional<GuestRoomEnd> endReason() const;
    // What session autosave may store: the pre-room snapshot while one is held, live otherwise.
    [[nodiscard]] SessionSnapshot sessionForAutosave(SessionSnapshot live) const;
    [[nodiscard]] bool holdsLocalSession() const noexcept;
    // The verified local copy of a room file, nullopt while it is missing or still arriving.
    [[nodiscard]] std::optional<std::filesystem::path> localFile(std::string_view sha256) const;
    // The host's lyrics for the current item, when it sent them.
    [[nodiscard]] std::optional<RoomLyrics> lyrics() const;
    // S11.2: the host's last catalog while in the room (empty otherwise).
    [[nodiscard]] const RoomCatalog& catalog() const noexcept;

    std::function<void()> onChanged;
    std::function<void(std::uint64_t, IntentRejection)> onRejected;
    std::function<void(RoomLinkFailure)> onEnded;

  private:
    struct Alive;

    void connect();
    void onConnected(std::uint64_t attempt, std::shared_ptr<RoomLink> link);
    void onConnectFailed(std::uint64_t attempt, RoomLinkFailure failure);
    void onControl(std::uint64_t link_id, const RoomControlMessage& message);
    void onLinkClosed(std::uint64_t link_id, RoomLinkFailure failure);
    void onJoinResult(JoinDecision decision);
    void onState(const RoomStateMessage& message);
    void lost(RoomLinkFailure failure);
    void onRetry(std::uint64_t episode);
    void onWindowExpired(std::uint64_t episode);
    void end(GuestEndReason reason, RoomLinkFailure failure);
    void stop();
    void detachLink();
    [[nodiscard]] RoomHello hello() const;
    [[nodiscard]] std::vector<std::string> fileWindow() const;
    void updateFiles();
    void onFileStatus(std::uint64_t epoch, const std::string& sha256, const RoomFileStatus& status);
    void onOffer(const RoomFileOffer& offer);
    void onChunk(RoomFileChunk chunk);
    void onFileComplete(const std::string& sha256, const std::filesystem::path& file);
    void onFileFailed(const std::string& sha256, std::uint32_t transfer_id, RoomFileFailure failure);
    void stopFiles(bool purge);
    void follow();
    void ping(std::uint64_t link_id);
    void changed() const;

    RoomGuestConnector& connector_;
    ListenerProfile profile_;
    std::function<std::optional<std::string>()> stored_custom_avatar_sha256_;
    std::function<SessionSnapshot()> capture_session_;
    std::function<void(const SessionSnapshot&)> restore_session_;
    std::function<void(std::int64_t, std::function<void()>)> schedule_;
    RoomFileInbox* files_;
    RoomPlaybackFollower* follower_;
    std::function<std::int64_t()> local_clock_ms_;

    GuestRoomState state_{GuestRoomState::Idle};
    std::optional<RoomInviteLink> invite_;
    std::shared_ptr<RoomLink> link_;
    // Bumped for every connect and every link so late callbacks of older ones do nothing.
    std::uint64_t attempt_{0};
    std::uint64_t link_id_{0};
    bool connecting_{false};
    // Bumped when a reconnect window starts or stops: retry and expiry tasks of older windows do nothing.
    std::uint64_t episode_{0};
    std::int64_t retry_delay_ms_{room_reconnect_retry_initial_ms};
    RoomLinkFailure last_failure_{RoomLinkFailure::Closed};
    // The next state is taken whatever its version (first state of a newly admitted link).
    bool fresh_link_{false};
    RoomSnapshot snapshot_;
    RoomPermissionSet permissions_;
    std::optional<GuestRoomEnd> end_;
    std::optional<SessionSnapshot> saved_session_;
    std::uint64_t next_request_id_{1};
    // Room files: complete ones, status asked, request sent, transfers accepted (id -> sha), failed.
    std::map<std::string, std::filesystem::path, std::less<>> local_files_;
    std::set<std::string, std::less<>> checking_;
    std::set<std::string, std::less<>> requested_;
    std::map<std::uint32_t, std::string> receiving_;
    std::set<std::string, std::less<>> failed_;
    // Failed transfers asked for again in this window entry (at most room_file_retries per file).
    std::map<std::string, int, std::less<>> retries_;
    // Bumped when the guest stops its transfers: older status answers do nothing.
    std::uint64_t files_epoch_{0};
    std::optional<RoomLyrics> lyrics_;
    RoomCatalog catalog_;
    // Chunks that came before their offer (control and media are separate channels, not ordered with
    // each other), kept only while a request is outstanding and up to a fixed total.
    std::map<std::uint32_t, std::vector<RoomFileChunk>> early_chunks_;
    std::size_t early_bytes_{0};
    // The highest transfer id offered so far. The host numbers transfers in increasing order and its
    // offers arrive in order, so a chunk at or below it that is not being received belongs to a transfer
    // that is over (refused, replaced or abandoned) and is dropped instead of kept as early.
    std::uint32_t last_offer_id_{0};
    std::shared_ptr<Alive> alive_;
};

} // namespace music_player::m0
