// ListeningRoomHost and ListeningRoomGuest end to end (S7.4) over the real transport
// (DataChannelRoomTransport) and the in-process relay (tests/support/LoopbackSignaling.hpp): 127.0.0.1,
// host ICE candidates only, real timers (ListeningRoomBinding::scheduler). The host's player is the
// silent deterministic one (FakeRoomHost.hpp) with a fixed host clock; the guests' local queues are plain
// SessionSnapshots. No internet, no real Worker, no audio and no personal files. The file-transfer
// cases (S8.2) send the project's small audio fixtures (tests/audio/corpus/fixtures/valid) into a
// QTemporaryDir room cache.
#include "../support/FakeRoomHost.hpp"
#include "../support/LoopbackSignaling.hpp"
#include "adapters/DataChannelRoomTransport.hpp"
#include "adapters/QtRoomFileInbox.hpp"
#include "adapters/QtRoomFileOutbox.hpp"
#include "adapters/WindowsRoomIdentity.hpp"
#include "m0/application/ListeningRoomGuest.hpp"
#include "m0/application/ListeningRoomHost.hpp"
#include "m0/application/RoomPlaybackFollower.hpp"
#include "m0/domain/ListeningRoomLimits.hpp"
#include "m0/domain/RoomInviteLink.hpp"
#include "presentation/ListeningRoomBinding.hpp"

#ifdef RC_MUSIC_E2E_UPLOAD_PROBE
#include "adapters/QtRoomTrackCatalog.hpp"
#include "m0/adapters/TagLibMetadataExtractor.hpp"
#endif

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
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

std::filesystem::path fixture(const char* name) {
    return std::filesystem::path{QDir(QStringLiteral(RC_MUSIC_TEST_AUDIO_FIXTURES)).filePath(name).toStdU16String()};
}

std::string sha256Of(const std::filesystem::path& file) {
    return ListeningRoomBinding::fileSha256(QString::fromStdU16String(file.u16string())).value_or(std::string{});
}

// A file of `size` random bytes: the size of a real song without shipping one (the host's catalog here only
// hashes it, and the guest's player is silent).
std::filesystem::path randomFile(const QTemporaryDir& directory, const char* name, const qsizetype size) {
    const auto path = QDir(directory.path()).filePath(QString::fromLatin1(name));
    QByteArray bytes(size, Qt::Uninitialized);
    QRandomGenerator::global()->fillRange(reinterpret_cast<quint32*>(bytes.data()), size / 4);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != size)
        qFatal("cannot write a test file");
    return std::filesystem::path{path.toStdU16String()};
}

// A 16-bit stereo 44.1 kHz PCM WAV of `data_bytes` random samples: valid audio of a real song's size for the
// host's upload probe. Tests never play it.
std::filesystem::path randomWav(const QTemporaryDir& directory, const char* name, const quint32 data_bytes) {
    const auto path = QDir(directory.path()).filePath(QString::fromLatin1(name));
    QByteArray bytes;
    const auto le32 = [&bytes](const quint32 value) {
        for (int shift = 0; shift < 32; shift += 8)
            bytes.append(static_cast<char>((value >> shift) & 0xFF));
    };
    const auto le16 = [&bytes](const quint16 value) {
        bytes.append(static_cast<char>(value & 0xFF));
        bytes.append(static_cast<char>(value >> 8));
    };
    bytes.append("RIFF");
    le32(36 + data_bytes);
    bytes.append("WAVEfmt ");
    le32(16);
    le16(1); // PCM
    le16(2);
    le32(44'100);
    le32(44'100 * 4);
    le16(4);
    le16(16);
    bytes.append("data");
    le32(data_bytes);
    const auto header = bytes.size();
    bytes.resize(header + static_cast<qsizetype>(data_bytes));
    QRandomGenerator::global()->fillRange(reinterpret_cast<quint32*>(bytes.data() + header), data_bytes / 4);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        qFatal("cannot write a test file");
    return std::filesystem::path{path.toStdU16String()};
}

std::int64_t steadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// The guest's player: silent, ready as soon as it loads, and a record of what it loaded.
class RecordingBackend final : public PlaybackBackend {
  public:
    [[nodiscard]] const PlaybackSnapshot& snapshot() const noexcept override { return snapshot_; }
    void setEventCallback(PlaybackEventCallback) override {}
    PlaybackCommandResult load(const std::filesystem::path& source) override {
        loads.push_back(source);
        snapshot_ = PlaybackSnapshot{PlaybackStatus::Ready, 0, 180'000, true, snapshot_.generation + 1};
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
        snapshot_.position_ms = position_ms;
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult setVolume(double) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult setMuted(bool) override { return PlaybackCommandResult::Accepted; }
    PlaybackCommandResult unload() override {
        snapshot_ = PlaybackSnapshot{PlaybackStatus::Empty, 0, 0, false, snapshot_.generation + 1};
        return PlaybackCommandResult::Accepted;
    }
    PlaybackCommandResult shutdown() override { return PlaybackCommandResult::Accepted; }

    std::vector<std::filesystem::path> loads;

  private:
    PlaybackSnapshot snapshot_{};
};

// Describes real files like QtRoomTrackCatalog: their SHA-256, size and the lyrics the test chose.
class HashingCatalog final : public RoomTrackCatalog {
  public:
    void describe(std::filesystem::path source,
                  std::function<void(std::optional<RoomTrackDescription>)> done) override {
        const auto sha = sha256Of(source);
        if (sha.empty()) {
            done(std::nullopt);
            return;
        }
        RoomTrackDescription description;
        description.item.origin = RoomItemOrigin::HostTrack;
        description.item.content_sha256 = sha;
        description.item.size_bytes =
            static_cast<std::uint64_t>(QFileInfo(QString::fromStdU16String(source.u16string())).size());
        description.item.summary = RoomTrackSummary{source.stem().string(), "Artista", "Album", 1000};
        if (const auto found = lyrics.find(source); found != lyrics.end())
            description.lyrics_lrc = found->second;
        done(description);
    }
    std::map<std::filesystem::path, std::string> lyrics;
};

// The guest's room cache (QtRoomFileInbox) with a record of the offers it took and the files it got.
class RecordingInbox final : public RoomFileInbox {
  public:
    explicit RecordingInbox(const QString& config_directory) : inner(config_directory) {
        inner.onComplete = [this](std::string sha256, std::filesystem::path file) {
            completed.push_back(sha256);
            if (onComplete)
                onComplete(std::move(sha256), std::move(file));
        };
        inner.onFailed = [this](std::string sha256, const std::uint32_t transfer_id, const RoomFileFailure failure) {
            failed.push_back(sha256);
            if (onFailed)
                onFailed(std::move(sha256), transfer_id, failure);
        };
    }
    void status(const std::string& sha256, std::function<void(RoomFileStatus)> done) override {
        inner.status(sha256, std::move(done));
    }
    void accept(const RoomFileOffer& offer) override {
        accepted.push_back(offer.content_sha256);
        inner.accept(offer);
    }
    void consume(RoomFileChunk chunk) override { inner.consume(std::move(chunk)); }
    void abandon(const std::uint32_t transfer_id) override { inner.abandon(transfer_id); }
    void markPlayed(const std::string& sha256) override { inner.markPlayed(sha256); }
    void purge() override { inner.purge(); }
    void discard(const std::string& sha256) override { inner.discard(sha256); }
    [[nodiscard]] QStringList cachedFiles() {
        inner.waitForWorker();
        return QDir(inner.cacheDirectory()).entryList(QDir::Files | QDir::NoDotAndDotDot);
    }

    QtRoomFileInbox inner;
    std::vector<std::string> accepted;
    std::vector<std::string> completed;
    std::vector<std::string> failed;
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
    GuestApp(const LoopbackSignaling& relay, const RoomIdentity& identity, std::string nickname,
             RoomFileInbox* files = nullptr, RoomTrackCatalog* upload_catalog = nullptr,
             RoomFileOutbox* upload_files = nullptr, RoomPlaybackFollower* follower = nullptr)
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
              .files = files,
              .follower = follower,
              .local_clock_ms = steadyMs,
              .upload_catalog = upload_catalog,
              .upload_files = upload_files,
          }) {
        guest.onEnded = [this](const RoomLinkFailure failure) { ended.push_back(failure); };
        guest.onUploadEnded = [this](const GuestUploadResult result) { uploads.push_back(result); };
    }

    std::string fingerprint;
    QObject context;
    DataChannelRoomGuestConnector real;
    SeverableConnector connector;
    SessionSnapshot local{localQueue()};
    std::vector<SessionSnapshot> restored;
    std::vector<RoomLinkFailure> ended;
    std::vector<GuestUploadResult> uploads;
    ListeningRoomGuest guest; // last: destroyed first
};

// A host app that sends real files: the fixtures as its queue, HashingCatalog and QtRoomFileOutbox.
struct FileHostApp final {
    FileHostApp(const LoopbackSignaling& relay, const RoomIdentity& identity,
                std::vector<std::filesystem::path> sources, const bool select_first = true)
        : player(std::move(sources), select_first), endpoint(relay.origin(), identity, local_only),
          host(ListeningRoomHostDependencies{
              .roster_store = roster,
              .player = player,
              .endpoint = endpoint,
              .catalog = catalog,
              .host_clock_ms = [] { return std::int64_t{100'000}; },
              .random_bytes =
                  [](std::span<std::uint8_t> bytes) {
                      QRandomGenerator::system()->generate(bytes.begin(), bytes.end());
                  },
              .host_fingerprint = identity.fingerprint,
              .host_profile = ListenerProfile{"Joao", AvatarChoice{}},
              .schedule = ListeningRoomBinding::scheduler(&context),
              .files = &outbox,
          }) {}

    QObject context;
    testing::FakeRoomHostPlayer player;
    HashingCatalog catalog;
    testing::InMemoryRoomRosterStore roster;
    DataChannelRoomHostEndpoint endpoint;
    QtRoomFileOutbox outbox;
    ListeningRoomHost host; // last: destroyed first
};

#ifdef RC_MUSIC_E2E_UPLOAD_PROBE
// S12.1: a host that also takes guest uploads into its own upload cache (QtRoomFileInbox in
// <config>/room-uploads) and checks them with the real probe (QtRoomTrackCatalog over TagLib).
struct UploadHostApp final {
    UploadHostApp(const LoopbackSignaling& relay, const RoomIdentity& identity, const QString& config,
                  std::vector<std::filesystem::path> sources)
        : player(std::move(sources)), endpoint(relay.origin(), identity, local_only),
          uploads(config, QStringLiteral("room-uploads"), maximum_guest_upload_bytes),
          probe(std::make_shared<TagLibMetadataExtractor>(), nullptr),
          host(ListeningRoomHostDependencies{
              .roster_store = roster,
              .player = player,
              .endpoint = endpoint,
              .catalog = catalog,
              .host_clock_ms = [] { return std::int64_t{100'000}; },
              .random_bytes =
                  [](std::span<std::uint8_t> bytes) {
                      QRandomGenerator::system()->generate(bytes.begin(), bytes.end());
                  },
              .host_fingerprint = identity.fingerprint,
              .host_profile = ListenerProfile{"Joao", AvatarChoice{}},
              .schedule = ListeningRoomBinding::scheduler(&context),
              .files = &outbox,
              .uploads = &uploads,
              .upload_probe = &probe,
          }) {}
    [[nodiscard]] QStringList uploadedFiles() {
        uploads.waitForWorker();
        return QDir(uploads.cacheDirectory()).entryList(QDir::Files | QDir::NoDotAndDotDot);
    }
    [[nodiscard]] std::optional<RoomItem> item(const std::string& sha256) const {
        const auto queue = host.snapshot().queue;
        const auto found = std::find_if(queue.begin(), queue.end(),
                                        [&](const RoomItem& item) { return item.content_sha256 == sha256; });
        return found == queue.end() ? std::nullopt : std::optional{*found};
    }

    QObject context;
    testing::FakeRoomHostPlayer player;
    HashingCatalog catalog;
    testing::InMemoryRoomRosterStore roster;
    DataChannelRoomHostEndpoint endpoint;
    QtRoomFileOutbox outbox;
    QtRoomFileInbox uploads;
    QtRoomTrackCatalog probe;
    ListeningRoomHost host; // last: destroyed first
};
#endif

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
    template <typename App>
    static std::optional<RoomInviteLink> openRoom(App& app, const LoopbackSignaling& relay,
                                                  const RoomOpenOptions& options) {
        const auto before = relay.host_tokens.size();
        auto invite = app.host.open(options);
        if (!invite || !QTest::qWaitFor([&] { return relay.host_tokens.size() > before; }, connect_ms))
            return std::nullopt;
        return invite;
    }

    // An unknown guest asks to enter and the host approves: the guest is Joined with the host's state.
    template <typename App> static bool approveIn(App& app, GuestApp& visitor, const RoomInviteLink& invite) {
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

    // S11.2: the host's visible playlists reach a guest through the real codec and transport, a DJ adds a
    // whole list from them, and a list made private leaves the guest's catalog.
    void aDjAddsAVisiblePlaylistAndPrivateListsNeverArrive() {
        LoopbackSignaling relay;
        HostApp app(relay, host_id_, rosterWith());
        app.host.setPlaylists(PlaylistLibrarySnapshot{{
            NamedPlaylist{1, "Segredos", {{11, "music/segredo.mp3"}}, PlaylistVisibility::Private},
            NamedPlaylist{2, "Festa", {{21, "music/d.mp3"}, {22, "music/e.mp3"}}, PlaylistVisibility::Room},
        }});
        const auto invite = openRoom(app, relay, {.default_role_id = dj_role});
        QVERIFY(invite.has_value());
        GuestApp ana(relay, ana_id_, "Ana");
        QVERIFY(approveIn(app, ana, *invite));
        QTRY_COMPARE_WITH_TIMEOUT(ana.guest.catalog(), app.host.catalog(), connect_ms);
        const auto catalog = ana.guest.catalog();
        QCOMPARE(catalog.playlists.size(), std::size_t{1});
        QCOMPARE(catalog.playlists.front().name, std::string{"Festa"});
        QCOMPARE(catalog.playlists.front().entries.size(), std::size_t{2});

        QVERIFY(ana.guest.sendIntent(
                    RoomIntent{.kind = RoomIntentKind::AddPlaylist, .playlist_id = catalog.playlists.front().id}) != 0);
        QTRY_COMPARE_WITH_TIMEOUT(app.player.paths().size(), std::size_t{5}, connect_ms);
        QCOMPARE(app.player.paths()[3], std::filesystem::path{"music/d.mp3"});
        QCOMPARE(app.player.paths()[4], std::filesystem::path{"music/e.mp3"});
        QTRY_COMPARE_WITH_TIMEOUT(ana.guest.snapshot().queue.size(), std::size_t{5}, connect_ms);

        app.host.setPlaylists(PlaylistLibrarySnapshot{{
            NamedPlaylist{2, "Festa", {{21, "music/d.mp3"}, {22, "music/e.mp3"}}, PlaylistVisibility::Private},
        }});
        QTRY_VERIFY_WITH_TIMEOUT(ana.guest.catalog().playlists.empty(), connect_ms);
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
            .restore_session = [](const SessionSnapshot&) { return true; },
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
    // S8.2: the current item first, then the next room_prefetch_items in queue order; a file the guest
    // already has is not sent again; the host's lyrics arrive; leaving empties the cache.
    void theGuestReceivesTheCurrentItemFirstThenTheNextOnesAndLeavesNoCache() {
        LoopbackSignaling relay;
        const auto wav = fixture("wav-pcm-mono.wav");
        const auto flac = fixture("flac-16-stereo.flac");
        const auto m4a = fixture("m4a-aac-lc.m4a");
        const auto ogg = fixture("ogg-vorbis.ogg");
        // The wav twice inside the window: one transfer.
        FileHostApp app(relay, host_id_, {wav, flac, wav, m4a, ogg});
        app.catalog.lyrics[wav] = "[00:01.00]la\n[00:02.00]lo";
        const auto invite = openRoom(app, relay, {});
        QVERIFY(invite.has_value());
        QTemporaryDir ana_config;
        QVERIFY(ana_config.isValid());
        RecordingInbox inbox(ana_config.path());
        GuestApp ana(relay, ana_id_, "Ana", &inbox);
        QVERIFY(approveIn(app, ana, *invite));

        QTRY_COMPARE_WITH_TIMEOUT(inbox.completed.size(), std::size_t{3}, connect_ms);
        const std::vector<std::string> window{sha256Of(wav), sha256Of(flac), sha256Of(m4a)};
        QCOMPARE(inbox.accepted, window);
        QCOMPARE(inbox.completed, window);
        QVERIFY(inbox.failed.empty());
        const auto received = ana.guest.localFile(sha256Of(wav));
        QVERIFY(received.has_value());
        {
            QFile original(QString::fromStdU16String(wav.u16string()));
            QFile copy(QString::fromStdU16String(received->u16string()));
            QVERIFY(original.open(QIODevice::ReadOnly) && copy.open(QIODevice::ReadOnly));
            QCOMPARE(copy.readAll(), original.readAll());
        } // closed: Windows cannot delete an open file
        QVERIFY(!ana.guest.localFile(sha256Of(ogg)));
        const RoomLyrics lyrics{app.player.occurrenceAt(0), "[00:01.00]la\n[00:02.00]lo"};
        QTRY_VERIFY_WITH_TIMEOUT(ana.guest.lyrics() == std::optional{lyrics}, connect_ms);

        // The next song: ogg enters the window; nothing else is sent again.
        QVERIFY(app.player.next());
        QTRY_COMPARE_WITH_TIMEOUT(inbox.completed.size(), std::size_t{4}, connect_ms);
        QCOMPARE(inbox.accepted.back(), sha256Of(ogg));
        QCOMPARE(inbox.accepted.size(), std::size_t{4});
        QVERIFY(!ana.guest.lyrics()); // flac has none
        QCOMPARE(inbox.cachedFiles().size(), qsizetype{4});

        ana.guest.leave();
        QCOMPARE(inbox.cachedFiles(), QStringList{});
    }

    // S12.1: a DJ's own file goes up through the real transport, the host checks it with the local-import
    // rules and queues it as Ana's; it stays and reaches Bia after Ana leaves; once it has played it leaves the
    // queue and its file the host's upload cache.
    void aGuestsOwnTrackIsCheckedQueuedOutlivesItsOwnerAndLeavesOnceItHasPlayed() {
#ifndef RC_MUSIC_E2E_UPLOAD_PROBE
        QSKIP("built without the TagLib metadata extractor");
#else
        LoopbackSignaling relay;
        const auto wav = fixture("wav-pcm-mono.wav");
        const auto mp3 = fixture("mp3-cbr-stereo.mp3");
        QTemporaryDir host_config;
        QVERIFY(host_config.isValid());
        UploadHostApp app(relay, host_id_, host_config.path(), {wav});
        const auto invite = openRoom(app, relay, {.default_role_id = dj_role});
        QVERIFY(invite.has_value());
        HashingCatalog ana_catalog;
        QtRoomFileOutbox ana_outbox;
        GuestApp ana(relay, ana_id_, "Ana", nullptr, &ana_catalog, &ana_outbox);
        QVERIFY(approveIn(app, ana, *invite));
        QTemporaryDir bia_config;
        QVERIFY(bia_config.isValid());
        RecordingInbox bia_inbox(bia_config.path());
        GuestApp bia(relay, bia_id_, "Bia", &bia_inbox);
        QVERIFY(approveIn(app, bia, *invite));

        QVERIFY(ana.guest.uploadTrack(mp3));
        const auto sha = sha256Of(mp3);
        QTRY_COMPARE_WITH_TIMEOUT(ana.uploads, std::vector<GuestUploadResult>{GuestUploadResult::Added}, connect_ms);
        const auto queued = app.item(sha);
        QVERIFY(queued.has_value());
        QCOMPARE(queued->origin, RoomItemOrigin::GuestTrack);
        QCOMPARE(queued->owner_fingerprint, ana.fingerprint);
        QCOMPARE(queued->added_by, std::string{"Ana"});
        QCOMPARE(queued->size_bytes,
                 static_cast<std::uint64_t>(QFileInfo(QString::fromStdU16String(mp3.u16string())).size()));
        QCOMPARE(app.uploadedFiles(), QStringList{QString::fromStdString(sha)});
        // The host's copy is byte for byte Ana's file.
        {
            QFile original(QString::fromStdU16String(mp3.u16string()));
            QFile copy(QDir(app.uploads.cacheDirectory()).filePath(QString::fromStdString(sha)));
            QVERIFY(original.open(QIODevice::ReadOnly) && copy.open(QIODevice::ReadOnly));
            QCOMPARE(copy.readAll(), original.readAll());
        }

        // Ana leaves: her track stays and Bia, who follows the queue, receives it from the host.
        ana.guest.leave();
        QTRY_VERIFY_WITH_TIMEOUT(!hasMember(app.host.snapshot(), ana.fingerprint), connect_ms);
        QVERIFY(app.item(sha).has_value());
        QTRY_VERIFY_WITH_TIMEOUT(std::ranges::find(bia_inbox.completed, sha) != bia_inbox.completed.end(), connect_ms);
        QVERIFY(bia.guest.localFile(sha).has_value());

        // Fix rounds 1 and 2: once it has played (current, then the room moves on) it leaves the queue, and its
        // file the host's upload cache, so the cache's quota is not used up by tracks that are gone.
        QVERIFY(app.player.next());
        QCOMPARE(app.host.snapshot().playback.current_item, queued->id);
        app.player.repeat = true;
        QVERIFY(app.player.next());
        QTRY_VERIFY_WITH_TIMEOUT(!app.item(sha).has_value() && app.player.paths().size() == 1, connect_ms);
        QTRY_COMPARE_WITH_TIMEOUT(app.uploadedFiles(), QStringList{}, connect_ms);

        // The room ends with the host's own track only.
        app.host.close();
        QCOMPARE(app.player.paths().size(), std::size_t{1});
        QCOMPARE(app.uploadedFiles(), QStringList{});
#endif
    }

    // S12.1: a text file renamed .mp3 passes the guest's extension check but not the host's probe: it is
    // refused as invalid audio and nothing of it stays on the host.
    void aTextFileRenamedMp3IsRefusedAndLeavesNothingOnTheHost() {
#ifndef RC_MUSIC_E2E_UPLOAD_PROBE
        QSKIP("built without the TagLib metadata extractor");
#else
        LoopbackSignaling relay;
        QTemporaryDir host_config;
        QVERIFY(host_config.isValid());
        UploadHostApp app(relay, host_id_, host_config.path(), {fixture("wav-pcm-mono.wav")});
        const auto invite = openRoom(app, relay, {.default_role_id = dj_role});
        QVERIFY(invite.has_value());
        QTemporaryDir ana_files;
        QVERIFY(ana_files.isValid());
        const auto fake = QDir(ana_files.path()).filePath(QStringLiteral("letra.mp3"));
        {
            QFile text(fake);
            QVERIFY(text.open(QIODevice::WriteOnly));
            QByteArray lines;
            for (int line = 0; line < 2000; ++line)
                lines += "Isto e um texto, nao uma musica.\n";
            QCOMPARE(text.write(lines), lines.size());
        }
        HashingCatalog ana_catalog;
        QtRoomFileOutbox ana_outbox;
        GuestApp ana(relay, ana_id_, "Ana", nullptr, &ana_catalog, &ana_outbox);
        QVERIFY(approveIn(app, ana, *invite));
        const auto version = app.host.snapshot().version;

        QVERIFY(ana.guest.uploadTrack(std::filesystem::path{fake.toStdU16String()}));
        QTRY_COMPARE_WITH_TIMEOUT(ana.uploads, std::vector<GuestUploadResult>{GuestUploadResult::InvalidAudio},
                                  connect_ms);
        QCOMPARE(app.host.snapshot().version, version);
        QCOMPARE(app.host.snapshot().queue.size(), std::size_t{1});
        QCOMPARE(app.uploadedFiles(), QStringList{});
#endif
    }

    // S13.1 (first real two-app run): two 7 MB files, the size of a 3-minute MP3. Over the real transport the
    // second one's offer arrived after all of its chunks, and the transfer stopped at 2 MiB for good.
    void twoSongFilesOfRealSizeBothArrive() {
        QTemporaryDir sources;
        QVERIFY(sources.isValid());
        const auto left = randomFile(sources, "left.mp3", 7'202'019);
        const auto right = randomFile(sources, "right.mp3", 7'202'018);
        LoopbackSignaling relay;
        FileHostApp app(relay, host_id_, {left, right});
        const auto invite = openRoom(app, relay, {});
        QVERIFY(invite.has_value());
        QTemporaryDir ana_config;
        QVERIFY(ana_config.isValid());
        RecordingInbox inbox(ana_config.path());
        GuestApp ana(relay, ana_id_, "Ana", &inbox);
        QVERIFY(approveIn(app, ana, *invite));

        QTRY_COMPARE_WITH_TIMEOUT(inbox.completed.size(), std::size_t{2}, connect_ms);
        QCOMPARE(inbox.completed, (std::vector<std::string>{sha256Of(left), sha256Of(right)}));
        QVERIFY(inbox.failed.empty());
    }

    // S13.1 fix round 2: a guest's two 7 MB uploads, one after the other on its link. The second upload's offer
    // can come after all of its chunks, like the host's own files in twoSongFilesOfRealSizeBothArrive.
    void twoGuestUploadsOfRealSizeBothJoinTheQueue() {
#ifndef RC_MUSIC_E2E_UPLOAD_PROBE
        QSKIP("built without the TagLib metadata extractor");
#else
        QTemporaryDir sources;
        QVERIFY(sources.isValid());
        const auto first = randomWav(sources, "first.wav", 7'200'000);
        const auto second = randomWav(sources, "second.wav", 7'200'004);
        LoopbackSignaling relay;
        QTemporaryDir host_config;
        QVERIFY(host_config.isValid());
        UploadHostApp app(relay, host_id_, host_config.path(), {fixture("wav-pcm-mono.wav")});
        const auto invite = openRoom(app, relay, {.default_role_id = dj_role});
        QVERIFY(invite.has_value());
        HashingCatalog ana_catalog;
        QtRoomFileOutbox ana_outbox;
        GuestApp ana(relay, ana_id_, "Ana", nullptr, &ana_catalog, &ana_outbox);
        QVERIFY(approveIn(app, ana, *invite));

        QVERIFY(ana.guest.uploadTrack(first));
        QVERIFY(ana.guest.uploadTrack(second));
        QTRY_COMPARE_WITH_TIMEOUT(ana.uploads,
                                  (std::vector<GuestUploadResult>{GuestUploadResult::Added, GuestUploadResult::Added}),
                                  connect_ms);
        QVERIFY(app.item(sha256Of(first)).has_value());
        QVERIFY(app.item(sha256Of(second)).has_value());
        auto uploaded = app.uploadedFiles();
        uploaded.sort();
        QStringList expected{QString::fromStdString(sha256Of(first)), QString::fromStdString(sha256Of(second))};
        expected.sort();
        QCOMPARE(uploaded, expected);
#endif
    }

    // S13.1: the host queued two tracks without playing either, the guest came in, and only then the host
    // played the first one. The guest's player (silent) must load the cached file and play.
    void aGuestWhoJoinedBeforeAnythingPlayedPlaysTheFirstItemWhenTheHostStarts() {
        QTemporaryDir sources;
        QVERIFY(sources.isValid());
        const auto left = randomFile(sources, "left.mp3", 7'202'019);
        const auto right = randomFile(sources, "right.mp3", 7'202'018);
        LoopbackSignaling relay;
        FileHostApp app(relay, host_id_, {left, right}, false);
        QVERIFY(!app.player.currentIndex());
        const auto invite = openRoom(app, relay, {});
        QVERIFY(invite.has_value());
        QTemporaryDir ana_config;
        QVERIFY(ana_config.isValid());
        RecordingInbox inbox(ana_config.path());
        RecordingBackend backend;
        QObject follower_context;
        RoomPlaybackFollower follower(backend, steadyMs, room_drift_nudge_ms, room_drift_seek_ms,
                                      ListeningRoomBinding::scheduler(&follower_context));
        GuestApp ana(relay, ana_id_, "Ana", &inbox, nullptr, nullptr, &follower);
        QVERIFY(approveIn(app, ana, *invite));
        QTRY_VERIFY_WITH_TIMEOUT(ana.guest.localFile(sha256Of(left)).has_value(), connect_ms);
        QVERIFY(backend.loads.empty());

        QVERIFY(app.player.playAt(0));
        QTRY_COMPARE_WITH_TIMEOUT(ana.guest.snapshot().playback.current_item, std::optional{app.player.occurrenceAt(0)},
                                  connect_ms);
        QTRY_COMPARE_WITH_TIMEOUT(backend.snapshot().status, PlaybackStatus::Playing, connect_ms);
        QCOMPARE(backend.loads, std::vector{*ana.guest.localFile(sha256Of(left))});
    }

    void aGuestJoiningMidSongReceivesTheCurrentItemFirst() {
        LoopbackSignaling relay;
        const auto wav = fixture("wav-pcm-mono.wav");
        const auto flac = fixture("flac-16-stereo.flac");
        const auto m4a = fixture("m4a-aac-lc.m4a");
        const auto mp3 = fixture("mp3-cbr-stereo.mp3");
        const auto ogg = fixture("ogg-vorbis.ogg");
        FileHostApp app(relay, host_id_, {wav, flac, m4a, mp3, ogg});
        QVERIFY(app.player.next()); // flac is playing when Ana arrives
        const auto invite = openRoom(app, relay, {});
        QVERIFY(invite.has_value());
        QTemporaryDir ana_config;
        QVERIFY(ana_config.isValid());
        RecordingInbox inbox(ana_config.path());
        GuestApp ana(relay, ana_id_, "Ana", &inbox);
        QVERIFY(approveIn(app, ana, *invite));

        QTRY_COMPARE_WITH_TIMEOUT(inbox.completed.size(), std::size_t{4}, connect_ms);
        const std::vector<std::string> window{sha256Of(flac), sha256Of(m4a), sha256Of(mp3), sha256Of(ogg)};
        QCOMPARE(inbox.accepted, window);
        QCOMPARE(inbox.completed.front(), sha256Of(flac));
        QVERIFY(!ana.guest.localFile(sha256Of(wav)));
    }
};

QTEST_GUILESS_MAIN(ListeningRoomEndToEndTests)
#include "ListeningRoomEndToEndTests.moc"
