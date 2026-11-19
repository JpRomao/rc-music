// ListeningRoomHost (S7.1) over the in-memory transport, the silent deterministic player, a fake
// catalog and an in-memory roster store. No network, files or audio.
#include "../support/FakeRoomFiles.hpp"
#include "../support/FakeRoomHost.hpp"
#include "../support/FakeRoomTransport.hpp"

#include "m0/application/ListeningRoomHost.hpp"
#include "m0/application/SessionAutosave.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
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
            .schedule = [this](std::int64_t delay_ms,
                               std::function<void()> task) { scheduled.push_back({delay_ms, std::move(task)}); },
            .files = &outbox,
        });
        host->onJoinRequest = [this](const RoomMember& member) { join_requests.push_back(member); };
        host->onDriftsChanged = [this] { ++drift_changes; };
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
    // Runs every task scheduled so far (as if their delays elapsed), in order.
    void runScheduled() {
        auto due = std::move(scheduled);
        scheduled.clear();
        for (auto& [delay, task] : due)
            task();
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
    testing::FakeRoomFileOutbox outbox;
    std::int64_t now{10'000};
    std::uint8_t next_random{1};
    std::vector<RoomMember> join_requests;
    int drift_changes{0};
    std::vector<std::pair<std::int64_t, std::function<void()>>> scheduled;
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
    // Reopening with default options keeps the persisted settings.
    REQUIRE(f.store.stored.invite->max_listeners == 3);
    REQUIRE(f.host->persistedInvite().has_value());
    REQUIRE(f.host->persistedInvite()->max_listeners == 3);
    REQUIRE(f.host->persistedInvite()->room_id == persistent->room_id);
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

    // The clock moves while the (silent) player does not: a refused intent must not re-anchor or broadcast.
    f.now += 500;
    const auto listener_states = listener->all<RoomStateMessage>().size();
    listener->intent(41, RoomIntent{.kind = RoomIntentKind::Next});
    listener->intent(42, RoomIntent{.kind = RoomIntentKind::Next});
    REQUIRE(listener->all<RoomStateMessage>().size() == listener_states);
    REQUIRE(listener->all<RoomIntentRejected>() ==
            std::vector<RoomIntentRejected>{RoomIntentRejected{41, IntentRejection::MissingPermission},
                                            RoomIntentRejected{42, IntentRejection::MissingPermission}});
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
    // From the current item on, the snapshot order is the play order.
    dj->intent(9, RoomIntent{.kind = RoomIntentKind::Next});
    REQUIRE(dj->lastState()->snapshot.playback.current_item == shuffled.queue[1].id);
    dj->intent(8, RoomIntent{.kind = RoomIntentKind::Previous});

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
    REQUIRE(f.isMember("fp-dj"));
    f.runScheduled();
    REQUIRE_FALSE(f.isMember("fp-dj"));
}

TEST_CASE("room host: a Bye from a member is an immediate leave, without the reconnect hold",
          "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    auto listener = f.join("fp-listener", "Lia");
    auto stranger = f.join("fp-new", "Caio");
    REQUIRE(f.isMember("fp-new"));
    const auto version = f.host->snapshot().version;
    f.scheduled.clear();

    static_cast<void>(dj->link->sendControl(RoomBye{RoomByeReason::Closed}));
    REQUIRE_FALSE(f.isMember("fp-dj"));
    REQUIRE(f.host->snapshot().version == version + 1);
    REQUIRE(dj->link->closed);
    REQUIRE(listener->lastState()->snapshot.version == version + 1);
    REQUIRE(listener->lastState()->snapshot.members.size() == 2);
    REQUIRE(std::none_of(f.scheduled.begin(), f.scheduled.end(),
                         [](const auto& task) { return task.first == room_reconnect_window_ms; }));

    // A pending person who gives up leaves the same way.
    static_cast<void>(stranger->link->sendControl(RoomBye{RoomByeReason::Closed}));
    REQUIRE_FALSE(f.isMember("fp-new"));
    REQUIRE(stranger->link->closed);

    // Coming back is a new arrival: a known listener is admitted again.
    auto back = f.join("fp-dj");
    REQUIRE(back->inbox.front() == RoomControlMessage{RoomJoinResult{JoinDecision::Admitted}});
    REQUIRE(f.isMember("fp-dj"));
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

TEST_CASE("room host: playback jitter within the tolerance keeps the anchor and the version",
          "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    const auto before = f.host->snapshot();
    // Each tick moves the player 200 ms while the host clock moves 210 ms: 50 ms behind after five.
    for (int step = 0; step < 5; ++step) {
        f.now += 210;
        f.player.tick();
    }
    REQUIRE(f.host->snapshot() == before);
    REQUIRE(dj->lastState()->snapshot.version == before.version);
}

TEST_CASE("room host: drift beyond the tolerance re-anchors once", "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    const auto before = f.host->snapshot();
    for (int step = 0; step < 3; ++step) {
        f.now += 260; // 60 ms of drift per tick: beyond 100 ms on the second one.
        f.player.tick();
    }
    const auto after = f.host->snapshot();
    REQUIRE(after.version == before.version + 1);
    REQUIRE(after.playback.anchor_position_ms == 84'400);
    REQUIRE(after.playback.anchor_host_ms == 10'520);
    REQUIRE(dj->lastState()->snapshot == after);
}

TEST_CASE("room host: summaries are capped and the state window starts at the current item",
          "[core][listening-room][room-host]") {
    HostFixture f;
    // 300 x "e-acute" (2 bytes each): the cut must land on a code point boundary.
    std::string long_title;
    for (int i = 0; i < 300; ++i)
        long_title += "\xc3\xa9";
    f.catalog.summary_override = RoomTrackSummary{long_title, std::string(20'000, 'a'), "Album", 1'000};
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    REQUIRE(f.host->snapshot().queue.size() == 3);
    for (const auto& item : f.host->snapshot().queue) {
        REQUIRE(item.summary.title.size() <= maximum_room_summary_text_bytes);
        REQUIRE(item.summary.title.size() % 2 == 0);
        REQUIRE(item.summary.title.substr(0, 2) == "\xc3\xa9");
        REQUIRE(item.summary.artist == std::string(maximum_room_summary_text_bytes, 'a'));
        REQUIRE(item.summary.album == "Album");
    }
    RoomItem upload;
    upload.content_sha256 = std::string(64, 'e');
    upload.summary.title = std::string(20'000, 'g');
    const auto id = f.host->addGuestTrack("fp-dj", "room-cache/g.mp3", upload);
    REQUIRE(id.has_value());
    REQUIRE(f.host->snapshot().queue.back().summary.title.size() == maximum_room_summary_text_bytes);

    dj->intent(1, RoomIntent{.kind = RoomIntentKind::Next});
    REQUIRE(f.host->snapshot().queue.front().id == f.player.occurrenceAt(1));
    REQUIRE(f.host->snapshot().playback.current_item == f.player.occurrenceAt(1));
    REQUIRE(dj->lastState()->snapshot == f.host->snapshot());
}

TEST_CASE("room host: the estimated state size bounds the queue window", "[core][listening-room][room-host]") {
    std::vector<std::filesystem::path> sources;
    for (std::size_t index = 0; index < maximum_room_queue_items; ++index)
        sources.emplace_back("music/" + std::to_string(index) + ".mp3");
    HostFixture f(sources);
    const std::string quotes(maximum_room_summary_text_bytes, '"');
    f.catalog.summary_override = RoomTrackSummary{quotes, quotes, quotes, 1'000};
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    const auto state = dj->lastState();
    REQUIRE(state.has_value());
    REQUIRE_FALSE(state->snapshot.queue.empty());
    REQUIRE(state->snapshot.queue.size() < maximum_room_queue_items);
    REQUIRE(estimatedRoomStateBytes(*state) <= maximum_room_control_message_bytes);
    REQUIRE(state->snapshot.queue.front().id == f.player.occurrenceAt(0));
}

TEST_CASE("room host: destroying the host removes guest items from the player", "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    RoomItem upload;
    upload.content_sha256 = std::string(64, 'e');
    REQUIRE(f.host->addGuestTrack("fp-dj", "room-cache/g.mp3", upload).has_value());
    REQUIRE(f.player.paths().size() == 4);
    f.host.reset();
    const auto paths = f.player.paths();
    REQUIRE(std::find(paths.begin(), paths.end(), "room-cache/g.mp3") == paths.end());
    REQUIRE(paths.size() == 3);
}

TEST_CASE("room host: a transport that closes another link while sending does not break admission",
          "[core][listening-room][room-host]") {
    HostFixture f;
    f.catalog.lyrics["music/a.mp3"] = "[00:01.00]la";
    f.start();
    static_cast<void>(f.host->open({}));
    auto listener = f.join("fp-listener", "Lia");
    auto first = f.join("fp-dj");
    // A newer link of an already admitted person gets the state directly; while it is sent, the
    // transport synchronously drops the listener's link.
    auto second = f.connect("fp-dj");
    second->host_side->accepts_control = [&listener](const RoomControlMessage& m) {
        if (std::holds_alternative<RoomStateMessage>(m))
            listener->link->close();
        return true;
    };
    second->hello();
    REQUIRE(second->inbox.front() == RoomControlMessage{RoomJoinResult{JoinDecision::Admitted}});
    REQUIRE(second->lastState().has_value());
    REQUIRE(second->all<RoomLyrics>() == std::vector<RoomLyrics>{RoomLyrics{f.player.occurrenceAt(0), "[00:01.00]la"}});
}

TEST_CASE("room host: an admitted member who drops keeps the seat and resumes inside the reconnect window",
          "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({.max_listeners = 1}));
    auto dj = f.join("fp-dj");
    const auto version = f.host->snapshot().version;

    dj->link->close();
    REQUIRE(f.host->snapshot().version == version);
    REQUIRE(f.isMember("fp-dj"));
    REQUIRE(std::any_of(f.scheduled.begin(), f.scheduled.end(),
                        [](const auto& task) { return task.first == room_reconnect_window_ms; }));
    auto other = f.join("fp-listener");
    REQUIRE(other->inbox == std::vector<RoomControlMessage>{RoomJoinResult{JoinDecision::RejectedFull}});

    auto back = f.join("fp-dj");
    REQUIRE(back->inbox.front() == RoomControlMessage{RoomJoinResult{JoinDecision::Admitted}});
    REQUIRE(back->lastState()->snapshot.version == version);
    REQUIRE(f.host->snapshot().version == version);
    f.runScheduled();
    REQUIRE(f.isMember("fp-dj"));
    REQUIRE(f.host->snapshot().version == version);
    REQUIRE_FALSE(back->link->closed);
}

TEST_CASE("room host: after the reconnect window the member leaves", "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    auto listener = f.join("fp-listener", "Lia");
    const auto version = f.host->snapshot().version;
    dj->link->close();
    f.runScheduled();
    REQUIRE_FALSE(f.isMember("fp-dj"));
    REQUIRE(f.host->snapshot().version == version + 1);
    REQUIRE(listener->lastState()->snapshot.members.size() == 2);
}

TEST_CASE("room host: a link that never says hello is closed after the hello timeout",
          "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto silent = f.connect("fp-dj");
    auto talker = f.join("fp-listener", "Lia");
    REQUIRE(std::any_of(f.scheduled.begin(), f.scheduled.end(),
                        [](const auto& task) { return task.first == room_hello_timeout_ms; }));
    f.runScheduled();
    REQUIRE(silent->link->closed);
    REQUIRE_FALSE(talker->link->closed);
    REQUIRE(f.isMember("fp-listener"));
}

TEST_CASE("room host: guests never see pending people", "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    const auto dj_states = dj->all<RoomStateMessage>().size();
    auto stranger = f.join("fp-new", "Caio");
    REQUIRE(f.isMember("fp-new"));
    REQUIRE(dj->all<RoomStateMessage>().size() == dj_states);
    for (const auto& state : dj->all<RoomStateMessage>())
        for (const auto& member : state.snapshot.members)
            REQUIRE(member.state == MemberState::Admitted);
    REQUIRE(f.host->approve("fp-new"));
    const auto last = dj->lastState()->snapshot;
    REQUIRE(std::any_of(last.members.begin(), last.members.end(),
                        [](const RoomMember& m) { return m.fingerprint == "fp-new"; }));
}

namespace {
std::string sha(const std::filesystem::path& source) { return testing::FakeRoomTrackCatalog::fakeSha256(source); }
std::vector<std::string> shas(const std::vector<std::filesystem::path>& sources) {
    std::vector<std::string> result;
    for (const auto& source : sources)
        result.push_back(sha(source));
    return result;
}
const std::vector<std::filesystem::path> six_tracks{"music/a.mp3", "music/b.mp3", "music/c.mp3",
                                                    "music/d.mp3", "music/e.mp3", "music/f.mp3"};
} // namespace

TEST_CASE("room host: files go out for the current item first, then the next room_prefetch_items in queue order",
          "[core][listening-room][room-host][room-files]") {
    HostFixture f{six_tracks};
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    f.runScheduled();
    // Asked in reverse order, and for one item past the window: served in window order, without it.
    for (const auto* source : {"music/e.mp3", "music/d.mp3", "music/c.mp3", "music/b.mp3", "music/a.mp3"})
        static_cast<void>(dj->link->sendControl(RoomFileRequest{sha(source), 0}));
    REQUIRE(f.outbox.sent.empty()); // served on the next scheduler turn
    f.runScheduled();
    REQUIRE(f.outbox.sentHashes() == shas({"music/a.mp3", "music/b.mp3", "music/c.mp3", "music/d.mp3"}));
    REQUIRE(f.outbox.sent.front().peer == "fp-dj");
    REQUIRE(f.outbox.sent.front().file == std::filesystem::path{"music/a.mp3"});
    REQUIRE(f.outbox.sent.front().offset == 0);
    const std::set<std::uint32_t> ids{f.outbox.sent[0].transfer_id, f.outbox.sent[1].transfer_id,
                                      f.outbox.sent[2].transfer_id, f.outbox.sent[3].transfer_id};
    REQUIRE(ids.size() == 4);
    REQUIRE(room_prefetch_items == 3);
}

TEST_CASE("room host: a request outside the window, from a pending person or for an unknown hash is not served",
          "[core][listening-room][room-host][room-files]") {
    HostFixture f{six_tracks};
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    auto stranger = f.join("fp-stranger", "Bia"); // pending
    REQUIRE(f.player.next());
    f.runScheduled();
    static_cast<void>(dj->link->sendControl(RoomFileRequest{sha("music/a.mp3"), 0})); // left the window
    static_cast<void>(dj->link->sendControl(RoomFileRequest{std::string(64, 'f'), 0}));
    static_cast<void>(stranger->link->sendControl(RoomFileRequest{sha("music/b.mp3"), 0}));
    f.runScheduled();
    REQUIRE(f.outbox.sent.empty());

    // A guest that resumes asks from its offset: the host sends from there.
    static_cast<void>(dj->link->sendControl(RoomFileRequest{sha("music/b.mp3"), 3 * 65536}));
    f.runScheduled();
    REQUIRE(f.outbox.sentHashes() == shas({"music/b.mp3"}));
    REQUIRE(f.outbox.sent.front().offset == 3 * 65536);
}

TEST_CASE("room host: a queue change while sending cancels the transfers of items that left the window",
          "[core][listening-room][room-host][room-files]") {
    HostFixture f{six_tracks};
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    for (const auto* source : {"music/a.mp3", "music/b.mp3", "music/c.mp3", "music/d.mp3"})
        static_cast<void>(dj->link->sendControl(RoomFileRequest{sha(source), 0}));
    f.runScheduled();
    REQUIRE(f.outbox.sent.size() == 4);
    const auto id_a = f.outbox.transferOf(sha("music/a.mp3"));
    const auto id_d = f.outbox.transferOf(sha("music/d.mp3"));

    // d moves to the end of the queue: out of the window, its transfer stops; e comes in, nothing sent
    // until the guest asks for it.
    REQUIRE(f.player.move(LocalQueueOccurrenceId{f.player.occurrenceAt(3)}, 5));
    REQUIRE(f.outbox.cancelled == std::vector<std::pair<std::string, std::uint32_t>>{{"fp-dj", id_d}});
    // The current item changes: a leaves the window.
    REQUIRE(f.player.next());
    REQUIRE(f.outbox.cancelled.back() == std::pair<std::string, std::uint32_t>{"fp-dj", id_a});
    REQUIRE(f.outbox.cancelled.size() == 2);
    f.runScheduled();
    REQUIRE(f.outbox.sent.size() == 4);
}

TEST_CASE("room host: a second request for the same hash replaces the first transfer",
          "[core][listening-room][room-host][room-files]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    static_cast<void>(dj->link->sendControl(RoomFileRequest{sha("music/a.mp3"), 0}));
    f.runScheduled();
    const auto first = f.outbox.transferOf(sha("music/a.mp3"));
    static_cast<void>(dj->link->sendControl(RoomFileRequest{sha("music/a.mp3"), 65536}));
    f.runScheduled();
    REQUIRE(f.outbox.sent.size() == 2);
    REQUIRE(f.outbox.sent.back().transfer_id != first);
    REQUIRE(f.outbox.cancelled == std::vector<std::pair<std::string, std::uint32_t>>{{"fp-dj", first}});
}

TEST_CASE("room host: a link that goes is forgotten by the outbox", "[core][listening-room][room-host][room-files]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    auto listener = f.join("fp-listener", "Ouvinte");
    dj->link->close(); // dropped by the network
    REQUIRE(f.outbox.forgotten == std::vector<std::string>{"fp-dj"});
    static_cast<void>(f.host->kick("fp-listener", false));
    REQUIRE(f.outbox.forgotten == std::vector<std::string>{"fp-dj", "fp-listener"});
}

TEST_CASE("room host: lyrics above maximum_room_lyrics_bytes are not sent",
          "[core][listening-room][room-host][room-files]") {
    HostFixture f;
    f.catalog.lyrics["music/a.mp3"] = std::string(maximum_room_lyrics_bytes + 1, 'a');
    f.catalog.lyrics["music/b.mp3"] = std::string(maximum_room_lyrics_bytes, 'b');
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    REQUIRE(dj->all<RoomLyrics>().empty());
    dj->intent(1, RoomIntent{.kind = RoomIntentKind::Next});
    REQUIRE(dj->all<RoomLyrics>() ==
            std::vector<RoomLyrics>{RoomLyrics{f.player.occurrenceAt(1), std::string(maximum_room_lyrics_bytes, 'b')}});
}

TEST_CASE("room host: keeps the latest sync difference of each admitted member", "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    auto listener = f.join("fp-listener", "Lia");
    REQUIRE(f.host->memberDrifts().empty());

    static_cast<void>(dj->link->sendControl(RoomDriftReport{-42}));
    static_cast<void>(listener->link->sendControl(RoomDriftReport{15}));
    static_cast<void>(dj->link->sendControl(RoomDriftReport{7}));
    REQUIRE(f.host->memberDrifts() ==
            std::map<std::string, std::int64_t, std::less<>>{{"fp-dj", 7}, {"fp-listener", 15}});
    REQUIRE(f.drift_changes == 3);

    // The same value again changes nothing; the limits themselves are valid.
    static_cast<void>(dj->link->sendControl(RoomDriftReport{7}));
    REQUIRE(f.drift_changes == 3);
    static_cast<void>(dj->link->sendControl(RoomDriftReport{room_drift_report_limit_ms}));
    REQUIRE(f.host->memberDrifts().at("fp-dj") == room_drift_report_limit_ms);
    static_cast<void>(dj->link->sendControl(RoomDriftReport{-room_drift_report_limit_ms}));
    REQUIRE(f.host->memberDrifts().at("fp-dj") == -room_drift_report_limit_ms);

    // Absurd values are ignored: the previous one stays.
    static_cast<void>(dj->link->sendControl(RoomDriftReport{room_drift_report_limit_ms + 1}));
    static_cast<void>(dj->link->sendControl(RoomDriftReport{-room_drift_report_limit_ms - 1}));
    REQUIRE(f.host->memberDrifts().at("fp-dj") == -room_drift_report_limit_ms);
}

TEST_CASE("room host: a pending person's sync difference is ignored", "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto stranger = f.join("fp-new", "Caio");
    static_cast<void>(stranger->link->sendControl(RoomDriftReport{9}));
    REQUIRE(f.host->memberDrifts().empty());
    REQUIRE(f.host->approve("fp-new"));
    REQUIRE(f.host->memberDrifts().empty());
    static_cast<void>(stranger->link->sendControl(RoomDriftReport{9}));
    REQUIRE(f.host->memberDrifts().at("fp-new") == 9);
}

TEST_CASE("room host: the sync difference goes away with the person", "[core][listening-room][room-host]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    auto listener = f.join("fp-listener", "Lia");
    auto third = f.join("fp-new", "Caio");
    REQUIRE(f.host->approve("fp-new"));
    for (auto* guest : {dj.get(), listener.get(), third.get()})
        static_cast<void>(guest->link->sendControl(RoomDriftReport{11}));
    REQUIRE(f.host->memberDrifts().size() == 3);

    // A dropped link: the value is stale at once, before the reconnect window ends.
    dj->link->close();
    REQUIRE(f.isMember("fp-dj"));
    REQUIRE_FALSE(f.host->memberDrifts().contains("fp-dj"));

    static_cast<void>(listener->link->sendControl(RoomBye{RoomByeReason::Closed}));
    REQUIRE_FALSE(f.host->memberDrifts().contains("fp-listener"));

    REQUIRE(f.host->kick("fp-new", false));
    REQUIRE(f.host->memberDrifts().empty());

    // Coming back starts without a value; closing the room forgets everything.
    auto back = f.join("fp-dj");
    REQUIRE(f.host->memberDrifts().empty());
    static_cast<void>(back->link->sendControl(RoomDriftReport{3}));
    REQUIRE(f.host->memberDrifts().size() == 1);
    f.host->close();
    REQUIRE(f.host->memberDrifts().empty());
}

namespace {
RoomPermissionSet permissionsOf(std::initializer_list<RoomPermission> permissions) {
    RoomPermissionSet set;
    for (const auto permission : permissions)
        set.set(static_cast<std::size_t>(permission));
    return set;
}
const KnownListener* storedListener(const HostFixture& f, const std::string& fingerprint) {
    const auto& listeners = f.store.stored.listeners;
    const auto found = std::find_if(listeners.begin(), listeners.end(),
                                    [&](const KnownListener& l) { return l.fingerprint == fingerprint; });
    return found == listeners.end() ? nullptr : &*found;
}
} // namespace

TEST_CASE("room host: a new role assigned to a member reaches that member in the next state",
          "[core][listening-room][room-host][room-roles]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto listener = f.join("fp-listener", "Lia");
    REQUIRE(listener->lastState()->my_permissions == RoomPermissionSet{});
    const auto version = f.host->snapshot().version;

    const auto friends = f.host->createRole("Amigos", permissionsOf({RoomPermission::Skip, RoomPermission::Pause}));
    REQUIRE(friends.has_value());
    REQUIRE(std::any_of(f.store.stored.roles.begin(), f.store.stored.roles.end(),
                        [&](const RoomRole& r) { return r.id == *friends && r.name == "Amigos"; }));
    REQUIRE(f.host->assignRole("fp-listener", *friends));
    REQUIRE(storedListener(f, "fp-listener")->role_id == *friends);
    const auto state = listener->lastState();
    REQUIRE(state->my_permissions == permissionsOf({RoomPermission::Skip, RoomPermission::Pause}));
    REQUIRE(state->snapshot.version > version);
    REQUIRE(state->snapshot == f.host->snapshot());

    listener->intent(5, RoomIntent{.kind = RoomIntentKind::Next});
    REQUIRE(listener->all<RoomIntentRejected>().empty());
    REQUIRE(f.player.currentIndex() == 1);

    // Changing the role's permissions reaches the member too; a rename only persists.
    REQUIRE(f.host->setRolePermissions(*friends, permissionsOf({RoomPermission::Skip})));
    REQUIRE(listener->lastState()->my_permissions == permissionsOf({RoomPermission::Skip}));
    REQUIRE(f.host->renameRole(*friends, "Turma"));
    REQUIRE(f.host->roster().roles.back().name == "Turma");
    REQUIRE(f.store.stored.roles.back().name == "Turma");
    REQUIRE_FALSE(f.host->createRole("Turma", {}).has_value());
    REQUIRE_FALSE(f.host->assignRole("fp-listener", 99));
}

TEST_CASE("room host: a per-person override wins over the role in the next state",
          "[core][listening-room][room-host][room-roles]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    REQUIRE(f.host->setOverride("fp-dj", RoomPermission::Pause, false));
    REQUIRE_FALSE(dj->lastState()->my_permissions.test(static_cast<std::size_t>(RoomPermission::Pause)));
    REQUIRE(dj->lastState()->my_permissions.test(static_cast<std::size_t>(RoomPermission::Skip)));
    const auto& overrides = storedListener(f, "fp-dj")->overrides;
    REQUIRE(overrides.size() == 1);
    REQUIRE(overrides[0].permission == RoomPermission::Pause);
    REQUIRE_FALSE(overrides[0].allowed);
    dj->intent(3, RoomIntent{.kind = RoomIntentKind::Pause});
    REQUIRE(dj->all<RoomIntentRejected>() ==
            std::vector<RoomIntentRejected>{RoomIntentRejected{3, IntentRejection::MissingPermission}});

    REQUIRE(f.host->setOverride("fp-dj", RoomPermission::Pause, std::nullopt));
    REQUIRE(dj->lastState()->my_permissions == allPermissions());
    REQUIRE_FALSE(f.host->setOverride("fp-nobody", RoomPermission::Pause, true));
}

TEST_CASE("room host: unblocking lets a blocked person in again; after a kick without block they ask again",
          "[core][listening-room][room-host][room-roles]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    REQUIRE(f.host->kick("fp-dj", true));
    auto refused = f.join("fp-dj");
    REQUIRE(refused->inbox == std::vector<RoomControlMessage>{RoomJoinResult{JoinDecision::RejectedBlocked}});

    REQUIRE(f.host->unblock("fp-dj"));
    REQUIRE_FALSE(storedListener(f, "fp-dj")->blocked);
    auto back = f.join("fp-dj");
    REQUIRE(back->inbox.front() == RoomControlMessage{RoomJoinResult{JoinDecision::Admitted}});
    REQUIRE_FALSE(f.host->unblock("fp-dj"));

    REQUIRE(f.host->kick("fp-dj", false));
    REQUIRE(back->inbox.back() == RoomControlMessage{RoomBye{RoomByeReason::Kicked}});
    REQUIRE(storedListener(f, "fp-dj") == nullptr);
    REQUIRE(f.host->roster().listeners.size() == 1);
    f.join_requests.clear();
    auto again = f.join("fp-dj");
    REQUIRE(again->inbox == std::vector<RoomControlMessage>{RoomJoinResult{JoinDecision::Pending}});
    REQUIRE(f.join_requests.size() == 1);
    REQUIRE(f.host->approve("fp-dj"));
    REQUIRE(again->inbox.at(1) == RoomControlMessage{RoomJoinResult{JoinDecision::Admitted}});
}

TEST_CASE("room host: someone kicked while their link is down hears it on reconnect and then asks again",
          "[core][listening-room][room-host][room-roles]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    dj->link->close();
    REQUIRE(f.isMember("fp-dj"));

    REQUIRE(f.host->kick("fp-dj", false));
    REQUIRE_FALSE(f.isMember("fp-dj"));
    REQUIRE(storedListener(f, "fp-dj") == nullptr);
    // The guest's app retries inside the reconnect window: it is told it was kicked, not let back in.
    auto retry = f.join("fp-dj");
    REQUIRE(retry->inbox == std::vector<RoomControlMessage>{RoomBye{RoomByeReason::Kicked}});
    REQUIRE(retry->link->closed);
    REQUIRE_FALSE(f.isMember("fp-dj"));
    REQUIRE(f.join_requests.empty());

    // Once told, the person may ask again like anyone new.
    auto asks = f.join("fp-dj");
    REQUIRE(asks->inbox == std::vector<RoomControlMessage>{RoomJoinResult{JoinDecision::Pending}});
    REQUIRE(f.join_requests.size() == 1);
}

TEST_CASE("room host: the kick notice for a disconnected person lasts only the reconnect window",
          "[core][listening-room][room-host][room-roles]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    auto listener = f.join("fp-listener", "Lia");
    listener->link->close();
    dj->link->close();
    REQUIRE(f.host->kick("fp-dj", false));
    REQUIRE(f.host->kick("fp-listener", true));
    f.runScheduled();
    auto later = f.join("fp-dj");
    REQUIRE(later->inbox == std::vector<RoomControlMessage>{RoomJoinResult{JoinDecision::Pending}});
    auto blocked = f.join("fp-listener", "Lia");
    REQUIRE(blocked->inbox == std::vector<RoomControlMessage>{RoomJoinResult{JoinDecision::RejectedBlocked}});
}

TEST_CASE("room host: the invite's initial role cannot be removed; people in a removed role fall back to it",
          "[core][listening-room][room-host][room-roles]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    REQUIRE(f.host->initialRoleId() == 1);
    REQUIRE(f.host->removeRole(1) == RoleRemoval::InitialRole);
    REQUIRE(f.host->roster().roles.size() == 2);
    REQUIRE(f.host->removeRole(77) == RoleRemoval::Refused);

    auto dj = f.join("fp-dj");
    const auto saves = f.store.saves;
    REQUIRE(f.host->removeRole(2) == RoleRemoval::Removed);
    REQUIRE(f.store.saves > saves);
    REQUIRE(storedListener(f, "fp-dj")->role_id == 1);
    REQUIRE(dj->lastState()->my_permissions == RoomPermissionSet{});
    REQUIRE(f.store.stored.roles.size() == 1);
    // The last role is never removed.
    REQUIRE(f.host->removeRole(1) == RoleRemoval::InitialRole);
}

TEST_CASE("room host: choosing another initial role frees the old one and reaches the persisted invite",
          "[core][listening-room][room-host][room-roles]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({.until_revoked = true, .default_role_id = 2}));
    REQUIRE(f.host->initialRoleId() == 2);
    REQUIRE(f.host->removeRole(2) == RoleRemoval::InitialRole);
    REQUIRE_FALSE(f.host->setInitialRole(99));

    const auto friends = f.host->createRole("Amigos", permissionsOf({RoomPermission::Skip}));
    REQUIRE(f.host->setInitialRole(*friends));
    REQUIRE(f.host->initialRoleId() == *friends);
    REQUIRE(f.store.stored.invite->default_role_id == *friends);
    auto stranger = f.join("fp-new", "Caio");
    REQUIRE(f.host->approve("fp-new"));
    REQUIRE(storedListener(f, "fp-new")->role_id == *friends);
    REQUIRE(stranger->lastState()->my_permissions == permissionsOf({RoomPermission::Skip}));

    REQUIRE(f.host->removeRole(2) == RoleRemoval::Removed);
    REQUIRE(storedListener(f, "fp-dj")->role_id == *friends);
}

TEST_CASE("room host: an initial role removed since it was chosen opens with the first role",
          "[core][listening-room][room-host][room-roles]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({.default_role_id = 99}));
    REQUIRE(f.host->initialRoleId() == 1);
    auto stranger = f.join("fp-new", "Caio");
    REQUIRE(f.host->approve("fp-new"));
    REQUIRE(storedListener(f, "fp-new")->role_id == 1);
}

} // namespace music_player::m0::test
