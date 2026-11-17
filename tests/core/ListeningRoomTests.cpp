#include "m0/domain/ListeningRoom.hpp"
#include "m0/domain/ListeningRoomLimits.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace music_player::m0::test {
namespace {
constexpr std::uint64_t listener_role = 1;
constexpr std::uint64_t dj_role = 2;

RoomMember candidate(const std::string& fp, const std::string& nick = "Ana") {
    RoomMember m;
    m.fingerprint = fp;
    m.nickname = nick;
    return m;
}

// Joins a new person and approves them.
void admitNew(ListeningRoom& room, const std::string& fp) {
    REQUIRE(room.requestJoin(candidate(fp)) == JoinDecision::Pending);
    REQUIRE(room.approve(fp));
}

RoomSnapshot snapshotWithItems(const std::size_t count) {
    RoomSnapshot s;
    for (std::size_t i = 0; i < count; ++i) {
        RoomItem item;
        item.id = i + 1;
        s.queue.push_back(item);
    }
    return s;
}

RoomIntent intent(const RoomIntentKind kind) {
    RoomIntent i;
    i.kind = kind;
    return i;
}
} // namespace

TEST_CASE("join: unknown people wait for approval and are remembered on approve", "[core][listening-room][room]") {
    RoomRoster roster;
    ListeningRoom room(4, listener_role, roster);
    REQUIRE(room.requestJoin(candidate("fp-a")) == JoinDecision::Pending);
    REQUIRE(room.admittedCount() == 0);
    REQUIRE(roster.find("fp-a") == nullptr);
    REQUIRE(room.approve("fp-a"));
    REQUIRE(room.admittedCount() == 1);
    const auto* known = roster.find("fp-a");
    REQUIRE(known != nullptr);
    REQUIRE(known->role_id == listener_role);
    REQUIRE(known->last_nickname == "Ana");
    REQUIRE_FALSE(room.approve("fp-a"));
    REQUIRE_FALSE(room.approve("nobody"));
}

TEST_CASE("join: known people enter directly with their stored role; blocked are refused",
          "[core][listening-room][room]") {
    RoomRoster roster;
    roster.remember("fp-dj", "Dee", dj_role);
    roster.remember("fp-bad", "Bad", listener_role);
    REQUIRE(roster.setBlocked("fp-bad", true));
    ListeningRoom room(4, listener_role, roster);
    REQUIRE(room.requestJoin(candidate("fp-dj")) == JoinDecision::Admitted);
    REQUIRE(roster.find("fp-dj")->role_id == dj_role);
    REQUIRE(room.requestJoin(candidate("fp-bad")) == JoinDecision::RejectedBlocked);
    REQUIRE(room.members().size() == 1);
}

TEST_CASE("join: repeated requests keep a single entry", "[core][listening-room][room]") {
    RoomRoster roster;
    ListeningRoom room(4, listener_role, roster);
    REQUIRE(room.requestJoin(candidate("fp-a", "Ana")) == JoinDecision::Pending);
    auto again = candidate("fp-a", "Ana Maria");
    again.avatar_sha256 = "abc";
    REQUIRE(room.requestJoin(again) == JoinDecision::Pending);
    REQUIRE(room.members().size() == 1);
    REQUIRE(room.members()[0].nickname == "Ana Maria");
    REQUIRE(room.members()[0].avatar_sha256 == "abc");
    REQUIRE(room.approve("fp-a"));
    REQUIRE(room.requestJoin(candidate("fp-a")) == JoinDecision::Admitted);
    REQUIRE(room.members().size() == 1);
    REQUIRE(room.admittedCount() == 1);
}

TEST_CASE("capacity counts only admitted people", "[core][listening-room][room]") {
    RoomRoster roster;
    roster.remember("fp-known", "K", listener_role);
    ListeningRoom room(2, listener_role, roster);
    admitNew(room, "fp-1");
    admitNew(room, "fp-2");
    REQUIRE(room.requestJoin(candidate("fp-known")) == JoinDecision::RejectedFull);
    REQUIRE(room.requestJoin(candidate("fp-p1")) == JoinDecision::Pending);
    REQUIRE(room.requestJoin(candidate("fp-p2")) == JoinDecision::Pending);
    REQUIRE_FALSE(room.approve("fp-p1"));
    room.leave("fp-1");
    REQUIRE(room.approve("fp-p1"));
    REQUIRE_FALSE(room.approve("fp-p2"));
    REQUIRE(room.admittedCount() == 2);
}

TEST_CASE("max listeners is clamped into the supported range", "[core][listening-room][room]") {
    RoomRoster roster;
    ListeningRoom none(0, listener_role, roster);
    admitNew(none, "fp-1");
    REQUIRE(none.requestJoin(candidate("fp-2")) == JoinDecision::Pending);
    REQUIRE_FALSE(none.approve("fp-2"));

    ListeningRoom many(1000, listener_role, roster);
    for (std::uint32_t i = 0; i < maximum_room_listeners; ++i)
        admitNew(many, "m-" + std::to_string(i));
    REQUIRE(many.requestJoin(candidate("extra")) == JoinDecision::Pending);
    REQUIRE_FALSE(many.approve("extra"));
}

TEST_CASE("reject removes a pending person without blocking", "[core][listening-room][room]") {
    RoomRoster roster;
    ListeningRoom room(2, listener_role, roster);
    REQUIRE(room.requestJoin(candidate("fp-a")) == JoinDecision::Pending);
    REQUIRE(room.reject("fp-a"));
    REQUIRE(room.members().empty());
    REQUIRE(roster.find("fp-a") == nullptr);
    REQUIRE_FALSE(room.reject("fp-a"));
    admitNew(room, "fp-b");
    REQUIRE_FALSE(room.reject("fp-b"));
}

TEST_CASE("kick removes a member and can block them", "[core][listening-room][room]") {
    RoomRoster roster;
    ListeningRoom room(3, listener_role, roster);
    admitNew(room, "fp-a");
    REQUIRE(room.kick("fp-a", false));
    REQUIRE(room.members().empty());
    REQUIRE_FALSE(roster.find("fp-a")->blocked);
    REQUIRE(room.requestJoin(candidate("fp-a")) == JoinDecision::Admitted);
    REQUIRE(room.kick("fp-a", true));
    REQUIRE(roster.find("fp-a")->blocked);
    REQUIRE(room.requestJoin(candidate("fp-a")) == JoinDecision::RejectedBlocked);
    REQUIRE_FALSE(room.kick("fp-a", true));
}

TEST_CASE("kick with block of a pending, unknown person still blocks", "[core][listening-room][room]") {
    RoomRoster roster;
    ListeningRoom room(3, listener_role, roster);
    REQUIRE(room.requestJoin(candidate("fp-p")) == JoinDecision::Pending);
    REQUIRE(room.kick("fp-p", true));
    REQUIRE(room.members().empty());
    REQUIRE(roster.find("fp-p") != nullptr);
    REQUIRE(roster.find("fp-p")->blocked);
}

TEST_CASE("requiredPermission maps each intent", "[core][listening-room][room]") {
    REQUIRE(requiredPermission(intent(RoomIntentKind::Next)) == RoomPermission::Skip);
    REQUIRE(requiredPermission(intent(RoomIntentKind::Previous)) == RoomPermission::Skip);
    REQUIRE(requiredPermission(intent(RoomIntentKind::Seek)) == RoomPermission::Skip);
    REQUIRE(requiredPermission(intent(RoomIntentKind::Pause)) == RoomPermission::Pause);
    REQUIRE(requiredPermission(intent(RoomIntentKind::Resume)) == RoomPermission::Pause);
    REQUIRE(requiredPermission(intent(RoomIntentKind::AddItems)) == RoomPermission::AddFromRoom);
    auto upload = intent(RoomIntentKind::AddItems);
    upload.uploaded = RoomItem{};
    REQUIRE(requiredPermission(upload) == RoomPermission::UploadOwnTracks);
    REQUIRE(requiredPermission(intent(RoomIntentKind::AddPlaylist)) == RoomPermission::AddPlaylist);
    REQUIRE(requiredPermission(intent(RoomIntentKind::RemoveItem)) == RoomPermission::EditQueue);
    REQUIRE(requiredPermission(intent(RoomIntentKind::MoveItem)) == RoomPermission::EditQueue);
}

TEST_CASE("authorize: admission and permissions come first", "[core][listening-room][room]") {
    RoomRoster roster;
    roster.remember("fp-dj", "Dee", dj_role);
    ListeningRoom room(4, listener_role, roster);
    const auto snap = snapshotWithItems(2);
    REQUIRE(room.requestJoin(candidate("fp-pending")) == JoinDecision::Pending);
    REQUIRE(room.authorize("fp-pending", intent(RoomIntentKind::Pause), snap) == IntentRejection::NotAdmitted);
    REQUIRE(room.authorize("stranger", intent(RoomIntentKind::Pause), snap) == IntentRejection::NotAdmitted);
    admitNew(room, "fp-l");
    REQUIRE(room.authorize("fp-l", intent(RoomIntentKind::Next), snap) == IntentRejection::MissingPermission);
    REQUIRE(room.requestJoin(candidate("fp-dj")) == JoinDecision::Admitted);
    REQUIRE_FALSE(room.authorize("fp-dj", intent(RoomIntentKind::Next), snap).has_value());
    // Permission is checked before the item.
    auto remove = intent(RoomIntentKind::RemoveItem);
    remove.item_id = 99;
    REQUIRE(room.authorize("fp-l", remove, snap) == IntentRejection::MissingPermission);
}

TEST_CASE("authorize: queue edits are checked against the current snapshot", "[core][listening-room][room]") {
    RoomRoster roster;
    roster.remember("fp-dj", "Dee", dj_role);
    ListeningRoom room(4, listener_role, roster);
    REQUIRE(room.requestJoin(candidate("fp-dj")) == JoinDecision::Admitted);
    auto snap = snapshotWithItems(3);

    auto remove = intent(RoomIntentKind::RemoveItem);
    remove.item_id = 99;
    REQUIRE(room.authorize("fp-dj", remove, snap) == IntentRejection::UnknownItem);

    // Review focus 2: the same removal, replayed after the item left the snapshot.
    remove.item_id = 2;
    REQUIRE_FALSE(room.authorize("fp-dj", remove, snap).has_value());
    snap.queue.erase(snap.queue.begin() + 1);
    REQUIRE(room.authorize("fp-dj", remove, snap) == IntentRejection::UnknownItem);

    auto move = intent(RoomIntentKind::MoveItem);
    move.item_id = 1;
    move.destination = 1;
    REQUIRE_FALSE(room.authorize("fp-dj", move, snap).has_value());
    move.destination = 2;
    REQUIRE(room.authorize("fp-dj", move, snap) == IntentRejection::InvalidArgument);
    move.item_id = 42;
    move.destination = 0;
    REQUIRE(room.authorize("fp-dj", move, snap) == IntentRejection::UnknownItem);
}

TEST_CASE("authorize: permissions are read at call time", "[core][listening-room][room]") {
    RoomRoster roster;
    roster.remember("fp-dj", "Dee", dj_role);
    ListeningRoom room(4, listener_role, roster);
    REQUIRE(room.requestJoin(candidate("fp-dj")) == JoinDecision::Admitted);
    const auto snap = snapshotWithItems(1);
    REQUIRE_FALSE(room.authorize("fp-dj", intent(RoomIntentKind::Next), snap).has_value());
    REQUIRE(roster.setOverride("fp-dj", RoomPermission::Skip, false));
    REQUIRE(room.authorize("fp-dj", intent(RoomIntentKind::Next), snap) == IntentRejection::MissingPermission);
}

TEST_CASE("authorize: seek and add arguments", "[core][listening-room][room]") {
    RoomRoster roster;
    roster.remember("fp-dj", "Dee", dj_role);
    ListeningRoom room(4, listener_role, roster);
    REQUIRE(room.requestJoin(candidate("fp-dj")) == JoinDecision::Admitted);
    const auto snap = snapshotWithItems(0);

    auto seek = intent(RoomIntentKind::Seek);
    seek.position_ms = -1;
    REQUIRE(room.authorize("fp-dj", seek, snap) == IntentRejection::InvalidArgument);
    seek.position_ms = 0;
    REQUIRE_FALSE(room.authorize("fp-dj", seek, snap).has_value());

    auto add = intent(RoomIntentKind::AddItems);
    REQUIRE(room.authorize("fp-dj", add, snap) == IntentRejection::InvalidArgument);
    add.catalog_entry_ids = {1, 2};
    REQUIRE_FALSE(room.authorize("fp-dj", add, snap).has_value());
}

TEST_CASE("authorize: a full queue refuses additions", "[core][listening-room][room]") {
    RoomRoster roster;
    roster.remember("fp-dj", "Dee", dj_role);
    ListeningRoom room(4, listener_role, roster);
    REQUIRE(room.requestJoin(candidate("fp-dj")) == JoinDecision::Admitted);

    auto add = intent(RoomIntentKind::AddItems);
    add.catalog_entry_ids = {7};
    REQUIRE(room.authorize("fp-dj", add, snapshotWithItems(maximum_room_queue_items)) == IntentRejection::QueueFull);
    REQUIRE_FALSE(room.authorize("fp-dj", add, snapshotWithItems(maximum_room_queue_items - 1)).has_value());
    add.catalog_entry_ids = {7, 8};
    REQUIRE(room.authorize("fp-dj", add, snapshotWithItems(maximum_room_queue_items - 1)) ==
            IntentRejection::QueueFull);
    auto upload = intent(RoomIntentKind::AddItems);
    upload.uploaded = RoomItem{};
    REQUIRE(room.authorize("fp-dj", upload, snapshotWithItems(maximum_room_queue_items)) == IntentRejection::QueueFull);
}

} // namespace music_player::m0::test
