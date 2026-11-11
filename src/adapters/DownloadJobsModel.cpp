#include "DownloadJobsModel.hpp"

#include <QScopedValueRollback>

#include <algorithm>
#include <limits>

namespace music_player::m0 {
namespace {

bool active(const DownloadJobState state) {
    return state == DownloadJobState::Queued || state == DownloadJobState::Downloading ||
           state == DownloadJobState::Converting || state == DownloadJobState::Publishing ||
           state == DownloadJobState::Cancelling || state == DownloadJobState::Importing;
}

bool hasFinalFile(const DownloadJobState state) {
    return state == DownloadJobState::Completed || state == DownloadJobState::ImportFailed ||
           state == DownloadJobState::Importing || state == DownloadJobState::Imported;
}

QString stateName(const DownloadJobState state) {
    switch (state) {
    case DownloadJobState::Queued:
        return QStringLiteral("queued");
    case DownloadJobState::Downloading:
        return QStringLiteral("downloading");
    case DownloadJobState::Converting:
        return QStringLiteral("converting");
    case DownloadJobState::Publishing:
        return QStringLiteral("publishing");
    case DownloadJobState::Cancelling:
        return QStringLiteral("cancelling");
    case DownloadJobState::Completed:
        return QStringLiteral("completed");
    case DownloadJobState::Cancelled:
        return QStringLiteral("cancelled");
    case DownloadJobState::Failed:
        return QStringLiteral("failed");
    case DownloadJobState::ImportFailed:
        return QStringLiteral("import_failed");
    case DownloadJobState::Interrupted:
        return QStringLiteral("interrupted");
    case DownloadJobState::Importing:
        return QStringLiteral("importing");
    case DownloadJobState::Imported:
        return QStringLiteral("imported");
    }
    return QStringLiteral("failed");
}

QString statusText(const DownloadJobState state) {
    switch (state) {
    case DownloadJobState::Queued:
        return DownloadJobsModel::tr("Na fila");
    case DownloadJobState::Downloading:
        return DownloadJobsModel::tr("Baixando");
    case DownloadJobState::Converting:
        return DownloadJobsModel::tr("Convertendo para MP3");
    case DownloadJobState::Publishing:
        return DownloadJobsModel::tr("Finalizando arquivo");
    case DownloadJobState::Cancelling:
        return DownloadJobsModel::tr("Cancelando");
    case DownloadJobState::Completed:
        return DownloadJobsModel::tr("Concluído");
    case DownloadJobState::Cancelled:
        return DownloadJobsModel::tr("Cancelado");
    case DownloadJobState::Failed:
        return DownloadJobsModel::tr("Falhou");
    case DownloadJobState::ImportFailed:
        return DownloadJobsModel::tr("Arquivo pronto; importação pendente");
    case DownloadJobState::Interrupted:
        return DownloadJobsModel::tr("Interrompido; revisão necessária");
    case DownloadJobState::Importing:
        return DownloadJobsModel::tr("Confirmando importação na fila");
    case DownloadJobState::Imported:
        return DownloadJobsModel::tr("Já importado na fila");
    }
    return DownloadJobsModel::tr("Falhou");
}

QString errorText(const DownloadFailure failure) {
    switch (failure) {
    case DownloadFailure::None:
        return {};
    case DownloadFailure::Unavailable:
        return DownloadJobsModel::tr("O serviço de download não está disponível. Tente novamente mais tarde.");
    case DownloadFailure::Network:
        return DownloadJobsModel::tr("Falha na conexão. Verifique a rede e selecione a mídia novamente.");
    case DownloadFailure::Expired:
        return DownloadJobsModel::tr("O endereço expirou. Atualize a página e selecione a mídia novamente.");
    case DownloadFailure::Restricted:
        return DownloadJobsModel::tr(
            "Conteúdo privado ou protegido não é compatível. Selecione outra mídia autorizada.");
    case DownloadFailure::UnsupportedMedia:
        return DownloadJobsModel::tr("Formato não compatível. Selecione outro áudio ou vídeo.");
    case DownloadFailure::NoAudio:
        return DownloadJobsModel::tr("Este arquivo não contém áudio. Selecione outra mídia.");
    case DownloadFailure::LimitExceeded:
        return DownloadJobsModel::tr("Um limite de download foi atingido. Confira o tamanho da mídia e se há "
                                     "pedidos encerrados para arquivar.");
    case DownloadFailure::Conversion:
        return DownloadJobsModel::tr("Não foi possível converter o áudio em MP3. Selecione outra mídia.");
    case DownloadFailure::Publication:
        return DownloadJobsModel::tr(
            "Não foi possível salvar o arquivo. Verifique espaço e permissões da pasta de downloads.");
    case DownloadFailure::Import:
        return DownloadJobsModel::tr("O MP3 está pronto, mas não entrou na fila. Tente importar novamente.");
    case DownloadFailure::Timeout:
        return DownloadJobsModel::tr("O download excedeu o tempo limite. Verifique a rede e tente novamente.");
    case DownloadFailure::InvalidResult:
        return DownloadJobsModel::tr("O resultado não pôde ser validado. Selecione outra mídia.");
    }
    return DownloadJobsModel::tr("Não foi possível concluir o download. Selecione outra mídia.");
}

QString pathText(const std::filesystem::path& path) {
#ifdef _WIN32
    return QString::fromStdWString(path.native());
#else
    return QString::fromStdString(path.native());
#endif
}

bool sameSnapshot(const DownloadJobSnapshot& left, const DownloadJobSnapshot& right) {
    return left.job_id == right.job_id && left.title == right.title && left.state == right.state &&
           left.completed_units == right.completed_units && left.total_units == right.total_units &&
           left.completed_mp3 == right.completed_mp3 && left.error == right.error;
}

QString reviewText(const DownloadRecoveryReviewOutcome outcome) {
    switch (outcome) {
    case DownloadRecoveryReviewOutcome::Unverified:
        return DownloadJobsModel::tr("A origem dos arquivos não pôde ser confirmada. Nada foi alterado; selecione a "
                                     "mídia novamente se necessário.");
    case DownloadRecoveryReviewOutcome::OwnedPartial:
        return DownloadJobsModel::tr("Arquivos parciais reconhecidos e preservados. Não há áudio pronto para importar; "
                                     "selecione a mídia novamente se necessário.");
    case DownloadRecoveryReviewOutcome::OwnedFinalCandidate:
        return DownloadJobsModel::tr(
            "Arquivo final reconhecido; conteúdo ainda não validado ou importado. Nada foi alterado.");
    case DownloadRecoveryReviewOutcome::Missing:
        return DownloadJobsModel::tr(
            "Nenhum arquivo deste download foi encontrado. Se necessário, selecione a mídia novamente na página.");
    case DownloadRecoveryReviewOutcome::Conflict:
        return DownloadJobsModel::tr(
            "Conflito entre os arquivos e o histórico. Tudo foi preservado; não use este resultado "
            "como um áudio validado. Você pode verificar novamente.");
    case DownloadRecoveryReviewOutcome::Unreadable:
        return DownloadJobsModel::tr("Não foi possível verificar os arquivos. Tudo foi preservado; verifique o acesso "
                                     "às pastas e tente novamente.");
    case DownloadRecoveryReviewOutcome::HistoryMissing:
        return DownloadJobsModel::tr("O histórico de arquivos deste download não foi encontrado. Não é possível "
                                     "confirmar sua origem; nada foi alterado.");
    case DownloadRecoveryReviewOutcome::HistoryInvalid:
        return DownloadJobsModel::tr("O histórico de arquivos não pôde ser validado. Tudo foi preservado; não remova "
                                     "os dados para tentar recuperar este áudio.");
    case DownloadRecoveryReviewOutcome::HistoryFuture:
        return DownloadJobsModel::tr("O histórico de arquivos pertence a uma versão mais nova. Tudo foi preservado; "
                                     "use uma versão compatível do RC Music.");
    case DownloadRecoveryReviewOutcome::Discarded:
        return DownloadJobsModel::tr(
            "Temporários descartados e histórico confirmado. O MP3 final, se existir, não foi removido.");
    case DownloadRecoveryReviewOutcome::DiscardIncomplete:
        return DownloadJobsModel::tr("O descarte dos temporários não foi concluído. Parte deles pode ter sido "
                                     "removida; o MP3 final não é alvo. Verifique novamente antes de outra tentativa.");
    case DownloadRecoveryReviewOutcome::DiscardUnconfirmed:
        return DownloadJobsModel::tr(
            "O descarte não foi confirmado no histórico. Temporários podem ter sido removidos; o MP3 final não é alvo. "
            "Não é possível afirmar que o histórico foi salvo.");
    }
    return reviewText(DownloadRecoveryReviewOutcome::Unreadable);
}

QString validationText(const DownloadRecoveryValidationOutcome outcome) {
    switch (outcome) {
    case DownloadRecoveryValidationOutcome::Validated:
    case DownloadRecoveryValidationOutcome::ImportReady:
        return DownloadJobsModel::tr("MP3 validado; não foi importado na fila. Libere a validação quando terminar a "
                                     "revisão; ela não publica nem importa o arquivo.");
    case DownloadRecoveryValidationOutcome::Released:
        return DownloadJobsModel::tr("Validação liberada. O arquivo não foi importado nem removido.");
    case DownloadRecoveryValidationOutcome::Unverified:
        return DownloadJobsModel::tr("A origem do arquivo não pôde ser confirmada. Nada foi importado; verifique "
                                     "os arquivos novamente.");
    case DownloadRecoveryValidationOutcome::OwnedPartial:
        return DownloadJobsModel::tr("Há apenas arquivos parciais; nenhum MP3 final foi validado ou importado.");
    case DownloadRecoveryValidationOutcome::Missing:
        return DownloadJobsModel::tr("O arquivo final não foi encontrado. Se necessário, selecione a mídia "
                                     "novamente na página.");
    case DownloadRecoveryValidationOutcome::Conflict:
        return DownloadJobsModel::tr("Conflito entre o arquivo e o histórico. Nada foi importado; verifique "
                                     "os arquivos novamente antes de outra tentativa.");
    case DownloadRecoveryValidationOutcome::Unreadable:
        return DownloadJobsModel::tr("Não foi possível validar o arquivo. Verifique o acesso às pastas e "
                                     "tente novamente; nada foi importado.");
    case DownloadRecoveryValidationOutcome::HistoryMissing:
        return DownloadJobsModel::tr("O histórico de arquivos deste download não foi encontrado. A validação "
                                     "não pode confirmar sua origem; nada foi importado.");
    case DownloadRecoveryValidationOutcome::HistoryInvalid:
        return DownloadJobsModel::tr("O histórico de arquivos não pôde ser validado. Não remova os dados "
                                     "para tentar recuperar este áudio; nada foi importado.");
    case DownloadRecoveryValidationOutcome::HistoryFuture:
        return DownloadJobsModel::tr("O histórico de arquivos pertence a uma versão mais nova. Use uma versão "
                                     "compatível do RC Music; nada foi importado.");
    case DownloadRecoveryValidationOutcome::InvalidMedia:
        return DownloadJobsModel::tr("Não foi possível validar este arquivo como MP3. Nada foi importado ou alterado.");
    case DownloadRecoveryValidationOutcome::Timeout:
        return DownloadJobsModel::tr("A validação de MP3 excedeu o tempo limite. Você pode tentar novamente; "
                                     "nada foi importado.");
    case DownloadRecoveryValidationOutcome::PublicationUnconfirmed:
        return DownloadJobsModel::tr("Não foi possível confirmar o arquivo no histórico. Verifique e valide o áudio "
                                     "novamente antes de adicionar à fila. Nada foi importado.");
    }
    return validationText(DownloadRecoveryValidationOutcome::Unreadable);
}

} // namespace

DownloadJobsModel::DownloadJobsModel(DownloadService& service, QObject* parent)
    : QAbstractListModel(parent), service_(service) {
    refresh();
}

int DownloadJobsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(items_.size());
}

QVariant DownloadJobsModel::data(const QModelIndex& index, const int role) const {
    if (!index.isValid() || index.model() != this || index.column() != 0 || index.row() < 0 ||
        index.row() >= rowCount())
        return {};
    const auto& item = items_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case JobIdRole:
        return QString::fromStdString(item.job_id);
    case Qt::DisplayRole:
    case TitleRole:
        return QString::fromStdString(item.title);
    case StateRole:
        return stateName(item.state);
    case StatusTextRole:
        return statusText(item.state);
    case CompletedUnitsRole:
        return QVariant::fromValue(static_cast<qulonglong>(item.completed_units));
    case TotalUnitsRole:
        return item.total_units ? QVariant::fromValue(static_cast<qulonglong>(*item.total_units)) : QVariant{};
    case IndeterminateRole:
        return active(item.state) && !hasFinalFile(item.state) && (!item.total_units || *item.total_units == 0);
    case ProgressRatioRole:
        if (hasFinalFile(item.state))
            return 1.0;
        if (!item.total_units || *item.total_units == 0)
            return 0.0;
        return std::clamp(static_cast<double>(item.completed_units) / static_cast<double>(*item.total_units), 0.0, 1.0);
    case OutputPathRole:
        return item.state != DownloadJobState::Imported && hasFinalFile(item.state) ? pathText(item.completed_mp3)
                                                                                    : QString{};
    case ErrorTextRole:
        return errorText(item.error);
    case CanCancelRole:
        return active(item.state) && item.state != DownloadJobState::Cancelling &&
               item.state != DownloadJobState::Importing;
    case CanRetryImportRole:
        return item.state == DownloadJobState::ImportFailed;
    case CanReviewFilesRole:
        return review_available_ && !review_unavailable_ && review_job_id_.isEmpty() && validation_job_id_.isEmpty() &&
               item.state == DownloadJobState::Interrupted;
    case ReviewTextRole: {
        if (item.state != DownloadJobState::Interrupted)
            return QString{};
        const auto job_id = QString::fromStdString(item.job_id);
        if (review_job_id_ == job_id)
            return review_action_ == DownloadRecoveryReviewAction::ReviewFiles
                       ? tr("Verificando arquivos locais; nada será alterado.")
                       : tr("Descartando apenas temporários próprios; o MP3 final não é alvo.");
        const auto found = reviews_.constFind(job_id);
        if (review_unavailable_ && found != reviews_.cend() && *found == DownloadRecoveryReviewOutcome::Unreadable)
            return tr("Não foi possível verificar os arquivos. Reinicie o RC Music antes de tentar novamente. Tudo foi "
                      "preservado.");
        if (review_unavailable_ && found != reviews_.cend() &&
            *found == DownloadRecoveryReviewOutcome::DiscardUnconfirmed)
            return reviewText(*found) + tr(" Reinicie o RC Music antes de verificar novamente.");
        return found != reviews_.cend() ? reviewText(*found) : QString{};
    }
    case ReviewBusyRole:
        return item.state == DownloadJobState::Interrupted && review_job_id_ == QString::fromStdString(item.job_id);
    case CanDiscardPartialsRole: {
        const auto found = reviews_.constFind(QString::fromStdString(item.job_id));
        return review_available_ && !review_unavailable_ && discard_supported_ && review_job_id_.isEmpty() &&
               validation_job_id_.isEmpty() && item.state == DownloadJobState::Interrupted &&
               found != reviews_.cend() && *found == DownloadRecoveryReviewOutcome::OwnedPartial;
    }
    case CanValidateMediaRole: {
        const auto found = reviews_.constFind(QString::fromStdString(item.job_id));
        return validation_available_ && !validation_unavailable_ && validation_job_id_.isEmpty() &&
               review_job_id_.isEmpty() && item.state == DownloadJobState::Interrupted && found != reviews_.cend() &&
               *found == DownloadRecoveryReviewOutcome::OwnedFinalCandidate;
    }
    case ValidationTextRole: {
        if (item.state != DownloadJobState::Interrupted)
            return QString{};
        const auto job_id = QString::fromStdString(item.job_id);
        if (validation_job_id_ == job_id) {
            if (validation_unavailable_)
                return tr("A validação foi interrompida. Não é possível confirmar a aprovação do áudio; "
                          "reinicie o RC Music para verificar novamente. Nada foi importado.");
            if (validation_phase_ == ValidationPhase::Pending)
                return tr("Validando o MP3 local; nada será importado ou alterado.");
            if (validation_phase_ == ValidationPhase::ReleasePending)
                return tr("Liberando a validação; aguardando a parada e a liberação dos arquivos.");
            if (validation_phase_ == ValidationPhase::PreparingImport)
                return tr("Preparando o áudio validado para adicionar à fila.");
            if (validation_phase_ == ValidationPhase::ImportPending)
                return tr("Adicionando à fila; aguardando a confirmação da gravação.");
            if (recovered_import_state_ == DownloadImportState::Failed)
                return tr("Não foi possível confirmar a gravação da fila. Tente adicionar novamente; "
                          "o áudio não será duplicado.");
        }
        const auto found = validations_.constFind(job_id);
        return found != validations_.cend() ? validationText(*found) : QString{};
    }
    case ValidationBusyRole:
        return validation_job_id_ == QString::fromStdString(item.job_id) &&
               (validation_phase_ == ValidationPhase::Pending || validation_phase_ == ValidationPhase::ReleasePending ||
                validation_phase_ == ValidationPhase::PreparingImport ||
                validation_phase_ == ValidationPhase::ImportPending);
    case CanReleaseValidationRole:
        return validation_available_ && !validation_unavailable_ &&
               validation_job_id_ == QString::fromStdString(item.job_id) &&
               (validation_phase_ == ValidationPhase::Pending || validation_phase_ == ValidationPhase::Held);
    case CanImportRecoveredRole:
        return recovered_import_available_ && validation_available_ && !validation_unavailable_ &&
               validation_job_id_ == QString::fromStdString(item.job_id) &&
               validation_phase_ == ValidationPhase::Held && item.state == DownloadJobState::Interrupted;
    default:
        return {};
    }
}

QHash<int, QByteArray> DownloadJobsModel::roleNames() const {
    return {{JobIdRole, "jobId"},
            {TitleRole, "title"},
            {StateRole, "state"},
            {StatusTextRole, "statusText"},
            {CompletedUnitsRole, "completedUnits"},
            {TotalUnitsRole, "totalUnits"},
            {IndeterminateRole, "indeterminate"},
            {ProgressRatioRole, "progressRatio"},
            {OutputPathRole, "outputPath"},
            {ErrorTextRole, "errorText"},
            {CanCancelRole, "canCancel"},
            {CanRetryImportRole, "canRetryImport"},
            {CanReviewFilesRole, "canReviewFiles"},
            {ReviewTextRole, "reviewText"},
            {ReviewBusyRole, "reviewBusy"},
            {CanDiscardPartialsRole, "canDiscardPartials"},
            {CanValidateMediaRole, "canValidateMedia"},
            {ValidationTextRole, "validationText"},
            {ValidationBusyRole, "validationBusy"},
            {CanReleaseValidationRole, "canReleaseValidation"},
            {CanImportRecoveredRole, "canImportRecovered"}};
}

int DownloadJobsModel::activeCount() const noexcept { return active_count_; }
int DownloadJobsModel::completedCount() const noexcept { return completed_count_; }
void DownloadJobsModel::notifyReviewChanged() {
    if (!items_.empty())
        emit dataChanged(
            index(0, 0), index(rowCount() - 1, 0),
            {CanReviewFilesRole, ReviewTextRole, ReviewBusyRole, CanDiscardPartialsRole, CanValidateMediaRole});
}
void DownloadJobsModel::notifyValidationChanged() {
    if (!items_.empty())
        emit dataChanged(index(0, 0), index(rowCount() - 1, 0),
                         {CanValidateMediaRole, ValidationTextRole, ValidationBusyRole, CanReleaseValidationRole,
                          CanReviewFilesRole, CanDiscardPartialsRole, CanImportRecoveredRole});
}
void DownloadJobsModel::setReviewAvailable(const bool available) {
    if (review_available_ == available)
        return;
    review_available_ = available;
    notifyReviewChanged();
}
void DownloadJobsModel::setReviewUnavailable() {
    if (review_unavailable_)
        return;
    review_unavailable_ = true;
    notifyReviewChanged();
}
void DownloadJobsModel::setDiscardSupported(const bool supported) {
    if (discard_supported_ == supported)
        return;
    discard_supported_ = supported;
    notifyReviewChanged();
}
bool DownloadJobsModel::beginReview(const QString& job_id, const DownloadRecoveryReviewAction action) {
    if (action != DownloadRecoveryReviewAction::ReviewFiles && action != DownloadRecoveryReviewAction::DiscardPartials)
        return false;
    if (!review_available_ || review_unavailable_ || !review_job_id_.isEmpty() || !validation_job_id_.isEmpty() ||
        std::ranges::none_of(items_, [&](const DownloadJobSnapshot& item) {
            return item.state == DownloadJobState::Interrupted && QString::fromStdString(item.job_id) == job_id;
        }))
        return false;
    if (action == DownloadRecoveryReviewAction::DiscardPartials) {
        const auto found = reviews_.constFind(job_id);
        if (!discard_supported_ || found == reviews_.cend() || *found != DownloadRecoveryReviewOutcome::OwnedPartial)
            return false;
        reviews_.remove(job_id);
    }
    review_action_ = action;
    review_job_id_ = job_id;
    notifyReviewChanged();
    return true;
}
bool DownloadJobsModel::finishReview(const QString& job_id, const DownloadRecoveryReviewOutcome outcome) {
    if (review_job_id_.isEmpty() || review_job_id_ != job_id)
        return false;
    reviews_.insert(job_id, outcome);
    review_job_id_.clear();
    notifyReviewChanged();
    return true;
}

void DownloadJobsModel::setValidationAvailable(const bool available) {
    if (validation_available_ == available)
        return;
    validation_available_ = available;
    notifyValidationChanged();
}
void DownloadJobsModel::setRecoveredImportAvailable(const bool available) {
    if (recovered_import_available_ == available)
        return;
    recovered_import_available_ = available;
    notifyValidationChanged();
}
bool DownloadJobsModel::beginRecoveredImportPreparation(const QString& job_id) {
    const auto found = std::ranges::find_if(
        items_, [&](const DownloadJobSnapshot& item) { return QString::fromStdString(item.job_id) == job_id; });
    if (found == items_.end() ||
        !data(index(static_cast<int>(found - items_.begin()), 0), CanImportRecoveredRole).toBool())
        return false;
    validation_phase_ = ValidationPhase::PreparingImport;
    recovered_import_state_.reset();
    notifyValidationChanged();
    return true;
}
bool DownloadJobsModel::beginRecoveredImportHandoff(const QString& job_id) {
    if (validation_unavailable_ || validation_job_id_ != job_id || validation_job_id_.isEmpty() ||
        validation_phase_ != ValidationPhase::PreparingImport)
        return false;
    validation_phase_ = ValidationPhase::ImportPending;
    recovered_import_state_ = DownloadImportState::Pending;
    notifyValidationChanged();
    return true;
}
bool DownloadJobsModel::finishRecoveredImport(const QString& job_id, const DownloadImportState state) {
    if (validation_unavailable_ || validation_job_id_.isEmpty() || validation_job_id_ != job_id ||
        (validation_phase_ != ValidationPhase::ImportPending &&
         validation_phase_ != ValidationPhase::PreparingImport) ||
        (state != DownloadImportState::Pending && state != DownloadImportState::Committed &&
         state != DownloadImportState::Failed))
        return false;
    recovered_import_state_ = state;
    validation_phase_ = state == DownloadImportState::Pending     ? ValidationPhase::ImportPending
                        : state == DownloadImportState::Committed ? ValidationPhase::ReleasePending
                                                                  : ValidationPhase::Held;
    notifyValidationChanged();
    return true;
}
void DownloadJobsModel::setValidationUnavailable() {
    if (validation_unavailable_)
        return;
    validation_unavailable_ = true;
    if (!validation_job_id_.isEmpty())
        validations_.insert(validation_job_id_, DownloadRecoveryValidationOutcome::Unreadable);
    notifyValidationChanged();
}
bool DownloadJobsModel::beginValidation(const QString& job_id) {
    const auto found = std::ranges::find_if(
        items_, [&](const DownloadJobSnapshot& item) { return QString::fromStdString(item.job_id) == job_id; });
    if (found == items_.end() ||
        !data(index(static_cast<int>(found - items_.begin()), 0), CanValidateMediaRole).toBool())
        return false;
    validation_job_id_ = job_id;
    validation_phase_ = ValidationPhase::Pending;
    recovered_import_state_.reset();
    validations_.remove(job_id);
    notifyValidationChanged();
    return true;
}
bool DownloadJobsModel::beginValidationRelease(const QString& job_id) {
    if (!validation_available_ || validation_unavailable_ || validation_job_id_.isEmpty() ||
        validation_job_id_ != job_id ||
        (validation_phase_ != ValidationPhase::Pending && validation_phase_ != ValidationPhase::Held))
        return false;
    validation_phase_ = ValidationPhase::ReleasePending;
    notifyValidationChanged();
    return true;
}
bool DownloadJobsModel::finishValidation(const QString& job_id, const DownloadRecoveryValidationOutcome outcome) {
    if (validation_job_id_.isEmpty() || validation_job_id_ != job_id ||
        outcome == DownloadRecoveryValidationOutcome::ImportReady)
        return false;
    if (outcome == DownloadRecoveryValidationOutcome::Validated) {
        if (validation_unavailable_ || validation_phase_ != ValidationPhase::Pending)
            return false;
        validation_phase_ = ValidationPhase::Held;
    } else {
        if (outcome == DownloadRecoveryValidationOutcome::Released &&
            validation_phase_ != ValidationPhase::ReleasePending)
            return false;
        validation_job_id_.clear();
        validation_phase_ = ValidationPhase::Idle;
    }
    validations_.insert(job_id, outcome);
    notifyValidationChanged();
    return true;
}

void DownloadJobsModel::refresh() {
    if (refreshing_) {
        refresh_requested_ = true;
        return;
    }
    const QScopedValueRollback refreshing_guard(refreshing_, true);
    // A synchronous observer may mutate the service while receiving a model signal.
    // Finish the current model transaction, then project its newest state. A reader
    // requesting refresh on every signal causes one silent pass, not recursive signals.
    do {
        refresh_requested_ = false;
        refreshSnapshot();
    } while (refresh_requested_);
}

void DownloadJobsModel::refreshSnapshot() {
    auto next = service_.snapshot();
    if (next.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return;
    const bool same_prefix = next.size() >= items_.size() && std::equal(items_.begin(), items_.end(), next.begin(),
                                                                        [](const auto& left, const auto& right) {
                                                                            return left.job_id == right.job_id;
                                                                        });
    if (!same_prefix) {
        // Archiving can remove rows anywhere. Surviving IDs keep their order,
        // and newly admitted IDs follow them.
        std::vector<bool> keep(items_.size(), false);
        auto first_survivor = items_.cbegin();
        bool appending = false;
        for (const auto& item : next) {
            const auto previous = std::ranges::find(items_, item.job_id, &DownloadJobSnapshot::job_id);
            if (previous == items_.end()) {
                appending = true;
                continue;
            }
            if (appending || previous < first_survivor)
                return;
            keep[static_cast<std::size_t>(previous - items_.cbegin())] = true;
            first_survivor = previous + 1;
        }
        for (std::size_t last = items_.size(); last > 0;) {
            if (keep[last - 1]) {
                --last;
                continue;
            }
            auto first = last - 1;
            while (first > 0 && !keep[first - 1])
                --first;
            beginRemoveRows({}, static_cast<int>(first), static_cast<int>(last - 1));
            for (auto row = first; row < last; ++row) {
                const auto id = QString::fromStdString(items_[row].job_id);
                reviews_.remove(id);
                validations_.remove(id);
            }
            items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(first),
                         items_.begin() + static_cast<std::ptrdiff_t>(last));
            endRemoveRows();
            last = first;
        }
    }
    const auto existing_count = items_.size();
    for (std::size_t row = 0; row < existing_count; ++row) {
        if (!sameSnapshot(items_[row], next[row])) {
            items_[row] = next[row];
            const auto changed_index = index(static_cast<int>(row), 0);
            emit dataChanged(changed_index, changed_index);
        }
    }
    if (next.size() > existing_count) {
        items_.reserve(next.size());
        beginInsertRows({}, static_cast<int>(existing_count), static_cast<int>(next.size() - 1));
        for (std::size_t row = existing_count; row < next.size(); ++row)
            items_.push_back(std::move(next[row]));
        endInsertRows();
    }
    int next_active = 0;
    int next_completed = 0;
    for (const auto& item : items_) {
        if (active(item.state))
            ++next_active;
        if (item.state == DownloadJobState::Completed || item.state == DownloadJobState::Imported)
            ++next_completed;
    }
    if (active_count_ != next_active || completed_count_ != next_completed) {
        active_count_ = next_active;
        completed_count_ = next_completed;
        emit summaryChanged();
    }
}

bool DownloadJobsModel::cancel(const QString& job_id) {
    const bool changed = service_.cancel(job_id.toStdString());
    refresh();
    return changed;
}

bool DownloadJobsModel::retryImport(const QString& job_id) {
    const bool changed = service_.retryImport(job_id.toStdString());
    refresh();
    return changed;
}

} // namespace music_player::m0
