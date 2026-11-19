#pragma once

#include "m0/domain/ListeningRoom.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QUrl>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace music_player::m0 {

// People in a listening room for QML: admitted members or pending join requests (ADR-0022, S7.3).
class RoomMembersModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

  public:
    enum Roles {
        FingerprintRole = Qt::UserRole + 1,
        NicknameRole,
        AvatarSourceRole,
        HostRole,
        SelfRole,
        HasSyncDriftRole,
        SyncDriftMsRole
    };

    explicit RoomMembersModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // host_fingerprint marks the host's row; self_fingerprint the local listener's.
    // sync_drifts: the host's per-person sync difference in ms (S9.2), keyed by fingerprint; the roles
    // hasSyncDrift/syncDriftMs expose it by row. The fingerprint itself is never shown by the UI.
    void replace(std::vector<RoomMember> members, const std::string& host_fingerprint,
                 const std::string& self_fingerprint,
                 std::map<std::string, std::int64_t, std::less<>> sync_drifts = {});
    [[nodiscard]] const std::vector<RoomMember>& members() const noexcept;
    // Bundled avatars only: a peer's custom image is not fetched yet (S8+), so it shows the first one.
    [[nodiscard]] static QUrl avatarSource(const AvatarChoice& avatar);

  signals:
    void countChanged();

  private:
    std::vector<RoomMember> members_;
    std::string host_fingerprint_;
    std::string self_fingerprint_;
    std::map<std::string, std::int64_t, std::less<>> sync_drifts_;
};

} // namespace music_player::m0
