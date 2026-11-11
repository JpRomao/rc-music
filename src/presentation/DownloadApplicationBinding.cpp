#include "presentation/DownloadApplicationBinding.hpp"

#include "adapters/DownloadJobsModel.hpp"
#include "m0/application/DownloadAdmissionController.hpp"
#include "m0/ports/DownloadRecoveryValidation.hpp"
#include "presentation/PlayerPresenter.hpp"

#include <QElapsedTimer>
#include <QPointer>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace music_player::m0 {
namespace {
bool validPreparedPath(const std::filesystem::path& path) {
    if (path.empty() || !path.is_absolute() || path != path.lexically_normal() || path.native().size() > 30'000 ||
        std::ranges::find(path.native(), 0) != path.native().end())
        return false;
    auto extension = path.extension().native();
    for (auto& value : extension)
        if (value >= 'A' && value <= 'Z')
            value = static_cast<std::filesystem::path::value_type>(value + ('a' - 'A'));
    return extension == std::filesystem::path{".mp3"}.native();
}
bool validValidationOutcome(const DownloadRecoveryValidationOutcome outcome) {
    switch (outcome) {
    case DownloadRecoveryValidationOutcome::Validated:
    case DownloadRecoveryValidationOutcome::ImportReady:
    case DownloadRecoveryValidationOutcome::PublicationUnconfirmed:
    case DownloadRecoveryValidationOutcome::Released:
    case DownloadRecoveryValidationOutcome::Unverified:
    case DownloadRecoveryValidationOutcome::OwnedPartial:
    case DownloadRecoveryValidationOutcome::Missing:
    case DownloadRecoveryValidationOutcome::Conflict:
    case DownloadRecoveryValidationOutcome::Unreadable:
    case DownloadRecoveryValidationOutcome::HistoryMissing:
    case DownloadRecoveryValidationOutcome::HistoryInvalid:
    case DownloadRecoveryValidationOutcome::HistoryFuture:
    case DownloadRecoveryValidationOutcome::InvalidMedia:
    case DownloadRecoveryValidationOutcome::Timeout:
        return true;
    }
    return false;
}
bool validReviewOutcome(const DownloadRecoveryReviewOutcome outcome, const DownloadRecoveryReviewAction action) {
    switch (outcome) {
    case DownloadRecoveryReviewOutcome::Unverified:
    case DownloadRecoveryReviewOutcome::OwnedFinalCandidate:
    case DownloadRecoveryReviewOutcome::Missing:
    case DownloadRecoveryReviewOutcome::Conflict:
    case DownloadRecoveryReviewOutcome::Unreadable:
    case DownloadRecoveryReviewOutcome::HistoryMissing:
    case DownloadRecoveryReviewOutcome::HistoryInvalid:
    case DownloadRecoveryReviewOutcome::HistoryFuture:
        return true;
    case DownloadRecoveryReviewOutcome::OwnedPartial:
        return action == DownloadRecoveryReviewAction::ReviewFiles;
    case DownloadRecoveryReviewOutcome::Discarded:
    case DownloadRecoveryReviewOutcome::DiscardIncomplete:
    case DownloadRecoveryReviewOutcome::DiscardUnconfirmed:
        return action == DownloadRecoveryReviewAction::DiscardPartials;
    }
    return false;
}
} // namespace
struct DownloadApplicationBinding::Impl {
    enum class ValidationPhase { Pending, Held, PreparingImport, ImportPending, Releasing };
    struct ValidationOperation final {
        std::uint64_t id;
        std::string job_id;
        ValidationPhase phase{ValidationPhase::Pending};
    };
    struct ReviewOperation final {
        std::uint64_t id;
        std::string job_id;
        DownloadRecoveryReviewAction action{DownloadRecoveryReviewAction::ReviewFiles};
    };
    struct DiscardConsent final {
        QString job_id;
        QString token;
    };
    struct ImportConsent final {
        QString job_id;
        QString token;
        std::uint64_t validation_id;
    };
    struct ArchiveConsent final {
        QString token;
        std::vector<DownloadJobSnapshot> jobs;
    };
    struct Projection final {
        QString status;
        QString warning;
        QString capacity;
        bool retry_startup{false};
        bool renew_namespace{false};
        bool retry_admission{false};
        bool archive_settled{false};
        bool retry_archive{false};
        bool retry_final{false};
        bool close_failed{false};
        bool operator==(const Projection&) const = default;
    } projection;
    QPointer<PlayerPresenter> presenter;
    DownloadAdmissionController& controller;
    DownloadService& download_service;
    DownloadExecutionDrainCallbacks execution;
    DownloadApplicationBindingOptions options;
    DownloadJobsModel model;
    QPointer<QObject> lyrics;
    QTimer timer;
    QElapsedTimer elapsed;
    std::int64_t last_now{0};
    bool gate_attached{false};
    bool operating{false};
    bool closing{false};
    bool before_shutdown_in_progress{false};
    bool before_shutdown_completed{false};
    bool controller_tick_in_progress{false};
    bool execution_shutdown_requested{false};
    bool execution_stopped{false};
    bool explicit_loss_accepted{false};
    bool poll_failed{false};
    std::uint64_t review_operation_id{0};
    std::optional<ReviewOperation> review_operation{};
    bool review_shutdown_requested{false};
    bool review_stopped{false};
    bool review_unavailable{false};
    std::uint64_t consent_revision{0};
    std::optional<DiscardConsent> discard_consent{};
    std::optional<ImportConsent> import_consent{};
    std::optional<ArchiveConsent> archive_consent{};
    bool discard_uncertain{false};
    std::uint64_t validation_operation_id{0};
    std::optional<ValidationOperation> validation_operation{};
    bool validation_shutdown_requested{false};
    bool validation_stopped{false};
    bool validation_unavailable{false};

    Impl(DownloadApplicationBinding& binding, PlayerPresenter& player, DownloadAdmissionController& admission,
         DownloadService& service, DownloadExecutionDrainCallbacks drain, DownloadApplicationBindingOptions settings)
        : presenter(&player), controller(admission), download_service(service), execution(std::move(drain)),
          options(std::move(settings)), model(service, &binding), timer(&binding) {
        review_stopped = options.recovery_review == nullptr;
        validation_stopped = options.recovery_validation == nullptr;
        elapsed.start();
        timer.setInterval(25);
    }
};
DownloadApplicationBinding::DownloadApplicationBinding(PlayerPresenter& presenter,
                                                       DownloadAdmissionController& controller,
                                                       DownloadService& service,
                                                       DownloadExecutionDrainCallbacks execution,
                                                       DownloadApplicationBindingOptions options, QObject* parent)
    : QObject(parent),
      impl_(std::make_unique<Impl>(*this, presenter, controller, service, std::move(execution), std::move(options))) {
    if (QThread::currentThread() != thread() || presenter.thread() != thread() || !impl_->execution.shutdown ||
        !impl_->execution.stopped)
        throw std::invalid_argument{"download binding requires same-thread owner and complete drain callbacks"};
    connect(&impl_->timer, &QTimer::timeout, this, &DownloadApplicationBinding::poll);
    refreshProjection();
}
DownloadApplicationBinding::~DownloadApplicationBinding() {
    impl_->timer.stop();
    // An unused gate can detach; a gate that has begun remains installed with
    // its expired QPointer returning Failed, never silently approving exit.
    if (impl_->gate_attached && impl_->presenter)
        static_cast<void>(impl_->presenter->setExitPreparationCallbacks({}));
}
QString DownloadApplicationBinding::importConsentJobId() const {
    return impl_->import_consent ? impl_->import_consent->job_id : QString{};
}
QString DownloadApplicationBinding::importConsentToken() const {
    return impl_->import_consent ? impl_->import_consent->token : QString{};
}
void DownloadApplicationBinding::invalidateImportConsent() {
    if (!impl_->import_consent)
        return;
    impl_->import_consent.reset();
    emit importConsentChanged();
}
bool DownloadApplicationBinding::canImportForJob(const QString& job_id) const {
    if (!impl_->gate_attached || impl_->closing || impl_->validation_unavailable || !impl_->validation_operation ||
        impl_->validation_operation->phase != Impl::ValidationPhase::Held ||
        impl_->validation_operation->job_id != job_id.toStdString() || impl_->review_operation ||
        impl_->controller.state() != DownloadAdmissionControllerState::Ready)
        return false;
    for (int row = 0; row < impl_->model.rowCount(); ++row) {
        const auto index = impl_->model.index(row, 0);
        if (impl_->model.data(index, DownloadJobsModel::JobIdRole).toString() == job_id)
            return impl_->model.data(index, DownloadJobsModel::CanImportRecoveredRole).toBool();
    }
    return false;
}
bool DownloadApplicationBinding::requestRecoveredImport(const QString& job_id) {
    if (QThread::currentThread() != thread() || impl_->operating ||
        impl_->consent_revision == std::numeric_limits<std::uint64_t>::max() || !canImportForJob(job_id))
        return false;
    const QPointer<DownloadApplicationBinding> guard{this};
    impl_->operating = true;
    invalidateDiscardConsent();
    if (!guard)
        return false;
    if (!impl_->closing) {
        impl_->import_consent =
            Impl::ImportConsent{job_id, QString::number(++impl_->consent_revision), impl_->validation_operation->id};
        emit importConsentChanged();
        if (!guard)
            return false;
    }
    impl_->operating = false;
    if (impl_->closing)
        beginShutdown();
    return guard && impl_->import_consent.has_value();
}
void DownloadApplicationBinding::cancelRecoveredImportConfirmation(const QString& job_id, const QString& token) {
    if (QThread::currentThread() != thread() || impl_->operating || !impl_->import_consent ||
        impl_->import_consent->job_id != job_id || impl_->import_consent->token != token)
        return;
    const QPointer<DownloadApplicationBinding> guard{this};
    impl_->operating = true;
    invalidateImportConsent();
    if (!guard)
        return;
    impl_->operating = false;
    if (impl_->closing)
        beginShutdown();
}
bool DownloadApplicationBinding::confirmRecoveredImport(const QString& job_id, const QString& token) {
    if (QThread::currentThread() != thread() || impl_->operating || !impl_->import_consent ||
        impl_->import_consent->job_id != job_id || impl_->import_consent->token != token || !canImportForJob(job_id) ||
        impl_->import_consent->validation_id != impl_->validation_operation->id)
        return false;
    const QPointer<DownloadApplicationBinding> guard{this};
    const auto id = impl_->validation_operation->id;
    const auto trusted_job_id = impl_->validation_operation->job_id;
    impl_->operating = true;
    invalidateImportConsent();
    if (!guard)
        return false;
    const bool eligible = !impl_->closing && impl_->download_service.canImportRecovered(trusted_job_id);
    if (!guard)
        return false;
    bool accepted = false;
    if (eligible && !impl_->closing) {
        impl_->validation_operation->phase = Impl::ValidationPhase::PreparingImport;
        const bool projected = impl_->model.beginRecoveredImportPreparation(job_id);
        if (!guard)
            return false;
        bool uncertain = false;
        if (projected && !impl_->closing) {
            try {
                const auto command = impl_->options.recovery_validation->prepareImport(id);
                accepted = command == DownloadRecoveryValidationCommand::Accepted;
                uncertain = !accepted && command != DownloadRecoveryValidationCommand::Busy;
            } catch (...) {
                uncertain = true;
            }
        }
        if (!guard)
            return false;
        if (uncertain) {
            failValidation();
            if (!guard)
                return false;
        } else if (!accepted) {
            impl_->validation_operation->phase = Impl::ValidationPhase::Held;
            static_cast<void>(impl_->model.finishRecoveredImport(job_id, DownloadImportState::Failed));
            if (!guard)
                return false;
        }
    }
    refreshProjection();
    if (!guard)
        return false;
    impl_->operating = false;
    if (impl_->closing)
        beginShutdown();
    return guard && accepted;
}
void DownloadApplicationBinding::pollRecoveredImport() {
    if (!impl_->validation_operation || impl_->validation_operation->phase != Impl::ValidationPhase::ImportPending)
        return;
    const auto operation = *impl_->validation_operation;
    const auto status = impl_->download_service.recoveredImportState(operation.job_id);
    if (status == DownloadImportState::Pending)
        return;
    const QPointer<DownloadApplicationBinding> guard{this};
    const auto result = status.value_or(DownloadImportState::Failed);
    impl_->validation_operation->phase =
        result == DownloadImportState::Committed ? Impl::ValidationPhase::Releasing : Impl::ValidationPhase::Held;
    if (!impl_->validation_unavailable) {
        static_cast<void>(impl_->model.finishRecoveredImport(QString::fromStdString(operation.job_id), result));
        if (!guard)
            return;
    }
    if (result == DownloadImportState::Committed && !impl_->validation_unavailable) {
        bool accepted = false;
        try {
            accepted = impl_->options.recovery_validation->release(operation.id);
        } catch (...) {
        }
        if (!guard)
            return;
        if (!accepted)
            failValidation();
    }
}
bool DownloadApplicationBinding::canValidateForJob(const QString& job_id) const {
    if (!impl_->gate_attached || impl_->closing || impl_->validation_unavailable || impl_->validation_operation ||
        impl_->review_operation || !impl_->options.recovery_validation ||
        impl_->controller.state() != DownloadAdmissionControllerState::Ready)
        return false;
    for (int row = 0; row < impl_->model.rowCount(); ++row) {
        const auto index = impl_->model.index(row, 0);
        if (impl_->model.data(index, DownloadJobsModel::JobIdRole).toString() == job_id &&
            impl_->model.data(index, DownloadJobsModel::CanValidateMediaRole).toBool()) {
            const auto jobs = impl_->download_service.snapshot();
            return std::ranges::any_of(jobs, [&](const DownloadJobSnapshot& job) {
                return job.state == DownloadJobState::Interrupted && QString::fromStdString(job.job_id) == job_id;
            });
        }
    }
    return false;
}
bool DownloadApplicationBinding::validateMedia(const QString& job_id) {
    if (QThread::currentThread() != thread() || impl_->operating ||
        impl_->validation_operation_id == std::numeric_limits<std::uint64_t>::max() || !canValidateForJob(job_id))
        return false;
    const QPointer<DownloadApplicationBinding> guard{this};
    const auto id = ++impl_->validation_operation_id;
    const auto trusted_job_id = job_id.toStdString();
    auto* const validation = impl_->options.recovery_validation;
    impl_->operating = true;
    impl_->validation_operation = Impl::ValidationOperation{id, trusted_job_id};
    invalidateDiscardConsent();
    if (!guard)
        return false;
    const bool projected = impl_->model.beginValidation(job_id);
    if (!guard)
        return false;
    bool accepted = false;
    bool uncertain = false;
    if (projected && !impl_->closing) {
        try {
            const auto command = validation->startValidation(id, trusted_job_id);
            accepted = command == DownloadRecoveryValidationCommand::Accepted;
            uncertain = !accepted && command != DownloadRecoveryValidationCommand::Busy;
        } catch (...) {
            uncertain = true;
        }
    }
    if (!guard)
        return false;
    if (uncertain) {
        failValidation();
        if (!guard)
            return false;
    } else if (!accepted) {
        impl_->validation_operation.reset();
        static_cast<void>(impl_->model.finishValidation(job_id, DownloadRecoveryValidationOutcome::Unreadable));
        if (!guard)
            return false;
    }
    refreshProjection();
    if (!guard)
        return false;
    impl_->operating = false;
    if (impl_->closing)
        beginShutdown();
    return guard && accepted;
}
bool DownloadApplicationBinding::releaseValidation(const QString& job_id) {
    if (QThread::currentThread() != thread() || impl_->operating || impl_->closing || impl_->validation_unavailable ||
        !impl_->validation_operation || impl_->review_operation ||
        impl_->validation_operation->job_id != job_id.toStdString() ||
        impl_->validation_operation->phase == Impl::ValidationPhase::Releasing)
        return false;
    const QPointer<DownloadApplicationBinding> guard{this};
    const auto id = impl_->validation_operation->id;
    const auto previous_phase = impl_->validation_operation->phase;
    impl_->operating = true;
    invalidateImportConsent();
    if (!guard)
        return false;
    impl_->validation_operation->phase = Impl::ValidationPhase::Releasing;
    const bool projected = impl_->model.beginValidationRelease(job_id);
    if (!guard)
        return false;
    if (!projected)
        impl_->validation_operation->phase = previous_phase;
    bool accepted = false;
    if (projected && !impl_->closing) {
        try {
            accepted = impl_->options.recovery_validation->release(id);
        } catch (...) {
            accepted = false;
        }
        if (!guard)
            return false;
        if (!accepted)
            failValidation();
        if (!guard)
            return false;
    }
    refreshProjection();
    if (!guard)
        return false;
    impl_->operating = false;
    if (impl_->closing)
        beginShutdown();
    return guard && accepted;
}
void DownloadApplicationBinding::shutdownValidation() {
    if ((!impl_->closing && !impl_->validation_unavailable) || !impl_->options.recovery_validation ||
        impl_->validation_shutdown_requested)
        return;
    if (impl_->validation_operation && impl_->download_service.recoveredImportState(
                                           impl_->validation_operation->job_id) == DownloadImportState::Pending)
        return;
    const QPointer<DownloadApplicationBinding> guard{this};
    // A shutdown can release the retained proof before exit is committed. A
    // failed final save may keep the panel visible; its approval must retire
    // before the worker can drop that proof, including a late success result.
    impl_->model.setValidationUnavailable();
    if (!guard)
        return;
    try {
        impl_->options.recovery_validation->shutdown();
        if (guard)
            impl_->validation_shutdown_requested = true;
    } catch (...) {
        if (guard)
            impl_->poll_failed = true;
    }
}
void DownloadApplicationBinding::pollValidation() {
    if (impl_->validation_operation && impl_->validation_operation->phase == Impl::ValidationPhase::ImportPending) {
        pollRecoveredImport();
        return;
    }
    if (!impl_->validation_operation || (impl_->validation_operation->phase == Impl::ValidationPhase::Held &&
                                         !impl_->closing && !impl_->validation_unavailable))
        return;
    const QPointer<DownloadApplicationBinding> guard{this};
    const auto operation = *impl_->validation_operation;
    std::optional<DownloadRecoveryValidationResult> result;
    bool failed = false;
    try {
        result = impl_->options.recovery_validation->poll(operation.id);
    } catch (...) {
        failed = true;
    }
    if (!guard || (!failed && !result))
        return;
    const bool retiring = impl_->closing || impl_->validation_unavailable;
    if (failed || result->operation_id != operation.id || result->job_id != operation.job_id ||
        !validValidationOutcome(result->outcome) ||
        (result->outcome == DownloadRecoveryValidationOutcome::ImportReady ? !validPreparedPath(result->trusted_mp3)
                                                                           : !result->trusted_mp3.empty()) ||
        (!retiring && ((result->outcome == DownloadRecoveryValidationOutcome::Validated &&
                        operation.phase != Impl::ValidationPhase::Pending) ||
                       ((result->outcome == DownloadRecoveryValidationOutcome::ImportReady ||
                         result->outcome == DownloadRecoveryValidationOutcome::PublicationUnconfirmed) &&
                        operation.phase != Impl::ValidationPhase::PreparingImport) ||
                       (result->outcome == DownloadRecoveryValidationOutcome::Released &&
                        operation.phase != Impl::ValidationPhase::Releasing)))) {
        failValidation();
        return;
    }
    if (result->outcome == DownloadRecoveryValidationOutcome::ImportReady) {
        if (retiring) {
            impl_->validation_operation->phase = Impl::ValidationPhase::Held;
            return;
        }
        impl_->validation_operation->phase = Impl::ValidationPhase::ImportPending;
        const auto projected_job_id = QString::fromStdString(operation.job_id);
        const bool projected = impl_->model.beginRecoveredImportHandoff(projected_job_id);
        if (!guard)
            return;
        if (projected && !impl_->closing) {
            static_cast<void>(impl_->download_service.beginRecoveredImport(
                operation.job_id, result->trusted_mp3, [guard, id = operation.id] {
                    return guard && !guard->impl_->closing && !guard->impl_->validation_unavailable &&
                           guard->impl_->validation_operation && guard->impl_->validation_operation->id == id &&
                           guard->impl_->validation_operation->phase == Impl::ValidationPhase::ImportPending;
                }));
            if (!guard)
                return;
            pollRecoveredImport();
        } else {
            impl_->validation_operation->phase = Impl::ValidationPhase::Held;
        }
        return;
    }
    if (result->outcome == DownloadRecoveryValidationOutcome::Validated)
        impl_->validation_operation->phase = Impl::ValidationPhase::Held;
    else
        impl_->validation_operation.reset();
    if (!retiring)
        static_cast<void>(impl_->model.finishValidation(QString::fromStdString(operation.job_id), result->outcome));
}
void DownloadApplicationBinding::failValidation() {
    const QPointer<DownloadApplicationBinding> guard{this};
    if (!impl_->validation_unavailable) {
        impl_->validation_unavailable = true;
        invalidateDiscardConsent();
        if (!guard)
            return;
        invalidateImportConsent();
        if (!guard)
            return;
        impl_->model.setValidationUnavailable();
        if (!guard)
            return;
    }
    // An exception does not prove rejection or release. Retain the correlation
    // until the worker acknowledges terminal drain or physical shutdown.
    shutdownValidation();
}
QAbstractItemModel* DownloadApplicationBinding::jobsModel() const { return &impl_->model; }
QObject* DownloadApplicationBinding::lyrics() const { return impl_->lyrics.data(); }
void DownloadApplicationBinding::setLyrics(QObject* value) {
    if (impl_->lyrics == value)
        return;
    impl_->lyrics = value;
    if (value)
        connect(value, &QObject::destroyed, this, [this] { emit stateChanged(); });
    emit stateChanged();
}
QString DownloadApplicationBinding::statusText() const { return impl_->projection.status; }
QString DownloadApplicationBinding::warningText() const { return impl_->projection.warning; }
QString DownloadApplicationBinding::historyCapacityText() const { return impl_->projection.capacity; }
bool DownloadApplicationBinding::canRetryStartup() const { return impl_->projection.retry_startup; }
bool DownloadApplicationBinding::canRenewNamespace() const { return impl_->projection.renew_namespace; }
bool DownloadApplicationBinding::canRetryAdmission() const { return impl_->projection.retry_admission; }
bool DownloadApplicationBinding::canArchiveSettled() const { return impl_->projection.archive_settled; }
bool DownloadApplicationBinding::canRetryArchive() const { return impl_->projection.retry_archive; }
bool DownloadApplicationBinding::canRetryFinalSave() const { return impl_->projection.retry_final; }
bool DownloadApplicationBinding::canCloseAfterFinalSaveFailure() const { return impl_->projection.close_failed; }
QString DownloadApplicationBinding::discardConsentJobId() const {
    return impl_->discard_consent ? impl_->discard_consent->job_id : QString{};
}
QString DownloadApplicationBinding::discardConsentToken() const {
    return impl_->discard_consent ? impl_->discard_consent->token : QString{};
}
void DownloadApplicationBinding::invalidateDiscardConsent() {
    if (!impl_->discard_consent)
        return;
    impl_->discard_consent.reset();
    emit discardConsentChanged();
}
bool DownloadApplicationBinding::canDiscardForJob(const QString& job_id) const {
    if (!impl_->gate_attached || impl_->closing || impl_->review_unavailable || impl_->review_operation ||
        impl_->validation_operation || !impl_->options.recovery_review ||
        impl_->controller.state() != DownloadAdmissionControllerState::Ready)
        return false;
    for (int row = 0; row < impl_->model.rowCount(); ++row) {
        const auto index = impl_->model.index(row, 0);
        if (impl_->model.data(index, DownloadJobsModel::JobIdRole).toString() == job_id &&
            impl_->model.data(index, DownloadJobsModel::CanDiscardPartialsRole).toBool()) {
            const auto jobs = impl_->download_service.snapshot();
            return std::ranges::any_of(jobs, [&](const DownloadJobSnapshot& job) {
                return job.state == DownloadJobState::Interrupted && QString::fromStdString(job.job_id) == job_id;
            });
        }
    }
    return false;
}
bool DownloadApplicationBinding::requestDiscardPartials(const QString& job_id) {
    if (QThread::currentThread() != thread() || impl_->operating ||
        impl_->consent_revision == std::numeric_limits<std::uint64_t>::max() || !canDiscardForJob(job_id))
        return false;
    const QPointer<DownloadApplicationBinding> guard{this};
    impl_->operating = true;
    impl_->discard_consent = Impl::DiscardConsent{job_id, QString::number(++impl_->consent_revision)};
    emit discardConsentChanged();
    if (!guard)
        return false;
    impl_->operating = false;
    if (impl_->closing)
        beginShutdown();
    return guard && impl_->discard_consent.has_value();
}
void DownloadApplicationBinding::cancelDiscardPartialsConfirmation(const QString& job_id, const QString& token) {
    if (QThread::currentThread() != thread() || impl_->operating || !impl_->discard_consent ||
        impl_->discard_consent->job_id != job_id || impl_->discard_consent->token != token)
        return;
    impl_->operating = true;
    const QPointer<DownloadApplicationBinding> guard{this};
    invalidateDiscardConsent();
    if (!guard)
        return;
    impl_->operating = false;
    if (impl_->closing)
        beginShutdown();
}
bool DownloadApplicationBinding::confirmDiscardPartials(const QString& job_id, const QString& token) {
    if (QThread::currentThread() != thread() || impl_->operating || !impl_->discard_consent ||
        impl_->discard_consent->job_id != job_id || impl_->discard_consent->token != token || !canDiscardForJob(job_id))
        return false;
    const auto trusted_job_id = impl_->discard_consent->job_id;
    const QPointer<DownloadApplicationBinding> guard{this};
    impl_->operating = true;
    invalidateDiscardConsent();
    if (!guard)
        return false;
    refreshProjection();
    if (!guard)
        return false;
    impl_->operating = false;
    if (impl_->closing) {
        beginShutdown();
        return false;
    }
    return startReviewAction(trusted_job_id, DownloadRecoveryReviewAction::DiscardPartials);
}
bool DownloadApplicationBinding::reviewFiles(const QString& job_id) {
    return startReviewAction(job_id, DownloadRecoveryReviewAction::ReviewFiles);
}
bool DownloadApplicationBinding::startReviewAction(const QString& job_id, const DownloadRecoveryReviewAction action) {
    if (QThread::currentThread() != thread() || impl_->operating || !impl_->gate_attached || impl_->closing ||
        !impl_->options.recovery_review || impl_->review_unavailable || impl_->review_operation ||
        impl_->validation_operation || impl_->review_operation_id == std::numeric_limits<std::uint64_t>::max() ||
        impl_->controller.state() != DownloadAdmissionControllerState::Ready)
        return false;
    if (action == DownloadRecoveryReviewAction::DiscardPartials && !canDiscardForJob(job_id))
        return false;
    const auto jobs = impl_->download_service.snapshot();
    const auto found = std::ranges::find_if(jobs, [&](const DownloadJobSnapshot& job) {
        return job.state == DownloadJobState::Interrupted && QString::fromStdString(job.job_id) == job_id;
    });
    if (found == jobs.end())
        return false;
    const QPointer<DownloadApplicationBinding> guard{this};
    const auto id = ++impl_->review_operation_id;
    const auto trusted_job_id = found->job_id;
    const auto projected_job_id = QString::fromStdString(trusted_job_id);
    auto* const review = impl_->options.recovery_review;
    impl_->operating = true;
    invalidateDiscardConsent();
    if (!guard)
        return false;
    impl_->review_operation = Impl::ReviewOperation{id, trusted_job_id, action};
    const bool projected = impl_->model.beginReview(projected_job_id, action);
    if (!guard)
        return false;
    bool accepted = false;
    bool failed = false;
    if (projected && !impl_->closing) {
        try {
            const auto command = action == DownloadRecoveryReviewAction::ReviewFiles
                                     ? review->startReview(id, trusted_job_id)
                                     : review->startDiscardPartials(id, trusted_job_id);
            accepted = command == DownloadRecoveryReviewCommand::Accepted;
            failed =
                command != DownloadRecoveryReviewCommand::Accepted && command != DownloadRecoveryReviewCommand::Busy;
        } catch (...) {
            failed = true;
        }
    }
    if (!guard)
        return false;
    if (failed) {
        failReview();
        if (!guard)
            return false;
    } else if (!accepted) {
        impl_->review_operation.reset();
        static_cast<void>(impl_->model.finishReview(projected_job_id, DownloadRecoveryReviewOutcome::Unreadable));
        if (!guard)
            return false;
    }
    refreshProjection();
    if (!guard)
        return false;
    impl_->operating = false;
    if (impl_->closing)
        beginShutdown();
    return guard && accepted;
}

void DownloadApplicationBinding::shutdownReview() {
    if ((!impl_->closing && !impl_->review_unavailable) || !impl_->options.recovery_review ||
        impl_->review_shutdown_requested)
        return;
    const QPointer<DownloadApplicationBinding> guard{this};
    auto* const review = impl_->options.recovery_review;
    try {
        review->shutdown();
        if (guard)
            impl_->review_shutdown_requested = true;
    } catch (...) {
        if (guard)
            impl_->poll_failed = true;
    }
}

void DownloadApplicationBinding::failReview() {
    const QPointer<DownloadApplicationBinding> guard{this};
    if (!impl_->review_unavailable) {
        impl_->review_unavailable = true;
        invalidateDiscardConsent();
        if (!guard)
            return;
        impl_->model.setReviewUnavailable();
        if (!guard)
            return;
        if (impl_->review_operation) {
            impl_->discard_uncertain = impl_->review_operation->action == DownloadRecoveryReviewAction::DiscardPartials;
            static_cast<void>(impl_->model.finishReview(QString::fromStdString(impl_->review_operation->job_id),
                                                        impl_->discard_uncertain
                                                            ? DownloadRecoveryReviewOutcome::DiscardUnconfirmed
                                                            : DownloadRecoveryReviewOutcome::Unreadable));
        }
        if (!guard)
            return;
    }
    // start may have admitted before throwing; poll may still own a completion.
    // Never invent rejection or lose the only ID that can drain that slot.
    shutdownReview();
}

void DownloadApplicationBinding::pollReview() {
    if (!impl_->review_operation)
        return;
    const QPointer<DownloadApplicationBinding> guard{this};
    const auto operation = *impl_->review_operation;
    auto* const review = impl_->options.recovery_review;
    std::optional<DownloadRecoveryReviewResult> result;
    bool failed = false;
    try {
        result = review->poll(operation.id);
    } catch (...) {
        failed = true;
    }
    if (!guard || (!failed && !result))
        return;
    if (failed || result->operation_id != operation.id || result->job_id != operation.job_id ||
        result->action != operation.action || !validReviewOutcome(result->outcome, operation.action)) {
        failReview();
        return;
    }
    impl_->review_operation.reset();
    if (!impl_->review_unavailable)
        static_cast<void>(impl_->model.finishReview(QString::fromStdString(operation.job_id), result->outcome));
}

bool DownloadApplicationBinding::start() {
    if (QThread::currentThread() != thread() || impl_->operating || impl_->closing || impl_->gate_attached ||
        !impl_->presenter || impl_->controller.state() != DownloadAdmissionControllerState::NotStarted)
        return false;
    const QPointer<DownloadApplicationBinding> guard{this};
    if (!impl_->presenter->setExitPreparationCallbacks(
            {.begin =
                 [guard] {
                     if (guard)
                         guard->beginShutdown();
                 },
             .poll = [guard] { return guard ? guard->pollExitPreparation() : ExitPreparationState::Failed; }}))
        return false;
    impl_->gate_attached = true;
    impl_->operating = true;
    const bool accepted = impl_->controller.start();
    if (!guard)
        return false;
    if (impl_->options.automatic_polling)
        impl_->timer.start();
    refreshProjection();
    if (!guard)
        return false;
    impl_->operating = false;
    if (impl_->closing)
        beginShutdown();
    return guard && accepted;
}

bool DownloadApplicationBinding::requestClosure() {
    const QPointer<DownloadApplicationBinding> guard{this};
    impl_->closing = true;
    if (impl_->before_shutdown_completed)
        return true;
    if (impl_->before_shutdown_in_progress)
        return false;
    impl_->before_shutdown_in_progress = true;
    try {
        // Retain the callable independently: prompt closure may reenter or
        // destroy the binding. No controller drain precedes its return.
        const auto before_shutdown = impl_->execution.before_shutdown;
        if (before_shutdown)
            before_shutdown();
        if (!guard)
            return false;
        impl_->before_shutdown_completed = true;
        impl_->before_shutdown_in_progress = false;
        return true;
    } catch (...) {
        if (guard) {
            impl_->before_shutdown_in_progress = false;
            impl_->poll_failed = true;
            // The hook has unwound. Seal a tick already on the core stack so
            // its successor predicate closes, but keep ingress closure pending
            // and forbid further ticks/exit until a healthy hook retry.
            if (impl_->controller_tick_in_progress)
                impl_->controller.shutdown();
        }
        return false;
    }
}

void DownloadApplicationBinding::beginShutdown() {
    if (QThread::currentThread() != thread())
        return;
    const QPointer<DownloadApplicationBinding> guard{this};
    const bool already_operating = impl_->operating;
    impl_->operating = true;
    if (!requestClosure()) {
        if (!guard || already_operating)
            return;
        refreshProjection();
        if (guard)
            impl_->operating = false;
        return;
    }
    invalidateDiscardConsent();
    if (!guard)
        return;
    invalidateImportConsent();
    if (!guard)
        return;
    invalidateArchiveConsent();
    if (!guard)
        return;
    // The controller explicitly accepts a nested shutdown intent. Publish it
    // even during our poll so its same-tick successor start predicate closes.
    impl_->controller.shutdown();
    if (!guard)
        return;
    if (already_operating)
        return;
    if (!impl_->execution_shutdown_requested) {
        try {
            const auto shutdown = impl_->execution.shutdown;
            shutdown();
            if (!guard)
                return;
            impl_->execution_shutdown_requested = true;
        } catch (...) {
            if (guard)
                impl_->poll_failed = true;
        }
    }
    if (!guard)
        return;
    shutdownReview();
    if (!guard)
        return;
    shutdownValidation();
    if (!guard)
        return;
    impl_->operating = false;
    poll();
}

void DownloadApplicationBinding::poll() {
    if (QThread::currentThread() != thread() || impl_->operating || !impl_->gate_attached)
        return;
    const QPointer<DownloadApplicationBinding> guard{this};
    impl_->operating = true;
    const auto pause_for_closure = [&] {
        if (!guard)
            return;
        refreshProjection();
        if (guard)
            impl_->operating = false;
    };
    bool clock_healthy = false;
    try {
        const auto clock = impl_->options.now_ms;
        const auto now = clock ? clock() : impl_->elapsed.elapsed();
        if (!guard)
            return;
        if (now < 0 || now < impl_->last_now)
            throw std::runtime_error{"download clock is not monotonic"};
        impl_->last_now = now;
        clock_healthy = true;
    } catch (...) {
        if (!guard)
            return;
        if (!requestClosure()) {
            pause_for_closure();
            return;
        }
    }
    impl_->poll_failed = !clock_healthy;
    // A nested player exit can request closure while a controller port is on
    // the stack. Apply it here/on the next outer tick before starting successors.
    if (impl_->closing) {
        if (!requestClosure()) {
            pause_for_closure();
            return;
        }
        impl_->controller.shutdown();
    }
    if (!guard)
        return;
    if (impl_->closing && !impl_->execution_shutdown_requested) {
        try {
            const auto shutdown = impl_->execution.shutdown;
            shutdown();
            if (!guard)
                return;
            impl_->execution_shutdown_requested = true;
        } catch (...) {
            if (!guard)
                return;
            impl_->poll_failed = true;
        }
    }
    shutdownReview();
    if (!guard)
        return;
    shutdownValidation();
    if (!guard)
        return;
    try {
        impl_->controller_tick_in_progress = true;
        impl_->controller.tick(impl_->last_now);
    } catch (...) {
        if (!guard)
            return;
        impl_->controller_tick_in_progress = false;
        impl_->poll_failed = true;
        if (!requestClosure()) {
            pause_for_closure();
            return;
        }
    }
    if (!guard)
        return;
    impl_->controller_tick_in_progress = false;
    // A nested exit may have failed to close ingress while the core tick was
    // on the stack. Do not visit any further outward ports until it succeeds.
    if (impl_->closing && !requestClosure()) {
        pause_for_closure();
        return;
    }
    pollReview();
    if (!guard)
        return;
    pollValidation();
    if (!guard)
        return;
    shutdownReview();
    if (!guard)
        return;
    shutdownValidation();
    if (!guard)
        return;
    // Do not recurse into another tick when closure arrived during a port call.
    if (impl_->closing)
        impl_->controller.shutdown();
    if (!guard)
        return;
    // Observing stopped is prompt, not a wait. Keep native drain/failures visible
    // after a reversible player exit is cancelled; only a healthy observation
    // can clear the warning or project the whole component as stopped.
    if (impl_->closing && impl_->execution_shutdown_requested) {
        try {
            const auto stopped = impl_->execution.stopped;
            const bool observed = stopped();
            if (!guard)
                return;
            impl_->execution_stopped = observed;
        } catch (...) {
            if (!guard)
                return;
            impl_->execution_stopped = false;
            impl_->poll_failed = true;
        }
    }
    if (impl_->review_shutdown_requested) {
        auto* const review = impl_->options.recovery_review;
        try {
            const bool observed = review->stopped();
            if (!guard)
                return;
            impl_->review_stopped = observed;
        } catch (...) {
            if (!guard)
                return;
            impl_->review_stopped = false;
            if (impl_->closing)
                impl_->poll_failed = true;
        }
    }
    if (impl_->validation_shutdown_requested) {
        try {
            const bool observed = impl_->options.recovery_validation->stopped();
            if (!guard)
                return;
            impl_->validation_stopped = observed;
            if (observed)
                impl_->validation_operation.reset();
        } catch (...) {
            if (!guard)
                return;
            impl_->validation_stopped = false;
            if (impl_->closing)
                impl_->poll_failed = true;
        }
    }
    impl_->model.refresh();
    if (guard) {
        refreshProjection();
        if (guard)
            impl_->operating = false;
    }
}

ExitPreparationState DownloadApplicationBinding::pollExitPreparation() {
    if (QThread::currentThread() != thread() || !impl_->gate_attached)
        return ExitPreparationState::Failed;
    if (impl_->operating)
        return ExitPreparationState::Pending;
    const QPointer<DownloadApplicationBinding> guard{this};
    poll();
    if (!guard)
        return ExitPreparationState::Failed;
    if (!impl_->before_shutdown_completed || impl_->before_shutdown_in_progress)
        return impl_->poll_failed ? ExitPreparationState::Failed : ExitPreparationState::Pending;
    if (impl_->poll_failed || impl_->controller.state() == DownloadAdmissionControllerState::FinalSaveFailed)
        return ExitPreparationState::Failed;
    if (!impl_->controller.stopped() || !impl_->execution_shutdown_requested)
        return ExitPreparationState::Pending;
    if (!impl_->execution_stopped || !impl_->review_stopped || !impl_->validation_stopped)
        return ExitPreparationState::Pending;
    const auto saved = impl_->controller.saveStatus();
    if (saved && *saved != DownloadCheckpointSaveStatus::Saved && !impl_->explicit_loss_accepted)
        return ExitPreparationState::Failed;
    // No save was required when closing a blocked/not-yet-loaded startup. The
    // controller preserves existing bytes and only drains its native resources.
    return ExitPreparationState::Ready;
}

bool DownloadApplicationBinding::performControl(const std::function<bool()>& action) {
    if (QThread::currentThread() != thread() || impl_->operating || !impl_->gate_attached)
        return false;
    const QPointer<DownloadApplicationBinding> guard{this};
    impl_->operating = true;
    const bool accepted = action();
    if (!guard)
        return false;
    refreshProjection();
    if (guard)
        impl_->operating = false;
    return accepted;
}
bool DownloadApplicationBinding::retryStartup() {
    return performControl([this] { return !impl_->closing && impl_->controller.retryStartup(); });
}
bool DownloadApplicationBinding::renewNamespace() {
    return performControl([this] {
        return !impl_->closing &&
               impl_->controller.startupRecoveryAction() == DownloadStartupRecoveryAction::RenewNamespace &&
               impl_->controller.renewStartupNamespace();
    });
}
bool DownloadApplicationBinding::retryAdmission() {
    return performControl([this] { return !impl_->closing && impl_->controller.retryAdmission(); });
}
bool DownloadApplicationBinding::archiveActionsAllowed() const {
    return impl_->gate_attached && !impl_->closing && !impl_->poll_failed && !impl_->review_operation &&
           !impl_->validation_operation && !impl_->discard_consent && !impl_->import_consent;
}
QString DownloadApplicationBinding::archiveConsentToken() const {
    return impl_->archive_consent ? impl_->archive_consent->token : QString{};
}
void DownloadApplicationBinding::invalidateArchiveConsent() {
    if (!impl_->archive_consent)
        return;
    impl_->archive_consent.reset();
    emit archiveConsentChanged();
}
bool DownloadApplicationBinding::requestArchiveConfirmation() {
    return performControl([this] {
        if (!archiveActionsAllowed() || impl_->archive_consent || !impl_->controller.canArchiveSettled() ||
            impl_->consent_revision == std::numeric_limits<std::uint64_t>::max())
            return false;
        impl_->archive_consent =
            Impl::ArchiveConsent{QString::number(++impl_->consent_revision), impl_->download_service.snapshot()};
        const QPointer<DownloadApplicationBinding> guard{this};
        emit archiveConsentChanged();
        return guard && !impl_->closing && impl_->archive_consent.has_value();
    });
}
bool DownloadApplicationBinding::confirmArchive(const QString& token) {
    return performControl([this, &token] {
        if (!impl_->archive_consent || impl_->archive_consent->token != token)
            return false;
        if (!archiveActionsAllowed() || !impl_->controller.canArchiveSettled() ||
            impl_->archive_consent->jobs != impl_->download_service.snapshot()) {
            invalidateArchiveConsent();
            return false;
        }
        // Consume consent before offering the save. Announce it after the
        // offer so synchronous observers cannot replace the reviewed plan.
        impl_->archive_consent.reset();
        const QPointer<DownloadApplicationBinding> guard{this};
        const bool accepted = impl_->controller.archiveSettled();
        if (guard)
            emit archiveConsentChanged();
        return guard && accepted;
    });
}
void DownloadApplicationBinding::cancelArchiveConfirmation(const QString& token) {
    static_cast<void>(performControl([this, &token] {
        if (!impl_->archive_consent || impl_->archive_consent->token != token)
            return false;
        invalidateArchiveConsent();
        return true;
    }));
}
bool DownloadApplicationBinding::retryArchive() {
    return performControl([this] { return archiveActionsAllowed() && impl_->controller.retryArchive(); });
}
bool DownloadApplicationBinding::retryFinalSave() {
    return performControl([this] { return impl_->closing && impl_->controller.retryFinalSave(); });
}
bool DownloadApplicationBinding::closeAfterFinalSaveFailure() {
    return performControl([this] {
        const QPointer<DownloadApplicationBinding> guard{this};
        if (!impl_->closing || !impl_->controller.closeAfterFinalSaveFailure())
            return false;
        if (!guard)
            return false;
        impl_->explicit_loss_accepted = true;
        return true;
    });
}

void DownloadApplicationBinding::refreshProjection() {
    const QPointer<DownloadApplicationBinding> guard{this};
    if (impl_->closing && !impl_->before_shutdown_completed) {
        // A failed ingress closure freezes the controller and consent model.
        // Publish only the blocked-exit notice; never query recovery ports or
        // invalidate consent before the outer closure callback has returned.
        auto next = impl_->projection;
        next.warning = tr("Não foi possível confirmar o andamento dos downloads. O encerramento permanece bloqueado.");
        next.retry_startup = false;
        next.renew_namespace = false;
        next.retry_admission = false;
        next.archive_settled = false;
        next.retry_archive = false;
        next.retry_final = false;
        next.close_failed = false;
        if (next != impl_->projection) {
            impl_->projection = std::move(next);
            emit stateChanged();
        }
        return;
    }
    bool discard_supported = false;
    if (impl_->gate_attached && impl_->options.recovery_review && !impl_->review_unavailable) {
        try {
            discard_supported = impl_->options.recovery_review->supportsDiscardPartials();
        } catch (...) {
            failReview();
        }
        if (!guard)
            return;
    }
    impl_->model.setDiscardSupported(discard_supported);
    if (!guard)
        return;
    impl_->model.setReviewAvailable(impl_->gate_attached && !impl_->closing && !impl_->review_unavailable &&
                                    impl_->options.recovery_review && !impl_->validation_operation &&
                                    impl_->controller.state() == DownloadAdmissionControllerState::Ready &&
                                    impl_->review_operation_id != std::numeric_limits<std::uint64_t>::max());
    if (!guard)
        return;
    impl_->model.setValidationAvailable(
        impl_->gate_attached && !impl_->closing && !impl_->validation_unavailable &&
        impl_->options.recovery_validation && !impl_->review_operation &&
        impl_->controller.state() == DownloadAdmissionControllerState::Ready &&
        (impl_->validation_operation || impl_->validation_operation_id != std::numeric_limits<std::uint64_t>::max()));
    if (!guard)
        return;
    const bool import_available = impl_->gate_attached && !impl_->closing && !impl_->validation_unavailable &&
                                  impl_->validation_operation &&
                                  impl_->validation_operation->phase == Impl::ValidationPhase::Held &&
                                  impl_->controller.state() == DownloadAdmissionControllerState::Ready &&
                                  impl_->download_service.canImportRecovered(impl_->validation_operation->job_id);
    if (!guard)
        return;
    impl_->model.setRecoveredImportAvailable(import_available);
    if (!guard)
        return;
    // Model notifications can synchronously request exit. Withdraw capabilities
    // again before publishing the rest of that same projection.
    if (impl_->closing) {
        impl_->model.setReviewAvailable(false);
        if (!guard)
            return;
        impl_->model.setValidationAvailable(false);
        if (!guard)
            return;
        impl_->model.setRecoveredImportAvailable(false);
        if (!guard)
            return;
    }
    if (impl_->import_consent && (!canImportForJob(impl_->import_consent->job_id) || !impl_->validation_operation ||
                                  impl_->import_consent->validation_id != impl_->validation_operation->id))
        invalidateImportConsent();
    if (!guard)
        return;
    if (impl_->discard_consent && !canDiscardForJob(impl_->discard_consent->job_id))
        invalidateDiscardConsent();
    if (!guard)
        return;
    if (impl_->archive_consent && (!archiveActionsAllowed() || !impl_->controller.canArchiveSettled() ||
                                   impl_->archive_consent->jobs != impl_->download_service.snapshot()))
        invalidateArchiveConsent();
    if (!guard)
        return;
    Impl::Projection next;
    const auto state = impl_->controller.state();
    const auto startup_action = impl_->controller.startupRecoveryAction();
    switch (state) {
    case DownloadAdmissionControllerState::NotStarted:
        next.status = tr("Downloads ainda não inicializados");
        break;
    case DownloadAdmissionControllerState::Loading:
        next.status = tr("Carregando histórico de downloads");
        break;
    case DownloadAdmissionControllerState::Ready:
        next.status = tr("Histórico de downloads carregado");
        break;
    case DownloadAdmissionControllerState::AdmissionPending:
        next.status = tr("Confirmando solicitação de download");
        break;
    case DownloadAdmissionControllerState::AdmissionFailed:
        next.status = tr("A solicitação ainda não foi confirmada");
        next.warning = tr("A gravação da solicitação falhou. O download não será iniciado sem confirmação.");
        break;
    case DownloadAdmissionControllerState::ArchivePending:
        next.status = tr("Salvando o arquivamento de pedidos encerrados");
        break;
    case DownloadAdmissionControllerState::ArchiveFailed:
        next.status = tr("O arquivamento ainda não foi confirmado");
        next.warning =
            tr("Não foi possível confirmar o salvamento do arquivamento. O histórico exibido foi preservado.");
        break;
    case DownloadAdmissionControllerState::Blocked:
        next.status = tr("Downloads bloqueados para preservar o histórico");
        if (startup_action == DownloadStartupRecoveryAction::ReloadSession)
            next.warning = tr("Reabra o RC Music para revisar a sessão da fila. Repetir a leitura de downloads não "
                              "confirma essa sessão; os dados existentes foram preservados.");
        else if (startup_action == DownloadStartupRecoveryAction::RenewNamespace)
            next.warning = tr("Não foi possível reconciliar o histórico de downloads. Selecione Recuperar histórico "
                              "para verificar novamente. Os dados e as confirmações de importação serão preservados.");
        else if (startup_action == DownloadStartupRecoveryAction::ReviewHistory)
            next.warning = tr("O histórico de downloads diverge das confirmações de importação da fila. Os dados "
                              "foram preservados. É necessária uma revisão desses históricos antes de liberar novos "
                              "downloads.");
        else
            next.warning = tr("Não foi possível reconciliar o histórico. Os dados existentes foram preservados.");
        break;
    case DownloadAdmissionControllerState::Stopping:
        next.status = tr("Encerrando downloads e salvando o histórico");
        break;
    case DownloadAdmissionControllerState::Stopped:
        next.status = impl_->execution_stopped && impl_->review_stopped && impl_->validation_stopped
                          ? tr("Downloads encerrados")
                          : tr("Finalizando encerramento dos downloads");
        break;
    case DownloadAdmissionControllerState::FinalSaveFailed:
        next.status = tr("Falha ao salvar o histórico final");
        next.warning = tr("A gravação final não foi confirmada. Tente novamente ou encerre sem confirmar o histórico.");
        break;
    }
    const auto saved = impl_->controller.saveStatus();
    if (state == DownloadAdmissionControllerState::Stopped && saved && *saved != DownloadCheckpointSaveStatus::Saved)
        next.warning = tr("O histórico final não foi confirmado; o último estado salvo foi preservado.");
    if (next.warning.isEmpty()) {
        const auto recovery = impl_->controller.recoveryReport();
        if (!recovery.session_only_job_ids.empty())
            next.warning = tr("Recibos de importação sem download correspondente foram preservados.");
        else if (std::any_of(recovery.items.begin(), recovery.items.end(), [](const DownloadRecoveryItem& item) {
                     return item.disposition == DownloadRecoveryDisposition::Unconfirmed ||
                            item.disposition == DownloadRecoveryDisposition::Interrupted;
                 }))
            next.warning = tr("Há downloads interrompidos para revisão. Eles não serão retomados automaticamente.");
    }
    if (impl_->poll_failed)
        next.warning = tr("Não foi possível confirmar o andamento dos downloads. O encerramento permanece bloqueado.");
    else if (impl_->review_unavailable)
        next.warning = impl_->discard_uncertain
                           ? tr("O descarte de temporários não foi confirmado. Reinicie o RC Music para verificar "
                                "novamente; não é possível afirmar o que foi removido ou salvo.")
                           : tr("A verificação de arquivos foi interrompida. Reinicie o RC Music para tentar "
                                "novamente; os dados foram preservados.");
    else if (impl_->validation_unavailable)
        next.warning = tr("A validação do MP3 foi interrompida. Reinicie o RC Music para verificar novamente; "
                          "não há confirmação de importação ou liberação do arquivo.");
    next.capacity = impl_->projection.capacity;
    if (state == DownloadAdmissionControllerState::Ready ||
        state == DownloadAdmissionControllerState::AdmissionPending ||
        state == DownloadAdmissionControllerState::AdmissionFailed ||
        state == DownloadAdmissionControllerState::ArchivePending ||
        state == DownloadAdmissionControllerState::ArchiveFailed) {
        const auto capacity = impl_->download_service.capacity();
        next.capacity = tr("%1/%2 pedidos · %3/%4 itens · %5/%6 bytes de pedidos")
                            .arg(static_cast<qulonglong>(capacity.requests))
                            .arg(static_cast<qulonglong>(capacity.max_requests))
                            .arg(static_cast<qulonglong>(capacity.jobs))
                            .arg(static_cast<qulonglong>(capacity.max_jobs))
                            .arg(static_cast<qulonglong>(capacity.retained_request_bytes))
                            .arg(static_cast<qulonglong>(capacity.max_retained_request_bytes));
        if (state == DownloadAdmissionControllerState::Ready && next.warning.isEmpty()) {
            if (capacity.requests >= capacity.max_requests)
                next.warning = tr("O histórico atingiu o limite de pedidos. Novos pedidos estão indisponíveis.");
            else if (capacity.jobs >= capacity.max_jobs)
                next.warning = tr("O histórico atingiu o limite de itens. Novos pedidos estão indisponíveis.");
            else if (capacity.retained_request_bytes >= capacity.max_retained_request_bytes)
                next.warning =
                    tr("O histórico atingiu o limite de dados dos pedidos. Novos pedidos estão indisponíveis.");
        }
    }
    next.retry_startup = !impl_->closing && startup_action == DownloadStartupRecoveryAction::RetryCheckpoint;
    next.renew_namespace = !impl_->closing && startup_action == DownloadStartupRecoveryAction::RenewNamespace;
    next.retry_admission = !impl_->closing && state == DownloadAdmissionControllerState::AdmissionFailed;
    next.archive_settled = archiveActionsAllowed() && impl_->controller.canArchiveSettled();
    next.retry_archive = archiveActionsAllowed() && impl_->controller.canRetryArchive();
    next.retry_final = impl_->closing && state == DownloadAdmissionControllerState::FinalSaveFailed;
    next.close_failed = next.retry_final;
    if (next == impl_->projection)
        return;
    impl_->projection = std::move(next);
    emit stateChanged();
}
} // namespace music_player::m0
