#pragma once

#include "m0/adapters/fixture/FixtureRepository.hpp"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QModelIndex>
#include <QObject>
#include <QVariant>

namespace music_player::m0 {

class QueueListModel final : public QAbstractListModel {
    Q_OBJECT

  public:
    enum Role {
        OrderIndexRole = Qt::UserRole + 1,
        OccurrenceIdRole,
        SemanticKeyRole,
        PresentationFormRole,
        FileNameRole,
        TitleRole,
        ArtistRole,
        AlbumRole,
        DurationMsRole,
        AvailableRole,
        ErrorCodeRole,
    };
    Q_ENUM(Role)

    explicit QueueListModel(QObject* parent = nullptr);
    explicit QueueListModel(QList<QueueItem> items, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  private:
    QList<QueueItem> items_;
};

} // namespace music_player::m0
