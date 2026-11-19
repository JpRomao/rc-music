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
    : QObject(parent), dependencies_(std::move(dependencies)), members_(this), pending_(this), queue_(this) {
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
    open_options_ =
        RoomOpenOptions{.max_listeners = static_cast<std::size_t>(maxListeners), .until_revoked = untilRevoked};
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
    host_->onJoinRequest = [this](const RoomMember& member) { hostJoinRequest(member); };
    host_->onEndpointFailed = [this](const RoomLinkFailure failure) { hostEndpointFailed(failure); };
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
    retired_guests_.push_back(std::move(guest_));
    QMetaObject::invokeMethod(this, [this] { retired_guests_.clear(); }, Qt::QueuedConnection);
}

// The guest's room queue (which files are here yet) and the host's lyrics for the current item.
void ListeningRoomBinding::refreshGuestRoom() {
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

void ListeningRoomBinding::refreshMembers() {
    refreshGuestRoom();
    const auto self = network_.has_value() ? lowerHex(network_->fingerprint) : std::string{};
    if (role_ == HostRole && host_) {
        std::vector<RoomMember> admitted;
        std::vector<RoomMember> pending;
        for (auto& member : host_->snapshot().members)
            (member.state == MemberState::Pending ? pending : admitted).push_back(std::move(member));
        members_.replace(std::move(admitted), self, self);
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
