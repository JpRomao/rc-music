// ListeningRoomHost (S7.1) against the real room codec: whatever the queue holds, every state the host
// sends must encode. In-memory transport, silent deterministic player, fake catalog; no network,
// files or audio.
#include "../support/FakeRoomHost.hpp"
#include "../support/FakeRoomTransport.hpp"

#include "adapters/RoomProtocolCodec.hpp"
#include "m0/application/ListeningRoomHost.hpp"
#include "m0/domain/ListeningRoomLimits.hpp"
#include "m0/domain/PlaylistLibrary.hpp"

#include <QTest>

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <variant>
#include <vector>

using namespace music_player::m0;
namespace {

constexpr std::array<std::uint8_t, 32> host_fingerprint{0xab, 0xcd, 0x01, 0x02};
const std::string host_hex = "abcd0102" + std::string(56, '0');

struct Guest final {
    std::shared_ptr<testing::FakeRoomLink> host_side;
    std::shared_ptr<testing::FakeRoomLink> link;
    std::vector<RoomStateMessage> states;
    std::vector<RoomCatalog> catalogs;
};

NamedPlaylist namedPlaylist(const std::uint64_t id, std::string name, const PlaylistVisibility visibility,
                            const std::vector<std::filesystem::path>& paths) {
    NamedPlaylist playlist{id, std::move(name), {}, visibility};
    for (std::size_t index = 0; index < paths.size(); ++index)
        playlist.entries.push_back(PlaylistEntry{id * 100'000 + index + 1, paths[index]});
    return playlist;
}

// A host over the fake player, catalog and transport whose link refuses what the codec cannot encode.
struct CatalogRoom final {
    explicit CatalogRoom(std::vector<std::filesystem::path> queue) : player(std::move(queue)) {
        store.stored = defaultRoomRoster();
        store.stored.listeners.push_back(KnownListener{"fp-dj", "Dj", 2, {}, false});
        store.status = RoomRosterLoadStatus::Loaded;
    }
    void start() {
        host = std::make_unique<ListeningRoomHost>(ListeningRoomHostDependencies{
            .roster_store = store,
            .player = player,
            .endpoint = endpoint,
            .catalog = catalog,
            .host_clock_ms = [] { return std::int64_t{10'000}; },
            .random_bytes =
                [this](std::span<std::uint8_t> bytes) {
                    for (auto& byte : bytes)
                        byte = next_random++;
                },
            .host_fingerprint = host_fingerprint,
            .host_profile = ListenerProfile{"Joao", AvatarChoice{}},
            .schedule = [](std::int64_t, std::function<void()>) {},
        });
    }
    std::unique_ptr<Guest> join(const std::string& fingerprint) {
        auto guest = std::make_unique<Guest>();
        auto [host_side, guest_side] = testing::FakeRoomLink::pair(host_hex, fingerprint);
        host_side->accepts_control = [](const RoomControlMessage& m) { return !encodeRoomControl(m).isEmpty(); };
        guest_side->onControl = [raw = guest.get()](RoomControlMessage m) {
            if (auto* catalog = std::get_if<RoomCatalog>(&m))
                raw->catalogs.push_back(std::move(*catalog));
        };
        guest->host_side = host_side;
        guest->link = guest_side;
        endpoint.admit(host_side);
        static_cast<void>(guest_side->sendControl(RoomHello{room_protocol_version, "Ana", AvatarChoice{}}));
        return guest;
    }

    testing::FakeRoomHostPlayer player;
    testing::InMemoryRoomRosterStore store;
    testing::FakeRoomHostEndpoint endpoint;
    testing::FakeRoomTrackCatalog catalog;
    std::uint8_t next_random{1};
    std::unique_ptr<ListeningRoomHost> host;
};

// Longer than maximum_room_summary_text_bytes and as expensive as JSON gets per byte: quotes,
// backslashes, 2- and 3-byte code points.
std::string adversarialText() {
    std::string text;
    while (text.size() <= maximum_room_summary_text_bytes)
        text += "\"\\\xc3\xa9\xe2\x82\xac";
    return text;
}

} // namespace

class ListeningRoomHostStateTests final : public QObject {
    Q_OBJECT

  private slots:
    void everyMemberGetsAnEncodableStateWithAFullQueueOfMaximumTags() {
        std::vector<std::filesystem::path> sources;
        for (std::size_t index = 0; index < maximum_room_queue_items; ++index)
            sources.emplace_back("music/" + std::to_string(index) + ".mp3");
        testing::FakeRoomHostPlayer player(sources);
        testing::InMemoryRoomRosterStore store;
        auto roster = defaultRoomRoster();
        roster.listeners.push_back(KnownListener{"fp-dj", "Dj", 2, {}, false});
        roster.listeners.push_back(KnownListener{"fp-listener", "Ouvinte", 1, {}, false});
        store.stored = roster;
        store.status = RoomRosterLoadStatus::Loaded;
        testing::FakeRoomHostEndpoint endpoint;
        testing::FakeRoomTrackCatalog catalog;
        const auto text = adversarialText();
        catalog.summary_override = RoomTrackSummary{text, text, text, 248'000};
        std::int64_t now = 10'000;
        std::uint8_t next_random = 1;
        ListeningRoomHost host(ListeningRoomHostDependencies{
            .roster_store = store,
            .player = player,
            .endpoint = endpoint,
            .catalog = catalog,
            .host_clock_ms = [&now] { return now; },
            .random_bytes =
                [&next_random](std::span<std::uint8_t> bytes) {
                    for (auto& byte : bytes)
                        byte = next_random++;
                },
            .host_fingerprint = host_fingerprint,
            .host_profile = ListenerProfile{"Joao", AvatarChoice{}},
            .schedule = [](std::int64_t, std::function<void()>) {},
        });
        QVERIFY(host.open({}).has_value());

        std::vector<std::unique_ptr<Guest>> guests;
        for (const std::string fingerprint : {"fp-dj", "fp-listener", "fp-new"}) {
            auto guest = std::make_unique<Guest>();
            auto [host_side, guest_side] = testing::FakeRoomLink::pair(host_hex, fingerprint);
            // The link refuses what the real transport would refuse: a message the codec cannot encode.
            host_side->accepts_control = [](const RoomControlMessage& m) { return !encodeRoomControl(m).isEmpty(); };
            guest_side->onControl = [raw = guest.get()](RoomControlMessage m) {
                if (auto* state = std::get_if<RoomStateMessage>(&m))
                    raw->states.push_back(std::move(*state));
            };
            guest->host_side = host_side;
            guest->link = guest_side;
            endpoint.admit(host_side);
            QVERIFY(guest_side->sendControl(RoomHello{room_protocol_version, "Ana", AvatarChoice{}}));
            guests.push_back(std::move(guest));
        }
        QVERIFY(host.approve("fp-new"));
        QCOMPARE(host.snapshot().members.size(), std::size_t{4});

        const auto check = [&] {
            const auto version = host.snapshot().version;
            for (const auto& guest : guests) {
                QVERIFY(!guest->states.empty());
                const auto& state = guest->states.back();
                QCOMPARE(state.snapshot.version, version);
                QVERIFY(!state.snapshot.queue.empty());
                QCOMPARE(state.snapshot.queue.front().id, *state.snapshot.playback.current_item);
                for (const auto& item : state.snapshot.queue)
                    QVERIFY(item.summary.title.size() <= maximum_room_summary_text_bytes);
                const auto encoded = encodeRoomControl(state);
                QVERIFY(!encoded.isEmpty());
                QVERIFY(static_cast<std::size_t>(encoded.size()) <= estimatedRoomStateBytes(state));
                QVERIFY(estimatedRoomStateBytes(state) <= maximum_room_control_message_bytes);
            }
        };
        check();
        // A change after joining is sent as well.
        player.next();
        check();
        QCOMPARE(guests.front()->states.back().snapshot.playback.current_item, std::optional{player.occurrenceAt(1)});
    }

    // S11.2: what guests receive of the host's playlists never names a file or a private list.
    void theEncodedCatalogHasNoPathsAndNoPrivatePlaylists() {
        CatalogRoom room({"music/a.mp3"});
        room.start();
        room.host->setPlaylists(PlaylistLibrarySnapshot{{
            namedPlaylist(1, "Segredos", PlaylistVisibility::Private, {"C:\\Users\\Ana\\Music\\privada.mp3"}),
            namedPlaylist(2, "Festa", PlaylistVisibility::Room,
                          {"C:\\Users\\Ana\\Music\\Festa\\d.mp3", "/home/ana/musica/e.mp3"}),
        }});
        QVERIFY(room.host->open({}).has_value());
        auto dj = room.join("fp-dj");
        QCOMPARE(dj->catalogs.size(), std::size_t{1});
        const auto& catalog = dj->catalogs.back();
        QCOMPARE(catalog.playlists.size(), std::size_t{1});
        QCOMPARE(catalog.playlists.front().entries.size(), std::size_t{2});
        const auto json = encodeRoomControl(catalog);
        QVERIFY(!json.isEmpty());
        QVERIFY(json.contains("Festa"));
        QVERIFY(!json.contains(":\\"));
        QVERIFY(!json.contains("/"));
        for (const auto* hidden : {"Segredos", "privada", "Users", "Music", "musica", ".mp3"})
            QVERIFY2(!json.contains(hidden), hidden);
    }

    // S11.2: a catalog of long lists with the most expensive tags still fits one control message.
    void theCatalogOfLongListsWithMaximumTagsStaysEncodable() {
        CatalogRoom room({"music/a.mp3"});
        const auto text = adversarialText();
        room.catalog.summary_override = RoomTrackSummary{text, text, text, 248'000};
        room.start();
        std::vector<std::filesystem::path> first;
        std::vector<std::filesystem::path> second;
        for (std::size_t index = 0; index < 400; ++index) {
            first.emplace_back("music/x" + std::to_string(index) + ".mp3");
            second.emplace_back("music/y" + std::to_string(index) + ".mp3");
        }
        room.host->setPlaylists(PlaylistLibrarySnapshot{{
            namedPlaylist(1, "Uma", PlaylistVisibility::Room, first),
            namedPlaylist(2, "Outra", PlaylistVisibility::Room, second),
        }});
        QVERIFY(room.host->open({}).has_value());
        auto dj = room.join("fp-dj");
        QVERIFY(!dj->catalogs.empty());
        const auto& catalog = dj->catalogs.back();
        QCOMPARE(catalog, room.host->catalog());
        QVERIFY(!catalog.playlists.empty());
        QVERIFY(!catalog.playlists.front().entries.empty());
        const auto json = encodeRoomControl(catalog);
        QVERIFY(!json.isEmpty());
        QVERIFY(static_cast<std::size_t>(json.size()) <= estimatedRoomCatalogBytes(catalog));
        QVERIFY(estimatedRoomCatalogBytes(catalog) <= maximum_room_control_message_bytes);
        // The files left out of the catalog were never read.
        std::size_t entries = 0;
        for (const auto& playlist : catalog.playlists)
            entries += playlist.entries.size();
        QVERIFY(room.catalog.requests.size() < entries + 64);
    }
};

QTEST_GUILESS_MAIN(ListeningRoomHostStateTests)
#include "ListeningRoomHostStateTests.moc"
