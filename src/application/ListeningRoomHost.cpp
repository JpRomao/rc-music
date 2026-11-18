#include "m0/application/ListeningRoomHost.hpp"

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

} // namespace

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
};

ListeningRoomHost::ListeningRoomHost(ListeningRoomHostDependencies dependencies)
    : roster_store_(dependencies.roster_store), player_(dependencies.player), endpoint_(dependencies.endpoint),
      catalog_(dependencies.catalog), host_clock_ms_(std::move(dependencies.host_clock_ms)),
      random_bytes_(std::move(dependencies.random_bytes)), host_fingerprint_(dependencies.host_fingerprint),
      host_fingerprint_hex_(lowerHex(dependencies.host_fingerprint)),
      host_profile_(std::move(dependencies.host_profile)), alive_(std::make_shared<Alive>(Alive{this})) {
    if (!host_clock_ms_ || !random_bytes_)
        throw std::invalid_argument{"the listening room host needs a clock and a random source"};
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
        connection.link->close();
    }
    if (invite_)
        endpoint_.closeAll();
}

std::optional<RoomInviteLink> ListeningRoomHost::open(const RoomOpenOptions& options) {
    if (invite_)
        return invite_;
    const std::size_t max_listeners =
        std::clamp<std::size_t>(options.max_listeners, minimum_room_listeners, maximum_room_listeners);
    RoomInviteLink invite{};
    invite.host_fingerprint = host_fingerprint_;
    auto roster = roster_.snapshot();
    if (options.until_revoked && roster.invite) {
        invite.room_id = roster.invite->room_id;
        invite.secret = roster.invite->secret;
    } else {
        random_bytes_(invite.room_id);
        random_bytes_(invite.secret);
    }
    if (options.until_revoked) {
        roster.invite = PersistedInvite{invite.room_id, invite.secret, max_listeners, options.default_role_id};
        if (roster_.restore(std::move(roster)))
            saveRoster();
    }

    room_.emplace(max_listeners, options.default_role_id, roster_);
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
    if (block)
        saveRoster();
    if (auto* connection = findMember(fingerprint)) {
        const auto link = connection->link;
        static_cast<void>(link->sendControl(RoomBye{RoomByeReason::Kicked}));
        drop(link);
    }
    static_cast<void>(refresh());
    return true;
}

RoomSnapshot ListeningRoomHost::snapshot() const { return current_; }

bool ListeningRoomHost::isOpen() const noexcept { return invite_.has_value(); }

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
    guest_items_[described.id] = described;
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
    link->onMedia = [](RoomFileChunk) {}; // File transfer: S8.
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
    }
    // A second hello, file messages (S8) and drift reports (S9.2) are ignored here.
}

void ListeningRoomHost::onLinkClosed(const std::shared_ptr<RoomLink>& link) {
    const auto* connection = find(link.get());
    if (connection == nullptr)
        return;
    const auto fingerprint = connection->fingerprint;
    const bool greeted = connection->greeted;
    std::erase_if(connections_, [&](const Connection& c) { return c.link == link; });
    if (greeted && room_) {
        room_->leave(fingerprint);
        static_cast<void>(refresh());
    }
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
    if (!rejection)
        rejection = apply(message.intent);
    if (rejection)
        static_cast<void>(link->sendControl(RoomIntentRejected{message.request_id, *rejection}));
    static_cast<void>(refresh());
}

std::optional<IntentRejection> ListeningRoomHost::apply(const RoomIntent& intent) {
    const auto accepted = [](const bool ok) {
        return ok ? std::nullopt : std::optional<IntentRejection>{IntentRejection::InvalidArgument};
    };
    switch (intent.kind) {
    case RoomIntentKind::Next:
        return accepted(player_.next());
    case RoomIntentKind::Previous:
        return accepted(player_.previous());
    case RoomIntentKind::Seek:
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
    case RoomIntentKind::AddItems:
        // Uploads need the guest-file transfer (S8/S12); catalog entries need the room catalog (S11.2).
        return intent.uploaded ? IntentRejection::InvalidArgument : IntentRejection::UnknownItem;
    case RoomIntentKind::AddPlaylist:
        return IntentRejection::UnknownItem;
    }
    return IntentRejection::InvalidArgument;
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
    item.added_by = host_profile_.nickname;
    item.youtube_id.clear();
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
    std::erase_if(guest_items_, [&](const auto& guest) {
        return std::none_of(items.begin(), items.end(), [&](const LocalFileQueueSnapshotItem& item) {
            return item.occurrence_id.value == guest.first;
        });
    });
    std::optional<std::size_t> current_index;
    for (std::size_t index = 0; index < items.size(); ++index)
        if (player.queue.current_occurrence_id == std::optional{items[index].occurrence_id})
            current_index = index;
    // A longer local queue shows the maximum_room_queue_items items from the current one on.
    const std::size_t first = items.size() > maximum_room_queue_items
                                  ? std::min(current_index.value_or(0), items.size() - maximum_room_queue_items)
                                  : 0;
    const std::size_t end = std::min(items.size(), first + maximum_room_queue_items);
    if (current_index && !guest_items_.contains(items[*current_index].occurrence_id.value))
        requestDescription(items[*current_index].source_path);
    for (std::size_t index = first; index < end; ++index)
        if (!guest_items_.contains(items[index].occurrence_id.value))
            requestDescription(items[index].source_path);

    RoomSnapshot next;
    next.members.push_back(hostMember());
    for (auto& member : room_->members())
        next.members.push_back(std::move(member));
    for (std::size_t index = first; index < end; ++index)
        if (auto item = roomItem(items[index]))
            next.queue.push_back(std::move(*item));

    if (current_index && std::any_of(next.queue.begin(), next.queue.end(), [&](const RoomItem& item) {
            return item.id == items[*current_index].occurrence_id.value;
        })) {
        auto& playback = next.playback;
        playback.current_item = items[*current_index].occurrence_id.value;
        playback.playing = player.status == PlaybackStatus::Playing;
        const auto position = std::max<std::int64_t>(player.position_ms, 0);
        const auto now = host_clock_ms_();
        const auto& previous = current_.playback;
        if (previous.current_item == playback.current_item && previous.playing == playback.playing &&
            expectedPositionMs(previous, now) == position) {
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
    // Iterate over a copy: a synchronous transport may call back into the host while sending.
    std::vector<std::shared_ptr<RoomLink>> links;
    for (const auto& connection : connections_)
        links.push_back(connection.link);
    for (const auto& link : links) {
        auto* connection = find(link.get());
        if (connection == nullptr || !connection->greeted ||
            memberState(connection->fingerprint) != MemberState::Admitted)
            continue;
        sendState(*connection);
        if ((connection = find(link.get())) != nullptr)
            sendLyrics(*connection);
    }
    if (onChanged)
        onChanged(current_);
    return true;
}

void ListeningRoomHost::admitted(const std::shared_ptr<RoomLink>& link) {
    // Admission changes the members, so a broadcast normally carries the state; when nothing changed
    // (a newer link of someone already admitted) this member gets the current state directly.
    const bool broadcast = refresh();
    auto* connection = find(link.get());
    if (connection == nullptr)
        return;
    if (!broadcast)
        sendState(*connection);
    sendLyrics(*connection);
}

void ListeningRoomHost::sendState(const Connection& connection) const {
    // A refused message (e.g. too large for the codec) is skipped: the member keeps its older version.
    static_cast<void>(
        connection.link->sendControl(RoomStateMessage{current_, roster_.effectivePermissions(connection.fingerprint)}));
}

void ListeningRoomHost::sendLyrics(Connection& connection) const {
    if (!current_.playback.current_item || connection.lyrics_item == current_.playback.current_item)
        return;
    const auto id = *current_.playback.current_item;
    connection.lyrics_item = id;
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
        static_cast<void>(connection.link->sendControl(RoomLyrics{id, found->second.lyrics_lrc}));
}

void ListeningRoomHost::drop(const std::shared_ptr<RoomLink>& link) {
    std::erase_if(connections_, [&](const Connection& c) { return c.link == link; });
    link->close();
}

void ListeningRoomHost::end(const RoomByeReason reason) {
    if (!invite_)
        return;
    invite_.reset();
    room_.reset();
    ++generation_;
    auto connections = std::move(connections_);
    connections_.clear();
    for (const auto& connection : connections) {
        static_cast<void>(connection.link->sendControl(RoomBye{reason}));
        connection.link->close();
    }
    endpoint_.closeAll();
    // Guest items never outlive the room in the host's queue.
    auto guests = std::move(guest_items_);
    guest_items_.clear();
    for (const auto& guest : guests)
        static_cast<void>(player_.remove(LocalQueueOccurrenceId{guest.first}));
    descriptions_.clear();
    describing_.clear();
    unreadable_.clear();
    current_ = RoomSnapshot{.version = current_.version + 1};
    if (onChanged)
        onChanged(current_);
}

void ListeningRoomHost::saveRoster() {
    if (!roster_writable_)
        return;
    if (roster_store_.save(roster_.snapshot()) == RoomRosterSaveStatus::BlockedByFutureVersion)
        roster_writable_ = false;
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
