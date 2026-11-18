#include "m0/domain/ListeningRoom.hpp"

#include "m0/domain/ListeningRoomLimits.hpp"

#include <algorithm>
#include <utility>

namespace music_player::m0 {

RoomPermission requiredPermission(const RoomIntent& intent) noexcept {
    switch (intent.kind) {
    case RoomIntentKind::Next:
    case RoomIntentKind::Previous:
    case RoomIntentKind::Seek:
        return RoomPermission::Skip;
    case RoomIntentKind::Pause:
    case RoomIntentKind::Resume:
        return RoomPermission::Pause;
    case RoomIntentKind::AddItems:
        return intent.uploaded.has_value() ? RoomPermission::UploadOwnTracks : RoomPermission::AddFromRoom;
    case RoomIntentKind::AddPlaylist:
        return RoomPermission::AddPlaylist;
    case RoomIntentKind::RemoveItem:
    case RoomIntentKind::MoveItem:
        return RoomPermission::EditQueue;
    }
    return RoomPermission::EditQueue;
}

ListeningRoom::ListeningRoom(const std::size_t max_listeners, const std::uint64_t default_role_id, RoomRoster& roster)
    : max_listeners_(std::clamp<std::size_t>(max_listeners, minimum_room_listeners, maximum_room_listeners)),
      default_role_id_(default_role_id), roster_(roster) {}

JoinDecision ListeningRoom::requestJoin(const RoomMember& candidate) {
    const auto existing = std::find_if(members_.begin(), members_.end(),
                                       [&](const RoomMember& m) { return m.fingerprint == candidate.fingerprint; });
    if (existing != members_.end()) {
        existing->nickname = candidate.nickname;
        existing->avatar = candidate.avatar;
        return existing->state == MemberState::Admitted ? JoinDecision::Admitted : JoinDecision::Pending;
    }
    const KnownListener* known = roster_.find(candidate.fingerprint);
    if (known != nullptr && known->blocked)
        return JoinDecision::RejectedBlocked;
    RoomMember member = candidate;
    if (known != nullptr) {
        if (admittedCount() >= max_listeners_)
            return JoinDecision::RejectedFull;
        member.state = MemberState::Admitted;
        members_.push_back(std::move(member));
        return JoinDecision::Admitted;
    }
    member.state = MemberState::Pending;
    members_.push_back(std::move(member));
    return JoinDecision::Pending;
}

bool ListeningRoom::approve(const std::string_view fingerprint) {
    const auto it = std::find_if(members_.begin(), members_.end(),
                                 [&](const RoomMember& m) { return m.fingerprint == fingerprint; });
    if (it == members_.end() || it->state != MemberState::Pending || admittedCount() >= max_listeners_)
        return false;
    it->state = MemberState::Admitted;
    roster_.remember(it->fingerprint, it->nickname, default_role_id_);
    return true;
}

bool ListeningRoom::reject(const std::string_view fingerprint) {
    const auto it = std::find_if(members_.begin(), members_.end(), [&](const RoomMember& m) {
        return m.fingerprint == fingerprint && m.state == MemberState::Pending;
    });
    if (it == members_.end())
        return false;
    members_.erase(it);
    return true;
}

void ListeningRoom::leave(const std::string_view fingerprint) {
    std::erase_if(members_, [&](const RoomMember& m) { return m.fingerprint == fingerprint; });
}

bool ListeningRoom::kick(const std::string_view fingerprint, const bool block) {
    const auto it = std::find_if(members_.begin(), members_.end(),
                                 [&](const RoomMember& m) { return m.fingerprint == fingerprint; });
    if (it == members_.end())
        return false;
    const RoomMember member = *it;
    members_.erase(it);
    if (block) {
        if (roster_.find(fingerprint) == nullptr) {
            roster_.remember(member.fingerprint, member.nickname, default_role_id_);
            if (roster_.find(fingerprint) == nullptr) {
                // Default role no longer exists: use any existing role so the block can be recorded.
                const auto roles = roster_.snapshot().roles;
                if (!roles.empty())
                    roster_.remember(member.fingerprint, member.nickname, roles.front().id);
            }
        }
        (void)roster_.setBlocked(fingerprint, true);
    }
    return true;
}

std::optional<IntentRejection> ListeningRoom::authorize(const std::string_view fingerprint, const RoomIntent& intent,
                                                        const RoomSnapshot& current) const {
    const auto member = std::find_if(members_.begin(), members_.end(), [&](const RoomMember& m) {
        return m.fingerprint == fingerprint && m.state == MemberState::Admitted;
    });
    if (member == members_.end())
        return IntentRejection::NotAdmitted;
    // One shape per AddItems intent: catalog entries or one upload, never both. Checked before the
    // permission bit so a mixed intent cannot ride on a single permission (InvalidArgument for anyone admitted).
    if (intent.kind == RoomIntentKind::AddItems && !intent.catalog_entry_ids.empty() && intent.uploaded.has_value())
        return IntentRejection::InvalidArgument;
    if (!roster_.effectivePermissions(fingerprint).test(static_cast<std::size_t>(requiredPermission(intent))))
        return IntentRejection::MissingPermission;

    const auto hasItem = [&](const std::uint64_t id) {
        return std::any_of(current.queue.begin(), current.queue.end(),
                           [&](const RoomItem& item) { return item.id == id; });
    };
    switch (intent.kind) {
    case RoomIntentKind::RemoveItem:
        if (!hasItem(intent.item_id))
            return IntentRejection::UnknownItem;
        break;
    case RoomIntentKind::MoveItem:
        if (!hasItem(intent.item_id))
            return IntentRejection::UnknownItem;
        if (intent.destination >= current.queue.size())
            return IntentRejection::InvalidArgument;
        break;
    case RoomIntentKind::Seek:
        if (intent.position_ms < 0)
            return IntentRejection::InvalidArgument;
        break;
    case RoomIntentKind::AddItems: {
        const std::size_t added = intent.catalog_entry_ids.size() + (intent.uploaded.has_value() ? 1 : 0);
        if (added == 0)
            return IntentRejection::InvalidArgument;
        if (current.queue.size() + added > maximum_room_queue_items)
            return IntentRejection::QueueFull;
        break;
    }
    default:
        break;
    }
    return std::nullopt;
}

std::size_t ListeningRoom::admittedCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        members_.begin(), members_.end(), [](const RoomMember& m) { return m.state == MemberState::Admitted; }));
}

std::vector<RoomMember> ListeningRoom::members() const { return members_; }

} // namespace music_player::m0
