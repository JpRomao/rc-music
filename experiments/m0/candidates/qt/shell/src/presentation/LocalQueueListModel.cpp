#include "m0/presentation/LocalQueueListModel.hpp"

#include <QList>

#include <algorithm>
#include <utility>

namespace music_player::m0 {

LocalQueueListModel::LocalQueueListModel(QObject* parent) : QAbstractListModel(parent) {}

bool LocalQueueListModel::loaded() const noexcept { return loaded_; }

bool LocalQueueListModel::replace(const LocalFileQueue& queue) {
    if (queue.empty()) {
        return false;
    }

    QList<Item> replacement;
    replacement.reserve(static_cast<qsizetype>(queue.size()));
    int order_index = 0;
    for (const LocalFileQueueItem& source : queue.items())
        replacement.append(makeItem(source, order_index++));

    const bool loaded_changed = !loaded_;
    beginResetModel();
    items_ = std::move(replacement);
    loaded_ = true;
    endResetModel();
    if (loaded_changed) {
        emit loadedChanged();
    }
    return true;
}

bool LocalQueueListModel::append(const std::span<const LocalFileQueueItem> source_items) {
    if (source_items.empty()) {
        return false;
    }

    QList<Item> appended_items;
    appended_items.reserve(static_cast<qsizetype>(source_items.size()));
    int order_index = rowCount();
    for (const LocalFileQueueItem& source : source_items) {
        if (source.source_path.empty() || source.occurrence_id.value == 0 ||
            find(source.occurrence_id) != items_.end() ||
            std::ranges::find(appended_items, source.occurrence_id, &Item::occurrence_id) != appended_items.end()) {
            return false;
        }
        appended_items.append(makeItem(source, order_index++));
    }

    const int first_row = rowCount();
    const int last_row = first_row + static_cast<int>(appended_items.size()) - 1;
    const bool loaded_changed = !loaded_;
    items_.reserve(items_.size() + appended_items.size());
    beginInsertRows({}, first_row, last_row);
    for (Item& item : appended_items)
        items_.append(std::move(item));
    loaded_ = true;
    endInsertRows();
    if (loaded_changed)
        emit loadedChanged();
    return true;
}

bool LocalQueueListModel::move(const std::size_t source_index, const std::size_t destination_index) {
    const auto item_count = static_cast<std::size_t>(items_.size());
    if (source_index >= item_count || destination_index >= item_count || source_index == destination_index) {
        return false;
    }

    const int source_row = static_cast<int>(source_index);
    const int destination_row = static_cast<int>(destination_index);
    const int destination_child = destination_index > source_index ? destination_row + 1 : destination_row;
    if (!beginMoveRows({}, source_row, source_row, {}, destination_child)) {
        return false;
    }
    items_.move(source_row, destination_row);
    endMoveRows();
    reindex(std::min(source_index, destination_index));
    return true;
}

bool LocalQueueListModel::remove(const std::size_t index_to_remove) {
    if (index_to_remove >= static_cast<std::size_t>(items_.size())) {
        return false;
    }

    const int row = static_cast<int>(index_to_remove);
    beginRemoveRows({}, row, row);
    items_.removeAt(row);
    const bool loaded_changed = items_.empty() && loaded_;
    if (loaded_changed)
        loaded_ = false;
    endRemoveRows();
    reindex(index_to_remove);
    if (loaded_changed)
        emit loadedChanged();
    return true;
}

bool LocalQueueListModel::clear() {
    if (items_.empty()) {
        return false;
    }

    beginRemoveRows({}, 0, rowCount() - 1);
    items_.clear();
    const bool loaded_changed = loaded_;
    loaded_ = false;
    endRemoveRows();
    if (loaded_changed)
        emit loadedChanged();
    return true;
}

bool LocalQueueListModel::updateMetadata(const LocalQueueOccurrenceId occurrence_id, QString title, QString artist,
                                         QString album, const std::optional<std::int64_t> duration_ms) {
    const auto item = find(occurrence_id);
    if (item == items_.end()) {
        return false;
    }
    item->title = std::move(title);
    item->artist = std::move(artist);
    item->album = std::move(album);
    item->duration_ms = duration_ms;
    const QModelIndex changed = index(static_cast<int>(std::distance(items_.begin(), item)), 0);
    emit dataChanged(changed, changed, {TitleRole, ArtistRole, AlbumRole, DurationMsRole});
    return true;
}

bool LocalQueueListModel::markUnavailable(const LocalQueueOccurrenceId occurrence_id, QString error_code) {
    const auto item = find(occurrence_id);
    if (item == items_.end() || !item->available) {
        return false;
    }
    item->available = false;
    item->error_code = std::move(error_code);
    const QModelIndex changed = index(static_cast<int>(std::distance(items_.begin(), item)), 0);
    emit dataChanged(changed, changed, {AvailableRole, ErrorCodeRole});
    return true;
}

QString LocalQueueListModel::occurrenceId(const LocalQueueOccurrenceId occurrence_id) {
    return QStringLiteral("local-%1").arg(occurrence_id.value);
}

int LocalQueueListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(items_.size());
}

QVariant LocalQueueListModel::data(const QModelIndex& index, const int role) const {
    if (!index.isValid() || index.parent().isValid() || index.column() != 0 || index.row() < 0 ||
        index.row() >= items_.size()) {
        return {};
    }
    const Item& item = items_.at(index.row());
    switch (role) {
    case OrderIndexRole:
        return item.order_index;
    case OccurrenceIdRole:
        return occurrenceId(item.occurrence_id);
    case SemanticKeyRole:
        return occurrenceId(item.occurrence_id);
    case PresentationFormRole:
        return QStringLiteral("local_file");
    case FileNameRole:
        return item.file_name;
    case TitleRole:
        return item.title;
    case ArtistRole:
        return item.artist;
    case AlbumRole:
        return item.album;
    case DurationMsRole:
        return item.duration_ms.has_value() ? QVariant::fromValue<qlonglong>(*item.duration_ms) : QVariant{};
    case AvailableRole:
        return item.available;
    case ErrorCodeRole:
        return item.error_code.has_value() ? QVariant{*item.error_code} : QVariant{};
    default:
        return {};
    }
}

QHash<int, QByteArray> LocalQueueListModel::roleNames() const {
    return {
        {OrderIndexRole, QByteArrayLiteral("orderIndex")},
        {OccurrenceIdRole, QByteArrayLiteral("occurrenceId")},
        {SemanticKeyRole, QByteArrayLiteral("semanticKey")},
        {PresentationFormRole, QByteArrayLiteral("presentationForm")},
        {FileNameRole, QByteArrayLiteral("fileName")},
        {TitleRole, QByteArrayLiteral("title")},
        {ArtistRole, QByteArrayLiteral("artist")},
        {AlbumRole, QByteArrayLiteral("album")},
        {DurationMsRole, QByteArrayLiteral("durationMs")},
        {AvailableRole, QByteArrayLiteral("available")},
        {ErrorCodeRole, QByteArrayLiteral("errorCode")},
    };
}

LocalQueueListModel::Item LocalQueueListModel::makeItem(const LocalFileQueueItem& source, const int order_index) {
    const auto file_name = pathComponent(source.source_path.filename());
    auto title = pathComponent(source.source_path.stem());
    if (title.isEmpty())
        title = file_name;
    return {.order_index = order_index,
            .occurrence_id = source.occurrence_id,
            .file_name = file_name,
            .title = std::move(title),
            .available = source.available};
}

void LocalQueueListModel::reindex(const std::size_t first_index) {
    if (first_index >= static_cast<std::size_t>(items_.size()))
        return;

    for (std::size_t index = first_index; index < static_cast<std::size_t>(items_.size()); ++index)
        items_[static_cast<qsizetype>(index)].order_index = static_cast<int>(index);
    emit dataChanged(this->index(static_cast<int>(first_index), 0), this->index(rowCount() - 1, 0), {OrderIndexRole});
}

QString LocalQueueListModel::pathComponent(const std::filesystem::path& value) {
#ifdef _WIN32
    return QString::fromStdWString(value.native());
#else
    const auto& native = value.native();
    return QString::fromUtf8(native.data(), static_cast<qsizetype>(native.size()));
#endif
}

QList<LocalQueueListModel::Item>::iterator LocalQueueListModel::find(const LocalQueueOccurrenceId occurrence_id) {
    return std::ranges::find(items_, occurrence_id, &Item::occurrence_id);
}

} // namespace music_player::m0
