#include "presentation/RoomRolesModel.hpp"

#include "presentation/RoomMembersModel.hpp"

#include <algorithm>
#include <utility>

namespace music_player::m0 {
namespace {

[[nodiscard]] bool sameKeys(const auto& before, const auto& after, const auto& key) {
    return std::equal(before.begin(), before.end(), after.begin(), after.end(),
                      [&](const auto& a, const auto& b) { return key(a) == key(b); });
}

} // namespace

RoomRolesModel::RoomRolesModel(QObject* parent) : QAbstractListModel(parent) {}

int RoomRolesModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant RoomRolesModel::data(const QModelIndex& index, const int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const auto& row = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case RoleIdRole:
        return static_cast<int>(row.role.id);
    case Qt::DisplayRole:
    case NameRole:
        return QString::fromStdString(row.role.name);
    case PermissionsRole:
        return static_cast<int>(row.role.permissions.to_ulong());
    case InitialRole:
        return row.initial;
    case HoldersRole:
        return row.holders;
    default:
        return {};
    }
}

QHash<int, QByteArray> RoomRolesModel::roleNames() const {
    return {{RoleIdRole, "roleId"},
            {NameRole, "name"},
            {PermissionsRole, "permissions"},
            {InitialRole, "initial"},
            {HoldersRole, "holders"}};
}

void RoomRolesModel::replace(const RoomRosterSnapshot& roster, const std::uint64_t initial_role_id) {
    std::vector<Row> rows;
    rows.reserve(roster.roles.size());
    for (const auto& role : roster.roles) {
        const auto holders = std::count_if(roster.listeners.begin(), roster.listeners.end(),
                                           [&](const KnownListener& l) { return l.role_id == role.id; });
        rows.push_back(Row{role, role.id == initial_role_id, static_cast<int>(holders)});
    }
    if (sameKeys(rows_, rows, [](const Row& row) { return row.role.id; })) {
        for (std::size_t index = 0; index < rows.size(); ++index) {
            auto& current = rows_[index];
            const auto& next = rows[index];
            if (current.role.name == next.role.name && current.role.permissions == next.role.permissions &&
                current.initial == next.initial && current.holders == next.holders)
                continue;
            current = next;
            const auto changed = this->index(static_cast<int>(index));
            emit dataChanged(changed, changed);
        }
        return;
    }
    const bool count_changed = rows.size() != rows_.size();
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
    if (count_changed)
        emit countChanged();
}

RoomKnownPeopleModel::RoomKnownPeopleModel(QObject* parent) : QAbstractListModel(parent) {}

int RoomKnownPeopleModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant RoomKnownPeopleModel::data(const QModelIndex& index, const int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const auto& row = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case FingerprintRole:
        return QString::fromStdString(row.person.fingerprint);
    case Qt::DisplayRole:
    case NicknameRole:
        return QString::fromStdString(row.person.last_nickname);
    case AvatarSourceRole:
        return RoomMembersModel::avatarSource(row.avatar);
    case RoleIdRole:
        return static_cast<int>(row.person.role_id);
    case RoleNameRole:
        return row.role_name;
    case BlockedRole:
        return row.person.blocked;
    case PresentRole:
        return row.present;
    default:
        return {};
    }
}

QHash<int, QByteArray> RoomKnownPeopleModel::roleNames() const {
    return {{FingerprintRole, "fingerprint"}, {NicknameRole, "nickname"}, {AvatarSourceRole, "avatarSource"},
            {RoleIdRole, "roleId"},           {RoleNameRole, "roleName"}, {BlockedRole, "blocked"},
            {PresentRole, "present"}};
}

void RoomKnownPeopleModel::replace(const RoomRosterSnapshot& roster, const std::vector<RoomMember>& present) {
    std::vector<Row> rows;
    rows.reserve(roster.listeners.size());
    for (const auto& person : roster.listeners) {
        Row row{person, {}, {}, false};
        const auto role = std::find_if(roster.roles.begin(), roster.roles.end(),
                                       [&](const RoomRole& r) { return r.id == person.role_id; });
        if (role != roster.roles.end())
            row.role_name = QString::fromStdString(role->name);
        const auto member = std::find_if(present.begin(), present.end(),
                                         [&](const RoomMember& m) { return m.fingerprint == person.fingerprint; });
        if (member != present.end()) {
            row.avatar = member->avatar;
            row.present = member->state == MemberState::Admitted;
        }
        rows.push_back(std::move(row));
    }
    if (sameKeys(rows_, rows, [](const Row& row) { return row.person.fingerprint; })) {
        for (std::size_t index = 0; index < rows.size(); ++index) {
            auto& current = rows_[index];
            const auto& next = rows[index];
            if (current.person.last_nickname == next.person.last_nickname &&
                current.person.role_id == next.person.role_id && current.person.blocked == next.person.blocked &&
                current.role_name == next.role_name && current.avatar == next.avatar &&
                current.present == next.present) {
                current.person.overrides = next.person.overrides;
                continue;
            }
            current = next;
            const auto changed = this->index(static_cast<int>(index));
            emit dataChanged(changed, changed);
        }
        return;
    }
    const bool count_changed = rows.size() != rows_.size();
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
    if (count_changed)
        emit countChanged();
}

} // namespace music_player::m0
