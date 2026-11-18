// Smoke use of the in-memory transport fake; compiling it in the core suite also proves that
// m0/ports/RoomTransport.hpp stays free of Qt and libdatachannel.
#include "../support/FakeRoomTransport.hpp"

#include <catch2/catch_test_macros.hpp>

#include <variant>
#include <vector>

namespace music_player::m0::test {

TEST_CASE("fake room links deliver both ways and report a remote close", "[core][listening-room][transport]") {
    auto [host, guest] = testing::FakeRoomLink::pair("fp-host", "fp-guest");
    REQUIRE(host->peerFingerprint() == "fp-guest");
    REQUIRE(guest->peerFingerprint() == "fp-host");

    std::vector<RoomControlMessage> at_host;
    std::vector<RoomFileChunk> at_guest;
    std::vector<RoomLinkFailure> guest_closed;
    host->onControl = [&](RoomControlMessage m) { at_host.push_back(std::move(m)); };
    guest->onMedia = [&](RoomFileChunk c) { at_guest.push_back(std::move(c)); };
    guest->onClosed = [&](RoomLinkFailure f) { guest_closed.push_back(f); };

    REQUIRE(guest->sendControl(RoomBye{RoomByeReason::Closed}));
    REQUIRE(host->sendMedia(RoomFileChunk{7, 0, {1, 2, 3}}));
    REQUIRE(at_host.size() == 1);
    REQUIRE(std::get<RoomBye>(at_host[0]).reason == RoomByeReason::Closed);
    REQUIRE(at_guest == std::vector<RoomFileChunk>{RoomFileChunk{7, 0, {1, 2, 3}}});

    host->close();
    REQUIRE(guest_closed == std::vector<RoomLinkFailure>{RoomLinkFailure::Closed});
    REQUIRE_FALSE(host->sendControl(RoomBye{}));
    REQUIRE_FALSE(guest->sendMedia(RoomFileChunk{}));
}

TEST_CASE("fake host endpoint and guest connector hand out links on demand", "[core][listening-room][transport]") {
    testing::FakeRoomHostEndpoint endpoint;
    testing::FakeRoomGuestConnector connector;
    RoomInviteLink invite;
    invite.room_id[0] = 1;
    std::vector<std::shared_ptr<RoomLink>> incoming, connected;
    std::vector<RoomLinkFailure> failures;
    endpoint.open(invite, [&](auto link) { incoming.push_back(link); }, [&](auto f) { failures.push_back(f); });
    connector.connect(invite, [&](auto link) { connected.push_back(link); }, [&](auto f) { failures.push_back(f); });
    REQUIRE(endpoint.opened == invite);
    REQUIRE(connector.requested == invite);

    auto [host_side, guest_side] = testing::FakeRoomLink::pair("fp-host", "fp-guest");
    endpoint.admit(host_side);
    connector.complete(guest_side);
    REQUIRE(incoming.size() == 1);
    REQUIRE(connected.size() == 1);
    REQUIRE(incoming[0]->peerFingerprint() == "fp-guest");

    endpoint.closeAll();
    REQUIRE(failures.empty());
    REQUIRE(endpoint.closed_all);
}

TEST_CASE("fake guest connector calls exactly one callback once and nothing after cancel",
          "[core][listening-room][transport]") {
    testing::FakeRoomGuestConnector connector;
    RoomInviteLink invite;
    std::vector<std::shared_ptr<RoomLink>> connected;
    std::vector<RoomLinkFailure> failures;
    const auto connect = [&] {
        connector.connect(
            invite, [&](auto link) { connected.push_back(link); }, [&](auto f) { failures.push_back(f); });
    };
    auto [host_side, guest_side] = testing::FakeRoomLink::pair("fp-host", "fp-guest");

    connect();
    connector.complete(guest_side);
    connector.fail(RoomLinkFailure::HostOffline);
    connector.complete(guest_side);
    REQUIRE(connected.size() == 1);
    REQUIRE(failures.empty());

    connect();
    connector.cancel();
    REQUIRE(connector.cancelled);
    connector.fail(RoomLinkFailure::Unreachable);
    connector.complete(guest_side);
    REQUIRE(connected.size() == 1);
    REQUIRE(failures.empty());

    connect();
    connector.fail(RoomLinkFailure::Refused);
    connector.fail(RoomLinkFailure::Refused);
    REQUIRE(failures == std::vector<RoomLinkFailure>{RoomLinkFailure::Refused});

    testing::FakeRoomGuestConnector never_connected;
    never_connected.complete(guest_side);
    never_connected.fail(RoomLinkFailure::Closed);
}

TEST_CASE("fake host endpoint delivers nothing when closed or never opened", "[core][listening-room][transport]") {
    testing::FakeRoomHostEndpoint endpoint;
    auto [host_side, guest_side] = testing::FakeRoomLink::pair("fp-host", "fp-guest");
    endpoint.admit(host_side);
    endpoint.fail(RoomLinkFailure::SignalingUnavailable);

    std::vector<std::shared_ptr<RoomLink>> incoming;
    std::vector<RoomLinkFailure> failures;
    endpoint.open(
        RoomInviteLink{}, [&](auto link) { incoming.push_back(link); }, [&](auto f) { failures.push_back(f); });
    endpoint.closeAll();
    endpoint.admit(host_side);
    endpoint.fail(RoomLinkFailure::SignalingUnavailable);
    REQUIRE(incoming.empty());
    REQUIRE(failures.empty());

    endpoint.open(
        RoomInviteLink{}, [&](auto link) { incoming.push_back(link); }, [&](auto f) { failures.push_back(f); });
    endpoint.fail(RoomLinkFailure::SignalingUnavailable);
    endpoint.admit(host_side);
    REQUIRE(failures == std::vector<RoomLinkFailure>{RoomLinkFailure::SignalingUnavailable});
    REQUIRE(incoming.empty());
}

TEST_CASE("fake room link can refuse a control message like the codec-checking adapter",
          "[core][listening-room][transport]") {
    auto [host, guest] = testing::FakeRoomLink::pair("fp-host", "fp-guest");
    std::vector<RoomControlMessage> at_guest;
    guest->onControl = [&](RoomControlMessage m) { at_guest.push_back(std::move(m)); };
    host->accepts_control = [](const RoomControlMessage& m) { return !std::holds_alternative<RoomStateMessage>(m); };
    REQUIRE_FALSE(host->sendControl(RoomStateMessage{}));
    REQUIRE(host->sendControl(RoomBye{}));
    REQUIRE(at_guest.size() == 1);
    REQUIRE(host->sent_control.size() == 1);
}

} // namespace music_player::m0::test
