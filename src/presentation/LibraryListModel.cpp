#include "LibraryListModel.hpp"

#include <algorithm>

namespace music_player::m0 {
namespace {
QString text(const std::string& value) { return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())); }
QString fileStem(const std::filesystem::path& path) {
#ifdef Q_OS_WIN
    return QString::fromStdWString(path.stem().native());
#else
    const auto& stem = path.stem().native();
    return QString::fromUtf8(stem.data(), static_cast<qsizetype>(stem.size()));
#endif
}
QString fileName(const std::filesystem::path& path) {
#ifdef Q_OS_WIN
    return QString::fromStdWString(path.filename().native());
#else
    const auto& name = path.filename().native();
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
#endif
}
QString durationText(const std::optional<std::uint64_t>& milliseconds) {
    if (!milliseconds)
        return {};
    const auto seconds = *milliseconds / 1000;
    const auto hours = seconds / 3600, minutes = (seconds / 60) % 60, rest = seconds % 60;
    const auto pad = [](std::uint64_t value) { return QStringLiteral("%1").arg(value, 2, 10, QLatin1Char('0')); };
    return hours > 0 ? QStringLiteral("%1:%2:%3").arg(hours).arg(pad(minutes), pad(rest))
                     : QStringLiteral("%1:%2").arg(minutes).arg(pad(rest));
}
} // namespace

LibraryListModel::LibraryListModel(QObject* parent) : QAbstractListModel(parent) {}

QString LibraryListModel::searchKey(const QString& value) {
    // Decompose, drop combining marks, then fold case: "Canção" matches "cancao".
    const auto decomposed = value.normalized(QString::NormalizationForm_D);
    QString result;
    result.reserve(decomposed.size());
    for (const auto unit : decomposed)
        if (unit.category() != QChar::Mark_NonSpacing)
            result.append(unit);
    return result.toCaseFolded().simplified();
}

void LibraryListModel::setRecords(const std::vector<LibraryRecord>& records) {
    std::vector<Row> rows;
    rows.reserve(records.size());
    for (const auto& record : records) {
        Row row;
        row.id = QString::number(record.id);
        row.title = record.title.empty() ? fileStem(record.source_path) : text(record.title);
        row.artist = text(record.artist);
        row.album = text(record.album);
        row.duration = durationText(record.duration_ms);
        row.file_name = fileName(record.source_path);
        row.available = record.available;
        row.key = searchKey(row.title + u'\n' + row.artist + u'\n' + row.album + u'\n' + row.file_name);
        rows.push_back(std::move(row));
    }
    beginResetModel();
    rows_ = std::move(rows);
    refilter();
    endResetModel();
    emit countChanged();
}

void LibraryListModel::setFilter(const QString& value) {
    auto terms = searchKey(value).split(u' ', Qt::SkipEmptyParts);
    if (terms == terms_)
        return;
    beginResetModel();
    terms_ = std::move(terms);
    refilter();
    endResetModel();
    emit countChanged();
}

void LibraryListModel::refilter() {
    visible_.clear();
    for (int index = 0; index < static_cast<int>(rows_.size()); ++index) {
        const auto& key = rows_[static_cast<std::size_t>(index)].key;
        if (std::all_of(terms_.begin(), terms_.end(), [&key](const QString& term) { return key.contains(term); }))
            visible_.push_back(index);
    }
}

int LibraryListModel::count() const { return static_cast<int>(visible_.size()); }
int LibraryListModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : count(); }

QVariant LibraryListModel::data(const QModelIndex& index, const int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count())
        return {};
    const auto& row = rows_[static_cast<std::size_t>(visible_[static_cast<std::size_t>(index.row())])];
    switch (role) {
    case RecordIdRole:
        return row.id;
    case Qt::DisplayRole:
    case TitleRole:
        return row.title;
    case ArtistRole:
        return row.artist;
    case AlbumRole:
        return row.album;
    case DurationTextRole:
        return row.duration;
    case AvailableRole:
        return row.available;
    case FileNameRole:
        return row.file_name;
    default:
        return {};
    }
}

QHash<int, QByteArray> LibraryListModel::roleNames() const {
    return {{RecordIdRole, "recordId"},
            {TitleRole, "title"},
            {ArtistRole, "artist"},
            {AlbumRole, "album"},
            {DurationTextRole, "durationText"},
            {AvailableRole, "available"},
            {FileNameRole, "fileName"}};
}

} // namespace music_player::m0
