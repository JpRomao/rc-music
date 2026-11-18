// ListeningRoomGuest (S7.2) over the in-memory transport, a fake scheduler with its own clock and a
// fake local session. No network, files or audio.
#include "../support/FakeRoomTransport.hpp"

#include "m0/application/ListeningRoomGuest.hpp"
#include "m0/application/SessionAutosave.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace music_player::m0::test {
namespace {

const std::string host_hex = "abcd0102" + std::string(56, '0');
const std::string guest_hex = "fedc0304" + std::string(56, '0');
const std::string avatar_hash = std::string(64, 'a');

RoomInviteLink invite() {
    RoomInviteLink link{};
    link.room_id[0] = 7;
    link.secret[0] = 9;
    link.host_fingerprint[0] = 0xab;
    return link;
}

SessionSnapshot localSession() {
    SessionSnapshot session;
    session.queue.items = {{LocalQueueOccurrenceId{1}, "music/a.mp3"}, {LocalQueueOccurrenceId{2}, "music/b.mp3"}};
    session.queue.current_occurrence_id = LocalQueueOccurrenceId{2};
    session.position_ms = 42'000;
    return session;
}

RoomStateMessage state(const std::uint64_t version, const RoomPermissionSet permissions = {}) {
    RoomStateMessage message;
    message.snapshot.version = version;
    message.snapshot.members = {RoomMember{host_hex, "Joao", AvatarChoice{}, MemberState::Admitted}};
    message.snapshot.playback.playing = true;
    message.snapshot.playback.anchor_position_ms = static_cast<std::int64_t>(version) * 1000;
    message.my_permissions = permissions;
    return message;
}

// Tasks run when advance() moves the clock past their due time, in due order (ties: scheduling order).
struct FakeTimers final {
    struct Task final {
        std::int64_t due{};
        std::uint64_t order{};
        std::function<void()> run;
    };
    void schedule(const std::int64_t delay_ms, std::function<void()> task) {
        tasks.push_back({now + delay_ms, next_order++, std::move(task)});
    }
    void advance(const std::int64_t ms) {
        const auto end = now + ms;
        for (;;) {
            const auto next = std::min_element(tasks.begin(), tasks.end(), [](const Task& a, const Task& b) {
                return a.due != b.due ? a.due < b.due : a.order < b.order;
            });
            if (next == tasks.end() || next->due > end)
                break;
            auto task = std::move(*next);
            tasks.erase(next);
            now = task.due;
            task.run();
        }
        now = end;
    }
    std::int64_t now{0};
    std::uint64_t next_order{0};
    std::vector<Task> tasks;
};

struct GuestFixture final {
    void start() {
        guest = std::make_unique<ListeningRoomGuest>(ListeningRoomGuestDependencies{
            .connector = connector,
            .profile = profile,
            .stored_custom_avatar_sha256 = [this] { return stored_avatar; },
            .capture_session =
                [this] {
                    ++captures;
                    return local;
                },
            .restore_session =
                [this](const SessionSnapshot& session) {
                    restored.push_back(session);
                    local = session;
                },
            .schedule = [this](const std::int64_t delay_ms,
                               std::function<void()> task) { timers.schedule(delay_ms, std::move(task)); },
        });
        guest->onChanged = [this] { ++changes; };
        guest->onRejected = [this](const std::uint64_t id, const IntentRejection reason) {
            rejected.emplace_back(id, reason);
        };
        guest->onEnded = [this](const RoomLinkFailure failure) { ended.push_back(failure); };
    }
    // The connector hands the guest a new link to the host; host_side records what the guest sends.
    void connectLink() {
        auto [host_end, guest_end_link] = testing::FakeRoomLink::pair(guest_hex, host_hex);
        host_side = host_end;
        guest_end = guest_end_link;
        host_side->onControl = [this](RoomControlMessage message) { host_inbox.push_back(std::move(message)); };
        host_side->onClosed = [this](const RoomLinkFailure failure) { host_closed.push_back(failure); };
        connector.complete(guest_end_link);
    }
    void send(const RoomControlMessage& message) { REQUIRE(host_side->sendControl(message)); }
    void joinAdmitted() {
        guest->join(invite());
        connectLink();
        send(RoomJoinResult{JoinDecision::Admitted});
        REQUIRE(guest->state() == GuestRoomState::Joined);
    }
    // The network ends the link: the guest sees onClosed(failure).
    void dropLink(const RoomLinkFailure failure) const { guest_end->drop(failure); }
    template <typename T> [[nodiscard]] std::vector<T> received() const {
        std::vector<T> result;
        for (const auto& message : host_inbox)
            if (const auto* typed = std::get_if<T>(&message))
                result.push_back(*typed);
        return result;
    }

    testing::FakeRoomGuestConnector connector;
    ListenerProfile profile{"Ana", AvatarChoice{AvatarKind::Bundled, 3, {}}};
    std::optional<std::string> stored_avatar;
    SessionSnapshot local{localSession()};
    int captures{};
    std::vector<SessionSnapshot> restored;
    FakeTimers timers;
    std::shared_ptr<testing::FakeRoomLink> host_side;
    std::shared_ptr<testing::FakeRoomLink> guest_end;
    std::vector<RoomControlMessage> host_inbox;
    std::vector<RoomLinkFailure> host_closed;
    int changes{};
    std::vector<std::pair<std::uint64_t, IntentRejection>> rejected;
    std::vector<RoomLinkFailure> ended;
    std::unique_ptr<ListeningRoomGuest> guest;
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

TEST_CASE("room guest: join goes Connecting, WaitingApproval and Joined, saying hello on the link",
          "[core][listening-room][room-guest]") {
    GuestFixture f;
    f.start();
    REQUIRE(f.guest->state() == GuestRoomState::Idle);

    f.guest->join(invite());
    REQUIRE(f.guest->state() == GuestRoomState::Connecting);
    REQUIRE(f.connector.requested == invite());
    REQUIRE(f.changes == 1);

    f.connectLink();
    REQUIRE(f.guest->state() == GuestRoomState::Connecting);
    const auto hellos = f.received<RoomHello>();
    REQUIRE(hellos.size() == 1);
    REQUIRE(hellos.front() == RoomHello{room_protocol_version, "Ana", AvatarChoice{AvatarKind::Bundled, 3, {}}});

    f.send(RoomJoinResult{JoinDecision::Pending});
    REQUIRE(f.guest->state() == GuestRoomState::WaitingApproval);
    REQUIRE(f.captures == 0);
    f.send(state(4)); // nothing is applied before admission
    REQUIRE(f.guest->snapshot().version == 0);

    f.send(RoomJoinResult{JoinDecision::Admitted});
    REQUIRE(f.guest->state() == GuestRoomState::Joined);
    REQUIRE(f.captures == 1);
    REQUIRE(f.guest->holdsLocalSession());

    const auto permissions = RoomPermissionSet{}.set(static_cast<std::size_t>(RoomPermission::Pause));
    const auto changes_before = f.changes;
    f.send(state(5, permissions));
    REQUIRE(f.guest->snapshot() == state(5).snapshot);
    REQUIRE(f.guest->permissions() == permissions);
    REQUIRE(f.changes == changes_before + 1);
    REQUIRE(f.ended.empty());
}

TEST_CASE("room guest: a state with a version not above the current one is ignored, gaps are fine",
          "[core][listening-room][room-guest]") {
    GuestFixture f;
    f.start();
    f.joinAdmitted();
    f.send(state(5));
    const auto changes = f.changes;

    f.send(state(3));
    REQUIRE(f.guest->snapshot().version == 5);
    auto same_version = state(5);
    same_version.snapshot.playback.playing = false;
    f.send(same_version);
    REQUIRE(f.guest->snapshot() == state(5).snapshot);
    REQUIRE(f.changes == changes);

    f.send(state(9));
    REQUIRE(f.guest->snapshot().version == 9);
    REQUIRE(f.changes == changes + 1);
}

TEST_CASE("room guest: leave restores exactly the queue and position from before the room",
          "[core][listening-room][room-guest]") {
    GuestFixture f;
    f.start();
    const auto before = f.local;
    f.joinAdmitted();
    f.send(state(2));
    // While in the room the local player is driven by the room.
    f.local = SessionSnapshot{};

    f.guest->leave();
    REQUIRE(f.guest->state() == GuestRoomState::Idle);
    REQUIRE(f.restored.size() == 1);
    REQUIRE(f.restored.front() == before);
    REQUIRE(f.local == before);
    REQUIRE(f.guest->snapshot() == RoomSnapshot{});
    REQUIRE_FALSE(f.guest->holdsLocalSession());
    REQUIRE(f.host_closed == std::vector{RoomLinkFailure::Closed});
    REQUIRE(f.ended.empty());
    REQUIRE_FALSE(f.guest->endReason().has_value());

    f.guest->leave(); // nothing held any more
    REQUIRE(f.restored.size() == 1);
}

TEST_CASE("room guest: request ids start at 1 and a rejection reaches onRejected with its id",
          "[core][listening-room][room-guest]") {
    GuestFixture f;
    f.start();
    f.joinAdmitted();

    REQUIRE(f.guest->sendIntent(RoomIntent{.kind = RoomIntentKind::Next}) == 1);
    REQUIRE(f.guest->sendIntent(RoomIntent{.kind = RoomIntentKind::Seek, .position_ms = 1234}) == 2);
    const auto intents = f.received<RoomIntentMessage>();
    REQUIRE(intents.size() == 2);
    REQUIRE(intents[0] == RoomIntentMessage{1, RoomIntent{.kind = RoomIntentKind::Next}});
    REQUIRE(intents[1] == RoomIntentMessage{2, RoomIntent{.kind = RoomIntentKind::Seek, .position_ms = 1234}});

    f.send(RoomIntentRejected{2, IntentRejection::MissingPermission});
    REQUIRE(f.rejected ==
            std::vector<std::pair<std::uint64_t, IntentRejection>>{{2, IntentRejection::MissingPermission}});
    REQUIRE(f.guest->state() == GuestRoomState::Joined);
}

TEST_CASE("room guest: intents sent while not joined send nothing and return 0", "[core][listening-room][room-guest]") {
    GuestFixture f;
    f.start();
    const RoomIntent next{.kind = RoomIntentKind::Next};
    REQUIRE(f.guest->sendIntent(next) == 0); // Idle

    f.guest->join(invite());
    REQUIRE(f.guest->sendIntent(next) == 0); // Connecting, no link
    f.connectLink();
    REQUIRE(f.guest->sendIntent(next) == 0); // Connecting, hello sent
    f.send(RoomJoinResult{JoinDecision::Pending});
    REQUIRE(f.guest->sendIntent(next) == 0); // WaitingApproval
    f.send(RoomJoinResult{JoinDecision::Admitted});
    f.dropLink(RoomLinkFailure::Unreachable);
    REQUIRE(f.guest->state() == GuestRoomState::Reconnecting);
    REQUIRE(f.guest->sendIntent(next) == 0); // Reconnecting
    REQUIRE(f.received<RoomIntentMessage>().empty());

    f.connectLink();
    f.send(RoomJoinResult{JoinDecision::Admitted});
    REQUIRE(f.guest->sendIntent(next) == 1); // the first id actually sent
    f.send(RoomBye{RoomByeReason::Closed});
    REQUIRE(f.guest->sendIntent(next) == 0); // Ended
    REQUIRE(f.received<RoomIntentMessage>().size() == 1);
}

TEST_CASE("room guest: a dropped link reconnects with the same invite inside 30 s and is Joined again",
          "[core][listening-room][room-guest]") {
    GuestFixture f;
    f.start();
    f.joinAdmitted();
    f.send(state(6));
    f.connector.requested.reset();

    f.dropLink(RoomLinkFailure::Unreachable);
    REQUIRE(f.guest->state() == GuestRoomState::Reconnecting);
    REQUIRE(f.connector.requested == invite());
    REQUIRE(f.guest->snapshot().version == 6); // the last view stays while reconnecting

    f.timers.advance(room_reconnect_window_ms - 1000);
    f.connectLink();
    REQUIRE(f.received<RoomHello>().size() == 2);
    f.send(RoomJoinResult{JoinDecision::Admitted});
    REQUIRE(f.guest->state() == GuestRoomState::Joined);
    REQUIRE(f.captures == 1); // the pre-room session is not captured again
    f.send(state(6));         // the host resumes the member with the same version
    REQUIRE(f.guest->snapshot().version == 6);

    f.timers.advance(room_reconnect_window_ms * 2); // the old window has no effect any more
    REQUIRE(f.guest->state() == GuestRoomState::Joined);
    REQUIRE(f.ended.empty());
    REQUIRE(f.restored.empty());
}

TEST_CASE("room guest: after 30 s without a host the room ends and the local session comes back",
          "[core][listening-room][room-guest]") {
    GuestFixture f;
    f.start();
    const auto before = f.local;
    f.joinAdmitted();
    f.local = SessionSnapshot{};

    f.dropLink(RoomLinkFailure::Unreachable);
    f.timers.advance(room_reconnect_window_ms - 1);
    REQUIRE(f.guest->state() == GuestRoomState::Reconnecting);
    REQUIRE(f.ended.empty());

    f.timers.advance(1);
    REQUIRE(f.guest->state() == GuestRoomState::Ended);
    REQUIRE(f.local == before);
    REQUIRE(f.restored == std::vector{before});
    REQUIRE(f.ended == std::vector{RoomLinkFailure::Unreachable});
    REQUIRE(f.guest->endReason() == GuestRoomEnd{GuestEndReason::LinkLost, RoomLinkFailure::Unreachable});
    REQUIRE(f.guest->snapshot() == RoomSnapshot{});
}

TEST_CASE("room guest: failures inside the reconnect window keep retrying with a bounded backoff",
          "[core][listening-room][room-guest]") {
    GuestFixture f;
    f.start();
    f.joinAdmitted();
    f.connector.requested.reset();

    f.dropLink(RoomLinkFailure::Closed);
    REQUIRE(f.connector.requested == invite()); // first attempt at once
    f.connector.requested.reset();
    f.connector.fail(RoomLinkFailure::HostOffline);
    REQUIRE(f.guest->state() == GuestRoomState::Reconnecting);

    // Each failed attempt waits initial, 2x, 4x... up to the maximum before the next connect.
    std::vector<std::int64_t> attempts;
    std::int64_t expected_delay = room_reconnect_retry_initial_ms;
    for (int i = 0; i < 4; ++i) {
        f.timers.advance(expected_delay - 1);
        REQUIRE_FALSE(f.connector.requested.has_value());
        f.timers.advance(1);
        REQUIRE(f.connector.requested == invite());
        attempts.push_back(f.timers.now);
        f.connector.requested.reset();
        f.connector.fail(i % 2 == 0 ? RoomLinkFailure::SignalingUnavailable : RoomLinkFailure::HostOffline);
        REQUIRE(f.guest->state() == GuestRoomState::Reconnecting);
        expected_delay = std::min<std::int64_t>(expected_delay * 2, room_reconnect_retry_maximum_ms);
    }
    REQUIRE(attempts == std::vector<std::int64_t>{1000, 3000, 7000, 15000});

    // A link that connects and then drops before admission also counts as a failed attempt.
    f.timers.advance(room_reconnect_retry_maximum_ms); // 23000
    REQUIRE(f.connector.requested.has_value());
    f.connector.requested.reset();
    f.connectLink();
    f.dropLink(RoomLinkFailure::Refused);
    REQUIRE(f.guest->state() == GuestRoomState::Reconnecting);

    f.timers.advance(room_reconnect_window_ms - f.timers.now - 1);
    REQUIRE(f.guest->state() == GuestRoomState::Reconnecting);
    f.timers.advance(1); // the window ends with the last failure seen
    REQUIRE(f.guest->state() == GuestRoomState::Ended);
    REQUIRE(f.ended == std::vector{RoomLinkFailure::Refused});
    f.timers.advance(room_reconnect_window_ms); // no attempt after the end
    REQUIRE_FALSE(f.connector.requested.has_value());
}

TEST_CASE("room guest: the window ending during an attempt cancels it", "[core][listening-room][room-guest]") {
    GuestFixture f;
    f.start();
    f.joinAdmitted();
    f.dropLink(RoomLinkFailure::Unreachable);
    f.timers.advance(room_reconnect_window_ms);
    REQUIRE(f.guest->state() == GuestRoomState::Ended);
    REQUIRE(f.connector.cancelled);
}

TEST_CASE("room guest: Bye from the host ends the room with onEnded(Closed) and restores the session",
          "[core][listening-room][room-guest]") {
    GuestFixture f;
    f.start();
    const auto before = f.local;
    f.joinAdmitted();
    f.send(state(3, RoomPermissionSet{}.set()));
    f.local = SessionSnapshot{};
    f.connector.requested.reset();

    f.send(RoomBye{RoomByeReason::Closed});
    REQUIRE(f.guest->state() == GuestRoomState::Ended);
    REQUIRE(f.ended == std::vector{RoomLinkFailure::Closed});
    REQUIRE(f.guest->endReason() == GuestRoomEnd{GuestEndReason::Closed, RoomLinkFailure::Closed});
    REQUIRE(f.local == before);
    REQUIRE(f.guest->permissions() == RoomPermissionSet{});
    REQUIRE(f.guest_end->closed);
    f.timers.advance(room_reconnect_window_ms * 2); // no reconnect after a Bye
    REQUIRE_FALSE(f.connector.requested.has_value());
    REQUIRE(f.ended.size() == 1);
}

TEST_CASE("room guest: every Bye reason ends the room and says which", "[core][listening-room][room-guest]") {
    struct Case final {
        RoomByeReason bye;
        GuestEndReason reason;
        RoomLinkFailure failure;
    };
    for (const auto& c :
         {Case{RoomByeReason::Closed, GuestEndReason::Closed, RoomLinkFailure::Closed},
          Case{RoomByeReason::Revoked, GuestEndReason::Revoked, RoomLinkFailure::Closed},
          Case{RoomByeReason::Kicked, GuestEndReason::Kicked, RoomLinkFailure::Refused},
          Case{RoomByeReason::VersionMismatch, GuestEndReason::VersionMismatch, RoomLinkFailure::VersionMismatch},
          Case{RoomByeReason::Full, GuestEndReason::RoomFull, RoomLinkFailure::Refused}}) {
        GuestFixture f;
        f.start();
        f.guest->join(invite());
        f.connectLink();
        f.send(RoomBye{c.bye}); // a Bye may come right after the hello (VersionMismatch) or later
        REQUIRE(f.guest->state() == GuestRoomState::Ended);
        REQUIRE(f.guest->endReason() == GuestRoomEnd{c.reason, c.failure});
        REQUIRE(f.ended == std::vector{c.failure});
        REQUIRE(f.restored.empty()); // nothing was captured before admission
    }
}

TEST_CASE("room guest: a rejected JoinResult ends the room with a reason the UI can map",
          "[core][listening-room][room-guest]") {
    struct Case final {
        JoinDecision decision;
        GuestEndReason reason;
    };
    for (const auto& c : {Case{JoinDecision::RejectedFull, GuestEndReason::RoomFull},
                          Case{JoinDecision::RejectedBlocked, GuestEndReason::Blocked},
                          Case{JoinDecision::RejectedClosed, GuestEndReason::Rejected}}) {
        GuestFixture f;
        f.start();
        f.guest->join(invite());
        f.connectLink();
        f.send(RoomJoinResult{JoinDecision::Pending});
        f.send(RoomJoinResult{c.decision});
        REQUIRE(f.guest->state() == GuestRoomState::Ended);
        REQUIRE(f.guest->endReason() == GuestRoomEnd{c.reason, RoomLinkFailure::Refused});
        REQUIRE(f.ended == std::vector{RoomLinkFailure::Refused});
        REQUIRE(f.guest_end->closed);
        REQUIRE(f.captures == 0);
    }
}

TEST_CASE("room guest: the first connection failing ends at once and a new join can start",
          "[core][listening-room][room-guest]") {
    GuestFixture f;
    f.start();
    f.guest->join(invite());
    f.connector.fail(RoomLinkFailure::HostOffline);
    REQUIRE(f.guest->state() == GuestRoomState::Ended);
    REQUIRE(f.guest->endReason() == GuestRoomEnd{GuestEndReason::LinkLost, RoomLinkFailure::HostOffline});
    REQUIRE(f.ended == std::vector{RoomLinkFailure::HostOffline});
    REQUIRE(f.restored.empty());
    REQUIRE(f.timers.tasks.empty());

    f.guest->join(invite());
    REQUIRE(f.guest->state() == GuestRoomState::Connecting);
    REQUIRE_FALSE(f.guest->endReason().has_value());
    f.connectLink();
    f.dropLink(RoomLinkFailure::Closed); // dropped before admission: no reconnect
    REQUIRE(f.guest->state() == GuestRoomState::Ended);
    REQUIRE(f.ended == std::vector{RoomLinkFailure::HostOffline, RoomLinkFailure::Closed});
}

TEST_CASE("room guest: a custom avatar is sent only when the stored image matches its hash",
          "[core][listening-room][room-guest]") {
    const AvatarChoice custom{AvatarKind::Custom, 0, avatar_hash};
    const AvatarChoice fallback{AvatarKind::Bundled, 0, {}};
    struct Case final {
        std::optional<std::string> stored;
        AvatarChoice sent;
    };
    for (const auto& c :
         {Case{avatar_hash, custom}, Case{std::string(64, 'b'), fallback}, Case{std::nullopt, fallback}}) {
        GuestFixture f;
        f.profile.avatar = custom;
        f.stored_avatar = c.stored;
        f.start();
        f.guest->join(invite());
        f.connectLink();
        const auto hellos = f.received<RoomHello>();
        REQUIRE(hellos.size() == 1);
        REQUIRE(hellos.front().avatar == c.sent);
        REQUIRE(hellos.front().nickname == "Ana");
    }
}

TEST_CASE("room guest: while in the room the session autosave keeps the pre-room session",
          "[core][listening-room][room-guest]") {
    GuestFixture f;
    f.start();
    const auto before = f.local;
    RecordingSessionWriter writer;
    SessionAutosave autosave{writer, before};

    f.joinAdmitted();
    REQUIRE(f.guest->sessionForAutosave(f.local) == before);
    // The room drives the local player: what the app would autosave now is not the user's queue.
    SessionSnapshot room_driven;
    room_driven.queue.items = {{LocalQueueOccurrenceId{9}, "room-cache/x.mp3"}};
    room_driven.queue.current_occurrence_id = LocalQueueOccurrenceId{9};
    room_driven.position_ms = 5;
    REQUIRE(isValidSessionSnapshot(room_driven));
    f.local = room_driven;
    REQUIRE(f.guest->sessionForAutosave(f.local) == before);
    REQUIRE_FALSE(autosave.scheduleStructural(f.guest->sessionForAutosave(f.local)));
    REQUIRE(autosave.flush() == SessionSaveStatus::Saved);
    REQUIRE(writer.writes.empty());

    f.dropLink(RoomLinkFailure::Unreachable); // still held while reconnecting
    REQUIRE(f.guest->sessionForAutosave(f.local) == before);

    f.guest->leave();
    REQUIRE(f.local == before);
    REQUIRE_FALSE(f.guest->holdsLocalSession());
    auto edited = f.local;
    edited.position_ms = 1;
    REQUIRE(f.guest->sessionForAutosave(edited) == edited);
}

TEST_CASE("room guest: after reconnecting to a restarted host the first state is taken whatever its version",
          "[core][listening-room][room-guest]") {
    GuestFixture f;
    f.start();
    f.joinAdmitted();
    f.send(state(40));
    f.dropLink(RoomLinkFailure::HostOffline);
    f.connectLink();
    f.send(RoomJoinResult{JoinDecision::Admitted});
    f.send(state(2));
    REQUIRE(f.guest->snapshot().version == 2);
    f.send(state(1));
    REQUIRE(f.guest->snapshot().version == 2);
    f.send(state(3));
    REQUIRE(f.guest->snapshot().version == 3);
}

TEST_CASE("room guest: a reconnect answered Pending waits for approval and the old window stops",
          "[core][listening-room][room-guest]") {
    GuestFixture f;
    f.start();
    const auto before = f.local;
    f.joinAdmitted();
    f.dropLink(RoomLinkFailure::HostOffline);
    f.connectLink();
    f.send(RoomJoinResult{JoinDecision::Pending}); // the host forgot this member
    REQUIRE(f.guest->state() == GuestRoomState::WaitingApproval);
    f.timers.advance(room_reconnect_window_ms * 2);
    REQUIRE(f.guest->state() == GuestRoomState::WaitingApproval);
    REQUIRE(f.guest->holdsLocalSession());

    f.local = SessionSnapshot{};
    f.guest->leave();
    REQUIRE(f.local == before);
    REQUIRE(f.captures == 1);
}

TEST_CASE("room guest: an intent the link refuses returns 0 and its id is not reused",
          "[core][listening-room][room-guest]") {
    GuestFixture f;
    f.start();
    f.joinAdmitted();
    f.guest_end->accepts_control = [](const RoomControlMessage&) { return false; };
    REQUIRE(f.guest->sendIntent(RoomIntent{.kind = RoomIntentKind::Pause}) == 0);
    f.guest_end->accepts_control = nullptr;
    REQUIRE(f.guest->sendIntent(RoomIntent{.kind = RoomIntentKind::Resume}) == 2);
    REQUIRE(f.received<RoomIntentMessage>().size() == 1);
}

TEST_CASE("room guest: destroying the guest cancels the attempt and calls nothing back",
          "[core][listening-room][room-guest]") {
    GuestFixture f;
    f.start();
    f.joinAdmitted();
    f.dropLink(RoomLinkFailure::Unreachable);
    const auto changes = f.changes;
    f.guest.reset();
    REQUIRE(f.connector.cancelled);
    f.timers.advance(room_reconnect_window_ms * 2);
    REQUIRE(f.changes == changes);
    REQUIRE(f.ended.empty());
    REQUIRE(f.restored.empty());

    GuestFixture g;
    g.start();
    g.joinAdmitted();
    g.guest.reset();
    REQUIRE(g.guest_end->closed);
    REQUIRE(g.host_closed == std::vector{RoomLinkFailure::Closed});
}

TEST_CASE("room guest: join while already in a room does nothing", "[core][listening-room][room-guest]") {
    GuestFixture f;
    f.start();
    f.joinAdmitted();
    f.connector.requested.reset();
    auto other = invite();
    other.room_id[1] = 1;
    f.guest->join(other);
    REQUIRE(f.guest->state() == GuestRoomState::Joined);
    REQUIRE_FALSE(f.connector.requested.has_value());
}

} // namespace music_player::m0::test
