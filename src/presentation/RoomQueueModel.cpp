#include "presentation/RoomQueueModel.hpp"

#include "presentation/RoomMembersModel.hpp"

#include <algorithm>
#include <utility>

namespace music_player::m0 {

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
        const auto owner =
            std::find_if(snapshot.members.begin(), snapshot.members.end(),
                         [&](const RoomMember& member) { return member.fingerprint == item.owner_fingerprint; });
        rows.push_back(Row{item, owner == snapshot.members.end() ? AvatarChoice{} : owner->avatar,
                           available && available(item.content_sha256), snapshot.playback.current_item == item.id});
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
