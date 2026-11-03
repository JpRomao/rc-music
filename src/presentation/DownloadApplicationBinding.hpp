#pragma once

#include <QAbstractItemModel>
#include <QObject>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>

namespace music_player::m0 {
class PlayerPresenter;
class DownloadAdmissionController;
class DownloadService;
class DownloadRecoveryReview;
class DownloadRecoveryValidation;
enum class ExitPreparationState;
enum class DownloadRecoveryReviewAction;

struct DownloadExecutionDrainCallbacks final {
    std::function<void()> shutdown{};
    std::function<bool()> stopped{};
};

struct DownloadApplicationBindingOptions final {
    // Test/composition seam; omitted uses a monotonic QElapsedTimer. No clock
    // callback may perform I/O, wait, or mutate borrowed controller/service.
    std::function<std::int64_t()> now_ms{};
    bool automatic_polling{true};
    // Borrowed prompt, worker-backed port; outlives this binding and its drain.
    DownloadRecoveryReview* recovery_review{nullptr};
    // Borrowed Qt-worker validation owner, including its retained candidate.
    // No path/capability is exposed to QML. Owner survives physical drain.
    DownloadRecoveryValidation* recovery_validation{nullptr};
};

// Qt application-thread owner of projection/timer/exit coordination only. The
// presenter, controller, service and execution callbacks remain borrowed and
// outlive this binding plus physical shutdown/drain. No production executor,
// URL ingress, worker waits, filesystem access or destructor I/O is introduced.
class DownloadApplicationBinding final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* jobsModel READ jobsModel CONSTANT)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString warningText READ warningText NOTIFY stateChanged)
    Q_PROPERTY(bool canRetryStartup READ canRetryStartup NOTIFY stateChanged)
    Q_PROPERTY(bool canRetryAdmission READ canRetryAdmission NOTIFY stateChanged)
    Q_PROPERTY(bool canRetryFinalSave READ canRetryFinalSave NOTIFY stateChanged)
    Q_PROPERTY(bool canCloseAfterFinalSaveFailure READ canCloseAfterFinalSaveFailure NOTIFY stateChanged)
    Q_PROPERTY(QString discardConsentJobId READ discardConsentJobId NOTIFY discardConsentChanged)
    Q_PROPERTY(QString discardConsentToken READ discardConsentToken NOTIFY discardConsentChanged)
    Q_PROPERTY(QString importConsentJobId READ importConsentJobId NOTIFY importConsentChanged)
    Q_PROPERTY(QString importConsentToken READ importConsentToken NOTIFY importConsentChanged)

  public:
    DownloadApplicationBinding(PlayerPresenter& presenter, DownloadAdmissionController& controller,
                               DownloadService& service, DownloadExecutionDrainCallbacks execution,
                               DownloadApplicationBindingOptions options = {}, QObject* parent = nullptr);
    ~DownloadApplicationBinding() override;
    [[nodiscard]] QAbstractItemModel* jobsModel() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QString warningText() const;
    [[nodiscard]] bool canRetryStartup() const;
    [[nodiscard]] bool canRetryAdmission() const;
    [[nodiscard]] bool canRetryFinalSave() const;
    [[nodiscard]] bool canCloseAfterFinalSaveFailure() const;
    [[nodiscard]] QString discardConsentJobId() const;
    [[nodiscard]] QString discardConsentToken() const;
    [[nodiscard]] QString importConsentJobId() const;
    [[nodiscard]] QString importConsentToken() const;
    // Trusted composition only, not exposed to QML. poll is deterministic when
    // automatic_polling is false. Neither method admits remote requests.
    [[nodiscard]] bool start();
    void poll();
    Q_INVOKABLE bool retryStartup();
    Q_INVOKABLE bool retryAdmission();
    Q_INVOKABLE bool retryFinalSave();
    Q_INVOKABLE bool closeAfterFinalSaveFailure();
    // Read-only assessment by exact Interrupted job ID. A typed read failure
    // permits explicit recheck; an uncertain/broken port is retired until a new
    // owner is composed, preserving its original operation for physical drain.
    Q_INVOKABLE bool reviewFiles(const QString& job_id);
    Q_INVOKABLE bool requestDiscardPartials(const QString& job_id);
    Q_INVOKABLE bool confirmDiscardPartials(const QString& job_id, const QString& token);
    Q_INVOKABLE void cancelDiscardPartialsConfirmation(const QString& job_id, const QString& token);
    Q_INVOKABLE bool validateMedia(const QString& job_id);
    Q_INVOKABLE bool releaseValidation(const QString& job_id);
    Q_INVOKABLE bool requestRecoveredImport(const QString& job_id);
    Q_INVOKABLE bool confirmRecoveredImport(const QString& job_id, const QString& token);
    Q_INVOKABLE void cancelRecoveredImportConfirmation(const QString& job_id, const QString& token);

  signals:
    void stateChanged();
    void discardConsentChanged();
    void importConsentChanged();

  private:
    void beginShutdown();
    void shutdownReview();
    void pollReview();
    void failReview();
    void shutdownValidation();
    void pollValidation();
    void failValidation();
    void pollRecoveredImport();
    void invalidateImportConsent();
    [[nodiscard]] bool canImportForJob(const QString& job_id) const;
    [[nodiscard]] bool canValidateForJob(const QString& job_id) const;
    void invalidateDiscardConsent();
    [[nodiscard]] bool canDiscardForJob(const QString& job_id) const;
    [[nodiscard]] bool startReviewAction(const QString& job_id, DownloadRecoveryReviewAction action);
    [[nodiscard]] ExitPreparationState pollExitPreparation();
    void refreshProjection();
    [[nodiscard]] bool performControl(const std::function<bool()>& action);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
