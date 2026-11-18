#pragma once

#include "m0/domain/ListeningRoom.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QString>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace music_player::m0 {

// The room queue as a guest sees it, for QML (ADR-0022, S8.2): the current item first, then the next
// ones in queue order. "available" says whether the item's file is in the local room cache; QML shows
// "Carregando…" until it is. The avatar is the one of the member who added the item (bundled only, as
// in RoomMembersModel; someone no longer in the room shows the first bundled avatar).
class RoomQueueModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

  public:
    enum Roles {
        ItemIdRole = Qt::UserRole + 1,
        TitleRole,
        ArtistRole,
        AddedByRole,
        AvatarSourceRole,
        AvailableRole,
        CurrentRole
    };

    explicit RoomQueueModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // available(sha) tells whether that file is complete locally.
    void replace(const RoomSnapshot& snapshot, const std::function<bool(std::string_view)>& available);

  signals:
    void countChanged();

  private:
    struct Row final {
        RoomItem item;
        AvatarChoice avatar;
        bool available{};
        bool current{};
        bool operator==(const Row&) const = default;
    };
    std::vector<Row> rows_;
};

} // namespace music_player::m0
