#pragma once

#include "m0/domain/ListenerProfile.hpp"
#include "m0/domain/RoomRoster.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace music_player::m0 {

enum class MemberState : std::uint8_t { Pending, Admitted };

struct RoomMember final {
    std::string fingerprint{};
    std::string nickname{};
    AvatarChoice avatar{};
    MemberState state{};
    bool operator==(const RoomMember&) const = default;
};

enum class RoomItemOrigin : std::uint8_t { HostTrack, GuestTrack, YoutubeVideo };

struct RoomTrackSummary final {
    std::string title{};
    std::string artist{};
    std::string album{};
    std::int64_t duration_ms{};
    bool operator==(const RoomTrackSummary&) const = default;
};

struct RoomItem final {
    std::uint64_t id{};
    RoomItemOrigin origin{};
    std::string owner_fingerprint{};
    std::string content_sha256{};
    std::uint64_t size_bytes{};
    RoomTrackSummary summary{};
    std::string youtube_id{};
    std::string added_by{};
    bool operator==(const RoomItem&) const = default;
};

struct RoomPlayback final {
    std::optional<std::uint64_t> current_item{};
    bool playing{};
    std::int64_t anchor_host_ms{};
    std::int64_t anchor_position_ms{};
    bool operator==(const RoomPlayback&) const = default;
};

struct RoomSnapshot final {
    std::uint64_t version{};
    std::vector<RoomMember> members{};
    std::vector<RoomItem> queue{};
    RoomPlayback playback{};
    bool operator==(const RoomSnapshot&) const = default;
};

// RejectedClosed is produced by the application layer, never by the domain.
enum class JoinDecision : std::uint8_t { Admitted, Pending, RejectedFull, RejectedBlocked, RejectedClosed };

enum class RoomIntentKind : std::uint8_t {
    Next,
    Previous,
    Seek,
    Pause,
    Resume,
    AddItems,
    AddPlaylist,
    RemoveItem,
    MoveItem
};

struct RoomIntent final {
    RoomIntentKind kind{};
    std::int64_t position_ms{};
    std::uint64_t item_id{};
    std::size_t destination{};
    std::uint64_t playlist_id{};
    std::vector<std::uint64_t> catalog_entry_ids{};
    std::optional<RoomItem> uploaded{};
    bool operator==(const RoomIntent&) const = default;
};

enum class IntentRejection : std::uint8_t { NotAdmitted, MissingPermission, UnknownItem, InvalidArgument, QueueFull };

[[nodiscard]] RoomPermission requiredPermission(const RoomIntent& intent) noexcept;

// Host-side authority over who is in the room and whether an intent is allowed.
// The roster is borrowed and must outlive the room. max_listeners is clamped into
// minimum_room_listeners..maximum_room_listeners.
class ListeningRoom final {
  public:
    ListeningRoom(std::size_t max_listeners, std::uint64_t default_role_id, RoomRoster& roster);

    // Already-present fingerprint (pending or admitted): keeps one entry, updates nickname/avatar and
    // returns its current state as Admitted/Pending. Blocked -> RejectedBlocked. Known -> Admitted
    // (RejectedFull at capacity). New -> Pending (capacity is checked on approve).
    [[nodiscard]] JoinDecision requestJoin(const RoomMember& candidate);
    // Pending -> Admitted and remembered in the roster with the default role. False when not pending
    // or when admittedCount() == max_listeners.
    [[nodiscard]] bool approve(std::string_view fingerprint);
    // Removes a pending person without blocking.
    [[nodiscard]] bool reject(std::string_view fingerprint);
    void leave(std::string_view fingerprint);
    // Removes a member; block=true leaves them blocked in the roster. False when not a member.
    [[nodiscard]] bool kick(std::string_view fingerprint, bool block);
    // Checks the intent against the current snapshot and roster permissions at call time.
    [[nodiscard]] std::optional<IntentRejection> authorize(std::string_view fingerprint, const RoomIntent& intent,
                                                           const RoomSnapshot& current) const;
    [[nodiscard]] std::size_t admittedCount() const noexcept;
    [[nodiscard]] std::vector<RoomMember> members() const;
    // The role people approved from now on are remembered with (the invite's initial role).
    [[nodiscard]] std::uint64_t defaultRoleId() const noexcept;
    void setDefaultRole(std::uint64_t role_id) noexcept;

  private:
    std::size_t max_listeners_;
    std::uint64_t default_role_id_;
    RoomRoster& roster_;
    std::vector<RoomMember> members_;
};

} // namespace music_player::m0
