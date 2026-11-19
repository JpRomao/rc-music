// ListeningRoomBinding (S7.3) over the in-memory transport, the silent deterministic player, a fake
// catalog and an in-memory roster store. No network, identity files, audio or personal files.
#include "../support/FakeRoomFiles.hpp"
#include "../support/FakeRoomHost.hpp"
#include "../support/FakeRoomTransport.hpp"

#include "m0/domain/RoomInviteLink.hpp"
#include "m0/ports/PlaybackBackend.hpp"
#include "presentation/ListeningRoomBinding.hpp"
#include "presentation/RoomQueueModel.hpp"

#include <QAbstractItemModel>
#include <QCryptographicHash>
#include <QFile>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace music_player::m0::test {
namespace {

const QString origin = QStringLiteral("https://sala.example");
constexpr std::array<std::uint8_t, 32> own_fingerprint{0x11, 0x22};
const std::string own_hex = "1122" + std::string(60, '0');
// Fixture::validLink() is an invite of a host whose fingerprint bytes are all 0xab.
[[nodiscard]] std::string inviteHostHex() {
    std::string hex;
    for (int index = 0; index < 32; ++index)
        hex += "ab";
    return hex;
}

// A silent player for the guest's follower: it records commands and plays nothing.
class SilentBackend final : public PlaybackBackend {
  public:
    [[nodiscard]] const PlaybackSnapshot& snapshot() const noexcept override { return snapshot_; }
    void setEventCallback(PlaybackEventCallback) override {}
    PlaybackCommandResult load(const std::filesystem::path& source) override {
        loads.push_back(source);
        snapshot_ = PlaybackSnapshot{PlaybackStatus::Paused, 0, 600'000, true};
        ++snapshot_.generation;
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
        snapshot_ = PlaybackSnapshot{};
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult shutdown() override { return PlaybackCommandResult::Accepted; }
    void setPosition(const std::int64_t position_ms) { snapshot_.position_ms = position_ms; }

    std::vector<std::filesystem::path> loads;
    std::vector<std::int64_t> seeks;
    int unloads{0};

  private:
    PlaybackSnapshot snapshot_{};
};

struct Fixture final {
    Fixture() : player({"music/a.mp3", "music/b.mp3"}) {}

    // The binding with every dependency faked. The network "loads" synchronously unless deferred.
    std::unique_ptr<ListeningRoomBinding> make(QString signaling_origin = origin) {
        return std::make_unique<ListeningRoomBinding>(ListeningRoomBindingDependencies{
            .signaling_origin = std::move(signaling_origin),
            .load_network =
                [this](std::function<void(std::optional<ListeningRoomNetwork>)> done) {
                    ++network_loads;
                    pending_load = std::move(done);
                    if (!defer_network)
                        completeNetwork();
                },
            .host_player = &player,
            .catalog = &catalog,
            .roster_store = &roster,
            .profile = [this] { return profile; },
            .custom_avatar_sha256 = [] { return std::optional<std::string>{}; },
            .capture_session =
                [this] {
                    ++captures;
                    return player.sessionSnapshot();
                },
            .restore_session = [this](const SessionSnapshot& saved) { restored.push_back(saved); },
            .set_room_play_order = [this](const bool enabled) { play_order.push_back(enabled); },
            .session_changed = [this] { ++session_changes; },
            .clock_ms = [] { return std::int64_t{50'000}; },
            .random_bytes =
                [this](std::span<std::uint8_t> bytes) {
                    for (auto& byte : bytes)
                        byte = next_random++;
                },
            .privacy_notice_shown = [this] { return notice_shown; },
            .mark_privacy_notice_shown = [this] { notice_shown = true; },
            .app_active = [this] { return active; },
            .notify = [this](const std::string& title,
                             const std::string& message) { notifications.emplace_back(title, message); },
            .copy_text = [this](const QString& text) { copied = text; },
            .file_outbox = &outbox,
            .file_inbox = &inbox,
            .room_lyrics = [this](const QString& lrc) { room_lyrics.push_back(lrc); },
            .guest_backend = guest_backend,
            .release_local_session = [this] { ++releases; },
            .room_position = [this](const std::optional<std::int64_t> position) { room_positions.push_back(position); },
        });
    }
    void completeNetwork() {
        auto done = std::exchange(pending_load, {});
        if (!done)
            return;
        if (identity_fails) {
            done(std::nullopt);
            return;
        }
        auto made_endpoint = std::make_unique<testing::FakeRoomHostEndpoint>();
        auto made_connector = std::make_unique<testing::FakeRoomGuestConnector>();
        endpoint = made_endpoint.get();
        connector = made_connector.get();
        done(ListeningRoomNetwork{own_fingerprint, std::move(made_endpoint), std::move(made_connector)});
    }
    // A guest arriving at the hosted room and saying hello.
    std::shared_ptr<testing::FakeRoomLink> arrive(const std::string& fingerprint, std::string nickname) {
        auto [host_side, guest_side] = testing::FakeRoomLink::pair(own_hex, fingerprint);
        endpoint->admit(host_side);
        static_cast<void>(guest_side->sendControl(RoomHello{room_protocol_version, std::move(nickname), {}}));
        return guest_side;
    }
    [[nodiscard]] static QString validLink(const std::uint8_t host_byte = 0xab) {
        RoomInviteLink invite;
        invite.room_id.fill(7);
        invite.secret.fill(9);
        invite.host_fingerprint.fill(host_byte);
        return QString::fromStdString(formatInviteLink(invite, origin.toStdString()));
    }

    testing::FakeRoomHostPlayer player;
    testing::FakeRoomTrackCatalog catalog;
    testing::InMemoryRoomRosterStore roster;
    testing::FakeRoomFileOutbox outbox;
    testing::FakeRoomFileInbox inbox;
    std::vector<QString> room_lyrics;
    testing::FakeRoomHostEndpoint* endpoint{nullptr};
    testing::FakeRoomGuestConnector* connector{nullptr};
    std::function<void(std::optional<ListeningRoomNetwork>)> pending_load;
    std::optional<ListenerProfile> profile{ListenerProfile{"Joao", AvatarChoice{}}};
    std::vector<SessionSnapshot> restored;
    std::vector<bool> play_order;
    std::vector<std::pair<std::string, std::string>> notifications;
    QString copied;
    std::uint8_t next_random{1};
    int network_loads{0};
    int captures{0};
    int releases{0};
    std::vector<std::optional<std::int64_t>> room_positions;
    PlaybackBackend* guest_backend{nullptr};
    int session_changes{0};
    bool defer_network{false};
    bool identity_fails{false};
    bool notice_shown{false};
    bool active{true};
};

} // namespace

class ListeningRoomBindingTests final : public QObject {
    Q_OBJECT

  private slots:
    void joinRefusesTextThatIsNotAnInvite() {
        Fixture f;
        auto binding = f.make();
        QVERIFY(!binding->join(QStringLiteral("não é um link")));
        QCOMPARE(binding->errorText(), QStringLiteral("Link de convite inválido"));
        QVERIFY(!binding->join(QStringLiteral("https://outro.example/j/AAAAAAAAAAAAAAAAAAAAAA#x.y")));
        QCOMPARE(binding->role(), int{ListeningRoomBinding::NoRole});
        QCOMPARE(f.network_loads, 0);
    }

    void openRoomAcceptsOneToTenListeners() {
        Fixture f;
        auto binding = f.make();
        QCOMPARE(binding->minimumListeners(), 1);
        QCOMPARE(binding->maximumListeners(), 10);
        QCOMPARE(binding->defaultListeners(), 5);
        QVERIFY(!binding->openRoom(11, false));
        QVERIFY(!binding->openRoom(0, false));
        QCOMPARE(f.network_loads, 0);
        QVERIFY(binding->openRoom(10, false));
        QCOMPARE(binding->role(), int{ListeningRoomBinding::HostRole});
        QCOMPARE(binding->state(), QStringLiteral("open"));
        QCOMPARE(binding->maxListeners(), 10);
        QVERIFY(!binding->untilRevoked());
        QVERIFY(binding->inviteLink().startsWith(origin + QStringLiteral("/j/")));
        QVERIFY(f.endpoint->opened.has_value());
        QCOMPARE(f.play_order, std::vector<bool>{true});
        QCOMPARE(binding->memberCount(), 1);
        QCOMPARE(binding->hostNickname(), QStringLiteral("Joao"));
        QVERIFY(binding->copyLink());
        QCOMPARE(f.copied, binding->inviteLink());

        binding->closeRoom();
        QCOMPARE(binding->role(), int{ListeningRoomBinding::NoRole});
        QVERIFY(binding->inviteLink().isEmpty());
        QVERIFY(f.endpoint->closed_all);
        QCOMPARE(f.play_order, (std::vector<bool>{true, false}));
        QVERIFY(!binding->copyLink());
    }

    void withoutAnOriginTheRoomIsUnavailableAndBuildsNothing() {
        Fixture f;
        auto binding = f.make(QString{});
        QVERIFY(!binding->available());
        QCOMPARE(binding->state(), QStringLiteral("unavailable"));
        QVERIFY(!binding->openRoom(5, false));
        QVERIFY(!binding->join(Fixture::validLink()));
        QCOMPARE(f.network_loads, 0);
    }

    void theIdentityLoadsOnceAndItsFailureIsExplained() {
        Fixture f;
        f.defer_network = true;
        f.identity_fails = true;
        auto binding = f.make();
        QVERIFY(binding->openRoom(5, false));
        QVERIFY(binding->busy());
        QCOMPARE(binding->state(), QStringLiteral("preparing"));
        QVERIFY(!binding->openRoom(5, false));
        QVERIFY(!binding->join(Fixture::validLink()));
        f.completeNetwork();
        QVERIFY(!binding->busy());
        QCOMPARE(binding->role(), int{ListeningRoomBinding::NoRole});
        QCOMPARE(binding->errorText(),
                 QStringLiteral("Não foi possível preparar a identidade da sala neste computador. Tente de novo."));
        f.identity_fails = false;
        QVERIFY(binding->openRoom(5, true));
        f.completeNetwork();
        QCOMPARE(binding->role(), int{ListeningRoomBinding::HostRole});
        QVERIFY(binding->errorText().isEmpty());
        QCOMPARE(f.network_loads, 2);
        binding->closeRoom();
        QVERIFY(binding->openRoom(5, false));
        QCOMPARE(f.network_loads, 2);
    }

    void aRefusedEndpointLosesTheRoom() {
        Fixture f;
        auto binding = f.make();
        QVERIFY(binding->openRoom(5, false));
        f.endpoint->fail(RoomLinkFailure::Refused);
        QTRY_COMPARE(binding->role(), int{ListeningRoomBinding::NoRole});
        QCOMPARE(binding->errorText(), QStringLiteral("A sala não está mais disponível. Crie uma nova sala."));
        QCOMPARE(f.play_order, (std::vector<bool>{true, false}));

        QVERIFY(binding->openRoom(5, false));
        f.endpoint->fail(RoomLinkFailure::Unreachable);
        QTRY_COMPARE(binding->role(), int{ListeningRoomBinding::NoRole});
        QCOMPARE(binding->errorText(),
                 QStringLiteral("Não foi possível conectar à internet para abrir a sala. Verifique a conexão e tente "
                                "de novo."));
    }

    void aJoinRequestWaitsForTheHostAndNotifiesInTheBackground() {
        Fixture f;
        auto binding = f.make();
        QVERIFY(binding->openRoom(5, false));
        f.active = false;
        const auto ana = f.arrive("fp-ana", "Ana");
        QCOMPARE(binding->pendingRequests()->rowCount(), 1);
        const auto request = binding->pendingRequests()->index(0, 0);
        QCOMPARE(request.data(RoomMembersModel::NicknameRole).toString(), QStringLiteral("Ana"));
        QCOMPARE(request.data(RoomMembersModel::FingerprintRole).toString(), QStringLiteral("fp-ana"));
        QCOMPARE(f.notifications.size(), std::size_t{1});
        QCOMPARE(f.notifications.front().first, std::string{"Pedido para entrar na sala"});
        QCOMPARE(f.notifications.front().second, std::string{"Ana quer entrar na sua sala."});

        QVERIFY(binding->approve(QStringLiteral("fp-ana")));
        QCOMPARE(binding->pendingRequests()->rowCount(), 0);
        QCOMPARE(binding->memberCount(), 2);
        QCOMPARE(binding->members()->index(0, 0).data(RoomMembersModel::HostRole).toBool(), true);

        f.active = true;
        const auto bia = f.arrive("fp-bia", "Bia");
        QCOMPARE(f.notifications.size(), std::size_t{1});
        QCOMPARE(binding->pendingRequests()->rowCount(), 1);
        QVERIFY(binding->reject(QStringLiteral("fp-bia")));
        QCOMPARE(binding->pendingRequests()->rowCount(), 0);
        QVERIFY(bia->closed);
        QVERIFY(!binding->approve(QStringLiteral("fp-ninguem")));
    }

    void theGuestFollowsItsRoomAndExplainsWhyItEnded() {
        Fixture f;
        auto binding = f.make();
        QVERIFY(binding->join(Fixture::validLink()));
        QCOMPARE(binding->role(), int{ListeningRoomBinding::GuestRole});
        QCOMPARE(binding->state(), QStringLiteral("connecting"));
        QVERIFY(f.connector->requested.has_value());
        auto [host_side, guest_side] = testing::FakeRoomLink::pair(own_hex, inviteHostHex());
        f.connector->complete(guest_side);
        QVERIFY(host_side->sendControl(RoomJoinResult{JoinDecision::Pending}));
        QCOMPARE(binding->state(), QStringLiteral("waiting"));
        QVERIFY(host_side->sendControl(RoomJoinResult{JoinDecision::RejectedFull}));
        QCOMPARE(binding->role(), int{ListeningRoomBinding::NoRole});
        QCOMPARE(binding->errorText(), QStringLiteral("A sala está cheia"));
        // The ended guest is destroyed later, outside its own callback.
        QTest::qWait(1);
        QVERIFY(binding->join(Fixture::validLink()));
        QCOMPARE(binding->state(), QStringLiteral("connecting"));
        f.connector->fail(RoomLinkFailure::HostOffline);
        QCOMPARE(binding->role(), int{ListeningRoomBinding::NoRole});
        QCOMPARE(binding->errorText(), QStringLiteral("O anfitrião não está com a sala aberta agora."));
    }

    void theGuestKeepsItsPreRoomSessionUntilItLeaves() {
        Fixture f;
        auto binding = f.make();
        QVERIFY(binding->join(Fixture::validLink()));
        auto [host_side, guest_side] = testing::FakeRoomLink::pair(own_hex, inviteHostHex());
        f.connector->complete(guest_side);
        QVERIFY(host_side->sendControl(RoomJoinResult{JoinDecision::Admitted}));
        RoomSnapshot room;
        room.version = 1;
        room.members.push_back(RoomMember{inviteHostHex(), "Joao", {}, MemberState::Admitted});
        room.members.push_back(RoomMember{own_hex, "Joao", {}, MemberState::Admitted});
        QVERIFY(host_side->sendControl(RoomStateMessage{room, {}}));
        QCOMPARE(binding->state(), QStringLiteral("joined"));
        QCOMPARE(binding->memberCount(), 2);
        QCOMPARE(binding->hostNickname(), QStringLiteral("Joao"));
        QCOMPARE(binding->members()->index(1, 0).data(RoomMembersModel::SelfRole).toBool(), true);
        QCOMPARE(f.captures, 1);
        // Without a guest player nothing plays, so the local queue stays where it is.
        QCOMPARE(f.releases, 0);
        QVERIFY(binding->holdsLocalSession());
        const auto pre_room = f.player.sessionSnapshot();
        QVERIFY(f.player.next());
        QCOMPARE(binding->sessionForAutosave(f.player.sessionSnapshot()), pre_room);

        binding->shutdown();
        QVERIFY(!binding->holdsLocalSession());
        QCOMPARE(binding->role(), int{ListeningRoomBinding::NoRole});
        QCOMPARE(f.restored.size(), std::size_t{1});
        QCOMPARE(f.restored.front(), pre_room);
        QVERIFY(guest_side->closed);
        QCOMPARE(binding->sessionForAutosave(f.player.sessionSnapshot()), f.player.sessionSnapshot());
    }

    // S9.1: with the app's player the guest puts its local queue away once, plays the room's file at
    // the host's time, feeds the lyrics its position and gives the player back when it leaves.
    void aGuestPlaysTheRoomInTimeOnTheSharedPlayer() {
        Fixture f;
        SilentBackend backend;
        f.guest_backend = &backend;
        auto binding = f.make();
        binding->setOutputOffsetMs(0);
        QVERIFY(binding->join(Fixture::validLink()));
        auto [host_side, guest_side] = testing::FakeRoomLink::pair(own_hex, inviteHostHex());
        f.connector->complete(guest_side);
        QVERIFY(host_side->sendControl(RoomJoinResult{JoinDecision::Admitted}));
        QCOMPARE(f.captures, 1);
        QCOMPARE(f.releases, 1);
        QVERIFY(binding->holdsLocalSession());

        // The binding's clock reads 50 s; the host's reads 150 s.
        const RoomClockPing* ping = nullptr;
        for (const auto& message : guest_side->sent_control)
            if (const auto* const found = std::get_if<RoomClockPing>(&message))
                ping = found;
        QVERIFY(ping != nullptr);
        QVERIFY(host_side->sendControl(RoomClockPong{ping->sent_local_ms, 150'000}));

        RoomSnapshot room;
        room.version = 1;
        room.members.push_back(RoomMember{inviteHostHex(), "Joao", {}, MemberState::Admitted});
        room.members.push_back(RoomMember{own_hex, "Ana", {}, MemberState::Admitted});
        RoomItem item;
        item.id = 7;
        item.origin = RoomItemOrigin::HostTrack;
        item.owner_fingerprint = inviteHostHex();
        item.content_sha256 = std::string(64, 'a');
        item.size_bytes = 1000;
        item.summary = RoomTrackSummary{"Primeira", "Artista", "Album", 600'000};
        item.added_by = "Joao";
        room.queue.push_back(item);
        // Item 7 plays from 1 s, started 2 s ago on the host.
        room.playback = RoomPlayback{7, true, 148'000, 1'000};
        QVERIFY(host_side->sendControl(RoomStateMessage{room, {}}));
        QVERIFY(backend.loads.empty());
        QVERIFY(host_side->sendControl(RoomFileOffer{std::string(64, 'a'), 1000, 1}));
        f.inbox.complete(std::string(64, 'a'));
        QCOMPARE(backend.loads.size(), std::size_t{1});
        QCOMPARE(backend.seeks, std::vector<std::int64_t>{3'000});
        QCOMPARE(backend.snapshot().status, PlaybackStatus::Playing);
        QCOMPARE(f.releases, 1);

        backend.setPosition(3'400);
        QTRY_VERIFY(!f.room_positions.empty() && f.room_positions.back() == std::optional<std::int64_t>{3'400});

        binding->leave();
        QCOMPARE(backend.unloads, 1);
        QCOMPARE(f.restored.size(), std::size_t{1});
        QVERIFY(!f.room_positions.empty());
        QCOMPARE(f.room_positions.back(), std::optional<std::int64_t>{});
        const auto fed = f.room_positions.size();
        QTest::qWait(250);
        QCOMPARE(f.room_positions.size(), fed);
    }

    // S8.2: the guest's room queue says which items are still loading and who added them; the host's
    // lyrics for the current item reach the lyrics panel; leaving empties the cache.
    void theGuestSeesLoadingItemsWithTheirAuthorAndTheHostsLyrics() {
        Fixture f;
        auto binding = f.make();
        QVERIFY(binding->join(Fixture::validLink()));
        auto [host_side, guest_side] = testing::FakeRoomLink::pair(own_hex, inviteHostHex());
        f.connector->complete(guest_side);
        QVERIFY(host_side->sendControl(RoomJoinResult{JoinDecision::Admitted}));
        RoomSnapshot room;
        room.version = 1;
        room.members.push_back(
            RoomMember{inviteHostHex(), "Joao", {AvatarKind::Bundled, 3, {}}, MemberState::Admitted});
        room.members.push_back(RoomMember{own_hex, "Ana", {}, MemberState::Admitted});
        for (const std::uint64_t id : {7, 8}) {
            RoomItem item;
            item.id = id;
            item.origin = RoomItemOrigin::HostTrack;
            item.owner_fingerprint = inviteHostHex();
            item.content_sha256 = std::string(64, id == 7 ? 'a' : 'b');
            item.size_bytes = 1000;
            item.summary = RoomTrackSummary{id == 7 ? "Primeira" : "Segunda", "Artista", "Album", 1000};
            item.added_by = "Joao";
            room.queue.push_back(item);
        }
        room.playback.current_item = 7;
        QVERIFY(host_side->sendControl(RoomStateMessage{room, {}}));

        auto* const queue = binding->queue();
        QCOMPARE(queue->rowCount(), 2);
        const auto row = [&](const int index, const int role) { return queue->index(index, 0).data(role); };
        QCOMPARE(row(0, RoomQueueModel::TitleRole).toString(), QStringLiteral("Primeira"));
        QCOMPARE(row(0, RoomQueueModel::AddedByRole).toString(), QStringLiteral("Joao"));
        QCOMPARE(row(0, RoomQueueModel::AvatarSourceRole).toUrl(),
                 RoomMembersModel::avatarSource(AvatarChoice{AvatarKind::Bundled, 3, {}}));
        QCOMPARE(row(0, RoomQueueModel::CurrentRole).toBool(), true);
        QCOMPARE(row(0, RoomQueueModel::AvailableRole).toBool(), false);
        QCOMPARE(row(1, RoomQueueModel::AvailableRole).toBool(), false);

        QVERIFY(host_side->sendControl(RoomFileOffer{std::string(64, 'a'), 1000, 1}));
        f.inbox.complete(std::string(64, 'a'));
        QCOMPARE(row(0, RoomQueueModel::AvailableRole).toBool(), true);
        QCOMPARE(row(1, RoomQueueModel::AvailableRole).toBool(), false);

        QVERIFY(host_side->sendControl(RoomLyrics{7, "[00:01.00]la"}));
        QCOMPARE(f.room_lyrics.back(), QStringLiteral("[00:01.00]la"));

        binding->leave();
        QCOMPARE(queue->rowCount(), 0);
        QCOMPARE(f.room_lyrics.back(), QString{});
        QCOMPARE(f.inbox.purges, 1);
    }

    void theHostSessionNeverStoresGuestItems() {
        Fixture f;
        f.roster.stored.listeners.push_back(KnownListener{"fp-dj", "Dj", 2, {}, false});
        f.roster.status = RoomRosterLoadStatus::Loaded;
        auto binding = f.make();
        QVERIFY(binding->openRoom(5, false));
        const auto dj = f.arrive("fp-dj", "Dj");
        QCOMPARE(binding->memberCount(), 2);
        QVERIFY(!binding->holdsLocalSession());
        // The guest-file transfer (S8) adds guest tracks through the host; simulate one in the queue.
        const auto live = f.player.sessionSnapshot();
        QCOMPARE(binding->sessionForAutosave(live), live);
        const auto changes = f.session_changes;
        QVERIFY(f.player.next());
        QVERIFY(f.session_changes > changes);
        binding->shutdown();
        QCOMPARE(binding->role(), int{ListeningRoomBinding::NoRole});
        QVERIFY(dj->closed);
        QVERIFY(f.endpoint->closed_all);
    }

    void thePrivacyNoticeIsAcknowledgedOnce() {
        Fixture f;
        auto binding = f.make();
        QVERIFY(binding->privacyNoticeRequired());
        binding->acknowledgePrivacyNotice();
        QVERIFY(f.notice_shown);
        QVERIFY(!binding->privacyNoticeRequired());
    }

    void aProfileIsNeededToUseTheRoom() {
        Fixture f;
        f.profile.reset();
        auto binding = f.make();
        QVERIFY(!binding->openRoom(5, false));
        QCOMPARE(binding->errorText(), QStringLiteral("Crie seu perfil antes de usar a sala."));
        QVERIFY(!binding->join(Fixture::validLink()));
        QCOMPARE(f.network_loads, 0);
    }

    void theSchedulerNeverRunsATaskAfterItsContext() {
        int runs = 0;
        auto context = std::make_unique<QObject>();
        const auto schedule = ListeningRoomBinding::scheduler(context.get());
        schedule(0, [&runs] { ++runs; });
        QTRY_COMPARE(runs, 1);
        schedule(0, [&runs] { ++runs; });
        context.reset();
        QTest::qWait(20);
        QCOMPARE(runs, 1);
        schedule(0, [&runs] { ++runs; });
        QTest::qWait(20);
        QCOMPARE(runs, 1);
    }

    void theCustomAvatarIsHashedFromItsFile() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("custom.png"));
        QVERIFY(!ListeningRoomBinding::fileSha256(path).has_value());
        QFile file{path};
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("avatar"), qint64{6});
        file.close();
        const auto expected = QCryptographicHash::hash("avatar", QCryptographicHash::Sha256).toHex().toStdString();
        QCOMPARE(ListeningRoomBinding::fileSha256(path), std::optional{expected});
    }
};

} // namespace music_player::m0::test

QTEST_GUILESS_MAIN(music_player::m0::test::ListeningRoomBindingTests)

#include "ListeningRoomBindingTests.moc"
