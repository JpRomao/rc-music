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
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)

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

    [[nodiscard]] bool loaded() const noexcept;
    [[nodiscard]] bool loadOnce(QList<QueueItem> items);
    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  signals:
    void loadedChanged();

  private:
    QList<QueueItem> items_;
    bool loaded_{false};
};

} // namespace music_player::m0
