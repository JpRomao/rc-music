// ListeningRoomGuest (S7.2) over the in-memory transport, a fake scheduler with its own clock and a
// fake local session. No network, files or audio.
#include "../support/FakeRoomFiles.hpp"
#include "../support/FakeRoomHost.hpp"
#include "../support/FakeRoomTransport.hpp"

#include "m0/application/ListeningRoomGuest.hpp"
#include "m0/application/RoomPlaybackFollower.hpp"
#include "m0/application/SessionAutosave.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
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
                    if (on_restore)
                        on_restore();
                    restored.push_back(session);
                    local = session;
                },
            .schedule = [this](const std::int64_t delay_ms,
                               std::function<void()> task) { timers.schedule(delay_ms, std::move(task)); },
            .files = &inbox,
            .follower = follower,
            .local_clock_ms = follower != nullptr ? std::function<std::int64_t()>{[this] { return timers.now; }}
                                                  : std::function<std::int64_t()>{},
            .upload_catalog = &upload_catalog,
            .upload_files = &upload_outbox,
        });
        guest->onUploadEnded = [this](const GuestUploadResult result) { uploads_ended.push_back(result); };
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
    testing::FakeRoomFileInbox inbox;
    testing::FakeRoomTrackCatalog upload_catalog;
    testing::FakeRoomFileOutbox upload_outbox;
    std::vector<GuestUploadResult> uploads_ended;
    std::shared_ptr<testing::FakeRoomLink> host_side;
    std::shared_ptr<testing::FakeRoomLink> guest_end;
    std::vector<RoomControlMessage> host_inbox;
    std::vector<RoomLinkFailure> host_closed;
    int changes{};
    std::vector<std::pair<std::uint64_t, IntentRejection>> rejected;
    std::vector<RoomLinkFailure> ended;
    // S9.1: set before start() to follow the host's time.
    RoomPlaybackFollower* follower{nullptr};
    std::function<void()> on_restore;
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

TEST_CASE("room guest: leave tells the host with Bye before closing the link and does not reconnect",
          "[core][listening-room][room-guest]") {
    for (const bool admitted : {true, false}) {
        GuestFixture f;
        f.start();
        f.guest->join(invite());
        f.connectLink();
        f.send(RoomJoinResult{admitted ? JoinDecision::Admitted : JoinDecision::Pending});
        f.connector.requested.reset();
        // The host closes the link when it reads the Bye: that close must not look like a drop.
        f.host_side->onControl = [&f](RoomControlMessage message) {
            f.host_inbox.push_back(std::move(message));
            if (std::holds_alternative<RoomBye>(f.host_inbox.back()))
                f.host_side->close();
        };

        f.guest->leave();
        REQUIRE(f.received<RoomBye>() == std::vector{RoomBye{RoomByeReason::Closed}});
        REQUIRE(std::holds_alternative<RoomBye>(f.host_inbox.back()));
        REQUIRE(f.guest_end->closed);
        REQUIRE(f.guest->state() == GuestRoomState::Idle);
        REQUIRE_FALSE(f.connector.requested.has_value());
        f.timers.advance(room_reconnect_window_ms * 2);
        REQUIRE(f.guest->state() == GuestRoomState::Idle);
        REQUIRE_FALSE(f.connector.requested.has_value());
        REQUIRE(f.ended.empty());
    }
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
    f.connector.fail(RoomLinkFailure::Unreachable);
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
        f.connector.fail(i % 2 == 0 ? RoomLinkFailure::SignalingUnavailable : RoomLinkFailure::Unreachable);
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

TEST_CASE("room guest: the relay saying the host is gone ends a reconnect at once as a closed room",
          "[core][listening-room][room-guest]") {
    for (const bool after_a_retry : {false, true}) {
        GuestFixture f;
        f.start();
        const auto before = f.local;
        f.joinAdmitted();
        f.local = SessionSnapshot{};
        f.dropLink(RoomLinkFailure::Closed); // the host app went away without a Bye
        REQUIRE(f.guest->state() == GuestRoomState::Reconnecting);
        if (after_a_retry) {
            f.connector.fail(RoomLinkFailure::Unreachable);
            f.timers.advance(room_reconnect_retry_initial_ms);
            REQUIRE(f.guest->state() == GuestRoomState::Reconnecting);
        }
        f.connector.requested.reset();

        f.connector.fail(RoomLinkFailure::HostOffline);
        REQUIRE(f.guest->state() == GuestRoomState::Ended);
        REQUIRE(f.guest->endReason() == GuestRoomEnd{GuestEndReason::Closed, RoomLinkFailure::HostOffline});
        REQUIRE(f.ended == std::vector{RoomLinkFailure::HostOffline});
        REQUIRE(f.local == before);
        f.timers.advance(room_reconnect_window_ms * 2); // no attempt after the end
        REQUIRE_FALSE(f.connector.requested.has_value());
        REQUIRE(f.ended.size() == 1);
    }
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

namespace {
std::string hashOf(const char letter) { return std::string(64, letter); }

// A room queue of items 11, 12, ... with hashes "aaa...", "bbb...", ...; the first one is current.
RoomStateMessage roomQueue(const std::uint64_t version, const std::string& letters) {
    auto message = state(version);
    std::uint64_t id = 11;
    for (const char letter : letters) {
        RoomItem item;
        item.id = id++;
        item.origin = RoomItemOrigin::HostTrack;
        item.owner_fingerprint = host_hex;
        item.content_sha256 = hashOf(letter);
        item.size_bytes = 1000;
        item.added_by = "Joao";
        message.snapshot.queue.push_back(item);
    }
    if (!message.snapshot.queue.empty())
        message.snapshot.playback.current_item = message.snapshot.queue.front().id;
    return message;
}

std::vector<std::string> requestedHashes(const GuestFixture& f) {
    std::vector<std::string> result;
    for (const auto& request : f.received<RoomFileRequest>())
        result.push_back(request.content_sha256);
    return result;
}
} // namespace

TEST_CASE("room guest: asks for the current item first, then the next room_prefetch_items in queue order",
          "[core][listening-room][room-guest][room-files]") {
    GuestFixture f;
    f.inbox.deferred = true;
    f.start();
    f.joinAdmitted();
    f.send(roomQueue(1, "abcdef"));
    REQUIRE(f.received<RoomFileRequest>().empty()); // after the cache answered
    f.inbox.answerAll();
    REQUIRE(requestedHashes(f) == std::vector{hashOf('a'), hashOf('b'), hashOf('c'), hashOf('d')});
    REQUIRE(f.received<RoomFileRequest>().front().offset == 0);
    REQUIRE(!f.guest->localFile(hashOf('a')));
}

TEST_CASE("room guest: a hash already in the cache is not asked for again, a partial one resumes",
          "[core][listening-room][room-guest][room-files]") {
    GuestFixture f;
    f.inbox.files[hashOf('b')] = "cache/b";
    f.inbox.partial[hashOf('a')] = 3 * 65536;
    f.start();
    f.joinAdmitted();
    // Item 13 has the same file as item 12: one request, one copy.
    f.send(roomQueue(1, "abbc"));
    REQUIRE(f.received<RoomFileRequest>() == std::vector<RoomFileRequest>{{hashOf('a'), 3 * 65536}, {hashOf('c'), 0}});
    REQUIRE(f.guest->localFile(hashOf('b')) == std::optional<std::filesystem::path>{"cache/b"});

    // Offers it did not ask for are ignored; the requested one is accepted and its chunks go to the cache.
    f.send(RoomFileOffer{hashOf('b'), 1000, 7});
    f.send(RoomFileOffer{hashOf('a'), 1000, 8});
    REQUIRE(f.inbox.accepted == std::vector<RoomFileOffer>{{hashOf('a'), 1000, 8}});
    REQUIRE(f.host_side->sendMedia({8, 3 * 65536, {1, 2, 3}}));
    REQUIRE(f.host_side->sendMedia({7, 0, {1}}));
    REQUIRE(f.inbox.consumed == std::vector<RoomFileChunk>{{8, 3 * 65536, {1, 2, 3}}});
    const auto changes = f.changes;
    f.inbox.complete(hashOf('a'));
    REQUIRE(f.guest->localFile(hashOf('a')) ==
            std::optional<std::filesystem::path>{std::filesystem::path{"cache"} / hashOf('a')});
    REQUIRE(f.changes > changes);
    f.send(roomQueue(2, "abbcd")); // d is past the window
    REQUIRE(f.received<RoomFileRequest>().size() == 2);
}

TEST_CASE("room guest: a queue change abandons transfers of items that left and asks for the new ones",
          "[core][listening-room][room-guest][room-files]") {
    GuestFixture f;
    f.start();
    f.joinAdmitted();
    f.send(roomQueue(1, "abcde"));
    std::uint32_t id = 20;
    for (const char letter : std::string{"abcd"})
        f.send(RoomFileOffer{hashOf(letter), 1000, id++}); // a=20 b=21 c=22 d=23
    REQUIRE(f.inbox.accepted.size() == 4);

    // The host moved on to b and dropped c: a and c stop, a is marked played, e comes in.
    auto next = roomQueue(2, "bde");
    next.snapshot.queue[0].id = 12;
    next.snapshot.queue[1].id = 14;
    next.snapshot.queue[2].id = 15;
    next.snapshot.playback.current_item = 12;
    f.send(next);
    std::vector<std::uint32_t> abandoned = f.inbox.abandoned;
    std::sort(abandoned.begin(), abandoned.end());
    REQUIRE(abandoned == std::vector<std::uint32_t>{20, 22});
    REQUIRE(f.inbox.played == std::vector{hashOf('a')});
    REQUIRE(requestedHashes(f).back() == hashOf('e'));
    REQUIRE(requestedHashes(f).size() == 5);
    // A late chunk of an abandoned transfer is not passed on.
    REQUIRE(f.host_side->sendMedia({20, 0, {1}}));
    REQUIRE(f.inbox.consumed.empty());
}

TEST_CASE("room guest: a dropped link abandons transfers and the next admission resumes them",
          "[core][listening-room][room-guest][room-files]") {
    GuestFixture f;
    f.start();
    f.joinAdmitted();
    f.send(roomQueue(1, "a"));
    f.send(RoomFileOffer{hashOf('a'), 1000, 5});
    f.dropLink(RoomLinkFailure::Unreachable);
    REQUIRE(f.inbox.abandoned == std::vector<std::uint32_t>{5});
    REQUIRE(f.inbox.purges == 0);

    f.inbox.partial[hashOf('a')] = 65536;
    f.connectLink();
    f.send(RoomJoinResult{JoinDecision::Admitted});
    f.send(roomQueue(1, "a")); // same version: the host kept it
    REQUIRE(f.received<RoomFileRequest>() == std::vector<RoomFileRequest>{{hashOf('a'), 0}, {hashOf('a'), 65536}});
}

TEST_CASE("room guest: leaving or the end of the room abandons transfers, then empties the cache",
          "[core][listening-room][room-guest][room-files]") {
    GuestFixture f;
    f.start();
    f.joinAdmitted();
    f.send(roomQueue(1, "ab"));
    f.send(RoomFileOffer{hashOf('a'), 1000, 5});
    f.send(RoomFileOffer{hashOf('b'), 1000, 6});
    f.inbox.complete(hashOf('b'));
    f.inbox.log.clear();
    f.guest->leave();
    REQUIRE(f.inbox.log == std::vector<std::string>{"abandon:5", "purge"});
    REQUIRE(!f.guest->localFile(hashOf('b')));

    f.inbox.log.clear();
    f.inbox.files.clear();
    f.joinAdmitted();
    f.send(roomQueue(2, "a"));
    f.send(RoomFileOffer{hashOf('a'), 1000, 7});
    f.send(RoomBye{RoomByeReason::Closed});
    REQUIRE(f.guest->state() == GuestRoomState::Ended);
    REQUIRE(f.inbox.log.back() == "purge");
    REQUIRE(std::find(f.inbox.log.begin(), f.inbox.log.end(), "abandon:7") != f.inbox.log.end());
    REQUIRE(f.inbox.purges == 2);
}

TEST_CASE("room guest: keeps the host's lyrics for the current item only",
          "[core][listening-room][room-guest][room-files]") {
    GuestFixture f;
    f.start();
    f.joinAdmitted();
    f.send(roomQueue(1, "ab"));
    f.send(RoomLyrics{12, "[00:01.00]outra"}); // not the current item
    REQUIRE(!f.guest->lyrics());
    f.send(RoomLyrics{11, "[00:01.00]la"});
    REQUIRE(f.guest->lyrics() == std::optional{RoomLyrics{11, "[00:01.00]la"}});

    auto next = roomQueue(2, "b");
    next.snapshot.queue[0].id = 12;
    next.snapshot.playback.current_item = 12;
    f.send(next);
    REQUIRE(!f.guest->lyrics());
    f.send(RoomLyrics{12, "[00:02.00]lo"});
    f.guest->leave();
    REQUIRE(!f.guest->lyrics());
}

// Control and media are separate channels: a chunk can overtake its offer.
TEST_CASE("room guest: chunks that arrive before their offer are kept for it",
          "[core][listening-room][room-guest][room-files]") {
    GuestFixture f;
    f.start();
    f.joinAdmitted();
    f.send(roomQueue(1, "a"));
    REQUIRE(f.host_side->sendMedia({9, 0, {1}}));
    REQUIRE(f.host_side->sendMedia({9, 1, {2}}));
    REQUIRE(f.inbox.consumed.empty());
    f.send(RoomFileOffer{hashOf('a'), 2, 9});
    REQUIRE(f.inbox.consumed == std::vector<RoomFileChunk>{{9, 0, {1}}, {9, 1, {2}}});

    // With nothing asked for, stray chunks are not kept.
    f.inbox.complete(hashOf('a'));
    REQUIRE(f.host_side->sendMedia({10, 0, {1}}));
    f.send(RoomFileOffer{hashOf('a'), 1, 10});
    REQUIRE(f.inbox.consumed.size() == 2);
}

// S13.1: the offer can come after the whole file. The chunks past what is kept are gone and no later
// chunk shows the gap, so the guest asks again from what it has instead of waiting forever.
TEST_CASE("room guest: an offer that comes after more chunks than are kept asks again from the partial file",
          "[core][listening-room][room-guest][room-files]") {
    constexpr std::size_t chunk = 64 * 1024;
    constexpr std::size_t kept = 2 * 1024 * 1024 / chunk;
    GuestFixture f;
    f.start();
    f.joinAdmitted();
    f.send(roomQueue(1, "a"));
    REQUIRE(requestedHashes(f) == std::vector{hashOf('a')});
    for (std::size_t index = 0; index < kept + 3; ++index)
        REQUIRE(f.host_side->sendMedia({9, index * chunk, std::vector<std::uint8_t>(chunk, 1)}));
    f.inbox.partial[hashOf('a')] = kept * chunk; // what the receiver wrote from the kept chunks
    f.send(RoomFileOffer{hashOf('a'), (kept + 3) * chunk, 9});

    REQUIRE(f.inbox.consumed.size() == kept);
    REQUIRE(f.inbox.abandoned == std::vector<std::uint32_t>{9});
    REQUIRE(requestedHashes(f) == std::vector{hashOf('a'), hashOf('a')});
    REQUIRE(f.received<RoomFileRequest>().back() == RoomFileRequest{hashOf('a'), kept * chunk});

    // The host replaces the transfer from there; a late chunk of the old one is dropped.
    REQUIRE(f.host_side->sendMedia({9, (kept + 3) * chunk, {1}}));
    f.send(RoomFileOffer{hashOf('a'), (kept + 3) * chunk, 10});
    REQUIRE(f.host_side->sendMedia({10, kept * chunk, {2}}));
    REQUIRE(f.inbox.consumed.size() == kept + 1);
    REQUIRE(f.inbox.consumed.back() == RoomFileChunk{10, kept * chunk, {2}});
}

TEST_CASE("room guest: a newer offer for a file being received replaces the stale one",
          "[core][listening-room][room-guest][room-files]") {
    GuestFixture f;
    f.start();
    f.joinAdmitted();
    f.send(roomQueue(1, "ab")); // asks for a and b
    // The host drops a and brings it back before serving the first request: the guest asks again.
    auto without_a = roomQueue(2, "b");
    without_a.snapshot.queue[0].id = 12;
    without_a.snapshot.playback.current_item = 12;
    f.send(without_a);
    f.send(roomQueue(3, "ab"));
    REQUIRE(requestedHashes(f) == std::vector{hashOf('a'), hashOf('b'), hashOf('a')});

    // The offer for the first request arrives after the second request; then the host replaces it.
    f.send(RoomFileOffer{hashOf('a'), 1000, 30});
    f.inbox.log.clear();
    f.send(RoomFileOffer{hashOf('a'), 1000, 31});
    REQUIRE(f.inbox.log == std::vector<std::string>{"abandon:30", "accept:31"});
    REQUIRE(f.host_side->sendMedia({31, 0, {1}}));
    REQUIRE(f.inbox.consumed == std::vector<RoomFileChunk>{{31, 0, {1}}});
    f.inbox.complete(hashOf('a'));
    REQUIRE(f.guest->localFile(hashOf('a')));

    // A late chunk of the replaced transfer is dropped, not kept for an offer that will never come,
    // so it takes no room from the next transfer's early chunks.
    REQUIRE(f.host_side->sendMedia({30, 0, std::vector<std::uint8_t>(2 * 1024 * 1024, 1)}));
    REQUIRE(f.host_side->sendMedia({32, 0, {2}}));
    f.send(RoomFileOffer{hashOf('b'), 1000, 32});
    REQUIRE(f.inbox.consumed == std::vector<RoomFileChunk>{{31, 0, {1}}, {32, 0, {2}}});
}

namespace {
std::size_t requestsFor(const GuestFixture& f, const char letter) {
    const auto hashes = requestedHashes(f);
    return static_cast<std::size_t>(std::count(hashes.begin(), hashes.end(), hashOf(letter)));
}
} // namespace

TEST_CASE("room guest: a failed transfer is asked for again at most room_file_retries times per window entry",
          "[core][listening-room][room-guest][room-files]") {
    static_assert(room_file_retries == 2);
    GuestFixture f;
    f.start();
    f.joinAdmitted();
    f.send(roomQueue(1, "a"));
    REQUIRE(requestsFor(f, 'a') == 1);
    // A failure that does not go away (the host's file does not match its hash, say).
    std::uint32_t id = 5;
    for (int round = 0; round < room_file_retries + 2; ++round) {
        f.send(RoomFileOffer{hashOf('a'), 1000, id});
        f.inbox.fail(hashOf('a'), id++);
    }
    REQUIRE(requestsFor(f, 'a') == 1 + static_cast<std::size_t>(room_file_retries));
    REQUIRE(f.inbox.accepted.size() == 1 + static_cast<std::size_t>(room_file_retries));

    // Out of the window and back: a new entry, new tries.
    auto without_a = roomQueue(2, "b");
    without_a.snapshot.queue[0].id = 12;
    without_a.snapshot.playback.current_item = 12;
    f.send(without_a);
    f.send(roomQueue(3, "a"));
    REQUIRE(requestsFor(f, 'a') == 2 + static_cast<std::size_t>(room_file_retries));
}

TEST_CASE("room guest: a gap asks again at once from the partial file without using a retry",
          "[core][listening-room][room-guest][room-files]") {
    GuestFixture f;
    f.start();
    f.joinAdmitted();
    f.send(roomQueue(1, "a"));
    std::uint32_t id = 5;
    for (int round = 0; round < room_file_retries + 1; ++round) {
        f.send(RoomFileOffer{hashOf('a'), 1000, id});
        f.inbox.partial[hashOf('a')] = 65536; // the receiver kept the partial file
        f.inbox.fail(hashOf('a'), id++, RoomFileFailure::Gap);
    }
    REQUIRE(requestsFor(f, 'a') == 2 + static_cast<std::size_t>(room_file_retries));
    REQUIRE(f.received<RoomFileRequest>().back() == RoomFileRequest{hashOf('a'), 65536});
    // The retries for real failures are all still there.
    for (int round = 0; round < room_file_retries; ++round) {
        f.send(RoomFileOffer{hashOf('a'), 1000, id});
        f.inbox.fail(hashOf('a'), id++);
    }
    REQUIRE(requestsFor(f, 'a') == 2 + 2 * static_cast<std::size_t>(room_file_retries));
}

TEST_CASE("room guest: a late failure of a replaced transfer leaves the live one alone",
          "[core][listening-room][room-guest][room-files]") {
    GuestFixture f;
    f.start();
    f.joinAdmitted();
    f.send(roomQueue(1, "a"));
    f.send(roomQueue(2, "a")); // same window: nothing asked again
    f.send(RoomFileOffer{hashOf('a'), 1000, 5});
    f.send(RoomFileOffer{hashOf('a'), 1000, 6}); // replaces 5
    f.inbox.fail(hashOf('a'), 5);                // the answer for 5 comes late
    REQUIRE(requestsFor(f, 'a') == 1);
    REQUIRE(f.host_side->sendMedia({6, 0, {1}}));
    REQUIRE(f.inbox.consumed == std::vector<RoomFileChunk>{{6, 0, {1}}});
    f.inbox.complete(hashOf('a'));
    REQUIRE(f.guest->localFile(hashOf('a')));
}
namespace {

// A silent player: loads are ready at once and the position moves only through seeks.
class RecordingBackend final : public PlaybackBackend {
  public:
    [[nodiscard]] const PlaybackSnapshot& snapshot() const noexcept override { return snapshot_; }
    void setEventCallback(PlaybackEventCallback) override {}
    PlaybackCommandResult load(const std::filesystem::path& source_path) override {
        loads.push_back(source_path);
        snapshot_ = {PlaybackStatus::Ready, 0, 240'000, true, snapshot_.generation + 1};
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult play() override {
        snapshot_.status = PlaybackStatus::Playing;
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult pause() override {
        snapshot_.status = PlaybackStatus::Paused;
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult seekTo(const std::int64_t position_ms) override {
        seeks.push_back(position_ms);
        snapshot_.position_ms = position_ms;
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult setVolume(double) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult setMuted(bool) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult unload() override {
        ++unloads;
        snapshot_ = {PlaybackStatus::Empty, 0, 0, false, snapshot_.generation + 1};
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult shutdown() override { return unload(); }

    std::vector<std::filesystem::path> loads;
    std::vector<std::int64_t> seeks;
    int unloads{0};

  private:
    PlaybackSnapshot snapshot_{};
};

struct FollowingGuest final {
    FollowingGuest() {
        f.follower = &follower;
        f.start();
    }
    // The host's clock reads local + 100 s; the pong comes back 10 ms after the ping.
    void answerPing() {
        const auto pings = f.received<RoomClockPing>();
        REQUIRE(!pings.empty());
        f.timers.advance(10);
        f.send(RoomClockPong{pings.back().sent_local_ms, pings.back().sent_local_ms + 5 + 100'000});
    }
    [[nodiscard]] std::int64_t hostNow() const { return f.timers.now + 100'000; }
    // Item 11 ("aaa...") plays from 1 s, started 2 s ago on the host.
    [[nodiscard]] RoomStateMessage playingQueue(const std::uint64_t version) const {
        auto message = roomQueue(version, "ab");
        message.snapshot.playback = RoomPlayback{11, true, hostNow() - 2'000, 1'000};
        return message;
    }

    GuestFixture f;
    RecordingBackend backend;
    RoomPlaybackFollower follower{backend, [this] { return f.timers.now; }, room_drift_nudge_ms, room_drift_seek_ms,
                                  [this](const std::int64_t delay_ms, std::function<void()> task) {
                                      f.timers.schedule(delay_ms, std::move(task));
                                  }};
};

} // namespace

TEST_CASE("room guest: pings the host's clock when admitted and every room_clock_resample_ms",
          "[core][listening-room][room-guest][room-follow]") {
    FollowingGuest g;
    g.f.timers.advance(1'000);
    g.f.joinAdmitted();
    REQUIRE(g.f.received<RoomClockPing>() == std::vector{RoomClockPing{1'000}});
    g.f.timers.advance(room_clock_resample_ms);
    REQUIRE(g.f.received<RoomClockPing>() == std::vector{RoomClockPing{1'000}, RoomClockPing{6'000}});

    // A dropped link stops them; the next admission starts again.
    g.f.dropLink(RoomLinkFailure::Unreachable);
    g.f.timers.advance(room_clock_resample_ms * 2);
    REQUIRE(g.f.received<RoomClockPing>().size() == 2);
}

TEST_CASE("room guest: reports the follower's latest drift with each clock ping, only when there is one",
          "[core][listening-room][room-guest][room-follow]") {
    FollowingGuest g;
    g.f.inbox.files[hashOf('a')] = "cache/a";
    g.f.joinAdmitted();
    // No evaluation yet: the first ping goes alone.
    REQUIRE(g.f.received<RoomClockPing>().size() == 1);
    REQUIRE(g.f.received<RoomDriftReport>().empty());
    g.answerPing();
    g.f.send(g.playingQueue(1));

    // The backend does not move, so the guest falls behind the host as time passes.
    g.f.timers.advance(1'000);
    g.follower.tick();
    const auto drift = g.follower.lastDriftMs();
    REQUIRE(drift.has_value());
    REQUIRE(*drift < 0);
    g.f.timers.advance(room_clock_resample_ms);
    REQUIRE(g.f.received<RoomClockPing>().size() == 2);
    REQUIRE(g.f.received<RoomDriftReport>() == std::vector{RoomDriftReport{*drift}});

    // Without a valid evaluation (held) the next ping carries no report.
    g.follower.setHeld(true);
    g.f.timers.advance(room_clock_resample_ms);
    REQUIRE(g.f.received<RoomClockPing>().size() == 3);
    REQUIRE(g.f.received<RoomDriftReport>().size() == 1);
}

TEST_CASE("room guest: a drift beyond the report limit is clamped to it",
          "[core][listening-room][room-guest][room-follow]") {
    FollowingGuest g;
    g.f.inbox.files[hashOf('a')] = "cache/a";
    g.f.joinAdmitted();
    g.answerPing();
    g.f.send(g.playingQueue(1));
    g.f.timers.advance(room_drift_report_limit_ms + 5'000);
    g.follower.tick();
    REQUIRE(*g.follower.lastDriftMs() < -room_drift_report_limit_ms);
    g.f.timers.advance(room_clock_resample_ms);
    REQUIRE(g.f.received<RoomDriftReport>().back() == RoomDriftReport{-room_drift_report_limit_ms});
}

TEST_CASE("room guest: without a follower it sends no clock pings", "[core][listening-room][room-guest][room-follow]") {
    GuestFixture f;
    f.start();
    f.joinAdmitted();
    f.timers.advance(room_clock_resample_ms * 2);
    REQUIRE(f.received<RoomClockPing>().empty());
}

TEST_CASE("room guest: the current item's local file follows the host's time",
          "[core][listening-room][room-guest][room-follow]") {
    FollowingGuest g;
    g.f.inbox.files[hashOf('a')] = "cache/a";
    g.f.joinAdmitted();
    g.answerPing();
    g.f.send(g.playingQueue(1));
    REQUIRE(g.backend.loads == std::vector<std::filesystem::path>{"cache/a"});
    REQUIRE(g.backend.seeks == std::vector<std::int64_t>{3'000});
    REQUIRE(g.backend.snapshot().status == PlaybackStatus::Playing);
}

TEST_CASE("room guest: a file that arrives late enters at the expected position",
          "[core][listening-room][room-guest][room-follow]") {
    FollowingGuest g;
    g.f.joinAdmitted();
    g.answerPing();
    g.f.send(g.playingQueue(1));
    REQUIRE(g.backend.loads.empty());
    g.f.send(RoomFileOffer{hashOf('a'), 1000, 4});
    g.f.timers.advance(4'000);
    g.f.inbox.complete(hashOf('a'));
    REQUIRE(g.backend.loads == std::vector<std::filesystem::path>{std::filesystem::path{"cache"} / hashOf('a')});
    REQUIRE(g.backend.seeks == std::vector<std::int64_t>{7'000});
    REQUIRE(g.backend.snapshot().status == PlaybackStatus::Playing);
}

TEST_CASE("room guest: leaving unloads the room file before the local session comes back",
          "[core][listening-room][room-guest][room-follow]") {
    FollowingGuest g;
    g.f.inbox.files[hashOf('a')] = "cache/a";
    g.f.joinAdmitted();
    g.answerPing();
    g.f.send(g.playingQueue(1));
    REQUIRE(g.backend.snapshot().status == PlaybackStatus::Playing);
    std::optional<PlaybackStatus> at_restore;
    g.f.on_restore = [&g, &at_restore] { at_restore = g.backend.snapshot().status; };
    g.f.guest->leave();
    REQUIRE(at_restore == PlaybackStatus::Empty);
    REQUIRE(g.backend.unloads == 1);

    // The next room starts without the old host's clock.
    g.f.joinAdmitted();
    g.f.send(g.playingQueue(2));
    REQUIRE(g.backend.snapshot().status != PlaybackStatus::Playing);
}

TEST_CASE("room guest: keeps the host's last catalog while in the room and forgets it when the room ends",
          "[core][listening-room][room-guest][room-catalog]") {
    GuestFixture f;
    f.start();
    const RoomCatalog catalog{
        {RoomCatalogPlaylist{5, "Festa", {RoomCatalogEntry{6, RoomTrackSummary{"d", "Artista", "Album", 1000}}}}}};
    f.guest->join(invite());
    f.connectLink();
    f.send(catalog); // before admission: ignored
    REQUIRE(f.guest->catalog().playlists.empty());
    f.send(RoomJoinResult{JoinDecision::Admitted});

    const auto changes = f.changes;
    f.send(catalog);
    REQUIRE(f.guest->catalog() == catalog);
    REQUIRE(f.changes == changes + 1);
    f.send(catalog); // the same catalog again changes nothing
    REQUIRE(f.changes == changes + 1);
    f.send(RoomCatalog{});
    REQUIRE(f.guest->catalog().playlists.empty());

    // A dropped link keeps it; the end of the room forgets it.
    f.send(catalog);
    f.dropLink(RoomLinkFailure::Unreachable);
    REQUIRE(f.guest->catalog() == catalog);
    f.guest->leave();
    REQUIRE(f.guest->catalog().playlists.empty());
}

// S12.1: "Adicionar música minha". The guest describes its own file, sends only hash and size, and streams
// the file when the host asks for it.
namespace {

RoomPermissionSet uploadPermission() {
    return RoomPermissionSet{}.set(static_cast<std::size_t>(RoomPermission::UploadOwnTracks));
}

std::vector<RoomIntentMessage> sentIntents(const GuestFixture& f) { return f.received<RoomIntentMessage>(); }

RoomStateMessage withGuestTrack(const std::uint64_t version, const std::string& sha256) {
    auto message = state(version, uploadPermission());
    RoomItem item;
    item.id = 50;
    item.origin = RoomItemOrigin::GuestTrack;
    item.owner_fingerprint = guest_hex;
    item.content_sha256 = sha256;
    item.size_bytes = 1000;
    item.added_by = "Ana";
    message.snapshot.queue.push_back(item);
    return message;
}

} // namespace

TEST_CASE("room guest: uploadTrack needs the room, the permission and the upload seams",
          "[core][listening-room][room-guest][room-uploads]") {
    GuestFixture f;
    f.start();
    REQUIRE_FALSE(f.guest->uploadTrack("music/minha.mp3"));
    f.joinAdmitted();
    f.send(state(1));
    REQUIRE_FALSE(f.guest->uploadTrack("music/minha.mp3"));
    REQUIRE(f.upload_catalog.requests.empty());
    REQUIRE(sentIntents(f).empty());
    REQUIRE(f.uploads_ended.empty());
    REQUIRE(f.guest->uploadsInProgress() == 0);
}

TEST_CASE("room guest: an upload sends hash and size only, serves the host's request and ends in the queue",
          "[core][listening-room][room-guest][room-uploads]") {
    GuestFixture f;
    f.start();
    f.joinAdmitted();
    f.send(state(1, uploadPermission()));
    const std::filesystem::path file{"C:/Músicas/Pessoais/minha.mp3"};
    const auto sha = testing::FakeRoomTrackCatalog::fakeSha256(file);

    REQUIRE(f.guest->uploadTrack(file));
    REQUIRE(f.upload_catalog.requests == std::vector<std::filesystem::path>{file});
    REQUIRE(f.guest->uploadsInProgress() == 1);
    const auto intents = sentIntents(f);
    REQUIRE(intents.size() == 1);
    RoomItem expected;
    expected.origin = RoomItemOrigin::GuestTrack;
    expected.content_sha256 = sha;
    expected.size_bytes = 1000;
    REQUIRE(intents.front().intent == RoomIntent{.kind = RoomIntentKind::AddItems, .uploaded = expected});

    // The host asks for the file: it goes from the guest's own path, under a transfer id of the guest's.
    f.send(RoomFileRequest{sha, 0});
    REQUIRE(f.upload_outbox.sent.size() == 1);
    const auto first = f.upload_outbox.sent.front();
    REQUIRE(first.peer == f.guest_end->peerFingerprint());
    REQUIRE(first.file == file);
    REQUIRE(first.sha256 == sha);
    REQUIRE(first.offset == 0);
    // Asked again (a resume): the earlier transfer stops and a new one starts from the offset.
    f.send(RoomFileRequest{sha, 65536});
    REQUIRE(f.upload_outbox.cancelled ==
            std::vector<std::pair<std::string, std::uint32_t>>{{f.guest_end->peerFingerprint(), first.transfer_id}});
    REQUIRE(f.upload_outbox.sent.size() == 2);
    REQUIRE(f.upload_outbox.sent.back().transfer_id != first.transfer_id);
    REQUIRE(f.upload_outbox.sent.back().offset == 65536);
    // Nothing else of the guest's is ever sent.
    f.send(RoomFileRequest{std::string(64, 'c'), 0});
    REQUIRE(f.upload_outbox.sent.size() == 2);

    // The host checked it and queued it.
    f.send(withGuestTrack(2, sha));
    REQUIRE(f.uploads_ended == std::vector<GuestUploadResult>{GuestUploadResult::Added});
    REQUIRE(f.guest->uploadsInProgress() == 0);
    // After that the file is no longer served from the guest's path.
    f.send(RoomFileRequest{sha, 0});
    REQUIRE(f.upload_outbox.sent.size() == 2);
}

TEST_CASE("room guest: a refused upload ends with its reason and never as a plain rejection",
          "[core][listening-room][room-guest][room-uploads]") {
    GuestFixture f;
    f.start();
    f.joinAdmitted();
    f.send(state(1, uploadPermission()));
    REQUIRE(f.guest->uploadTrack("music/texto.mp3"));
    const auto sha = testing::FakeRoomTrackCatalog::fakeSha256("music/texto.mp3");
    f.send(RoomFileRequest{sha, 0});
    const auto transfer = f.upload_outbox.sent.back().transfer_id;
    f.send(RoomIntentRejected{sentIntents(f).back().request_id, IntentRejection::InvalidArgument});
    REQUIRE(f.uploads_ended == std::vector<GuestUploadResult>{GuestUploadResult::InvalidAudio});
    REQUIRE(f.upload_outbox.cancelled ==
            std::vector<std::pair<std::string, std::uint32_t>>{{f.guest_end->peerFingerprint(), transfer}});

    REQUIRE(f.guest->uploadTrack("music/b.mp3"));
    f.send(RoomIntentRejected{sentIntents(f).back().request_id, IntentRejection::MissingPermission});
    REQUIRE(f.guest->uploadTrack("music/c.mp3"));
    f.send(RoomIntentRejected{sentIntents(f).back().request_id, IntentRejection::QueueFull});
    REQUIRE(f.uploads_ended == std::vector<GuestUploadResult>{GuestUploadResult::InvalidAudio,
                                                              GuestUploadResult::MissingPermission,
                                                              GuestUploadResult::QueueFull});
    REQUIRE(f.rejected.empty());
    REQUIRE(f.guest->uploadsInProgress() == 0);

    // Other intents are still rejected the usual way.
    const auto next = f.guest->sendIntent(RoomIntent{.kind = RoomIntentKind::Next});
    f.send(RoomIntentRejected{next, IntentRejection::MissingPermission});
    REQUIRE(f.rejected ==
            std::vector<std::pair<std::uint64_t, IntentRejection>>{{next, IntentRejection::MissingPermission}});
}

// S12.1 fix round 1: a file the guest can no longer read (moved, deleted, its drive removed) ends the upload
// at once instead of leaving it "in progress" until the guest leaves.
TEST_CASE("room guest: an upload whose file cannot be sent ends as failed",
          "[core][listening-room][room-guest][room-uploads]") {
    GuestFixture f;
    f.start();
    f.joinAdmitted();
    f.send(state(1, uploadPermission()));
    REQUIRE(f.guest->uploadTrack("music/sumiu.mp3"));
    const auto sha = testing::FakeRoomTrackCatalog::fakeSha256("music/sumiu.mp3");
    REQUIRE(f.guest->uploadsInProgress() == 1);
    f.upload_outbox.accepts = false;
    f.send(RoomFileRequest{sha, 0});
    REQUIRE(f.uploads_ended == std::vector<GuestUploadResult>{GuestUploadResult::Failed});
    REQUIRE(f.guest->uploadsInProgress() == 0);
    REQUIRE(f.upload_outbox.cancelled.empty());
    // The host's later answer for it changes nothing.
    f.send(RoomIntentRejected{sentIntents(f).back().request_id, IntentRejection::UnknownItem});
    REQUIRE(f.uploads_ended.size() == 1);
    REQUIRE(f.rejected.empty());
}

// S12.1 fix round 1: the host drops an upload that stalled or that the guest sent again (UnknownItem): that is
// a failed send, not invalid audio.
TEST_CASE("room guest: an upload the host dropped ends as failed, not as invalid audio",
          "[core][listening-room][room-guest][room-uploads]") {
    GuestFixture f;
    f.start();
    f.joinAdmitted();
    f.send(state(1, uploadPermission()));
    REQUIRE(f.guest->uploadTrack("music/lenta.mp3"));
    f.send(RoomIntentRejected{sentIntents(f).back().request_id, IntentRejection::UnknownItem});
    REQUIRE(f.uploads_ended == std::vector<GuestUploadResult>{GuestUploadResult::Failed});
    REQUIRE(f.rejected.empty());
    REQUIRE(f.guest->uploadsInProgress() == 0);
}

TEST_CASE("room guest: files a local import refuses, or above 50 MiB, never leave the guest",
          "[core][listening-room][room-guest][room-uploads]") {
    GuestFixture f;
    f.start();
    f.joinAdmitted();
    f.send(state(1, uploadPermission()));
    REQUIRE(f.guest->uploadTrack("notas/letra.txt"));
    REQUIRE(f.upload_catalog.requests.empty());
    f.upload_catalog.unreadable.insert("music/quebrada.mp3");
    REQUIRE(f.guest->uploadTrack("music/quebrada.mp3"));
    f.upload_catalog.size_override = maximum_guest_upload_bytes + 1;
    REQUIRE(f.guest->uploadTrack("music/enorme.flac"));
    REQUIRE(f.uploads_ended == std::vector<GuestUploadResult>{GuestUploadResult::InvalidAudio,
                                                              GuestUploadResult::InvalidAudio,
                                                              GuestUploadResult::TooLarge});
    REQUIRE(sentIntents(f).empty());
    REQUIRE(f.guest->uploadsInProgress() == 0);

    f.upload_catalog.size_override = maximum_guest_upload_bytes;
    REQUIRE(f.guest->uploadTrack("music/cabe.flac"));
    REQUIRE(sentIntents(f).size() == 1);
    REQUIRE(sentIntents(f).front().intent.uploaded->size_bytes == maximum_guest_upload_bytes);
}

TEST_CASE("room guest: a dropped link ends the uploads in progress; leaving drops them silently",
          "[core][listening-room][room-guest][room-uploads]") {
    GuestFixture f;
    f.start();
    f.joinAdmitted();
    f.send(state(1, uploadPermission()));
    REQUIRE(f.guest->uploadTrack("music/minha.mp3"));
    f.send(RoomFileRequest{testing::FakeRoomTrackCatalog::fakeSha256("music/minha.mp3"), 0});
    f.dropLink(RoomLinkFailure::Unreachable);
    REQUIRE(f.uploads_ended == std::vector<GuestUploadResult>{GuestUploadResult::Failed});
    REQUIRE(f.guest->uploadsInProgress() == 0);
    REQUIRE(f.upload_outbox.forgotten == std::vector<std::string>{f.guest_end->peerFingerprint()});

    // Back in the room, an upload still being described when the guest leaves ends without a word.
    f.connectLink();
    f.send(RoomJoinResult{JoinDecision::Admitted});
    f.send(state(2, uploadPermission()));
    f.upload_catalog.deferred = true;
    REQUIRE(f.guest->uploadTrack("music/outra.mp3"));
    REQUIRE(f.guest->uploadsInProgress() == 1);
    const auto intents = sentIntents(f).size();
    f.guest->leave();
    f.upload_catalog.completeAll();
    REQUIRE(f.uploads_ended.size() == 1);
    REQUIRE(sentIntents(f).size() == intents);
    REQUIRE(f.guest->uploadsInProgress() == 0);
}

} // namespace music_player::m0::test
