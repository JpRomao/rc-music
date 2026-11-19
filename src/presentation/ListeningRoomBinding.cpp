#include "presentation/ListeningRoomBinding.hpp"

#include "m0/domain/ListeningRoomLimits.hpp"
#include "m0/domain/RoomInviteLink.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <utility>

namespace music_player::m0 {
namespace {

// How often a guest playing the room feeds the lyrics panel its position.
constexpr int room_position_feed_ms = 100;
// How often a pre-room session the local player refused is offered again.
constexpr int room_give_back_retry_ms = 100;

[[nodiscard]] std::string lowerHex(const std::array<std::uint8_t, 32>& bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        text.push_back(digits[byte >> 4]);
        text.push_back(digits[byte & 0x0F]);
    }
    return text;
}

} // namespace

ListeningRoomBinding::ListeningRoomBinding(ListeningRoomBindingDependencies dependencies, QObject* parent)
    : QObject(parent), dependencies_(std::move(dependencies)), members_(this), pending_(this), queue_(this),
      roles_(this), known_people_(this) {
    if (!dependencies_.schedule)
        dependencies_.schedule = scheduler(this);
    if (!dependencies_.capture_session)
        dependencies_.capture_session = [] { return SessionSnapshot{}; };
    if (!dependencies_.restore_session)
        dependencies_.restore_session = [](const SessionSnapshot&) { return true; };
    if (!dependencies_.custom_avatar_sha256)
        dependencies_.custom_avatar_sha256 = [] { return std::optional<std::string>{}; };
    if (available() &&
        (dependencies_.host_player == nullptr || dependencies_.catalog == nullptr ||
         dependencies_.roster_store == nullptr || !dependencies_.clock_ms || !dependencies_.random_bytes))
        throw std::invalid_argument{"the listening room needs its player, catalog, roster, clock and random source"};
    if (available() && dependencies_.guest_backend != nullptr)
        follower_ =
            std::make_unique<RoomPlaybackFollower>(*dependencies_.guest_backend, dependencies_.clock_ms,
                                                   room_drift_nudge_ms, room_drift_seek_ms, dependencies_.schedule);
    follow_timer_.setInterval(room_drift_check_ms);
    position_timer_.setInterval(room_position_feed_ms);
    connect(&follow_timer_, &QTimer::timeout, this, &ListeningRoomBinding::followTick);
    connect(&position_timer_, &QTimer::timeout, this, &ListeningRoomBinding::feedPosition);
    give_back_timer_.setInterval(room_give_back_retry_ms);
    connect(&give_back_timer_, &QTimer::timeout, this, &ListeningRoomBinding::retryGiveBack);
}

ListeningRoomBinding::~ListeningRoomBinding() {
    // Neither destructor calls back; the host still removes its guest items from the player.
    follow_timer_.stop();
    position_timer_.stop();
    give_back_timer_.stop();
    retired_guests_.clear();
    guest_.reset();
    host_.reset();
}

void ListeningRoomBinding::setOutputOffsetMs(const int offset_ms) {
    if (follower_)
        follower_->setOutputOffsetMs(offset_ms);
}

bool ListeningRoomBinding::available() const noexcept { return !dependencies_.signaling_origin.isEmpty(); }
int ListeningRoomBinding::role() const noexcept { return role_; }

QString ListeningRoomBinding::state() const {
    if (!available())
        return QStringLiteral("unavailable");
    if (busy_)
        return QStringLiteral("preparing");
    if (role_ == HostRole)
        return QStringLiteral("open");
    if (role_ == GuestRole && guest_) {
        switch (guest_->state()) {
        case GuestRoomState::Connecting:
            return QStringLiteral("connecting");
        case GuestRoomState::WaitingApproval:
            return QStringLiteral("waiting");
        case GuestRoomState::Joined:
            return QStringLiteral("joined");
        case GuestRoomState::Reconnecting:
            return QStringLiteral("reconnecting");
        case GuestRoomState::Idle:
        case GuestRoomState::Ended:
            break;
        }
    }
    return QStringLiteral("idle");
}

bool ListeningRoomBinding::busy() const noexcept { return busy_; }
QString ListeningRoomBinding::inviteLink() const { return invite_link_; }
QAbstractItemModel* ListeningRoomBinding::members() noexcept { return &members_; }
QAbstractItemModel* ListeningRoomBinding::pendingRequests() noexcept { return &pending_; }
QAbstractItemModel* ListeningRoomBinding::queue() noexcept { return &queue_; }
int ListeningRoomBinding::memberCount() const noexcept { return members_.rowCount(); }
QString ListeningRoomBinding::hostNickname() const { return host_nickname_; }

int ListeningRoomBinding::maxListeners() const noexcept {
    if (role_ == HostRole && host_ && open_options_.until_revoked) {
        if (const auto persisted = host_->persistedInvite(); persisted.has_value())
            return static_cast<int>(persisted->max_listeners);
    }
    return static_cast<int>(open_options_.max_listeners);
}

bool ListeningRoomBinding::untilRevoked() const noexcept { return open_options_.until_revoked; }
QString ListeningRoomBinding::errorText() const { return error_text_; }

bool ListeningRoomBinding::privacyNoticeRequired() const {
    return dependencies_.privacy_notice_shown && !dependencies_.privacy_notice_shown();
}

int ListeningRoomBinding::minimumListeners() noexcept { return static_cast<int>(minimum_room_listeners); }
int ListeningRoomBinding::maximumListeners() noexcept { return static_cast<int>(maximum_room_listeners); }
int ListeningRoomBinding::defaultListeners() noexcept { return static_cast<int>(default_room_listeners); }

bool ListeningRoomBinding::openRoom(const int maxListeners, const bool untilRevoked) {
    if (!available() || role_ != NoRole || busy_ || maxListeners < minimumListeners() ||
        maxListeners > maximumListeners())
        return false;
    if (!currentProfile()) {
        setError(tr("Crie seu perfil antes de usar a sala."));
        publish();
        return false;
    }
    // The initial role chosen in an earlier room of this session is kept (the host checks it exists).
    open_options_ = RoomOpenOptions{.max_listeners = static_cast<std::size_t>(maxListeners),
                                    .until_revoked = untilRevoked,
                                    .default_role_id = open_options_.default_role_id};
    error_text_.clear();
    ensureNetwork(Pending::Open);
    return true;
}

void ListeningRoomBinding::closeRoom() {
    if (busy_ && pending_action_ == Pending::Open) {
        busy_ = false;
        pending_action_ = Pending::None;
    }
    if (role_ == HostRole)
        endHost();
    publish();
}

void ListeningRoomBinding::revoke() {
    if (role_ != HostRole || !host_)
        return;
    host_->revoke();
    endHost();
    publish();
}

bool ListeningRoomBinding::copyLink() {
    if (invite_link_.isEmpty() || !dependencies_.copy_text)
        return false;
    dependencies_.copy_text(invite_link_);
    return true;
}

bool ListeningRoomBinding::approve(const QString& fingerprint) {
    if (role_ != HostRole || !host_ || !host_->approve(fingerprint.toStdString()))
        return false;
    refreshMembers();
    publish();
    return true;
}

bool ListeningRoomBinding::reject(const QString& fingerprint) {
    if (role_ != HostRole || !host_ || !host_->reject(fingerprint.toStdString()))
        return false;
    refreshMembers();
    publish();
    return true;
}

bool ListeningRoomBinding::join(const QString& link) {
    if (!available() || role_ != NoRole || busy_)
        return false;
    const auto invite = parseInviteLink(link.trimmed().toStdString(), dependencies_.signaling_origin.toStdString());
    if (!invite.has_value()) {
        setError(tr("Link de convite inválido"));
        publish();
        return false;
    }
    if (!currentProfile()) {
        setError(tr("Crie seu perfil antes de usar a sala."));
        publish();
        return false;
    }
    join_invite_ = invite;
    error_text_.clear();
    ensureNetwork(Pending::Join);
    return true;
}

void ListeningRoomBinding::leave() {
    if (busy_ && pending_action_ == Pending::Join) {
        busy_ = false;
        pending_action_ = Pending::None;
    }
    if (role_ == GuestRole && guest_) {
        guest_->leave();
        role_ = NoRole;
        retireGuest();
        refreshMembers();
    }
    publish();
}

void ListeningRoomBinding::acknowledgePrivacyNotice() {
    if (dependencies_.mark_privacy_notice_shown && privacyNoticeRequired())
        dependencies_.mark_privacy_notice_shown();
    publish();
}

void ListeningRoomBinding::dismissError() {
    if (error_text_.isEmpty())
        return;
    error_text_.clear();
    publish();
}

QAbstractItemModel* ListeningRoomBinding::roles() noexcept { return &roles_; }
QAbstractItemModel* ListeningRoomBinding::knownPeople() noexcept { return &known_people_; }

int ListeningRoomBinding::initialRoleId() const {
    return role_ == HostRole && host_ ? static_cast<int>(host_->initialRoleId()) : 0;
}

int ListeningRoomBinding::permissionCount() noexcept { return static_cast<int>(room_permission_count); }

int ListeningRoomBinding::permissions() const {
    if (role_ != GuestRole || !guest_ || guest_->state() != GuestRoomState::Joined)
        return 0;
    return static_cast<int>(guest_->permissions().to_ulong());
}

bool ListeningRoomBinding::guestMay(const RoomPermission permission) const {
    return (permissions() & (1 << static_cast<int>(permission))) != 0;
}

bool ListeningRoomBinding::canSkip() const { return guestMay(RoomPermission::Skip); }
bool ListeningRoomBinding::canPause() const { return guestMay(RoomPermission::Pause); }
bool ListeningRoomBinding::canEditQueue() const { return guestMay(RoomPermission::EditQueue); }
bool ListeningRoomBinding::canAddFromRoom() const { return guestMay(RoomPermission::AddFromRoom); }
bool ListeningRoomBinding::canAddPlaylist() const { return guestMay(RoomPermission::AddPlaylist); }

QVariantList ListeningRoomBinding::roomPlaylists() const {
    QVariantList playlists;
    for (const auto& playlist : room_catalog_.playlists) {
        QVariantList entries;
        for (const auto& entry : playlist.entries)
            entries.append(QVariantMap{{QStringLiteral("entryId"), QString::number(entry.id)},
                                       {QStringLiteral("title"), QString::fromStdString(entry.summary.title)},
                                       {QStringLiteral("artist"), QString::fromStdString(entry.summary.artist)},
                                       {QStringLiteral("durationMs"), static_cast<double>(entry.summary.duration_ms)}});
        playlists.append(QVariantMap{{QStringLiteral("playlistId"), QString::number(playlist.id)},
                                     {QStringLiteral("name"), QString::fromStdString(playlist.name)},
                                     {QStringLiteral("entries"), entries}});
    }
    return playlists;
}

bool ListeningRoomBinding::roomHasCurrent() const {
    return role_ == GuestRole && guest_ && guest_->state() == GuestRoomState::Joined &&
           guest_->snapshot().playback.current_item.has_value();
}

bool ListeningRoomBinding::roomPlaying() const { return roomHasCurrent() && guest_->snapshot().playback.playing; }

bool ListeningRoomBinding::rosterUnsaved() const { return role_ == HostRole && host_ && host_->rosterUnsaved(); }

// While a guest (or a local session still held for the room), the shared player belongs to the room.
bool ListeningRoomBinding::followsRoom() const { return role_ == GuestRole || holdsLocalSession(); }

bool ListeningRoomBinding::roomAllows(const SystemMediaCommandKind kind) const {
    if (role_ != GuestRole)
        return false;
    switch (kind) {
    case SystemMediaCommandKind::Play:
        return canPause() && roomHasCurrent() && !roomPlaying();
    case SystemMediaCommandKind::Pause:
        return canPause() && roomPlaying();
    case SystemMediaCommandKind::Previous:
    case SystemMediaCommandKind::Next:
        return canSkip();
    case SystemMediaCommandKind::Seek:
        return canSkip() && roomHasCurrent();
    }
    return false;
}

bool ListeningRoomBinding::roomCommand(const SystemMediaCommand& command) {
    if (!roomAllows(command.kind))
        return false;
    switch (command.kind) {
    case SystemMediaCommandKind::Play:
    case SystemMediaCommandKind::Pause:
        return roomTogglePlayback();
    case SystemMediaCommandKind::Previous:
        return roomPrevious();
    case SystemMediaCommandKind::Next:
        return roomNext();
    case SystemMediaCommandKind::Seek:
        return sendGuestIntent(
            RoomPermission::Skip,
            RoomIntent{.kind = RoomIntentKind::Seek, .position_ms = std::max<std::int64_t>(command.position_ms, 0)});
    }
    return false;
}

// Every roster change goes through the host (saved, then sent to the members) and refreshes the page.
bool ListeningRoomBinding::hostEdited(const bool done) {
    if (!done)
        return false;
    refreshMembers();
    publish();
    return true;
}

int ListeningRoomBinding::createRole(const QString& name, const int permissions) {
    if (role_ != HostRole || !host_ || permissions < 0 || permissions >= (1 << room_permission_count))
        return 0;
    const auto id =
        host_->createRole(name.toStdString(), RoomPermissionSet{static_cast<unsigned long long>(permissions)});
    return hostEdited(id.has_value()) ? static_cast<int>(*id) : 0;
}

bool ListeningRoomBinding::renameRole(const int roleId, const QString& name) {
    return role_ == HostRole && host_ && roleId > 0 &&
           hostEdited(host_->renameRole(static_cast<std::uint64_t>(roleId), name.toStdString()));
}

bool ListeningRoomBinding::setRolePermission(const int roleId, const int permission, const bool allowed) {
    if (role_ != HostRole || !host_ || roleId <= 0 || permission < 0 || permission >= permissionCount())
        return false;
    const auto roster = host_->roster();
    const auto role = std::find_if(roster.roles.begin(), roster.roles.end(),
                                   [&](const RoomRole& r) { return r.id == static_cast<std::uint64_t>(roleId); });
    if (role == roster.roles.end())
        return false;
    auto permissions = role->permissions;
    permissions.set(static_cast<std::size_t>(permission), allowed);
    return hostEdited(host_->setRolePermissions(role->id, permissions));
}

int ListeningRoomBinding::removeRole(const int roleId) {
    if (role_ != HostRole || !host_ || roleId <= 0)
        return RemovalRefused;
    switch (host_->removeRole(static_cast<std::uint64_t>(roleId))) {
    case RoleRemoval::Removed:
        hostEdited(true);
        return RemovalDone;
    case RoleRemoval::InitialRole:
        return RemovalInitialRole;
    case RoleRemoval::Refused:
        break;
    }
    return RemovalRefused;
}

bool ListeningRoomBinding::setInitialRole(const int roleId) {
    if (role_ != HostRole || !host_ || roleId <= 0 || !host_->setInitialRole(static_cast<std::uint64_t>(roleId)))
        return false;
    open_options_.default_role_id = static_cast<std::uint64_t>(roleId);
    return hostEdited(true);
}

bool ListeningRoomBinding::assignRole(const QString& fingerprint, const int roleId) {
    return role_ == HostRole && host_ && roleId > 0 &&
           hostEdited(host_->assignRole(fingerprint.toStdString(), static_cast<std::uint64_t>(roleId)));
}

bool ListeningRoomBinding::setOverride(const QString& fingerprint, const int permission, const int value) {
    if (role_ != HostRole || !host_ || permission < 0 || permission >= permissionCount() || value < -1 || value > 1)
        return false;
    const auto allowed = value < 0 ? std::optional<bool>{} : std::optional<bool>{value == 1};
    return hostEdited(host_->setOverride(fingerprint.toStdString(), static_cast<RoomPermission>(permission), allowed));
}

bool ListeningRoomBinding::kick(const QString& fingerprint) {
    return role_ == HostRole && host_ && hostEdited(host_->kick(fingerprint.toStdString(), false));
}

bool ListeningRoomBinding::block(const QString& fingerprint) {
    return role_ == HostRole && host_ && hostEdited(host_->kick(fingerprint.toStdString(), true));
}

bool ListeningRoomBinding::unblock(const QString& fingerprint) {
    return role_ == HostRole && host_ && hostEdited(host_->unblock(fingerprint.toStdString()));
}

QVariantMap ListeningRoomBinding::person(const QString& fingerprint) const {
    const QVariantMap unknown{{QStringLiteral("known"), false}};
    if (role_ != HostRole || !host_)
        return unknown;
    const auto roster = host_->roster();
    const auto key = fingerprint.toStdString();
    const auto found = std::find_if(roster.listeners.begin(), roster.listeners.end(),
                                    [&](const KnownListener& l) { return l.fingerprint == key; });
    if (found == roster.listeners.end())
        return unknown;
    QVariantList overrides;
    for (std::size_t permission = 0; permission < room_permission_count; ++permission) {
        const auto set =
            std::find_if(found->overrides.begin(), found->overrides.end(), [&](const PermissionOverride& o) {
                return static_cast<std::size_t>(o.permission) == permission;
            });
        overrides.push_back(set == found->overrides.end() ? -1 : (set->allowed ? 1 : 0));
    }
    return {{QStringLiteral("known"), true},
            {QStringLiteral("roleId"), static_cast<int>(found->role_id)},
            {QStringLiteral("blocked"), found->blocked},
            {QStringLiteral("overrides"), overrides}};
}

bool ListeningRoomBinding::sendGuestIntent(const RoomPermission permission, const RoomIntent& intent) {
    return guestMay(permission) && guest_->sendIntent(intent) != 0;
}

bool ListeningRoomBinding::roomNext() {
    return sendGuestIntent(RoomPermission::Skip, RoomIntent{.kind = RoomIntentKind::Next});
}

bool ListeningRoomBinding::roomPrevious() {
    return sendGuestIntent(RoomPermission::Skip, RoomIntent{.kind = RoomIntentKind::Previous});
}

bool ListeningRoomBinding::roomTogglePlayback() {
    return roomHasCurrent() &&
           sendGuestIntent(RoomPermission::Pause,
                           RoomIntent{.kind = roomPlaying() ? RoomIntentKind::Pause : RoomIntentKind::Resume});
}

bool ListeningRoomBinding::roomSeekBy(const int deltaMs) {
    if (!roomHasCurrent())
        return false;
    std::int64_t position = guest_->snapshot().playback.anchor_position_ms;
    if (local_session_released_ && dependencies_.guest_backend != nullptr)
        position = dependencies_.guest_backend->snapshot().position_ms;
    return sendGuestIntent(
        RoomPermission::Skip,
        RoomIntent{.kind = RoomIntentKind::Seek, .position_ms = std::max<std::int64_t>(position + deltaMs, 0)});
}

bool ListeningRoomBinding::roomRemove(const QString& itemId) {
    bool valid = false;
    const auto id = itemId.toULongLong(&valid);
    return valid &&
           sendGuestIntent(RoomPermission::EditQueue, RoomIntent{.kind = RoomIntentKind::RemoveItem, .item_id = id});
}

bool ListeningRoomBinding::roomMove(const QString& itemId, const int destination) {
    bool valid = false;
    const auto id = itemId.toULongLong(&valid);
    return valid && destination >= 0 &&
           sendGuestIntent(RoomPermission::EditQueue, RoomIntent{.kind = RoomIntentKind::MoveItem,
                                                                 .item_id = id,
                                                                 .destination = static_cast<std::size_t>(destination)});
}

bool ListeningRoomBinding::roomAddItem(const QString& entryId) {
    bool valid = false;
    const auto id = entryId.toULongLong(&valid);
    return valid && sendGuestIntent(RoomPermission::AddFromRoom,
                                    RoomIntent{.kind = RoomIntentKind::AddItems, .catalog_entry_ids = {id}});
}

bool ListeningRoomBinding::roomAddPlaylist(const QString& playlistId) {
    bool valid = false;
    const auto id = playlistId.toULongLong(&valid);
    return valid && sendGuestIntent(RoomPermission::AddPlaylist,
                                    RoomIntent{.kind = RoomIntentKind::AddPlaylist, .playlist_id = id});
}

void ListeningRoomBinding::playlistsChanged() {
    if (role_ == HostRole && host_ && dependencies_.playlists)
        host_->setPlaylists(dependencies_.playlists());
}

void ListeningRoomBinding::guestRejected(const IntentRejection reason) {
    if (reason == IntentRejection::MissingPermission)
        setError(tr("Sem permissão"));
    else if (reason == IntentRejection::QueueFull)
        setError(tr("A fila da sala está cheia."));
    else
        return;
    publish();
}

void ListeningRoomBinding::shutdown() {
    busy_ = false;
    pending_action_ = Pending::None;
    if (role_ == GuestRole)
        leave();
    if (role_ == HostRole)
        endHost();
    publish();
}

SessionSnapshot ListeningRoomBinding::sessionForAutosave(SessionSnapshot live) const {
    if (host_)
        return host_->withoutGuestItems(std::move(live));
    if (guest_ && guest_->holdsLocalSession())
        return guest_->sessionForAutosave(std::move(live));
    if (pending_give_back_)
        return *pending_give_back_;
    return live;
}

bool ListeningRoomBinding::holdsLocalSession() const {
    return (guest_ && guest_->holdsLocalSession()) || pending_give_back_.has_value();
}

ListeningRoomSchedule ListeningRoomBinding::scheduler(QObject* context) {
    return [guard = QPointer<QObject>{context}](const std::int64_t delay_ms, std::function<void()> task) {
        if (!guard || !task)
            return;
        const auto bounded = std::clamp<std::int64_t>(delay_ms, 0, std::numeric_limits<int>::max());
        QTimer::singleShot(std::chrono::milliseconds{bounded}, guard.data(), std::move(task));
    };
}

std::optional<std::string> ListeningRoomBinding::fileSha256(const QString& path) {
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    QCryptographicHash hash{QCryptographicHash::Sha256};
    if (!hash.addData(&file))
        return std::nullopt;
    return hash.result().toHex().toStdString();
}

std::optional<ListenerProfile> ListeningRoomBinding::currentProfile() const {
    if (!dependencies_.profile)
        return std::nullopt;
    auto profile = dependencies_.profile();
    if (!profile.has_value() || !isValidListenerProfile(*profile))
        return std::nullopt;
    return profile;
}

void ListeningRoomBinding::ensureNetwork(const Pending action) {
    if (network_.has_value()) {
        if (action == Pending::Open)
            startHost();
        else if (action == Pending::Join)
            startGuest();
        publish();
        return;
    }
    busy_ = true;
    pending_action_ = action;
    const auto generation = ++load_generation_;
    publish();
    if (!dependencies_.load_network) {
        networkLoaded(generation, std::nullopt);
        return;
    }
    const QPointer<ListeningRoomBinding> self{this};
    dependencies_.load_network([self, generation](std::optional<ListeningRoomNetwork> network) {
        if (self)
            self->networkLoaded(generation, std::move(network));
    });
}

void ListeningRoomBinding::networkLoaded(const std::uint64_t generation, std::optional<ListeningRoomNetwork> network) {
    if (generation != load_generation_ || network_.has_value())
        return;
    const bool was_busy = busy_;
    busy_ = false;
    const auto action = std::exchange(pending_action_, Pending::None);
    if (!network.has_value() || !network->host_endpoint || !network->guest_connector) {
        if (was_busy && action != Pending::None)
            setError(tr("Não foi possível preparar a identidade da sala neste computador. Tente de novo."));
        publish();
        return;
    }
    network_ = std::move(network);
    if (action == Pending::Open)
        startHost();
    else if (action == Pending::Join)
        startGuest();
    publish();
}

void ListeningRoomBinding::startHost() {
    const auto profile = currentProfile();
    if (!profile) {
        setError(tr("Crie seu perfil antes de usar a sala."));
        return;
    }
    host_ = std::make_unique<ListeningRoomHost>(ListeningRoomHostDependencies{
        .roster_store = *dependencies_.roster_store,
        .player = *dependencies_.host_player,
        .endpoint = *network_->host_endpoint,
        .catalog = *dependencies_.catalog,
        .host_clock_ms = dependencies_.clock_ms,
        .random_bytes = dependencies_.random_bytes,
        .host_fingerprint = network_->fingerprint,
        .host_profile = *profile,
        .schedule = dependencies_.schedule,
        .files = dependencies_.file_outbox,
    });
    host_->onChanged = [this](const RoomSnapshot&) { hostChanged(); };
    host_->onDriftsChanged = [this] { refreshMembers(); };
    host_->onRosterUnsavedChanged = [this] { publish(); };
    host_->onJoinRequest = [this](const RoomMember& member) { hostJoinRequest(member); };
    host_->onEndpointFailed = [this](const RoomLinkFailure failure) { hostEndpointFailed(failure); };
    if (dependencies_.playlists)
        host_->setPlaylists(dependencies_.playlists());
    if (dependencies_.set_room_play_order)
        dependencies_.set_room_play_order(true);
    role_ = HostRole;
    host_nickname_ = QString::fromStdString(profile->nickname);
    const auto invite = host_->open(open_options_);
    if (!invite.has_value()) {
        endHost();
        setError(tr("Não foi possível abrir a sala. Tente de novo."));
        return;
    }
    invite_link_ = QString::fromStdString(formatInviteLink(*invite, dependencies_.signaling_origin.toStdString()));
    refreshMembers();
    if (dependencies_.session_changed)
        dependencies_.session_changed();
}

void ListeningRoomBinding::startGuest() {
    if (!join_invite_.has_value())
        return;
    if (join_invite_->host_fingerprint == network_->fingerprint) {
        setError(tr("Este convite é da sua própria sala."));
        return;
    }
    const auto profile = currentProfile();
    if (!profile) {
        setError(tr("Crie seu perfil antes de usar a sala."));
        return;
    }
    // The room waits for the local queue to let go of the player.
    if (follower_)
        follower_->setHeld(true);
    guest_ = std::make_unique<ListeningRoomGuest>(ListeningRoomGuestDependencies{
        .connector = *network_->guest_connector,
        .profile = *profile,
        .stored_custom_avatar_sha256 = dependencies_.custom_avatar_sha256,
        // A session still waiting to go back is the pre-room session of the next room too.
        .capture_session =
            [this] {
                if (auto pending = std::exchange(pending_give_back_, std::nullopt)) {
                    give_back_timer_.stop();
                    return *pending;
                }
                return dependencies_.capture_session();
            },
        .restore_session = [this](const SessionSnapshot& saved) { giveBackLocalSession(saved); },
        .schedule = dependencies_.schedule,
        .files = dependencies_.file_inbox,
        .follower = follower_.get(),
        .local_clock_ms = dependencies_.clock_ms,
    });
    guest_->onChanged = [this] { guestChanged(); };
    guest_->onEnded = [this](RoomLinkFailure) { guestEnded(); };
    guest_->onRejected = [this](std::uint64_t, const IntentRejection reason) { guestRejected(reason); };
    role_ = GuestRole;
    host_nickname_.clear();
    guest_->join(*join_invite_);
    refreshMembers();
}

void ListeningRoomBinding::hostChanged() {
    refreshMembers();
    if (dependencies_.session_changed)
        dependencies_.session_changed();
    publish();
}

void ListeningRoomBinding::hostJoinRequest(const RoomMember& member) {
    refreshMembers();
    publish();
    const bool active = !dependencies_.app_active || dependencies_.app_active();
    if (!active && dependencies_.notify) {
        dependencies_.notify(
            tr("Pedido para entrar na sala").toStdString(),
            tr("%1 quer entrar na sua sala.").arg(QString::fromStdString(member.nickname)).toStdString());
    }
}

void ListeningRoomBinding::hostEndpointFailed(const RoomLinkFailure failure) {
    QString message;
    switch (failure) {
    case RoomLinkFailure::Refused:
        message = tr("A sala não está mais disponível. Crie uma nova sala.");
        break;
    case RoomLinkFailure::VersionMismatch:
        message = tr("O servidor da sala usa outra versão do RC Music. Atualize o app para abrir salas.");
        break;
    case RoomLinkFailure::SignalingUnavailable:
    case RoomLinkFailure::HostOffline:
    case RoomLinkFailure::Unreachable:
    case RoomLinkFailure::Closed:
        message = tr("Não foi possível conectar à internet para abrir a sala. Verifique a conexão e tente de novo.");
        break;
    }
    // Never close or destroy the host from inside its own callback.
    const auto host = host_.get();
    QMetaObject::invokeMethod(
        this,
        [this, host, message] {
            if (role_ != HostRole || host_.get() != host)
                return;
            endHost();
            setError(message);
            publish();
        },
        Qt::QueuedConnection);
}

void ListeningRoomBinding::guestChanged() {
    // The guest captured its pre-room session: the local queue leaves the shared player before the room
    // plays on it. Once per room; a reconnect keeps the same saved session.
    if (follower_ && !position_timer_.isActive() && !local_session_released_ && guest_ && guest_->holdsLocalSession()) {
        position_timer_.start();
        takeOverPlayer();
    }
    refreshMembers();
    publish();
}

void ListeningRoomBinding::followTick() {
    if (follower_ && guest_)
        follower_->tick();
}

// One owner of the player at a time: until the local queue lets go of it (it refuses while busy), the
// follower is held and the binding asks again on every position tick.
void ListeningRoomBinding::takeOverPlayer() {
    if (!follower_ || !guest_ || local_session_released_)
        return;
    if (dependencies_.release_local_session && !dependencies_.release_local_session())
        return;
    local_session_released_ = true;
    follower_->setHeld(false);
    follow_timer_.start();
}

void ListeningRoomBinding::feedPosition() {
    if (!local_session_released_) {
        takeOverPlayer();
        return;
    }
    if (!follower_ || !guest_ || !dependencies_.room_position)
        return;
    feeding_position_ = true;
    dependencies_.room_position(std::max<std::int64_t>(dependencies_.guest_backend->snapshot().position_ms, 0));
}

// The guest's pre-room session goes back to the local player. A refusal (the player is busy) never loses
// it: it stays held for the autosave and is offered again until accepted.
void ListeningRoomBinding::giveBackLocalSession(const SessionSnapshot& saved) {
    pending_give_back_ = saved;
    retryGiveBack();
}

void ListeningRoomBinding::retryGiveBack() {
    if (!pending_give_back_)
        return;
    if (!dependencies_.restore_session(*pending_give_back_)) {
        give_back_timer_.start();
        return;
    }
    pending_give_back_.reset();
    give_back_timer_.stop();
    if (dependencies_.session_changed)
        dependencies_.session_changed();
}

void ListeningRoomBinding::stopFollowing() {
    follow_timer_.stop();
    position_timer_.stop();
    local_session_released_ = false;
    if (follower_)
        follower_->setHeld(true);
    if (std::exchange(feeding_position_, false) && dependencies_.room_position)
        dependencies_.room_position(std::nullopt);
}

void ListeningRoomBinding::guestEnded() {
    const auto end = guest_ ? guest_->endReason() : std::nullopt;
    QString message = tr("A conexão com a sala caiu.");
    if (end.has_value()) {
        switch (end->reason) {
        case GuestEndReason::RoomFull:
            message = tr("A sala está cheia");
            break;
        case GuestEndReason::Blocked:
            message = tr("Você não pode entrar nesta sala.");
            break;
        case GuestEndReason::Rejected:
            message = tr("O anfitrião recusou o seu pedido.");
            break;
        case GuestEndReason::Closed:
            message = tr("O anfitrião encerrou a sala.");
            break;
        case GuestEndReason::Revoked:
            message = tr("O anfitrião cancelou este convite.");
            break;
        case GuestEndReason::Kicked:
            message = tr("Você foi removido da sala.");
            break;
        case GuestEndReason::VersionMismatch:
            message = tr("A sala usa outra versão do RC Music. Atualize o app para entrar.");
            break;
        case GuestEndReason::LinkLost:
            switch (end->failure) {
            case RoomLinkFailure::HostOffline:
                message = tr("O anfitrião não está com a sala aberta agora.");
                break;
            case RoomLinkFailure::SignalingUnavailable:
                message = tr("Não foi possível conectar ao servidor da sala. Verifique a conexão e tente de novo.");
                break;
            case RoomLinkFailure::Unreachable:
                message = tr("Não foi possível conectar ao anfitrião. Verifique a conexão e tente de novo.");
                break;
            case RoomLinkFailure::Refused:
                message = tr("A sala recusou a conexão. Confira o convite com quem criou a sala.");
                break;
            case RoomLinkFailure::VersionMismatch:
                message = tr("A sala usa outra versão do RC Music. Atualize o app para entrar.");
                break;
            case RoomLinkFailure::Closed:
                break;
            }
            break;
        }
    }
    role_ = NoRole;
    host_nickname_.clear();
    retireGuest();
    refreshMembers();
    setError(message);
    publish();
}

void ListeningRoomBinding::endHost() {
    if (host_) {
        host_->close();
        host_.reset();
    }
    if (dependencies_.set_room_play_order)
        dependencies_.set_room_play_order(false);
    role_ = NoRole;
    invite_link_.clear();
    host_nickname_.clear();
    refreshMembers();
    if (dependencies_.session_changed)
        dependencies_.session_changed();
}

void ListeningRoomBinding::retireGuest() {
    if (!guest_)
        return;
    stopFollowing();
    // This may run inside the guest's own callback: destroy it from the event loop instead.
    guest_->onChanged = nullptr;
    guest_->onEnded = nullptr;
    guest_->onRejected = nullptr;
    retired_guests_.push_back(std::move(guest_));
    QMetaObject::invokeMethod(this, [this] { retired_guests_.clear(); }, Qt::QueuedConnection);
}

// The guest's room queue (which files are here yet), the host's playlists and its lyrics for the current item.
void ListeningRoomBinding::refreshGuestRoom() {
    auto catalog = role_ == GuestRole && guest_ ? guest_->catalog() : RoomCatalog{};
    if (catalog != room_catalog_) {
        room_catalog_ = std::move(catalog);
        emit catalogChanged();
    }
    QString lyrics;
    if (role_ == GuestRole && guest_) {
        const auto* const guest = guest_.get();
        queue_.replace(guest->snapshot(),
                       [guest](const std::string_view sha256) { return guest->localFile(sha256).has_value(); });
        if (const auto current = guest->lyrics())
            lyrics = QString::fromStdString(current->lrc_text);
    } else {
        queue_.replace({}, {});
    }
    if (lyrics == room_lyrics_)
        return;
    room_lyrics_ = lyrics;
    if (dependencies_.room_lyrics)
        dependencies_.room_lyrics(room_lyrics_);
}

void ListeningRoomBinding::setError(QString text) { error_text_ = std::move(text); }

// The host's roles and known people; empty outside a hosted room.
void ListeningRoomBinding::refreshRoster() {
    if (role_ == HostRole && host_) {
        const auto roster = host_->roster();
        roles_.replace(roster, host_->initialRoleId());
        known_people_.replace(roster, host_->snapshot().members);
        return;
    }
    roles_.replace({}, 0);
    known_people_.replace({}, {});
}

void ListeningRoomBinding::refreshMembers() {
    refreshGuestRoom();
    refreshRoster();
    const auto self = network_.has_value() ? lowerHex(network_->fingerprint) : std::string{};
    if (role_ == HostRole && host_) {
        std::vector<RoomMember> admitted;
        std::vector<RoomMember> pending;
        for (auto& member : host_->snapshot().members)
            (member.state == MemberState::Pending ? pending : admitted).push_back(std::move(member));
        members_.replace(std::move(admitted), self, self, host_->memberDrifts());
        pending_.replace(std::move(pending), self, self);
        return;
    }
    if (role_ == GuestRole && guest_ && join_invite_.has_value()) {
        const auto host = lowerHex(join_invite_->host_fingerprint);
        auto members = guest_->snapshot().members;
        host_nickname_.clear();
        for (const auto& member : members) {
            if (member.fingerprint == host)
                host_nickname_ = QString::fromStdString(member.nickname);
        }
        members_.replace(std::move(members), host, self);
        pending_.replace({}, {}, {});
        return;
    }
    members_.replace({}, {}, {});
    pending_.replace({}, {}, {});
}

void ListeningRoomBinding::publish() { emit changed(); }

} // namespace music_player::m0
