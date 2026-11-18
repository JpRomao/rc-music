#pragma once

#include "m0/domain/ListenerProfile.hpp"
#include "m0/domain/ListeningRoom.hpp"
#include "m0/domain/ListeningRoomLimits.hpp"
#include "m0/domain/RoomInviteLink.hpp"
#include "m0/domain/RoomMessages.hpp"
#include "m0/domain/RoomRoster.hpp"
#include "m0/domain/SessionSnapshot.hpp"
#include "m0/ports/RoomTransport.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace music_player::m0 {

enum class GuestRoomState : std::uint8_t { Idle, Connecting, WaitingApproval, Joined, Reconnecting, Ended };

// Why the room ended for this guest (the UI maps it to text, S7.3).
enum class GuestEndReason : std::uint8_t {
    // The first connection failed, the link dropped before admission, or the reconnect window ran out.
    LinkLost,
    RoomFull,        // JoinResult{RejectedFull} or Bye{Full}
    Blocked,         // JoinResult{RejectedBlocked}
    Rejected,        // JoinResult{RejectedClosed}: the host refused the request
    Closed,          // Bye{Closed}: "Encerrar sala"
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
// and after each failure (connector failure of any kind, HostOffline included, or a new link that drops
// before admission) another attempt after room_reconnect_retry_initial_ms, doubling up to
// room_reconnect_retry_maximum_ms. JoinResult{Admitted} returns to Joined (snapshot kept), Pending to
// WaitingApproval. room_reconnect_window_ms after the drop the window ends: Ended(LinkLost) with
// onEnded(last failure seen). From Connecting or WaitingApproval a drop or a connect failure ends at
// once (LinkLost, that failure).
//
// Ending: JoinResult RejectedFull/RejectedBlocked/RejectedClosed and RoomBye end the room with the
// reason in endReason(). onEnded gets: Bye{Closed} and Bye{Revoked} -> Closed, Bye{VersionMismatch} ->
// VersionMismatch, Bye{Kicked}, Bye{Full} and every rejected JoinResult -> Refused, a lost link -> its
// failure. Ending closes the link, clears snapshot() and permissions(), restores the local session (if
// one was saved), then calls onChanged and then onEnded. leave() does the same but goes to Idle and does not
// call onEnded; the host sees the link close (it holds the seat for its own reconnect window).
//
// Local session: on the first entry to Joined of a join() the guest calls capture_session once and
// keeps that snapshot until leave() or Ended, which call restore_session with it exactly (a reconnect
// does not capture again). While it is held, the app's session autosave must store
// sessionForAutosave(live) — the pre-room snapshot — so room playback never overwrites it on disk, and
// must not feed observeConfirmedPosition (holdsLocalSession() tells when). The destructor closes the
// link without calling any callback or restoring: call leave() first while the app keeps running.
//
// Other control messages (lyrics, clock, catalog, files) are left to later tasks (S8-S11).
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
    void changed() const;

    RoomGuestConnector& connector_;
    ListenerProfile profile_;
    std::function<std::optional<std::string>()> stored_custom_avatar_sha256_;
    std::function<SessionSnapshot()> capture_session_;
    std::function<void(const SessionSnapshot&)> restore_session_;
    std::function<void(std::int64_t, std::function<void()>)> schedule_;

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
    std::shared_ptr<Alive> alive_;
};

} // namespace music_player::m0
