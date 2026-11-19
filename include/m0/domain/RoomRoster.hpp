#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace music_player::m0 {

enum class RoomPermission : std::uint8_t {
    Skip,
    Pause,
    AddFromRoom,
    AddPlaylist,
    EditQueue,
    UploadOwnTracks,
    AddYoutube
};
inline constexpr std::size_t room_permission_count = 7;
using RoomPermissionSet = std::bitset<room_permission_count>;

struct RoomRole final {
    std::uint64_t id{};
    std::string name{};
    RoomPermissionSet permissions{};
};

struct PermissionOverride final {
    RoomPermission permission{};
    bool allowed{};
};

struct KnownListener final {
    std::string fingerprint{};
    std::string last_nickname{};
    std::uint64_t role_id{};
    std::vector<PermissionOverride> overrides{};
    bool blocked{};
};

struct PersistedInvite final {
    std::array<std::uint8_t, 16> room_id{};
    std::array<std::uint8_t, 16> secret{};
    std::size_t max_listeners{};
    std::uint64_t default_role_id{};
};

struct RoomRosterSnapshot final {
    std::vector<RoomRole> roles{};
    std::vector<KnownListener> listeners{};
    std::optional<PersistedInvite> invite{};
};

// Roles: {1, "Ouvinte", none} and {2, "DJ", all}. No known listeners, no invite.
[[nodiscard]] RoomRosterSnapshot defaultRoomRoster();

// Roles, per-person overrides, known people, blocks and the persistent invite of a room host.
// Role names are single-line text (same policy as nicknames), unique case-sensitively after trimming.
class RoomRoster final {
  public:
    RoomRoster();

    // Validates the whole snapshot; on failure returns false and keeps the previous state.
    [[nodiscard]] bool restore(RoomRosterSnapshot snapshot);
    [[nodiscard]] RoomRosterSnapshot snapshot() const;

    [[nodiscard]] std::optional<std::uint64_t> createRole(std::string name, RoomPermissionSet permissions);
    [[nodiscard]] bool renameRole(std::uint64_t role_id, std::string name);
    [[nodiscard]] bool setRolePermissions(std::uint64_t role_id, RoomPermissionSet permissions);
    // Moves people (and the invite default) from role_id to fallback_role_id, keeping overrides.
    [[nodiscard]] bool removeRole(std::uint64_t role_id, std::uint64_t fallback_role_id);

    // New fingerprint: added when role_id exists (ignored otherwise). Existing: only last_nickname changes.
    void remember(std::string_view fingerprint, std::string_view nickname, std::uint64_t role_id);
    [[nodiscard]] bool assignRole(std::string_view fingerprint, std::uint64_t role_id);
    [[nodiscard]] bool setOverride(std::string_view fingerprint, RoomPermission permission,
                                   std::optional<bool> allowed);
    [[nodiscard]] bool setBlocked(std::string_view fingerprint, bool blocked);
    // The person is no longer known (role and overrides go too): next time they wait for approval.
    [[nodiscard]] bool forget(std::string_view fingerprint);
    [[nodiscard]] const KnownListener* find(std::string_view fingerprint) const noexcept;
    // Empty for unknown or blocked people.
    [[nodiscard]] RoomPermissionSet effectivePermissions(std::string_view fingerprint) const;

  private:
    RoomRosterSnapshot state_;
};

} // namespace music_player::m0
