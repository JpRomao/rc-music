#include "RoomProtocolCodec.hpp"

#include "StrictJson.hpp"
#include "m0/domain/ListeningRoomLimits.hpp"
#include "m0/domain/MetadataSanitization.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1StringView>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <utility>

namespace music_player::m0 {
namespace {
constexpr std::int64_t max_safe_integer = std::int64_t{1} << 53;
constexpr std::int64_t max_drift_ms = 60000;
constexpr std::size_t max_fingerprint_bytes = 128;
constexpr std::size_t youtube_id_chars = 11;
// Deepest path the format needs: root > playlists > playlist > entries > entry > summary.
constexpr std::size_t max_json_depth = 6;
constexpr qsizetype chunk_header_bytes = 12;

constexpr std::array join_names{"admitted", "pending", "rejected_full", "rejected_blocked", "rejected_closed"};
constexpr std::array rejection_names{"not_admitted", "missing_permission", "unknown_item", "invalid_argument",
                                     "queue_full"};
constexpr std::array bye_names{"closed", "revoked", "kicked", "version_mismatch", "full"};
constexpr std::array intent_names{"next",      "previous",     "seek",        "pause",    "resume",
                                  "add_items", "add_playlist", "remove_item", "move_item"};
constexpr std::array origin_names{"host_track", "guest_track", "youtube_video"};
constexpr std::array member_state_names{"pending", "admitted"};
constexpr std::array avatar_kind_names{"bundled", "custom"};

// ---- Validation shared by encode and decode -------------------------------------------------------------

template <typename Enum, std::size_t Count> bool knownEnum(const Enum value, const std::array<const char*, Count>&) {
    return static_cast<std::size_t>(value) < Count;
}

bool safeUnsigned(const std::uint64_t value) { return value <= static_cast<std::uint64_t>(max_safe_integer); }
bool safeSigned(const std::int64_t value) { return value >= -max_safe_integer && value <= max_safe_integer; }
bool safeNonNegative(const std::int64_t value) { return value >= 0 && value <= max_safe_integer; }

bool lowerHex64(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](const char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

bool validFingerprint(const std::string& value) {
    return !value.empty() && value.size() <= max_fingerprint_bytes &&
           std::all_of(value.begin(), value.end(), [](const char c) { return c >= 0x21 && c <= 0x7e; });
}

bool validYoutubeId(const std::string& value) {
    return value.size() == youtube_id_chars && std::all_of(value.begin(), value.end(), [](const char c) {
               return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
                      c == '-';
           });
}

bool canonical(const std::string& nickname) {
    const auto result = canonicalNickname(nickname);
    return result.has_value() && *result == nickname;
}

// Metadata text the peer would display: sanitizeMetadataText must leave it unchanged.
bool unchangedText(const std::string& value, const bool allow_empty) {
    if (value.empty())
        return allow_empty;
    return sanitizeMetadataText(value).disposition == MetadataTextDisposition::Unchanged;
}

bool validLyrics(const std::string& value) {
    return value.size() <= maximum_room_lyrics_bytes &&
           (value.empty() || sanitizeMetadataText(value).disposition != MetadataTextDisposition::InvalidEncoding);
}

bool valid(const RoomTrackSummary& summary) {
    return unchangedText(summary.title, true) && unchangedText(summary.artist, true) &&
           unchangedText(summary.album, true) && safeNonNegative(summary.duration_ms);
}

bool valid(const RoomItem& item) {
    if (!safeUnsigned(item.id) || !safeUnsigned(item.size_bytes) || !valid(item.summary) ||
        !unchangedText(item.added_by, true) ||
        (!item.owner_fingerprint.empty() && !validFingerprint(item.owner_fingerprint)))
        return false;
    switch (item.origin) {
    case RoomItemOrigin::HostTrack:
    case RoomItemOrigin::GuestTrack:
        return lowerHex64(item.content_sha256) && item.youtube_id.empty();
    case RoomItemOrigin::YoutubeVideo:
        return (item.content_sha256.empty() || lowerHex64(item.content_sha256)) && validYoutubeId(item.youtube_id);
    }
    return false;
}

bool valid(const RoomMember& member) {
    return validFingerprint(member.fingerprint) && canonical(member.nickname) && isValidAvatarChoice(member.avatar) &&
           knownEnum(member.state, member_state_names);
}

bool valid(const RoomSnapshot& snapshot) {
    const auto& playback = snapshot.playback;
    return safeUnsigned(snapshot.version) && snapshot.queue.size() <= maximum_room_queue_items &&
           std::all_of(snapshot.members.begin(), snapshot.members.end(), [](const auto& m) { return valid(m); }) &&
           std::all_of(snapshot.queue.begin(), snapshot.queue.end(), [](const auto& i) { return valid(i); }) &&
           (!playback.current_item.has_value() || safeUnsigned(*playback.current_item)) &&
           safeSigned(playback.anchor_host_ms) && safeNonNegative(playback.anchor_position_ms);
}

bool valid(const RoomIntent& intent) {
    switch (intent.kind) {
    case RoomIntentKind::Next:
    case RoomIntentKind::Previous:
    case RoomIntentKind::Pause:
    case RoomIntentKind::Resume:
        return true;
    case RoomIntentKind::Seek:
        return safeNonNegative(intent.position_ms);
    case RoomIntentKind::AddItems:
        if (intent.uploaded.has_value())
            return intent.catalog_entry_ids.empty() && valid(*intent.uploaded);
        return !intent.catalog_entry_ids.empty() && intent.catalog_entry_ids.size() <= maximum_room_queue_items &&
               std::all_of(intent.catalog_entry_ids.begin(), intent.catalog_entry_ids.end(), safeUnsigned);
    case RoomIntentKind::AddPlaylist:
        return safeUnsigned(intent.playlist_id);
    case RoomIntentKind::RemoveItem:
        return safeUnsigned(intent.item_id);
    case RoomIntentKind::MoveItem:
        return safeUnsigned(intent.item_id) && intent.destination <= maximum_room_queue_items;
    }
    return false;
}

bool valid(const RoomHello& m) { return canonical(m.nickname) && isValidAvatarChoice(m.avatar); }
bool valid(const RoomJoinResult& m) { return knownEnum(m.decision, join_names); }
bool valid(const RoomStateMessage& m) { return valid(m.snapshot); }
bool valid(const RoomIntentMessage& m) { return safeUnsigned(m.request_id) && valid(m.intent); }
bool valid(const RoomIntentRejected& m) { return safeUnsigned(m.request_id) && knownEnum(m.reason, rejection_names); }
bool valid(const RoomClockPing& m) { return safeSigned(m.sent_local_ms); }
bool valid(const RoomClockPong& m) { return safeSigned(m.sent_local_ms) && safeSigned(m.host_ms); }
bool valid(const RoomCatalog& m) {
    return std::all_of(m.playlists.begin(), m.playlists.end(), [](const RoomCatalogPlaylist& playlist) {
        return safeUnsigned(playlist.id) && unchangedText(playlist.name, false) &&
               std::all_of(playlist.entries.begin(), playlist.entries.end(), [](const RoomCatalogEntry& entry) {
                   return safeUnsigned(entry.id) && valid(entry.summary);
               });
    });
}
bool valid(const RoomFileRequest& m) { return lowerHex64(m.content_sha256) && safeUnsigned(m.offset); }
bool valid(const RoomFileOffer& m) { return lowerHex64(m.content_sha256) && safeUnsigned(m.size_bytes); }
bool valid(const RoomLyrics& m) { return safeUnsigned(m.item_id) && validLyrics(m.lrc_text); }
bool valid(const RoomBye& m) { return knownEnum(m.reason, bye_names); }
bool valid(const RoomDriftReport& m) { return m.drift_ms >= -max_drift_ms && m.drift_ms <= max_drift_ms; }

// ---- Encoding (only called on validated messages) --------------------------------------------------------

QJsonValue number(const std::uint64_t value) { return QJsonValue{static_cast<qint64>(value)}; }
QJsonValue number(const std::int64_t value) { return QJsonValue{static_cast<qint64>(value)}; }
QJsonValue text(const std::string& value) { return QJsonValue{QString::fromStdString(value)}; }
template <typename Enum, std::size_t Count>
QJsonValue name(const Enum value, const std::array<const char*, Count>& names) {
    return QJsonValue{QString::fromLatin1(names[static_cast<std::size_t>(value)])};
}

QJsonObject encode(const AvatarChoice& avatar) {
    return {{QStringLiteral("kind"), name(avatar.kind, avatar_kind_names)},
            {QStringLiteral("index"), number(std::uint64_t{avatar.bundled_index})},
            {QStringLiteral("sha256"), text(avatar.custom_sha256)}};
}

QJsonObject encode(const RoomTrackSummary& summary) {
    return {{QStringLiteral("title"), text(summary.title)},
            {QStringLiteral("artist"), text(summary.artist)},
            {QStringLiteral("album"), text(summary.album)},
            {QStringLiteral("duration_ms"), number(summary.duration_ms)}};
}

QJsonObject encode(const RoomItem& item) {
    return {{QStringLiteral("id"), number(item.id)},
            {QStringLiteral("origin"), name(item.origin, origin_names)},
            {QStringLiteral("owner"), text(item.owner_fingerprint)},
            {QStringLiteral("sha256"), text(item.content_sha256)},
            {QStringLiteral("size_bytes"), number(item.size_bytes)},
            {QStringLiteral("summary"), encode(item.summary)},
            {QStringLiteral("youtube_id"), text(item.youtube_id)},
            {QStringLiteral("added_by"), text(item.added_by)}};
}

QJsonObject encode(const RoomMember& member) {
    return {{QStringLiteral("fingerprint"), text(member.fingerprint)},
            {QStringLiteral("nickname"), text(member.nickname)},
            {QStringLiteral("avatar"), encode(member.avatar)},
            {QStringLiteral("state"), name(member.state, member_state_names)}};
}

QJsonObject encode(const RoomSnapshot& snapshot) {
    QJsonArray members;
    for (const auto& member : snapshot.members)
        members.append(encode(member));
    QJsonArray queue;
    for (const auto& item : snapshot.queue)
        queue.append(encode(item));
    const auto& playback = snapshot.playback;
    const QJsonObject encoded_playback{
        {QStringLiteral("current"), playback.current_item ? number(*playback.current_item) : QJsonValue{}},
        {QStringLiteral("playing"), playback.playing},
        {QStringLiteral("anchor_host_ms"), number(playback.anchor_host_ms)},
        {QStringLiteral("anchor_position_ms"), number(playback.anchor_position_ms)}};
    return {{QStringLiteral("version"), number(snapshot.version)},
            {QStringLiteral("members"), members},
            {QStringLiteral("queue"), queue},
            {QStringLiteral("playback"), encoded_playback}};
}

QJsonObject encode(const RoomIntent& intent) {
    QJsonObject object{{QStringLiteral("kind"), name(intent.kind, intent_names)}};
    switch (intent.kind) {
    case RoomIntentKind::Seek:
        object.insert(QStringLiteral("position_ms"), number(intent.position_ms));
        break;
    case RoomIntentKind::AddItems: {
        QJsonArray entries;
        for (const auto id : intent.catalog_entry_ids)
            entries.append(number(id));
        object.insert(QStringLiteral("entries"), entries);
        object.insert(QStringLiteral("uploaded"),
                      intent.uploaded ? QJsonValue{encode(*intent.uploaded)} : QJsonValue{});
        break;
    }
    case RoomIntentKind::AddPlaylist:
        object.insert(QStringLiteral("playlist_id"), number(intent.playlist_id));
        break;
    case RoomIntentKind::RemoveItem:
        object.insert(QStringLiteral("item_id"), number(intent.item_id));
        break;
    case RoomIntentKind::MoveItem:
        object.insert(QStringLiteral("item_id"), number(intent.item_id));
        object.insert(QStringLiteral("destination"), number(std::uint64_t{intent.destination}));
        break;
    default:
        break;
    }
    return object;
}

struct Body final {
    const char* type;
    QJsonObject fields;
};

Body body(const RoomHello& m) {
    return {"hello",
            {{QStringLiteral("protocol_version"), number(std::uint64_t{m.protocol_version})},
             {QStringLiteral("nickname"), text(m.nickname)},
             {QStringLiteral("avatar"), encode(m.avatar)}}};
}
Body body(const RoomJoinResult& m) {
    return {"join_result", {{QStringLiteral("decision"), name(m.decision, join_names)}}};
}
Body body(const RoomStateMessage& m) {
    return {"state",
            {{QStringLiteral("snapshot"), encode(m.snapshot)},
             {QStringLiteral("permissions"), number(std::uint64_t{m.my_permissions.to_ullong()})}}};
}
Body body(const RoomIntentMessage& m) {
    return {"intent",
            {{QStringLiteral("request_id"), number(m.request_id)}, {QStringLiteral("intent"), encode(m.intent)}}};
}
Body body(const RoomIntentRejected& m) {
    return {"intent_rejected",
            {{QStringLiteral("request_id"), number(m.request_id)},
             {QStringLiteral("reason"), name(m.reason, rejection_names)}}};
}
Body body(const RoomClockPing& m) {
    return {"clock_ping", {{QStringLiteral("sent_local_ms"), number(m.sent_local_ms)}}};
}
Body body(const RoomClockPong& m) {
    return {
        "clock_pong",
        {{QStringLiteral("sent_local_ms"), number(m.sent_local_ms)}, {QStringLiteral("host_ms"), number(m.host_ms)}}};
}
Body body(const RoomCatalog& m) {
    QJsonArray playlists;
    for (const auto& playlist : m.playlists) {
        QJsonArray entries;
        for (const auto& entry : playlist.entries)
            entries.append(QJsonObject{{QStringLiteral("id"), number(entry.id)},
                                       {QStringLiteral("summary"), encode(entry.summary)}});
        playlists.append(QJsonObject{{QStringLiteral("id"), number(playlist.id)},
                                     {QStringLiteral("name"), text(playlist.name)},
                                     {QStringLiteral("entries"), entries}});
    }
    return {"catalog", {{QStringLiteral("playlists"), playlists}}};
}
Body body(const RoomFileRequest& m) {
    return {"file_request",
            {{QStringLiteral("sha256"), text(m.content_sha256)}, {QStringLiteral("offset"), number(m.offset)}}};
}
Body body(const RoomFileOffer& m) {
    return {"file_offer",
            {{QStringLiteral("sha256"), text(m.content_sha256)},
             {QStringLiteral("size_bytes"), number(m.size_bytes)},
             {QStringLiteral("transfer_id"), number(std::uint64_t{m.transfer_id})}}};
}
Body body(const RoomLyrics& m) {
    return {"lyrics", {{QStringLiteral("item_id"), number(m.item_id)}, {QStringLiteral("lrc"), text(m.lrc_text)}}};
}
Body body(const RoomBye& m) { return {"bye", {{QStringLiteral("reason"), name(m.reason, bye_names)}}}; }
Body body(const RoomDriftReport& m) { return {"drift", {{QStringLiteral("drift_ms"), number(m.drift_ms)}}}; }

// ---- Decoding ---------------------------------------------------------------------------------------------

bool exactKeys(const QJsonObject& object, const std::initializer_list<const char*> keys) {
    return object.size() == static_cast<qsizetype>(keys.size()) &&
           std::all_of(keys.begin(), keys.end(),
                       [&object](const char* key) { return object.contains(QLatin1StringView{key}); });
}

std::optional<std::int64_t> safeInteger(const QJsonValue& value) {
    if (!value.isDouble())
        return std::nullopt;
    const double approximate = value.toDouble();
    if (!std::isfinite(approximate) || std::trunc(approximate) != approximate ||
        std::fabs(approximate) > static_cast<double>(max_safe_integer))
        return std::nullopt;
    // Integers parsed exactly keep their exact value here, so 2^53 + 1 cannot round into range.
    const qint64 exact = value.toInteger(std::numeric_limits<qint64>::min());
    if (!safeSigned(exact))
        return std::nullopt;
    return exact;
}

// Reads typed fields of one object; any failure latches ok() to false and yields a default value.
class Fields final {
  public:
    explicit Fields(const QJsonObject& object) : object_(object) {}

    [[nodiscard]] bool ok() const noexcept { return ok_; }
    void require(const bool condition) noexcept { ok_ = ok_ && condition; }

    std::int64_t integer(const char* key, const std::int64_t minimum, const std::int64_t maximum) {
        const auto value = safeInteger(object_.value(QLatin1StringView{key}));
        require(value.has_value() && *value >= minimum && *value <= maximum);
        return ok_ ? *value : 0;
    }
    std::int64_t signedInteger(const char* key) { return integer(key, -max_safe_integer, max_safe_integer); }
    std::uint64_t unsignedInteger(const char* key) {
        return static_cast<std::uint64_t>(integer(key, 0, max_safe_integer));
    }
    std::uint32_t u32(const char* key) {
        return static_cast<std::uint32_t>(integer(key, 0, std::numeric_limits<std::uint32_t>::max()));
    }
    std::string string(const char* key) {
        const auto value = object_.value(QLatin1StringView{key});
        require(value.isString());
        return ok_ ? value.toString().toStdString() : std::string{};
    }
    bool boolean(const char* key) {
        const auto value = object_.value(QLatin1StringView{key});
        require(value.isBool());
        return ok_ && value.toBool();
    }
    QJsonObject object(const char* key) {
        const auto value = object_.value(QLatin1StringView{key});
        require(value.isObject());
        return ok_ ? value.toObject() : QJsonObject{};
    }
    QJsonArray array(const char* key, const std::size_t maximum_size) {
        const auto value = object_.value(QLatin1StringView{key});
        require(value.isArray() && static_cast<std::size_t>(value.toArray().size()) <= maximum_size);
        return ok_ ? value.toArray() : QJsonArray{};
    }
    template <typename Enum, std::size_t Count>
    Enum enumeration(const char* key, const std::array<const char*, Count>& names) {
        const auto value = object_.value(QLatin1StringView{key});
        if (value.isString()) {
            const auto found = value.toString();
            for (std::size_t index = 0; index < Count; ++index) {
                if (found == QLatin1StringView{names[index]})
                    return static_cast<Enum>(index);
            }
        }
        require(false);
        return Enum{};
    }

  private:
    const QJsonObject& object_;
    bool ok_{true};
};

constexpr std::size_t unbounded = std::numeric_limits<std::size_t>::max();

std::optional<AvatarChoice> decodeAvatar(const QJsonObject& object) {
    if (!exactKeys(object, {"kind", "index", "sha256"}))
        return std::nullopt;
    Fields f{object};
    AvatarChoice avatar{f.enumeration<AvatarKind>("kind", avatar_kind_names), f.u32("index"), f.string("sha256")};
    return f.ok() ? std::optional{std::move(avatar)} : std::nullopt;
}

std::optional<RoomTrackSummary> decodeSummary(const QJsonObject& object) {
    if (!exactKeys(object, {"title", "artist", "album", "duration_ms"}))
        return std::nullopt;
    Fields f{object};
    RoomTrackSummary summary{f.string("title"), f.string("artist"), f.string("album"),
                             f.integer("duration_ms", 0, max_safe_integer)};
    return f.ok() ? std::optional{std::move(summary)} : std::nullopt;
}

std::optional<RoomItem> decodeItem(const QJsonObject& object) {
    if (!exactKeys(object, {"id", "origin", "owner", "sha256", "size_bytes", "summary", "youtube_id", "added_by"}))
        return std::nullopt;
    Fields f{object};
    RoomItem item;
    item.id = f.unsignedInteger("id");
    item.origin = f.enumeration<RoomItemOrigin>("origin", origin_names);
    item.owner_fingerprint = f.string("owner");
    item.content_sha256 = f.string("sha256");
    item.size_bytes = f.unsignedInteger("size_bytes");
    const auto summary = decodeSummary(f.object("summary"));
    f.require(summary.has_value());
    item.youtube_id = f.string("youtube_id");
    item.added_by = f.string("added_by");
    if (!f.ok())
        return std::nullopt;
    item.summary = *summary;
    return item;
}

std::optional<RoomMember> decodeMember(const QJsonObject& object) {
    if (!exactKeys(object, {"fingerprint", "nickname", "avatar", "state"}))
        return std::nullopt;
    Fields f{object};
    RoomMember member;
    member.fingerprint = f.string("fingerprint");
    member.nickname = f.string("nickname");
    const auto avatar = decodeAvatar(f.object("avatar"));
    f.require(avatar.has_value());
    member.state = f.enumeration<MemberState>("state", member_state_names);
    if (!f.ok())
        return std::nullopt;
    member.avatar = *avatar;
    return member;
}

std::optional<RoomSnapshot> decodeSnapshot(const QJsonObject& object) {
    if (!exactKeys(object, {"version", "members", "queue", "playback"}))
        return std::nullopt;
    Fields f{object};
    RoomSnapshot snapshot;
    snapshot.version = f.unsignedInteger("version");
    const auto members = f.array("members", unbounded);
    const auto queue = f.array("queue", maximum_room_queue_items);
    const auto playback = f.object("playback");
    if (!f.ok() || !exactKeys(playback, {"current", "playing", "anchor_host_ms", "anchor_position_ms"}))
        return std::nullopt;
    for (const auto& value : members) {
        auto member = value.isObject() ? decodeMember(value.toObject()) : std::nullopt;
        if (!member)
            return std::nullopt;
        snapshot.members.push_back(std::move(*member));
    }
    snapshot.queue.reserve(static_cast<std::size_t>(queue.size()));
    for (const auto& value : queue) {
        auto item = value.isObject() ? decodeItem(value.toObject()) : std::nullopt;
        if (!item)
            return std::nullopt;
        snapshot.queue.push_back(std::move(*item));
    }
    Fields p{playback};
    if (!playback.value(QStringLiteral("current")).isNull())
        snapshot.playback.current_item = p.unsignedInteger("current");
    snapshot.playback.playing = p.boolean("playing");
    snapshot.playback.anchor_host_ms = p.signedInteger("anchor_host_ms");
    snapshot.playback.anchor_position_ms = p.integer("anchor_position_ms", 0, max_safe_integer);
    return p.ok() ? std::optional{std::move(snapshot)} : std::nullopt;
}

std::optional<RoomIntent> decodeIntent(const QJsonObject& object) {
    Fields f{object};
    RoomIntent intent;
    intent.kind = f.enumeration<RoomIntentKind>("kind", intent_names);
    if (!f.ok())
        return std::nullopt;
    switch (intent.kind) {
    case RoomIntentKind::Next:
    case RoomIntentKind::Previous:
    case RoomIntentKind::Pause:
    case RoomIntentKind::Resume:
        f.require(exactKeys(object, {"kind"}));
        break;
    case RoomIntentKind::Seek:
        f.require(exactKeys(object, {"kind", "position_ms"}));
        intent.position_ms = f.integer("position_ms", 0, max_safe_integer);
        break;
    case RoomIntentKind::AddItems: {
        f.require(exactKeys(object, {"kind", "entries", "uploaded"}));
        const auto entries = f.array("entries", maximum_room_queue_items);
        if (!f.ok())
            return std::nullopt;
        for (const auto& entry : entries) {
            const auto id = safeInteger(entry);
            if (!id || *id < 0)
                return std::nullopt;
            intent.catalog_entry_ids.push_back(static_cast<std::uint64_t>(*id));
        }
        const auto uploaded = object.value(QStringLiteral("uploaded"));
        if (!uploaded.isNull()) {
            intent.uploaded = uploaded.isObject() ? decodeItem(uploaded.toObject()) : std::nullopt;
            f.require(intent.uploaded.has_value());
        }
        break;
    }
    case RoomIntentKind::AddPlaylist:
        f.require(exactKeys(object, {"kind", "playlist_id"}));
        intent.playlist_id = f.unsignedInteger("playlist_id");
        break;
    case RoomIntentKind::RemoveItem:
        f.require(exactKeys(object, {"kind", "item_id"}));
        intent.item_id = f.unsignedInteger("item_id");
        break;
    case RoomIntentKind::MoveItem:
        f.require(exactKeys(object, {"kind", "item_id", "destination"}));
        intent.item_id = f.unsignedInteger("item_id");
        intent.destination =
            static_cast<std::size_t>(f.integer("destination", 0, static_cast<std::int64_t>(maximum_room_queue_items)));
        break;
    }
    return f.ok() ? std::optional{std::move(intent)} : std::nullopt;
}

std::optional<RoomCatalog> decodeCatalog(Fields& f) {
    RoomCatalog catalog;
    const auto playlists = f.array("playlists", unbounded);
    if (!f.ok())
        return std::nullopt;
    for (const auto& value : playlists) {
        const auto object = value.toObject();
        if (!value.isObject() || !exactKeys(object, {"id", "name", "entries"}))
            return std::nullopt;
        Fields p{object};
        RoomCatalogPlaylist playlist{p.unsignedInteger("id"), p.string("name"), {}};
        const auto entries = p.array("entries", unbounded);
        if (!p.ok())
            return std::nullopt;
        for (const auto& entry_value : entries) {
            const auto entry = entry_value.toObject();
            if (!entry_value.isObject() || !exactKeys(entry, {"id", "summary"}))
                return std::nullopt;
            Fields e{entry};
            const auto id = e.unsignedInteger("id");
            const auto summary = decodeSummary(e.object("summary"));
            if (!e.ok() || !summary)
                return std::nullopt;
            playlist.entries.push_back({id, *summary});
        }
        catalog.playlists.push_back(std::move(playlist));
    }
    return catalog;
}

template <typename Message> std::optional<RoomControlMessage> finish(const Fields& f, Message message) {
    if (!f.ok())
        return std::nullopt;
    return RoomControlMessage{std::move(message)};
}

std::optional<RoomControlMessage> decodeBody(const QString& type, const QJsonObject& root) {
    Fields f{root};
    // Every root carries "v" and "t" plus exactly the fields of its type.
    const auto has = [&root](const std::initializer_list<const char*> fields) {
        return root.size() == static_cast<qsizetype>(fields.size() + 2) &&
               std::all_of(fields.begin(), fields.end(),
                           [&root](const char* key) { return root.contains(QLatin1StringView{key}); });
    };
    if (type == QLatin1StringView{"hello"}) {
        if (!has({"protocol_version", "nickname", "avatar"}))
            return std::nullopt;
        RoomHello hello{f.u32("protocol_version"), f.string("nickname"), {}};
        const auto avatar = decodeAvatar(f.object("avatar"));
        f.require(avatar.has_value());
        if (avatar)
            hello.avatar = *avatar;
        return finish(f, std::move(hello));
    }
    if (type == QLatin1StringView{"join_result"})
        return has({"decision"}) ? finish(f, RoomJoinResult{f.enumeration<JoinDecision>("decision", join_names)})
                                 : std::nullopt;
    if (type == QLatin1StringView{"state"}) {
        if (!has({"snapshot", "permissions"}))
            return std::nullopt;
        const auto permissions = f.integer("permissions", 0, (std::int64_t{1} << room_permission_count) - 1);
        auto snapshot = decodeSnapshot(f.object("snapshot"));
        f.require(snapshot.has_value());
        if (!f.ok())
            return std::nullopt;
        return RoomControlMessage{
            RoomStateMessage{std::move(*snapshot), RoomPermissionSet{static_cast<unsigned long long>(permissions)}}};
    }
    if (type == QLatin1StringView{"intent"}) {
        if (!has({"request_id", "intent"}))
            return std::nullopt;
        const auto request_id = f.unsignedInteger("request_id");
        auto intent = decodeIntent(f.object("intent"));
        f.require(intent.has_value());
        if (!f.ok())
            return std::nullopt;
        return RoomControlMessage{RoomIntentMessage{request_id, std::move(*intent)}};
    }
    if (type == QLatin1StringView{"intent_rejected"}) {
        if (!has({"request_id", "reason"}))
            return std::nullopt;
        return finish(f, RoomIntentRejected{f.unsignedInteger("request_id"),
                                            f.enumeration<IntentRejection>("reason", rejection_names)});
    }
    if (type == QLatin1StringView{"clock_ping"})
        return has({"sent_local_ms"}) ? finish(f, RoomClockPing{f.signedInteger("sent_local_ms")}) : std::nullopt;
    if (type == QLatin1StringView{"clock_pong"}) {
        if (!has({"sent_local_ms", "host_ms"}))
            return std::nullopt;
        return finish(f, RoomClockPong{f.signedInteger("sent_local_ms"), f.signedInteger("host_ms")});
    }
    if (type == QLatin1StringView{"catalog"}) {
        if (!has({"playlists"}))
            return std::nullopt;
        auto catalog = decodeCatalog(f);
        return catalog ? std::optional<RoomControlMessage>{std::move(*catalog)} : std::nullopt;
    }
    if (type == QLatin1StringView{"file_request"}) {
        if (!has({"sha256", "offset"}))
            return std::nullopt;
        return finish(f, RoomFileRequest{f.string("sha256"), f.unsignedInteger("offset")});
    }
    if (type == QLatin1StringView{"file_offer"}) {
        if (!has({"sha256", "size_bytes", "transfer_id"}))
            return std::nullopt;
        return finish(f, RoomFileOffer{f.string("sha256"), f.unsignedInteger("size_bytes"), f.u32("transfer_id")});
    }
    if (type == QLatin1StringView{"lyrics"}) {
        if (!has({"item_id", "lrc"}))
            return std::nullopt;
        return finish(f, RoomLyrics{f.unsignedInteger("item_id"), f.string("lrc")});
    }
    if (type == QLatin1StringView{"bye"})
        return has({"reason"}) ? finish(f, RoomBye{f.enumeration<RoomByeReason>("reason", bye_names)}) : std::nullopt;
    if (type == QLatin1StringView{"drift"})
        return has({"drift_ms"}) ? finish(f, RoomDriftReport{f.integer("drift_ms", -max_drift_ms, max_drift_ms)})
                                 : std::nullopt;
    return std::nullopt;
}

bool validMessage(const RoomControlMessage& message) {
    return std::visit([](const auto& alternative) { return valid(alternative); }, message);
}

void appendLittleEndian(QByteArray& out, std::uint64_t value, const int bytes) {
    for (int index = 0; index < bytes; ++index) {
        out.append(static_cast<char>(value & 0xffU));
        value >>= 8U;
    }
}

std::uint64_t readLittleEndian(const QByteArrayView bytes, const qsizetype offset, const int count) {
    std::uint64_t value = 0;
    for (int index = count - 1; index >= 0; --index)
        value = (value << 8U) | static_cast<unsigned char>(bytes[offset + index]);
    return value;
}
} // namespace

QByteArray encodeRoomControl(const RoomControlMessage& message) {
    if (!validMessage(message))
        return {};
    auto [type, root] = std::visit([](const auto& alternative) { return body(alternative); }, message);
    root.insert(QStringLiteral("v"), static_cast<qint64>(room_protocol_version));
    root.insert(QStringLiteral("t"), QString::fromLatin1(type));
    auto encoded = QJsonDocument{root}.toJson(QJsonDocument::Compact);
    // The peer checks the size and the strict lexical rules (e.g. no string starting with a BOM) too.
    if (encoded.size() > static_cast<qsizetype>(maximum_room_control_message_bytes) ||
        !detail::strictJsonPreflight(encoded, max_json_depth))
        return {};
    return encoded;
}

std::optional<RoomControlMessage> decodeRoomControl(const QByteArrayView bytes) {
    if (bytes.size() > static_cast<qsizetype>(maximum_room_control_message_bytes))
        return std::nullopt;
    const auto payload = bytes.toByteArray();
    if (!detail::strictJsonPreflight(payload, max_json_depth))
        return std::nullopt;
    QJsonParseError error{};
    const auto document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    const auto root = document.object();
    const auto version = safeInteger(root.value(QStringLiteral("v")));
    const auto type = root.value(QStringLiteral("t"));
    if (!version || *version != static_cast<std::int64_t>(room_protocol_version) || !type.isString())
        return std::nullopt;
    auto message = decodeBody(type.toString(), root);
    if (!message || !validMessage(*message))
        return std::nullopt;
    return message;
}

QByteArray encodeFileChunk(const RoomFileChunk& chunk) {
    if (chunk.payload.size() > room_file_chunk_bytes)
        return {};
    QByteArray out;
    out.reserve(chunk_header_bytes + static_cast<qsizetype>(chunk.payload.size()));
    appendLittleEndian(out, chunk.transfer_id, 4);
    appendLittleEndian(out, chunk.offset, 8);
    out.append(reinterpret_cast<const char*>(chunk.payload.data()), static_cast<qsizetype>(chunk.payload.size()));
    return out;
}

std::optional<RoomFileChunk> decodeFileChunk(const QByteArrayView bytes) {
    if (bytes.size() < chunk_header_bytes ||
        bytes.size() - chunk_header_bytes > static_cast<qsizetype>(room_file_chunk_bytes))
        return std::nullopt;
    RoomFileChunk chunk;
    chunk.transfer_id = static_cast<std::uint32_t>(readLittleEndian(bytes, 0, 4));
    chunk.offset = readLittleEndian(bytes, 4, 8);
    const auto* first = reinterpret_cast<const std::uint8_t*>(bytes.data()) + chunk_header_bytes;
    chunk.payload.assign(first, first + (bytes.size() - chunk_header_bytes));
    return chunk;
}

} // namespace music_player::m0
