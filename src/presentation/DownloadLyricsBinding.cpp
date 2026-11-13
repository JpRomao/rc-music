#include "DownloadLyricsBinding.hpp"
#include "LyricsBinding.hpp"

#include <QDesktopServices>
#include <QPointer>
#include <QUrl>
#include <QUuid>
#include <QVariantMap>

#include <algorithm>
#include <optional>
#include <utility>

namespace music_player::m0 {
namespace {
QString owned(const QString& value) { return QString(value.constData(), value.size()); }
bool bounded(const QString& value, const qsizetype maximum) {
    if (value.size() > maximum)
        return false;
    const auto bytes = value.toUtf8();
    return bytes.size() <= maximum && QString::fromUtf8(bytes) == value;
}
std::optional<std::string> pastedContent(const QString& content) {
    if (!bounded(content, static_cast<qsizetype>(lyrics_max_input_bytes)))
        return std::nullopt;
    auto pasted = content.toUtf8().toStdString();
    const auto parsed = parseLrc(pasted);
    if (parsed.error != LyricsParseError::None ||
        parsed.document.plain_text.find_first_not_of(" \t\r\n") == std::string::npos ||
        !lyricsDocumentAsLrc(parsed.document))
        return std::nullopt;
    return pasted;
}
std::optional<LyricsVariant> versionDraft(std::string content, std::string source, const QString& name,
                                          const QString& language, const QString& script, const QString& kind) {
    if (!bounded(name, maximum_lyrics_variant_name_bytes) || !bounded(language, maximum_lyrics_variant_tag_bytes) ||
        !bounded(script, maximum_lyrics_variant_tag_bytes) || kind.size() > 11)
        return std::nullopt;
    const auto variant_kind = lyricsVariantKindFromName(kind.toLatin1().toStdString());
    if (!variant_kind)
        return std::nullopt;
    LyricsVariant draft{1,
                        name.trimmed().toUtf8().toStdString(),
                        language.trimmed().toUtf8().toStdString(),
                        script.trimmed().toUtf8().toStdString(),
                        std::move(source),
                        std::move(content),
                        *variant_kind,
                        false};
    if (!isValidLyricsVariants({draft}))
        return std::nullopt;
    return draft;
}
PublishedLyricsRequest owned(const PublishedLyricsRequest& request) {
    return {request.job_id,        owned(request.final_mp3),   owned(request.title),
            owned(request.artist), owned(request.lyrics_page), owned(request.album),
            request.duration_ms};
}
QString newToken() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool pending(const AcquiredLyricsSaveState state) {
    return state == AcquiredLyricsSaveState::Loading || state == AcquiredLyricsSaveState::Saving ||
           state == AcquiredLyricsSaveState::Conflict || state == AcquiredLyricsSaveState::LoadFailed ||
           state == AcquiredLyricsSaveState::SaveFailed || state == AcquiredLyricsSaveState::BlockedByFutureVersion ||
           state == AcquiredLyricsSaveState::Invalid;
}
QString recordingNotice(const LyricsSearchError error, const bool found) {
    switch (error) {
    case LyricsSearchError::None:
        return found ? DownloadLyricsBinding::tr("Escolha uma gravação do LRCLIB.")
                     : DownloadLyricsBinding::tr("Nenhuma gravação encontrada no LRCLIB.");
    case LyricsSearchError::InvalidQuery:
        return DownloadLyricsBinding::tr("Confira o título, o artista e o álbum da busca.");
    case LyricsSearchError::Unavailable:
        return DownloadLyricsBinding::tr("A busca de letras está indisponível agora.");
    case LyricsSearchError::RateLimited:
        return DownloadLyricsBinding::tr("Aguarde antes de buscar novamente no LRCLIB.");
    case LyricsSearchError::Timeout:
        return DownloadLyricsBinding::tr("A busca no LRCLIB excedeu o tempo limite.");
    case LyricsSearchError::Network:
        return DownloadLyricsBinding::tr("Não foi possível conectar ao LRCLIB.");
    case LyricsSearchError::AccessDenied:
        return DownloadLyricsBinding::tr("O LRCLIB não permitiu a consulta.");
    case LyricsSearchError::InvalidResponse:
        return DownloadLyricsBinding::tr("O LRCLIB enviou uma resposta inválida.");
    case LyricsSearchError::ResponseTooLarge:
        return DownloadLyricsBinding::tr("A resposta do LRCLIB excedeu os limites. Tente uma busca mais específica.");
    }
    return {};
}
} // namespace

struct DownloadLyricsBinding::Impl final {
    DownloadLyricsBinding& owner;
    DownloadLyricsLookup& lookup;
    LyricsBinding& writer;
    std::optional<PublishedLyricsRequest> request;
    std::optional<DownloadLyricsLookup::Result> preview;
    std::vector<LyricsSearchCandidate> recordings;
    QString recording_token, recording_notice;
    quint64 recording_revision{};
    QString token, notice, save_target;
    std::string save_ticket;
    bool writing_call{}, recording_busy{}, preparing{}, stopping{};

    Impl(DownloadLyricsBinding& view, DownloadLyricsLookup& source, LyricsBinding& destination)
        : owner(view), lookup(source), writer(destination) {
        QObject::connect(&lookup, &DownloadLyricsLookup::changed, &owner, [this] { refreshLookup(); });
        QObject::connect(&writer, &LyricsBinding::changed, &owner, [this] { emit owner.changed(); });
    }
    bool allowed() const { return !preparing && !stopping; }
    bool own(const AcquiredLyricsSaveSnapshot& snapshot) const {
        return !save_ticket.empty() && snapshot.ticket == save_ticket && snapshot.target == save_target;
    }
    bool ownPending() const {
        if (writing_call)
            return true;
        const auto snapshot = writer.acquiredLyricsSave();
        return own(snapshot) && pending(snapshot.state);
    }
    bool busy() const {
        if (writing_call || recording_busy || (request && lookup.state() == DownloadLyricsLookup::State::Searching))
            return true;
        const auto snapshot = writer.acquiredLyricsSave();
        return own(snapshot) && (snapshot.state == AcquiredLyricsSaveState::Loading ||
                                 snapshot.state == AcquiredLyricsSaveState::Saving);
    }
    const DownloadLyricsLookup::Result* currentPreview() const {
        const auto& actual = lookup.result();
        if (!request || !preview || !actual || lookup.state() != DownloadLyricsLookup::State::Ready ||
            actual->generation == 0 || actual->generation != preview->generation || actual->job_id != request->job_id ||
            actual->final_mp3 != request->final_mp3 || preview->job_id != request->job_id ||
            preview->final_mp3 != request->final_mp3)
            return nullptr;
        return &*preview;
    }
    bool canSavePasted() const {
        return allowed() && request && !ownPending() && !writer.variantsBusy() && !writer.variantsHasPendingChange();
    }
    bool canSave() const {
        const auto* snapshot = currentPreview();
        return canSavePasted() && snapshot && !snapshot->copy_protected;
    }
    bool canRetry() const {
        return allowed() && request && !ownPending() && !recording_busy &&
               lookup.state() != DownloadLyricsLookup::State::Searching;
    }
    void clearRecordings() {
        ++recording_revision;
        recording_busy = false;
        recordings.clear();
        recording_token.clear();
        recording_notice.clear();
    }
    const LyricsDocument* recordingFormat(const QString& request_token, const QString& result_token, const QString& id,
                                          const int format) const {
        if (!allowed() || !request || token != request_token || recording_token.isEmpty() ||
            recording_token != result_token || format < 0 || id.size() > 19)
            return nullptr;
        const auto found = std::ranges::find_if(
            recordings, [&id](const auto& candidate) { return QString::number(candidate.id) == id; });
        if (found == recordings.end() || static_cast<std::size_t>(format) >= found->formats.size())
            return nullptr;
        return &found->formats[static_cast<std::size_t>(format)];
    }
    bool canRetrySave() const {
        const auto snapshot = writer.acquiredLyricsSave();
        return allowed() && !writing_call && own(snapshot) && pending(snapshot.state) && writer.variantsCanRetrySave();
    }
    QString pendingDiscardToken() const {
        const auto snapshot = writer.acquiredLyricsSave();
        if (!allowed() || !request || writing_call || !own(snapshot) ||
            snapshot.state != AcquiredLyricsSaveState::Invalid || !writer.variantsCanRetrySave())
            return {};
        return QString::fromStdString(snapshot.ticket);
    }
    bool canDismiss() const { return allowed() && request && !ownPending(); }
    void refreshLookup() {
        const auto& result = lookup.result();
        if (allowed() && request && result && lookup.state() == DownloadLyricsLookup::State::Ready &&
            result->generation != 0 && result->job_id == request->job_id && result->final_mp3 == request->final_mp3)
            preview = *result;
        else
            preview.reset();
        emit owner.changed();
    }
    bool invalidSave() {
        notice = DownloadLyricsBinding::tr("Confira o texto, o nome, a forma e os limites da versão antes de guardar.");
        emit owner.changed();
        return false;
    }
    bool invalidBatchVersion(const qsizetype index) {
        notice =
            DownloadLyricsBinding::tr("Confira o texto, o nome e a forma da versão %1 do conjunto.").arg(index + 1);
        emit owner.changed();
        return false;
    }
    bool saveVersion(std::string content, std::string source, const QString& name, const QString& language,
                     const QString& script, const QString& kind) {
        auto draft = versionDraft(std::move(content), std::move(source), name, language, script, kind);
        return draft ? saveVersions({std::move(*draft)}) : invalidSave();
    }
    bool saveVersions(LyricsVariants drafts) {
        const auto expected_token = token;
        const auto ticket = newToken().toStdString();
        const auto target = owned(request->final_mp3);
        save_ticket = ticket;
        save_target = target;
        notice.clear();
        // Reserve before the shared writer emits changes from inside this call.
        writing_call = true;
        const bool accepted = writer.appendAcquiredLyrics(ticket, target, std::move(drafts), true);
        writing_call = false;
        if (!accepted && !ownPending()) {
            save_ticket.clear();
            save_target.clear();
        }
        if (token == expected_token)
            emit owner.changed();
        return accepted;
    }
    QString statusText() const {
        const auto snapshot = writer.acquiredLyricsSave();
        QString action;
        if (own(snapshot)) {
            switch (snapshot.state) {
            case AcquiredLyricsSaveState::Loading:
            case AcquiredLyricsSaveState::Saving:
                action = DownloadLyricsBinding::tr("Guardando letras…");
                break;
            case AcquiredLyricsSaveState::Saved:
                action = DownloadLyricsBinding::tr("Letras guardadas.");
                break;
            case AcquiredLyricsSaveState::Unchanged:
                action = DownloadLyricsBinding::tr("O conteúdo já estava guardado.");
                break;
            case AcquiredLyricsSaveState::LoadFailed:
            case AcquiredLyricsSaveState::SaveFailed:
            case AcquiredLyricsSaveState::BlockedByFutureVersion:
            case AcquiredLyricsSaveState::Conflict:
                action = DownloadLyricsBinding::tr(
                    "O salvamento está pendente. Tente guardar novamente; o áudio foi preservado.");
                break;
            case AcquiredLyricsSaveState::Invalid:
                action = DownloadLyricsBinding::tr("A versão excede os limites do catálogo ou contém dados inválidos.");
                break;
            case AcquiredLyricsSaveState::Abandoned:
                action = DownloadLyricsBinding::tr("Tentativa de guardar a letra descartada.");
                break;
            case AcquiredLyricsSaveState::Idle:
                break;
            }
        }
        if (!notice.isEmpty())
            action = notice;
        if (!action.isEmpty())
            return lookup.providerNotice() + QStringLiteral(" ") + action;
        if (!request)
            return {};
        if (!recording_notice.isEmpty())
            return recording_notice;
        const auto lookup_message = lookup.message();
        return lookup_message.isEmpty() ? lookup.providerNotice() : lookup_message;
    }
};

DownloadLyricsBinding::DownloadLyricsBinding(DownloadLyricsLookup& lookup, LyricsBinding& writer, QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>(*this, lookup, writer)) {}
DownloadLyricsBinding::~DownloadLyricsBinding() {
    impl_->stopping = true;
    QObject::disconnect(&impl_->lookup, nullptr, this, nullptr);
    QObject::disconnect(&impl_->writer, nullptr, this, nullptr);
    impl_->lookup.clear(); // Never abandon or retry the separately owned writer.
}
bool DownloadLyricsBinding::offerPublished(PublishedLyricsRequest request) {
    auto& value = *impl_;
    if (!value.allowed() || value.request || !DownloadLyricsLookup::validPublication(request))
        return false;
    const auto captured = owned(request);
    const auto expected_token = newToken();
    value.request = captured;
    value.token = expected_token;
    value.preview.reset();
    value.notice.clear();
    value.save_ticket.clear();
    value.save_target.clear();
    const bool accepted = value.lookup.request(captured);
    if (value.token != expected_token || !value.request || !value.allowed())
        return false;
    if (!accepted) {
        value.request.reset();
        value.token.clear();
        value.preview.reset();
        emit changed();
        return false;
    }
    value.refreshLookup();
    return value.allowed() && value.request && value.token == expected_token;
}
bool DownloadLyricsBinding::hasRequest() const { return impl_->request.has_value(); }
bool DownloadLyricsBinding::busy() const { return impl_->busy(); }
bool DownloadLyricsBinding::canSave() const { return impl_->canSave(); }
bool DownloadLyricsBinding::canSavePasted() const { return impl_->canSavePasted(); }
bool DownloadLyricsBinding::canRetry() const { return impl_->canRetry(); }
bool DownloadLyricsBinding::canRetrySave() const { return impl_->canRetrySave(); }
QString DownloadLyricsBinding::pendingDiscardToken() const { return impl_->pendingDiscardToken(); }
bool DownloadLyricsBinding::canDismiss() const { return impl_->canDismiss(); }
QString DownloadLyricsBinding::title() const { return impl_->request ? impl_->request->title : QString{}; }
QString DownloadLyricsBinding::artist() const { return impl_->request ? impl_->request->artist : QString{}; }
QString DownloadLyricsBinding::album() const { return impl_->request ? impl_->request->album : QString{}; }
bool DownloadLyricsBinding::hasLyricsPage() const {
    return impl_->allowed() && impl_->request && !impl_->request->lyrics_page.isEmpty();
}
QString DownloadLyricsBinding::requestToken() const { return impl_->token; }
QString DownloadLyricsBinding::statusText() const { return impl_->statusText(); }
QString DownloadLyricsBinding::lyricsAttribution() const {
    const auto* snapshot = impl_->currentPreview();
    return snapshot && snapshot->copy_protected ? snapshot->attribution : QString{};
}
bool DownloadLyricsBinding::lyricsCopyProtected() const {
    const auto* snapshot = impl_->currentPreview();
    return snapshot && snapshot->copy_protected;
}
QVariantList DownloadLyricsBinding::candidates() const {
    QVariantList result;
    const auto* snapshot = impl_->currentPreview();
    if (!impl_->allowed() || !snapshot)
        return result;
    for (std::size_t index = 0; index < snapshot->candidates.size(); ++index) {
        const auto& document = snapshot->candidates[index];
        result.push_back(QVariantMap{
            {"index", static_cast<int>(index)},
            {"label",
             tr("Versão %1 · %2").arg(index + 1).arg(document.lines.empty() ? tr("texto") : tr("sincronizada"))},
            {"text", QString::fromStdString(document.plain_text)},
            {"synced", !document.lines.empty()}});
    }
    return result;
}
QVariantList DownloadLyricsBinding::recordings() const {
    QVariantList result;
    const auto& value = *impl_;
    if (!value.allowed())
        return result;
    for (const auto& candidate : value.recordings) {
        QVariantList formats;
        for (const auto& document : candidate.formats)
            formats.push_back(
                QVariantMap{{"label", document.lines.empty() ? tr("Texto simples") : tr("Sincronizada")}});
        result.push_back(QVariantMap{{"id", QString::number(candidate.id)},
                                     {"title", candidate.recording.title},
                                     {"artist", candidate.recording.artist},
                                     {"album", candidate.recording.album},
                                     {"durationMs", candidate.recording.duration_ms},
                                     {"instrumental", candidate.instrumental},
                                     {"formats", std::move(formats)}});
    }
    return result;
}
QString DownloadLyricsBinding::recordingResultToken() const { return impl_->recording_token; }
bool DownloadLyricsBinding::recordingSearchBusy() const { return impl_->recording_busy; }
bool DownloadLyricsBinding::openLyricsPage(const QString& expected_request_token) {
    auto& value = *impl_;
    if (!hasLyricsPage() || expected_request_token != value.token)
        return false;
    // The request already passed validPublication. The view supplies only its identity.
    const QUrl page(value.request->lyrics_page, QUrl::StrictMode);
    if (QDesktopServices::openUrl(page))
        return true;
    if (value.allowed() && expected_request_token == value.token) {
        value.notice = tr("Não foi possível abrir a página no navegador.");
        emit changed();
    }
    return false;
}
bool DownloadLyricsBinding::retry(const QString& title, const QString& artist) {
    auto& value = *impl_;
    if (!value.canRetry() || !bounded(title, 512) || !bounded(artist, 512))
        return false;
    auto captured = owned(*value.request);
    captured.title = owned(title);
    captured.artist = owned(artist);
    const auto expected_token = value.token;
    value.request = captured;
    value.preview.reset();
    value.clearRecordings();
    value.notice.clear();
    // The retained trusted target also permits an explicit retry after exit was
    // cancelled. It never asks the audio service to acquire the file again.
    const bool accepted = value.lookup.request(captured);
    if (!accepted || !value.allowed() || !value.request || value.token != expected_token)
        return false;
    value.refreshLookup();
    return value.allowed() && value.request && value.token == expected_token &&
           (value.lookup.state() == DownloadLyricsLookup::State::Searching ||
            value.lookup.state() == DownloadLyricsLookup::State::Ready);
}
bool DownloadLyricsBinding::searchRecordings(const QString& expected_request_token, const QString& title,
                                             const QString& artist) {
    auto& value = *impl_;
    if (!value.canRetry() || expected_request_token != value.token || !bounded(title, 512) || !bounded(artist, 512))
        return false;
    const auto captured_title = owned(title);
    const auto captured_artist = owned(artist);
    value.clearRecordings();
    value.recording_busy = true;
    value.recording_notice = tr("Buscando gravações no LRCLIB…");
    const auto expected = value.recording_revision;
    const QPointer<DownloadLyricsBinding> guard(this);
    emit changed();
    if (!guard || value.recording_revision != expected || !value.allowed())
        return false;
    const auto completed = [guard, expected](LyricsSearchResult result) {
        if (!guard || guard->impl_->recording_revision != expected || !guard->impl_->allowed())
            return;
        auto& state = *guard->impl_;
        state.recording_busy = false;
        state.recordings = std::move(result.candidates);
        state.recording_notice = recordingNotice(result.error, !state.recordings.empty());
        if (!state.recordings.empty())
            state.recording_token = newToken();
        emit guard->changed();
    };
    return value.lookup.searchRecordings(captured_title, captured_artist, completed);
}
void DownloadLyricsBinding::cancelRecordingSearch(const QString& expected_request_token) {
    auto& value = *impl_;
    if (expected_request_token != value.token || (!value.recording_busy && value.recording_notice.isEmpty()))
        return;
    value.clearRecordings();
    value.lookup.cancelRecordingSearch();
    emit changed();
}
QString DownloadLyricsBinding::previewRecordingCandidate(const QString& expected_request_token,
                                                         const QString& expected_result_token, const QString& id,
                                                         const int format) const {
    const auto* document = impl_->recordingFormat(expected_request_token, expected_result_token, id, format);
    return document ? QString::fromStdString(document->plain_text) : QString{};
}
bool DownloadLyricsBinding::saveRecordingCandidate(const QString& expected_request_token,
                                                   const QString& expected_result_token, const QString& id,
                                                   const int format, const QString& name, const QString& language,
                                                   const QString& script, const QString& kind) {
    auto& value = *impl_;
    if (!value.canSavePasted())
        return false;
    const auto* document = value.recordingFormat(expected_request_token, expected_result_token, id, format);
    if (!document)
        return false;
    auto content = lyricsDocumentAsLrc(*document);
    return content ? value.saveVersion(std::move(*content), "LRCLIB", name, language, script, kind)
                   : value.invalidSave();
}
bool DownloadLyricsBinding::saveCandidate(const int index, const QString& name, const QString& language,
                                          const QString& script, const QString& kind) {
    auto& value = *impl_;
    if (!value.canSave())
        return false;
    const auto* available = value.currentPreview();
    if (index < 0 || static_cast<std::size_t>(index) >= available->candidates.size())
        return value.invalidSave();
    auto content = lyricsDocumentAsLrc(available->candidates[static_cast<std::size_t>(index)]);
    if (!content)
        return value.invalidSave();
    return value.saveVersion(std::move(*content), available->source.toUtf8().toStdString(), name, language, script,
                             kind);
}
bool DownloadLyricsBinding::savePasted(const QString& expected_request_token, const QString& content,
                                       const QString& name, const QString& language, const QString& script,
                                       const QString& kind, const bool from_lyrics_page) {
    auto& value = *impl_;
    if (!value.canSavePasted() || expected_request_token != value.token)
        return false;
    if (from_lyrics_page && !hasLyricsPage())
        return false;
    auto pasted = pastedContent(content);
    if (!pasted)
        return value.invalidSave();
    // Keep the user's source text. The shared writer compares canonical lyrics for duplicates.
    auto source = from_lyrics_page ? value.request->lyrics_page.toUtf8().toStdString() : std::string{"manual"};
    return value.saveVersion(std::move(*pasted), std::move(source), name, language, script, kind);
}
bool DownloadLyricsBinding::saveLetrasVersions(const QString& expected_request_token, const QVariantList& versions) {
    auto& value = *impl_;
    if (!value.canSavePasted() || expected_request_token != value.token || !hasLyricsPage())
        return false;
    if (versions.empty() || static_cast<std::size_t>(versions.size()) > maximum_lyrics_variants)
        return value.invalidSave();
    const auto source = value.request->lyrics_page.toUtf8().toStdString();
    LyricsVariants drafts;
    drafts.reserve(static_cast<std::size_t>(versions.size()));
    for (qsizetype index = 0; index < versions.size(); ++index) {
        const auto fields = versions[index].toMap();
        auto content = pastedContent(fields.value("content").toString());
        if (!content)
            return value.invalidBatchVersion(index);
        auto draft = versionDraft(std::move(*content), source, fields.value("name").toString(),
                                  fields.value("language").toString(), fields.value("script").toString(),
                                  fields.value("kind").toString());
        if (!draft)
            return value.invalidBatchVersion(index);
        drafts.push_back(std::move(*draft));
    }
    return value.saveVersions(std::move(drafts));
}
bool DownloadLyricsBinding::retrySave() {
    auto& value = *impl_;
    if (!value.canRetrySave())
        return false;
    value.writing_call = true;
    const bool accepted = value.writer.retryVariantsSave();
    value.writing_call = false;
    emit changed();
    return accepted;
}
bool DownloadLyricsBinding::discardPendingSave(const QString& expected_request_token,
                                               const QString& expected_discard_token) {
    auto& value = *impl_;
    if (expected_request_token != value.token || expected_discard_token.isEmpty() ||
        expected_discard_token != value.pendingDiscardToken())
        return false;
    const auto snapshot = value.writer.acquiredLyricsSave();
    return value.writer.discardInvalidAcquiredLyrics(snapshot.ticket, snapshot.target);
}
bool DownloadLyricsBinding::dismiss() {
    auto& value = *impl_;
    if (!value.canDismiss())
        return false;
    const auto job_id = QString::fromStdString(value.request->job_id);
    const QPointer<DownloadLyricsBinding> guard(this);
    value.request.reset();
    value.preview.reset();
    value.clearRecordings();
    value.token.clear();
    value.notice.clear();
    value.save_ticket.clear();
    value.save_target.clear();
    // A FIFO owner can offer the next request from changed(). All mutation of
    // the dismissed identity precedes that callback; never clear a new offer.
    value.lookup.clear();
    if (!guard)
        return true;
    emit reviewFinished(job_id);
    if (guard)
        emit changed();
    return true;
}
bool DownloadLyricsBinding::prepareExit() {
    impl_->preparing = true;
    impl_->preview.reset();
    impl_->clearRecordings();
    return impl_->lookup.prepareExit();
}
void DownloadLyricsBinding::resume() {
    if (impl_->stopping)
        return;
    impl_->preparing = false;
    impl_->lookup.resume();
    emit changed();
}
void DownloadLyricsBinding::beginShutdown() {
    if (impl_->stopping)
        return;
    impl_->stopping = true;
    impl_->preparing = true;
    impl_->preview.reset();
    impl_->clearRecordings();
    impl_->lookup.beginShutdown();
}
bool DownloadLyricsBinding::drained() const { return impl_->lookup.drained(); }
} // namespace music_player::m0
