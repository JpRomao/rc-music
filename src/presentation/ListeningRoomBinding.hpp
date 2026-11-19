#pragma once

#include "m0/application/ListeningRoomGuest.hpp"
#include "m0/application/ListeningRoomHost.hpp"
#include "m0/application/RoomPlaybackFollower.hpp"
#include "m0/domain/ListenerProfile.hpp"
#include "m0/domain/SessionSnapshot.hpp"
#include "m0/ports/PlaybackBackend.hpp"
#include "m0/ports/RoomFiles.hpp"
#include "m0/ports/RoomHostPlayer.hpp"
#include "m0/ports/RoomRosterStore.hpp"
#include "m0/ports/RoomTrackCatalog.hpp"
#include "m0/ports/RoomTransport.hpp"
#include "presentation/RoomMembersModel.hpp"
#include "presentation/RoomQueueModel.hpp"
#include "presentation/RoomRolesModel.hpp"

#include <QAbstractItemModel>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace music_player::m0 {

// The network side of the listening room, built once the device's room identity is loaded.
struct ListeningRoomNetwork final {
    std::array<std::uint8_t, 32> fingerprint{};
    std::unique_ptr<RoomHostEndpoint> host_endpoint;
    std::unique_ptr<RoomGuestConnector> guest_connector;
};

using ListeningRoomSchedule = std::function<void(std::int64_t delay_ms, std::function<void()> task)>;

struct ListeningRoomBindingDependencies final {
    // "https://<worker>" of the signaling relay. Empty: the room is unavailable in this build and
    // nothing network-related is ever built (load_network is never called).
    QString signaling_origin;
    // Loads the identity off the UI thread (it can take seconds) and calls done ONCE on the binding's
    // thread, with nullopt when the identity cannot be loaded or created. Called at most once at a time.
    std::function<void(std::function<void(std::optional<ListeningRoomNetwork>)> done)> load_network;
    // Required when signaling_origin is set; they must outlive the binding.
    RoomHostPlayer* host_player{nullptr};
    RoomTrackCatalog* catalog{nullptr};
    RoomRosterStore* roster_store{nullptr};
    // The local profile when it is set up and valid.
    std::function<std::optional<ListenerProfile>()> profile;
    // SHA-256 (lowercase hex) of <config>/avatars/custom.png, nullopt when missing or unreadable.
    std::function<std::optional<std::string>()> custom_avatar_sha256;
    // The guest's seam to the local queue (PlayerPresenter::liveSessionSnapshot / restoreRoomSession).
    // restore_session returns false when the local player refuses it for now (busy); the binding keeps
    // the session, still holds it for the autosave and tries again every room_give_back_retry_ms.
    std::function<SessionSnapshot()> capture_session;
    std::function<bool(const SessionSnapshot&)> restore_session;
    // PlayerPresenter::setRoomPlayOrder while a hosted room is open.
    std::function<void(bool)> set_room_play_order;
    // PlayerPresenter::refreshRoomSession: what sessionForAutosave filters may have changed.
    std::function<void()> session_changed;
    std::function<std::int64_t()> clock_ms;
    std::function<void(std::span<std::uint8_t>)> random_bytes;
    // FR-058: the one-time IP privacy notice, kept with the preferences.
    std::function<bool()> privacy_notice_shown;
    std::function<void()> mark_privacy_notice_shown;
    // Whether the app window is active; a join request while it is not raises a system notification.
    std::function<bool()> app_active;
    std::function<void(const std::string& title, const std::string& message)> notify;
    std::function<void(const QString& text)> copy_text;
    // Defaults to scheduler(binding).
    ListeningRoomSchedule schedule;
    // Room files (S8.2): the host streams what guests ask for; a guest keeps them in its room cache.
    // nullptr: no files move. They must outlive the binding.
    RoomFileOutbox* file_outbox{nullptr};
    RoomFileInbox* file_inbox{nullptr};
    // A guest's lyrics panel: the host's LRC for the current item, empty when there is none (or when
    // this app is not a guest any more).
    std::function<void(const QString& lrc)> room_lyrics;
    // S9.1: the app's one player, where a guest plays the room in time with the host. nullptr: a guest
    // follows the room's queue and plays nothing (tests and the E2E stay silent). It must outlive the
    // binding.
    PlaybackBackend* guest_backend{nullptr};
    // Puts the local queue away once a guest holds its pre-room session, so only the room drives the
    // shared player (PlayerPresenter::restoreRoomSession with an empty session). false: the local player
    // is busy and keeps it for now; the room stays held and the binding asks again every
    // room_position_feed_ms. The session comes back from capture_session's copy when the guest leaves.
    // Only with guest_backend.
    std::function<bool()> release_local_session;
    // While a guest plays the room: the player's position every room_position_feed_ms (the lyrics
    // panel follows it), then nullopt once when the guest stops. Only with guest_backend.
    std::function<void(std::optional<std::int64_t> position_ms)> room_position;
};

// "Ouvir junto" for QML (ADR-0022, S7.3): hosts a room (ListeningRoomHost over the real player) or
// joins one (ListeningRoomGuest). Every call and callback runs on the binding's thread. Host and guest
// objects are destroyed only outside their own callbacks. Destroy the binding before the player.
class ListeningRoomBinding final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(int role READ role NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString inviteLink READ inviteLink NOTIFY changed)
    Q_PROPERTY(QAbstractItemModel* members READ members CONSTANT)
    Q_PROPERTY(QAbstractItemModel* pendingRequests READ pendingRequests CONSTANT)
    // The room queue as a guest sees it (empty otherwise).
    Q_PROPERTY(QAbstractItemModel* queue READ queue CONSTANT)
    Q_PROPERTY(int memberCount READ memberCount NOTIFY changed)
    Q_PROPERTY(QString hostNickname READ hostNickname NOTIFY changed)
    Q_PROPERTY(int maxListeners READ maxListeners NOTIFY changed)
    Q_PROPERTY(bool untilRevoked READ untilRevoked NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(bool privacyNoticeRequired READ privacyNoticeRequired NOTIFY changed)
    Q_PROPERTY(int minimumListeners READ minimumListeners CONSTANT)
    Q_PROPERTY(int maximumListeners READ maximumListeners CONSTANT)
    Q_PROPERTY(int defaultListeners READ defaultListeners CONSTANT)
    // S10.1, host: its roles, the people it knows and the invite's initial role (empty and 0 outside a
    // hosted room).
    Q_PROPERTY(QAbstractItemModel* roles READ roles CONSTANT)
    Q_PROPERTY(QAbstractItemModel* knownPeople READ knownPeople CONSTANT)
    Q_PROPERTY(int initialRoleId READ initialRoleId NOTIFY changed)
    Q_PROPERTY(int permissionCount READ permissionCount CONSTANT)
    // S10.1, guest: its permissions in the room's last state (bit i = RoomPermission i) and the room's
    // playback, for the transport and queue controls. All false outside a joined room.
    Q_PROPERTY(int permissions READ permissions NOTIFY changed)
    Q_PROPERTY(bool canSkip READ canSkip NOTIFY changed)
    Q_PROPERTY(bool canPause READ canPause NOTIFY changed)
    Q_PROPERTY(bool canEditQueue READ canEditQueue NOTIFY changed)
    Q_PROPERTY(bool roomPlaying READ roomPlaying NOTIFY changed)
    Q_PROPERTY(bool roomHasCurrent READ roomHasCurrent NOTIFY changed)

  public:
    enum Role { NoRole = 0, HostRole = 1, GuestRole = 2 };
    Q_ENUM(Role)
    enum RoleRemovalResult { RemovalDone = 0, RemovalInitialRole = 1, RemovalRefused = 2 };
    Q_ENUM(RoleRemovalResult)

    explicit ListeningRoomBinding(ListeningRoomBindingDependencies dependencies, QObject* parent = nullptr);
    ~ListeningRoomBinding() override;

    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] int role() const noexcept;
    // "idle", "preparing", "open" (host), "connecting", "waiting", "joined", "reconnecting" (guest).
    [[nodiscard]] QString state() const;
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] QString inviteLink() const;
    [[nodiscard]] QAbstractItemModel* members() noexcept;
    [[nodiscard]] QAbstractItemModel* pendingRequests() noexcept;
    [[nodiscard]] QAbstractItemModel* queue() noexcept;
    [[nodiscard]] int memberCount() const noexcept;
    [[nodiscard]] QString hostNickname() const;
    [[nodiscard]] int maxListeners() const noexcept;
    [[nodiscard]] bool untilRevoked() const noexcept;
    [[nodiscard]] QString errorText() const;
    [[nodiscard]] bool privacyNoticeRequired() const;
    [[nodiscard]] static int minimumListeners() noexcept;
    [[nodiscard]] static int maximumListeners() noexcept;
    [[nodiscard]] static int defaultListeners() noexcept;
    [[nodiscard]] QAbstractItemModel* roles() noexcept;
    [[nodiscard]] QAbstractItemModel* knownPeople() noexcept;
    [[nodiscard]] int initialRoleId() const;
    [[nodiscard]] static int permissionCount() noexcept;
    [[nodiscard]] int permissions() const;
    [[nodiscard]] bool canSkip() const;
    [[nodiscard]] bool canPause() const;
    [[nodiscard]] bool canEditQueue() const;
    [[nodiscard]] bool roomPlaying() const;
    [[nodiscard]] bool roomHasCurrent() const;

    // false when refused at once (limits, no profile, already in a room, unavailable); true when the
    // room opens or is being prepared (identity loading).
    Q_INVOKABLE bool openRoom(int maxListeners, bool untilRevoked);
    Q_INVOKABLE void closeRoom();
    Q_INVOKABLE void revoke();
    Q_INVOKABLE bool copyLink();
    Q_INVOKABLE bool approve(const QString& fingerprint);
    Q_INVOKABLE bool reject(const QString& fingerprint);
    // false with errorText "Link de convite inválido" when the text is not an invite of this build's relay.
    Q_INVOKABLE bool join(const QString& link);
    Q_INVOKABLE void leave();
    Q_INVOKABLE void acknowledgePrivacyNotice();
    Q_INVOKABLE void dismissError();

    // S10.1, host only (false, 0 or RemovalRefused otherwise). Each change goes through the host, which
    // saves it in the roster store and sends the members their new permissions in the next state.
    // permissions: bit i = RoomPermission i. Returns the new role's id, 0 when refused.
    Q_INVOKABLE int createRole(const QString& name, int permissions);
    Q_INVOKABLE bool renameRole(int roleId, const QString& name);
    Q_INVOKABLE bool setRolePermission(int roleId, int permission, bool allowed);
    // RemovalInitialRole: the invite's initial role is never removed (choose another one first).
    Q_INVOKABLE int removeRole(int roleId);
    Q_INVOKABLE bool setInitialRole(int roleId);
    Q_INVOKABLE bool assignRole(const QString& fingerprint, int roleId);
    // value: -1 the role decides, 0 denied, 1 allowed.
    Q_INVOKABLE bool setOverride(const QString& fingerprint, int permission, int value);
    // Removes the person; they may ask to come in again.
    Q_INVOKABLE bool kick(const QString& fingerprint);
    // Removes the person and blocks them: a new join gets RejectedBlocked until unblock().
    Q_INVOKABLE bool block(const QString& fingerprint);
    Q_INVOKABLE bool unblock(const QString& fingerprint);
    // {known, roleId, blocked, overrides: [-1|0|1 per permission]} of a known person; {known: false}
    // otherwise.
    Q_INVOKABLE QVariantMap person(const QString& fingerprint) const;

    // S10.1, guest only: what the transport and queue controls do in a room. Each sends one intent to the
    // host and returns true; false (nothing sent) without the permission or outside a joined room. The
    // host checks it again and a refusal shows "Sem permissão".
    Q_INVOKABLE bool roomNext();
    Q_INVOKABLE bool roomPrevious();
    Q_INVOKABLE bool roomTogglePlayback();
    // From where the room plays now on this player (the anchor before it plays here).
    Q_INVOKABLE bool roomSeekBy(int deltaMs);
    Q_INVOKABLE bool roomRemove(const QString& itemId);
    Q_INVOKABLE bool roomMove(const QString& itemId, int destination);

    // S9.1: the guest's output offset (Preferences::room_output_offset_ms); a positive value plays earlier.
    void setOutputOffsetMs(int offset_ms);

    // App exit, before the final session save: the guest leaves (its pre-room session comes back) and
    // the host closes (guest items leave the queue).
    void shutdown();
    // PlayerPresenterRoomSessionGate: what the session autosave may store, and whether it must keep its
    // position (a guest holding its pre-room session).
    [[nodiscard]] SessionSnapshot sessionForAutosave(SessionSnapshot live) const;
    [[nodiscard]] bool holdsLocalSession() const;

    // A QTimer::singleShot scheduler bound to context: a task never runs after context is destroyed.
    [[nodiscard]] static ListeningRoomSchedule scheduler(QObject* context);
    // Lowercase hex SHA-256 of a file, nullopt when it cannot be read.
    [[nodiscard]] static std::optional<std::string> fileSha256(const QString& path);

  signals:
    void changed();

  private:
    enum class Pending { None, Open, Join };

    [[nodiscard]] std::optional<ListenerProfile> currentProfile() const;
    void ensureNetwork(Pending action);
    void networkLoaded(std::uint64_t generation, std::optional<ListeningRoomNetwork> network);
    void startHost();
    void startGuest();
    void hostChanged();
    void hostJoinRequest(const RoomMember& member);
    void hostEndpointFailed(RoomLinkFailure failure);
    void guestChanged();
    void guestEnded();
    void endHost();
    void retireGuest();
    void giveBackLocalSession(const SessionSnapshot& saved);
    void retryGiveBack();
    void followTick();
    void takeOverPlayer();
    void feedPosition();
    void stopFollowing();
    void setError(QString text);
    void refreshMembers();
    void refreshGuestRoom();
    void refreshRoster();
    bool hostEdited(bool done);
    [[nodiscard]] bool guestMay(RoomPermission permission) const;
    bool sendGuestIntent(RoomPermission permission, const RoomIntent& intent);
    void guestRejected(IntentRejection reason);
    void publish();

    ListeningRoomBindingDependencies dependencies_;
    RoomMembersModel members_;
    RoomMembersModel pending_;
    RoomQueueModel queue_;
    RoomRolesModel roles_;
    RoomKnownPeopleModel known_people_;
    QString room_lyrics_;
    std::optional<ListeningRoomNetwork> network_;
    // S9.1: the guest's player, declared before the guests that use it.
    std::unique_ptr<RoomPlaybackFollower> follower_;
    QTimer follow_timer_;
    QTimer position_timer_;
    bool local_session_released_{false};
    // A pre-room session the local player refused back: kept (and held for the autosave) until accepted.
    std::optional<SessionSnapshot> pending_give_back_;
    QTimer give_back_timer_;
    bool feeding_position_{false};
    std::unique_ptr<ListeningRoomHost> host_;
    std::unique_ptr<ListeningRoomGuest> guest_;
    // Ended guests waiting for the event loop (never destroyed inside their own callbacks). Declared
    // after network_, so they go before the connector they use.
    std::vector<std::unique_ptr<ListeningRoomGuest>> retired_guests_;
    Role role_{NoRole};
    bool busy_{false};
    Pending pending_action_{Pending::None};
    std::uint64_t load_generation_{0};
    RoomOpenOptions open_options_{};
    std::optional<RoomInviteLink> join_invite_;
    QString invite_link_;
    QString host_nickname_;
    QString error_text_;
};

} // namespace music_player::m0
