#include "m0/application/ListeningRoomGuest.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <variant>

namespace music_player::m0 {
namespace {

GuestRoomEnd byeEnd(const RoomByeReason reason) {
    switch (reason) {
    case RoomByeReason::Closed:
        return {GuestEndReason::Closed, RoomLinkFailure::Closed};
    case RoomByeReason::Revoked:
        return {GuestEndReason::Revoked, RoomLinkFailure::Closed};
    case RoomByeReason::Kicked:
        return {GuestEndReason::Kicked, RoomLinkFailure::Refused};
    case RoomByeReason::VersionMismatch:
        return {GuestEndReason::VersionMismatch, RoomLinkFailure::VersionMismatch};
    case RoomByeReason::Full:
        return {GuestEndReason::RoomFull, RoomLinkFailure::Refused};
    }
    return {GuestEndReason::Closed, RoomLinkFailure::Closed};
}

} // namespace

// Callbacks handed to the connector, links and the scheduler hold a weak reference to this token, so
// one that arrives after the guest is gone does nothing.
struct ListeningRoomGuest::Alive final {
    ListeningRoomGuest* guest{};
};

ListeningRoomGuest::ListeningRoomGuest(ListeningRoomGuestDependencies dependencies)
    : connector_(dependencies.connector), profile_(std::move(dependencies.profile)),
      stored_custom_avatar_sha256_(std::move(dependencies.stored_custom_avatar_sha256)),
      capture_session_(std::move(dependencies.capture_session)),
      restore_session_(std::move(dependencies.restore_session)), schedule_(std::move(dependencies.schedule)),
      alive_(std::make_shared<Alive>(Alive{this})) {
    if (!capture_session_ || !restore_session_ || !schedule_)
        throw std::invalid_argument{"the listening room guest needs the local session seam and a scheduler"};
    if (!isValidListenerProfile(profile_))
        throw std::invalid_argument{"the listening room guest needs a valid listener profile"};
}

ListeningRoomGuest::~ListeningRoomGuest() {
    alive_.reset();
    if (connecting_)
        connector_.cancel();
    if (link_) {
        link_->onControl = nullptr;
        link_->onMedia = nullptr;
        link_->onClosed = nullptr;
        link_->close();
    }
}

void ListeningRoomGuest::join(const RoomInviteLink& invite) {
    if (state_ != GuestRoomState::Idle && state_ != GuestRoomState::Ended)
        return;
    invite_ = invite;
    state_ = GuestRoomState::Connecting;
    end_.reset();
    snapshot_ = {};
    permissions_.reset();
    fresh_link_ = false;
    changed();
    if (state_ == GuestRoomState::Connecting && !connecting_ && !link_)
        connect();
}

void ListeningRoomGuest::leave() {
    if (state_ == GuestRoomState::Idle)
        return;
    // Tell the host first, so it frees the seat at once instead of holding it for its reconnect window.
    // The link is detached before the send: the host closing it in answer is not a drop.
    if (const auto link = std::exchange(link_, nullptr)) {
        ++link_id_;
        static_cast<void>(link->sendControl(RoomBye{RoomByeReason::Closed}));
        link->close();
    }
    stop();
    state_ = GuestRoomState::Idle;
    end_.reset();
    changed();
}

std::uint64_t ListeningRoomGuest::sendIntent(const RoomIntent& intent) {
    if (state_ != GuestRoomState::Joined || !link_)
        return 0;
    // Taken before sending: an answer delivered during the send may lead to another sendIntent.
    const auto request_id = next_request_id_++;
    // Keep the link alive even if the send makes the host answer (and this guest end) synchronously.
    const auto link = link_;
    return link->sendControl(RoomIntentMessage{request_id, intent}) ? request_id : 0;
}

GuestRoomState ListeningRoomGuest::state() const { return state_; }

const RoomSnapshot& ListeningRoomGuest::snapshot() const { return snapshot_; }

RoomPermissionSet ListeningRoomGuest::permissions() const { return permissions_; }

std::optional<GuestRoomEnd> ListeningRoomGuest::endReason() const { return end_; }

SessionSnapshot ListeningRoomGuest::sessionForAutosave(SessionSnapshot live) const {
    return saved_session_ ? *saved_session_ : live;
}

bool ListeningRoomGuest::holdsLocalSession() const noexcept { return saved_session_.has_value(); }

void ListeningRoomGuest::connect() {
    const auto attempt = ++attempt_;
    connecting_ = true;
    const std::weak_ptr<Alive> alive = alive_;
    connector_.connect(
        *invite_,
        [alive, attempt](std::shared_ptr<RoomLink> link) {
            if (const auto self = alive.lock())
                self->guest->onConnected(attempt, std::move(link));
            else if (link)
                link->close();
        },
        [alive, attempt](const RoomLinkFailure failure) {
            if (const auto self = alive.lock())
                self->guest->onConnectFailed(attempt, failure);
        });
}

void ListeningRoomGuest::onConnected(const std::uint64_t attempt, std::shared_ptr<RoomLink> link) {
    if (attempt != attempt_ || !connecting_ || !link) {
        if (link)
            link->close();
        return;
    }
    connecting_ = false;
    link_ = link;
    const auto link_id = ++link_id_;
    const std::weak_ptr<Alive> alive = alive_;
    link->onControl = [alive, link_id](RoomControlMessage message) {
        if (const auto self = alive.lock())
            self->guest->onControl(link_id, message);
    };
    link->onClosed = [alive, link_id](const RoomLinkFailure failure) {
        if (const auto self = alive.lock())
            self->guest->onLinkClosed(link_id, failure);
    };
    // The fake transport answers synchronously: anything may happen during this send.
    if (!link->sendControl(hello()) && link_id == link_id_) {
        detachLink();
        lost(RoomLinkFailure::Closed);
    }
}

void ListeningRoomGuest::onConnectFailed(const std::uint64_t attempt, const RoomLinkFailure failure) {
    if (attempt != attempt_ || !connecting_)
        return;
    connecting_ = false;
    lost(failure);
}

void ListeningRoomGuest::onControl(const std::uint64_t link_id, const RoomControlMessage& message) {
    if (link_id != link_id_ || !link_)
        return;
    // Keeps the link object alive while this call may close and release it.
    const auto keep = link_;
    if (const auto* result = std::get_if<RoomJoinResult>(&message)) {
        onJoinResult(result->decision);
    } else if (const auto* state = std::get_if<RoomStateMessage>(&message)) {
        if (state_ == GuestRoomState::Joined)
            onState(*state);
    } else if (const auto* rejected = std::get_if<RoomIntentRejected>(&message)) {
        if (onRejected)
            onRejected(rejected->request_id, rejected->reason);
    } else if (const auto* bye = std::get_if<RoomBye>(&message)) {
        const auto ending = byeEnd(bye->reason);
        end(ending.reason, ending.failure);
    }
    // Lyrics, clock, catalog and file messages belong to later tasks (S8-S11).
}

void ListeningRoomGuest::onLinkClosed(const std::uint64_t link_id, const RoomLinkFailure failure) {
    if (link_id != link_id_ || !link_)
        return;
    const auto keep = link_;
    // Closed by the network: no local close is needed, only forget it.
    link_.reset();
    ++link_id_;
    lost(failure);
}

void ListeningRoomGuest::onJoinResult(const JoinDecision decision) {
    switch (decision) {
    case JoinDecision::Admitted:
        if (state_ == GuestRoomState::Joined)
            return;
        ++episode_; // stops a running reconnect window
        fresh_link_ = true;
        if (!saved_session_)
            saved_session_ = capture_session_();
        state_ = GuestRoomState::Joined;
        changed();
        return;
    case JoinDecision::Pending:
        if (state_ == GuestRoomState::WaitingApproval || state_ == GuestRoomState::Joined)
            return;
        ++episode_;
        state_ = GuestRoomState::WaitingApproval;
        changed();
        return;
    case JoinDecision::RejectedFull:
        end(GuestEndReason::RoomFull, RoomLinkFailure::Refused);
        return;
    case JoinDecision::RejectedBlocked:
        end(GuestEndReason::Blocked, RoomLinkFailure::Refused);
        return;
    case JoinDecision::RejectedClosed:
        end(GuestEndReason::Rejected, RoomLinkFailure::Refused);
        return;
    }
}

void ListeningRoomGuest::onState(const RoomStateMessage& message) {
    if (!fresh_link_ && message.snapshot.version <= snapshot_.version)
        return;
    fresh_link_ = false;
    if (message.snapshot == snapshot_ && message.my_permissions == permissions_)
        return;
    snapshot_ = message.snapshot;
    permissions_ = message.my_permissions;
    changed();
}

void ListeningRoomGuest::lost(const RoomLinkFailure failure) {
    last_failure_ = failure;
    switch (state_) {
    case GuestRoomState::Joined: {
        // The window counts from the drop; the first attempt starts at once.
        const auto episode = ++episode_;
        retry_delay_ms_ = room_reconnect_retry_initial_ms;
        state_ = GuestRoomState::Reconnecting;
        const std::weak_ptr<Alive> alive = alive_;
        schedule_(room_reconnect_window_ms, [alive, episode] {
            if (const auto self = alive.lock())
                self->guest->onWindowExpired(episode);
        });
        connect();
        changed();
        return;
    }
    case GuestRoomState::Reconnecting: {
        const auto episode = episode_;
        const auto delay = retry_delay_ms_;
        retry_delay_ms_ = std::min<std::int64_t>(retry_delay_ms_ * 2, room_reconnect_retry_maximum_ms);
        const std::weak_ptr<Alive> alive = alive_;
        schedule_(delay, [alive, episode] {
            if (const auto self = alive.lock())
                self->guest->onRetry(episode);
        });
        return;
    }
    case GuestRoomState::Connecting:
    case GuestRoomState::WaitingApproval:
        end(GuestEndReason::LinkLost, failure);
        return;
    case GuestRoomState::Idle:
    case GuestRoomState::Ended:
        return;
    }
}

void ListeningRoomGuest::onRetry(const std::uint64_t episode) {
    if (episode != episode_ || state_ != GuestRoomState::Reconnecting || connecting_ || link_)
        return;
    connect();
}

void ListeningRoomGuest::onWindowExpired(const std::uint64_t episode) {
    if (episode != episode_ || state_ != GuestRoomState::Reconnecting)
        return;
    end(GuestEndReason::LinkLost, last_failure_);
}

void ListeningRoomGuest::end(const GuestEndReason reason, const RoomLinkFailure failure) {
    stop();
    state_ = GuestRoomState::Ended;
    end_ = GuestRoomEnd{reason, failure};
    changed();
    if (onEnded)
        onEnded(failure);
}

// Leaves the room locally: no link, no attempt, no window, an empty view and the local session back.
void ListeningRoomGuest::stop() {
    if (connecting_) {
        connecting_ = false;
        connector_.cancel();
    }
    ++attempt_;
    ++episode_;
    detachLink();
    snapshot_ = {};
    permissions_.reset();
    fresh_link_ = false;
    if (auto saved = std::exchange(saved_session_, std::nullopt))
        restore_session_(*saved);
}

// The link's callbacks are left in place (they may be running) and turn into no-ops with the new id.
void ListeningRoomGuest::detachLink() {
    ++link_id_;
    if (const auto link = std::exchange(link_, nullptr))
        link->close();
}

RoomHello ListeningRoomGuest::hello() const {
    auto avatar = profile_.avatar;
    if (avatar.kind == AvatarKind::Custom) {
        const auto stored = stored_custom_avatar_sha256_ ? stored_custom_avatar_sha256_() : std::nullopt;
        if (!stored || *stored != avatar.custom_sha256)
            avatar = AvatarChoice{AvatarKind::Bundled, 0, {}};
    }
    return RoomHello{room_protocol_version, profile_.nickname, std::move(avatar)};
}

void ListeningRoomGuest::changed() const {
    if (onChanged)
        onChanged();
}

} // namespace music_player::m0
