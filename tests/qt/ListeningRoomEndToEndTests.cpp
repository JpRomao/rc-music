// ListeningRoomHost and ListeningRoomGuest end to end (S7.4) over the real transport
// (DataChannelRoomTransport) and the in-process relay (tests/support/LoopbackSignaling.hpp): 127.0.0.1,
// host ICE candidates only, real timers (ListeningRoomBinding::scheduler). The host's player is the
// silent deterministic one (FakeRoomHost.hpp) with a fixed host clock; the guests' local queues are plain
// SessionSnapshots. No internet, no real Worker, no audio and no personal files.
#include "../support/FakeRoomHost.hpp"
#include "../support/LoopbackSignaling.hpp"
#include "adapters/DataChannelRoomTransport.hpp"
#include "adapters/WindowsRoomIdentity.hpp"
#include "m0/application/ListeningRoomGuest.hpp"
#include "m0/application/ListeningRoomHost.hpp"
#include "m0/domain/ListeningRoomLimits.hpp"
#include "m0/domain/RoomInviteLink.hpp"
#include "presentation/ListeningRoomBinding.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace music_player::m0;
using music_player::m0::testing::LoopbackSignaling;

namespace {
constexpr int connect_ms = 20000;
const std::vector<std::string> local_only{}; // host candidates only: no STUN, no internet
constexpr std::uint64_t dj_role = 2;         // defaultRoomRoster(): 1 "Ouvinte", 2 "DJ"

RoomIdentity identityIn(const QString& directory) {
    auto identity = loadOrCreateRoomIdentity(directory);
    if (!identity)
        qFatal("cannot create a test identity");
    return *identity;
}

std::string hex(const std::array<std::uint8_t, 32>& bytes) {
    return QByteArray(reinterpret_cast<const char*>(bytes.data()), 32).toHex().toStdString();
}

SessionSnapshot localQueue() {
    SessionSnapshot session;
    session.queue.items = {{LocalQueueOccurrenceId{1}, "local/um.mp3"}, {LocalQueueOccurrenceId{2}, "local/dois.mp3"}};
    session.queue.current_occurrence_id = LocalQueueOccurrenceId{2};
    session.position_ms = 42'000;
    return session;
}

std::optional<GuestRoomEnd> ending(const GuestEndReason reason, const RoomLinkFailure failure) {
    return GuestRoomEnd{reason, failure};
}

bool hasMember(const RoomSnapshot& snapshot, const std::string& fingerprint) {
    return std::any_of(snapshot.members.begin(), snapshot.members.end(),
                       [&](const RoomMember& m) { return m.fingerprint == fingerprint; });
}

// A guest link the test can cut as the network would: the host's side sees the link close, and the
// guest sees onClosed(Unreachable). Everything else goes straight to the real link.
class SeverableLink final : public RoomLink, public std::enable_shared_from_this<SeverableLink> {
  public:
    explicit SeverableLink(std::shared_ptr<RoomLink> inner)
        : inner_(std::move(inner)), fingerprint_(inner_->peerFingerprint()) {}
    void wire() {
        const std::weak_ptr<SeverableLink> weak = weak_from_this();
        inner_->onControl = [weak](RoomControlMessage message) {
            if (const auto self = weak.lock(); self && self->onControl)
                self->onControl(std::move(message));
        };
        inner_->onMedia = [weak](RoomFileChunk chunk) {
            if (const auto self = weak.lock(); self && self->onMedia)
                self->onMedia(std::move(chunk));
        };
        inner_->onClosed = [weak](const RoomLinkFailure failure) {
            if (const auto self = weak.lock()) {
                self->release();
                if (self->onClosed)
                    self->onClosed(failure);
            }
        };
    }
    void sever() {
        if (!inner_)
            return;
        release();
        if (const auto callback = onClosed)
            callback(RoomLinkFailure::Unreachable);
    }

    [[nodiscard]] std::string peerFingerprint() const override { return fingerprint_; }
    bool sendControl(const RoomControlMessage& message) override { return inner_ && inner_->sendControl(message); }
    bool sendMedia(const RoomFileChunk& chunk) override { return inner_ && inner_->sendMedia(chunk); }
    [[nodiscard]] std::size_t mediaBufferedBytes() const override { return inner_ ? inner_->mediaBufferedBytes() : 0; }
    void close() override { release(); }

  private:
    void release() {
        if (const auto inner = std::exchange(inner_, nullptr)) {
            inner->onControl = nullptr;
            inner->onMedia = nullptr;
            inner->onClosed = nullptr;
            inner->close();
        }
    }
    std::shared_ptr<RoomLink> inner_;
    std::string fingerprint_;
};

class SeverableConnector final : public RoomGuestConnector {
  public:
    explicit SeverableConnector(RoomGuestConnector& inner) : inner_(inner) {}
    void connect(const RoomInviteLink& invite, std::function<void(std::shared_ptr<RoomLink>)> on_connected,
                 std::function<void(RoomLinkFailure)> on_failure) override {
        inner_.connect(
            invite,
            [this, on_connected = std::move(on_connected)](std::shared_ptr<RoomLink> link) {
                auto wrapped = std::make_shared<SeverableLink>(std::move(link));
                wrapped->wire();
                last = wrapped;
                on_connected(std::move(wrapped));
            },
            std::move(on_failure));
    }
    void cancel() override { inner_.cancel(); }

    std::weak_ptr<SeverableLink> last;

  private:
    RoomGuestConnector& inner_;
};

// The host app: ListeningRoomHost over the real endpoint, the silent player and a fixed host clock.
struct HostApp final {
    HostApp(const LoopbackSignaling& relay, const RoomIdentity& identity, testing::InMemoryRoomRosterStore roster_store)
        : roster(std::move(roster_store)), endpoint(relay.origin(), identity, local_only),
          host(ListeningRoomHostDependencies{
              .roster_store = roster,
              .player = player,
              .endpoint = endpoint,
              .catalog = catalog,
              .host_clock_ms = [this] { return now; },
              .random_bytes =
                  [](std::span<std::uint8_t> bytes) {
                      QRandomGenerator::system()->generate(bytes.begin(), bytes.end());
                  },
              .host_fingerprint = identity.fingerprint,
              .host_profile = ListenerProfile{"Joao", AvatarChoice{}},
              .schedule = ListeningRoomBinding::scheduler(&context),
          }) {
        host.onJoinRequest = [this](const RoomMember& member) { join_requests.push_back(member); };
    }

    QObject context;
    testing::FakeRoomHostPlayer player{{"music/a.mp3", "music/b.mp3", "music/c.mp3"}};
    testing::FakeRoomTrackCatalog catalog;
    testing::InMemoryRoomRosterStore roster;
    DataChannelRoomHostEndpoint endpoint;
    std::int64_t now{100'000};
    std::vector<RoomMember> join_requests;
    ListeningRoomHost host; // last: destroyed first
};

// A guest app: ListeningRoomGuest over the real connector (behind SeverableConnector).
struct GuestApp final {
    GuestApp(const LoopbackSignaling& relay, const RoomIdentity& identity, std::string nickname)
        : fingerprint(hex(identity.fingerprint)), real(relay.origin(), identity, local_only), connector(real),
          guest(ListeningRoomGuestDependencies{
              .connector = connector,
              .profile = ListenerProfile{std::move(nickname), AvatarChoice{AvatarKind::Bundled, 2, {}}},
              .stored_custom_avatar_sha256 = [] { return std::optional<std::string>{}; },
              .capture_session = [this] { return local; },
              .restore_session =
                  [this](const SessionSnapshot& session) {
                      restored.push_back(session);
                      local = session;
                  },
              .schedule = ListeningRoomBinding::scheduler(&context),
          }) {
        guest.onEnded = [this](const RoomLinkFailure failure) { ended.push_back(failure); };
    }

    std::string fingerprint;
    QObject context;
    DataChannelRoomGuestConnector real;
    SeverableConnector connector;
    SessionSnapshot local{localQueue()};
    std::vector<SessionSnapshot> restored;
    std::vector<RoomLinkFailure> ended;
    ListeningRoomGuest guest; // last: destroyed first
};

testing::InMemoryRoomRosterStore rosterWith(std::vector<KnownListener> listeners = {}) {
    testing::InMemoryRoomRosterStore store;
    auto roster = defaultRoomRoster();
    roster.listeners = std::move(listeners);
    store.stored = roster;
    store.status = RoomRosterLoadStatus::Loaded;
    return store;
}
} // namespace

class ListeningRoomEndToEndTests final : public QObject {
    Q_OBJECT

    QTemporaryDir host_dir_;
    QTemporaryDir ana_dir_;
    QTemporaryDir bia_dir_;
    RoomIdentity host_id_;
    RoomIdentity ana_id_;
    RoomIdentity bia_id_;

    // Opens the room and waits until the relay has the host, so a guest joining next finds it.
    static std::optional<RoomInviteLink> openRoom(HostApp& app, const LoopbackSignaling& relay,
                                                  const RoomOpenOptions& options) {
        const auto before = relay.host_tokens.size();
        auto invite = app.host.open(options);
        if (!invite || !QTest::qWaitFor([&] { return relay.host_tokens.size() > before; }, connect_ms))
            return std::nullopt;
        return invite;
    }

    // An unknown guest asks to enter and the host approves: the guest is Joined with the host's state.
    static bool approveIn(HostApp& app, GuestApp& visitor, const RoomInviteLink& invite) {
        visitor.guest.join(invite);
        if (!QTest::qWaitFor([&] { return visitor.guest.state() == GuestRoomState::WaitingApproval; }, connect_ms))
            return false;
        if (!app.host.approve(visitor.fingerprint))
            return false;
        return QTest::qWaitFor(
            [&] {
                return visitor.guest.state() == GuestRoomState::Joined &&
                       visitor.guest.snapshot().version == app.host.snapshot().version;
            },
            connect_ms);
    }

  private slots:
    void initTestCase() {
        QVERIFY(host_dir_.isValid() && ana_dir_.isValid() && bia_dir_.isValid());
        host_id_ = identityIn(host_dir_.path());
        ana_id_ = identityIn(ana_dir_.path());
        bia_id_ = identityIn(bia_dir_.path());
    }

    void anUnknownGuestIsApprovedFollowsTheStateAndSkipsAsDj() {
        LoopbackSignaling relay;
        HostApp app(relay, host_id_, rosterWith());
        const auto invite = openRoom(app, relay, {.default_role_id = dj_role});
        QVERIFY(invite.has_value());
        GuestApp ana(relay, ana_id_, "Ana");

        ana.guest.join(*invite);
        QTRY_COMPARE_WITH_TIMEOUT(ana.guest.state(), GuestRoomState::WaitingApproval, connect_ms);
        QCOMPARE(app.join_requests.size(), std::size_t{1});
        QCOMPARE(app.join_requests.front().fingerprint, ana.fingerprint);
        QCOMPARE(app.join_requests.front().nickname, std::string{"Ana"});
        QCOMPARE(ana.guest.snapshot(), RoomSnapshot{}); // waiting people get no state

        QVERIFY(app.host.approve(ana.fingerprint));
        QTRY_COMPARE_WITH_TIMEOUT(ana.guest.state(), GuestRoomState::Joined, connect_ms);
        QTRY_COMPARE_WITH_TIMEOUT(ana.guest.snapshot().version, app.host.snapshot().version, connect_ms);
        const auto joined = ana.guest.snapshot();
        QCOMPARE(joined, app.host.snapshot());
        QCOMPARE(joined.members.size(), std::size_t{2});
        QCOMPARE(joined.members.front().fingerprint, hex(host_id_.fingerprint));
        QVERIFY(hasMember(joined, ana.fingerprint));
        QCOMPARE(joined.queue.size(), std::size_t{3});
        QCOMPARE(joined.playback.current_item, std::optional{app.player.occurrenceAt(0)});
        QVERIFY(joined.playback.playing);
        QVERIFY(ana.guest.permissions().test(static_cast<std::size_t>(RoomPermission::Skip)));
        QVERIFY(ana.guest.holdsLocalSession());

        QVERIFY(ana.guest.sendIntent(RoomIntent{.kind = RoomIntentKind::Next}) != 0);
        QTRY_COMPARE_WITH_TIMEOUT(app.player.currentIndex(), std::optional<std::size_t>{1}, connect_ms);
        QTRY_COMPARE_WITH_TIMEOUT(ana.guest.snapshot().version, app.host.snapshot().version, connect_ms);
        QVERIFY(ana.guest.snapshot().version > joined.version);
        QCOMPARE(ana.guest.snapshot().playback.current_item, std::optional{app.player.occurrenceAt(1)});
        QCOMPARE(ana.guest.snapshot().queue.size(), std::size_t{2});
    }

    void aKnownListenerArrivingAtAFullRoomIsToldTheRoomIsFull() {
        LoopbackSignaling relay;
        const auto bia_hex = hex(bia_id_.fingerprint);
        HostApp app(relay, host_id_, rosterWith({KnownListener{bia_hex, "Bia", 1, {}, false}}));
        const auto invite = openRoom(app, relay, {.max_listeners = 1});
        QVERIFY(invite.has_value());
        GuestApp ana(relay, ana_id_, "Ana");
        QVERIFY(approveIn(app, ana, *invite));

        GuestApp bia(relay, bia_id_, "Bia");
        bia.guest.join(*invite);
        QTRY_COMPARE_WITH_TIMEOUT(bia.guest.state(), GuestRoomState::Ended, connect_ms);
        QCOMPARE(bia.guest.endReason(), ending(GuestEndReason::RoomFull, RoomLinkFailure::Refused));
        QCOMPARE(bia.ended, std::vector{RoomLinkFailure::Refused});
        QVERIFY(!hasMember(app.host.snapshot(), bia_hex));
        QCOMPARE(ana.guest.state(), GuestRoomState::Joined);

        // The page's text for it comes from ListeningRoomBinding::guestEnded (RoomFull).
        testing::FakeRoomHostPlayer other_player({"local/um.mp3"});
        testing::FakeRoomTrackCatalog other_catalog;
        testing::InMemoryRoomRosterStore other_roster;
        ListeningRoomBinding page(ListeningRoomBindingDependencies{
            .signaling_origin = relay.origin(),
            .load_network =
                [&](std::function<void(std::optional<ListeningRoomNetwork>)> done) {
                    done(ListeningRoomNetwork{
                        bia_id_.fingerprint,
                        std::make_unique<DataChannelRoomHostEndpoint>(relay.origin(), bia_id_, local_only),
                        std::make_unique<DataChannelRoomGuestConnector>(relay.origin(), bia_id_, local_only)});
                },
            .host_player = &other_player,
            .catalog = &other_catalog,
            .roster_store = &other_roster,
            .profile = [] { return std::optional{ListenerProfile{"Bia", AvatarChoice{}}}; },
            .custom_avatar_sha256 = [] { return std::optional<std::string>{}; },
            .capture_session = [&] { return other_player.sessionSnapshot(); },
            .restore_session = [](const SessionSnapshot&) {},
            .set_room_play_order = [](bool) {},
            .session_changed = [] {},
            .clock_ms = [] { return std::int64_t{50'000}; },
            .random_bytes =
                [](std::span<std::uint8_t> bytes) { QRandomGenerator::system()->generate(bytes.begin(), bytes.end()); },
            .privacy_notice_shown = [] { return true; },
            .mark_privacy_notice_shown = [] {},
            .app_active = [] { return true; },
            .notify = [](const std::string&, const std::string&) {},
            .copy_text = [](const QString&) {},
        });
        QVERIFY(page.join(QString::fromStdString(formatInviteLink(*invite, relay.origin().toStdString()))));
        QTRY_VERIFY_WITH_TIMEOUT(!page.errorText().isEmpty(), connect_ms);
        QCOMPARE(page.errorText(), QStringLiteral("A sala está cheia"));
        QCOMPARE(page.role(), int{ListeningRoomBinding::NoRole});
    }

    void revokeEndsTheRoomTheOldLinkFindsNoHostAndANewLinkWorks() {
        LoopbackSignaling relay;
        HostApp app(relay, host_id_, rosterWith());
        const auto old_invite = openRoom(app, relay, {});
        QVERIFY(old_invite.has_value());
        GuestApp ana(relay, ana_id_, "Ana");
        QVERIFY(approveIn(app, ana, *old_invite));
        ana.local = SessionSnapshot{}; // the room drives the guest's player now

        app.host.revoke();
        QTRY_COMPARE_WITH_TIMEOUT(ana.guest.state(), GuestRoomState::Ended, connect_ms);
        QCOMPARE(ana.guest.endReason(), ending(GuestEndReason::Revoked, RoomLinkFailure::Closed));
        QCOMPARE(ana.local, localQueue());

        ana.guest.join(*old_invite);
        QTRY_COMPARE_WITH_TIMEOUT(ana.guest.state(), GuestRoomState::Ended, connect_ms);
        QCOMPARE(ana.guest.endReason(), ending(GuestEndReason::LinkLost, RoomLinkFailure::HostOffline));

        const auto new_invite = openRoom(app, relay, {});
        QVERIFY(new_invite.has_value());
        QVERIFY(new_invite->room_id != old_invite->room_id);
        ana.guest.join(*new_invite);
        // Approved in the revoked room, so remembered: admitted at once this time.
        QTRY_COMPARE_WITH_TIMEOUT(ana.guest.state(), GuestRoomState::Joined, connect_ms);
        QTRY_COMPARE_WITH_TIMEOUT(ana.guest.snapshot().version, app.host.snapshot().version, connect_ms);
        QCOMPARE(app.join_requests.size(), std::size_t{1});
    }

    // Review Focus 5: the host app goes away without a Bye.
    void aHostDestroyedWithoutByeEndsTheRoomWithinTenSeconds() {
        LoopbackSignaling relay;
        auto app = std::make_unique<HostApp>(relay, host_id_, rosterWith());
        const auto invite = openRoom(*app, relay, {});
        QVERIFY(invite.has_value());
        GuestApp ana(relay, ana_id_, "Ana");
        QVERIFY(approveIn(*app, ana, *invite));
        ana.local = SessionSnapshot{};

        QElapsedTimer elapsed;
        elapsed.start();
        app.reset();
        QTRY_COMPARE_WITH_TIMEOUT(ana.guest.state(), GuestRoomState::Ended, 10000);
        QVERIFY2(elapsed.elapsed() < 10000, qPrintable(QString::number(elapsed.elapsed())));
        QCOMPARE(ana.guest.endReason(), ending(GuestEndReason::Closed, RoomLinkFailure::HostOffline));
        QCOMPARE(ana.local, localQueue());
        QCOMPARE(ana.restored, std::vector{localQueue()});
        QCOMPARE(ana.guest.snapshot(), RoomSnapshot{});
    }

    // Review Focus 1: the guest's link drops in the middle of a song while the host stays.
    void aGuestWhoseLinkDropsDuringASongIsJoinedAgainWithTheSameVersion() {
        LoopbackSignaling relay;
        HostApp app(relay, host_id_, rosterWith());
        const auto invite = openRoom(app, relay, {.default_role_id = dj_role});
        QVERIFY(invite.has_value());
        GuestApp ana(relay, ana_id_, "Ana");
        QVERIFY(approveIn(app, ana, *invite));
        QVERIFY(app.host.snapshot().playback.playing);
        const auto version = app.host.snapshot().version;

        const auto link = ana.connector.last.lock();
        QVERIFY(link);
        link->sever();
        QCOMPARE(ana.guest.state(), GuestRoomState::Reconnecting);
        QTRY_COMPARE_WITH_TIMEOUT(ana.guest.state(), GuestRoomState::Joined, connect_ms);
        QTRY_VERIFY_WITH_TIMEOUT(ana.connector.last.lock() != link, connect_ms);
        QCOMPARE(ana.guest.snapshot().version, version);
        QCOMPARE(app.host.snapshot().version, version);
        QVERIFY(hasMember(app.host.snapshot(), ana.fingerprint));
        QVERIFY(ana.ended.empty());
        QVERIFY(ana.restored.empty());
        QCOMPARE(app.join_requests.size(), std::size_t{1});

        // The new link carries the room: the DJ skips again.
        QVERIFY(ana.guest.sendIntent(RoomIntent{.kind = RoomIntentKind::Next}) != 0);
        QTRY_COMPARE_WITH_TIMEOUT(app.player.currentIndex(), std::optional<std::size_t>{1}, connect_ms);
        QTRY_COMPARE_WITH_TIMEOUT(ana.guest.snapshot().version, app.host.snapshot().version, connect_ms);
        QVERIFY(ana.guest.snapshot().version > version);
    }

    // Leaving says Bye: the host frees the seat at once instead of holding it for the reconnect window.
    void aGuestWhoLeavesIsGoneFromTheRoomAtOnce() {
        LoopbackSignaling relay;
        HostApp app(relay, host_id_, rosterWith());
        const auto invite = openRoom(app, relay, {});
        QVERIFY(invite.has_value());
        GuestApp ana(relay, ana_id_, "Ana");
        GuestApp bia(relay, bia_id_, "Bia");
        QVERIFY(approveIn(app, ana, *invite));
        QVERIFY(approveIn(app, bia, *invite));
        QTRY_COMPARE_WITH_TIMEOUT(ana.guest.snapshot().version, app.host.snapshot().version, connect_ms);
        const auto version = app.host.snapshot().version;

        bia.guest.leave();
        QCOMPARE(bia.guest.state(), GuestRoomState::Idle);
        QTRY_VERIFY_WITH_TIMEOUT(!hasMember(app.host.snapshot(), bia.fingerprint), 5000);
        QCOMPARE(app.host.snapshot().version, version + 1);
        QTRY_COMPARE_WITH_TIMEOUT(ana.guest.snapshot().version, version + 1, 5000);
        QVERIFY(!hasMember(ana.guest.snapshot(), bia.fingerprint));
        QCOMPARE(ana.guest.snapshot().members.size(), std::size_t{2});
    }
};

QTEST_GUILESS_MAIN(ListeningRoomEndToEndTests)
#include "ListeningRoomEndToEndTests.moc"
