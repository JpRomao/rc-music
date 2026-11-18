#include "m0/application/ListeningRoomGuest.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <variant>

namespace music_player::m0 {
namespace {

// Chunks kept for offers still on their way: about two media buffers (RoomFileSender stops at 1 MiB).
constexpr std::size_t maximum_early_chunk_bytes = 2 * 1024 * 1024;

bool lowerHex64(const std::string_view text) {
    return text.size() == 64 && std::all_of(text.begin(), text.end(), [](const char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

const RoomItem* itemWithId(const RoomSnapshot& snapshot, const std::optional<std::uint64_t> id) {
    if (!id)
        return nullptr;
    const auto found = std::find_if(snapshot.queue.begin(), snapshot.queue.end(),
                                    [&](const RoomItem& item) { return item.id == *id; });
    return found == snapshot.queue.end() ? nullptr : &*found;
}

GuestRoomEnd byeEnd(const RoomByeReason reason) {
    switch (reason) {
    case RoomByeReason::Closed:
        return {GuestEndReason::Closed, RoomLinkFailure::Closed};
    case RoomByeReason::Revoked:
        return {GuestEndReason::Revoked, RoomLinkFailure::Closed};
    case RoomByeReason::Kicked:
        return {GuestEndReason::Kicked, RoomLinkFailure::Refused};
    case RoomByeReason::VersionMismatch:
        return {GuestEndReason::VersionMismatch, RoomLinkFailure::VersionMismatch};
    case RoomByeReason::Full:
        return {GuestEndReason::RoomFull, RoomLinkFailure::Refused};
    }
    return {GuestEndReason::Closed, RoomLinkFailure::Closed};
}

} // namespace

// Callbacks handed to the connector, links and the scheduler hold a weak reference to this token, so
// one that arrives after the guest is gone does nothing.
struct ListeningRoomGuest::Alive final {
    ListeningRoomGuest* guest{};
};

ListeningRoomGuest::ListeningRoomGuest(ListeningRoomGuestDependencies dependencies)
    : connector_(dependencies.connector), profile_(std::move(dependencies.profile)),
      stored_custom_avatar_sha256_(std::move(dependencies.stored_custom_avatar_sha256)),
      capture_session_(std::move(dependencies.capture_session)),
      restore_session_(std::move(dependencies.restore_session)), schedule_(std::move(dependencies.schedule)),
      files_(dependencies.files), alive_(std::make_shared<Alive>(Alive{this})) {
    if (!capture_session_ || !restore_session_ || !schedule_)
        throw std::invalid_argument{"the listening room guest needs the local session seam and a scheduler"};
    if (!isValidListenerProfile(profile_))
        throw std::invalid_argument{"the listening room guest needs a valid listener profile"};
    if (files_ != nullptr) {
        // One guest at a time uses the inbox: a newer guest replaces these callbacks, and they do
        // nothing once this guest is gone.
        const std::weak_ptr<Alive> alive = alive_;
        files_->onComplete = [alive](std::string sha256, std::filesystem::path file) {
            if (const auto self = alive.lock())
                self->guest->onFileComplete(sha256, file);
        };
        files_->onFailed = [alive](std::string sha256, std::uint32_t transfer_id, RoomFileFailure failure) {
            if (const auto self = alive.lock())
                self->guest->onFileFailed(sha256, transfer_id, failure);
        };
    }
}

ListeningRoomGuest::~ListeningRoomGuest() {
    alive_.reset();
    if (connecting_)
        connector_.cancel();
    if (link_) {
        link_->onControl = nullptr;
        link_->onMedia = nullptr;
        link_->onClosed = nullptr;
        link_->close();
    }
}

void ListeningRoomGuest::join(const RoomInviteLink& invite) {
    if (state_ != GuestRoomState::Idle && state_ != GuestRoomState::Ended)
        return;
    invite_ = invite;
    state_ = GuestRoomState::Connecting;
    end_.reset();
    snapshot_ = {};
    permissions_.reset();
    fresh_link_ = false;
    changed();
    if (state_ == GuestRoomState::Connecting && !connecting_ && !link_)
        connect();
}

void ListeningRoomGuest::leave() {
    if (state_ == GuestRoomState::Idle)
        return;
    // Tell the host first, so it frees the seat at once instead of holding it for its reconnect window.
    // The link is detached before the send: the host closing it in answer is not a drop.
    if (const auto link = std::exchange(link_, nullptr)) {
        ++link_id_;
        static_cast<void>(link->sendControl(RoomBye{RoomByeReason::Closed}));
        link->close();
    }
    stop();
    state_ = GuestRoomState::Idle;
    end_.reset();
    changed();
}

std::uint64_t ListeningRoomGuest::sendIntent(const RoomIntent& intent) {
    if (state_ != GuestRoomState::Joined || !link_)
        return 0;
    // Taken before sending: an answer delivered during the send may lead to another sendIntent.
    const auto request_id = next_request_id_++;
    // Keep the link alive even if the send makes the host answer (and this guest end) synchronously.
    const auto link = link_;
    return link->sendControl(RoomIntentMessage{request_id, intent}) ? request_id : 0;
}

GuestRoomState ListeningRoomGuest::state() const { return state_; }

const RoomSnapshot& ListeningRoomGuest::snapshot() const { return snapshot_; }

RoomPermissionSet ListeningRoomGuest::permissions() const { return permissions_; }

std::optional<GuestRoomEnd> ListeningRoomGuest::endReason() const { return end_; }

SessionSnapshot ListeningRoomGuest::sessionForAutosave(SessionSnapshot live) const {
    return saved_session_ ? *saved_session_ : live;
}

bool ListeningRoomGuest::holdsLocalSession() const noexcept { return saved_session_.has_value(); }

std::optional<std::filesystem::path> ListeningRoomGuest::localFile(const std::string_view sha256) const {
    const auto found = local_files_.find(sha256);
    return found == local_files_.end() ? std::nullopt : std::optional{found->second};
}

std::optional<RoomLyrics> ListeningRoomGuest::lyrics() const { return lyrics_; }

void ListeningRoomGuest::connect() {
    const auto attempt = ++attempt_;
    connecting_ = true;
    const std::weak_ptr<Alive> alive = alive_;
    connector_.connect(
        *invite_,
        [alive, attempt](std::shared_ptr<RoomLink> link) {
            if (const auto self = alive.lock())
                self->guest->onConnected(attempt, std::move(link));
            else if (link)
                link->close();
        },
        [alive, attempt](const RoomLinkFailure failure) {
            if (const auto self = alive.lock())
                self->guest->onConnectFailed(attempt, failure);
        });
}

void ListeningRoomGuest::onConnected(const std::uint64_t attempt, std::shared_ptr<RoomLink> link) {
    if (attempt != attempt_ || !connecting_ || !link) {
        if (link)
            link->close();
        return;
    }
    connecting_ = false;
    link_ = link;
    const auto link_id = ++link_id_;
    const std::weak_ptr<Alive> alive = alive_;
    link->onControl = [alive, link_id](RoomControlMessage message) {
        if (const auto self = alive.lock())
            self->guest->onControl(link_id, message);
    };
    link->onMedia = [alive, link_id](RoomFileChunk chunk) {
        if (const auto self = alive.lock(); self && link_id == self->guest->link_id_)
            self->guest->onChunk(std::move(chunk));
    };
    link->onClosed = [alive, link_id](const RoomLinkFailure failure) {
        if (const auto self = alive.lock())
            self->guest->onLinkClosed(link_id, failure);
    };
    // The fake transport answers synchronously: anything may happen during this send.
    if (!link->sendControl(hello()) && link_id == link_id_) {
        detachLink();
        lost(RoomLinkFailure::Closed);
    }
}

void ListeningRoomGuest::onConnectFailed(const std::uint64_t attempt, const RoomLinkFailure failure) {
    if (attempt != attempt_ || !connecting_)
        return;
    connecting_ = false;
    lost(failure);
}

void ListeningRoomGuest::onControl(const std::uint64_t link_id, const RoomControlMessage& message) {
    if (link_id != link_id_ || !link_)
        return;
    // Keeps the link object alive while this call may close and release it.
    const auto keep = link_;
    if (const auto* result = std::get_if<RoomJoinResult>(&message)) {
        onJoinResult(result->decision);
    } else if (const auto* state = std::get_if<RoomStateMessage>(&message)) {
        if (state_ == GuestRoomState::Joined)
            onState(*state);
    } else if (const auto* rejected = std::get_if<RoomIntentRejected>(&message)) {
        if (onRejected)
            onRejected(rejected->request_id, rejected->reason);
    } else if (const auto* bye = std::get_if<RoomBye>(&message)) {
        const auto ending = byeEnd(bye->reason);
        end(ending.reason, ending.failure);
    } else if (const auto* offer = std::get_if<RoomFileOffer>(&message)) {
        if (state_ == GuestRoomState::Joined)
            onOffer(*offer);
    } else if (const auto* lyrics = std::get_if<RoomLyrics>(&message)) {
        if (state_ == GuestRoomState::Joined && snapshot_.playback.current_item == lyrics->item_id &&
            lyrics->lrc_text.size() <= maximum_room_lyrics_bytes && lyrics_ != *lyrics) {
            lyrics_ = *lyrics;
            changed();
        }
    }
    // Clock and catalog messages belong to later tasks (S9-S11).
}

void ListeningRoomGuest::onLinkClosed(const std::uint64_t link_id, const RoomLinkFailure failure) {
    if (link_id != link_id_ || !link_)
        return;
    const auto keep = link_;
    // Closed by the network: no local close is needed, only forget it.
    link_.reset();
    ++link_id_;
    lost(failure);
}

void ListeningRoomGuest::onJoinResult(const JoinDecision decision) {
    switch (decision) {
    case JoinDecision::Admitted:
        if (state_ == GuestRoomState::Joined)
            return;
        ++episode_; // stops a running reconnect window
        fresh_link_ = true;
        if (!saved_session_)
            saved_session_ = capture_session_();
        state_ = GuestRoomState::Joined;
        changed();
        updateFiles();
        return;
    case JoinDecision::Pending:
        if (state_ == GuestRoomState::WaitingApproval || state_ == GuestRoomState::Joined)
            return;
        ++episode_;
        state_ = GuestRoomState::WaitingApproval;
        changed();
        return;
    case JoinDecision::RejectedFull:
        end(GuestEndReason::RoomFull, RoomLinkFailure::Refused);
        return;
    case JoinDecision::RejectedBlocked:
        end(GuestEndReason::Blocked, RoomLinkFailure::Refused);
        return;
    case JoinDecision::RejectedClosed:
        end(GuestEndReason::Rejected, RoomLinkFailure::Refused);
        return;
    }
}

void ListeningRoomGuest::onState(const RoomStateMessage& message) {
    if (!fresh_link_ && message.snapshot.version <= snapshot_.version)
        return;
    fresh_link_ = false;
    if (message.snapshot == snapshot_ && message.my_permissions == permissions_)
        return;
    // The item that stops being current was played: the cache may evict it.
    const auto* before = itemWithId(snapshot_, snapshot_.playback.current_item);
    if (before != nullptr && before->id != message.snapshot.playback.current_item && files_ != nullptr &&
        lowerHex64(before->content_sha256))
        files_->markPlayed(before->content_sha256);
    if (lyrics_ && lyrics_->item_id != message.snapshot.playback.current_item)
        lyrics_.reset();
    snapshot_ = message.snapshot;
    permissions_ = message.my_permissions;
    changed();
    updateFiles();
}

std::vector<std::string> ListeningRoomGuest::fileWindow() const {
    std::vector<std::string> window;
    const auto count = std::min(snapshot_.queue.size(), std::size_t{1} + room_prefetch_items);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& item = snapshot_.queue[index];
        if (item.origin != RoomItemOrigin::YoutubeVideo && lowerHex64(item.content_sha256) &&
            std::find(window.begin(), window.end(), item.content_sha256) == window.end())
            window.push_back(item.content_sha256);
    }
    return window;
}

void ListeningRoomGuest::updateFiles() {
    if (files_ == nullptr || state_ != GuestRoomState::Joined || !link_)
        return;
    const auto window = fileWindow();
    const auto inWindow = [&window](const std::string_view sha) {
        return std::find(window.begin(), window.end(), sha) != window.end();
    };
    // Transfers whose item left the window stop; they are asked again if it comes back.
    std::vector<std::uint32_t> stopped;
    for (const auto& [transfer_id, sha] : receiving_)
        if (!inWindow(sha))
            stopped.push_back(transfer_id);
    for (const auto transfer_id : stopped) {
        receiving_.erase(transfer_id);
        files_->abandon(transfer_id);
    }
    std::erase_if(requested_, [&](const std::string& sha) { return !inWindow(sha); });
    std::erase_if(checking_, [&](const std::string& sha) { return !inWindow(sha); });
    std::erase_if(failed_, [&](const std::string& sha) { return !inWindow(sha); });
    std::erase_if(retries_, [&](const auto& retry) { return !inWindow(retry.first); });
    if (requested_.empty()) {
        early_chunks_.clear();
        early_bytes_ = 0;
    }
    const auto epoch = files_epoch_;
    const std::weak_ptr<Alive> alive = alive_;
    for (const auto& sha : window) {
        const bool receiving = std::any_of(receiving_.begin(), receiving_.end(),
                                           [&](const auto& transfer) { return transfer.second == sha; });
        if (local_files_.contains(sha) || checking_.contains(sha) || requested_.contains(sha) || receiving ||
            failed_.contains(sha))
            continue;
        checking_.insert(sha);
        files_->status(sha, [alive, epoch, sha](const RoomFileStatus& status) {
            if (const auto self = alive.lock())
                self->guest->onFileStatus(epoch, sha, status);
        });
    }
}

void ListeningRoomGuest::onFileStatus(const std::uint64_t epoch, const std::string& sha256,
                                      const RoomFileStatus& status) {
    if (epoch != files_epoch_ || checking_.erase(sha256) == 0)
        return;
    if (status.complete) {
        local_files_[sha256] = status.file;
        changed();
        return;
    }
    if (state_ != GuestRoomState::Joined || !link_)
        return;
    const auto window = fileWindow();
    if (std::find(window.begin(), window.end(), sha256) == window.end())
        return;
    requested_.insert(sha256);
    // Keep the link alive even if the send makes the host answer synchronously.
    const auto link = link_;
    static_cast<void>(link->sendControl(RoomFileRequest{sha256, status.resume_offset}));
}

void ListeningRoomGuest::onOffer(const RoomFileOffer& offer) {
    auto early = early_chunks_.extract(offer.transfer_id);
    if (!early.empty())
        for (const auto& chunk : early.mapped())
            early_bytes_ -= chunk.payload.size();
    last_offer_id_ = std::max(last_offer_id_, offer.transfer_id);
    if (files_ == nullptr || receiving_.contains(offer.transfer_id))
        return;
    const auto held = std::find_if(receiving_.begin(), receiving_.end(),
                                   [&](const auto& transfer) { return transfer.second == offer.content_sha256; });
    if (requested_.erase(offer.content_sha256) == 0 && held == receiving_.end())
        return;
    // A newer offer for a file already being received: the host answered a later request for it and
    // cancelled the earlier transfer, so the one held here would never complete. It is abandoned first,
    // which also releases its cache reservation before the new one is accepted. The new transfer may
    // start below what the old one wrote; the inbox continues from the host's offset (RoomFileInbox).
    if (held != receiving_.end()) {
        const auto stale = held->first;
        receiving_.erase(held);
        files_->abandon(stale);
    }
    receiving_[offer.transfer_id] = offer.content_sha256;
    files_->accept(offer);
    if (!early.empty())
        for (auto& chunk : early.mapped())
            files_->consume(std::move(chunk));
    if (requested_.empty()) {
        early_chunks_.clear();
        early_bytes_ = 0;
    }
}

void ListeningRoomGuest::onChunk(RoomFileChunk chunk) {
    if (files_ == nullptr || state_ != GuestRoomState::Joined)
        return;
    if (receiving_.contains(chunk.transfer_id)) {
        files_->consume(std::move(chunk));
        return;
    }
    if (requested_.empty() || chunk.transfer_id <= last_offer_id_ ||
        early_bytes_ + chunk.payload.size() > maximum_early_chunk_bytes)
        return;
    early_bytes_ += chunk.payload.size();
    early_chunks_[chunk.transfer_id].push_back(std::move(chunk));
}

void ListeningRoomGuest::onFileComplete(const std::string& sha256, const std::filesystem::path& file) {
    // Only transfers still wanted: a late answer for one abandoned or purged is ignored.
    if (std::erase_if(receiving_, [&](const auto& transfer) { return transfer.second == sha256; }) == 0)
        return;
    local_files_[sha256] = file;
    changed();
}

void ListeningRoomGuest::onFileFailed(const std::string& sha256, const std::uint32_t transfer_id,
                                      const RoomFileFailure failure) {
    // Only the live transfer: a late answer for one already replaced or abandoned changes nothing.
    const auto found = receiving_.find(transfer_id);
    if (found == receiving_.end() || found->second != sha256)
        return;
    receiving_.erase(found);
    // A Gap is a race, not a fault: the partial file stays and asking again (status() first) resumes
    // from it, so the host replaces the transfer from the right offset.
    if (failure == RoomFileFailure::Gap) {
        updateFiles();
        return;
    }
    // Anything else may be transient: asked again a few times while its item stays in the window.
    if (auto& retries = retries_[sha256]; retries < room_file_retries) {
        ++retries;
        updateFiles();
        return;
    }
    failed_.insert(sha256);
}

// Stops every transfer: a dropped link keeps the partial files for resume; purge (leaving, the end of
// the room) then empties the cache. The receiver's transfers are abandoned before the purge.
void ListeningRoomGuest::stopFiles(const bool purge) {
    ++files_epoch_;
    checking_.clear();
    requested_.clear();
    failed_.clear();
    retries_.clear();
    early_chunks_.clear();
    early_bytes_ = 0;
    last_offer_id_ = 0;
    auto receiving = std::exchange(receiving_, {});
    if (files_ != nullptr)
        for (const auto& transfer : receiving)
            files_->abandon(transfer.first);
    if (!purge)
        return;
    local_files_.clear();
    lyrics_.reset();
    if (files_ != nullptr)
        files_->purge();
}

void ListeningRoomGuest::lost(const RoomLinkFailure failure) {
    last_failure_ = failure;
    stopFiles(false);
    switch (state_) {
    case GuestRoomState::Joined: {
        // The window counts from the drop; the first attempt starts at once.
        const auto episode = ++episode_;
        retry_delay_ms_ = room_reconnect_retry_initial_ms;
        state_ = GuestRoomState::Reconnecting;
        const std::weak_ptr<Alive> alive = alive_;
        schedule_(room_reconnect_window_ms, [alive, episode] {
            if (const auto self = alive.lock())
                self->guest->onWindowExpired(episode);
        });
        connect();
        changed();
        return;
    }
    case GuestRoomState::Reconnecting: {
        // The relay says the host is not in the room: its app went away (no Bye) and nothing brings
        // that room back inside the window, so it ended for this guest now.
        if (failure == RoomLinkFailure::HostOffline) {
            end(GuestEndReason::Closed, failure);
            return;
        }
        const auto episode = episode_;
        const auto delay = retry_delay_ms_;
        retry_delay_ms_ = std::min<std::int64_t>(retry_delay_ms_ * 2, room_reconnect_retry_maximum_ms);
        const std::weak_ptr<Alive> alive = alive_;
        schedule_(delay, [alive, episode] {
            if (const auto self = alive.lock())
                self->guest->onRetry(episode);
        });
        return;
    }
    case GuestRoomState::Connecting:
    case GuestRoomState::WaitingApproval:
        end(GuestEndReason::LinkLost, failure);
        return;
    case GuestRoomState::Idle:
    case GuestRoomState::Ended:
        return;
    }
}

void ListeningRoomGuest::onRetry(const std::uint64_t episode) {
    if (episode != episode_ || state_ != GuestRoomState::Reconnecting || connecting_ || link_)
        return;
    connect();
}

void ListeningRoomGuest::onWindowExpired(const std::uint64_t episode) {
    if (episode != episode_ || state_ != GuestRoomState::Reconnecting)
        return;
    end(GuestEndReason::LinkLost, last_failure_);
}

void ListeningRoomGuest::end(const GuestEndReason reason, const RoomLinkFailure failure) {
    stop();
    state_ = GuestRoomState::Ended;
    end_ = GuestRoomEnd{reason, failure};
    changed();
    if (onEnded)
        onEnded(failure);
}

// Leaves the room locally: no link, no attempt, no window, an empty view and the local session back.
void ListeningRoomGuest::stop() {
    if (connecting_) {
        connecting_ = false;
        connector_.cancel();
    }
    ++attempt_;
    ++episode_;
    detachLink();
    stopFiles(true);
    snapshot_ = {};
    permissions_.reset();
    fresh_link_ = false;
    if (auto saved = std::exchange(saved_session_, std::nullopt))
        restore_session_(*saved);
}

// The link's callbacks are left in place (they may be running) and turn into no-ops with the new id.
void ListeningRoomGuest::detachLink() {
    ++link_id_;
    if (const auto link = std::exchange(link_, nullptr))
        link->close();
}

RoomHello ListeningRoomGuest::hello() const {
    auto avatar = profile_.avatar;
    if (avatar.kind == AvatarKind::Custom) {
        const auto stored = stored_custom_avatar_sha256_ ? stored_custom_avatar_sha256_() : std::nullopt;
        if (!stored || *stored != avatar.custom_sha256)
            avatar = AvatarChoice{AvatarKind::Bundled, 0, {}};
    }
    return RoomHello{room_protocol_version, profile_.nickname, std::move(avatar)};
}

void ListeningRoomGuest::changed() const {
    if (onChanged)
        onChanged();
}

} // namespace music_player::m0
