#include "presentation/RoomCatalogModel.hpp"

#include <cstddef>
#include <utility>

namespace music_player::m0 {

RoomCatalogModel::RoomCatalogModel(QObject* parent) : QAbstractListModel(parent) {}

int RoomCatalogModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant RoomCatalogModel::data(const QModelIndex& index, const int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const auto& row = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case KindRole:
        return row.playlist ? QStringLiteral("playlist") : QStringLiteral("entry");
    case PlaylistIdRole:
        return QString::number(row.playlist_id);
    case EntryIdRole:
        return row.playlist ? QString{} : QString::number(row.entry_id);
    case Qt::DisplayRole:
        return row.playlist ? row.name : row.title;
    case NameRole:
        return row.name;
    case TitleRole:
        return row.title;
    case ArtistRole:
        return row.artist;
    case EntryCountRole:
        return row.entry_count;
    default:
        return {};
    }
}

QHash<int, QByteArray> RoomCatalogModel::roleNames() const {
    return {{KindRole, "kind"},   {PlaylistIdRole, "playlistId"}, {EntryIdRole, "entryId"},      {NameRole, "name"},
            {TitleRole, "title"}, {ArtistRole, "artist"},         {EntryCountRole, "entryCount"}};
}

// Rows with the same key (kind and ids) at the start and at the end stay, updated in place; only the rows
// between them are removed and inserted.
void RoomCatalogModel::replace(const RoomCatalog& catalog) {
    std::vector<Row> rows;
    for (const auto& playlist : catalog.playlists) {
        rows.push_back(Row{true,
                           playlist.id,
                           0,
                           QString::fromStdString(playlist.name),
                           {},
                           {},
                           static_cast<int>(playlist.entries.size())});
        for (const auto& entry : playlist.entries)
            rows.push_back(Row{false,
                               playlist.id,
                               entry.id,
                               {},
                               QString::fromStdString(entry.summary.title),
                               QString::fromStdString(entry.summary.artist),
                               0});
    }
    if (rows == rows_)
        return;
    const auto old_count = rows_.size();
    std::size_t head = 0;
    while (head < rows.size() && head < rows_.size() && rows[head].sameKey(rows_[head]))
        ++head;
    std::size_t tail = 0;
    while (tail < rows.size() - head && tail < rows_.size() - head &&
           rows[rows.size() - 1 - tail].sameKey(rows_[rows_.size() - 1 - tail]))
        ++tail;
    const auto updateKept = [&](const std::size_t from, const std::size_t to, const std::size_t offset) {
        for (auto index = from; index < to; ++index) {
            if (rows_[index] == rows[index + offset])
                continue;
            rows_[index] = rows[index + offset];
            const auto changed = this->index(static_cast<int>(index), 0);
            emit dataChanged(changed, changed);
        }
    };
    updateKept(0, head, 0);
    if (rows_.size() - tail > head) {
        beginRemoveRows({}, static_cast<int>(head), static_cast<int>(rows_.size() - tail - 1));
        rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(head), rows_.end() - static_cast<std::ptrdiff_t>(tail));
        endRemoveRows();
    }
    if (rows.size() - tail > head) {
        beginInsertRows({}, static_cast<int>(head), static_cast<int>(rows.size() - tail - 1));
        rows_.insert(rows_.begin() + static_cast<std::ptrdiff_t>(head),
                     rows.begin() + static_cast<std::ptrdiff_t>(head), rows.end() - static_cast<std::ptrdiff_t>(tail));
        endInsertRows();
    }
    // The kept tail now sits at the same positions as in the new rows.
    updateKept(rows_.size() - tail, rows_.size(), 0);
    if (rows_.size() != old_count)
        emit countChanged();
}

} // namespace music_player::m0
