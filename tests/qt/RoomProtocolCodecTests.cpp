#include "adapters/RoomProtocolCodec.hpp"

#include "m0/domain/ListeningRoomLimits.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <cstdint>
#include <functional>
#include <limits>
#include <set>
#include <string>
#include <vector>

using namespace music_player::m0;

namespace {
constexpr std::uint64_t max_safe = 1ull << 53;
const std::string hash_a(64, 'a');
const std::string hash_b = std::string(32, '0') + std::string(32, 'f');

RoomTrackSummary summary(const std::string& title = "Águas de Março") {
    return {title, "Elis Regina", "Elis & Tom", 212000};
}

RoomItem hostItem(const std::uint64_t id = 1) {
    return {id, RoomItemOrigin::HostTrack, "host-fp", hash_a, 4'000'000, summary(), {}, "Ana"};
}

RoomItem youtubeItem() {
    return {7, RoomItemOrigin::YoutubeVideo, "guest-fp", {}, 0, summary("Vídeo"), "dQw4w9WgXcQ", "Bia"};
}

RoomMember member(const std::string& fingerprint, const std::string& nickname, const AvatarChoice avatar,
                  const MemberState state) {
    return {fingerprint, nickname, avatar, state};
}

RoomStateMessage stateMessage(const std::size_t queue_items = 2) {
    RoomStateMessage state;
    state.snapshot.version = 42;
    state.snapshot.members = {member("guest-fp", "Bia 🎵", {AvatarKind::Bundled, 3, {}}, MemberState::Admitted),
                              member("other-fp", "Caio", {AvatarKind::Custom, 0, hash_b}, MemberState::Pending)};
    for (std::size_t index = 0; index < queue_items; ++index)
        state.snapshot.queue.push_back(hostItem(index + 1));
    state.snapshot.playback = {std::uint64_t{1}, true, 1'700'000'000'000, 5000};
    state.my_permissions.set(static_cast<std::size_t>(RoomPermission::Skip));
    state.my_permissions.set(static_cast<std::size_t>(RoomPermission::AddYoutube));
    return state;
}

RoomIntent intentOf(const RoomIntentKind kind) {
    RoomIntent intent;
    intent.kind = kind;
    return intent;
}

std::vector<RoomControlMessage> everyVariant() {
    std::vector<RoomControlMessage> messages;
    messages.emplace_back(RoomHello{room_protocol_version, "Ana", {AvatarKind::Bundled, 19, {}}});
    messages.emplace_back(RoomHello{7, "Zé Ninguém", {AvatarKind::Custom, 0, hash_a}});
    messages.emplace_back(RoomJoinResult{JoinDecision::RejectedClosed});
    messages.emplace_back(RoomJoinResult{JoinDecision::Admitted});
    messages.emplace_back(stateMessage());
    auto idle = stateMessage(0);
    idle.snapshot.members.clear();
    idle.snapshot.playback = {};
    idle.snapshot.queue.push_back(youtubeItem());
    idle.my_permissions = RoomPermissionSet{}.set();
    messages.emplace_back(idle);
    for (const auto kind :
         {RoomIntentKind::Next, RoomIntentKind::Previous, RoomIntentKind::Pause, RoomIntentKind::Resume})
        messages.emplace_back(RoomIntentMessage{1, intentOf(kind)});
    auto seek = intentOf(RoomIntentKind::Seek);
    seek.position_ms = 61000;
    messages.emplace_back(RoomIntentMessage{2, seek});
    auto add = intentOf(RoomIntentKind::AddItems);
    add.catalog_entry_ids = {3, 4, max_safe};
    messages.emplace_back(RoomIntentMessage{3, add});
    auto upload = intentOf(RoomIntentKind::AddItems);
    upload.uploaded = RoomItem{0, RoomItemOrigin::GuestTrack, {}, hash_b, 1234, summary(), {}, {}};
    messages.emplace_back(RoomIntentMessage{4, upload});
    auto youtube = intentOf(RoomIntentKind::AddItems);
    youtube.uploaded = youtubeItem();
    messages.emplace_back(RoomIntentMessage{5, youtube});
    auto playlist = intentOf(RoomIntentKind::AddPlaylist);
    playlist.playlist_id = 9;
    messages.emplace_back(RoomIntentMessage{6, playlist});
    auto remove = intentOf(RoomIntentKind::RemoveItem);
    remove.item_id = 11;
    messages.emplace_back(RoomIntentMessage{7, remove});
    auto move = intentOf(RoomIntentKind::MoveItem);
    move.item_id = 12;
    move.destination = maximum_room_queue_items;
    messages.emplace_back(RoomIntentMessage{max_safe, move});
    messages.emplace_back(RoomIntentRejected{8, IntentRejection::QueueFull});
    messages.emplace_back(RoomClockPing{-5});
    messages.emplace_back(RoomClockPong{123, 1'700'000'000'000});
    messages.emplace_back(RoomCatalog{{{1, "Favoritas", {{10, summary()}, {11, {"", "", "", 0}}}}, {2, "Vazia", {}}}});
    messages.emplace_back(RoomCatalog{});
    messages.emplace_back(RoomFileRequest{hash_a, 65536});
    messages.emplace_back(RoomFileOffer{hash_b, maximum_guest_upload_bytes, std::numeric_limits<std::uint32_t>::max()});
    messages.emplace_back(RoomLyrics{3, "[00:01.00]linha um\n[00:02.50]ação ☕\r\n"});
    messages.emplace_back(RoomLyrics{3, {}});
    for (const auto reason : {RoomByeReason::Closed, RoomByeReason::Revoked, RoomByeReason::Kicked,
                              RoomByeReason::VersionMismatch, RoomByeReason::Full})
        messages.emplace_back(RoomBye{reason});
    messages.emplace_back(RoomDriftReport{60000});
    messages.emplace_back(RoomDriftReport{-60000});
    return messages;
}

QJsonObject encodedObject(const RoomControlMessage& message) {
    const auto bytes = encodeRoomControl(message);
    return QJsonDocument::fromJson(bytes).object();
}

QByteArray bytes(const QJsonObject& object) { return QJsonDocument{object}.toJson(QJsonDocument::Compact); }

// Encodes a valid message, lets the test tamper with its JSON and returns the tampered bytes.
QByteArray tampered(const RoomControlMessage& message, const std::function<void(QJsonObject&)>& edit) {
    auto object = encodedObject(message);
    edit(object);
    return bytes(object);
}

QJsonObject child(const QJsonObject& object, const QString& key) { return object.value(key).toObject(); }

void setChild(QJsonObject& object, const QString& key, const std::function<void(QJsonObject&)>& edit) {
    auto inner = child(object, key);
    edit(inner);
    object.insert(key, inner);
}

void editFirstQueueItem(QJsonObject& root, const std::function<void(QJsonObject&)>& edit) {
    setChild(root, QStringLiteral("snapshot"), [&](QJsonObject& snapshot) {
        auto queue = snapshot.value(QStringLiteral("queue")).toArray();
        auto item = queue.first().toObject();
        edit(item);
        queue.replace(0, item);
        snapshot.insert(QStringLiteral("queue"), queue);
    });
}

void editFirstMember(QJsonObject& root, const std::function<void(QJsonObject&)>& edit) {
    setChild(root, QStringLiteral("snapshot"), [&](QJsonObject& snapshot) {
        auto members = snapshot.value(QStringLiteral("members")).toArray();
        auto first = members.first().toObject();
        edit(first);
        members.replace(0, first);
        snapshot.insert(QStringLiteral("members"), members);
    });
}

// JSON written with single quotes keeps the literals readable (and moc-friendly).
QByteArray json(const char* text) { return QByteArray(text).replace('\'', '"'); }

bool rejected(const QByteArray& payload) { return !decodeRoomControl(payload).has_value(); }

RoomIntentMessage moveIntent() {
    auto move = intentOf(RoomIntentKind::MoveItem);
    move.item_id = 12;
    move.destination = 3;
    return {1, move};
}
} // namespace

class RoomProtocolCodecTests final : public QObject {
    Q_OBJECT
  private slots:
    void everyVariantRoundTrips() {
        std::set<std::size_t> covered;
        for (const auto& message : everyVariant()) {
            const auto encoded = encodeRoomControl(message);
            QVERIFY2(!encoded.isEmpty(), "valid message must encode");
            QVERIFY(encoded.size() <= static_cast<qsizetype>(maximum_room_control_message_bytes));
            const auto object = QJsonDocument::fromJson(encoded).object();
            QCOMPARE(object.value(QStringLiteral("v")).toInteger(), qint64{1});
            QVERIFY(object.value(QStringLiteral("t")).isString());
            const auto decoded = decodeRoomControl(encoded);
            QVERIFY2(decoded.has_value(), encoded.constData());
            QVERIFY2(*decoded == message, encoded.constData());
            covered.insert(message.index());
        }
        QCOMPARE(covered.size(), std::variant_size_v<RoomControlMessage>);
    }

    void stateCarriesThePlannedWireNames() {
        const auto object = encodedObject(stateMessage());
        QCOMPARE(object.value(QStringLiteral("t")).toString(), QStringLiteral("state"));
        QCOMPARE(object.value(QStringLiteral("permissions")).toInteger(), qint64{(1 << 0) | (1 << 6)});
        const auto snapshot = child(object, QStringLiteral("snapshot"));
        const auto item = snapshot.value(QStringLiteral("queue")).toArray().first().toObject();
        QCOMPARE(item.value(QStringLiteral("origin")).toString(), QStringLiteral("host_track"));
        const auto first = snapshot.value(QStringLiteral("members")).toArray().first().toObject();
        QCOMPARE(first.value(QStringLiteral("state")).toString(), QStringLiteral("admitted"));
        QCOMPARE(child(first, QStringLiteral("avatar")).value(QStringLiteral("kind")).toString(),
                 QStringLiteral("bundled"));
    }

    void rejectsMessagesAboveTheControlLimitBeforeParsing() {
        const auto valid = encodeRoomControl(RoomClockPing{1});
        const auto limit = static_cast<qsizetype>(maximum_room_control_message_bytes);
        const auto at_limit = QByteArray(limit - valid.size(), ' ') + valid;
        QVERIFY(decodeRoomControl(at_limit).has_value());
        QVERIFY(rejected(QByteArray(limit - valid.size() + 1, ' ') + valid));
    }

    void encodeRefusesWhatThePeerWouldDrop() {
        RoomCatalog huge;
        huge.playlists.push_back({1, "Grande", {}});
        for (std::uint64_t id = 0; id < 3000; ++id)
            huge.playlists[0].entries.push_back({id, summary(std::string(100, 'x'))});
        QVERIFY(encodeRoomControl(huge).isEmpty());
        QVERIFY(encodeRoomControl(RoomLyrics{1, std::string(maximum_room_lyrics_bytes + 1, 'a')}).isEmpty());
        QVERIFY(!encodeRoomControl(RoomLyrics{1, std::string(maximum_room_lyrics_bytes, 'a')}).isEmpty());
        QVERIFY(encodeRoomControl(RoomLyrics{1, "\xff\xfe"}).isEmpty());
        QVERIFY(encodeRoomControl(RoomHello{1, " Ana", {}}).isEmpty());
        QVERIFY(encodeRoomControl(RoomHello{1, "", {}}).isEmpty());
        QVERIFY(encodeRoomControl(RoomHello{1, "Ana", {AvatarKind::Bundled, bundled_avatar_count, {}}}).isEmpty());
        QVERIFY(encodeRoomControl(RoomDriftReport{60001}).isEmpty());
        QVERIFY(encodeRoomControl(RoomFileRequest{"ABC", 0}).isEmpty());
        QVERIFY(encodeRoomControl(RoomIntentRejected{max_safe + 1, IntentRejection::NotAdmitted}).isEmpty());
        QVERIFY(encodeRoomControl(stateMessage(maximum_room_queue_items + 1)).isEmpty());
        auto bad_title = stateMessage();
        bad_title.snapshot.queue[0].summary.title = "\tTítulo";
        QVERIFY(encodeRoomControl(bad_title).isEmpty());
        QVERIFY(encodeRoomControl(RoomBye{static_cast<RoomByeReason>(99)}).isEmpty());
    }

    void rejectsAnotherProtocolVersionOrMissingEnvelope() {
        const RoomControlMessage ping = RoomClockPing{1};
        QVERIFY(!rejected(encodeRoomControl(ping)));
        QVERIFY(rejected(tampered(ping, [](QJsonObject& o) { o.insert(QStringLiteral("v"), 2); })));
        QVERIFY(rejected(tampered(ping, [](QJsonObject& o) { o.insert(QStringLiteral("v"), QStringLiteral("1")); })));
        QVERIFY(rejected(tampered(ping, [](QJsonObject& o) { o.insert(QStringLiteral("v"), 1.5); })));
        QVERIFY(rejected(tampered(ping, [](QJsonObject& o) { o.remove(QStringLiteral("v")); })));
        QVERIFY(rejected(tampered(ping, [](QJsonObject& o) { o.remove(QStringLiteral("t")); })));
        QVERIFY(rejected(json("{'v':1,'v':1,'t':'clock_ping','sent_local_ms':1}")));
        QVERIFY(rejected(json("{'v':1,'t':'clock_ping','t':'clock_ping','sent_local_ms':1}")));
    }

    void rejectsUnknownTypes() {
        const RoomControlMessage ping = RoomClockPing{1};
        QVERIFY(
            rejected(tampered(ping, [](QJsonObject& o) { o.insert(QStringLiteral("t"), QStringLiteral("nope")); })));
        QVERIFY(rejected(tampered(ping, [](QJsonObject& o) { o.insert(QStringLiteral("t"), 3); })));
        QVERIFY(
            rejected(tampered(ping, [](QJsonObject& o) { o.insert(QStringLiteral("t"), QStringLiteral("hello")); })));
    }

    void rejectsHostileOrMalformedDocuments() {
        QVERIFY(rejected({}));
        QVERIFY(rejected("[]"));
        QVERIFY(rejected("1"));
        QVERIFY(rejected("null"));
        QVERIFY(rejected(json("{'v':1,'t':'clock_ping','sent_local_ms':1")));
        QVERIFY(rejected(json("{'v':1,'t':'clock_ping','sent_local_ms':1} trailing")));
        QVERIFY(rejected(json("{'v':1,'t':'clock_ping','sent_local_ms':+1}")));
        QVERIFY(rejected(QByteArray(static_cast<qsizetype>(maximum_room_control_message_bytes), '[')));
        QVERIFY(rejected(QByteArray(json("{'v':1,'t':'lyrics','item_id':1,'lrc':'")) + "\xc3\x28" + "\"}"));
        QByteArray nested = json("{'v':1,'t':'clock_ping','sent_local_ms':1,'x':");
        nested += QByteArray(10, '[') + QByteArray(10, ']') + '}';
        QVERIFY(rejected(nested));
    }

    void rejectsMissingWrongTypedOrExtraFields() {
        const RoomControlMessage hello = RoomHello{1, "Ana", {AvatarKind::Bundled, 2, {}}};
        QVERIFY(rejected(tampered(hello, [](QJsonObject& o) { o.remove(QStringLiteral("nickname")); })));
        QVERIFY(rejected(tampered(hello, [](QJsonObject& o) { o.insert(QStringLiteral("nickname"), 5); })));
        QVERIFY(
            rejected(tampered(hello, [](QJsonObject& o) { o.insert(QStringLiteral("avatar"), QStringLiteral("x")); })));
        QVERIFY(rejected(tampered(hello, [](QJsonObject& o) { o.insert(QStringLiteral("extra"), true); })));
        QVERIFY(rejected(tampered(hello, [](QJsonObject& o) {
            setChild(o, QStringLiteral("avatar"), [](QJsonObject& a) { a.remove(QStringLiteral("sha256")); });
        })));
        const RoomControlMessage rejection = RoomIntentRejected{5, IntentRejection::UnknownItem};
        QVERIFY(rejected(
            tampered(rejection, [](QJsonObject& o) { o.insert(QStringLiteral("request_id"), QStringLiteral("5")); })));
        QVERIFY(rejected(tampered(rejection, [](QJsonObject& o) { o.insert(QStringLiteral("request_id"), 5.5); })));
        QVERIFY(rejected(tampered(rejection, [](QJsonObject& o) { o.insert(QStringLiteral("request_id"), -1); })));
        QVERIFY(rejected(tampered(rejection, [](QJsonObject& o) { o.insert(QStringLiteral("reason"), 2); })));
        QVERIFY(rejected(tampered(
            rejection, [](QJsonObject& o) { o.insert(QStringLiteral("reason"), QStringLiteral("unknown")); })));
        const RoomControlMessage state = stateMessage();
        QVERIFY(rejected(tampered(state, [](QJsonObject& o) {
            editFirstQueueItem(o, [](QJsonObject& item) { item.remove(QStringLiteral("added_by")); });
        })));
        QVERIFY(rejected(tampered(state, [](QJsonObject& o) {
            setChild(o, QStringLiteral("snapshot"), [](QJsonObject& s) {
                setChild(s, QStringLiteral("playback"), [](QJsonObject& p) { p.insert(QStringLiteral("playing"), 1); });
            });
        })));
        QVERIFY(rejected(tampered(state, [](QJsonObject& o) {
            setChild(o, QStringLiteral("snapshot"),
                     [](QJsonObject& s) { s.insert(QStringLiteral("queue"), QJsonObject{}); });
        })));
        QVERIFY(rejected(tampered(state, [](QJsonObject& o) {
            editFirstMember(o, [](QJsonObject& m) { m.insert(QStringLiteral("state"), QStringLiteral("blocked")); });
        })));
        QVERIFY(rejected(tampered(RoomBye{RoomByeReason::Full},
                                  [](QJsonObject& o) { o.insert(QStringLiteral("reason"), QStringLiteral("gone")); })));
    }

    void integersMustBeSafeIntegralAndInRange() {
        const RoomControlMessage rejection = RoomIntentRejected{5, IntentRejection::UnknownItem};
        QVERIFY(!rejected(
            tampered(rejection, [](QJsonObject& o) { o.insert(QStringLiteral("request_id"), qint64(max_safe)); })));
        QVERIFY(rejected(
            tampered(rejection, [](QJsonObject& o) { o.insert(QStringLiteral("request_id"), qint64(max_safe + 1)); })));
        QVERIFY(rejected(tampered(rejection, [](QJsonObject& o) { o.insert(QStringLiteral("request_id"), 1e300); })));
        QVERIFY(rejected(json("{'v':1,'t':'intent_rejected','request_id':9007199254740993,'reason':'queue_full'}")));
        const RoomControlMessage ping = RoomClockPing{1};
        QVERIFY(!rejected(
            tampered(ping, [](QJsonObject& o) { o.insert(QStringLiteral("sent_local_ms"), -qint64(max_safe)); })));
        QVERIFY(rejected(
            tampered(ping, [](QJsonObject& o) { o.insert(QStringLiteral("sent_local_ms"), -qint64(max_safe) - 1); })));
        const RoomControlMessage offer = RoomFileOffer{hash_a, 10, 1};
        QVERIFY(rejected(
            tampered(offer, [](QJsonObject& o) { o.insert(QStringLiteral("transfer_id"), qint64{1} << 32); })));
        QVERIFY(rejected(tampered(moveIntent(), [](QJsonObject& o) {
            setChild(o, QStringLiteral("intent"), [](QJsonObject& i) {
                i.insert(QStringLiteral("destination"), qint64(maximum_room_queue_items + 1));
            });
        })));
        auto seek = intentOf(RoomIntentKind::Seek);
        seek.position_ms = 1;
        QVERIFY(rejected(tampered(RoomIntentMessage{1, seek}, [](QJsonObject& o) {
            setChild(o, QStringLiteral("intent"), [](QJsonObject& i) { i.insert(QStringLiteral("position_ms"), -1); });
        })));
        QVERIFY(rejected(tampered(stateMessage(), [](QJsonObject& o) {
            editFirstQueueItem(o, [](QJsonObject& item) {
                setChild(item, QStringLiteral("summary"),
                         [](QJsonObject& s) { s.insert(QStringLiteral("duration_ms"), -1); });
            });
        })));
        QVERIFY(rejected(tampered(stateMessage(), [](QJsonObject& o) {
            setChild(o, QStringLiteral("snapshot"), [](QJsonObject& s) {
                setChild(s, QStringLiteral("playback"),
                         [](QJsonObject& p) { p.insert(QStringLiteral("anchor_position_ms"), -1); });
            });
        })));
    }

    void rejectsAQueueAboveTheRoomLimit() {
        QVERIFY(!rejected(encodeRoomControl(stateMessage(maximum_room_queue_items))));
        auto object = encodedObject(stateMessage(maximum_room_queue_items));
        setChild(object, QStringLiteral("snapshot"), [](QJsonObject& s) {
            auto queue = s.value(QStringLiteral("queue")).toArray();
            auto extra = queue.last().toObject();
            extra.insert(QStringLiteral("id"), qint64(maximum_room_queue_items + 1));
            queue.append(extra);
            s.insert(QStringLiteral("queue"), queue);
        });
        QVERIFY(rejected(bytes(object)));
    }

    void rejectsLyricsAboveTheLimit() {
        const RoomControlMessage lyrics = RoomLyrics{1, "x"};
        QVERIFY(!rejected(tampered(lyrics, [](QJsonObject& o) {
            o.insert(QStringLiteral("lrc"), QString(qsizetype(maximum_room_lyrics_bytes), QLatin1Char('a')));
        })));
        QVERIFY(rejected(tampered(lyrics, [](QJsonObject& o) {
            o.insert(QStringLiteral("lrc"), QString(qsizetype(maximum_room_lyrics_bytes + 1), QLatin1Char('a')));
        })));
        // The limit counts UTF-8 bytes, not characters.
        QVERIFY(rejected(tampered(lyrics, [](QJsonObject& o) {
            o.insert(QStringLiteral("lrc"), QString(qsizetype(maximum_room_lyrics_bytes / 2 + 1), QChar(0x00e7)));
        })));
    }

    void rejectsInvalidNicknames_data() {
        QTest::addColumn<QString>("nickname");
        QTest::newRow("empty") << QString{};
        QTest::newRow("leading space") << QStringLiteral(" Ana");
        QTest::newRow("trailing space") << QStringLiteral("Ana ");
        QTest::newRow("tab") << QStringLiteral("A\tna");
        QTest::newRow("bidi") << QStringLiteral("A") + QChar(0x202e) + QStringLiteral("na");
        QTest::newRow("too long") << QString(33, QLatin1Char('a'));
    }
    void rejectsInvalidNicknames() {
        QFETCH(QString, nickname);
        const RoomControlMessage hello = RoomHello{1, "Ana", {}};
        QVERIFY(!rejected(encodeRoomControl(hello)));
        QVERIFY(rejected(tampered(hello, [&](QJsonObject& o) { o.insert(QStringLiteral("nickname"), nickname); })));
        QVERIFY(rejected(tampered(stateMessage(), [&](QJsonObject& o) {
            editFirstMember(o, [&](QJsonObject& m) { m.insert(QStringLiteral("nickname"), nickname); });
        })));
    }

    void rejectsHashesOutsideLowercaseHex64_data() {
        QTest::addColumn<QString>("hash");
        QTest::newRow("empty") << QString{};
        QTest::newRow("63") << QString(63, QLatin1Char('a'));
        QTest::newRow("65") << QString(65, QLatin1Char('a'));
        QTest::newRow("upper") << QString(64, QLatin1Char('A'));
        QTest::newRow("non hex") << QString(64, QLatin1Char('g'));
    }
    void rejectsHashesOutsideLowercaseHex64() {
        QFETCH(QString, hash);
        const auto set_hash = [&](QJsonObject& o) { o.insert(QStringLiteral("sha256"), hash); };
        QVERIFY(rejected(tampered(RoomFileRequest{hash_a, 0}, set_hash)));
        QVERIFY(rejected(tampered(RoomFileOffer{hash_a, 1, 1}, set_hash)));
        QVERIFY(rejected(tampered(stateMessage(), [&](QJsonObject& o) { editFirstQueueItem(o, set_hash); })));
        QVERIFY(rejected(tampered(RoomHello{1, "Ana", {AvatarKind::Custom, 0, hash_a}},
                                  [&](QJsonObject& o) { setChild(o, QStringLiteral("avatar"), set_hash); })));
    }

    void avatarsFollowTheDomainRules() {
        const RoomControlMessage bundled = RoomHello{1, "Ana", {AvatarKind::Bundled, 0, {}}};
        const auto set_avatar = [&](const QJsonObject& avatar) {
            return tampered(bundled, [&](QJsonObject& o) { o.insert(QStringLiteral("avatar"), avatar); });
        };
        const QString hash = QString::fromStdString(hash_a);
        QVERIFY(!rejected(set_avatar({{"kind", "bundled"}, {"index", 19}, {"sha256", ""}})));
        QVERIFY(rejected(set_avatar({{"kind", "bundled"}, {"index", 20}, {"sha256", ""}})));
        QVERIFY(rejected(set_avatar({{"kind", "bundled"}, {"index", 1}, {"sha256", hash}})));
        QVERIFY(!rejected(set_avatar({{"kind", "custom"}, {"index", 0}, {"sha256", hash}})));
        QVERIFY(rejected(set_avatar({{"kind", "custom"}, {"index", 1}, {"sha256", hash}})));
        QVERIFY(rejected(set_avatar({{"kind", "photo"}, {"index", 0}, {"sha256", hash}})));
    }

    void rejectsPermissionsOutsideTheKnownBits() {
        const auto set_permissions = [](const qint64 value) {
            return tampered(stateMessage(), [&](QJsonObject& o) { o.insert(QStringLiteral("permissions"), value); });
        };
        const qint64 all = (qint64{1} << room_permission_count) - 1;
        QVERIFY(!rejected(set_permissions(all)));
        QVERIFY(rejected(set_permissions(all + 1)));
        QVERIFY(rejected(set_permissions(-1)));
    }

    void rejectsDriftBeyondSixtySeconds() {
        const RoomControlMessage drift = RoomDriftReport{0};
        const auto set_drift = [&](const qint64 value) {
            return tampered(drift, [&](QJsonObject& o) { o.insert(QStringLiteral("drift_ms"), value); });
        };
        QVERIFY(!rejected(set_drift(60000)));
        QVERIFY(!rejected(set_drift(-60000)));
        QVERIFY(rejected(set_drift(60001)));
        QVERIFY(rejected(set_drift(-60001)));
    }

    void rejectsTextThatSanitizationWouldChange() {
        const auto title_of = [](const QString& title) {
            return tampered(stateMessage(), [&](QJsonObject& o) {
                editFirstQueueItem(o, [&](QJsonObject& item) {
                    setChild(item, QStringLiteral("summary"),
                             [&](QJsonObject& s) { s.insert(QStringLiteral("title"), title); });
                });
            });
        };
        QVERIFY(!rejected(title_of(QString::fromUtf8("日本 ☕"))));
        QVERIFY(rejected(title_of(QStringLiteral(" lead"))));
        QVERIFY(rejected(title_of(QStringLiteral("two  spaces"))));
        QVERIFY(rejected(title_of(QStringLiteral("line\nbreak"))));
        QVERIFY(rejected(tampered(stateMessage(), [](QJsonObject& o) {
            editFirstQueueItem(o, [](QJsonObject& item) {
                item.insert(QStringLiteral("added_by"), QStringLiteral("x") + QChar(0x200b));
            });
        })));
        const RoomControlMessage catalog = RoomCatalog{{{1, "Lista", {}}}};
        const auto playlist_named = [&](const QString& name) {
            return tampered(catalog, [&](QJsonObject& o) {
                auto playlists = o.value(QStringLiteral("playlists")).toArray();
                auto first = playlists.first().toObject();
                first.insert(QStringLiteral("name"), name);
                playlists.replace(0, first);
                o.insert(QStringLiteral("playlists"), playlists);
            });
        };
        QVERIFY(rejected(playlist_named(QString{})));
        QVERIFY(rejected(playlist_named(QStringLiteral("Lista\t"))));
    }

    void fingerprintsAreBoundedPrintableAscii() {
        const auto fingerprint_of = [](const QString& fingerprint) {
            return tampered(stateMessage(), [&](QJsonObject& o) {
                editFirstMember(o, [&](QJsonObject& m) { m.insert(QStringLiteral("fingerprint"), fingerprint); });
            });
        };
        QVERIFY(!rejected(fingerprint_of(QString(128, QLatin1Char('~')))));
        QVERIFY(rejected(fingerprint_of(QString(129, QLatin1Char('a')))));
        QVERIFY(rejected(fingerprint_of(QString{})));
        QVERIFY(rejected(fingerprint_of(QStringLiteral("a b"))));
        QVERIFY(rejected(fingerprint_of(QString::fromUtf8("açaí"))));
    }

    void youtubeIdsBelongOnlyToYoutubeItems() {
        RoomStateMessage state = stateMessage(0);
        state.snapshot.queue.push_back(youtubeItem());
        const auto youtube_id = [&](const QString& id) {
            return tampered(state, [&](QJsonObject& o) {
                editFirstQueueItem(o, [&](QJsonObject& item) { item.insert(QStringLiteral("youtube_id"), id); });
            });
        };
        QVERIFY(!rejected(youtube_id(QStringLiteral("a-b_C9xyzAB"))));
        QVERIFY(rejected(youtube_id(QStringLiteral("short"))));
        QVERIFY(rejected(youtube_id(QStringLiteral("abcdefghijkl"))));
        QVERIFY(rejected(youtube_id(QStringLiteral("abc/efghijk"))));
        QVERIFY(rejected(youtube_id(QString{})));
        QVERIFY(rejected(tampered(stateMessage(), [](QJsonObject& o) {
            editFirstQueueItem(
                o, [](QJsonObject& item) { item.insert(QStringLiteral("youtube_id"), QStringLiteral("dQw4w9WgXcQ")); });
        })));
        auto host = stateMessage(1);
        host.snapshot.queue[0].content_sha256.clear();
        QVERIFY(encodeRoomControl(host).isEmpty());
    }

    void addItemsHasExactlyOneShape() {
        auto mixed = intentOf(RoomIntentKind::AddItems);
        mixed.catalog_entry_ids = {1};
        mixed.uploaded = hostItem();
        QVERIFY(encodeRoomControl(RoomIntentMessage{1, mixed}).isEmpty());
        QVERIFY(encodeRoomControl(RoomIntentMessage{1, intentOf(RoomIntentKind::AddItems)}).isEmpty());
        auto bad_upload = intentOf(RoomIntentKind::AddItems);
        bad_upload.uploaded = hostItem();
        bad_upload.uploaded->content_sha256 = "nope";
        QVERIFY(encodeRoomControl(RoomIntentMessage{1, bad_upload}).isEmpty());
        auto valid = intentOf(RoomIntentKind::AddItems);
        valid.catalog_entry_ids = {1};
        QVERIFY(rejected(tampered(RoomIntentMessage{1, valid}, [](QJsonObject& o) {
            setChild(o, QStringLiteral("intent"), [](QJsonObject& i) {
                i.insert(
                    QStringLiteral("uploaded"),
                    QJsonDocument::fromJson(QByteArray(json("{'id':1,'origin':'guest_track','owner':'','sha256':'")) +
                                            QByteArray(64, 'a') +
                                            json("','size_bytes':1,'summary':{'title':'','artist':'','album':'','"
                                                 "duration_ms':0},'youtube_id':'','added_by':''}"))
                        .object());
            });
        })));
        QVERIFY(rejected(tampered(RoomIntentMessage{1, valid}, [](QJsonObject& o) {
            setChild(o, QStringLiteral("intent"), [](QJsonObject& i) {
                QJsonArray many;
                for (std::size_t index = 0; index <= maximum_room_queue_items; ++index)
                    many.append(qint64(index));
                i.insert(QStringLiteral("entries"), many);
            });
        })));
        QVERIFY(rejected(tampered(RoomIntentMessage{1, valid}, [](QJsonObject& o) {
            setChild(o, QStringLiteral("intent"),
                     [](QJsonObject& i) { i.insert(QStringLiteral("entries"), QJsonArray{1.5}); });
        })));
        QVERIFY(rejected(tampered(moveIntent(), [](QJsonObject& o) {
            setChild(o, QStringLiteral("intent"), [](QJsonObject& i) { i.remove(QStringLiteral("destination")); });
        })));
        QVERIFY(rejected(tampered(moveIntent(), [](QJsonObject& o) {
            setChild(o, QStringLiteral("intent"), [](QJsonObject& i) { i.insert(QStringLiteral("position_ms"), 0); });
        })));
    }

    void fileChunkFrameIsLittleEndianHeaderAndPayload() {
        const RoomFileChunk chunk{0x01020304u, 0x1122334455667788ull, {0xde, 0xad}};
        const auto encoded = encodeFileChunk(chunk);
        const QByteArray expected = QByteArray::fromHex("04030201"
                                                        "8877665544332211"
                                                        "dead");
        QCOMPARE(encoded, expected);
        QCOMPARE(decodeFileChunk(encoded), std::optional<RoomFileChunk>{chunk});
        const RoomFileChunk empty{7, 0, {}};
        QCOMPARE(encodeFileChunk(empty).size(), qsizetype{12});
        QCOMPARE(decodeFileChunk(encodeFileChunk(empty)), std::optional<RoomFileChunk>{empty});
    }

    void fileChunkRejectsOversizedPayloadOrTruncatedHeader() {
        RoomFileChunk full{1, 2, std::vector<std::uint8_t>(room_file_chunk_bytes, 0x5a)};
        const auto encoded = encodeFileChunk(full);
        QCOMPARE(encoded.size(), qsizetype(12 + room_file_chunk_bytes));
        QCOMPARE(decodeFileChunk(encoded), std::optional<RoomFileChunk>{full});
        full.payload.push_back(0);
        QVERIFY(encodeFileChunk(full).isEmpty());
        QVERIFY(!decodeFileChunk(encoded + QByteArray(1, '\0')).has_value());
        QVERIFY(!decodeFileChunk(QByteArray(11, '\0')).has_value());
        QVERIFY(!decodeFileChunk(QByteArray{}).has_value());
    }
};

QTEST_GUILESS_MAIN(RoomProtocolCodecTests)
#include "RoomProtocolCodecTests.moc"
