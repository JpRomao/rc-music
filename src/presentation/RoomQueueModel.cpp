#include "presentation/RoomQueueModel.hpp"

#include "presentation/RoomMembersModel.hpp"

#include <algorithm>
#include <utility>

namespace music_player::m0 {

namespace {

// The avatar of who added the item. The owner added its own files; a host track a guest added from the
// catalog names that guest in added_by (nicknames may repeat: only one member with it counts). Otherwise
// the owner's avatar, or the first bundled one when the owner left.
AvatarChoice adderAvatar(const std::vector<RoomMember>& members, const RoomItem& item) {
    const auto owner = std::find_if(members.begin(), members.end(), [&](const RoomMember& member) {
        return member.fingerprint == item.owner_fingerprint;
    });
    if (owner != members.end() && owner->nickname == item.added_by)
        return owner->avatar;
    const auto named = [&](const RoomMember& member) { return member.nickname == item.added_by; };
    if (std::count_if(members.begin(), members.end(), named) == 1)
        return std::find_if(members.begin(), members.end(), named)->avatar;
    return owner == members.end() ? AvatarChoice{} : owner->avatar;
}

} // namespace

RoomQueueModel::RoomQueueModel(QObject* parent) : QAbstractListModel(parent) {}

int RoomQueueModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant RoomQueueModel::data(const QModelIndex& index, const int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const auto& row = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case ItemIdRole:
        return QString::number(row.item.id);
    case Qt::DisplayRole:
    case TitleRole:
        return QString::fromStdString(row.item.summary.title);
    case ArtistRole:
        return QString::fromStdString(row.item.summary.artist);
    case AddedByRole:
        return QString::fromStdString(row.item.added_by);
    case AvatarSourceRole:
        return RoomMembersModel::avatarSource(row.avatar);
    case AvailableRole:
        return row.available;
    case CurrentRole:
        return row.current;
    default:
        return {};
    }
}

QHash<int, QByteArray> RoomQueueModel::roleNames() const {
    return {{ItemIdRole, "itemId"},
            {TitleRole, "title"},
            {ArtistRole, "artist"},
            {AddedByRole, "addedBy"},
            {AvatarSourceRole, "avatarSource"},
            {AvailableRole, "available"},
            {CurrentRole, "current"}};
}

void RoomQueueModel::replace(const RoomSnapshot& snapshot, const std::function<bool(std::string_view)>& available) {
    std::vector<Row> rows;
    rows.reserve(snapshot.queue.size());
    for (const auto& item : snapshot.queue) {
        rows.push_back(Row{item, adderAvatar(snapshot.members, item), available && available(item.content_sha256),
                           snapshot.playback.current_item == item.id});
    }
    if (rows == rows_)
        return;
    const bool count_changed = rows.size() != rows_.size();
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
    if (count_changed)
        emit countChanged();
}

} // namespace music_player::m0
