#pragma once

#include "m0/application/DownloadService.hpp"

#include <QAbstractListModel>

namespace music_player::m0 {

// A read-only projection of the shared service; refresh never runs a worker or advances time.
class DownloadJobsModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int activeCount READ activeCount NOTIFY summaryChanged)
    Q_PROPERTY(int completedCount READ completedCount NOTIFY summaryChanged)

  public:
    enum Role {
        JobIdRole = Qt::UserRole + 1,
        TitleRole,
        StateRole,
        StatusTextRole,
        CompletedUnitsRole,
        TotalUnitsRole,
        IndeterminateRole,
        ProgressRatioRole,
        OutputPathRole,
        ErrorTextRole,
        CanCancelRole,
        CanRetryImportRole
    };
    Q_ENUM(Role)

    explicit DownloadJobsModel(DownloadService& service, QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int activeCount() const noexcept;
    [[nodiscard]] int completedCount() const noexcept;
    void refresh();
    Q_INVOKABLE bool cancel(const QString& job_id);
    Q_INVOKABLE bool retryImport(const QString& job_id);

  signals:
    void summaryChanged();

  private:
    void refreshSnapshot();

    DownloadService& service_;
    std::vector<DownloadJobSnapshot> items_{};
    int active_count_{0};
    int completed_count_{0};
    bool refreshing_{false};
    bool refresh_requested_{false};
};

} // namespace music_player::m0
