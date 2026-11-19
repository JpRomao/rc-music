#pragma once

#include "m0/domain/RoomMessages.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QString>

#include <cstdint>
#include <vector>

namespace music_player::m0 {

// The host's playlists visible to the room, as a guest sees them (S11.2), for QML: one row per playlist
// ("playlist": its name and how many tracks it shows) followed by one row per track ("entry": title and
// artist). Ids are text. replace() keeps the rows that did not change: a catalog that grew by a few tracks
// inserts only those rows, so the others (and the keyboard focus on them) stay.
class RoomCatalogModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

  public:
    enum Roles {
        KindRole = Qt::UserRole + 1,
        PlaylistIdRole,
        EntryIdRole,
        NameRole,
        TitleRole,
        ArtistRole,
        EntryCountRole
    };

    explicit RoomCatalogModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void replace(const RoomCatalog& catalog);

  signals:
    void countChanged();

  private:
    struct Row final {
        bool playlist{};
        std::uint64_t playlist_id{};
        std::uint64_t entry_id{};
        QString name;
        QString title;
        QString artist;
        int entry_count{};
        [[nodiscard]] bool sameKey(const Row& other) const {
            return playlist == other.playlist && playlist_id == other.playlist_id && entry_id == other.entry_id;
        }
        bool operator==(const Row&) const = default;
    };
    std::vector<Row> rows_;
};

} // namespace music_player::m0
