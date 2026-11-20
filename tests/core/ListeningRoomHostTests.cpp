// ListeningRoomHost (S7.1) over the in-memory transport, the silent deterministic player, a fake
// catalog and an in-memory roster store. No network, files or audio.
#include "../support/FakeRoomFiles.hpp"
#include "../support/FakeRoomHost.hpp"
#include "../support/FakeRoomTransport.hpp"

#include "m0/application/ListeningRoomHost.hpp"
#include "m0/application/SessionAutosave.hpp"
#include "m0/domain/PlaylistLibrary.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <iterator>
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
    [[nodiscard]] RoomCatalog lastCatalog() const {
        const auto catalogs = all<RoomCatalog>();
        REQUIRE_FALSE(catalogs.empty());
        return catalogs.back();
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
            .uploads = &uploads,
            .upload_probe = &probe,
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
    // Runs the tasks scheduled so far whose delay is at most max_delay_ms, in order; the others stay.
    void runScheduledWithin(const std::int64_t max_delay_ms) {
        auto due = std::move(scheduled);
        scheduled.clear();
        std::vector<std::pair<std::int64_t, std::function<void()>>> later;
        for (auto& [delay, task] : due) {
            if (delay <= max_delay_ms)
                task();
            else
                later.emplace_back(delay, std::move(task));
        }
        later.insert(later.end(), std::make_move_iterator(scheduled.begin()), std::make_move_iterator(scheduled.end()));
        scheduled = std::move(later);
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
    testing::FakeRoomFileInbox uploads;
    testing::FakeRoomUploadProbe probe;
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

NamedPlaylist namedPlaylist(const std::uint64_t id, std::string name, const PlaylistVisibility visibility,
                            const std::vector<std::filesystem::path>& paths) {
    NamedPlaylist playlist{id, std::move(name), {}, visibility};
    for (std::size_t index = 0; index < paths.size(); ++index)
        playlist.entries.push_back(PlaylistEntry{id * 100 + index + 1, paths[index]});
    return playlist;
}

// "Festa" (d, e) is visible to the room unless party says otherwise; "Privada" never is.
PlaylistLibrarySnapshot partyLibrary(const PlaylistVisibility party = PlaylistVisibility::Room,
                                     std::string party_name = "Festa") {
    return {{namedPlaylist(1, "Privada", PlaylistVisibility::Private, {"music/segredo.mp3"}),
             namedPlaylist(2, std::move(party_name), party, {"music/d.mp3", "music/e.mp3"})}};
}

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

TEST_CASE("room host: with guests in the room a new track starts room_play_lead_ms ahead, the host's player too",
          "[core][listening-room][room-host][room-lead]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    f.now = 20'000;
    REQUIRE(f.player.next());
    const auto state = dj->lastState()->snapshot;
    REQUIRE(state.playback.current_item == f.player.occurrenceAt(1));
    REQUIRE(state.playback.playing);
    REQUIRE(state.playback.anchor_host_ms == 20'000 + room_play_lead_ms);
    REQUIRE(state.playback.anchor_position_ms == 0);

    // Until that instant the host's own player stays silent; for the room it is still starting to play.
    f.runScheduledWithin(0);
    REQUIRE(f.player.state().status == PlaybackStatus::Paused);
    REQUIRE(f.player.state().position_ms == 0);
    REQUIRE(f.host->snapshot().playback == state.playback);
    REQUIRE(dj->lastState()->snapshot.version == state.version);

    f.now = 20'000 + room_play_lead_ms;
    f.runScheduledWithin(room_play_lead_ms);
    REQUIRE(f.player.state().status == PlaybackStatus::Playing);
    REQUIRE(f.host->snapshot().playback == state.playback);
    REQUIRE(dj->lastState()->snapshot.version == state.version);
}

TEST_CASE("room host: a Resume or a seek while playing starts room_play_lead_ms ahead",
          "[core][listening-room][room-host][room-lead]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    REQUIRE(f.player.pause());
    f.now = 30'000;
    dj->intent(1, RoomIntent{.kind = RoomIntentKind::Resume});
    REQUIRE(f.host->snapshot().playback.playing);
    REQUIRE(f.host->snapshot().playback.anchor_host_ms == 30'000 + room_play_lead_ms);
    REQUIRE(f.host->snapshot().playback.anchor_position_ms == 84'000);
    f.runScheduledWithin(0);
    REQUIRE(f.player.state().status == PlaybackStatus::Paused);
    f.now += room_play_lead_ms;
    f.runScheduledWithin(room_play_lead_ms);
    REQUIRE(f.player.state().status == PlaybackStatus::Playing);

    f.now = 40'000;
    dj->intent(2, RoomIntent{.kind = RoomIntentKind::Seek, .position_ms = 100'000});
    REQUIRE(f.host->snapshot().playback.playing);
    REQUIRE(f.host->snapshot().playback.anchor_host_ms == 40'000 + room_play_lead_ms);
    REQUIRE(f.host->snapshot().playback.anchor_position_ms == 100'000);
    f.runScheduledWithin(0);
    REQUIRE(f.player.state().status == PlaybackStatus::Paused);
    REQUIRE(dj->all<RoomIntentRejected>().empty());
}

TEST_CASE("room host: while a start waits, Pause cancels it, Resume does nothing and Next waits again",
          "[core][listening-room][room-host][room-lead]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    f.now = 20'000;
    REQUIRE(f.player.next());
    f.runScheduledWithin(0);

    // Resume while the start waits: it is already starting.
    const auto starting = f.host->snapshot();
    dj->intent(1, RoomIntent{.kind = RoomIntentKind::Resume});
    REQUIRE(f.host->snapshot() == starting);
    REQUIRE(f.player.state().status == PlaybackStatus::Paused);

    // Next while the start waits: the new track waits for its own instant (the player was silent only
    // because of the wait, so the room keeps playing).
    f.now = 20'100;
    dj->intent(2, RoomIntent{.kind = RoomIntentKind::Next});
    REQUIRE(f.host->snapshot().playback.current_item == f.player.occurrenceAt(2));
    REQUIRE(f.host->snapshot().playback.playing);
    REQUIRE(f.host->snapshot().playback.anchor_host_ms == 20'100 + room_play_lead_ms);

    // Pause while it waits: the room pauses and the player never starts.
    f.now = 20'200;
    dj->intent(3, RoomIntent{.kind = RoomIntentKind::Pause});
    REQUIRE_FALSE(f.host->snapshot().playback.playing);
    REQUIRE(f.host->snapshot().playback.anchor_host_ms == 20'200);
    f.now = 21'000;
    f.runScheduledWithin(room_play_lead_ms);
    REQUIRE(f.player.state().status != PlaybackStatus::Playing); // loaded, silent
    REQUIRE_FALSE(f.host->snapshot().playback.playing);
    REQUIRE(dj->all<RoomIntentRejected>().empty());
}

TEST_CASE("room host: alone in the room the host starts at once", "[core][listening-room][room-host][room-lead]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    f.now = 20'000;
    REQUIRE(f.player.next());
    REQUIRE(f.host->snapshot().playback.playing);
    REQUIRE(f.host->snapshot().playback.anchor_host_ms == 20'000);
    f.runScheduledWithin(room_play_lead_ms);
    REQUIRE(f.player.state().status == PlaybackStatus::Playing);
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

TEST_CASE("room host: a roster change the file did not take is reported until a later save succeeds",
          "[core][listening-room][room-host][room-roles]") {
    HostFixture f;
    f.start();
    int notices = 0;
    f.host->onRosterUnsavedChanged = [&] { ++notices; };
    static_cast<void>(f.host->open({}));
    auto listener = f.join("fp-listener", "Lia");
    REQUIRE_FALSE(f.host->rosterUnsaved());

    f.store.save_result = RoomRosterSaveStatus::Failed;
    REQUIRE(f.host->kick("fp-listener", true));
    REQUIRE(f.host->rosterUnsaved());
    REQUIRE(notices == 1);
    // The file still has the person unblocked; this session keeps the block.
    REQUIRE_FALSE(storedListener(f, "fp-listener")->blocked);
    auto again = f.join("fp-listener", "Lia");
    REQUIRE(again->inbox == std::vector<RoomControlMessage>{RoomJoinResult{JoinDecision::RejectedBlocked}});
    REQUIRE(f.host->createRole("Amigos", {}).has_value());
    REQUIRE(notices == 1);

    // The next save writes the whole roster, so nothing is pending any more.
    f.store.save_result = RoomRosterSaveStatus::Saved;
    REQUIRE(f.host->renameRole(f.host->roster().roles.back().id, "Turma"));
    REQUIRE_FALSE(f.host->rosterUnsaved());
    REQUIRE(notices == 2);
    REQUIRE(storedListener(f, "fp-listener")->blocked);
    REQUIRE(f.store.stored.roles.back().name == "Turma");
}

TEST_CASE("room host: changes to a roster file that cannot be written stay unsaved for the session",
          "[core][listening-room][room-host][room-roles]") {
    HostFixture f;
    f.store.status = RoomRosterLoadStatus::Invalid;
    f.start();
    REQUIRE_FALSE(f.host->rosterUnsaved());
    REQUIRE(f.host->createRole("Amigos", {}).has_value());
    REQUIRE(f.host->rosterUnsaved());
    REQUIRE(f.store.saves == 0);

    HostFixture newer;
    newer.start();
    newer.store.save_result = RoomRosterSaveStatus::BlockedByFutureVersion;
    REQUIRE(newer.host->createRole("Amigos", {}).has_value());
    REQUIRE(newer.host->rosterUnsaved());
    newer.store.save_result = RoomRosterSaveStatus::Saved;
    REQUIRE(newer.host->renameRole(newer.host->roster().roles.back().id, "Turma"));
    REQUIRE(newer.host->rosterUnsaved());
    REQUIRE(newer.store.saves == 1);
}

// ---- S11.2: the catalog of the host's playlists visible to the room ----------------------------------------

TEST_CASE("room host: the catalog has only the playlists visible to the room, with track summaries and its own ids",
          "[core][listening-room][room-host][room-catalog]") {
    HostFixture f;
    f.start();
    f.host->setPlaylists(partyLibrary());
    REQUIRE(f.host->catalog().playlists.empty()); // nothing is described before the room opens
    static_cast<void>(f.host->open({}));
    auto stranger = f.join("fp-new", "Caio");
    REQUIRE(stranger->all<RoomCatalog>().empty()); // pending people get nothing

    auto dj = f.join("fp-dj");
    REQUIRE(dj->all<RoomCatalog>().size() == 1);
    const auto catalog = dj->lastCatalog();
    REQUIRE(catalog == f.host->catalog());
    REQUIRE(catalog.playlists.size() == 1);
    const auto& party = catalog.playlists.front();
    REQUIRE(party.name == "Festa");
    REQUIRE(party.entries.size() == 2);
    REQUIRE(party.entries[0].summary == RoomTrackSummary{"d", "Artista", "Album", 248'000});
    REQUIRE(party.entries[1].summary.title == "e");
    // The ids are the room's own, never the library's (which would tell about other lists).
    const std::set<std::uint64_t> ids{party.id, party.entries[0].id, party.entries[1].id};
    REQUIRE(ids.size() == 3);
    REQUIRE_FALSE(ids.contains(0));
    REQUIRE(party.id != 2);
    REQUIRE_FALSE(ids.contains(201));
    REQUIRE_FALSE(ids.contains(202));
    // A private list's files are not even read.
    REQUIRE(std::find(f.catalog.requests.begin(), f.catalog.requests.end(), "music/segredo.mp3") ==
            f.catalog.requests.end());
    // The admitted member gets the state first, then the catalog.
    const auto first_state = std::find_if(dj->inbox.begin(), dj->inbox.end(), [](const RoomControlMessage& m) {
        return std::holds_alternative<RoomStateMessage>(m);
    });
    const auto first_catalog = std::find_if(dj->inbox.begin(), dj->inbox.end(), [](const RoomControlMessage& m) {
        return std::holds_alternative<RoomCatalog>(m);
    });
    REQUIRE(first_state < first_catalog);
}

TEST_CASE("room host: a playlist made private during the room leaves the guests' catalog and its ids stop working",
          "[core][listening-room][room-host][room-catalog]") {
    HostFixture f;
    f.start();
    f.host->setPlaylists(partyLibrary());
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    const auto party = dj->lastCatalog().playlists.front();

    f.host->setPlaylists(partyLibrary(PlaylistVisibility::Private));
    REQUIRE(dj->all<RoomCatalog>().size() == 2);
    REQUIRE(dj->lastCatalog().playlists.empty());
    REQUIRE(f.host->catalog().playlists.empty());

    dj->intent(1, RoomIntent{.kind = RoomIntentKind::AddItems, .catalog_entry_ids = {party.entries[0].id}});
    dj->intent(2, RoomIntent{.kind = RoomIntentKind::AddPlaylist, .playlist_id = party.id});
    REQUIRE(dj->all<RoomIntentRejected>() ==
            std::vector<RoomIntentRejected>{{1, IntentRejection::UnknownItem}, {2, IntentRejection::UnknownItem}});
    REQUIRE(f.player.paths().size() == 3);

    // The same library again changes nothing and sends nothing.
    f.host->setPlaylists(partyLibrary(PlaylistVisibility::Private));
    REQUIRE(dj->all<RoomCatalog>().size() == 2);
    // Visible again: back in the catalog.
    f.host->setPlaylists(partyLibrary());
    REQUIRE(dj->all<RoomCatalog>().size() == 3);
    REQUIRE(dj->lastCatalog().playlists.size() == 1);
}

TEST_CASE("room host: renaming or changing a visible playlist sends the new catalog",
          "[core][listening-room][room-host][room-catalog]") {
    HostFixture f;
    f.start();
    f.host->setPlaylists(partyLibrary());
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    const auto before = dj->lastCatalog();

    f.host->setPlaylists(partyLibrary(PlaylistVisibility::Room, "Festa de sábado"));
    REQUIRE(dj->all<RoomCatalog>().size() == 2);
    const auto renamed = dj->lastCatalog();
    REQUIRE(renamed.playlists.front().name == "Festa de sábado");
    // Ids stay the same while the room is open.
    REQUIRE(renamed.playlists.front().id == before.playlists.front().id);
    REQUIRE(renamed.playlists.front().entries == before.playlists.front().entries);

    auto library = partyLibrary();
    library.playlists[1].entries.push_back(PlaylistEntry{203, "music/f.mp3"});
    f.host->setPlaylists(library);
    REQUIRE(dj->lastCatalog().playlists.front().entries.size() == 3);
    REQUIRE(dj->lastCatalog().playlists.front().entries[2].summary.title == "f");
}

TEST_CASE("room host: catalog tracks appear once described and unreadable ones stay out",
          "[core][listening-room][room-host][room-catalog]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    f.catalog.deferred = true;
    f.catalog.unreadable.insert("music/d.mp3");
    f.host->setPlaylists(partyLibrary());
    REQUIRE(f.host->catalog().playlists.size() == 1);
    REQUIRE(f.host->catalog().playlists.front().entries.empty());
    f.catalog.completeAll();
    f.runScheduled();
    const auto& entries = f.host->catalog().playlists.front().entries;
    REQUIRE(entries.size() == 1);
    REQUIRE(entries.front().summary.title == "e");
    REQUIRE(dj->lastCatalog() == f.host->catalog());
}

TEST_CASE("room host: AddPlaylist with the permission queues every track of the playlist in order",
          "[core][listening-room][room-host][room-catalog]") {
    HostFixture f;
    f.start();
    f.host->setPlaylists(partyLibrary());
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    const auto party = dj->lastCatalog().playlists.front();

    dj->intent(1, RoomIntent{.kind = RoomIntentKind::AddPlaylist, .playlist_id = party.id});
    REQUIRE(dj->all<RoomIntentRejected>().empty());
    REQUIRE(f.player.paths() == std::vector<std::filesystem::path>{"music/a.mp3", "music/b.mp3", "music/c.mp3",
                                                                   "music/d.mp3", "music/e.mp3"});
    const auto queue = f.host->snapshot().queue;
    REQUIRE(queue.size() == 5);
    REQUIRE(queue[3].summary.title == "d");
    REQUIRE(queue[4].summary.title == "e");
    REQUIRE(queue[3].owner_fingerprint == host_hex);
    REQUIRE(dj->lastState()->snapshot == f.host->snapshot());

    // Single tracks, in the order asked.
    dj->intent(2, RoomIntent{.kind = RoomIntentKind::AddItems,
                             .catalog_entry_ids = {party.entries[1].id, party.entries[0].id}});
    REQUIRE(dj->all<RoomIntentRejected>().empty());
    REQUIRE(f.player.paths().size() == 7);
    REQUIRE(f.player.paths()[5] == "music/e.mp3");
    REQUIRE(f.player.paths()[6] == "music/d.mp3");

    // One unknown id refuses the whole intent.
    dj->intent(3, RoomIntent{.kind = RoomIntentKind::AddItems, .catalog_entry_ids = {party.entries[0].id, 999}});
    dj->intent(4, RoomIntent{.kind = RoomIntentKind::AddPlaylist, .playlist_id = 999});
    REQUIRE(dj->all<RoomIntentRejected>() ==
            std::vector<RoomIntentRejected>{{3, IntentRejection::UnknownItem}, {4, IntentRejection::UnknownItem}});
    REQUIRE(f.player.paths().size() == 7);
}

TEST_CASE("room host: adding from the catalog without the permission is refused",
          "[core][listening-room][room-host][room-catalog]") {
    HostFixture f;
    f.start();
    f.host->setPlaylists(partyLibrary());
    static_cast<void>(f.host->open({}));
    auto listener = f.join("fp-listener", "Lia");
    // A listener sees the catalog too; the permissions decide what it may add.
    const auto party = listener->lastCatalog().playlists.front();
    listener->intent(1, RoomIntent{.kind = RoomIntentKind::AddPlaylist, .playlist_id = party.id});
    listener->intent(2, RoomIntent{.kind = RoomIntentKind::AddItems, .catalog_entry_ids = {party.entries[0].id}});
    REQUIRE(listener->all<RoomIntentRejected>() ==
            std::vector<RoomIntentRejected>{{1, IntentRejection::MissingPermission},
                                            {2, IntentRejection::MissingPermission}});
    REQUIRE(f.player.paths().size() == 3);

    // AddFromRoom alone does not add whole playlists.
    REQUIRE(f.host->setOverride("fp-listener", RoomPermission::AddFromRoom, true));
    listener->intent(3, RoomIntent{.kind = RoomIntentKind::AddPlaylist, .playlist_id = party.id});
    listener->intent(4, RoomIntent{.kind = RoomIntentKind::AddItems, .catalog_entry_ids = {party.entries[0].id}});
    REQUIRE(listener->all<RoomIntentRejected>().back() == RoomIntentRejected{3, IntentRejection::MissingPermission});
    REQUIRE(f.player.paths().size() == 4);
}

TEST_CASE("room host: adding past maximum_room_queue_items is QueueFull and adds nothing",
          "[core][listening-room][room-host][room-catalog]") {
    std::vector<std::filesystem::path> sources;
    for (std::size_t index = 0; index + 1 < maximum_room_queue_items; ++index)
        sources.emplace_back("music/" + std::to_string(index) + ".mp3");
    HostFixture f(sources);
    f.start();
    f.host->setPlaylists(partyLibrary());
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    REQUIRE(f.host->snapshot().queue.size() == maximum_room_queue_items - 1);
    const auto party = dj->lastCatalog().playlists.front();

    dj->intent(1, RoomIntent{.kind = RoomIntentKind::AddPlaylist, .playlist_id = party.id});
    dj->intent(2, RoomIntent{.kind = RoomIntentKind::AddItems,
                             .catalog_entry_ids = {party.entries[0].id, party.entries[1].id}});
    REQUIRE(dj->all<RoomIntentRejected>() ==
            std::vector<RoomIntentRejected>{{1, IntentRejection::QueueFull}, {2, IntentRejection::QueueFull}});
    REQUIRE(f.player.paths().size() == maximum_room_queue_items - 1);

    dj->intent(3, RoomIntent{.kind = RoomIntentKind::AddItems, .catalog_entry_ids = {party.entries[0].id}});
    REQUIRE(dj->all<RoomIntentRejected>().size() == 2);
    REQUIRE(f.host->snapshot().queue.size() == maximum_room_queue_items);
    dj->intent(4, RoomIntent{.kind = RoomIntentKind::AddPlaylist, .playlist_id = party.id});
    REQUIRE(dj->all<RoomIntentRejected>().back() == RoomIntentRejected{4, IntentRejection::QueueFull});
    REQUIRE(f.player.paths().size() == maximum_room_queue_items);
}

TEST_CASE("room host: closing the room forgets the catalog", "[core][listening-room][room-host][room-catalog]") {
    HostFixture f;
    f.start();
    f.host->setPlaylists(partyLibrary());
    static_cast<void>(f.host->open({}));
    REQUIRE(f.host->catalog().playlists.size() == 1);
    f.host->close();
    REQUIRE(f.host->catalog().playlists.empty());
    // Reopened, it is built again from the playlists it was given.
    static_cast<void>(f.host->open({}));
    REQUIRE(f.host->catalog().playlists.size() == 1);
}

// Fix round 1: descriptions keep arriving while people are in the room; they must not each send the whole catalog.
TEST_CASE("room host: descriptions that arrive one after another reach guests in one catalog per refresh",
          "[core][listening-room][room-host][room-catalog]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj");
    f.catalog.deferred = true;
    std::vector<std::filesystem::path> paths;
    for (int index = 0; index < 20; ++index)
        paths.emplace_back("music/lista" + std::to_string(index) + ".mp3");
    f.host->setPlaylists(PlaylistLibrarySnapshot{{namedPlaylist(2, "Festa", PlaylistVisibility::Room, paths)}});
    const auto sent = dj->all<RoomCatalog>().size();
    REQUIRE(dj->lastCatalog().playlists.front().entries.empty());

    // Every file gets described (a few at a time) without waiting for a refresh, and nothing is sent meanwhile.
    for (int round = 0; round < 10; ++round)
        f.catalog.completeAll();
    for (const auto& path : paths)
        REQUIRE(std::count(f.catalog.requests.begin(), f.catalog.requests.end(), path) == 1);
    REQUIRE(dj->all<RoomCatalog>().size() == sent);
    const auto refreshes = std::count_if(f.scheduled.begin(), f.scheduled.end(),
                                         [](const auto& task) { return task.first == room_catalog_refresh_ms; });
    REQUIRE(refreshes == 1);

    // The refresh sends one catalog with everything described so far, to everyone admitted.
    f.runScheduled();
    REQUIRE(dj->all<RoomCatalog>().size() == sent + 1);
    REQUIRE(dj->lastCatalog().playlists.front().entries.size() == paths.size());
    REQUIRE(dj->lastCatalog() == f.host->catalog());
    // Nothing new: no more refreshes and no more catalogs.
    f.runScheduled();
    REQUIRE(dj->all<RoomCatalog>().size() == sent + 1);
    REQUIRE(f.scheduled.empty());
}

// Fix round 1: a track from the catalog is the host's file, but the queue shows who asked for it.
TEST_CASE("room host: catalog tracks a guest adds carry that guest as who added them",
          "[core][listening-room][room-host][room-catalog]") {
    HostFixture f;
    f.start();
    f.host->setPlaylists(partyLibrary());
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj", "Bia");
    const auto party = dj->lastCatalog().playlists.front();

    dj->intent(1, RoomIntent{.kind = RoomIntentKind::AddPlaylist, .playlist_id = party.id});
    dj->intent(2, RoomIntent{.kind = RoomIntentKind::AddItems, .catalog_entry_ids = {party.entries[1].id}});
    REQUIRE(dj->all<RoomIntentRejected>().empty());
    const auto queue = f.host->snapshot().queue;
    REQUIRE(queue.size() == 6);
    REQUIRE(queue[0].added_by == "Joao");
    for (std::size_t index = 3; index < 6; ++index) {
        REQUIRE(queue[index].added_by == "Bia");
        REQUIRE(queue[index].owner_fingerprint == host_hex);
        REQUIRE(queue[index].origin == RoomItemOrigin::HostTrack);
    }
    REQUIRE(dj->lastState()->snapshot == f.host->snapshot());

    // Once the room closes they are plain host tracks again.
    f.host->close();
    static_cast<void>(f.host->open({}));
    const auto reopened = f.host->snapshot().queue;
    REQUIRE(reopened.size() == 6);
    REQUIRE(
        std::all_of(reopened.begin(), reopened.end(), [](const RoomItem& item) { return item.added_by == "Joao"; }));
}

// S12.1: guests send their own tracks. The file goes up first; only a checked file joins the queue.
namespace {

const std::string upload_sha = std::string(63, '0') + "9";

// What a guest declares: a GuestTrack with its hash and size, and whatever else it claims.
RoomItem declaredUpload(const std::uint64_t size_bytes = 5000, const std::string& sha256 = upload_sha) {
    RoomItem item;
    item.id = 999;
    item.origin = RoomItemOrigin::GuestTrack;
    item.owner_fingerprint = "forged";
    item.content_sha256 = sha256;
    item.size_bytes = size_bytes;
    item.summary = RoomTrackSummary{"Falso", "Falso", "Falso", 1};
    item.added_by = "forged";
    return item;
}

RoomFileChunk chunk(const std::uint32_t transfer_id, const std::uint64_t offset, const std::size_t bytes) {
    return RoomFileChunk{transfer_id, offset, std::vector<std::uint8_t>(bytes, 0x5a)};
}

// A copy: snapshot() returns by value.
std::optional<RoomItem> findItem(const RoomSnapshot& snapshot, const std::string& sha256) {
    const auto found = std::find_if(snapshot.queue.begin(), snapshot.queue.end(),
                                    [&](const RoomItem& item) { return item.content_sha256 == sha256; });
    return found == snapshot.queue.end() ? std::nullopt : std::optional{*found};
}

// The DJ's upload, up to the moment its file is complete in the host's upload cache.
void uploadUntilComplete(HostFixture& f, Guest& dj, const std::uint64_t request_id = 40) {
    dj.intent(request_id, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = declaredUpload()});
    const auto requests = dj.all<RoomFileRequest>();
    REQUIRE_FALSE(requests.empty());
    REQUIRE(requests.back() == RoomFileRequest{upload_sha, 0});
    static_cast<void>(dj.link->sendControl(RoomFileOffer{upload_sha, 5000, 77}));
    REQUIRE_FALSE(f.uploads.accepted.empty());
    static_cast<void>(dj.link->sendMedia(chunk(77, 0, 5000)));
    f.uploads.complete(upload_sha);
}

} // namespace

TEST_CASE("room host: without the permission an upload is refused before any byte moves",
          "[core][listening-room][room-host][room-uploads]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto listener = f.join("fp-listener", "Lia");
    const auto version = f.host->snapshot().version;
    listener->intent(5, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = declaredUpload()});
    REQUIRE(listener->all<RoomIntentRejected>() ==
            std::vector<RoomIntentRejected>{{5, IntentRejection::MissingPermission}});
    REQUIRE(listener->all<RoomFileRequest>().empty());
    // An offer and chunks sent anyway go nowhere.
    static_cast<void>(listener->link->sendControl(RoomFileOffer{upload_sha, 5000, 1}));
    static_cast<void>(listener->link->sendMedia(chunk(1, 0, 5000)));
    REQUIRE(f.uploads.log.empty());
    REQUIRE(f.uploads.accepted.empty());
    REQUIRE(f.uploads.consumed.empty());
    REQUIRE(f.host->snapshot().version == version);
}

TEST_CASE("room host: an upload above 50 MiB, or not a guest track, is refused before any byte moves",
          "[core][listening-room][room-host][room-uploads]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj", "Bia");
    REQUIRE(maximum_guest_upload_bytes == 50ull * 1024 * 1024);
    dj->intent(
        1, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = declaredUpload(maximum_guest_upload_bytes + 1)});
    auto youtube = declaredUpload();
    youtube.origin = RoomItemOrigin::YoutubeVideo;
    youtube.youtube_id = "dQw4w9WgXcQ";
    dj->intent(2, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = youtube});
    auto host_track = declaredUpload();
    host_track.origin = RoomItemOrigin::HostTrack;
    dj->intent(3, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = host_track});
    dj->intent(4, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = declaredUpload(0)});
    dj->intent(5, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = declaredUpload(10, "nope")});
    REQUIRE(dj->all<RoomIntentRejected>() == std::vector<RoomIntentRejected>{
                                                 {1, IntentRejection::InvalidArgument},
                                                 {2, IntentRejection::InvalidArgument},
                                                 {3, IntentRejection::InvalidArgument},
                                                 {4, IntentRejection::InvalidArgument},
                                                 {5, IntentRejection::InvalidArgument},
                                             });
    REQUIRE(dj->all<RoomFileRequest>().empty());
    REQUIRE(f.uploads.log.empty());

    // Exactly 50 MiB is allowed: the host asks for the file.
    dj->intent(6, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = declaredUpload(maximum_guest_upload_bytes)});
    REQUIRE(dj->all<RoomFileRequest>() == std::vector<RoomFileRequest>{{upload_sha, 0}});
    // The same guest sending the same file again (fix round 1: after its own send failed, say) replaces the
    // upload on its way: the host asks for the file again and answers nothing for the old request.
    dj->intent(7, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = declaredUpload(maximum_guest_upload_bytes)});
    REQUIRE(dj->all<RoomIntentRejected>().size() == 5);
    REQUIRE(dj->all<RoomFileRequest>() == std::vector<RoomFileRequest>{{upload_sha, 0}, {upload_sha, 0}});
    // An offer larger than declared is refused and leaves nothing.
    static_cast<void>(dj->link->sendControl(RoomFileOffer{upload_sha, maximum_guest_upload_bytes + 1, 3}));
    REQUIRE(f.uploads.accepted.empty());
    REQUIRE(dj->all<RoomIntentRejected>().size() == 6);
    REQUIRE(dj->all<RoomIntentRejected>().back() == RoomIntentRejected{7, IntentRejection::InvalidArgument});
    REQUIRE(f.uploads.discarded == std::vector<std::string>{upload_sha});
}

TEST_CASE("room host: an upload goes up, is checked and joins the queue as the guest's track",
          "[core][listening-room][room-host][room-uploads]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj", "Bia");
    auto listener = f.join("fp-listener", "Lia");
    f.uploads.partial[upload_sha] = 65536; // a partial file from an earlier attempt: resumed
    f.probe.deferred = true;

    dj->intent(40, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = declaredUpload()});
    REQUIRE(dj->all<RoomIntentRejected>().empty());
    REQUIRE(dj->all<RoomFileRequest>() == std::vector<RoomFileRequest>{{upload_sha, 65536}});
    REQUIRE(listener->all<RoomFileRequest>().empty());
    // A chunk that overtakes its offer is kept and handed over once the offer is accepted.
    static_cast<void>(dj->link->sendMedia(chunk(77, 65536, 100)));
    REQUIRE(f.uploads.consumed.empty());
    static_cast<void>(dj->link->sendControl(RoomFileOffer{upload_sha, 5000, 77}));
    REQUIRE(f.uploads.accepted.size() == 1);
    const auto cache_transfer = f.uploads.accepted.front().transfer_id;
    REQUIRE(f.uploads.accepted.front() == RoomFileOffer{upload_sha, 5000, cache_transfer});
    static_cast<void>(dj->link->sendMedia(chunk(77, 65636, 200)));
    REQUIRE(f.uploads.consumed.size() == 2);
    REQUIRE(f.uploads.consumed[0].transfer_id == cache_transfer);
    REQUIRE(f.uploads.consumed[0].offset == 65536);
    REQUIRE(f.uploads.consumed[1].offset == 65636);
    // Chunks of another transfer, or from another member, are not the upload's.
    static_cast<void>(dj->link->sendMedia(chunk(76, 0, 10)));
    static_cast<void>(listener->link->sendMedia(chunk(77, 0, 10)));
    REQUIRE(f.uploads.consumed.size() == 2);

    // Complete and verified by the cache: probed, and only then queued.
    f.uploads.complete(upload_sha);
    const auto cached = std::filesystem::path{"cache"} / upload_sha;
    REQUIRE(f.probe.probed == std::vector<std::filesystem::path>{cached});
    REQUIRE(findItem(f.host->snapshot(), upload_sha) == std::nullopt);
    f.probe.completeAll();
    const auto item = findItem(f.host->snapshot(), upload_sha);
    REQUIRE(item.has_value());
    REQUIRE(item->origin == RoomItemOrigin::GuestTrack);
    REQUIRE(item->owner_fingerprint == "fp-dj");
    REQUIRE(item->added_by == "Bia");
    REQUIRE(item->size_bytes == 5000);
    REQUIRE(item->summary == f.probe.summary);
    REQUIRE(item->id != 999);
    REQUIRE(f.player.paths().back() == cached);
    REQUIRE(findItem(listener->lastState()->snapshot, upload_sha) != std::nullopt);
    REQUIRE(dj->all<RoomIntentRejected>().empty());
    REQUIRE(f.uploads.discarded.empty());
    // Like every guest item, it stays out of the saved session.
    const auto saved = f.host->withoutGuestItems(f.player.sessionSnapshot());
    REQUIRE(saved.queue.items.size() == 3);
}

TEST_CASE("room host: a file that is not valid audio is refused and nothing of it stays",
          "[core][listening-room][room-host][room-uploads]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj", "Bia");
    const auto version = f.host->snapshot().version;
    f.probe.invalid.insert(std::filesystem::path{"cache"} / upload_sha);
    uploadUntilComplete(f, *dj, 41);
    REQUIRE(dj->all<RoomIntentRejected>() == std::vector<RoomIntentRejected>{{41, IntentRejection::InvalidArgument}});
    REQUIRE(f.uploads.discarded == std::vector<std::string>{upload_sha});
    REQUIRE(findItem(f.host->snapshot(), upload_sha) == std::nullopt);
    REQUIRE(f.player.paths().size() == 3);
    REQUIRE(f.host->snapshot().version == version);
}

TEST_CASE("room host: a gap asks again from the partial file; repeated failures refuse the upload",
          "[core][listening-room][room-host][room-uploads]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj", "Bia");
    dj->intent(42, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = declaredUpload()});
    static_cast<void>(dj->link->sendControl(RoomFileOffer{upload_sha, 5000, 1}));
    REQUIRE(f.uploads.accepted.size() == 1);
    f.uploads.partial[upload_sha] = 65536;
    f.uploads.fail(upload_sha, f.uploads.accepted.back().transfer_id, RoomFileFailure::Gap);
    REQUIRE(dj->all<RoomFileRequest>().back() == RoomFileRequest{upload_sha, 65536});
    // The answer to the new request replaces the old transfer.
    static_cast<void>(dj->link->sendControl(RoomFileOffer{upload_sha, 5000, 2}));
    REQUIRE(f.uploads.accepted.size() == 2);
    for (int attempt = 0; attempt < room_file_retries; ++attempt) {
        f.uploads.fail(upload_sha, f.uploads.accepted.back().transfer_id);
        REQUIRE(dj->all<RoomFileRequest>().size() == static_cast<std::size_t>(attempt + 3));
        static_cast<void>(
            dj->link->sendControl(RoomFileOffer{upload_sha, 5000, static_cast<std::uint32_t>(3 + attempt)}));
    }
    REQUIRE(dj->all<RoomIntentRejected>().empty());
    f.uploads.fail(upload_sha, f.uploads.accepted.back().transfer_id);
    REQUIRE(dj->all<RoomIntentRejected>() == std::vector<RoomIntentRejected>{{42, IntentRejection::InvalidArgument}});
    REQUIRE(f.uploads.discarded == std::vector<std::string>{upload_sha});
    REQUIRE(dj->all<RoomFileRequest>().size() == static_cast<std::size_t>(room_file_retries + 2));
}

TEST_CASE("room host: the owner leaving keeps its track, which plays for the others",
          "[core][listening-room][room-host][room-uploads]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj", "Bia");
    auto listener = f.join("fp-listener", "Lia");
    uploadUntilComplete(f, *dj);
    REQUIRE(findItem(f.host->snapshot(), upload_sha) != std::nullopt);

    static_cast<void>(dj->link->sendControl(RoomBye{RoomByeReason::Closed}));
    REQUIRE_FALSE(f.isMember("fp-dj"));
    const auto item = findItem(f.host->snapshot(), upload_sha);
    REQUIRE(item.has_value());
    REQUIRE(item->added_by == "Bia");
    REQUIRE(f.uploads.discarded.empty());

    // When it comes up it is in the file window, and the host sends its copy.
    REQUIRE(f.player.next());
    REQUIRE(f.player.next());
    REQUIRE(f.player.next());
    REQUIRE(f.host->snapshot().playback.current_item == item->id);
    static_cast<void>(listener->link->sendControl(RoomFileRequest{upload_sha, 0}));
    f.runScheduled();
    REQUIRE(f.outbox.sentHashes() == std::vector<std::string>{upload_sha});
    REQUIRE(f.outbox.sent.back().file == std::filesystem::path{"cache"} / upload_sha);
}

// S13.1: the owner's offer can come after its whole file. The chunks past what is kept are gone and no later
// chunk shows the gap, so the host asks again from what it has instead of waiting for the stall timeout.
TEST_CASE("room host: an upload offer that comes after more chunks than are kept asks again from the partial file",
          "[core][listening-room][room-host][room-uploads]") {
    constexpr std::size_t piece = 64 * 1024;
    constexpr std::size_t kept = 2 * 1024 * 1024 / piece;
    constexpr std::uint64_t size = (kept + 3) * piece;
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj", "Bia");
    dj->intent(43, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = declaredUpload(size)});
    REQUIRE(dj->all<RoomFileRequest>() == std::vector<RoomFileRequest>{{upload_sha, 0}});
    for (std::size_t index = 0; index < kept + 3; ++index)
        static_cast<void>(dj->link->sendMedia(chunk(77, index * piece, piece)));
    f.uploads.partial[upload_sha] = kept * piece; // what the cache wrote from the kept chunks
    static_cast<void>(dj->link->sendControl(RoomFileOffer{upload_sha, size, 77}));

    REQUIRE(f.uploads.accepted.size() == 1);
    const auto first = f.uploads.accepted.front().transfer_id;
    REQUIRE(f.uploads.consumed.size() == kept);
    REQUIRE(f.uploads.abandoned == std::vector<std::uint32_t>{first});
    REQUIRE(dj->all<RoomFileRequest>() == std::vector<RoomFileRequest>{{upload_sha, 0}, {upload_sha, kept * piece}});
    REQUIRE(dj->all<RoomIntentRejected>().empty());

    // The owner replaces its transfer from there; a late chunk of the old one is dropped.
    static_cast<void>(dj->link->sendMedia(chunk(77, (kept + 3) * piece, 1)));
    static_cast<void>(dj->link->sendControl(RoomFileOffer{upload_sha, size, 78}));
    REQUIRE(f.uploads.accepted.size() == 2);
    const auto second = f.uploads.accepted.back().transfer_id;
    static_cast<void>(dj->link->sendMedia(chunk(78, kept * piece, 3 * piece)));
    REQUIRE(f.uploads.consumed.size() == kept + 1);
    REQUIRE(f.uploads.consumed.back().transfer_id == second);
    REQUIRE(f.uploads.consumed.back().offset == kept * piece);
    f.uploads.complete(upload_sha);
    f.probe.completeAll();
    REQUIRE(findItem(f.host->snapshot(), upload_sha).has_value());
    REQUIRE(dj->all<RoomIntentRejected>().empty());
}

namespace {
constexpr std::size_t overflow_piece = 64 * 1024;
constexpr std::size_t overflow_kept = 2 * 1024 * 1024 / overflow_piece;
constexpr std::uint64_t overflow_size = (overflow_kept + 3) * overflow_piece;
// The owner sends more chunks of transfer_id than the host keeps, all before their offer, then the offer (S13.1).
void overflowUpload(Guest& dj, const std::uint32_t transfer_id) {
    for (std::size_t index = 0; index < overflow_kept + 3; ++index)
        static_cast<void>(dj.link->sendMedia(chunk(transfer_id, index * overflow_piece, overflow_piece)));
    static_cast<void>(dj.link->sendControl(RoomFileOffer{upload_sha, overflow_size, transfer_id}));
}
} // namespace

// S13.1 fix round 1: when the cache cannot accept an overflowed upload (or write its kept chunks), the failure
// counts against room_file_retries; then the upload is refused and reported, and nothing is asked again.
TEST_CASE("room host: an overflowed upload that cannot be accepted uses the retries, then it is refused",
          "[core][listening-room][room-host][room-uploads]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj", "Bia");
    dj->intent(44, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = declaredUpload(overflow_size)});
    REQUIRE(dj->all<RoomFileRequest>().size() == 1);
    f.uploads.deferred = true; // like the worker: failures of accept() come before the status answer
    for (int round = 0; round < room_file_retries + 2; ++round) {
        overflowUpload(*dj, static_cast<std::uint32_t>(77 + round));
        if (!f.uploads.accepted.empty())
            f.uploads.fail(upload_sha, f.uploads.accepted.back().transfer_id); // QuotaExceeded, say
        f.uploads.answerAll();
    }
    REQUIRE(dj->all<RoomFileRequest>().size() == 1 + static_cast<std::size_t>(room_file_retries));
    REQUIRE(f.uploads.accepted.size() == 1 + static_cast<std::size_t>(room_file_retries));
    REQUIRE(dj->all<RoomIntentRejected>() == std::vector<RoomIntentRejected>{{44, IntentRejection::InvalidArgument}});
    REQUIRE(f.uploads.discarded == std::vector<std::string>{upload_sha});
}

// S13.1 fix round 1: an overflow round that adds nothing to the partial file is a failed try, so the host does
// not ask the owner for the whole file again forever.
TEST_CASE("room host: upload overflow rounds that gain nothing use the retries, then the upload is refused",
          "[core][listening-room][room-host][room-uploads]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj", "Bia");
    dj->intent(45, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = declaredUpload(overflow_size)});
    REQUIRE(dj->all<RoomFileRequest>() == std::vector<RoomFileRequest>{{upload_sha, 0}});
    // The kept chunks are written once; later rounds keep nothing new (the partial file stays the same).
    f.uploads.partial[upload_sha] = overflow_kept * overflow_piece;
    for (int round = 0; round < room_file_retries + 3; ++round)
        overflowUpload(*dj, static_cast<std::uint32_t>(77 + round));
    REQUIRE(dj->all<RoomFileRequest>().size() == 2 + static_cast<std::size_t>(room_file_retries));
    REQUIRE(dj->all<RoomFileRequest>().back() == RoomFileRequest{upload_sha, overflow_kept * overflow_piece});
    REQUIRE(f.uploads.accepted.size() == 2 + static_cast<std::size_t>(room_file_retries));
    REQUIRE(dj->all<RoomIntentRejected>() == std::vector<RoomIntentRejected>{{45, IntentRejection::InvalidArgument}});
    REQUIRE(f.uploads.discarded == std::vector<std::string>{upload_sha});
}

TEST_CASE("room host: an owner who goes mid-upload leaves nothing behind",
          "[core][listening-room][room-host][room-uploads]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj", "Bia");
    dj->intent(43, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = declaredUpload()});
    static_cast<void>(dj->link->sendControl(RoomFileOffer{upload_sha, 5000, 1}));
    REQUIRE(f.uploads.accepted.size() == 1);
    const auto transfer = f.uploads.accepted.back().transfer_id;
    dj->link->close();
    REQUIRE(f.uploads.abandoned == std::vector<std::uint32_t>{transfer});
    REQUIRE(f.uploads.discarded == std::vector<std::string>{upload_sha});
    // A late answer of the cache changes nothing.
    f.uploads.complete(upload_sha);
    f.uploads.fail(upload_sha, transfer);
    REQUIRE(f.probe.probed.empty());
    REQUIRE(f.player.paths().size() == 3);
}

TEST_CASE("room host: when the room ends, guest files leave the upload cache and the queue",
          "[core][listening-room][room-host][room-uploads]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj", "Bia");
    uploadUntilComplete(f, *dj);
    REQUIRE(f.player.paths().size() == 4);
    f.host->close();
    REQUIRE(f.uploads.purges == 1);
    const auto paths = f.player.paths();
    REQUIRE(paths.size() == 3);
    REQUIRE(std::find(paths.begin(), paths.end(), std::filesystem::path{"cache"} / upload_sha) == paths.end());

    // The destructor purges it too (an app closing with the room open).
    static_cast<void>(f.host->open({}));
    f.host.reset();
    REQUIRE(f.uploads.purges == 2);
}

TEST_CASE("room host: an upload into a full room queue is refused", "[core][listening-room][room-host][room-uploads]") {
    std::vector<std::filesystem::path> many;
    for (std::size_t index = 0; index + 1 < maximum_room_queue_items; ++index)
        many.push_back("music/" + std::to_string(index) + ".mp3");
    HostFixture f{many};
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj", "Bia");
    REQUIRE(f.host->snapshot().queue.size() == maximum_room_queue_items - 1);
    f.probe.deferred = true;
    const auto other = declaredUpload(5000, std::string(63, '0') + "8");
    dj->intent(1, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = other});
    uploadUntilComplete(f, *dj, 2);
    static_cast<void>(dj->link->sendControl(RoomFileOffer{other.content_sha256, 5000, 90}));
    f.uploads.complete(other.content_sha256);
    // The first file checked fills the queue; the other one is refused.
    f.probe.completeAll();
    REQUIRE(f.host->snapshot().queue.size() == maximum_room_queue_items);
    REQUIRE(findItem(f.host->snapshot(), upload_sha) != std::nullopt);
    REQUIRE(dj->all<RoomIntentRejected>() == std::vector<RoomIntentRejected>{{1, IntentRejection::QueueFull}});
    REQUIRE(f.uploads.discarded == std::vector<std::string>{other.content_sha256});
}

// S12.1 fix round 1: an upload nobody feeds (a guest that never answers the request, or stops mid-file) ends
// after room_upload_stall_ms without a byte from its owner, and the same file can be sent again.
TEST_CASE("room host: an upload with no bytes for room_upload_stall_ms is dropped and can be sent again",
          "[core][listening-room][room-host][room-uploads]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj", "Bia");
    const auto watches = [&f] {
        return std::count_if(f.scheduled.begin(), f.scheduled.end(),
                             [](const auto& task) { return task.first == room_upload_stall_ms; });
    };
    REQUIRE(room_upload_stall_ms == 60000);
    dj->intent(40, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = declaredUpload()});
    REQUIRE(watches() == 1);
    static_cast<void>(dj->link->sendControl(RoomFileOffer{upload_sha, 5000, 77}));
    static_cast<void>(dj->link->sendMedia(chunk(77, 0, 1000)));
    const auto transfer = f.uploads.accepted.back().transfer_id;
    // Bytes came in during the period: the watch goes on.
    f.runScheduled();
    REQUIRE(dj->all<RoomIntentRejected>().empty());
    REQUIRE(watches() == 1);
    // A whole period without a byte: dropped (UnknownItem, the host no longer has it) and nothing stays.
    f.runScheduled();
    REQUIRE(dj->all<RoomIntentRejected>() == std::vector<RoomIntentRejected>{{40, IntentRejection::UnknownItem}});
    REQUIRE(std::ranges::find(f.uploads.abandoned, transfer) != f.uploads.abandoned.end());
    REQUIRE(f.uploads.discarded == std::vector<std::string>{upload_sha});
    REQUIRE(watches() == 0);
    // The same file can be sent again at once.
    dj->intent(41, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = declaredUpload()});
    REQUIRE(dj->all<RoomFileRequest>().size() == 2);
    REQUIRE(dj->all<RoomIntentRejected>().size() == 1);
}

// A guest's files go up one after the other on its link: bytes of any of its uploads keep all of them alive,
// and a file being checked is the host's own work, never a stall.
TEST_CASE("room host: an upload waiting behind another of the same guest, or being checked, is not dropped",
          "[core][listening-room][room-host][room-uploads]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj", "Bia");
    f.probe.deferred = true;
    const auto other = declaredUpload(5000, std::string(63, '0') + "8");
    dj->intent(40, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = declaredUpload()});
    dj->intent(41, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = other});
    static_cast<void>(dj->link->sendControl(RoomFileOffer{upload_sha, 5000, 77}));
    for (std::uint64_t period = 0; period < 3; ++period) {
        static_cast<void>(dj->link->sendMedia(chunk(77, period * 1000, 1000)));
        f.runScheduled();
    }
    REQUIRE(dj->all<RoomIntentRejected>().empty());
    f.uploads.complete(upload_sha);
    static_cast<void>(dj->link->sendControl(RoomFileOffer{other.content_sha256, 5000, 78}));
    static_cast<void>(dj->link->sendMedia(chunk(78, 0, 5000)));
    f.uploads.complete(other.content_sha256);
    REQUIRE(f.probe.probed.size() == 2);
    f.runScheduled();
    f.runScheduled();
    REQUIRE(dj->all<RoomIntentRejected>().empty());
    f.probe.completeAll();
    REQUIRE(findItem(f.host->snapshot(), upload_sha).has_value());
    REQUIRE(findItem(f.host->snapshot(), other.content_sha256).has_value());
}

// S12.1 fix round 1: the upload cache only holds files of the queue (its quota would otherwise run out).
TEST_CASE("room host: a guest track that leaves the queue frees its file unless another item or upload uses it",
          "[core][listening-room][room-host][room-uploads]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj", "Bia");
    uploadUntilComplete(f, *dj, 40);
    const auto first = findItem(f.host->snapshot(), upload_sha);
    REQUIRE(first.has_value());

    // The same file again is in the cache already: checked again, not sent again.
    f.probe.deferred = true;
    dj->intent(41, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = declaredUpload()});
    REQUIRE(dj->all<RoomFileRequest>().size() == 1);
    // The first item leaves while that upload is being checked: the file stays for it.
    REQUIRE(f.player.remove(LocalQueueOccurrenceId{first->id}));
    REQUIRE(findItem(f.host->snapshot(), upload_sha) == std::nullopt);
    REQUIRE(f.uploads.discarded.empty());
    f.probe.completeAll();
    const auto second = findItem(f.host->snapshot(), upload_sha);
    REQUIRE(second.has_value());
    REQUIRE(second->id != first->id);
    REQUIRE(dj->all<RoomIntentRejected>().empty());

    // A copy of the same file stays while one of two items leaves.
    f.probe.deferred = false;
    dj->intent(42, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = declaredUpload()});
    const auto queue = f.host->snapshot().queue;
    REQUIRE(std::count_if(queue.begin(), queue.end(),
                          [](const RoomItem& item) { return item.content_sha256 == upload_sha; }) == 2);
    REQUIRE(f.player.remove(LocalQueueOccurrenceId{second->id}));
    REQUIRE(f.uploads.discarded.empty());
    // The last item using it leaves: the file goes.
    const auto third = findItem(f.host->snapshot(), upload_sha);
    REQUIRE(third.has_value());
    REQUIRE(f.player.remove(LocalQueueOccurrenceId{third->id}));
    REQUIRE(f.uploads.discarded == std::vector<std::string>{upload_sha});
    REQUIRE(f.player.paths().size() == 3);
}

// S12.1 fix round 1: a file already in the upload cache is not sent again, so its size is the cached file's:
// a declaration that does not match it is refused. Anyone with the permission who declares the right hash
// and size may queue it again (as with a duplicate from the catalog), and owns that new item.
TEST_CASE("room host: a file already in the upload cache keeps its real size; another declared size is refused",
          "[core][listening-room][room-host][room-uploads]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj", "Bia");
    auto listener = f.join("fp-listener", "Lia");
    uploadUntilComplete(f, *dj, 40);
    REQUIRE(f.uploads.lookup(upload_sha).size_bytes == 5000);
    REQUIRE(f.host->setOverride("fp-listener", RoomPermission::UploadOwnTracks, true));
    const auto version = f.host->snapshot().version;

    listener->intent(1, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = declaredUpload(4000)});
    REQUIRE(listener->all<RoomIntentRejected>() ==
            std::vector<RoomIntentRejected>{{1, IntentRejection::InvalidArgument}});
    REQUIRE(listener->all<RoomFileRequest>().empty());
    REQUIRE(f.host->snapshot().version == version);
    // Refused, but the file stays: the DJ's item still uses it.
    REQUIRE(f.uploads.discarded.empty());

    listener->intent(2, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = declaredUpload(5000)});
    REQUIRE(listener->all<RoomIntentRejected>().size() == 1);
    const auto queue = f.host->snapshot().queue;
    const auto copy = std::find_if(queue.begin(), queue.end(), [](const RoomItem& item) {
        return item.content_sha256 == upload_sha && item.owner_fingerprint == "fp-listener";
    });
    REQUIRE(copy != queue.end());
    REQUIRE(copy->size_bytes == 5000);
    REQUIRE(copy->added_by == "Lia");
}

// S12.1 fix round 1: a permission taken away mid-upload stops it; the file never joins the queue.
TEST_CASE("room host: taking the upload permission away stops the guest's uploads",
          "[core][listening-room][room-host][room-uploads]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj", "Bia");
    dj->intent(40, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = declaredUpload()});
    static_cast<void>(dj->link->sendControl(RoomFileOffer{upload_sha, 5000, 77}));
    static_cast<void>(dj->link->sendMedia(chunk(77, 0, 1000)));
    const auto transfer = f.uploads.accepted.back().transfer_id;
    REQUIRE(f.host->setOverride("fp-dj", RoomPermission::UploadOwnTracks, false));
    REQUIRE(dj->all<RoomIntentRejected>() == std::vector<RoomIntentRejected>{{40, IntentRejection::MissingPermission}});
    REQUIRE(std::ranges::find(f.uploads.abandoned, transfer) != f.uploads.abandoned.end());
    REQUIRE(f.uploads.discarded == std::vector<std::string>{upload_sha});

    // Taken away while the file is being checked: it never joins the queue.
    REQUIRE(f.host->setOverride("fp-dj", RoomPermission::UploadOwnTracks, std::nullopt));
    f.probe.deferred = true;
    uploadUntilComplete(f, *dj, 41);
    REQUIRE(f.probe.probed.size() == 1);
    REQUIRE(f.host->setOverride("fp-dj", RoomPermission::UploadOwnTracks, false));
    REQUIRE(dj->all<RoomIntentRejected>().back() == RoomIntentRejected{41, IntentRejection::MissingPermission});
    f.probe.completeAll();
    REQUIRE(findItem(f.host->snapshot(), upload_sha) == std::nullopt);
    REQUIRE(f.player.paths().size() == 3);
}

// S12.1 fix round 2 (spec: guest songs leave "quando tocam"): once a guest item has been current and the room
// moves on to another item, it leaves the host's queue (on the next scheduler turn) and frees its file.
TEST_CASE("room host: a guest track leaves the queue once it has played and frees its file",
          "[core][listening-room][room-host][room-uploads]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj", "Bia");
    uploadUntilComplete(f, *dj);
    const auto item = findItem(f.host->snapshot(), upload_sha);
    REQUIRE(item.has_value());
    // a, guest, b, c
    REQUIRE(f.player.move(LocalQueueOccurrenceId{item->id}, 1));
    f.runScheduled();
    REQUIRE(f.player.paths().size() == 4);

    REQUIRE(f.player.next());
    REQUIRE(f.host->snapshot().playback.current_item == item->id);
    f.runScheduled();
    // While it is current it stays.
    REQUIRE(f.player.paths().size() == 4);
    REQUIRE(f.uploads.discarded.empty());

    REQUIRE(f.player.next());
    f.runScheduled();
    REQUIRE(f.player.paths() == std::vector<std::filesystem::path>{"music/a.mp3", "music/b.mp3", "music/c.mp3"});
    REQUIRE(findItem(f.host->snapshot(), upload_sha) == std::nullopt);
    REQUIRE(f.uploads.discarded == std::vector<std::string>{upload_sha});
    // Host tracks that played stay; Previous goes back to them, never to the guest track.
    REQUIRE(f.player.previous());
    REQUIRE(f.player.currentIndex() == std::size_t{0});
    REQUIRE(f.player.paths().size() == 3);
}

// The last item: the end of the queue, or a wrap with repeat on, is moving on too. A guest track never yet
// current stays, and one that comes back to current before the scheduler turn stays.
TEST_CASE("room host: a played guest track also leaves at the end of the queue or when repeat wraps",
          "[core][listening-room][room-host][room-uploads]") {
    SECTION("repeat wraps to the first item") {
        HostFixture f;
        f.start();
        static_cast<void>(f.host->open({}));
        auto dj = f.join("fp-dj", "Bia");
        uploadUntilComplete(f, *dj);
        const auto item = findItem(f.host->snapshot(), upload_sha);
        REQUIRE(item.has_value());
        f.player.repeat = true;
        REQUIRE(f.player.next());
        REQUIRE(f.player.next());
        f.runScheduled();
        REQUIRE(f.player.paths().size() == 4); // not yet current: stays
        REQUIRE(f.player.next());
        REQUIRE(f.host->snapshot().playback.current_item == item->id);
        REQUIRE(f.player.next());
        REQUIRE(f.player.currentIndex() == std::size_t{0});
        f.runScheduled();
        REQUIRE(f.player.paths().size() == 3);
        REQUIRE(f.uploads.discarded == std::vector<std::string>{upload_sha});
    }
    SECTION("the queue ends and clears the selection") {
        HostFixture f;
        f.start();
        static_cast<void>(f.host->open({}));
        auto dj = f.join("fp-dj", "Bia");
        uploadUntilComplete(f, *dj);
        f.player.queue_end = QueueEndBehavior::StopClearSelection;
        for (int step = 0; step < 3; ++step)
            REQUIRE(f.player.next());
        f.player.finishTrack();
        REQUIRE_FALSE(f.player.currentIndex().has_value());
        f.runScheduled();
        REQUIRE(f.player.paths().size() == 3);
        REQUIRE(f.uploads.discarded == std::vector<std::string>{upload_sha});
    }
    SECTION("back to current before the scheduler turn") {
        HostFixture f;
        f.start();
        static_cast<void>(f.host->open({}));
        auto dj = f.join("fp-dj", "Bia");
        uploadUntilComplete(f, *dj);
        const auto item = findItem(f.host->snapshot(), upload_sha);
        REQUIRE(f.player.move(LocalQueueOccurrenceId{item->id}, 1));
        REQUIRE(f.player.next());
        REQUIRE(f.player.next());
        REQUIRE(f.player.previous());
        REQUIRE(f.host->snapshot().playback.current_item == item->id);
        f.runScheduled();
        REQUIRE(f.player.paths().size() == 4);
        REQUIRE(f.uploads.discarded.empty());
    }
}

// Fix round 2: another member declaring a hash that is on its way is refused; the owner's upload goes on.
TEST_CASE("room host: another member declaring a file already on its way is refused",
          "[core][listening-room][room-host][room-uploads]") {
    HostFixture f;
    f.start();
    static_cast<void>(f.host->open({}));
    auto dj = f.join("fp-dj", "Bia");
    auto listener = f.join("fp-listener", "Lia");
    REQUIRE(f.host->setOverride("fp-listener", RoomPermission::UploadOwnTracks, true));
    dj->intent(40, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = declaredUpload()});
    static_cast<void>(dj->link->sendControl(RoomFileOffer{upload_sha, 5000, 77}));
    REQUIRE(f.uploads.accepted.size() == 1);

    listener->intent(1, RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = declaredUpload()});
    REQUIRE(listener->all<RoomIntentRejected>() ==
            std::vector<RoomIntentRejected>{{1, IntentRejection::InvalidArgument}});
    REQUIRE(listener->all<RoomFileRequest>().empty());
    REQUIRE(f.uploads.abandoned.empty());
    REQUIRE(f.uploads.discarded.empty());
    // The DJ's upload goes on and joins the queue as the DJ's.
    static_cast<void>(dj->link->sendMedia(chunk(77, 0, 5000)));
    f.uploads.complete(upload_sha);
    const auto item = findItem(f.host->snapshot(), upload_sha);
    REQUIRE(item.has_value());
    REQUIRE(item->owner_fingerprint == "fp-dj");
    REQUIRE(dj->all<RoomIntentRejected>().empty());
}

} // namespace music_player::m0::test
