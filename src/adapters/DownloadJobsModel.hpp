#pragma once

#include "m0/application/DownloadService.hpp"
#include "m0/ports/DownloadRecoveryReview.hpp"
#include "m0/ports/DownloadRecoveryValidation.hpp"

#include <QAbstractListModel>
#include <QHash>

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
        CanRetryImportRole,
        CanReviewFilesRole,
        ReviewTextRole,
        ReviewBusyRole,
        CanDiscardPartialsRole,
        CanValidateMediaRole,
        ValidationTextRole,
        ValidationBusyRole,
        CanReleaseValidationRole
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
    void setReviewAvailable(bool available);
    void setReviewUnavailable();
    void setDiscardSupported(bool supported);
    [[nodiscard]] bool beginReview(const QString& job_id,
                                   DownloadRecoveryReviewAction action = DownloadRecoveryReviewAction::ReviewFiles);
    [[nodiscard]] bool finishReview(const QString& job_id, DownloadRecoveryReviewOutcome outcome);
    void setValidationAvailable(bool available);
    void setValidationUnavailable();
    [[nodiscard]] bool beginValidation(const QString& job_id);
    [[nodiscard]] bool beginValidationRelease(const QString& job_id);
    [[nodiscard]] bool finishValidation(const QString& job_id, DownloadRecoveryValidationOutcome outcome);

  signals:
    void summaryChanged();

  private:
    void refreshSnapshot();
    void notifyReviewChanged();
    void notifyValidationChanged();
    enum class ValidationPhase { Idle, Pending, Held, ReleasePending };

    DownloadService& service_;
    std::vector<DownloadJobSnapshot> items_{};
    int active_count_{0};
    int completed_count_{0};
    bool refreshing_{false};
    bool refresh_requested_{false};
    bool review_available_{false};
    bool review_unavailable_{false};
    bool discard_supported_{false};
    DownloadRecoveryReviewAction review_action_{DownloadRecoveryReviewAction::ReviewFiles};
    QString review_job_id_{};
    QHash<QString, DownloadRecoveryReviewOutcome> reviews_{};
    bool validation_available_{false};
    bool validation_unavailable_{false};
    ValidationPhase validation_phase_{ValidationPhase::Idle};
    QString validation_job_id_{};
    QHash<QString, DownloadRecoveryValidationOutcome> validations_{};
};

} // namespace music_player::m0
