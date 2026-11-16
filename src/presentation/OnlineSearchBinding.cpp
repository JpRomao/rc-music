#include "OnlineSearchBinding.hpp"
#include "adapters/YoutubeLinks.hpp"

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
        std::string lyrics_page{};
    };
    struct Admission final {
        std::string id;
        std::vector<std::string> video_ids;
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
    // L7: pasted links waiting for their titles, and problems per line.
    bool links_mode{false};
    std::vector<PastedYoutubeLink> pending_links;
    QStringList issues;

    void leaveLinks() {
        links_mode = false;
        pending_links.clear();
        issues.clear();
    }

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
    state.leaveLinks();
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
                                   {QStringLiteral("requested"), row.requested},
                                   {QStringLiteral("hasLyricsPage"), !row.lyrics_page.empty()}});
    return list;
}

bool OnlineSearchBinding::linksMode() const { return impl_->links_mode; }
QStringList OnlineSearchBinding::linkIssues() const { return impl_->issues; }
bool OnlineSearchBinding::canDownloadAll() const {
    return available() && impl_->links_mode && !impl_->ticket && !impl_->admission &&
           std::ranges::any_of(impl_->rows, [](const Impl::Row& row) { return !row.requested; });
}

bool OnlineSearchBinding::lookupLinks(const QString& text) {
    auto& state = *impl_;
    if (!available() || state.ticket || state.admission)
        return false;
    const auto parsed = parsePastedYoutubeLinks(text);
    state.leaveLinks();
    for (const auto& issue : parsed.issues) {
        switch (issue.kind) {
        case PastedLinkIssueKind::NotALink:
            state.issues.append(tr("Linha %1: não é um link do YouTube nem do Letras.").arg(issue.line));
            break;
        case PastedLinkIssueKind::LetrasWithoutYoutube:
            state.issues.append(tr("Linha %1: falta o link do YouTube para esta página do Letras.").arg(issue.line));
            break;
        case PastedLinkIssueKind::Duplicate:
            state.issues.append(tr("Linha %1: vídeo repetido, usado só uma vez.").arg(issue.line));
            break;
        case PastedLinkIssueKind::TooMany:
            state.issues.append(tr("Linha %1: passou de 20 links. Cole o restante depois.").arg(issue.line));
            break;
        case PastedLinkIssueKind::TextTooLong:
            state.issues.append(tr("O texto colado é grande demais. Cole até 20 links por vez."));
            break;
        }
    }
    state.rows.clear();
    state.query.clear();
    state.searched = false;
    if (parsed.links.empty()) {
        state.status = parsed.issues.empty() ? tr("Cole um ou mais links do YouTube, um por linha.")
                                             : tr("Nenhum link do YouTube encontrado.");
        emit changed();
        return false;
    }
    std::vector<std::string> ids;
    for (const auto& link : parsed.links)
        ids.push_back(link.video_id);
    std::optional<std::uint64_t> ticket;
    try {
        ticket = state.search->lookup(ids);
    } catch (...) {
        ticket.reset();
    }
    if (!ticket) {
        state.status = tr("Não foi possível consultar os links agora. Tente de novo.");
        emit changed();
        return false;
    }
    state.ticket = ticket;
    state.links_mode = true;
    state.pending_links = parsed.links;
    state.status = parsed.links.size() == 1
                       ? tr("Consultando 1 link no YouTube…")
                       : tr("Consultando %1 links no YouTube…").arg(static_cast<int>(parsed.links.size()));
    state.updateTimer();
    emit changed();
    return true;
}

bool OnlineSearchBinding::downloadAll() {
    auto& state = *impl_;
    if (!canDownloadAll())
        return false;
    YoutubeAudioRequest request{.request_id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()};
    std::vector<std::string> ids;
    for (const auto& row : state.rows) {
        if (row.requested)
            continue;
        request.items.push_back(
            {.video_id = row.result.video_id, .title = boundedText(row.result.title), .lyrics_page = row.lyrics_page});
        ids.push_back(row.result.video_id);
    }
    const auto count = static_cast<int>(ids.size());
    return admit(request, std::move(ids),
                 count == 1 ? QString::fromStdString(request.items.front().title) : tr("%1 áudios").arg(count));
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
    state.leaveLinks();
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
    request.items.push_back(
        {.video_id = row->result.video_id, .title = boundedText(row->result.title), .lyrics_page = row->lyrics_page});
    return admit(request, {row->result.video_id}, QString::fromStdString(row->result.title));
}

bool OnlineSearchBinding::admit(const YoutubeAudioRequest& request, std::vector<std::string> video_ids,
                                const QString& title) {
    auto& state = *impl_;
    state.admission = Impl::Admission{request.request_id, std::move(video_ids), title, {}};
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
            } else if (state.links_mode) {
                state.searched = true;
                // Keep the pasted order and each link's Letras page; a link
                // without a result is private, removed or not a video.
                for (const auto& link : state.pending_links) {
                    const auto found = std::ranges::find_if(outcome->results, [&](const OnlineSearchResult& result) {
                        return result.video_id == link.video_id;
                    });
                    if (found == outcome->results.end())
                        state.issues.append(
                            tr("Linha %1: vídeo não encontrado no YouTube (privado ou removido).").arg(link.line));
                    else
                        state.rows.push_back({*found, false, link.lyrics_page});
                }
                const auto count = static_cast<int>(state.rows.size());
                state.status = count == 0   ? tr("Nenhum dos links foi encontrado no YouTube.")
                               : count == 1 ? tr("1 link pronto para baixar.")
                                            : tr("%1 links prontos para baixar.").arg(count);
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
    const bool accepted = result.error == DownloadSubmissionError::None &&
                          result.job_ids.size() == admission.video_ids.size() &&
                          std::ranges::none_of(result.job_ids, [](const std::string& job) { return job.empty(); });
    if (accepted) {
        for (const auto& video_id : admission.video_ids)
            if (auto* row = state.row(video_id))
                row->requested = true;
        state.status = admission.video_ids.size() == 1
                           ? tr("“%1” foi para os downloads. Acompanhe em Atividade.").arg(admission.title)
                           : tr("%1 foram para os downloads. Acompanhe em Atividade.").arg(admission.title);
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
