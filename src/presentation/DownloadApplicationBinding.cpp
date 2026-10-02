#include "presentation/DownloadApplicationBinding.hpp"

#include "adapters/DownloadJobsModel.hpp"
#include "m0/application/DownloadAdmissionController.hpp"
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
    struct ReviewOperation final {
        std::uint64_t id;
        std::string job_id;
        DownloadRecoveryReviewAction action{DownloadRecoveryReviewAction::ReviewFiles};
    };
    struct DiscardConsent final {
        QString job_id;
        QString token;
    };
    struct Projection final {
        QString status;
        QString warning;
        bool retry_startup{false};
        bool retry_admission{false};
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
    QTimer timer;
    QElapsedTimer elapsed;
    std::int64_t last_now{0};
    bool gate_attached{false};
    bool operating{false};
    bool closing{false};
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
    bool discard_uncertain{false};

    Impl(DownloadApplicationBinding& binding, PlayerPresenter& player, DownloadAdmissionController& admission,
         DownloadService& service, DownloadExecutionDrainCallbacks drain, DownloadApplicationBindingOptions settings)
        : presenter(&player), controller(admission), download_service(service), execution(std::move(drain)),
          options(std::move(settings)), model(service, &binding), timer(&binding) {
        review_stopped = options.recovery_review == nullptr;
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
QAbstractItemModel* DownloadApplicationBinding::jobsModel() const { return &impl_->model; }
QString DownloadApplicationBinding::statusText() const { return impl_->projection.status; }
QString DownloadApplicationBinding::warningText() const { return impl_->projection.warning; }
bool DownloadApplicationBinding::canRetryStartup() const { return impl_->projection.retry_startup; }
bool DownloadApplicationBinding::canRetryAdmission() const { return impl_->projection.retry_admission; }
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
        !impl_->options.recovery_review || impl_->controller.state() != DownloadAdmissionControllerState::Ready)
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
        impl_->review_operation_id == std::numeric_limits<std::uint64_t>::max() ||
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

void DownloadApplicationBinding::beginShutdown() {
    if (QThread::currentThread() != thread())
        return;
    const QPointer<DownloadApplicationBinding> guard{this};
    const bool already_operating = impl_->operating;
    impl_->operating = true;
    impl_->closing = true;
    invalidateDiscardConsent();
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
    impl_->operating = false;
    poll();
}

void DownloadApplicationBinding::poll() {
    if (QThread::currentThread() != thread() || impl_->operating || !impl_->gate_attached)
        return;
    const QPointer<DownloadApplicationBinding> guard{this};
    impl_->operating = true;
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
        impl_->closing = true;
    }
    impl_->poll_failed = !clock_healthy;
    // A nested player exit can request closure while a controller port is on
    // the stack. Apply it here/on the next outer tick before starting successors.
    if (impl_->closing)
        impl_->controller.shutdown();
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
    try {
        impl_->controller.tick(impl_->last_now);
    } catch (...) {
        if (!guard)
            return;
        impl_->poll_failed = true;
        impl_->closing = true;
    }
    if (!guard)
        return;
    pollReview();
    if (!guard)
        return;
    shutdownReview();
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
    if (impl_->poll_failed || impl_->controller.state() == DownloadAdmissionControllerState::FinalSaveFailed)
        return ExitPreparationState::Failed;
    if (!impl_->controller.stopped() || !impl_->execution_shutdown_requested)
        return ExitPreparationState::Pending;
    if (!impl_->execution_stopped || !impl_->review_stopped)
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
bool DownloadApplicationBinding::retryAdmission() {
    return performControl([this] { return !impl_->closing && impl_->controller.retryAdmission(); });
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
                                    impl_->options.recovery_review &&
                                    impl_->controller.state() == DownloadAdmissionControllerState::Ready &&
                                    impl_->review_operation_id != std::numeric_limits<std::uint64_t>::max());
    if (!guard)
        return;
    if (impl_->discard_consent && !canDiscardForJob(impl_->discard_consent->job_id))
        invalidateDiscardConsent();
    if (!guard)
        return;
    Impl::Projection next;
    const auto state = impl_->controller.state();
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
    case DownloadAdmissionControllerState::Blocked:
        next.status = tr("Downloads bloqueados para preservar o histórico");
        next.warning = tr("Não foi possível reconciliar o histórico. Os dados existentes foram preservados.");
        break;
    case DownloadAdmissionControllerState::Stopping:
        next.status = tr("Encerrando downloads e salvando o histórico");
        break;
    case DownloadAdmissionControllerState::Stopped:
        next.status = impl_->execution_stopped && impl_->review_stopped ? tr("Downloads encerrados")
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
    next.retry_startup = !impl_->closing && state == DownloadAdmissionControllerState::Blocked;
    next.retry_admission = !impl_->closing && state == DownloadAdmissionControllerState::AdmissionFailed;
    next.retry_final = impl_->closing && state == DownloadAdmissionControllerState::FinalSaveFailed;
    next.close_failed = next.retry_final;
    if (next == impl_->projection)
        return;
    impl_->projection = std::move(next);
    emit stateChanged();
}
} // namespace music_player::m0
