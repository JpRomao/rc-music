#include "LibraryListModel.hpp"

#include <QHash>

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
    // 0 means the file was read but its duration is unknown.
    if (!milliseconds || *milliseconds == 0)
        return {};
    const auto seconds = *milliseconds / 1000;
    const auto hours = seconds / 3600, minutes = (seconds / 60) % 60, rest = seconds % 60;
    const auto pad = [](std::uint64_t value) { return QStringLiteral("%1").arg(value, 2, 10, QLatin1Char('0')); };
    return hours > 0 ? QStringLiteral("%1:%2:%3").arg(hours).arg(pad(minutes), pad(rest))
                     : QStringLiteral("%1:%2").arg(minutes).arg(pad(rest));
}
// Past this many removal blocks and insertion runs together, one reset is cheaper than the row signals: each
// block shifts the rows after it and adds a change that the views merge into their pending set, so a scattered
// filter on a large library costs blocks x rows. Measured on 20 000 rows (docs/ui-tests.md, Biblioteca).
constexpr int max_incremental_blocks = 1024;
// The number of contiguous removal blocks plus insertion runs that the row signals would need.
int changeBlocks(const std::vector<bool>& kept, const std::vector<int>& origin) {
    int blocks = 0;
    for (std::size_t index = 0; index < kept.size(); ++index)
        blocks += !kept[index] && (index == 0 || kept[index - 1]) ? 1 : 0;
    for (std::size_t index = 0; index < origin.size(); ++index)
        blocks += origin[index] < 0 && (index == 0 || origin[index - 1] >= 0) ? 1 : 0;
    return blocks;
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

bool LibraryListModel::Row::sameContent(const Row& other) const {
    return id == other.id && title == other.title && artist == other.artist && album == other.album &&
           duration == other.duration && file_name == other.file_name && available == other.available;
}

void LibraryListModel::setRecords(const std::vector<LibraryRecord>& records) {
    std::vector<RowPtr> rows;
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
        rows.push_back(std::make_shared<const Row>(std::move(row)));
    }
    rows_ = std::move(rows);
    applyVisible(filtered());
}

void LibraryListModel::setFilter(const QString& value) {
    auto terms = searchKey(value).split(u' ', Qt::SkipEmptyParts);
    if (terms == terms_)
        return;
    terms_ = std::move(terms);
    applyVisible(filtered());
}

std::vector<LibraryListModel::RowPtr> LibraryListModel::filtered() const {
    std::vector<RowPtr> result;
    for (const auto& row : rows_) {
        const auto& key = row->key;
        if (std::all_of(terms_.begin(), terms_.end(), [&key](const QString& term) { return key.contains(term); }))
            result.push_back(row);
    }
    return result;
}

// Turns the current visible list into `next` with row signals. The ids both lists
// share must keep their order; otherwise the model resets. Every step leaves
// visible_ self-consistent, because views read it between the signals.
void LibraryListModel::applyVisible(std::vector<RowPtr> next) {
    const auto before = count();
    QHash<QString, int> oldPosition;
    oldPosition.reserve(static_cast<qsizetype>(visible_.size()));
    for (int index = 0; index < before; ++index)
        oldPosition.insert(visible_[static_cast<std::size_t>(index)]->id, index);

    // For each new row: its old position, or -1 when it is new.
    std::vector<int> origin(next.size(), -1);
    std::vector<bool> kept(visible_.size(), false);
    int last = -1;
    bool ordered = true;
    for (std::size_t index = 0; index < next.size() && ordered; ++index) {
        const auto found = oldPosition.constFind(next[index]->id);
        if (found == oldPosition.constEnd())
            continue;
        if (*found <= last) {
            ordered = false;
            break;
        }
        last = *found;
        origin[index] = *found;
        kept[static_cast<std::size_t>(*found)] = true;
    }

    if (ordered && changeBlocks(kept, origin) > max_incremental_blocks)
        ordered = false;
    if (!ordered) {
        beginResetModel();
        visible_ = std::move(next);
        endResetModel();
    } else {
        // Removals from the back, in contiguous blocks, keep earlier rows in place.
        for (int end = before - 1; end >= 0; --end) {
            if (kept[static_cast<std::size_t>(end)])
                continue;
            int first = end;
            while (first > 0 && !kept[static_cast<std::size_t>(first - 1)])
                --first;
            beginRemoveRows({}, first, end);
            visible_.erase(visible_.begin() + first, visible_.begin() + end + 1);
            endRemoveRows();
            end = first;
        }
        // Insertions in ascending order: each run lands at its final position.
        const int total = static_cast<int>(next.size());
        for (int first = 0; first < total; ++first) {
            if (origin[static_cast<std::size_t>(first)] >= 0)
                continue;
            int end = first;
            while (end + 1 < total && origin[static_cast<std::size_t>(end + 1)] < 0)
                ++end;
            beginInsertRows({}, first, end);
            visible_.insert(visible_.begin() + first, next.begin() + first, next.begin() + end + 1);
            endInsertRows();
            first = end;
        }
        // Rows both lists share keep their pointer unless their content changed.
        for (int row = 0; row < total; ++row) {
            const auto position = static_cast<std::size_t>(row);
            if (origin[position] < 0 || visible_[position]->sameContent(*next[position]))
                continue;
            visible_[position] = next[position];
            const auto changed = index(row, 0);
            emit dataChanged(changed, changed);
        }
    }
    if (count() != before)
        emit countChanged();
}

int LibraryListModel::count() const { return static_cast<int>(visible_.size()); }
int LibraryListModel::rowOf(const QString& recordId) const {
    const auto found = std::ranges::find(visible_, recordId, [](const RowPtr& row) { return row->id; });
    return found == visible_.end() ? -1 : static_cast<int>(found - visible_.begin());
}
int LibraryListModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : count(); }

QVariant LibraryListModel::data(const QModelIndex& index, const int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count())
        return {};
    const auto& row = *visible_[static_cast<std::size_t>(index.row())];
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
