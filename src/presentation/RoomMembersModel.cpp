#include "presentation/RoomMembersModel.hpp"

#include <utility>

namespace music_player::m0 {

RoomMembersModel::RoomMembersModel(QObject* parent) : QAbstractListModel(parent) {}

int RoomMembersModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(members_.size());
}

QVariant RoomMembersModel::data(const QModelIndex& index, const int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const auto& member = members_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case FingerprintRole:
        return QString::fromStdString(member.fingerprint);
    case Qt::DisplayRole:
    case NicknameRole:
        return QString::fromStdString(member.nickname);
    case AvatarSourceRole:
        return avatarSource(member.avatar);
    case HostRole:
        return !host_fingerprint_.empty() && member.fingerprint == host_fingerprint_;
    case SelfRole:
        return !self_fingerprint_.empty() && member.fingerprint == self_fingerprint_;
    case HasSyncDriftRole:
        return sync_drifts_.contains(member.fingerprint);
    case SyncDriftMsRole: {
        const auto found = sync_drifts_.find(member.fingerprint);
        return found == sync_drifts_.end() ? 0 : static_cast<qlonglong>(found->second);
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> RoomMembersModel::roleNames() const {
    return {{FingerprintRole, "fingerprint"},
            {NicknameRole, "nickname"},
            {AvatarSourceRole, "avatarSource"},
            {HostRole, "isHost"},
            {SelfRole, "isSelf"},
            {HasSyncDriftRole, "hasSyncDrift"},
            {SyncDriftMsRole, "syncDriftMs"}};
}

void RoomMembersModel::replace(std::vector<RoomMember> members, const std::string& host_fingerprint,
                               const std::string& self_fingerprint,
                               std::map<std::string, std::int64_t, std::less<>> sync_drifts) {
    if (members == members_ && host_fingerprint == host_fingerprint_ && self_fingerprint == self_fingerprint_) {
        // Only the sync differences may differ: update the rows, keep the delegates (and focus) alive.
        if (sync_drifts == sync_drifts_)
            return;
        auto previous = std::exchange(sync_drifts_, std::move(sync_drifts));
        for (std::size_t row = 0; row < members_.size(); ++row) {
            const auto& fingerprint = members_[row].fingerprint;
            const auto before = previous.find(fingerprint);
            const auto after = sync_drifts_.find(fingerprint);
            const bool had = before != previous.end();
            const bool has = after != sync_drifts_.end();
            if (had != has || (had && before->second != after->second))
                emit dataChanged(index(static_cast<int>(row)), index(static_cast<int>(row)),
                                 {HasSyncDriftRole, SyncDriftMsRole});
        }
        return;
    }
    const bool count_changed = members.size() != members_.size();
    beginResetModel();
    members_ = std::move(members);
    host_fingerprint_ = host_fingerprint;
    self_fingerprint_ = self_fingerprint;
    sync_drifts_ = std::move(sync_drifts);
    endResetModel();
    if (count_changed)
        emit countChanged();
}

const std::vector<RoomMember>& RoomMembersModel::members() const noexcept { return members_; }

QUrl RoomMembersModel::avatarSource(const AvatarChoice& avatar) {
    const auto index =
        avatar.kind == AvatarKind::Bundled && avatar.bundled_index < bundled_avatar_count ? avatar.bundled_index : 0U;
    return QUrl{QStringLiteral("qrc:/rc_music/avatars/avatar-%1.svg").arg(index + 1, 2, 10, QLatin1Char{'0'})};
}

} // namespace music_player::m0
