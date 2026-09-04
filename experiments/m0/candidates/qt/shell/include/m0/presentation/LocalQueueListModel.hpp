#pragma once

#include "m0/domain/LocalFileQueue.hpp"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QVariant>

#include <cstdint>
#include <optional>
#include <span>

namespace music_player::m0 {

class LocalQueueListModel final : public QAbstractListModel {
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

    explicit LocalQueueListModel(QObject* parent = nullptr);

    [[nodiscard]] bool loaded() const noexcept;
    [[nodiscard]] bool replace(const LocalFileQueue& queue);
    [[nodiscard]] bool append(std::span<const LocalFileQueueItem> source_items);
    [[nodiscard]] bool insert(std::span<const LocalFileQueueItem> source_items, std::size_t insertion_index);
    [[nodiscard]] bool move(std::size_t source_index, std::size_t destination_index);
    [[nodiscard]] bool remove(std::size_t index);
    [[nodiscard]] bool clear();
    [[nodiscard]] bool updateMetadata(LocalQueueOccurrenceId occurrence_id, QString title, QString artist,
                                      QString album, std::optional<std::int64_t> duration_ms);
    [[nodiscard]] bool markUnavailable(LocalQueueOccurrenceId occurrence_id, QString error_code);
    [[nodiscard]] bool markAvailable(LocalQueueOccurrenceId occurrence_id);
    [[nodiscard]] static QString occurrenceId(LocalQueueOccurrenceId occurrence_id);
    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  signals:
    void loadedChanged();

  private:
    struct Item final {
        int order_index{0};
        LocalQueueOccurrenceId occurrence_id{};
        QString file_name;
        QString title;
        QString artist;
        QString album;
        std::optional<std::int64_t> duration_ms;
        bool available{true};
        std::optional<QString> error_code;
    };

    [[nodiscard]] static Item makeItem(const LocalFileQueueItem& source, int order_index);
    void reindex(std::size_t first_index);
    [[nodiscard]] static QString pathComponent(const std::filesystem::path& value);
    [[nodiscard]] QList<Item>::iterator find(LocalQueueOccurrenceId occurrence_id);

    QList<Item> items_;
    bool loaded_{false};
};

} // namespace music_player::m0
