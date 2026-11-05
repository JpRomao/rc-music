#include "LyricsBinding.hpp"
#include "adapters/QtLyricsService.hpp"
#include "m0/ports/LyricsVariantsStore.hpp"

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

namespace music_player::m0 {
namespace {
QString text(const std::string& value) { return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())); }
QString owned(const QString& value) { return QString(value.constData(), value.size()); }
std::filesystem::path nativePath(const QString& path) {
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toUtf8().toStdString());
#endif
}
std::optional<std::string> utf8(const QString& value, const bool trim) {
    const auto copy = trim ? value.trimmed() : value;
    const auto bytes = copy.toUtf8();
    if (QString::fromUtf8(bytes) != copy)
        return {};
    return bytes.toStdString();
}
std::optional<std::uint64_t> identifier(const QString& value) {
    bool valid{};
    const auto result = value.toULongLong(&valid);
    return valid && result != 0 && QString::number(result) == value ? std::optional<std::uint64_t>{result}
                                                                    : std::nullopt;
}
struct VariantsJob final {
    LyricsVariantsLoadResult loaded{};
    LyricsVariantsSaveStatus saved{LyricsVariantsSaveStatus::Failed};
};
} // namespace
struct LyricsBinding::VariantsState final {
    LyricsBinding& owner;
    std::shared_ptr<LyricsVariantsStore> store;
    LyricsVariants library;
    std::optional<LyricsVariants> pending;
    LyricsDocument selected_document;
    QFutureWatcher<VariantsJob> watcher;
    QString job_path, pending_path, notice;
    std::uint64_t selected{}, select_after_save{}, next_id{1}, next_after_save{1};
    bool busy{}, loading{}, ready{};
    VariantsState(LyricsBinding& binding, std::shared_ptr<LyricsVariantsStore> source)
        : owner(binding), store(std::move(source)) {
        QObject::connect(&watcher, &QFutureWatcher<VariantsJob>::finished, &owner, [this] { finished(); });
    }
    bool allowed() const { return !owner.shutting_down_ && !owner.exit_preparing_; }
    bool editable() const { return store && allowed() && ready && !busy && !pending; }
    auto find(const std::uint64_t id) const {
        return std::find_if(library.begin(), library.end(), [id](const auto& item) { return item.id == id; });
    }
    void clearSelection() {
        selected = 0;
        selected_document = {};
    }
    bool select(const std::uint64_t id) {
        if (id == 0) {
            clearSelection();
            return true;
        }
        const auto found = find(id);
        if (found == library.end())
            return false;
        auto parsed = parseLrc(found->content);
        if (parsed.error != LyricsParseError::None)
            return false;
        selected = id;
        selected_document = std::move(parsed.document);
        return true;
    }
    void clearView() {
        ready = false;
        library.clear();
        clearSelection();
        notice.clear();
        next_id = 1;
    }
    void resetAllocator() {
        next_id = 1;
        for (const auto& item : library)
            if (item.id >= next_id)
                next_id = item.id == std::numeric_limits<std::uint64_t>::max() ? 1 : item.id + 1;
        while (find(next_id) != library.end())
            next_id = next_id == std::numeric_limits<std::uint64_t>::max() ? 1 : next_id + 1;
    }
    std::uint64_t allocateId() const {
        auto candidate = next_id;
        while (find(candidate) != library.end())
            candidate = candidate == std::numeric_limits<std::uint64_t>::max() ? 1 : candidate + 1;
        return candidate;
    }
    bool invalid() {
        notice = LyricsBinding::tr("Confira o nome, o texto e os limites da versão da letra.");
        emit owner.changed();
        return false;
    }
    bool load() {
        if (!allowed() || !store || busy || pending || owner.path_.isEmpty())
            return false;
        const auto retained_store = store;
        const auto path = nativePath(owner.path_);
        job_path = owned(owner.path_);
        busy = true;
        loading = true;
        ready = false;
        notice = LyricsBinding::tr("Carregando versões da letra…");
        try {
            watcher.setFuture(QtConcurrent::run([retained_store, path] {
                VariantsJob result;
                try {
                    result.loaded = retained_store->load(path);
                } catch (...) {
                    result.loaded.status = LyricsVariantsLoadStatus::Unavailable;
                }
                return result;
            }));
        } catch (...) {
            busy = false;
            loading = false;
            notice = LyricsBinding::tr("Não foi possível carregar as versões da letra. Tente novamente.");
        }
        emit owner.changed();
        return busy;
    }
    bool save() {
        if (!allowed() || !store || busy || !pending)
            return false;
        const auto retained_store = store;
        const auto path = nativePath(pending_path);
        const auto candidate = *pending;
        job_path = owned(pending_path);
        busy = true;
        loading = false;
        notice = LyricsBinding::tr("Salvando versão da letra…");
        try {
            watcher.setFuture(QtConcurrent::run([retained_store, path, candidate] {
                VariantsJob result;
                try {
                    result.saved = retained_store->save(path, candidate);
                } catch (...) {
                }
                return result;
            }));
        } catch (...) {
            busy = false;
            notice = LyricsBinding::tr("Não foi possível confirmar o salvamento. Tente salvar novamente ou "
                                       "recarregue as versões do disco.");
        }
        emit owner.changed();
        return true; // The candidate remains admitted even if starting its job failed.
    }
    void finished() {
        const auto result = watcher.result();
        const bool same_track = job_path == owner.path_;
        busy = false;
        if (loading) {
            loading = false;
            if (same_track) {
                const auto status = result.loaded.status;
                const bool usable = status == LyricsVariantsLoadStatus::Missing ||
                                    status == LyricsVariantsLoadStatus::Loaded ||
                                    status == LyricsVariantsLoadStatus::RecoveredFromBackup;
                ready = usable && isValidLyricsVariants(result.loaded.variants);
                if (ready) {
                    library = result.loaded.variants;
                    clearSelection();
                    resetAllocator();
                    notice = status == LyricsVariantsLoadStatus::RecoveredFromBackup
                                 ? LyricsBinding::tr("Versões da letra recuperadas do backup.")
                                 : LyricsBinding::tr("Versões da letra prontas.");
                } else {
                    notice = status == LyricsVariantsLoadStatus::FutureVersion
                                 ? LyricsBinding::tr("Uma versão mais recente das letras foi encontrada. Os arquivos "
                                                     "foram preservados.")
                                 : LyricsBinding::tr("Não foi possível carregar as versões da letra. Os arquivos "
                                                     "foram preservados; tente recarregar.");
                }
            }
        } else if (result.saved == LyricsVariantsSaveStatus::Saved && pending) {
            if (same_track) {
                library = std::move(*pending);
                ready = true;
                if (!select(select_after_save))
                    clearSelection();
                next_id = next_after_save;
                notice = LyricsBinding::tr("Versão da letra salva.");
            }
            pending.reset();
            pending_path.clear();
        } else {
            notice = result.saved == LyricsVariantsSaveStatus::BlockedByFutureVersion
                         ? LyricsBinding::tr("Uma versão mais recente das letras foi encontrada. Os arquivos foram "
                                             "preservados; recarregue do disco.")
                         : LyricsBinding::tr("Não foi possível confirmar o salvamento. Tente salvar novamente ou "
                                             "recarregue as versões do disco.");
        }
        if (!same_track && allowed() && !pending && !owner.path_.isEmpty())
            static_cast<void>(load());
        emit owner.changed();
    }
};
LyricsBinding::LyricsBinding(QtLyricsService& service, QObject* parent) : LyricsBinding(service, {}, parent) {}
LyricsBinding::LyricsBinding(QtLyricsService& service, std::shared_ptr<LyricsVariantsStore> variants_store,
                             QObject* parent)
    : QObject(parent), variants_(std::make_unique<VariantsState>(*this, std::move(variants_store))), service_(service) {
    connect(&service_, &QtLyricsService::changed, this, &LyricsBinding::changed);
}
LyricsBinding::~LyricsBinding() = default;
QVariantList LyricsBinding::variants() const {
    QVariantList result;
    for (const auto& item : variants_->library)
        result.push_back(QVariantMap{{"id", QString::number(item.id)},
                                     {"name", text(item.name)},
                                     {"language", text(item.language)},
                                     {"script", text(item.script)},
                                     {"source", text(item.source)},
                                     {"kind", item.kind == LyricsVariantKind::Original ? "original" : "romanized"},
                                     {"generated", item.generated}});
    return result;
}
QString LyricsBinding::selectedVariantId() const {
    return variants_->selected ? QString::number(variants_->selected) : QString{};
}
bool LyricsBinding::variantsBusy() const { return variants_->busy; }
bool LyricsBinding::variantsReady() const { return variants_->ready; }
QString LyricsBinding::variantsStatusText() const {
    if (variants_->pending && variants_->pending_path != path_)
        return tr(
            "Há um salvamento pendente de outra faixa. Tente salvar novamente ou recarregue as versões do disco.");
    return variants_->notice;
}
bool LyricsBinding::variantsCanEdit() const { return variants_->editable(); }
bool LyricsBinding::variantsCanRetrySave() const {
    return variants_->allowed() && variants_->pending && !variants_->busy;
}
bool LyricsBinding::variantsHasPendingChange() const { return variants_->pending.has_value(); }
bool LyricsBinding::canSaveCurrentLyrics() const {
    return variants_->editable() && variants_->selected == 0 && service_.status() == QtLyricsService::Status::Ready &&
           (!service_.document().lines.empty() || !service_.document().plain_text.empty());
}
bool LyricsBinding::selectVariant(const QString& value) {
    if (!variants_->editable())
        return false;
    const auto id = value.isEmpty() ? std::optional<std::uint64_t>{0} : identifier(value);
    if (!id || !variants_->select(*id))
        return false;
    emit changed();
    return true;
}
bool LyricsBinding::addVariant(const QString& name, const QString& language, const QString& script,
                               const QString& source, const QString& kind, const QString& content,
                               const bool generated) {
    if (!variants_->editable())
        return false;
    if (name.size() > static_cast<qsizetype>(maximum_lyrics_variant_name_bytes) ||
        language.size() > static_cast<qsizetype>(maximum_lyrics_variant_tag_bytes) ||
        script.size() > static_cast<qsizetype>(maximum_lyrics_variant_tag_bytes) ||
        source.size() > static_cast<qsizetype>(maximum_lyrics_variant_source_bytes) ||
        content.size() > static_cast<qsizetype>(lyrics_max_input_bytes))
        return variants_->invalid();
    const auto label = utf8(name, true), lang = utf8(language, true), alphabet = utf8(script, true),
               origin = utf8(source, true), body = utf8(content, false);
    if (!label || !lang || !alphabet || !origin || !body || (kind != "original" && kind != "romanized"))
        return variants_->invalid();
    auto candidate = variants_->library;
    const auto id = variants_->allocateId();
    candidate.push_back({id, *label, *lang, *alphabet, *origin, *body,
                         kind == "original" ? LyricsVariantKind::Original : LyricsVariantKind::Romanized, generated});
    if (!isValidLyricsVariants(candidate))
        return variants_->invalid();
    variants_->pending = std::move(candidate);
    variants_->pending_path = owned(path_);
    variants_->select_after_save = id;
    variants_->next_after_save = id == std::numeric_limits<std::uint64_t>::max() ? 1 : id + 1;
    return variants_->save();
}
bool LyricsBinding::saveCurrentLyrics(const QString& name, const QString& language, const QString& script,
                                      const QString& kind) {
    if (!canSaveCurrentLyrics())
        return false;
    const auto content = lyricsDocumentAsLrc(service_.document());
    if (!content)
        return variants_->invalid();
    return addVariant(name, language, script,
                      service_.source() == QtLyricsService::Source::Online ? QStringLiteral("LRCLIB")
                                                                           : QStringLiteral("arquivo local"),
                      kind, text(*content), false);
}
bool LyricsBinding::removeVariant(const QString& value) {
    const auto id = identifier(value);
    if (!variants_->editable() || !id || variants_->find(*id) == variants_->library.end())
        return false;
    auto candidate = variants_->library;
    std::erase_if(candidate, [&](const auto& item) { return item.id == *id; });
    variants_->pending = std::move(candidate);
    variants_->pending_path = owned(path_);
    variants_->select_after_save = variants_->selected == *id ? 0 : variants_->selected;
    variants_->next_after_save = variants_->next_id;
    return variants_->save();
}
bool LyricsBinding::retryVariantsSave() { return variants_->save(); }
bool LyricsBinding::reloadVariants() {
    if (!variants_->allowed() || variants_->busy || !variants_->store || (path_.isEmpty() && !variants_->pending))
        return false;
    variants_->pending.reset();
    variants_->pending_path.clear();
    variants_->clearView();
    if (path_.isEmpty()) {
        variants_->notice = tr("Alteração em memória abandonada. Não há faixa selecionada; os arquivos existentes "
                               "foram preservados.");
        emit changed();
        return true;
    }
    return variants_->load();
}
const LyricsDocument& LyricsBinding::displayedDocument() const {
    return variants_->selected ? variants_->selected_document : service_.document();
}
void LyricsBinding::setTrack(const QString& path, const QString& title, const QString& artist, const QString& album,
                             const qint64 duration) {
    if (shutting_down_)
        return;
    const bool resuming = exit_preparing_;
    if (resuming) {
        exit_preparing_ = false;
        service_.setTrack({}, {}, {}, {}, 0);
    }
    const bool metadata_changed = title_ != title || artist_ != artist;
    const bool track_changed = path_ != path;
    if (track_changed) {
        position_ms_ = 0;
        variants_->clearView();
    }
    path_ = owned(path);
    title_ = owned(title);
    artist_ = owned(artist);
    service_.setTrack(path_, title_, artist_, album, duration);
    if ((track_changed || resuming) && !variants_->ready)
        static_cast<void>(variants_->load());
    if (metadata_changed || track_changed || resuming)
        emit changed();
}
void LyricsBinding::setPosition(const qint64 position) {
    const auto before = lyricsLineAt(displayedDocument(), position_ms_);
    position_ms_ = position;
    if (before != lyricsLineAt(displayedDocument(), position_ms_))
        emit changed();
}
void LyricsBinding::beginShutdown() {
    shutting_down_ = true;
    service_.beginShutdown();
}
bool LyricsBinding::stopped() const { return service_.stopped() && !variants_->busy && !variants_->pending; }
bool LyricsBinding::prepareExit() {
    if (!exit_preparing_) {
        exit_preparing_ = true;
        service_.cancel();
        // Force the presenter through its observable preparation phase even
        // when already drained; a synchronous save refusal can then rearm us.
        return false;
    }
    return service_.drained() && !variants_->busy && !variants_->pending;
}
QString LyricsBinding::title() const { return title_; }
QString LyricsBinding::artist() const { return artist_; }
QString LyricsBinding::statusText() const {
    if (variants_->pending && path_.isEmpty())
        return variantsStatusText();
    if (variants_->selected) {
        const auto found = variants_->find(variants_->selected);
        if (found != variants_->library.end())
            return tr("%1 · versão salva%2").arg(text(found->name), found->generated ? tr(" · gerada") : QString{});
    }
    if (!service_.message().isEmpty())
        return service_.message();
    switch (service_.status()) {
    case QtLyricsService::Status::Idle:
        return {};
    case QtLyricsService::Status::LoadingLocal:
        return tr("Carregando letra…");
    case QtLyricsService::Status::Searching:
        return tr("Buscando no LRCLIB…");
    case QtLyricsService::Status::Ready:
        if (service_.source() == QtLyricsService::Source::Online)
            return hasSyncedLyrics() ? tr("Letra sincronizada · LRCLIB") : tr("Letra sem sincronização · LRCLIB");
        return hasSyncedLyrics() ? tr("Letra sincronizada · arquivo local")
                                 : tr("Letra sem sincronização · arquivo local");
    case QtLyricsService::Status::NotFound:
        return tr("Letra não encontrada. Você pode buscar online.");
    case QtLyricsService::Status::Instrumental:
        return tr("Faixa instrumental.");
    case QtLyricsService::Status::Error:
        return tr("Não foi possível carregar a letra.");
    }
    return {};
}
QString LyricsBinding::currentLine() const {
    const auto& doc = displayedDocument();
    const auto at = lyricsLineAt(doc, position_ms_);
    return at ? text(doc.lines[*at].text) : QString{};
}
QString LyricsBinding::previousLine() const {
    const auto& doc = displayedDocument();
    const auto at = lyricsLineAt(doc, position_ms_);
    return at && *at > 0 ? text(doc.lines[*at - 1].text) : QString{};
}
QString LyricsBinding::nextLine() const {
    const auto& doc = displayedDocument();
    const auto at = lyricsLineAt(doc, position_ms_);
    const auto next = at ? *at + 1 : 0;
    return next < doc.lines.size() ? text(doc.lines[next].text) : QString{};
}
QString LyricsBinding::plainText() const { return text(displayedDocument().plain_text); }
bool LyricsBinding::hasSyncedLyrics() const { return !displayedDocument().lines.empty(); }
bool LyricsBinding::busy() const { return service_.busy(); }
bool LyricsBinding::canSearch() const { return !shutting_down_ && !exit_preparing_ && service_.canSearch(); }
bool LyricsBinding::search(const QString& title, const QString& artist) {
    if (shutting_down_ || exit_preparing_ || !service_.searchOnline(title, artist))
        return false;
    variants_->clearSelection();
    emit changed();
    return true;
}
} // namespace music_player::m0
