#include "m0/domain/ListeningRoomLimits.hpp"
#include "m0/domain/RoomRoster.hpp"

#include <catch2/catch_test_macros.hpp>

namespace music_player::m0::test {
namespace {
constexpr auto bit(const RoomPermission p) { return static_cast<std::size_t>(p); }
RoomRoster withAna() {
    RoomRoster roster;
    roster.remember("fp-ana", "Ana", 2);
    return roster;
}
} // namespace

TEST_CASE("default roster has Ouvinte without permissions and DJ with all", "[core][listening-room][roster]") {
    const auto s = defaultRoomRoster();
    REQUIRE(s.roles.size() == 2);
    REQUIRE(s.roles[0].id == 1);
    REQUIRE(s.roles[0].name == "Ouvinte");
    REQUIRE(s.roles[0].permissions.none());
    REQUIRE(s.roles[1].id == 2);
    REQUIRE(s.roles[1].name == "DJ");
    REQUIRE(s.roles[1].permissions.all());
    REQUIRE(s.listeners.empty());
    REQUIRE_FALSE(s.invite.has_value());
}

TEST_CASE("overrides adjust role permissions per person", "[core][listening-room][roster]") {
    auto roster = withAna();
    REQUIRE(roster.setOverride("fp-ana", RoomPermission::Pause, false));
    auto p = roster.effectivePermissions("fp-ana");
    REQUIRE_FALSE(p.test(bit(RoomPermission::Pause)));
    REQUIRE(p.test(bit(RoomPermission::Skip)));

    roster.remember("fp-bia", "Bia", 1);
    REQUIRE(roster.setOverride("fp-bia", RoomPermission::UploadOwnTracks, true));
    p = roster.effectivePermissions("fp-bia");
    REQUIRE(p.count() == 1);
    REQUIRE(p.test(bit(RoomPermission::UploadOwnTracks)));
}

TEST_CASE("clearing an override restores the role permission", "[core][listening-room][roster]") {
    auto roster = withAna();
    REQUIRE(roster.setOverride("fp-ana", RoomPermission::Pause, false));
    REQUIRE(roster.setOverride("fp-ana", RoomPermission::Pause, std::nullopt));
    REQUIRE(roster.find("fp-ana")->overrides.empty());
    REQUIRE(roster.effectivePermissions("fp-ana").all());
    REQUIRE_FALSE(roster.setOverride("nobody", RoomPermission::Pause, true));
}

TEST_CASE("removing a role moves people to the fallback keeping overrides", "[core][listening-room][roster]") {
    auto roster = withAna();
    REQUIRE(roster.setOverride("fp-ana", RoomPermission::Skip, true));
    REQUIRE(roster.removeRole(2, 1));
    REQUIRE(roster.find("fp-ana")->role_id == 1);
    REQUIRE(roster.find("fp-ana")->overrides.size() == 1);
    REQUIRE(roster.snapshot().roles.size() == 1);
    REQUIRE(roster.effectivePermissions("fp-ana").count() == 1);
}

TEST_CASE("invalid role removals and names are rejected", "[core][listening-room][roster]") {
    RoomRoster roster;
    REQUIRE_FALSE(roster.removeRole(2, 2));
    REQUIRE_FALSE(roster.removeRole(2, 99));
    REQUIRE_FALSE(roster.removeRole(99, 1));
    REQUIRE(roster.removeRole(2, 1));
    REQUIRE_FALSE(roster.removeRole(1, 1)); // last role
    REQUIRE_FALSE(roster.createRole("", {}).has_value());
    REQUIRE_FALSE(roster.createRole("   ", {}).has_value());
    REQUIRE_FALSE(roster.createRole("Ouvinte", {}).has_value());
    REQUIRE_FALSE(roster.createRole(" Ouvinte ", {}).has_value());
    REQUIRE_FALSE(roster.createRole("a\nb", {}).has_value());
    REQUIRE_FALSE(roster.createRole(std::string(maximum_nickname_code_points + 1, 'x'), {}).has_value());
    REQUIRE_FALSE(roster.renameRole(1, ""));
    REQUIRE_FALSE(roster.renameRole(99, "X"));
    const auto id = roster.createRole("Mod", {});
    REQUIRE(id.has_value());
    REQUIRE_FALSE(roster.renameRole(*id, "Ouvinte"));
    REQUIRE(roster.renameRole(*id, "Mod"));
    REQUIRE(roster.renameRole(*id, "Moderador"));
    REQUIRE(roster.snapshot().roles.back().name == "Moderador");
}

TEST_CASE("role ids are never reused and permissions can change", "[core][listening-room][roster]") {
    RoomRoster roster;
    REQUIRE(roster.createRole("A", {}) == 3);
    REQUIRE(roster.createRole("C", {}) == 4);
    REQUIRE(roster.removeRole(3, 1));
    REQUIRE(roster.createRole("B", {}) == 5);
    RoomPermissionSet one;
    one.set(bit(RoomPermission::Pause));
    REQUIRE(roster.setRolePermissions(5, one));
    REQUIRE_FALSE(roster.setRolePermissions(99, one));
    roster.remember("fp", "Zed", 5);
    REQUIRE(roster.effectivePermissions("fp") == one);
}

TEST_CASE("unknown and blocked people have no permissions", "[core][listening-room][roster]") {
    auto roster = withAna();
    REQUIRE(roster.effectivePermissions("nobody").none());
    REQUIRE(roster.setBlocked("fp-ana", true));
    REQUIRE(roster.effectivePermissions("fp-ana").none());
    REQUIRE(roster.setBlocked("fp-ana", false));
    REQUIRE(roster.effectivePermissions("fp-ana").all());
    REQUIRE_FALSE(roster.setBlocked("nobody", true));
}

TEST_CASE("remember updates only the nickname of known people", "[core][listening-room][roster]") {
    auto roster = withAna();
    REQUIRE(roster.assignRole("fp-ana", 1));
    REQUIRE(roster.setBlocked("fp-ana", true));
    roster.remember("fp-ana", "Ana Maria", 2);
    const auto* ana = roster.find("fp-ana");
    REQUIRE(ana->last_nickname == "Ana Maria");
    REQUIRE(ana->role_id == 1);
    REQUIRE(ana->blocked);
    roster.remember("fp-x", "X", 99);
    REQUIRE(roster.find("fp-x") == nullptr);
    REQUIRE_FALSE(roster.assignRole("fp-ana", 99));
}

TEST_CASE("restore validates the whole snapshot", "[core][listening-room][roster]") {
    auto roster = withAna();
    const auto before = roster.snapshot();

    auto bad = before;
    bad.listeners[0].role_id = 42;
    REQUIRE_FALSE(roster.restore(bad));
    REQUIRE(roster.find("fp-ana")->role_id == 2);

    bad = before;
    bad.roles.clear();
    REQUIRE_FALSE(roster.restore(bad));
    bad = before;
    bad.roles[1].name = "Ouvinte";
    REQUIRE_FALSE(roster.restore(bad));
    bad = before;
    bad.roles[1].id = 1;
    REQUIRE_FALSE(roster.restore(bad));
    bad = before;
    bad.listeners.push_back(bad.listeners[0]);
    REQUIRE_FALSE(roster.restore(bad));
    bad = before;
    bad.listeners[0].overrides = {{RoomPermission::Skip, true}, {RoomPermission::Skip, false}};
    REQUIRE_FALSE(roster.restore(bad));
    bad = before;
    bad.invite = PersistedInvite{{}, {}, maximum_room_listeners + 1, 1};
    REQUIRE_FALSE(roster.restore(bad));
    bad.invite = PersistedInvite{{}, {}, 5, 77};
    REQUIRE_FALSE(roster.restore(bad));
    REQUIRE(roster.snapshot().listeners.size() == 1);
    REQUIRE(roster.snapshot().roles.size() == 2);

    auto good = before;
    good.invite = PersistedInvite{{}, {}, 5, 1};
    good.listeners[0].overrides = {{RoomPermission::Pause, false}};
    REQUIRE(roster.restore(good));
    REQUIRE(roster.snapshot().invite->default_role_id == 1);
    REQUIRE_FALSE(roster.effectivePermissions("fp-ana").test(bit(RoomPermission::Pause)));
}

} // namespace music_player::m0::test
