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
    for (const LocalFileQueueItem& source : queue.items()) {
        const auto file_name = pathComponent(source.source_path.filename());
        auto title = pathComponent(source.source_path.stem());
        if (title.isEmpty()) {
            title = file_name;
        }
        replacement.append({.order_index = order_index++,
                            .occurrence_id = source.occurrence_id,
                            .file_name = file_name,
                            .title = std::move(title),
                            .available = source.available});
    }

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
