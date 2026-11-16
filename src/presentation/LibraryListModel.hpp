#pragma once

#include "m0/domain/LocalLibrary.hpp"

#include <QAbstractListModel>
#include <QString>
#include <QStringList>

#include <vector>

namespace music_player::m0 {

// Read-only projection of the confirmed library for the List view, with a local
// filter that ignores case and accents across title, artist, album and file name.
// Never touches files or the network.
class LibraryListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
  public:
    enum Role {
        RecordIdRole = Qt::UserRole + 1,
        TitleRole,
        ArtistRole,
        AlbumRole,
        DurationTextRole,
        AvailableRole,
        FileNameRole,
    };
    explicit LibraryListModel(QObject* parent = nullptr);
    void setRecords(const std::vector<LibraryRecord>& records);
    void setFilter(const QString& text);
    [[nodiscard]] int count() const;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    // Accent- and case-insensitive comparison key, shared with tests.
    [[nodiscard]] static QString searchKey(const QString& text);
  signals:
    void countChanged();

  private:
    struct Row final {
        QString id, title, artist, album, duration, file_name, key;
        bool available{true};
    };
    void refilter();
    std::vector<Row> rows_;
    std::vector<int> visible_;
    QStringList terms_;
};

} // namespace music_player::m0
