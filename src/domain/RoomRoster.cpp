#include "m0/domain/RoomRoster.hpp"

#include "m0/domain/ListenerProfile.hpp"
#include "m0/domain/ListeningRoomLimits.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace music_player::m0 {
namespace {

const RoomRole* findRole(const RoomRosterSnapshot& state, const std::uint64_t id) {
    const auto it =
        std::find_if(state.roles.begin(), state.roles.end(), [id](const RoomRole& r) { return r.id == id; });
    return it == state.roles.end() ? nullptr : &*it;
}

bool nameTaken(const RoomRosterSnapshot& state, const std::string& name, const std::uint64_t except_id) {
    return std::any_of(state.roles.begin(), state.roles.end(),
                       [&](const RoomRole& r) { return r.id != except_id && r.name == name; });
}

bool isValidSnapshot(const RoomRosterSnapshot& s) {
    if (s.roles.empty())
        return false;
    std::set<std::uint64_t> ids;
    std::set<std::string> names;
    for (const auto& role : s.roles) {
        const auto name = canonicalNickname(role.name);
        if (role.id < 1 || !name.has_value() || *name != role.name || !ids.insert(role.id).second ||
            !names.insert(role.name).second)
            return false;
    }
    std::set<std::string> prints;
    for (const auto& l : s.listeners) {
        if (l.fingerprint.empty() || !prints.insert(l.fingerprint).second || ids.count(l.role_id) == 0)
            return false;
        std::set<RoomPermission> seen;
        for (const auto& o : l.overrides) {
            if (static_cast<std::size_t>(o.permission) >= room_permission_count || !seen.insert(o.permission).second)
                return false;
        }
    }
    if (s.invite.has_value()) {
        if (s.invite->max_listeners < minimum_room_listeners || s.invite->max_listeners > maximum_room_listeners ||
            ids.count(s.invite->default_role_id) == 0)
            return false;
    }
    return true;
}

KnownListener* findMutable(RoomRosterSnapshot& state, const std::string_view fingerprint) {
    const auto it = std::find_if(state.listeners.begin(), state.listeners.end(),
                                 [&](const KnownListener& l) { return l.fingerprint == fingerprint; });
    return it == state.listeners.end() ? nullptr : &*it;
}

} // namespace

RoomRosterSnapshot defaultRoomRoster() {
    RoomRosterSnapshot s;
    s.roles.push_back({1, "Ouvinte", {}});
    RoomPermissionSet all;
    all.set();
    s.roles.push_back({2, "DJ", all});
    return s;
}

RoomRoster::RoomRoster() : state_(defaultRoomRoster()) {}

bool RoomRoster::restore(RoomRosterSnapshot snapshot) {
    if (!isValidSnapshot(snapshot))
        return false;
    state_ = std::move(snapshot);
    return true;
}

RoomRosterSnapshot RoomRoster::snapshot() const { return state_; }

std::optional<std::uint64_t> RoomRoster::createRole(std::string name, const RoomPermissionSet permissions) {
    const auto canonical = canonicalNickname(name);
    if (!canonical.has_value() || nameTaken(state_, *canonical, 0))
        return std::nullopt;
    std::uint64_t next = 1;
    for (const auto& r : state_.roles)
        next = std::max(next, r.id + 1);
    state_.roles.push_back({next, *canonical, permissions});
    return next;
}

bool RoomRoster::renameRole(const std::uint64_t role_id, std::string name) {
    const auto canonical = canonicalNickname(name);
    if (!canonical.has_value() || findRole(state_, role_id) == nullptr || nameTaken(state_, *canonical, role_id))
        return false;
    for (auto& r : state_.roles)
        if (r.id == role_id)
            r.name = *canonical;
    return true;
}

bool RoomRoster::setRolePermissions(const std::uint64_t role_id, const RoomPermissionSet permissions) {
    for (auto& r : state_.roles) {
        if (r.id == role_id) {
            r.permissions = permissions;
            return true;
        }
    }
    return false;
}

bool RoomRoster::removeRole(const std::uint64_t role_id, const std::uint64_t fallback_role_id) {
    if (role_id == fallback_role_id || findRole(state_, role_id) == nullptr ||
        findRole(state_, fallback_role_id) == nullptr)
        return false;
    for (auto& l : state_.listeners)
        if (l.role_id == role_id)
            l.role_id = fallback_role_id;
    if (state_.invite.has_value() && state_.invite->default_role_id == role_id)
        state_.invite->default_role_id = fallback_role_id;
    std::erase_if(state_.roles, [role_id](const RoomRole& r) { return r.id == role_id; });
    return true;
}

void RoomRoster::remember(const std::string_view fingerprint, const std::string_view nickname,
                          const std::uint64_t role_id) {
    if (fingerprint.empty())
        return;
    const auto canonical = canonicalNickname(nickname);
    if (auto* existing = findMutable(state_, fingerprint)) {
        if (canonical.has_value())
            existing->last_nickname = *canonical;
        return;
    }
    if (findRole(state_, role_id) == nullptr)
        return;
    state_.listeners.push_back({std::string{fingerprint}, canonical.value_or(std::string{}), role_id, {}, false});
}

bool RoomRoster::assignRole(const std::string_view fingerprint, const std::uint64_t role_id) {
    auto* l = findMutable(state_, fingerprint);
    if (l == nullptr || findRole(state_, role_id) == nullptr)
        return false;
    l->role_id = role_id;
    return true;
}

bool RoomRoster::setOverride(const std::string_view fingerprint, const RoomPermission permission,
                             const std::optional<bool> allowed) {
    auto* l = findMutable(state_, fingerprint);
    if (l == nullptr || static_cast<std::size_t>(permission) >= room_permission_count)
        return false;
    std::erase_if(l->overrides, [permission](const PermissionOverride& o) { return o.permission == permission; });
    if (allowed.has_value())
        l->overrides.push_back({permission, *allowed});
    return true;
}

bool RoomRoster::setBlocked(const std::string_view fingerprint, const bool blocked) {
    auto* l = findMutable(state_, fingerprint);
    if (l == nullptr)
        return false;
    l->blocked = blocked;
    return true;
}

const KnownListener* RoomRoster::find(const std::string_view fingerprint) const noexcept {
    const auto it = std::find_if(state_.listeners.begin(), state_.listeners.end(),
                                 [&](const KnownListener& l) { return l.fingerprint == fingerprint; });
    return it == state_.listeners.end() ? nullptr : &*it;
}

RoomPermissionSet RoomRoster::effectivePermissions(const std::string_view fingerprint) const {
    const auto* l = find(fingerprint);
    if (l == nullptr || l->blocked)
        return {};
    const auto* role = findRole(state_, l->role_id);
    RoomPermissionSet result = role != nullptr ? role->permissions : RoomPermissionSet{};
    for (const auto& o : l->overrides)
        result.set(static_cast<std::size_t>(o.permission), o.allowed);
    return result;
}

} // namespace music_player::m0
