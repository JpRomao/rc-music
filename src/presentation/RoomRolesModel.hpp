#pragma once

#include "m0/domain/ListeningRoom.hpp"
#include "m0/domain/RoomRoster.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QString>

#include <cstdint>
#include <vector>

namespace music_player::m0 {

// The host's room roles for QML (S10.1): id, name, permissions (bit i = RoomPermission i), whether it is
// the invite's initial role and how many known people hold it. An edit of the same role list updates
// the rows in place (dataChanged), so the editor keeps its delegates and keyboard focus; only another
// set of role ids resets the model.
class RoomRolesModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

  public:
    enum Roles { RoleIdRole = Qt::UserRole + 1, NameRole, PermissionsRole, InitialRole, HoldersRole };

    explicit RoomRolesModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void replace(const RoomRosterSnapshot& roster, std::uint64_t initial_role_id);

  signals:
    void countChanged();

  private:
    struct Row final {
        RoomRole role;
        bool initial{};
        int holders{};
    };
    std::vector<Row> rows_;
};

// The people the host knows (S10.1): nickname, avatar (a present member's, else the first bundled one),
// role and block. The fingerprint is only the key the actions use; no displayed role carries it.
// Same update rule as RoomRolesModel: the same people in the same order update in place.
class RoomKnownPeopleModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

  public:
    enum Roles {
        FingerprintRole = Qt::UserRole + 1,
        NicknameRole,
        AvatarSourceRole,
        RoleIdRole,
        RoleNameRole,
        BlockedRole,
        PresentRole
    };

    explicit RoomKnownPeopleModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // present: the admitted members (their avatars, and who is in the room now).
    void replace(const RoomRosterSnapshot& roster, const std::vector<RoomMember>& present);

  signals:
    void countChanged();

  private:
    struct Row final {
        KnownListener person;
        QString role_name;
        AvatarChoice avatar;
        bool present{};
    };
    std::vector<Row> rows_;
};

} // namespace music_player::m0
