#include "OnlineSearchBinding.hpp"

#include <QDesktopServices>
#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVariantMap>

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace music_player::m0 {
namespace {
constexpr int poll_interval_ms = 50;
constexpr qint64 admission_deadline_ms = 10 * 1000;
constexpr std::size_t request_text_bytes = 512;

QString durationText(const std::optional<int>& seconds) {
    if (!seconds || *seconds <= 0)
        return {};
    const int hours = *seconds / 3600, minutes = (*seconds % 3600) / 60, rest = *seconds % 60;
    if (hours > 0)
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(rest, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(minutes).arg(rest, 2, 10, QLatin1Char('0'));
}
// The admission codec caps each text field; cut on a code point boundary.
std::string boundedText(std::string text) {
    if (text.size() <= request_text_bytes)
        return text;
    auto end = request_text_bytes;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U)
        --end;
    text.resize(end);
    return text;
}
} // namespace

struct OnlineSearchBinding::Impl final {
    struct Row final {
        OnlineSearchResult result;
        bool requested{false};
    };
    struct Admission final {
        std::string id;
        std::string video_id;
        QString title;
        QElapsedTimer started;
    };
    std::unique_ptr<OnlineSearch> search;
    AsyncYoutubeAudioSubmission* submission{nullptr};
    QTimer timer;
    std::optional<std::uint64_t> ticket;
    std::optional<Admission> admission;
    std::vector<Row> rows;
    QString query, status;
    bool searched{false};

    Row* row(const std::string& video_id) {
        const auto found =
            std::ranges::find_if(rows, [&](const Row& item) { return item.result.video_id == video_id; });
        return found == rows.end() ? nullptr : &*found;
    }
    void updateTimer() {
        if (ticket || admission)
            timer.start();
        else
            timer.stop();
    }
};

OnlineSearchBinding::OnlineSearchBinding(QObject* parent) : QObject(parent), impl_(std::make_unique<Impl>()) {
    impl_->timer.setInterval(poll_interval_ms);
    connect(&impl_->timer, &QTimer::timeout, this, &OnlineSearchBinding::pollBackend);
    impl_->status = tr("A busca online fica disponível quando o componente de downloads do YouTube está ativo.");
}

OnlineSearchBinding::~OnlineSearchBinding() {
    impl_->timer.stop();
    if (impl_->search && impl_->ticket)
        impl_->search->cancel();
}

void OnlineSearchBinding::setBackend(std::unique_ptr<OnlineSearch> search, AsyncYoutubeAudioSubmission* submission) {
    auto& state = *impl_;
    if (state.search && state.ticket)
        state.search->cancel();
    // Destroying the previous search drains its worker before replacement.
    state.search.reset();
    state.search = std::move(search);
    state.submission = submission;
    state.ticket.reset();
    state.admission.reset();
    state.rows.clear();
    state.query.clear();
    state.searched = false;
    state.status = available()
                       ? QString{}
                       : tr("A busca online fica disponível quando o componente de downloads do YouTube está ativo.");
    state.updateTimer();
    emit changed();
}

bool OnlineSearchBinding::available() const { return impl_->search && impl_->submission; }
bool OnlineSearchBinding::busy() const { return impl_->ticket.has_value(); }
bool OnlineSearchBinding::requesting() const { return impl_->admission.has_value(); }
bool OnlineSearchBinding::searched() const { return impl_->searched; }
QString OnlineSearchBinding::query() const { return impl_->query; }
QString OnlineSearchBinding::statusText() const { return impl_->status; }

QVariantList OnlineSearchBinding::results() const {
    QVariantList list;
    list.reserve(static_cast<qsizetype>(impl_->rows.size()));
    for (const auto& row : impl_->rows)
        list.push_back(QVariantMap{{QStringLiteral("videoId"), QString::fromStdString(row.result.video_id)},
                                   {QStringLiteral("title"), QString::fromStdString(row.result.title)},
                                   {QStringLiteral("channel"), QString::fromStdString(row.result.channel)},
                                   {QStringLiteral("durationText"), durationText(row.result.duration_seconds)},
                                   {QStringLiteral("requested"), row.requested}});
    return list;
}

bool OnlineSearchBinding::search(const QString& text) {
    auto& state = *impl_;
    const auto query = text.simplified();
    if (!available() || state.ticket || query.isEmpty())
        return false;
    std::optional<std::uint64_t> ticket;
    try {
        ticket = state.search->start(query.toStdString());
    } catch (...) {
        ticket.reset();
    }
    if (!ticket) {
        state.status = tr("Não foi possível buscar esse texto. Use até 400 caracteres, sem símbolos de controle.");
        emit changed();
        return false;
    }
    state.ticket = ticket;
    state.query = query;
    state.searched = false;
    state.rows.clear();
    state.status = tr("Buscando “%1” no YouTube…").arg(query);
    state.updateTimer();
    emit changed();
    return true;
}

void OnlineSearchBinding::cancel() {
    if (impl_->search && impl_->ticket)
        impl_->search->cancel();
}

bool OnlineSearchBinding::download(const QString& video_id) {
    auto& state = *impl_;
    auto* row = available() && !state.admission ? state.row(video_id.toStdString()) : nullptr;
    if (!row || row->requested)
        return false;
    YoutubeAudioRequest request{.request_id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()};
    request.items.push_back({.video_id = row->result.video_id, .title = boundedText(row->result.title)});
    state.admission =
        Impl::Admission{request.request_id, row->result.video_id, QString::fromStdString(row->result.title), {}};
    state.admission->started.start();
    const QPointer<OnlineSearchBinding> guard{this};
    DownloadAdmissionResult decision{.state = DownloadAdmissionState::Completed};
    try {
        decision = state.submission->begin(request);
    } catch (...) {
        // An exception cannot prove admission did not begin; observe by polling.
        decision = {.state = DownloadAdmissionState::Pending};
    }
    if (!guard || !state.admission || state.admission->id != request.request_id)
        return false;
    if (decision.state == DownloadAdmissionState::Completed) {
        finishAdmission(decision.result);
        return decision.result.error == DownloadSubmissionError::None;
    }
    state.status = tr("Enviando “%1” para os downloads…").arg(state.admission->title);
    state.updateTimer();
    emit changed();
    return true;
}

bool OnlineSearchBinding::openPreview(const QString& video_id) {
    // Only listed IDs, which the decoder already limited to 11 opaque characters.
    const auto* row = available() ? impl_->row(video_id.toStdString()) : nullptr;
    if (!row)
        return false;
    return QDesktopServices::openUrl(
        QUrl(QStringLiteral("https://www.youtube.com/watch?v=") + QString::fromStdString(row->result.video_id)));
}

void OnlineSearchBinding::pollBackend() {
    auto& state = *impl_;
    if (state.ticket && state.search) {
        std::optional<OnlineSearchOutcome> outcome;
        try {
            outcome = state.search->poll();
        } catch (...) {
            outcome = OnlineSearchOutcome{.ticket = *state.ticket, .error = OnlineSearchError::Failed};
        }
        if (outcome && outcome->ticket == *state.ticket) {
            state.ticket.reset();
            if (outcome->cancelled) {
                state.status = tr("Busca cancelada.");
            } else if (outcome->error == OnlineSearchError::Unavailable) {
                state.status = tr("A busca online não respondeu. Confira a conexão e tente de novo.");
            } else if (outcome->error == OnlineSearchError::TimedOut) {
                state.status = tr("A busca demorou demais. Tente de novo.");
            } else if (outcome->error != OnlineSearchError::None) {
                state.status = tr("A busca devolveu uma resposta inesperada. Tente de novo.");
            } else {
                state.searched = true;
                for (auto& result : outcome->results)
                    state.rows.push_back({std::move(result), false});
                const auto count = static_cast<int>(state.rows.size());
                state.status = count == 0   ? tr("Nenhum vídeo encontrado para “%1”.").arg(state.query)
                               : count == 1 ? tr("1 resultado para “%1”.").arg(state.query)
                                            : tr("%1 resultados para “%2”.").arg(count).arg(state.query);
            }
            state.updateTimer();
            emit changed();
        }
    }
    if (state.admission && state.submission) {
        const auto id = state.admission->id;
        const QPointer<OnlineSearchBinding> guard{this};
        std::optional<DownloadSubmissionResult> result;
        try {
            result = state.submission->poll(id);
        } catch (...) {
            result = DownloadSubmissionResult{};
        }
        if (!guard || !state.admission || state.admission->id != id)
            return;
        if (result || state.admission->started.hasExpired(admission_deadline_ms))
            finishAdmission(result.value_or(DownloadSubmissionResult{}));
    }
}

void OnlineSearchBinding::finishAdmission(const DownloadSubmissionResult& result) {
    auto& state = *impl_;
    if (!state.admission)
        return;
    const auto admission = std::move(*state.admission);
    state.admission.reset();
    const bool accepted =
        result.error == DownloadSubmissionError::None && result.job_ids.size() == 1 && !result.job_ids.front().empty();
    if (accepted) {
        if (auto* row = state.row(admission.video_id))
            row->requested = true;
        state.status = tr("“%1” foi para os downloads. Acompanhe em Atividade.").arg(admission.title);
    } else if (result.error == DownloadSubmissionError::Busy) {
        state.status = tr("A fila de downloads está ocupada. Confira Atividade e tente de novo.");
    } else if (result.error == DownloadSubmissionError::LimitExceeded) {
        state.status = tr("A fila de downloads está cheia. Confira Atividade antes de pedir outro.");
    } else if (result.error == DownloadSubmissionError::InvalidRequest ||
               result.error == DownloadSubmissionError::UnsupportedSource) {
        state.status = tr("Os downloads recusaram esse vídeo. Escolha outro resultado.");
    } else {
        state.status = tr("Não foi possível confirmar o pedido. Confira Atividade antes de tentar de novo.");
    }
    state.updateTimer();
    emit changed();
}
} // namespace music_player::m0
