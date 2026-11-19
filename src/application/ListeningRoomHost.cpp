#include "m0/application/ListeningRoomHost.hpp"

#include "m0/domain/MetadataSanitization.hpp"
#include "m0/domain/RoomClock.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace music_player::m0 {
namespace {

std::string lowerHex(const std::span<const std::uint8_t> bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        text.push_back(digits[byte >> 4]);
        text.push_back(digits[byte & 0x0F]);
    }
    return text;
}

bool lowerHex64(const std::string_view text) {
    return text.size() == 64 && std::all_of(text.begin(), text.end(), [](const char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

bool sameContent(const RoomSnapshot& a, const RoomSnapshot& b) {
    return a.members == b.members && a.queue == b.queue && a.playback == b.playback;
}

// What guests see: pending people stay visible to the host only.
RoomSnapshot guestView(RoomSnapshot snapshot) {
    std::erase_if(snapshot.members, [](const RoomMember& m) { return m.state != MemberState::Admitted; });
    return snapshot;
}

// Sanitized, then cut at a code point boundary to maximum_room_summary_text_bytes; empty when the
// result is not stable under sanitizeMetadataText (the peer's codec would refuse it).
std::string summaryText(const std::string_view raw) {
    auto sanitized = sanitizeMetadataText(raw);
    if (sanitized.disposition == MetadataTextDisposition::InvalidEncoding)
        return {};
    std::string text = std::move(sanitized.text);
    if (text.size() > maximum_room_summary_text_bytes) {
        std::size_t cut = maximum_room_summary_text_bytes;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
            --cut;
        text.resize(cut);
        auto again = sanitizeMetadataText(text);
        if (again.disposition == MetadataTextDisposition::InvalidEncoding)
            return {};
        text = std::move(again.text);
    }
    if (text.empty() || text.size() > maximum_room_summary_text_bytes ||
        sanitizeMetadataText(text).disposition != MetadataTextDisposition::Unchanged)
        return {};
    return text;
}

RoomTrackSummary cappedSummary(const RoomTrackSummary& summary) {
    return {summaryText(summary.title), summaryText(summary.artist), summaryText(summary.album),
            std::max<std::int64_t>(summary.duration_ms, 0)};
}

// Worst JSON escape per byte: quote/backslash 2, control 6 (a 6-byte escape), non-ASCII 3 (a 6-byte escape of
// a 2-byte sequence, or a surrogate pair of a 4-byte one), plus the quotes.
std::size_t jsonTextBytes(const std::string_view text) {
    std::size_t bytes = 2;
    for (const char raw : text) {
        const auto c = static_cast<unsigned char>(raw);
        bytes += c == '"' || c == '\\' ? 2 : c < 0x20 ? 6 : c >= 0x80 ? 3 : 1;
    }
    return bytes;
}
constexpr std::size_t json_number_bytes = 24;       // up to 2^53 with sign, plus separators
constexpr std::size_t json_state_frame_bytes = 512; // type, keys, version, playback and permissions
constexpr std::size_t json_item_keys_bytes = 192;
constexpr std::size_t json_member_keys_bytes = 128;

std::size_t estimatedItemBytes(const RoomItem& item) {
    return json_item_keys_bytes + 3 * json_number_bytes + jsonTextBytes("youtube_video") +
           jsonTextBytes(item.owner_fingerprint) + jsonTextBytes(item.content_sha256) + jsonTextBytes(item.youtube_id) +
           jsonTextBytes(item.added_by) + jsonTextBytes(item.summary.title) + jsonTextBytes(item.summary.artist) +
           jsonTextBytes(item.summary.album);
}

std::size_t estimatedMemberBytes(const RoomMember& member) {
    return json_member_keys_bytes + json_number_bytes + jsonTextBytes(member.fingerprint) +
           jsonTextBytes(member.nickname) + jsonTextBytes(member.avatar.custom_sha256);
}

constexpr std::size_t json_catalog_frame_bytes = 128; // type and the playlists array
constexpr std::size_t json_playlist_keys_bytes = 96;
constexpr std::size_t json_entry_keys_bytes = 128; // id and the summary keys
// Catalog files the host asks the track catalog for at a time (each one is hashed).
constexpr std::size_t catalog_describing_limit = 8;
// S12.1: upload chunks kept per link for offers still on their way, like the guest's early chunks.
constexpr std::size_t maximum_early_upload_bytes = 2 * 1024 * 1024;

std::size_t estimatedPlaylistBytes(const RoomCatalogPlaylist& playlist) {
    return json_playlist_keys_bytes + json_number_bytes + jsonTextBytes(playlist.name);
}

std::size_t estimatedEntryBytes(const RoomCatalogEntry& entry) {
    return json_entry_keys_bytes + 2 * json_number_bytes + jsonTextBytes(entry.summary.title) +
           jsonTextBytes(entry.summary.artist) + jsonTextBytes(entry.summary.album);
}

} // namespace

std::size_t estimatedRoomStateBytes(const RoomStateMessage& message) {
    std::size_t bytes = json_state_frame_bytes;
    for (const auto& member : message.snapshot.members)
        bytes += estimatedMemberBytes(member);
    for (const auto& item : message.snapshot.queue)
        bytes += estimatedItemBytes(item);
    return bytes;
}

std::size_t estimatedRoomCatalogBytes(const RoomCatalog& catalog) {
    std::size_t bytes = json_catalog_frame_bytes;
    for (const auto& playlist : catalog.playlists) {
        bytes += estimatedPlaylistBytes(playlist);
        for (const auto& entry : playlist.entries)
            bytes += estimatedEntryBytes(entry);
    }
    return bytes;
}

// Callbacks handed to links, the endpoint, the player and the catalog hold a weak reference to this
// token, so a callback that arrives after the host is gone does nothing.
struct ListeningRoomHost::Alive final {
    ListeningRoomHost* host{};
};

struct ListeningRoomHost::Connection final {
    std::shared_ptr<RoomLink> link;
    std::string fingerprint;
    bool greeted{false};
    // The item whose lyrics this member already received (or that has none).
    std::optional<std::uint64_t> lyrics_item{};
    // File requests waiting for the next serveFiles() (sha -> offset), and transfers handed to the
    // outbox (transfer_id -> sha).
    std::map<std::string, std::uint64_t, std::less<>> wanted{};
    std::map<std::uint32_t, std::string> transfers{};
    // S12.1: chunks of this member's upload that came before their offer (its transfer id -> chunks), their
    // size, and the highest upload offer seen on this link.
    std::map<std::uint32_t, std::vector<RoomFileChunk>> early_upload{};
    std::size_t early_upload_bytes{0};
    std::uint32_t last_upload_offer{0};
    // Counts this member's upload offers and chunks: the stall watch of its uploads compares it.
    std::uint64_t upload_activity{0};
};

struct ListeningRoomHost::WindowFile final {
    std::string sha256;
    std::filesystem::path file;
};

ListeningRoomHost::ListeningRoomHost(ListeningRoomHostDependencies dependencies)
    : roster_store_(dependencies.roster_store), player_(dependencies.player), endpoint_(dependencies.endpoint),
      catalog_(dependencies.catalog), host_clock_ms_(std::move(dependencies.host_clock_ms)),
      random_bytes_(std::move(dependencies.random_bytes)), host_fingerprint_(dependencies.host_fingerprint),
      host_fingerprint_hex_(lowerHex(dependencies.host_fingerprint)),
      host_profile_(std::move(dependencies.host_profile)), schedule_(std::move(dependencies.schedule)),
      files_(dependencies.files), uploads_(dependencies.uploads), upload_probe_(dependencies.upload_probe),
      alive_(std::make_shared<Alive>(Alive{this})) {
    if (!host_clock_ms_ || !random_bytes_ || !schedule_)
        throw std::invalid_argument{"the listening room host needs a clock, a random source and a scheduler"};
    const auto loaded = roster_store_.load();
    // Invalid, FutureVersion and Unavailable: run with the default roster and never overwrite the file.
    roster_writable_ = loaded.status == RoomRosterLoadStatus::Missing ||
                       loaded.status == RoomRosterLoadStatus::Loaded ||
                       loaded.status == RoomRosterLoadStatus::RecoveredFromBackup;
    if (!roster_.restore(loaded.snapshot))
        roster_writable_ = false;
    player_.onChanged = [alive = std::weak_ptr<Alive>{alive_}] {
        if (const auto self = alive.lock())
            static_cast<void>(self->host->refresh());
    };
    if (uploads_ != nullptr) {
        const std::weak_ptr<Alive> alive = alive_;
        uploads_->onComplete = [alive](std::string sha256, std::filesystem::path file) {
            if (const auto self = alive.lock())
                self->host->onUploadComplete(sha256, file);
        };
        uploads_->onFailed = [alive](std::string sha256, const std::uint32_t transfer_id,
                                     const RoomFileFailure failure) {
            if (const auto self = alive.lock())
                self->host->onUploadFailed(sha256, transfer_id, failure);
        };
    }
}

ListeningRoomHost::~ListeningRoomHost() {
    alive_.reset();
    player_.onChanged = nullptr;
    auto connections = std::move(connections_);
    connections_.clear();
    for (auto& connection : connections) {
        connection.link->onControl = nullptr;
        connection.link->onMedia = nullptr;
        connection.link->onClosed = nullptr;
        forgetFiles(*connection.link);
        connection.link->close();
    }
    if (invite_)
        endpoint_.closeAll();
    // As in end(): guest items never outlive the room in the host's queue, nor their files the upload cache
    // (end() already emptied it when the room is closed).
    for (const auto& guest : guest_items_)
        static_cast<void>(player_.remove(LocalQueueOccurrenceId{guest.first}));
    if (room_)
        stopUploads();
}

std::optional<RoomInviteLink> ListeningRoomHost::open(const RoomOpenOptions& options) {
    if (invite_)
        return invite_;
    std::size_t max_listeners =
        std::clamp<std::size_t>(options.max_listeners, minimum_room_listeners, maximum_room_listeners);
    std::uint64_t default_role_id = options.default_role_id;
    RoomInviteLink invite{};
    invite.host_fingerprint = host_fingerprint_;
    auto roster = roster_.snapshot();
    if (options.until_revoked && roster.invite) {
        // Reopening a persisted invite keeps its own settings, not this call's.
        invite.room_id = roster.invite->room_id;
        invite.secret = roster.invite->secret;
        max_listeners = roster.invite->max_listeners;
        default_role_id = roster.invite->default_role_id;
    } else {
        random_bytes_(invite.room_id);
        random_bytes_(invite.secret);
        if (options.until_revoked) {
            roster.invite = PersistedInvite{invite.room_id, invite.secret, max_listeners, default_role_id};
            if (roster_.restore(std::move(roster)))
                saveRoster();
        }
    }

    // An initial role that no longer exists (removed since it was chosen) becomes the first role.
    const auto roles = roster_.snapshot().roles;
    if (!roles.empty() &&
        std::none_of(roles.begin(), roles.end(), [&](const RoomRole& r) { return r.id == default_role_id; }))
        default_role_id = roles.front().id;
    room_.emplace(max_listeners, default_role_id, roster_);
    uses_persisted_invite_ = options.until_revoked;
    invite_ = invite;
    const auto generation = ++generation_;
    const std::weak_ptr<Alive> alive = alive_;
    endpoint_.open(
        invite,
        [alive, generation](std::shared_ptr<RoomLink> link) {
            if (const auto self = alive.lock())
                self->host->onIncoming(generation, link);
            else if (link)
                link->close();
        },
        [alive, generation](const RoomLinkFailure failure) {
            if (const auto self = alive.lock())
                self->host->onEndpointFailure(generation, failure);
        });
    static_cast<void>(refresh());
    refreshCatalog();
    return invite;
}

void ListeningRoomHost::close() { end(RoomByeReason::Closed); }

void ListeningRoomHost::revoke() {
    end(RoomByeReason::Revoked);
    auto roster = roster_.snapshot();
    if (!roster.invite)
        return;
    roster.invite.reset();
    if (roster_.restore(std::move(roster)))
        saveRoster();
}

bool ListeningRoomHost::approve(const std::string_view fingerprint) {
    if (!room_ || !room_->approve(fingerprint))
        return false;
    saveRoster();
    if (auto* connection = findMember(fingerprint)) {
        const auto link = connection->link;
        static_cast<void>(link->sendControl(RoomJoinResult{JoinDecision::Admitted}));
        admitted(link);
    } else {
        static_cast<void>(refresh());
    }
    return true;
}

bool ListeningRoomHost::reject(const std::string_view fingerprint) {
    if (!room_ || !room_->reject(fingerprint))
        return false;
    held_.erase(std::string{fingerprint});
    if (auto* connection = findMember(fingerprint)) {
        const auto link = connection->link;
        static_cast<void>(link->sendControl(RoomJoinResult{JoinDecision::RejectedClosed}));
        drop(link);
    }
    static_cast<void>(refresh());
    return true;
}

bool ListeningRoomHost::kick(const std::string_view fingerprint, const bool block) {
    if (!room_ || !room_->kick(fingerprint, block))
        return false;
    const std::string key{fingerprint};
    const bool was_held = held_.erase(key) > 0;
    // Blocked, or forgotten so that coming back needs a new approval.
    saveRoster();
    if (auto* connection = findMember(key)) {
        const auto link = connection->link;
        static_cast<void>(link->sendControl(RoomBye{RoomByeReason::Kicked}));
        drop(link);
    } else if (was_held) {
        // Kicked while the link was down: the app retrying inside the reconnect window hears it.
        const auto token = ++next_hold_;
        kicked_away_[key] = token;
        schedule_(room_reconnect_window_ms,
                  [alive = std::weak_ptr<Alive>{alive_}, generation = generation_, key, token] {
                      const auto self = alive.lock();
                      if (!self || self->host->generation_ != generation)
                          return;
                      auto& kicked = self->host->kicked_away_;
                      if (const auto found = kicked.find(key); found != kicked.end() && found->second == token)
                          kicked.erase(found);
                  });
    }
    static_cast<void>(refresh());
    return true;
}

RoomSnapshot ListeningRoomHost::snapshot() const { return current_; }

bool ListeningRoomHost::isOpen() const noexcept { return invite_.has_value(); }

std::optional<PersistedInvite> ListeningRoomHost::persistedInvite() const { return roster_.snapshot().invite; }

RoomRosterSnapshot ListeningRoomHost::roster() const { return roster_.snapshot(); }

bool ListeningRoomHost::rosterUnsaved() const noexcept { return roster_unsaved_; }

std::uint64_t ListeningRoomHost::initialRoleId() const {
    if (room_)
        return room_->defaultRoleId();
    const auto roster = roster_.snapshot();
    if (roster.invite)
        return roster.invite->default_role_id;
    return roster.roles.empty() ? 0 : roster.roles.front().id;
}

bool ListeningRoomHost::setInitialRole(const std::uint64_t role_id) {
    auto roster = roster_.snapshot();
    if (std::none_of(roster.roles.begin(), roster.roles.end(), [&](const RoomRole& r) { return r.id == role_id; }))
        return false;
    if (room_)
        room_->setDefaultRole(role_id);
    if (roster.invite && (!room_ || uses_persisted_invite_) && roster.invite->default_role_id != role_id) {
        roster.invite->default_role_id = role_id;
        if (roster_.restore(std::move(roster)))
            saveRoster();
    }
    return true;
}

std::optional<std::uint64_t> ListeningRoomHost::createRole(std::string name, const RoomPermissionSet permissions) {
    const auto id = roster_.createRole(std::move(name), permissions);
    if (id)
        rosterChanged();
    return id;
}

bool ListeningRoomHost::renameRole(const std::uint64_t role_id, std::string name) {
    if (!roster_.renameRole(role_id, std::move(name)))
        return false;
    rosterChanged();
    return true;
}

bool ListeningRoomHost::setRolePermissions(const std::uint64_t role_id, const RoomPermissionSet permissions) {
    if (!roster_.setRolePermissions(role_id, permissions))
        return false;
    rosterChanged();
    return true;
}

RoleRemoval ListeningRoomHost::removeRole(const std::uint64_t role_id) {
    const auto roster = roster_.snapshot();
    if (role_id == initialRoleId() || (roster.invite && roster.invite->default_role_id == role_id))
        return RoleRemoval::InitialRole;
    if (!roster_.removeRole(role_id, initialRoleId()))
        return RoleRemoval::Refused;
    rosterChanged();
    return RoleRemoval::Removed;
}

bool ListeningRoomHost::assignRole(const std::string_view fingerprint, const std::uint64_t role_id) {
    if (!roster_.assignRole(fingerprint, role_id))
        return false;
    rosterChanged();
    return true;
}

bool ListeningRoomHost::setOverride(const std::string_view fingerprint, const RoomPermission permission,
                                    const std::optional<bool> allowed) {
    if (!roster_.setOverride(fingerprint, permission, allowed))
        return false;
    rosterChanged();
    return true;
}

bool ListeningRoomHost::unblock(const std::string_view fingerprint) {
    const auto* known = roster_.find(fingerprint);
    if (known == nullptr || !known->blocked || !roster_.setBlocked(fingerprint, false))
        return false;
    saveRoster();
    return true;
}

std::optional<std::uint64_t> ListeningRoomHost::addGuestTrack(const std::string_view owner_fingerprint,
                                                              const std::filesystem::path& cached_file,
                                                              RoomItem described) {
    if (!room_ || memberState(owner_fingerprint) != MemberState::Admitted ||
        current_.queue.size() >= maximum_room_queue_items || !lowerHex64(described.content_sha256))
        return std::nullopt;
    const auto members = room_->members();
    const auto owner = std::find_if(members.begin(), members.end(),
                                    [&](const RoomMember& m) { return m.fingerprint == owner_fingerprint; });
    // The player reports the new occurrence before it is known to be a guest item: hold the refresh so
    // the cached file is never handed to the catalog as a host track.
    const bool was_refreshing = refreshing_;
    refreshing_ = true;
    const auto occurrence = player_.enqueue(cached_file);
    refreshing_ = was_refreshing;
    if (!occurrence)
        return std::nullopt;
    described.id = occurrence->value;
    described.origin = RoomItemOrigin::GuestTrack;
    described.owner_fingerprint = std::string{owner_fingerprint};
    described.added_by = owner->nickname;
    described.youtube_id.clear();
    described.summary = cappedSummary(described.summary);
    guest_items_[described.id] = described;
    guest_files_[described.id] = cached_file;
    static_cast<void>(refresh());
    return described.id;
}

SessionSnapshot ListeningRoomHost::withoutGuestItems(SessionSnapshot session) const {
    if (guest_items_.empty())
        return session;
    const auto is_guest = [this](const LocalQueueOccurrenceId id) { return guest_items_.contains(id.value); };
    std::erase_if(session.queue.items,
                  [&](const LocalFileQueueSnapshotItem& item) { return is_guest(item.occurrence_id); });
    if (session.queue.current_occurrence_id && is_guest(*session.queue.current_occurrence_id)) {
        session.queue.current_occurrence_id.reset();
        session.position_ms = 0;
    }
    return session;
}

void ListeningRoomHost::attach(const std::shared_ptr<RoomLink>& link) {
    const std::weak_ptr<Alive> alive = alive_;
    const std::weak_ptr<RoomLink> weak = link;
    // The strong reference taken here keeps the link (and so this callback) alive while it runs.
    link->onControl = [alive, weak](RoomControlMessage message) {
        const auto self = alive.lock();
        const auto strong = weak.lock();
        if (self && strong)
            self->host->onLinkControl(strong, std::move(message));
    };
    // Guests send media only for their uploads (S12.1).
    link->onMedia = [alive, weak](RoomFileChunk chunk) {
        const auto self = alive.lock();
        const auto strong = weak.lock();
        if (self && strong)
            self->host->onUploadChunk(strong.get(), std::move(chunk));
    };
    link->onClosed = [alive, weak](RoomLinkFailure) {
        const auto self = alive.lock();
        const auto strong = weak.lock();
        if (self && strong)
            self->host->onLinkClosed(strong);
    };
}

void ListeningRoomHost::onIncoming(const std::uint64_t generation, const std::shared_ptr<RoomLink>& link) {
    if (!link)
        return;
    const auto fingerprint = link->peerFingerprint();
    if (generation != generation_ || !room_ || fingerprint.empty() || fingerprint == host_fingerprint_hex_) {
        link->close();
        return;
    }
    connections_.push_back(Connection{link, fingerprint, false});
    attach(link);
    schedule_(room_hello_timeout_ms,
              [alive = std::weak_ptr<Alive>{alive_}, weak = std::weak_ptr<RoomLink>{link}, generation] {
                  const auto self = alive.lock();
                  const auto strong = weak.lock();
                  if (self && strong)
                      self->host->onHelloTimeout(generation, strong);
              });
}

void ListeningRoomHost::onHelloTimeout(const std::uint64_t generation, const std::shared_ptr<RoomLink>& link) {
    if (generation != generation_)
        return;
    if (const auto* connection = find(link.get()); connection != nullptr && !connection->greeted)
        drop(link);
}

void ListeningRoomHost::onReconnectExpired(const std::uint64_t generation, const std::string& fingerprint,
                                           const std::uint64_t token) {
    if (generation != generation_ || !room_)
        return;
    const auto held = held_.find(fingerprint);
    if (held == held_.end() || held->second != token)
        return;
    held_.erase(held);
    if (findMember(fingerprint) != nullptr)
        return;
    room_->leave(fingerprint);
    static_cast<void>(refresh());
}

void ListeningRoomHost::onEndpointFailure(const std::uint64_t generation, const RoomLinkFailure failure) {
    if (generation == generation_ && room_ && onEndpointFailed)
        onEndpointFailed(failure);
}

void ListeningRoomHost::onLinkControl(const std::shared_ptr<RoomLink>& link, RoomControlMessage message) {
    const auto* connection = find(link.get());
    if (connection == nullptr || !room_)
        return;
    if (!connection->greeted) {
        if (const auto* hello = std::get_if<RoomHello>(&message))
            onHello(link, *hello);
        else
            drop(link);
        return;
    }
    if (const auto* intent = std::get_if<RoomIntentMessage>(&message)) {
        onIntent(link, *intent);
    } else if (const auto* ping = std::get_if<RoomClockPing>(&message)) {
        if (memberState(connection->fingerprint) == MemberState::Admitted)
            static_cast<void>(link->sendControl(RoomClockPong{ping->sent_local_ms, host_clock_ms_()}));
    } else if (const auto* report = std::get_if<RoomDriftReport>(&message)) {
        if (memberState(connection->fingerprint) == MemberState::Admitted &&
            report->drift_ms >= -room_drift_report_limit_ms && report->drift_ms <= room_drift_report_limit_ms) {
            const auto [entry, inserted] = drifts_.try_emplace(connection->fingerprint, report->drift_ms);
            if (inserted || entry->second != report->drift_ms) {
                entry->second = report->drift_ms;
                if (onDriftsChanged)
                    onDriftsChanged();
            }
        }
    } else if (const auto* request = std::get_if<RoomFileRequest>(&message)) {
        if (memberState(connection->fingerprint) == MemberState::Admitted)
            onFileRequest(*find(link.get()), *request);
    } else if (const auto* offer = std::get_if<RoomFileOffer>(&message)) {
        if (memberState(connection->fingerprint) == MemberState::Admitted)
            onUploadOffer(*find(link.get()), *offer);
    } else if (std::holds_alternative<RoomBye>(message)) {
        // The guest left ("Sair da sala"): no reconnect hold, the seat is free at once.
        const auto fingerprint = connection->fingerprint;
        held_.erase(fingerprint);
        drop(link);
        room_->leave(fingerprint);
        static_cast<void>(refresh());
    }
    // A second hello is ignored here.
}

void ListeningRoomHost::onLinkClosed(const std::shared_ptr<RoomLink>& link) {
    const auto* connection = find(link.get());
    if (connection == nullptr)
        return;
    const auto fingerprint = connection->fingerprint;
    const bool greeted = connection->greeted;
    std::erase_if(connections_, [&](const Connection& c) { return c.link == link; });
    forgetFiles(*link);
    forgetDrift(fingerprint);
    stopUploadsOf(fingerprint);
    if (!greeted || !room_)
        return;
    if (memberState(fingerprint) == MemberState::Admitted) {
        // Keep the seat and the version for room_reconnect_window_ms: a new link with the same
        // fingerprint resumes the membership.
        const auto token = ++next_hold_;
        held_[fingerprint] = token;
        schedule_(room_reconnect_window_ms,
                  [alive = std::weak_ptr<Alive>{alive_}, generation = generation_, fingerprint, token] {
                      if (const auto self = alive.lock())
                          self->host->onReconnectExpired(generation, fingerprint, token);
                  });
        return;
    }
    room_->leave(fingerprint);
    static_cast<void>(refresh());
}

void ListeningRoomHost::onHello(const std::shared_ptr<RoomLink>& link, const RoomHello& hello) {
    if (hello.protocol_version != room_protocol_version) {
        static_cast<void>(link->sendControl(RoomBye{RoomByeReason::VersionMismatch}));
        drop(link);
        return;
    }
    const auto nickname = canonicalNickname(hello.nickname);
    if (!nickname || *nickname != hello.nickname || !isValidAvatarChoice(hello.avatar)) {
        drop(link);
        return;
    }
    auto* connection = find(link.get());
    const auto fingerprint = connection->fingerprint;
    if (const auto kicked = kicked_away_.find(fingerprint); kicked != kicked_away_.end()) {
        kicked_away_.erase(kicked);
        static_cast<void>(link->sendControl(RoomBye{RoomByeReason::Kicked}));
        drop(link);
        return;
    }
    // One live link per identity: the newer link replaces the older one.
    std::vector<std::shared_ptr<RoomLink>> older;
    for (const auto& other : connections_)
        if (other.link != link && other.fingerprint == fingerprint)
            older.push_back(other.link);
    for (const auto& old : older)
        drop(old);
    connection = find(link.get());
    connection->greeted = true;

    RoomMember candidate{fingerprint, hello.nickname, hello.avatar, MemberState::Pending};
    const auto decision = room_->requestJoin(candidate);
    switch (decision) {
    case JoinDecision::Admitted:
        held_.erase(fingerprint);
        static_cast<void>(link->sendControl(RoomJoinResult{decision}));
        admitted(link);
        return;
    case JoinDecision::Pending: {
        static_cast<void>(link->sendControl(RoomJoinResult{decision}));
        static_cast<void>(refresh());
        if (onJoinRequest) {
            const auto members = room_ ? room_->members() : std::vector<RoomMember>{};
            const auto member = std::find_if(members.begin(), members.end(),
                                             [&](const RoomMember& m) { return m.fingerprint == fingerprint; });
            if (member != members.end())
                onJoinRequest(*member);
        }
        return;
    }
    case JoinDecision::RejectedFull:
    case JoinDecision::RejectedBlocked:
    case JoinDecision::RejectedClosed:
        static_cast<void>(link->sendControl(RoomJoinResult{decision}));
        drop(link);
        return;
    }
}

void ListeningRoomHost::onIntent(const std::shared_ptr<RoomLink>& link, const RoomIntentMessage& message) {
    const auto fingerprint = find(link.get())->fingerprint;
    auto rejection = room_->authorize(fingerprint, message.intent, current_);
    if (rejection == IntentRejection::NotAdmitted)
        return; // Pending people get nothing but their JoinResult.
    if (rejection) {
        // Refused before reaching the player: nothing changed, so no refresh and no broadcast.
        static_cast<void>(link->sendControl(RoomIntentRejected{message.request_id, *rejection}));
        return;
    }
    // S12.1: an upload changes nothing until its file arrives and passes the checks.
    if (message.intent.kind == RoomIntentKind::AddItems && message.intent.uploaded) {
        if (const auto refused = startUpload(*message.intent.uploaded, fingerprint, message.request_id))
            static_cast<void>(link->sendControl(RoomIntentRejected{message.request_id, *refused}));
        return;
    }
    rejection = apply(message.intent, fingerprint);
    if (rejection)
        static_cast<void>(link->sendControl(RoomIntentRejected{message.request_id, *rejection}));
    static_cast<void>(refresh());
}

std::optional<IntentRejection> ListeningRoomHost::apply(const RoomIntent& intent, const std::string_view fingerprint) {
    const auto accepted = [](const bool ok) {
        return ok ? std::nullopt : std::optional<IntentRejection>{IntentRejection::InvalidArgument};
    };
    switch (intent.kind) {
    case RoomIntentKind::Next:
        return accepted(player_.next());
    case RoomIntentKind::Previous:
        return accepted(player_.previous());
    case RoomIntentKind::Seek:
        reanchor_ = true; // A seek is a discrete event even when it moves less than the tolerance.
        return accepted(player_.seek(intent.position_ms));
    case RoomIntentKind::Pause:
        return player_.state().status == PlaybackStatus::Playing ? accepted(player_.pause()) : std::nullopt;
    case RoomIntentKind::Resume:
        return player_.state().status == PlaybackStatus::Playing ? std::nullopt : accepted(player_.play());
    case RoomIntentKind::RemoveItem:
        return player_.remove(LocalQueueOccurrenceId{intent.item_id})
                   ? std::nullopt
                   : std::optional<IntentRejection>{IntentRejection::UnknownItem};
    case RoomIntentKind::MoveItem: {
        // destination is a room-queue index (checked by authorize): move to where that item is now.
        const auto target = current_.queue[intent.destination].id;
        const auto items = player_.state().queue.items;
        const auto local = std::find_if(items.begin(), items.end(), [&](const LocalFileQueueSnapshotItem& item) {
            return item.occurrence_id.value == target;
        });
        if (local == items.end())
            return IntentRejection::UnknownItem;
        return accepted(
            player_.move(LocalQueueOccurrenceId{intent.item_id}, static_cast<std::size_t>(local - items.begin())));
    }
    case RoomIntentKind::AddItems: {
        // Uploads go through startUpload (onIntent). authorize already checked the room left in the queue.
        if (intent.uploaded)
            return IntentRejection::InvalidArgument;
        std::vector<std::filesystem::path> sources;
        for (const auto id : intent.catalog_entry_ids) {
            const auto found = catalog_sources_.find(id);
            if (found == catalog_sources_.end())
                return IntentRejection::UnknownItem;
            sources.push_back(found->second);
        }
        return enqueueAll(sources, fingerprint);
    }
    case RoomIntentKind::AddPlaylist: {
        const auto& playlists = room_catalog_.playlists;
        const auto playlist = std::find_if(playlists.begin(), playlists.end(),
                                           [&](const RoomCatalogPlaylist& p) { return p.id == intent.playlist_id; });
        if (playlist == playlists.end())
            return IntentRejection::UnknownItem;
        if (playlist->entries.empty())
            return IntentRejection::InvalidArgument;
        if (current_.queue.size() + playlist->entries.size() > maximum_room_queue_items)
            return IntentRejection::QueueFull;
        std::vector<std::filesystem::path> sources;
        for (const auto& entry : playlist->entries)
            sources.push_back(catalog_sources_.at(entry.id));
        return enqueueAll(sources, fingerprint);
    }
    }
    return IntentRejection::InvalidArgument;
}

// Appends host tracks for the room, all or none, added by that member; the caller refreshes once afterwards.
std::optional<IntentRejection> ListeningRoomHost::enqueueAll(const std::vector<std::filesystem::path>& sources,
                                                             const std::string_view fingerprint) {
    const auto members = room_->members();
    const auto adder = std::find_if(members.begin(), members.end(),
                                    [&](const RoomMember& member) { return member.fingerprint == fingerprint; });
    if (adder == members.end())
        return IntentRejection::NotAdmitted;
    const bool was_refreshing = refreshing_;
    refreshing_ = true;
    std::vector<LocalQueueOccurrenceId> added;
    for (const auto& source : sources) {
        const auto occurrence = player_.enqueue(source);
        if (!occurrence) {
            for (auto undo = added.rbegin(); undo != added.rend(); ++undo)
                static_cast<void>(player_.remove(*undo));
            refreshing_ = was_refreshing;
            return IntentRejection::InvalidArgument;
        }
        added.push_back(*occurrence);
    }
    for (const auto occurrence : added)
        catalog_adders_[occurrence.value] = adder->nickname;
    refreshing_ = was_refreshing;
    return std::nullopt;
}

void ListeningRoomHost::setPlaylists(PlaylistLibrarySnapshot library) {
    if (library == library_)
        return;
    library_ = std::move(library);
    refreshCatalog();
}

const RoomCatalog& ListeningRoomHost::catalog() const noexcept { return room_catalog_; }

// Builds the catalog again (describe callbacks that run meanwhile ask for another pass) and sends it to
// every admitted member when it changed.
void ListeningRoomHost::refreshCatalog() {
    if (!room_)
        return;
    if (cataloging_) {
        catalog_again_ = true;
        return;
    }
    cataloging_ = true;
    RoomCatalog next;
    do {
        catalog_again_ = false;
        next = buildCatalog();
    } while (catalog_again_ && room_);
    cataloging_ = false;
    if (!room_ || next == room_catalog_)
        return;
    room_catalog_ = std::move(next);
    // Iterate over a copy: a synchronous transport may call back into the host while sending.
    std::vector<std::shared_ptr<RoomLink>> links;
    for (const auto& connection : connections_)
        links.push_back(connection.link);
    for (const auto& link : links) {
        const auto* connection = find(link.get());
        if (connection != nullptr && connection->greeted &&
            memberState(connection->fingerprint) == MemberState::Admitted)
            sendCatalog(*connection);
    }
}

RoomCatalog ListeningRoomHost::buildCatalog() {
    RoomCatalog catalog;
    std::map<std::uint64_t, std::filesystem::path> sources;
    std::map<std::uint64_t, std::uint64_t> playlist_ids;
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::uint64_t> entry_ids;
    // An id the room already gave stays; anything new takes the next one.
    const auto idFor = [this](auto& fresh, const auto& known, const auto& key) {
        const auto found = known.find(key);
        const auto id = found != known.end() ? found->second : next_catalog_id_++;
        fresh.emplace(key, id);
        return id;
    };
    std::size_t budget = estimatedRoomCatalogBytes(catalog);
    bool full = false;
    std::deque<std::filesystem::path> waiting;
    // A copy: describe callbacks may run while it is walked.
    const auto library = library_;
    for (const auto& list : library.playlists) {
        if (full)
            break;
        if (list.visibility != PlaylistVisibility::Room)
            continue;
        RoomCatalogPlaylist playlist{0, summaryText(list.name), {}};
        if (playlist.name.empty())
            continue;
        const auto frame = estimatedPlaylistBytes(playlist);
        if (budget + frame > maximum_room_control_message_bytes)
            break;
        budget += frame;
        playlist.id = idFor(playlist_ids, catalog_playlist_ids_, list.id);
        for (const auto& entry : list.entries) {
            const auto& source = entry.source_path;
            if (!descriptions_.contains(source)) {
                if (describing_.size() < catalog_describing_limit)
                    requestDescription(source);
                else if (!describing_.contains(source) && !unreadable_.contains(source))
                    waiting.push_back(source);
            }
            const auto found = descriptions_.find(source);
            if (found == descriptions_.end())
                continue;
            RoomCatalogEntry item{0, cappedSummary(found->second.item.summary)};
            const auto cost = estimatedEntryBytes(item);
            if (budget + cost > maximum_room_control_message_bytes) {
                full = true;
                break;
            }
            budget += cost;
            item.id = idFor(entry_ids, catalog_entry_ids_, std::pair{list.id, entry.id});
            sources.emplace(item.id, source);
            playlist.entries.push_back(std::move(item));
        }
        catalog.playlists.push_back(std::move(playlist));
    }
    catalog_waiting_ = std::move(waiting);
    catalog_sources_ = std::move(sources);
    catalog_playlist_ids_ = std::move(playlist_ids);
    catalog_entry_ids_ = std::move(entry_ids);
    return catalog;
}

void ListeningRoomHost::sendCatalog(const Connection& connection) const {
    // Copied first: the send may run host callbacks that move or erase connections.
    const auto link = connection.link;
    static_cast<void>(link->sendControl(room_catalog_));
}

void ListeningRoomHost::requestDescription(const std::filesystem::path& source) {
    if (descriptions_.contains(source) || describing_.contains(source) || unreadable_.contains(source))
        return;
    describing_.insert(source);
    const std::weak_ptr<Alive> alive = alive_;
    catalog_.describe(source, [alive, generation = generation_, source](std::optional<RoomTrackDescription> found) {
        if (const auto self = alive.lock())
            self->host->onDescribed(generation, source, std::move(found));
    });
}

void ListeningRoomHost::onDescribed(const std::uint64_t generation, const std::filesystem::path& source,
                                    std::optional<RoomTrackDescription> description) {
    if (generation != generation_ || !room_)
        return;
    describing_.erase(source);
    if (description && lowerHex64(description->item.content_sha256)) {
        if (description->lyrics_lrc.size() > maximum_room_lyrics_bytes)
            description->lyrics_lrc.clear();
        descriptions_[source] = std::move(*description);
    } else {
        unreadable_.insert(source);
    }
    static_cast<void>(refresh());
    if (cataloging_) {
        catalog_again_ = true; // described while the catalog is being built: that build goes once more
        return;
    }
    if (room_catalog_.playlists.empty())
        return; // no visible list: nothing for guests to see
    describeCatalogFiles();
    scheduleCatalogRefresh();
}

// Asks for the next catalog files as earlier ones are described, up to catalog_describing_limit at a time.
void ListeningRoomHost::describeCatalogFiles() {
    if (std::exchange(describing_catalog_, true))
        return;
    while (room_ && describing_.size() < catalog_describing_limit && !catalog_waiting_.empty()) {
        auto source = std::move(catalog_waiting_.front());
        catalog_waiting_.pop_front();
        requestDescription(source);
    }
    describing_catalog_ = false;
}

// Descriptions that arrive close together reach guests in one catalog: one refresh per
// room_catalog_refresh_ms at most.
void ListeningRoomHost::scheduleCatalogRefresh() {
    if (!room_ || std::exchange(catalog_refresh_scheduled_, true))
        return;
    schedule_(room_catalog_refresh_ms, [alive = std::weak_ptr<Alive>{alive_}, generation = generation_] {
        if (const auto self = alive.lock(); self && self->host->generation_ == generation) {
            self->host->catalog_refresh_scheduled_ = false;
            self->host->refreshCatalog();
        }
    });
}

std::optional<RoomItem> ListeningRoomHost::roomItem(const LocalFileQueueSnapshotItem& entry) const {
    if (const auto guest = guest_items_.find(entry.occurrence_id.value); guest != guest_items_.end())
        return guest->second;
    const auto found = descriptions_.find(entry.source_path);
    if (found == descriptions_.end())
        return std::nullopt;
    RoomItem item = found->second.item;
    item.id = entry.occurrence_id.value;
    item.origin = RoomItemOrigin::HostTrack;
    item.owner_fingerprint = host_fingerprint_hex_;
    const auto adder = catalog_adders_.find(entry.occurrence_id.value);
    item.added_by = adder != catalog_adders_.end() ? adder->second : host_profile_.nickname;
    item.youtube_id.clear();
    item.summary = cappedSummary(item.summary);
    return item;
}

bool ListeningRoomHost::refresh() {
    if (!room_)
        return false;
    if (refreshing_) {
        refresh_again_ = true;
        return false;
    }
    refreshing_ = true;
    bool changed = false;
    do {
        refresh_again_ = false;
        changed = rebuild() || changed;
    } while (refresh_again_ && room_);
    refreshing_ = false;
    return changed;
}

bool ListeningRoomHost::rebuild() {
    const auto player = player_.state();
    const auto& items = player.queue.items;
    std::vector<std::string> left;
    std::erase_if(guest_items_, [&](const auto& guest) {
        const bool gone = std::none_of(items.begin(), items.end(), [&](const LocalFileQueueSnapshotItem& item) {
            return item.occurrence_id.value == guest.first;
        });
        if (gone)
            left.push_back(guest.second.content_sha256);
        return gone;
    });
    std::erase_if(guest_files_, [&](const auto& guest) { return !guest_items_.contains(guest.first); });
    freeUploadFiles(left);
    std::erase_if(catalog_adders_, [&](const auto& added) {
        return std::none_of(items.begin(), items.end(), [&](const LocalFileQueueSnapshotItem& item) {
            return item.occurrence_id.value == added.first;
        });
    });
    std::optional<std::size_t> current_index;
    for (std::size_t index = 0; index < items.size(); ++index)
        if (player.queue.current_occurrence_id == std::optional{items[index].occurrence_id})
            current_index = index;
    // A guest item that was current and no longer is has played: it leaves on the next scheduler turn (not from
    // inside the player's notification).
    const auto now_current =
        player.queue.current_occurrence_id ? std::optional{player.queue.current_occurrence_id->value} : std::nullopt;
    if (const auto played = std::exchange(last_current_, now_current);
        played && played != now_current && guest_items_.contains(*played))
        schedule_(0, [alive = std::weak_ptr<Alive>{alive_}, generation = generation_, occurrence = *played] {
            if (const auto self = alive.lock())
                self->host->removePlayedGuestItem(generation, occurrence);
        });
    // The room queue runs from the current item forward (the play order, see RoomHostPlayer): at most
    // maximum_room_queue_items items and only as many as keep the guests' state within
    // maximum_room_control_message_bytes by estimatedRoomStateBytes.
    const std::size_t first = current_index.value_or(0);
    const std::size_t end = std::min(items.size(), first + maximum_room_queue_items);
    for (std::size_t index = first; index < end; ++index)
        if (!guest_items_.contains(items[index].occurrence_id.value))
            requestDescription(items[index].source_path);

    RoomSnapshot next;
    next.members.push_back(hostMember());
    for (auto& member : room_->members())
        next.members.push_back(std::move(member));
    std::size_t budget = estimatedRoomStateBytes(RoomStateMessage{guestView(next), {}});
    for (std::size_t index = first; index < end; ++index) {
        auto item = roomItem(items[index]);
        if (!item)
            continue;
        const auto cost = estimatedItemBytes(*item);
        if (budget + cost > maximum_room_control_message_bytes)
            break;
        budget += cost;
        next.queue.push_back(std::move(*item));
    }

    if (current_index && !next.queue.empty() && next.queue.front().id == items[*current_index].occurrence_id.value) {
        auto& playback = next.playback;
        playback.current_item = items[*current_index].occurrence_id.value;
        playback.playing = player.status == PlaybackStatus::Playing;
        const auto position = std::max<std::int64_t>(player.position_ms, 0);
        const auto now = host_clock_ms_();
        const auto& previous = current_.playback;
        const auto expected = expectedPositionMs(previous, now);
        const auto drift = expected > position ? expected - position : position - expected;
        // Item changes, play/pause and seeks always re-anchor; otherwise the anchor stays while the
        // player is within room_anchor_tolerance_ms of it, so position jitter neither bumps the version
        // nor broadcasts.
        const bool forced = std::exchange(reanchor_, false);
        if (!forced && previous.current_item == playback.current_item && previous.playing == playback.playing &&
            drift <= room_anchor_tolerance_ms) {
            playback.anchor_host_ms = previous.anchor_host_ms;
            playback.anchor_position_ms = previous.anchor_position_ms;
        } else {
            playback.anchor_host_ms = now;
            playback.anchor_position_ms = position;
        }
    }

    if (sameContent(next, current_))
        return false;
    next.version = current_.version + 1;
    current_ = std::move(next);
    cancelFilesOutsideWindow();
    auto view = guestView(current_);
    const bool broadcast = !sameContent(view, sent_view_);
    if (broadcast) {
        sent_view_ = std::move(view);
        // Iterate over a copy: a synchronous transport may call back into the host while sending.
        std::vector<std::shared_ptr<RoomLink>> links;
        for (const auto& connection : connections_)
            links.push_back(connection.link);
        for (const auto& link : links) {
            const auto* connection = find(link.get());
            if (connection == nullptr || !connection->greeted ||
                memberState(connection->fingerprint) != MemberState::Admitted)
                continue;
            sendState(*connection);
            if (auto* again = find(link.get()))
                sendLyrics(*again);
        }
    }
    if (onChanged)
        onChanged(current_);
    return broadcast;
}

void ListeningRoomHost::admitted(const std::shared_ptr<RoomLink>& link) {
    // Admission changes what guests see, so a broadcast normally carries the state; when nothing changed
    // (someone resuming inside the reconnect window, or a newer link) this member gets it directly.
    // Re-find the connection after every send: a synchronous transport may close other links meanwhile.
    const bool broadcast = refresh();
    if (!broadcast)
        if (const auto* connection = find(link.get()))
            sendState(*connection);
    if (auto* connection = find(link.get()))
        sendLyrics(*connection);
    if (const auto* connection = find(link.get()))
        sendCatalog(*connection);
}

void ListeningRoomHost::sendState(const Connection& connection) const {
    // Copies first: the send may run host callbacks that move or erase connections.
    const auto link = connection.link;
    const auto permissions = roster_.effectivePermissions(connection.fingerprint);
    // A refused message is skipped: the member keeps its older version.
    static_cast<void>(link->sendControl(RoomStateMessage{guestView(current_), permissions}));
}

void ListeningRoomHost::sendLyrics(Connection& connection) const {
    if (!current_.playback.current_item || connection.lyrics_item == current_.playback.current_item)
        return;
    const auto id = *current_.playback.current_item;
    connection.lyrics_item = id;
    const auto link = connection.link;
    if (guest_items_.contains(id))
        return;
    const auto items = player_.state().queue.items;
    const auto entry = std::find_if(items.begin(), items.end(), [&](const LocalFileQueueSnapshotItem& item) {
        return item.occurrence_id.value == id;
    });
    if (entry == items.end())
        return;
    const auto found = descriptions_.find(entry->source_path);
    if (found != descriptions_.end() && !found->second.lyrics_lrc.empty())
        static_cast<void>(link->sendControl(RoomLyrics{id, found->second.lyrics_lrc}));
}

const std::map<std::string, std::int64_t, std::less<>>& ListeningRoomHost::memberDrifts() const noexcept {
    return drifts_;
}

void ListeningRoomHost::forgetDrift(const std::string_view fingerprint) {
    const auto found = drifts_.find(fingerprint);
    if (found == drifts_.end())
        return;
    drifts_.erase(found);
    if (onDriftsChanged)
        onDriftsChanged();
}

void ListeningRoomHost::drop(const std::shared_ptr<RoomLink>& link) {
    std::optional<std::string> fingerprint;
    if (const auto* connection = find(link.get())) {
        fingerprint = connection->fingerprint;
        forgetDrift(connection->fingerprint);
    }
    std::erase_if(connections_, [&](const Connection& c) { return c.link == link; });
    forgetFiles(*link);
    if (fingerprint)
        stopUploadsOf(*fingerprint);
    link->close();
}

std::vector<ListeningRoomHost::WindowFile> ListeningRoomHost::fileWindow() const {
    std::vector<WindowFile> window;
    const auto items = player_.state().queue.items;
    const auto count = std::min(current_.queue.size(), std::size_t{1} + room_prefetch_items);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& item = current_.queue[index];
        if (const auto guest = guest_files_.find(item.id); guest != guest_files_.end()) {
            window.push_back({item.content_sha256, guest->second});
            continue;
        }
        if (item.origin != RoomItemOrigin::HostTrack)
            continue;
        const auto entry = std::find_if(items.begin(), items.end(), [&](const LocalFileQueueSnapshotItem& local) {
            return local.occurrence_id.value == item.id;
        });
        if (entry == items.end())
            continue;
        const auto found = descriptions_.find(entry->source_path);
        if (found != descriptions_.end() && found->second.item.content_sha256 == item.content_sha256)
            window.push_back({item.content_sha256, entry->source_path});
    }
    return window;
}

void ListeningRoomHost::onFileRequest(Connection& connection, const RoomFileRequest& request) {
    if (files_ == nullptr || !lowerHex64(request.content_sha256))
        return;
    connection.wanted[request.content_sha256] = request.offset;
    if (std::exchange(serve_scheduled_, true))
        return;
    // Requests that arrive together are served together, in window order (the current item first).
    schedule_(0, [alive = std::weak_ptr<Alive>{alive_}, generation = generation_] {
        if (const auto self = alive.lock(); self && self->host->generation_ == generation)
            self->host->serveFiles();
    });
}

void ListeningRoomHost::serveFiles() {
    serve_scheduled_ = false;
    if (files_ == nullptr || !room_)
        return;
    const auto window = fileWindow();
    std::vector<std::shared_ptr<RoomLink>> links;
    for (const auto& connection : connections_)
        if (!connection.wanted.empty())
            links.push_back(connection.link);
    for (const auto& link : links) {
        auto* connection = find(link.get());
        if (connection == nullptr)
            continue;
        const auto wanted = std::exchange(connection->wanted, {});
        if (memberState(connection->fingerprint) != MemberState::Admitted)
            continue;
        std::set<std::string, std::less<>> served;
        for (const auto& entry : window) {
            const auto request = wanted.find(entry.sha256);
            if (request == wanted.end() || !served.insert(entry.sha256).second)
                continue;
            // A new request for the same file replaces the transfer still going.
            for (auto transfer = connection->transfers.begin(); transfer != connection->transfers.end();) {
                if (transfer->second != entry.sha256) {
                    ++transfer;
                    continue;
                }
                files_->cancel(*link, transfer->first);
                transfer = connection->transfers.erase(transfer);
            }
            const auto transfer_id = ++next_transfer_id_;
            if (!files_->send(link, entry.file, entry.sha256, transfer_id, request->second))
                continue;
            // The send may run host callbacks that move or erase connections.
            if ((connection = find(link.get())) == nullptr)
                break;
            connection->transfers[transfer_id] = entry.sha256;
        }
    }
}

void ListeningRoomHost::cancelFilesOutsideWindow() {
    if (files_ == nullptr)
        return;
    std::set<std::string, std::less<>> window;
    const auto count = std::min(current_.queue.size(), std::size_t{1} + room_prefetch_items);
    for (std::size_t index = 0; index < count; ++index)
        window.insert(current_.queue[index].content_sha256);
    for (auto& connection : connections_) {
        for (auto transfer = connection.transfers.begin(); transfer != connection.transfers.end();) {
            if (window.contains(transfer->second)) {
                ++transfer;
                continue;
            }
            files_->cancel(*connection.link, transfer->first);
            transfer = connection.transfers.erase(transfer);
        }
        std::erase_if(connection.wanted, [&](const auto& request) { return !window.contains(request.first); });
    }
}

void ListeningRoomHost::forgetFiles(const RoomLink& link) {
    if (files_ != nullptr)
        files_->forget(link);
}

// S12.1: guest uploads. Only the hash and size of the declared item are used; the rest comes from the probe
// and the link.
std::optional<IntentRejection> ListeningRoomHost::startUpload(const RoomItem& item, const std::string& fingerprint,
                                                              const std::uint64_t request_id) {
    if (uploads_ == nullptr || upload_probe_ == nullptr || item.origin != RoomItemOrigin::GuestTrack ||
        !lowerHex64(item.content_sha256) || item.size_bytes == 0 || item.size_bytes > maximum_guest_upload_bytes)
        return IntentRejection::InvalidArgument;
    if (const auto found = uploading_.find(item.content_sha256); found != uploading_.end()) {
        if (found->second.owner != fingerprint)
            return IntentRejection::InvalidArgument;
        // The same guest sends it again (its own send failed, say): this upload replaces the one on its way.
        // The partial file stays for resume.
        if (found->second.receiving)
            uploads_->abandon(*found->second.receiving);
        uploading_.erase(found);
    }
    const auto token = ++next_upload_token_;
    uploading_.emplace(item.content_sha256, Upload{fingerprint, request_id, item.size_bytes, token});
    const auto* owner = findMember(fingerprint);
    watchUpload(item.content_sha256, token, owner != nullptr ? owner->upload_activity : 0);
    askUpload(item.content_sha256);
    return std::nullopt;
}

void ListeningRoomHost::watchUpload(const std::string& sha256, const std::uint64_t token,
                                    const std::uint64_t seen_activity) {
    schedule_(room_upload_stall_ms, [alive = std::weak_ptr<Alive>{alive_}, sha256, token, seen_activity] {
        if (const auto self = alive.lock())
            self->host->onUploadWatch(sha256, token, seen_activity);
    });
}

// No offer or chunk from the owner during a whole period: the upload is dropped and the file can be sent again.
void ListeningRoomHost::onUploadWatch(const std::string& sha256, const std::uint64_t token,
                                      const std::uint64_t seen_activity) {
    const auto found = uploading_.find(sha256);
    if (found == uploading_.end() || found->second.token != token || found->second.probing)
        return;
    const auto* owner = findMember(found->second.owner);
    if (owner == nullptr)
        return;
    if (owner->upload_activity != seen_activity) {
        watchUpload(sha256, token, owner->upload_activity);
        return;
    }
    refuseUpload(sha256, IntentRejection::UnknownItem);
}

void ListeningRoomHost::removePlayedGuestItem(const std::uint64_t generation, const std::uint64_t occurrence) {
    if (generation != generation_ || !room_ || !guest_items_.contains(occurrence))
        return;
    const auto current = player_.state().queue.current_occurrence_id;
    if (current && current->value == occurrence)
        return;
    static_cast<void>(player_.remove(LocalQueueOccurrenceId{occurrence}));
}

bool ListeningRoomHost::mayUpload(const std::string_view fingerprint) const {
    return roster_.effectivePermissions(fingerprint).test(static_cast<std::size_t>(RoomPermission::UploadOwnTracks));
}

// Files of guest items that left the queue go, unless another guest item or an upload in flight uses them.
void ListeningRoomHost::freeUploadFiles(const std::vector<std::string>& hashes) {
    if (uploads_ == nullptr)
        return;
    for (const auto& sha : hashes) {
        const bool used = uploading_.contains(sha) ||
                          std::any_of(guest_items_.begin(), guest_items_.end(),
                                      [&](const auto& guest) { return guest.second.content_sha256 == sha; });
        if (!used)
            uploads_->discard(sha);
    }
}

// Asks the upload cache where the file stands; the answer leads to a request to the owner (or to the probe).
void ListeningRoomHost::askUpload(const std::string& sha256) {
    const auto found = uploading_.find(sha256);
    if (found == uploading_.end())
        return;
    found->second.requested = false;
    const auto token = found->second.token;
    uploads_->status(sha256, [alive = std::weak_ptr<Alive>{alive_}, token, sha256](const RoomFileStatus& status) {
        if (const auto self = alive.lock())
            self->host->onUploadStatus(token, sha256, status);
    });
}

void ListeningRoomHost::onUploadStatus(const std::uint64_t token, const std::string& sha256,
                                       const RoomFileStatus& status) {
    const auto found = uploading_.find(sha256);
    if (found == uploading_.end() || found->second.token != token)
        return;
    if (status.complete) {
        // Already in the cache (another queued guest item uses it) and not sent again: the declared size must be
        // the cached file's.
        if (status.size_bytes != found->second.size_bytes) {
            refuseUpload(sha256, IntentRejection::InvalidArgument);
            return;
        }
        probeUpload(sha256, status.file);
        return;
    }
    auto* owner = findMember(found->second.owner);
    if (owner == nullptr) {
        auto node = uploading_.extract(found);
        discardUpload(sha256, node.mapped());
        return;
    }
    found->second.requested = true;
    // Keep the link alive even if the send makes the guest answer synchronously.
    const auto link = owner->link;
    static_cast<void>(link->sendControl(RoomFileRequest{sha256, status.resume_offset}));
}

void ListeningRoomHost::onUploadOffer(Connection& connection, const RoomFileOffer& offer) {
    connection.last_upload_offer = std::max(connection.last_upload_offer, offer.transfer_id);
    auto early = connection.early_upload.extract(offer.transfer_id);
    if (!early.empty())
        for (const auto& chunk : early.mapped())
            connection.early_upload_bytes -= chunk.payload.size();
    const auto found = uploading_.find(offer.content_sha256);
    if (uploads_ == nullptr || found == uploading_.end() || found->second.owner != connection.fingerprint ||
        !found->second.requested)
        return;
    const auto owner = connection.fingerprint;
    // The cache checks the bytes against the offer: the offer must be the item the guest declared.
    if (offer.size_bytes != found->second.size_bytes) {
        refuseUpload(offer.content_sha256, IntentRejection::InvalidArgument);
        return;
    }
    auto& upload = found->second;
    ++connection.upload_activity;
    upload.requested = false;
    if (upload.receiving)
        uploads_->abandon(*upload.receiving);
    upload.offered = offer.transfer_id;
    // Guests choose their transfer ids: the cache gets one of the host's, unique across guests.
    const auto receiving = ++next_upload_transfer_id_;
    upload.receiving = receiving;
    uploads_->accept(RoomFileOffer{offer.content_sha256, offer.size_bytes, receiving});
    if (!early.empty())
        for (auto& chunk : early.mapped()) {
            chunk.transfer_id = receiving;
            uploads_->consume(std::move(chunk));
        }
    if (std::none_of(uploading_.begin(), uploading_.end(),
                     [&](const auto& entry) { return entry.second.owner == owner && entry.second.requested; })) {
        if (auto* still = findMember(owner)) {
            still->early_upload.clear();
            still->early_upload_bytes = 0;
        }
    }
}

void ListeningRoomHost::onUploadChunk(const RoomLink* link, RoomFileChunk chunk) {
    auto* connection = find(link);
    if (uploads_ == nullptr || connection == nullptr || !connection->greeted ||
        memberState(connection->fingerprint) != MemberState::Admitted)
        return;
    bool awaiting = false;
    for (const auto& [sha, upload] : uploading_) {
        if (upload.owner != connection->fingerprint)
            continue;
        if (upload.offered == chunk.transfer_id && upload.receiving) {
            ++connection->upload_activity;
            chunk.transfer_id = *upload.receiving;
            uploads_->consume(std::move(chunk));
            return;
        }
        awaiting = awaiting || upload.requested;
    }
    // Before its offer (control and media are not ordered with each other): kept while a request is out.
    if (!awaiting || chunk.transfer_id <= connection->last_upload_offer ||
        connection->early_upload_bytes + chunk.payload.size() > maximum_early_upload_bytes)
        return;
    ++connection->upload_activity;
    connection->early_upload_bytes += chunk.payload.size();
    connection->early_upload[chunk.transfer_id].push_back(std::move(chunk));
}

void ListeningRoomHost::onUploadComplete(const std::string& sha256, const std::filesystem::path& file) {
    const auto found = uploading_.find(sha256);
    if (found == uploading_.end() || !found->second.receiving)
        return;
    found->second.receiving.reset();
    found->second.offered.reset();
    probeUpload(sha256, file);
}

void ListeningRoomHost::onUploadFailed(const std::string& sha256, const std::uint32_t transfer_id,
                                       const RoomFileFailure failure) {
    const auto found = uploading_.find(sha256);
    if (found == uploading_.end() || found->second.receiving != transfer_id)
        return;
    found->second.receiving.reset();
    found->second.offered.reset();
    // A Gap keeps the partial file: asking again resumes from it.
    if (failure == RoomFileFailure::Gap) {
        askUpload(sha256);
        return;
    }
    if (found->second.retries < room_file_retries) {
        ++found->second.retries;
        askUpload(sha256);
        return;
    }
    refuseUpload(sha256, IntentRejection::InvalidArgument);
}

// The same rules as a local import, away from the host's thread.
void ListeningRoomHost::probeUpload(const std::string& sha256, const std::filesystem::path& file) {
    auto& upload = uploading_.at(sha256);
    upload.probing = true;
    const auto token = upload.token;
    upload_probe_->probe(file, [alive = std::weak_ptr<Alive>{alive_}, token, sha256,
                                file](const std::optional<RoomTrackSummary>& summary) {
        if (const auto self = alive.lock())
            self->host->onUploadProbed(token, sha256, file, summary);
    });
}

void ListeningRoomHost::onUploadProbed(const std::uint64_t token, const std::string& sha256,
                                       const std::filesystem::path& file,
                                       const std::optional<RoomTrackSummary>& summary) {
    const auto found = uploading_.find(sha256);
    if (found == uploading_.end() || found->second.token != token)
        return;
    if (!summary) {
        refuseUpload(sha256, IntentRejection::InvalidArgument);
        return;
    }
    if (!mayUpload(found->second.owner)) {
        refuseUpload(sha256, IntentRejection::MissingPermission);
        return;
    }
    const bool full = current_.queue.size() >= maximum_room_queue_items;
    RoomItem item;
    item.origin = RoomItemOrigin::GuestTrack;
    item.content_sha256 = sha256;
    item.size_bytes = found->second.size_bytes;
    item.summary = *summary;
    const auto owner = found->second.owner;
    if (!full && addGuestTrack(owner, file, std::move(item))) {
        uploading_.erase(sha256);
        return;
    }
    refuseUpload(sha256, full ? IntentRejection::QueueFull : IntentRejection::InvalidArgument);
}

// Tells the owner (when still here) and leaves nothing of the file behind.
void ListeningRoomHost::refuseUpload(const std::string& sha256, const IntentRejection reason) {
    auto node = uploading_.extract(sha256);
    if (node.empty())
        return;
    const auto& upload = node.mapped();
    if (const auto* owner = findMember(upload.owner)) {
        const auto link = owner->link;
        static_cast<void>(link->sendControl(RoomIntentRejected{upload.request_id, reason}));
    }
    discardUpload(sha256, upload);
}

void ListeningRoomHost::discardUpload(const std::string& sha256, const Upload& upload) {
    if (uploads_ == nullptr)
        return;
    if (upload.receiving)
        uploads_->abandon(*upload.receiving);
    // The same file may already be a queued guest item (uploaded before): that copy stays.
    const bool queued = std::any_of(guest_items_.begin(), guest_items_.end(),
                                    [&](const auto& guest) { return guest.second.content_sha256 == sha256; });
    if (!queued)
        uploads_->discard(sha256);
}

void ListeningRoomHost::stopUploadsOf(const std::string_view fingerprint) {
    std::vector<std::string> owned;
    for (const auto& [sha, upload] : uploading_)
        if (upload.owner == fingerprint)
            owned.push_back(sha);
    for (const auto& sha : owned) {
        auto node = uploading_.extract(sha);
        discardUpload(sha, node.mapped());
    }
}

// The room ended: every upload stops and the upload cache is emptied (its files were the guests').
void ListeningRoomHost::stopUploads() {
    uploading_.clear();
    if (uploads_ != nullptr)
        uploads_->purge();
}

void ListeningRoomHost::end(const RoomByeReason reason) {
    if (!invite_)
        return;
    invite_.reset();
    room_.reset();
    uses_persisted_invite_ = false;
    ++generation_;
    auto connections = std::move(connections_);
    connections_.clear();
    for (const auto& connection : connections) {
        static_cast<void>(connection.link->sendControl(RoomBye{reason}));
        forgetFiles(*connection.link);
        connection.link->close();
    }
    endpoint_.closeAll();
    // Guest items never outlive the room in the host's queue, nor their files the upload cache.
    auto guests = std::move(guest_items_);
    guest_items_.clear();
    for (const auto& guest : guests)
        static_cast<void>(player_.remove(LocalQueueOccurrenceId{guest.first}));
    guest_files_.clear();
    stopUploads();
    room_catalog_ = {};
    catalog_sources_.clear();
    catalog_playlist_ids_.clear();
    catalog_entry_ids_.clear();
    next_catalog_id_ = 1;
    catalog_again_ = false;
    catalog_waiting_.clear();
    catalog_refresh_scheduled_ = false;
    catalog_adders_.clear();
    if (!drifts_.empty()) {
        drifts_.clear();
        if (onDriftsChanged)
            onDriftsChanged();
    }
    serve_scheduled_ = false;
    descriptions_.clear();
    describing_.clear();
    unreadable_.clear();
    held_.clear();
    kicked_away_.clear();
    last_current_.reset();
    reanchor_ = false;
    current_ = RoomSnapshot{.version = current_.version + 1};
    sent_view_ = {};
    if (onChanged)
        onChanged(current_);
}

// Permissions are per member and not part of the view: a roster change bumps the version so every
// admitted member takes the new state (and its own permissions) even when the view is the same.
void ListeningRoomHost::rosterChanged() {
    saveRoster();
    if (!room_)
        return;
    // Uploads whose owner lost UploadOwnTracks stop here.
    std::vector<std::string> revoked;
    for (const auto& [sha, upload] : uploading_)
        if (!mayUpload(upload.owner))
            revoked.push_back(sha);
    for (const auto& sha : revoked)
        refuseUpload(sha, IntentRejection::MissingPermission);
    ++current_.version;
    sent_view_ = guestView(current_);
    // Iterate over a copy: a synchronous transport may call back into the host while sending.
    std::vector<std::shared_ptr<RoomLink>> links;
    for (const auto& connection : connections_)
        links.push_back(connection.link);
    for (const auto& link : links) {
        const auto* connection = find(link.get());
        if (connection != nullptr && connection->greeted &&
            memberState(connection->fingerprint) == MemberState::Admitted)
            sendState(*connection);
    }
    if (onChanged)
        onChanged(current_);
}

// The store writes the whole roster, so one save that works leaves nothing unsaved.
void ListeningRoomHost::saveRoster() {
    auto status = RoomRosterSaveStatus::Failed;
    if (roster_writable_) {
        status = roster_store_.save(roster_.snapshot());
        if (status == RoomRosterSaveStatus::BlockedByFutureVersion)
            roster_writable_ = false;
    }
    const bool unsaved = status != RoomRosterSaveStatus::Saved;
    if (unsaved == roster_unsaved_)
        return;
    roster_unsaved_ = unsaved;
    if (onRosterUnsavedChanged)
        onRosterUnsavedChanged();
}

ListeningRoomHost::Connection* ListeningRoomHost::find(const RoomLink* link) {
    const auto found = std::find_if(connections_.begin(), connections_.end(),
                                    [&](const Connection& c) { return c.link.get() == link; });
    return found == connections_.end() ? nullptr : &*found;
}

ListeningRoomHost::Connection* ListeningRoomHost::findMember(const std::string_view fingerprint) {
    const auto found = std::find_if(connections_.begin(), connections_.end(),
                                    [&](const Connection& c) { return c.greeted && c.fingerprint == fingerprint; });
    return found == connections_.end() ? nullptr : &*found;
}

std::optional<MemberState> ListeningRoomHost::memberState(const std::string_view fingerprint) const {
    if (!room_)
        return std::nullopt;
    for (const auto& member : room_->members())
        if (member.fingerprint == fingerprint)
            return member.state;
    return std::nullopt;
}

RoomMember ListeningRoomHost::hostMember() const {
    return RoomMember{host_fingerprint_hex_, host_profile_.nickname, host_profile_.avatar, MemberState::Admitted};
}

} // namespace music_player::m0
