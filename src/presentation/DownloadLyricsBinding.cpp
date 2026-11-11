#include "DownloadLyricsBinding.hpp"
#include "LyricsBinding.hpp"

#include <QPointer>
#include <QUuid>
#include <QVariantMap>

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
PublishedLyricsRequest owned(const PublishedLyricsRequest& request) {
    return {request.job_id,        owned(request.final_mp3),   owned(request.title),
            owned(request.artist), owned(request.lyrics_page), owned(request.album),
            request.duration_ms};
}
QString providerNotice() {
    return DownloadLyricsBinding::tr("Letras indisponível nesta versão; letras consultadas no LRCLIB.");
}
QString newToken() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool pending(const AcquiredLyricsSaveState state) {
    return state == AcquiredLyricsSaveState::Loading || state == AcquiredLyricsSaveState::Saving ||
           state == AcquiredLyricsSaveState::Conflict || state == AcquiredLyricsSaveState::LoadFailed ||
           state == AcquiredLyricsSaveState::SaveFailed || state == AcquiredLyricsSaveState::BlockedByFutureVersion ||
           state == AcquiredLyricsSaveState::Invalid;
}
} // namespace

struct DownloadLyricsBinding::Impl final {
    DownloadLyricsBinding& owner;
    DownloadLyricsLookup& lookup;
    LyricsBinding& writer;
    std::optional<PublishedLyricsRequest> request;
    std::optional<DownloadLyricsLookup::Result> preview;
    QString token, notice, save_target;
    std::string save_ticket;
    bool writing_call{}, preparing{}, stopping{};

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
        if (writing_call || (request && lookup.state() == DownloadLyricsLookup::State::Searching))
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
    bool canSave() const { return canSavePasted() && currentPreview(); }
    bool canRetry() const {
        return allowed() && request && !ownPending() && lookup.state() != DownloadLyricsLookup::State::Searching;
    }
    bool canRetrySave() const {
        const auto snapshot = writer.acquiredLyricsSave();
        return allowed() && !writing_call && own(snapshot) && pending(snapshot.state) && writer.variantsCanRetrySave();
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
    bool saveVersion(std::string content, std::string source, const QString& name, const QString& language,
                     const QString& script, const QString& kind) {
        if (!bounded(name, maximum_lyrics_variant_name_bytes) || !bounded(language, maximum_lyrics_variant_tag_bytes) ||
            !bounded(script, maximum_lyrics_variant_tag_bytes) || (kind != "original" && kind != "romanized"))
            return invalidSave();
        LyricsVariant draft{1,
                            name.trimmed().toUtf8().toStdString(),
                            language.trimmed().toUtf8().toStdString(),
                            script.trimmed().toUtf8().toStdString(),
                            std::move(source),
                            std::move(content),
                            kind == "original" ? LyricsVariantKind::Original : LyricsVariantKind::Romanized,
                            false};
        if (!isValidLyricsVariants({draft}))
            return invalidSave();
        const auto expected_token = token;
        const auto ticket = newToken().toStdString();
        const auto target = owned(request->final_mp3);
        save_ticket = ticket;
        save_target = target;
        notice.clear();
        // Reserve before the shared writer emits changes from inside this call.
        writing_call = true;
        const bool accepted = writer.appendAcquiredLyrics(ticket, target, {std::move(draft)}, true);
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
                action = DownloadLyricsBinding::tr("Guardando a versão da letra…");
                break;
            case AcquiredLyricsSaveState::Saved:
                action = DownloadLyricsBinding::tr("Versão da letra guardada.");
                break;
            case AcquiredLyricsSaveState::Unchanged:
                action = DownloadLyricsBinding::tr("Esta versão já estava guardada.");
                break;
            case AcquiredLyricsSaveState::LoadFailed:
            case AcquiredLyricsSaveState::SaveFailed:
            case AcquiredLyricsSaveState::BlockedByFutureVersion:
            case AcquiredLyricsSaveState::Conflict:
            case AcquiredLyricsSaveState::Invalid:
                action = DownloadLyricsBinding::tr(
                    "O salvamento está pendente. Tente guardar novamente; o áudio foi preservado.");
                break;
            case AcquiredLyricsSaveState::Idle:
            case AcquiredLyricsSaveState::Abandoned:
                break;
            }
        }
        if (!notice.isEmpty())
            action = notice;
        if (!action.isEmpty())
            return providerNotice() + QStringLiteral(" ") + action;
        if (!request)
            return {};
        const auto lookup_message = lookup.message();
        return lookup_message.isEmpty() ? providerNotice() : lookup_message;
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
bool DownloadLyricsBinding::canDismiss() const { return impl_->canDismiss(); }
QString DownloadLyricsBinding::title() const { return impl_->request ? impl_->request->title : QString{}; }
QString DownloadLyricsBinding::artist() const { return impl_->request ? impl_->request->artist : QString{}; }
QString DownloadLyricsBinding::requestToken() const { return impl_->token; }
QString DownloadLyricsBinding::statusText() const { return impl_->statusText(); }
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
                                       const QString& kind) {
    auto& value = *impl_;
    if (!value.canSavePasted() || expected_request_token != value.token)
        return false;
    if (content.size() > static_cast<qsizetype>(lyrics_max_input_bytes))
        return value.invalidSave();
    const auto bytes = content.toUtf8();
    if (bytes.size() > static_cast<qsizetype>(lyrics_max_input_bytes) || QString::fromUtf8(bytes) != content)
        return value.invalidSave();
    auto pasted = bytes.toStdString();
    const auto parsed = parseLrc(pasted);
    if (parsed.error != LyricsParseError::None ||
        parsed.document.plain_text.find_first_not_of(" \t\r\n") == std::string::npos ||
        !lyricsDocumentAsLrc(parsed.document))
        return value.invalidSave();
    // Keep the user's source text. The shared writer compares canonical lyrics for duplicates.
    return value.saveVersion(std::move(pasted), "manual", name, language, script, kind);
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
bool DownloadLyricsBinding::dismiss() {
    auto& value = *impl_;
    if (!value.canDismiss())
        return false;
    const auto job_id = QString::fromStdString(value.request->job_id);
    const QPointer<DownloadLyricsBinding> guard(this);
    value.request.reset();
    value.preview.reset();
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
    impl_->lookup.beginShutdown();
}
bool DownloadLyricsBinding::drained() const { return impl_->lookup.drained(); }
} // namespace music_player::m0
