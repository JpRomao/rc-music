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
        return DownloadJobsModel::tr("O arquivo excede os limites de download. Selecione uma mídia menor.");
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
    }
    return reviewText(DownloadRecoveryReviewOutcome::Unreadable);
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
        return review_available_ && !review_unavailable_ && review_job_id_.isEmpty() &&
               item.state == DownloadJobState::Interrupted;
    case ReviewTextRole: {
        if (item.state != DownloadJobState::Interrupted)
            return QString{};
        const auto job_id = QString::fromStdString(item.job_id);
        if (review_job_id_ == job_id)
            return tr("Verificando arquivos locais; nada será alterado.");
        const auto found = reviews_.constFind(job_id);
        if (review_unavailable_ && found != reviews_.cend() && *found == DownloadRecoveryReviewOutcome::Unreadable)
            return tr("Não foi possível verificar os arquivos. Reinicie o RC Music antes de tentar novamente. Tudo foi "
                      "preservado.");
        return found != reviews_.cend() ? reviewText(*found) : QString{};
    }
    case ReviewBusyRole:
        return item.state == DownloadJobState::Interrupted && review_job_id_ == QString::fromStdString(item.job_id);
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
            {ReviewBusyRole, "reviewBusy"}};
}

int DownloadJobsModel::activeCount() const noexcept { return active_count_; }
int DownloadJobsModel::completedCount() const noexcept { return completed_count_; }
void DownloadJobsModel::notifyReviewChanged() {
    if (!items_.empty())
        emit dataChanged(index(0, 0), index(rowCount() - 1, 0), {CanReviewFilesRole, ReviewTextRole, ReviewBusyRole});
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
bool DownloadJobsModel::beginReview(const QString& job_id) {
    if (!review_available_ || review_unavailable_ || !review_job_id_.isEmpty() ||
        std::ranges::none_of(items_, [&](const DownloadJobSnapshot& item) {
            return item.state == DownloadJobState::Interrupted && QString::fromStdString(item.job_id) == job_id;
        }))
        return false;
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
    // The service retains admission order without eviction. Refuse a broken projection
    // rather than replacing identities under focused delegates or overflowing Qt rows.
    if (next.size() < items_.size() || next.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return;
    for (std::size_t row = 0; row < items_.size(); ++row) {
        if (items_[row].job_id != next[row].job_id)
            return;
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
