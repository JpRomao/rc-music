#include "DownloadLyricsBinding.hpp"
#include "LyricsBinding.hpp"

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
bool bounded(const PublishedLyricsRequest& request) {
    return request.job_id.size() <= 64 && bounded(request.final_mp3, 8192) && bounded(request.title, 512) &&
           bounded(request.artist, 512) && bounded(request.album, 512) && bounded(request.lyrics_page, 2048);
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
    bool canSave() const {
        return allowed() && currentPreview() && !ownPending() && !writer.variantsBusy() &&
               !writer.variantsHasPendingChange();
    }
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
        notice = DownloadLyricsBinding::tr("Confira o nome, a forma e os limites da versão antes de guardar.");
        emit owner.changed();
        return false;
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
    if (!value.allowed() || value.request || !bounded(request))
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
    if (index < 0 || static_cast<std::size_t>(index) >= available->candidates.size() ||
        !bounded(name, maximum_lyrics_variant_name_bytes) || !bounded(language, maximum_lyrics_variant_tag_bytes) ||
        !bounded(script, maximum_lyrics_variant_tag_bytes) || (kind != "original" && kind != "romanized"))
        return value.invalidSave();
    const auto snapshot = *available;
    const auto expected_token = value.token;
    const auto content = lyricsDocumentAsLrc(snapshot.candidates[static_cast<std::size_t>(index)]);
    if (!content)
        return value.invalidSave();
    LyricsVariant draft{1,
                        name.trimmed().toUtf8().toStdString(),
                        language.trimmed().toUtf8().toStdString(),
                        script.trimmed().toUtf8().toStdString(),
                        snapshot.source.toUtf8().toStdString(),
                        *content,
                        kind == "original" ? LyricsVariantKind::Original : LyricsVariantKind::Romanized,
                        false};
    if (!isValidLyricsVariants({draft}))
        return value.invalidSave();
    const auto ticket = newToken().toStdString();
    const auto target = owned(snapshot.final_mp3);
    value.save_ticket = ticket;
    value.save_target = target;
    value.notice.clear();
    // Reserve before invoking the writer: its Loading notification can invoke
    // QML/application callbacks before appendAcquiredLyrics returns.
    value.writing_call = true;
    const bool accepted = value.writer.appendAcquiredLyrics(ticket, target, {std::move(draft)}, true);
    value.writing_call = false;
    if (!accepted && !value.ownPending()) {
        value.save_ticket.clear();
        value.save_target.clear();
    }
    if (value.token == expected_token)
        emit changed();
    return accepted;
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
    value.request.reset();
    value.preview.reset();
    value.token.clear();
    value.notice.clear();
    value.save_ticket.clear();
    value.save_target.clear();
    // A FIFO owner can offer the next request from changed(). All mutation of
    // the dismissed identity precedes that callback; never clear a new offer.
    value.lookup.clear();
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
