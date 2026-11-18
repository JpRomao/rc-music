// ListeningRoomHost (S7.1) over the in-memory transport, the silent deterministic player, a fake
// catalog and an in-memory roster store. No network, files or audio.
#include "../support/FakeRoomHost.hpp"
#include "../support/FakeRoomTransport.hpp"

#include "m0/application/ListeningRoomHost.hpp"
#include "m0/application/SessionAutosave.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace music_player::m0::test {
namespace {

constexpr std::array<std::uint8_t, 32> host_fingerprint{0xab, 0xcd, 0x01, 0x02};
const std::string host_hex = "abcd0102" + std::string(56, '0');

RoomPermissionSet allPermissions() { return RoomPermissionSet{}.set(); }

struct Guest final {
    std::shared_ptr<testing::FakeRoomLink> host_side;
    std::shared_ptr<testing::FakeRoomLink> link;
    std::vector<RoomControlMessage> inbox;
    std::vector<RoomLinkFailure> closed;

    template <typename T> [[nodiscard]] std::vector<T> all() const {
        std::vector<T> result;
        for (const auto& message : inbox)
            if (const auto* typed = std::get_if<T>(&message))
                result.push_back(*typed);
        return result;
    }
    [[nodiscard]] std::optional<RoomStateMessage> lastState() const {
        const auto states = all<RoomStateMessage>();
        return states.empty() ? std::nullopt : std::optional{states.back()};
    }
    void hello(std::string nickname = "Ana", const std::uint32_t version = room_protocol_version) {
        static_cast<void>(link->sendControl(RoomHello{version, std::move(nickname), AvatarChoice{}}));
    }
    void intent(const std::uint64_t request_id, RoomIntent intent) {
        static_cast<void>(link->sendControl(RoomIntentMessage{request_id, std::move(intent)}));
    }
};

struct HostFixture final {
    HostFixture() : HostFixture({"music/a.mp3", "music/b.mp3", "music/c.mp3"}) {}
    explicit HostFixture(std::vector<std::filesystem::path> sources) : player(std::move(sources)) {
        auto roster = defaultRoomRoster();
        roster.listeners.push_back(KnownListener{"fp-dj", "Dj", 2, {}, false});
        roster.listeners.push_back(KnownListener{"fp-listener", "Ouvinte", 1, {}, false});
        store.stored = roster;
        store.status = RoomRosterLoadStatus::Loaded;
    }

    void start() {
        host = std::make_unique<ListeningRoomHost>(ListeningRoomHostDependencies{
            .roster_store = store,
            .player = player,
            .endpoint = endpoint,
            .catalog = catalog,
            .host_clock_ms = [this] { return now; },
            .random_bytes =
                [this](std::span<std::uint8_t> bytes) {
                    for (auto& byte : bytes)
                        byte = next_random++;
                },
            .host_fingerprint = host_fingerprint,
            .host_profile = ListenerProfile{"Joao", AvatarChoice{}},
        });
        host->onJoinRequest = [this](const RoomMember& member) { join_requests.push_back(member); };
    }
    std::unique_ptr<Guest> connect(const std::string& fingerprint) {
        auto guest = std::make_unique<Guest>();
        auto [host_side, guest_side] = testing::FakeRoomLink::pair(host_hex, fingerprint);
        guest->host_side = host_side;
        guest->link = guest_side;
        guest_side->onControl = [raw = guest.get()](RoomControlMessage m) { raw->inbox.push_back(std::move(m)); };
        guest_side->onClosed = [raw = guest.get()](RoomLinkFailure f) { raw->closed.push_back(f); };
        endpoint.admit(host_side);
        return guest;
    }
    std::unique_ptr<Guest> join(const std::string& fingerprint, std::string nickname = "Ana") {
        auto guest = connect(fingerprint);
        guest->hello(std::move(nickname));
        return guest;
    }
    [[nodiscard]] bool isMember(const std::string& fingerprint) const {
        const auto members = host->snapshot().members;
        return std::any_of(members.begin(), members.end(),
                           [&](const RoomMember& m) { return m.fingerprint == fingerprint; });
    }

    testing::InMemoryRoomRosterStore store;
    testing::FakeRoomHostPlayer player;
    testing::FakeRoomHostEndpoint endpoint;
    testing::FakeRoomTrackCatalog catalog;
    std::int64_t now{10'000};
    std::uint8_t next_random{1};
    std::vector<RoomMember> join_requests;
    std::unique_ptr<ListeningRoomHost> host;
};

class RecordingSessionWriter final : public SessionWriter {
  public:
    SessionWriterCommand start(SessionWriteRequest request) override {
        writes.push_back(std::move(request));
        return SessionWriterCommand::Accepted;
    }
    std::optional<SessionWriteCompletion> poll() override { return std::nullopt; }
    std::optional<SessionWriteCompletion> flush(SessionWriteRequest request) override {
        writes.push_back(request);
        return SessionWriteCompletion{.revision = request.revision, .status = SessionSaveStatus::Saved};
    }
    bool shutdown() override { return true; }
    std::vector<SessionWriteRequest> writes;
};

} // namespace

TEST_CASE("room host: open makes a new link and until_revoked persists one that the next open reuses",
          "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    const auto first = f.host->open({});
    REQUIRE(first.has_value());
    REQUIRE(f.endpoint.opened == first);
    REQUIRE(first->host_fingerprint == host_fingerprint);
    REQUIRE_FALSE(f.store.stored.invite.has_value());
    f.host->close();

    const auto persistent = f.host->open({.max_listeners = 3, .until_revoked = true});
    REQUIRE(persistent.has_value());
    REQUIRE(persistent->room_id != first->room_id);
    REQUIRE(f.store.stored.invite.has_value());
    REQUIRE(f.store.stored.invite->room_id == persistent->room_id);
    REQUIRE(f.store.stored.invite->secret == persistent->secret);
    REQUIRE(f.store.stored.invite->max_listeners == 3);
    f.host->close();
    REQUIRE(f.store.stored.invite.has_value());

    REQUIRE(f.host->open({.until_revoked = true}) == persistent);
    f.host->close();
    f.host.reset();
    f.start();
    REQUIRE(f.host->open({.until_revoked = true}) == persistent);
}

TEST_CASE("room host: revoke forgets the invite, drops everyone and the next open makes another link",
          "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    const auto persistent = f.host->open({.until_revoked = true});
    auto dj = f.join("fp-dj");
    f.host->revoke();
    REQUIRE_FALSE(f.store.stored.invite.has_value());
    REQUIRE(f.endpoint.closed_all);
    REQUIRE(dj->all<RoomBye>() == std::vector<RoomBye>{RoomBye{RoomByeReason::Revoked}});
    REQUIRE(dj->link->closed);
    REQUIRE_FALSE(f.host->isOpen());

    const auto next = f.host->open({.until_revoked = true});
    REQUIRE(next.has_value());
    REQUIRE(next->room_id != persistent->room_id);
    REQUIRE(next->secret != persistent->secret);
}

TEST_CASE("room host: a persisted invite opens nothing until open() is called", "[core][listening-room][room-host]") {
    HostFixture f;
    PersistedInvite invite;
    invite.room_id.fill(7);
    invite.secret.fill(9);
    invite.max_listeners = 4;
    invite.default_role_id = 1;
    f.store.stored.invite = invite;
    f.start();
    REQUIRE(f.endpoint.open_count == 0);
    REQUIRE_FALSE(f.endpoint.opened.has_value());
    REQUIRE_FALSE(f.host->isOpen());
    const auto opened = f.host->open({.until_revoked = true});
    REQUIRE(f.endpoint.open_count == 1);
    REQUIRE(opened->room_id == invite.room_id);
}

TEST_CASE("room host: a known listener's hello is admitted and receives the state",
          "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj", "Bia");
    REQUIRE(dj->inbox.size() >= 2);
    REQUIRE(dj->inbox[0] == RoomControlMessage{RoomJoinResult{JoinDecision::Admitted}});
    const auto state = std::get<RoomStateMessage>(dj->inbox[1]);
    REQUIRE(state.my_permissions == allPermissions());
    REQUIRE(state.snapshot.members.size() == 2);
    REQUIRE(state.snapshot.members[0] == RoomMember{host_hex, "Joao", AvatarChoice{}, MemberState::Admitted});
    REQUIRE(state.snapshot.members[1] == RoomMember{"fp-dj", "Bia", AvatarChoice{}, MemberState::Admitted});
    REQUIRE(state.snapshot.queue.size() == 3);
    const auto& first = state.snapshot.queue[0];
    REQUIRE(first.id == f.player.occurrenceAt(0));
    REQUIRE(first.origin == RoomItemOrigin::HostTrack);
    REQUIRE(first.owner_fingerprint == host_hex);
    REQUIRE(first.added_by == "Joao");
    REQUIRE(first.content_sha256 == testing::FakeRoomTrackCatalog::fakeSha256("music/a.mp3"));
    REQUIRE(first.summary.title == "a");
    REQUIRE(state.snapshot.playback.current_item == first.id);
    REQUIRE(state.snapshot.playback.playing);
    REQUIRE(state.snapshot == f.host->snapshot());
}

TEST_CASE("room host: an unknown person waits, gets only Pending, and the state after approve",
          "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto stranger = f.join("fp-new", "Caio");
    REQUIRE(stranger->inbox == std::vector<RoomControlMessage>{RoomJoinResult{JoinDecision::Pending}});
    REQUIRE(f.join_requests.size() == 1);
    REQUIRE(f.join_requests[0].fingerprint == "fp-new");
    REQUIRE(f.join_requests[0].nickname == "Caio");
    REQUIRE(f.join_requests[0].state == MemberState::Pending);

    stranger->intent(1, RoomIntent{.kind = RoomIntentKind::Next});
    static_cast<void>(stranger->link->sendControl(RoomClockPing{5}));
    REQUIRE(stranger->inbox.size() == 1);
    REQUIRE(f.player.currentIndex() == 0);

    REQUIRE(f.host->approve("fp-new"));
    REQUIRE(stranger->inbox.size() >= 3);
    REQUIRE(stranger->inbox[1] == RoomControlMessage{RoomJoinResult{JoinDecision::Admitted}});
    const auto state = std::get<RoomStateMessage>(stranger->inbox[2]);
    REQUIRE(state.my_permissions == RoomPermissionSet{});
    REQUIRE(state.snapshot.queue.size() == 3);
    REQUIRE(std::any_of(f.store.stored.listeners.begin(), f.store.stored.listeners.end(),
                        [](const KnownListener& l) { return l.fingerprint == "fp-new" && l.role_id == 1; }));
}

TEST_CASE("room host: reject answers RejectedClosed and closes the link", "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto stranger = f.join("fp-new");
    REQUIRE(f.host->reject("fp-new"));
    REQUIRE(stranger->inbox.back() == RoomControlMessage{RoomJoinResult{JoinDecision::RejectedClosed}});
    REQUIRE(stranger->link->closed);
    REQUIRE_FALSE(f.isMember("fp-new"));
    REQUIRE_FALSE(f.host->reject("fp-new"));
}

TEST_CASE("room host: Next from a DJ advances the player and the version; from a listener it is refused",
          "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    auto listener = f.join("fp-listener", "Lia");
    const auto before = f.host->snapshot();

    listener->intent(41, RoomIntent{.kind = RoomIntentKind::Next});
    REQUIRE(listener->all<RoomIntentRejected>() ==
            std::vector<RoomIntentRejected>{RoomIntentRejected{41, IntentRejection::MissingPermission}});
    REQUIRE(f.player.currentIndex() == 0);
    REQUIRE(f.host->snapshot() == before);

    dj->intent(7, RoomIntent{.kind = RoomIntentKind::Next});
    REQUIRE(dj->all<RoomIntentRejected>().empty());
    REQUIRE(f.player.currentIndex() == 1);
    const auto after = f.host->snapshot();
    REQUIRE(after.version > before.version);
    REQUIRE(after.playback.current_item == f.player.occurrenceAt(1));
    REQUIRE(dj->lastState()->snapshot == after);
    REQUIRE(listener->lastState()->snapshot == after);
}

TEST_CASE("room host: each member receives its own effective permissions", "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    auto listener = f.join("fp-listener", "Lia");
    f.player.shuffleUpcoming();
    REQUIRE(dj->lastState()->snapshot.version == listener->lastState()->snapshot.version);
    REQUIRE(dj->lastState()->my_permissions == allPermissions());
    REQUIRE(listener->lastState()->my_permissions == RoomPermissionSet{});
}

TEST_CASE("room host: queue edits by a DJ and the intents waiting for later slices",
          "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    const auto c = f.player.occurrenceAt(2);
    dj->intent(1, RoomIntent{.kind = RoomIntentKind::MoveItem, .item_id = c, .destination = 1});
    REQUIRE(f.player.occurrenceAt(1) == c);
    REQUIRE(f.host->snapshot().queue[1].id == c);
    dj->intent(2, RoomIntent{.kind = RoomIntentKind::RemoveItem, .item_id = c});
    REQUIRE(f.player.paths().size() == 2);
    REQUIRE(f.host->snapshot().queue.size() == 2);
    dj->intent(3, RoomIntent{.kind = RoomIntentKind::RemoveItem, .item_id = 999});
    dj->intent(4, RoomIntent{.kind = RoomIntentKind::AddItems, .catalog_entry_ids = {1}});
    dj->intent(5, RoomIntent{.kind = RoomIntentKind::AddPlaylist, .playlist_id = 1});
    RoomItem upload;
    upload.origin = RoomItemOrigin::GuestTrack;
    upload.content_sha256 = std::string(64, 'a');
    dj->intent(6, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = upload});
    REQUIRE(dj->all<RoomIntentRejected>() == std::vector<RoomIntentRejected>{
                                                 {3, IntentRejection::UnknownItem},
                                                 {4, IntentRejection::UnknownItem},
                                                 {5, IntentRejection::UnknownItem},
                                                 {6, IntentRejection::InvalidArgument},
                                             });
    REQUIRE(f.player.paths().size() == 2);
}

TEST_CASE("room host: pausing in the host app broadcasts the anchor (host clock, position)",
          "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    const auto before = f.host->snapshot().version;
    f.now = 12'345;
    REQUIRE(f.player.pause());
    const auto state = dj->lastState()->snapshot;
    REQUIRE(state.version == before + 1);
    REQUIRE_FALSE(state.playback.playing);
    REQUIRE(state.playback.anchor_host_ms == 12'345);
    REQUIRE(state.playback.anchor_position_ms == 84'000);
    REQUIRE(state.playback.current_item == f.player.occurrenceAt(0));
}

TEST_CASE("room host: Pause/Resume intents and a seek re-anchor the playback", "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    f.now = 20'000;
    dj->intent(1, RoomIntent{.kind = RoomIntentKind::Pause});
    dj->intent(2, RoomIntent{.kind = RoomIntentKind::Pause});
    REQUIRE_FALSE(f.host->snapshot().playback.playing);
    f.now = 21'000;
    dj->intent(3, RoomIntent{.kind = RoomIntentKind::Seek, .position_ms = 100'000});
    REQUIRE(f.host->snapshot().playback.anchor_position_ms == 100'000);
    REQUIRE(f.host->snapshot().playback.anchor_host_ms == 21'000);
    dj->intent(4, RoomIntent{.kind = RoomIntentKind::Resume});
    REQUIRE(f.host->snapshot().playback.playing);
    REQUIRE(dj->all<RoomIntentRejected>().empty());
}

TEST_CASE("room host: close sends Bye and keeps the persisted invite", "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    const auto invite = f.host->open({.until_revoked = true});
    auto dj = f.join("fp-dj");
    auto waiting = f.join("fp-new");
    f.host->close();
    REQUIRE(dj->inbox.back() == RoomControlMessage{RoomBye{RoomByeReason::Closed}});
    REQUIRE(waiting->inbox.back() == RoomControlMessage{RoomBye{RoomByeReason::Closed}});
    REQUIRE(dj->link->closed);
    REQUIRE(f.endpoint.closed_all);
    REQUIRE_FALSE(f.host->isOpen());
    REQUIRE(f.store.stored.invite.has_value());
    REQUIRE(f.store.stored.invite->room_id == invite->room_id);
}

TEST_CASE("room host: guest items never reach the saved session and leave the queue when the room ends",
          "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj", "Bia");
    RoomItem described;
    described.origin = RoomItemOrigin::HostTrack;
    described.content_sha256 = std::string(64, 'e');
    described.size_bytes = 5;
    described.owner_fingerprint = "forged";
    described.added_by = "forged";
    described.summary.title = "Dela";
    const auto id = f.host->addGuestTrack("fp-dj", "room-cache/guest.mp3", described);
    REQUIRE(id.has_value());
    const auto queue = f.host->snapshot().queue;
    const auto guest = std::find_if(queue.begin(), queue.end(), [&](const RoomItem& i) { return i.id == *id; });
    REQUIRE(guest != queue.end());
    REQUIRE(guest->origin == RoomItemOrigin::GuestTrack);
    REQUIRE(guest->owner_fingerprint == "fp-dj");
    REQUIRE(guest->added_by == "Bia");
    REQUIRE(std::find(f.catalog.requests.begin(), f.catalog.requests.end(), "room-cache/guest.mp3") ==
            f.catalog.requests.end());
    REQUIRE_FALSE(f.host->addGuestTrack("fp-unknown", "room-cache/x.mp3", described).has_value());

    RecordingSessionWriter writer;
    SessionAutosave autosave{writer, f.player.sessionSnapshot()};
    REQUIRE(autosave.scheduleStructural(f.host->withoutGuestItems(f.player.sessionSnapshot())));
    REQUIRE(autosave.flush() == SessionSaveStatus::Saved);
    REQUIRE_FALSE(writer.writes.empty());
    const auto& saved = writer.writes.back().snapshot;
    REQUIRE(isValidSessionSnapshot(saved));
    REQUIRE(saved.queue.items.size() == 3);
    for (const auto& item : saved.queue.items)
        REQUIRE(item.source_path != "room-cache/guest.mp3");

    f.host->close();
    const auto paths = f.player.paths();
    REQUIRE(std::find(paths.begin(), paths.end(), "room-cache/guest.mp3") == paths.end());
    REQUIRE(paths.size() == 3);
}

TEST_CASE("room host: a hello with another protocol version gets Bye VersionMismatch and stays out",
          "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.connect("fp-dj");
    dj->hello("Ana", 2);
    REQUIRE(dj->inbox == std::vector<RoomControlMessage>{RoomBye{RoomByeReason::VersionMismatch}});
    REQUIRE(dj->link->closed);
    REQUIRE_FALSE(f.isMember("fp-dj"));
}

TEST_CASE("room host: anything before the hello, the host's own fingerprint or a bad nickname closes the link",
          "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto early = f.connect("fp-dj");
    early->intent(1, RoomIntent{.kind = RoomIntentKind::Next});
    REQUIRE(early->link->closed);
    REQUIRE(early->inbox.empty());

    auto impostor = f.connect(host_hex);
    REQUIRE(impostor->link->closed);
    impostor->hello();

    auto spaced = f.connect("fp-listener");
    spaced->hello(" Ana ");
    REQUIRE(spaced->link->closed);
    REQUIRE(f.host->snapshot().members.size() == 1);
    REQUIRE(f.player.currentIndex() == 0);
}

TEST_CASE("room host: host-only shuffle, repeat and queue end show up in the snapshot order",
          "[core][listening-room][room-host]") {
    // The guest intents are exactly Next..MoveItem: none for shuffle, repeat or the end of the queue.
    STATIC_REQUIRE(static_cast<int>(RoomIntentKind::MoveItem) == 8);
    HostFixture f({"music/a.mp3", "music/b.mp3", "music/c.mp3", "music/d.mp3"});
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    const auto before = f.host->snapshot().version;
    f.player.shuffleUpcoming();
    const auto shuffled = dj->lastState()->snapshot;
    REQUIRE(shuffled.version == before + 1);
    REQUIRE(shuffled.queue.size() == 4);
    for (std::size_t index = 0; index < shuffled.queue.size(); ++index)
        REQUIRE(shuffled.queue[index].id == f.player.occurrenceAt(index));
    REQUIRE(f.player.paths()[1] == "music/d.mp3");

    f.player.repeat = true;
    for (int step = 0; step < 4; ++step)
        dj->intent(10 + step, RoomIntent{.kind = RoomIntentKind::Next});
    REQUIRE(dj->all<RoomIntentRejected>().empty());
    REQUIRE(dj->lastState()->snapshot.playback.current_item == f.player.occurrenceAt(0));

    f.player.repeat = false;
    f.player.queue_end = QueueEndBehavior::StopClearSelection;
    dj->intent(20, RoomIntent{.kind = RoomIntentKind::Previous});
    REQUIRE(dj->all<RoomIntentRejected>() == std::vector<RoomIntentRejected>{{20, IntentRejection::InvalidArgument}});
    for (int step = 0; step < 4; ++step)
        f.player.finishTrack();
    REQUIRE_FALSE(dj->lastState()->snapshot.playback.current_item.has_value());
    REQUIRE_FALSE(dj->lastState()->snapshot.playback.playing);
}

TEST_CASE("room host: a state the link refuses is skipped without breaking the room",
          "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    auto listener = f.join("fp-listener", "Lia");
    dj->host_side->accepts_control = [](const RoomControlMessage& m) {
        return !std::holds_alternative<RoomStateMessage>(m);
    };
    const auto dj_states = dj->all<RoomStateMessage>().size();
    dj->intent(1, RoomIntent{.kind = RoomIntentKind::Next});
    REQUIRE(f.player.currentIndex() == 1);
    REQUIRE(dj->all<RoomStateMessage>().size() == dj_states);
    REQUIRE(listener->lastState()->snapshot == f.host->snapshot());
    REQUIRE(f.isMember("fp-dj"));
}

TEST_CASE("room host: the room queue never exceeds maximum_room_queue_items", "[core][listening-room][room-host]") {
    std::vector<std::filesystem::path> sources;
    for (std::size_t index = 0; index < maximum_room_queue_items + 20; ++index)
        sources.emplace_back("music/" + std::to_string(index) + ".mp3");
    HostFixture f(sources);
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    REQUIRE(f.host->snapshot().queue.size() == maximum_room_queue_items);
    REQUIRE(dj->lastState()->snapshot.queue.front().id == f.player.occurrenceAt(0));
}

TEST_CASE("room host: items appear once described and unreadable files stay out", "[core][listening-room][room-host]") {
    HostFixture f;
    f.catalog.deferred = true;
    f.catalog.unreadable.insert("music/c.mp3");
    f.start();
    static_cast<void>(f.host->open({}));
    REQUIRE(f.host->snapshot().queue.empty());
    REQUIRE_FALSE(f.host->snapshot().playback.current_item.has_value());
    REQUIRE(f.catalog.requests.front() == "music/a.mp3");
    const auto before = f.host->snapshot().version;
    f.catalog.completeAll();
    REQUIRE(f.host->snapshot().queue.size() == 2);
    REQUIRE(f.host->snapshot().version > before);
    REQUIRE(f.host->snapshot().playback.current_item == f.player.occurrenceAt(0));
    f.catalog.completeAll();
    REQUIRE(f.catalog.requests.size() == 3);
}

TEST_CASE("room host: kick with block sends Bye Kicked and the person cannot come back",
          "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    REQUIRE(f.host->kick("fp-dj", true));
    REQUIRE(dj->inbox.back() == RoomControlMessage{RoomBye{RoomByeReason::Kicked}});
    REQUIRE(dj->link->closed);
    REQUIRE_FALSE(f.isMember("fp-dj"));
    REQUIRE(std::any_of(f.store.stored.listeners.begin(), f.store.stored.listeners.end(),
                        [](const KnownListener& l) { return l.fingerprint == "fp-dj" && l.blocked; }));
    auto again = f.join("fp-dj");
    REQUIRE(again->inbox == std::vector<RoomControlMessage>{RoomJoinResult{JoinDecision::RejectedBlocked}});
    REQUIRE(again->link->closed);
}

TEST_CASE("room host: a dropped link leaves the room and a newer link replaces an older one",
          "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto first = f.join("fp-dj");
    auto second = f.join("fp-dj");
    REQUIRE(first->link->closed);
    REQUIRE(second->inbox.front() == RoomControlMessage{RoomJoinResult{JoinDecision::Admitted}});
    REQUIRE(second->lastState().has_value());
    REQUIRE(f.host->snapshot().members.size() == 2);

    second->link->close();
    REQUIRE_FALSE(f.isMember("fp-dj"));
}

TEST_CASE("room host: clock pings, lyrics of the current item and a full room", "[core][listening-room][room-host]") {
    HostFixture f;
    f.catalog.lyrics["music/a.mp3"] = "[00:01.00]la";
    f.catalog.lyrics["music/b.mp3"] = "[00:02.00]lo";
    f.start();
    static_cast<void>(f.host->open({.max_listeners = 1}));
    auto dj = f.join("fp-dj");
    REQUIRE(dj->all<RoomLyrics>() == std::vector<RoomLyrics>{RoomLyrics{f.player.occurrenceAt(0), "[00:01.00]la"}});
    f.now = 33'000;
    static_cast<void>(dj->link->sendControl(RoomClockPing{777}));
    REQUIRE(dj->all<RoomClockPong>() == std::vector<RoomClockPong>{RoomClockPong{777, 33'000}});
    dj->intent(1, RoomIntent{.kind = RoomIntentKind::Next});
    REQUIRE(dj->all<RoomLyrics>().back() == RoomLyrics{f.player.occurrenceAt(1), "[00:02.00]lo"});

    auto late = f.join("fp-listener");
    REQUIRE(late->inbox == std::vector<RoomControlMessage>{RoomJoinResult{JoinDecision::RejectedFull}});
    REQUIRE(late->link->closed);
}

} // namespace music_player::m0::test
