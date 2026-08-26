#include "m0/presentation/QueueListModel.hpp"

#include <utility>

namespace music_player::m0 {

QueueListModel::QueueListModel(QObject* parent) : QAbstractListModel(parent) {}

bool QueueListModel::loaded() const noexcept { return loaded_; }

bool QueueListModel::loadOnce(QList<QueueItem> items) {
    if (loaded_ || items.isEmpty()) {
        return false;
    }

    beginResetModel();
    items_ = std::move(items);
    loaded_ = true;
    endResetModel();
    emit loadedChanged();
    return true;
}

int QueueListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(items_.size());
}

QVariant QueueListModel::data(const QModelIndex& index, const int role) const {
    if (!index.isValid() || index.parent().isValid() || index.column() != 0 || index.row() < 0 ||
        index.row() >= items_.size()) {
        return {};
    }
    const QueueItem& item = items_.at(index.row());
    switch (role) {
    case OrderIndexRole:
        return item.order_index;
    case OccurrenceIdRole:
        return item.occurrence_id;
    case SemanticKeyRole:
        return item.semantic_key;
    case PresentationFormRole:
        return item.presentation_form;
    case FileNameRole:
        return item.file_name;
    case TitleRole:
        return item.title;
    case ArtistRole:
        return item.artist;
    case AlbumRole:
        return item.album;
    case DurationMsRole:
        return item.duration_ms.has_value() ? QVariant::fromValue<qlonglong>(item.duration_ms.value()) : QVariant{};
    case AvailableRole:
        return item.available;
    case ErrorCodeRole:
        return item.error_code.has_value() ? QVariant{*item.error_code} : QVariant{};
    default:
        return {};
    }
}

QHash<int, QByteArray> QueueListModel::roleNames() const {
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

} // namespace music_player::m0
