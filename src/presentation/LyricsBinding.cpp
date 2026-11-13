#include "LyricsBinding.hpp"
#include "adapters/QtLyricsService.hpp"
#include "boundary/FilesystemPath.hpp"
#include "m0/domain/AcquiredLyrics.hpp"
#include "m0/domain/LocalAudioFilePolicy.hpp"
#include "m0/ports/LyricsVariantsStore.hpp"

#include <QDir>
#include <QFutureWatcher>
#include <QPointer>
#include <QRegularExpression>
#include <QUuid>
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
    AcquiredLyricsMergeResult merged{};
    bool invalid_drafts{};
};
bool acquiredTarget(const QString& value) {
    if (value.isEmpty() || value.size() > 32768 || value.contains(QChar{0}) || value.startsWith(QStringLiteral("//")) ||
        value.startsWith(QStringLiteral("\\\\")) || !value.endsWith(QStringLiteral(".mp3"), Qt::CaseInsensitive) ||
        QString::fromUtf8(value.toUtf8()) != value || !QDir::isAbsolutePath(value))
        return false;
    const auto path = nativePath(value);
    return path.is_absolute() && path == path.lexically_normal();
}
bool acquiredTicket(const std::string& value) {
    return !value.empty() && value.size() <= 96 && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}
bool boundedDrafts(const LyricsVariants& values) {
    if (values.empty() || values.size() > maximum_lyrics_variants)
        return false;
    std::size_t bytes{};
    for (const auto& item : values) {
        if (item.name.size() > maximum_lyrics_variant_name_bytes ||
            item.language.size() > maximum_lyrics_variant_tag_bytes ||
            item.script.size() > maximum_lyrics_variant_tag_bytes ||
            item.source.size() > maximum_lyrics_variant_source_bytes || item.content.size() > lyrics_max_input_bytes ||
            item.content.size() > maximum_lyrics_variants_content_bytes - bytes)
            return false;
        bytes += item.content.size();
    }
    return true;
}
bool queuedTarget(const QString& value) {
    if (value.size() > 32768 || !isAbsoluteFilesystemSourcePath(value) || !QDir::isAbsolutePath(value) ||
        QString::fromUtf8(value.toUtf8()) != value)
        return false;
    const auto path = nativePath(value);
    return path.is_absolute() && LocalAudioFilePolicy::isCandidate(path);
}
std::optional<LyricsVariant> makeVariant(const std::uint64_t id, const QString& name, const QString& language,
                                         const QString& script, const QString& source, const QString& kind,
                                         const QString& content, const bool generated) {
    if (name.size() > static_cast<qsizetype>(maximum_lyrics_variant_name_bytes) ||
        language.size() > static_cast<qsizetype>(maximum_lyrics_variant_tag_bytes) ||
        script.size() > static_cast<qsizetype>(maximum_lyrics_variant_tag_bytes) ||
        source.size() > static_cast<qsizetype>(maximum_lyrics_variant_source_bytes) ||
        content.size() > static_cast<qsizetype>(lyrics_max_input_bytes) ||
        kind.size() > static_cast<qsizetype>(maximum_lyrics_variant_tag_bytes))
        return {};
    const auto variant_kind = lyricsVariantKindFromName(kind.toLatin1().toStdString());
    const auto label = utf8(name, true), lang = utf8(language, true), alphabet = utf8(script, true),
               origin = utf8(source, true), body = utf8(content, false);
    if (!label || !lang || !alphabet || !origin || !body || !variant_kind)
        return {};
    return LyricsVariant{id, *label, *lang, *alphabet, *origin, *body, *variant_kind, generated};
}
bool containsJapaneseScript(const LyricsDocument& document, const bool known_japanese) {
    static const QRegularExpression kana(QStringLiteral("[\\p{Hiragana}\\p{Katakana}]"));
    static const QRegularExpression kana_or_han(QStringLiteral("[\\p{Hiragana}\\p{Katakana}\\p{Han}]"));
    return (known_japanese ? kana_or_han : kana).match(text(document.plain_text)).hasMatch();
}
std::vector<std::string> visualLines(std::string_view value) {
    std::vector<std::string> lines;
    while (true) {
        const auto separator = value.find('\n');
        lines.emplace_back(value.substr(0, separator));
        if (separator == std::string_view::npos)
            return lines;
        value.remove_prefix(separator + 1);
    }
}
struct RomajiDraft final {
    std::string content;
    std::vector<LyricsLine> cues;
};
std::optional<RomajiDraft> makeRomajiDraft(const LyricsBinding::JapaneseRomanizer& romanizer,
                                           const LyricsDocument& document) {
    std::vector<std::vector<std::string>> groups;
    if (document.lines.empty()) {
        groups.push_back(visualLines(document.plain_text));
    } else {
        groups.reserve(document.lines.size());
        for (const auto& cue : document.lines)
            groups.push_back(visualLines(cue.text));
    }
    std::vector<std::string> input;
    for (const auto& group : groups) {
        for (const auto& line : group) {
            if (!line.empty())
                input.push_back(line);
        }
    }
    if (input.empty())
        return {};
    auto output = romanizer(input);
    if (!output || output->size() != input.size())
        return {};
    std::size_t total{};
    for (const auto& line : *output) {
        if (line.empty() || line.find_first_of("\r\n") != std::string::npos ||
            line.size() > lyrics_max_input_bytes - total)
            return {};
        total += line.size();
    }

    LyricsDocument converted;
    auto next_line = output->cbegin();
    for (std::size_t index = 0; index < groups.size(); ++index) {
        std::string group;
        const auto& original = groups[index];
        for (std::size_t line = 0; line < original.size(); ++line) {
            if (line)
                group += '\n';
            if (!original[line].empty())
                group += *next_line++;
        }
        if (document.lines.empty())
            converted.plain_text = std::move(group);
        else
            converted.lines.push_back({document.lines[index].timestamp_ms, std::move(group)});
    }
    const auto content = lyricsDocumentAsLrc(converted);
    if (!content)
        return {};
    return RomajiDraft{*content, std::move(converted.lines)};
}
} // namespace
struct LyricsBinding::VariantsState final {
    struct QueueTarget final {
        QString occurrence, path, token;
    };
    LyricsBinding& owner;
    std::shared_ptr<LyricsVariantsStore> store;
    LyricsVariants library;
    std::optional<LyricsVariants> pending;
    LyricsDocument selected_document;
    QFutureWatcher<VariantsJob> watcher;
    QString job_path, pending_path, notice;
    std::uint64_t selected{}, select_after_save{}, next_id{1}, next_after_save{1};
    bool busy{}, loading{}, ready{};
    AcquiredLyricsSaveSnapshot acquired;
    std::shared_ptr<const LyricsVariants> acquired_drafts;
    std::function<QString(const QString&)> queue_source;
    std::optional<QueueTarget> queue_target;
    std::string queued_write_ticket;
    bool acquired_pending{}, acquired_loading{}, keep_distinct{};
    VariantsState(LyricsBinding& binding, std::shared_ptr<LyricsVariantsStore> source)
        : owner(binding), store(std::move(source)) {
        QObject::connect(&watcher, &QFutureWatcher<VariantsJob>::finished, &owner, [this] { finished(); });
    }
    bool allowed() const { return !owner.shutting_down_ && !owner.exit_preparing_; }
    bool hasPending() const { return pending.has_value() || acquired_pending; }
    bool canAppend() const { return store && allowed() && !busy && !hasPending(); }
    bool editable() const { return ready && canAppend(); }
    bool append(std::string ticket, QString target, LyricsVariants drafts, const bool keep_distinct_versions) {
        auto retained = std::make_shared<const LyricsVariants>(std::move(drafts));
        target = owned(target);
        acquired = {std::move(ticket), target, AcquiredLyricsSaveState::Loading};
        acquired_drafts = std::move(retained);
        acquired_pending = true;
        keep_distinct = keep_distinct_versions;
        pending_path = std::move(target);
        return loadAcquired();
    }
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
        if (!allowed() || !store || busy || hasPending() || owner.path_.isEmpty())
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
    bool loadAcquired() {
        if (!allowed() || !store || busy || !acquired_pending || pending || !acquired_drafts)
            return false;
        const auto retained_store = store;
        const auto path = nativePath(acquired.target);
        const auto drafts = acquired_drafts;
        const auto keep = keep_distinct;
        job_path = owned(acquired.target);
        busy = true;
        acquired_loading = true;
        loading = false;
        acquired.state = AcquiredLyricsSaveState::Loading;
        notice = LyricsBinding::tr("Preparando versões da letra para guardar…");
        try {
            watcher.setFuture(QtConcurrent::run([retained_store, path, drafts, keep] {
                VariantsJob result;
                auto checked = mergeAcquiredLyrics({}, *drafts, true);
                if (checked.status == AcquiredLyricsMergeStatus::Invalid ||
                    checked.status == AcquiredLyricsMergeStatus::LimitExceeded) {
                    result.invalid_drafts = true;
                    result.merged = std::move(checked);
                    return result;
                }
                try {
                    result.loaded = retained_store->load(path);
                } catch (...) {
                    result.loaded.status = LyricsVariantsLoadStatus::Unavailable;
                }
                const auto status = result.loaded.status;
                if (status == LyricsVariantsLoadStatus::Missing || status == LyricsVariantsLoadStatus::Loaded ||
                    status == LyricsVariantsLoadStatus::RecoveredFromBackup)
                    result.merged = mergeAcquiredLyrics(result.loaded.variants, *drafts, keep);
                result.loaded.variants.clear();
                return result;
            }));
        } catch (...) {
            busy = false;
            acquired_loading = false;
            acquired.state = AcquiredLyricsSaveState::LoadFailed;
            notice =
                LyricsBinding::tr("Não foi possível preparar as versões. Tente novamente ou abandone a alteração.");
        }
        emit owner.changed();
        return true;
    }
    bool save(const bool continuation = false) {
        if ((!allowed() && !(continuation && acquired_pending && !owner.shutting_down_)) || !store || busy || !pending)
            return false;
        const auto retained_store = store;
        const auto path = nativePath(pending_path);
        const auto candidate = *pending;
        job_path = owned(pending_path);
        busy = true;
        loading = false;
        if (acquired_pending)
            acquired.state = AcquiredLyricsSaveState::Saving;
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
            if (acquired_pending)
                acquired.state = AcquiredLyricsSaveState::SaveFailed;
            notice = LyricsBinding::tr("Não foi possível confirmar o salvamento. Tente salvar novamente ou "
                                       "recarregue as versões do disco.");
        }
        emit owner.changed();
        return true; // The candidate remains admitted even if starting its job failed.
    }
    void finished() {
        auto result = watcher.result();
        const bool same_track = job_path == owner.path_;
        busy = false;
        if (acquired_loading) {
            acquired_loading = false;
            const auto status = result.loaded.status;
            const bool usable = status == LyricsVariantsLoadStatus::Missing ||
                                status == LyricsVariantsLoadStatus::Loaded ||
                                status == LyricsVariantsLoadStatus::RecoveredFromBackup;
            if (result.invalid_drafts) {
                acquired.state = AcquiredLyricsSaveState::Invalid;
                notice = LyricsBinding::tr("Confira o texto e os limites das versões recebidas.");
            } else if (!usable) {
                acquired.state = status == LyricsVariantsLoadStatus::FutureVersion
                                     ? AcquiredLyricsSaveState::BlockedByFutureVersion
                                     : AcquiredLyricsSaveState::LoadFailed;
                notice = LyricsBinding::tr(
                    "Não foi possível carregar as versões existentes. Os arquivos foram preservados.");
            } else if (result.merged.status == AcquiredLyricsMergeStatus::Conflict) {
                acquired.state = AcquiredLyricsSaveState::Conflict;
                notice = LyricsBinding::tr("Há outra versão desta forma. Confira o conteúdo antes de guardar ambas.");
            } else if (result.merged.status == AcquiredLyricsMergeStatus::Unchanged) {
                if (same_track) {
                    library = std::move(result.merged.variants);
                    ready = true;
                    if (!select(selected))
                        clearSelection();
                    resetAllocator();
                }
                acquired.state = AcquiredLyricsSaveState::Unchanged;
                acquired_pending = false;
                acquired_drafts.reset();
                pending_path.clear();
                notice = LyricsBinding::tr("Estas versões já estavam guardadas.");
                if (!same_track && !ready && allowed() && !owner.path_.isEmpty())
                    static_cast<void>(load());
            } else if (result.merged.status == AcquiredLyricsMergeStatus::Added) {
                pending = std::move(result.merged.variants);
                pending_path = owned(acquired.target);
                acquired_drafts.reset();
                if (!save(true))
                    acquired.state = AcquiredLyricsSaveState::SaveFailed;
                return;
            } else {
                acquired.state = AcquiredLyricsSaveState::Invalid;
                notice = LyricsBinding::tr(
                    "As versões excedem os limites ou contêm dados inválidos. Os arquivos foram preservados.");
            }
            emit owner.changed();
            return;
        }
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
                const auto keep_selection = acquired_pending ? selected : select_after_save;
                library = std::move(*pending);
                ready = true;
                if (!select(keep_selection))
                    clearSelection();
                if (acquired_pending)
                    resetAllocator();
                else
                    next_id = next_after_save;
            }
            notice = LyricsBinding::tr("Versão da letra salva.");
            pending.reset();
            pending_path.clear();
            if (acquired_pending) {
                acquired.state = AcquiredLyricsSaveState::Saved;
                acquired_pending = false;
                acquired_drafts.reset();
            }
        } else {
            if (acquired_pending)
                acquired.state = result.saved == LyricsVariantsSaveStatus::BlockedByFutureVersion
                                     ? AcquiredLyricsSaveState::BlockedByFutureVersion
                                     : AcquiredLyricsSaveState::SaveFailed;
            notice = result.saved == LyricsVariantsSaveStatus::BlockedByFutureVersion
                         ? LyricsBinding::tr("Uma versão mais recente das letras foi encontrada. Os arquivos foram "
                                             "preservados; recarregue do disco.")
                         : LyricsBinding::tr("Não foi possível confirmar o salvamento. Tente salvar novamente ou "
                                             "recarregue as versões do disco.");
        }
        if (!same_track && !ready && allowed() && !hasPending() && !owner.path_.isEmpty())
            static_cast<void>(load());
        emit owner.changed();
    }
};
struct LyricsBinding::RomajiState final {
    LyricsBinding& owner;
    JapaneseRomanizer romanizer;
    QFutureWatcher<std::optional<RomajiDraft>> watcher;
    QString token, revision, notice;
    QString checked_revision;
    std::optional<RomajiDraft> draft;
    bool busy{};
    bool japanese{};

    explicit RomajiState(LyricsBinding& binding) : owner(binding) {
        QObject::connect(&watcher, &QFutureWatcher<std::optional<RomajiDraft>>::finished, &owner, [this] {
            busy = false;
            if (!current(token)) {
                reset();
                emit owner.changed();
                return;
            }
            draft = watcher.result();
            notice = draft ? QString{} : LyricsBinding::tr("Não foi possível gerar romaji com o pykakasi.");
            const auto completed_token = token;
            const QPointer<LyricsBinding> guard{&owner};
            emit owner.changed();
            if (guard && draft && current(completed_token))
                emit owner.romajiDraftReady(completed_token, text(draft->content));
        });
    }
    bool current(const QString& expected_token) const {
        return !expected_token.isEmpty() && expected_token == token && revision == owner.romajiSourceRevision();
    }
    void reset() {
        token.clear();
        revision.clear();
        notice.clear();
        draft.reset();
    }
};
LyricsBinding::LyricsBinding(QtLyricsService& service, QObject* parent) : LyricsBinding(service, {}, parent) {}
LyricsBinding::LyricsBinding(QtLyricsService& service, std::shared_ptr<LyricsVariantsStore> variants_store,
                             QObject* parent)
    : QObject(parent), variants_(std::make_unique<VariantsState>(*this, std::move(variants_store))),
      romaji_(std::make_unique<RomajiState>(*this)), service_(service) {
    updateOnlineLyricsCandidates();
    connect(&service_, &QtLyricsService::changed, this, [this] {
        updateOnlineLyricsCandidates();
        emit changed();
    });
}
LyricsBinding::~LyricsBinding() = default;
void LyricsBinding::setQueueLyricsSource(std::function<QString(const QString&)> source) {
    variants_->queue_target.reset();
    variants_->queue_source = std::move(source);
    emit changed();
}
bool LyricsBinding::queueVariantAvailable() const { return variants_->canAppend() && bool(variants_->queue_source); }
QString LyricsBinding::queuedVariantStatusText() const {
    if (variants_->queued_write_ticket.empty() || variants_->queued_write_ticket != variants_->acquired.ticket)
        return {};
    switch (variants_->acquired.state) {
    case AcquiredLyricsSaveState::Loading:
        return tr("Preparando versões da letra para guardar…");
    case AcquiredLyricsSaveState::Saving:
        return tr("Salvando versão da letra…");
    case AcquiredLyricsSaveState::Saved:
        return tr("Versão da letra salva.");
    case AcquiredLyricsSaveState::Unchanged:
        return tr("Estas versões já estavam guardadas.");
    default:
        return variants_->hasPending() ? variantsStatusText() : QString{};
    }
}
QString LyricsBinding::prepareQueuedVariant(const QString& occurrence_id) {
    if (!queueVariantAvailable() || occurrence_id.isEmpty())
        return {};
    const auto path = variants_->queue_source(occurrence_id);
    if (!queuedTarget(path))
        return {};
    auto token = QStringLiteral("queue-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    variants_->queue_target = VariantsState::QueueTarget{owned(occurrence_id), owned(path), token};
    return token;
}
bool LyricsBinding::addQueuedVariant(const QString& token, const QString& name, const QString& language,
                                     const QString& script, const QString& source, const QString& kind,
                                     const QString& content, const bool generated) {
    const auto& target = variants_->queue_target;
    if (!queueVariantAvailable() || !target || target->token != token)
        return false;
    if (variants_->queue_source(target->occurrence) != target->path) {
        variants_->queue_target.reset();
        return false;
    }
    const auto draft = makeVariant(1, name, language, script, source, kind, content, generated);
    if (!draft || !isValidLyricsVariants({*draft}))
        return variants_->invalid();
    // Consume before the shared writer emits its first change. Retrying an
    // admitted write uses the original path and candidate, even after removal.
    auto captured = std::move(*variants_->queue_target);
    variants_->queue_target.reset();
    variants_->queued_write_ticket = captured.token.toStdString();
    return variants_->append(variants_->queued_write_ticket, std::move(captured.path), {*draft}, true);
}
void LyricsBinding::cancelQueuedVariant(const QString& token) {
    if (variants_->queue_target && variants_->queue_target->token == token)
        variants_->queue_target.reset();
}
bool LyricsBinding::appendAcquiredLyrics(std::string ticket, QString target, LyricsVariants drafts,
                                         bool keep_distinct_versions) {
    if (!variants_->canAppend() || !acquiredTicket(ticket) || !acquiredTarget(target) || !boundedDrafts(drafts))
        return false;
    return variants_->append(std::move(ticket), std::move(target), std::move(drafts), keep_distinct_versions);
}
AcquiredLyricsSaveSnapshot LyricsBinding::acquiredLyricsSave() const { return variants_->acquired; }
bool LyricsBinding::discardInvalidAcquiredLyrics(const std::string& ticket, const QString& target) {
    auto& value = *variants_;
    if (!value.allowed() || value.busy || !value.acquired_pending || value.pending ||
        value.acquired.state != AcquiredLyricsSaveState::Invalid || value.acquired.ticket != ticket ||
        value.acquired.target != target)
        return false;
    value.acquired.state = AcquiredLyricsSaveState::Abandoned;
    value.acquired_pending = false;
    value.acquired_drafts.reset();
    value.pending_path.clear();
    value.notice = tr("Tentativa de guardar a letra descartada.");
    if (!value.ready && !path_.isEmpty())
        static_cast<void>(value.load());
    else
        emit changed();
    return true;
}
QVariantList LyricsBinding::variants() const {
    QVariantList result;
    for (const auto& item : variants_->library)
        result.push_back(QVariantMap{{"id", QString::number(item.id)},
                                     {"name", text(item.name)},
                                     {"language", text(item.language)},
                                     {"script", text(item.script)},
                                     {"source", text(item.source)},
                                     {"kind", QString::fromUtf8(lyricsVariantKindName(item.kind))},
                                     {"generated", item.generated}});
    return result;
}
QString LyricsBinding::selectedVariantId() const {
    return variants_->selected ? QString::number(variants_->selected) : QString{};
}
bool LyricsBinding::variantsBusy() const { return variants_->busy; }
bool LyricsBinding::variantsReady() const { return variants_->ready; }
QString LyricsBinding::variantsStatusText() const {
    if (!variants_->busy && variants_->hasPending() && variants_->pending_path != path_)
        return tr(
            "Há um salvamento pendente de outra faixa. Tente salvar novamente ou recarregue as versões do disco.");
    return variants_->notice;
}
bool LyricsBinding::variantsCanEdit() const { return variants_->editable(); }
bool LyricsBinding::variantsCanRetrySave() const {
    return variants_->allowed() && variants_->hasPending() && !variants_->busy;
}
bool LyricsBinding::variantsHasPendingChange() const { return variants_->hasPending(); }
bool LyricsBinding::canSaveCurrentLyrics() const {
    if (!variants_->editable() || variants_->selected || lyricsCopyProtected() ||
        service_.status() != QtLyricsService::Status::Ready)
        return false;
    const auto& document = displayedDocument();
    return !document.lines.empty() || !document.plain_text.empty();
}
void LyricsBinding::updateOnlineLyricsCandidates() {
    QVariantList candidates;
    const auto& documents = service_.onlineDocuments();
    for (int index = 0; index < static_cast<int>(documents.size()); ++index) {
        const auto& document = documents[static_cast<std::size_t>(index)];
        candidates.push_back(QVariantMap{{"index", index},
                                         {"label", document.lines.empty() ? tr("Texto simples") : tr("Sincronizada")}});
    }
    if (candidates != online_candidates_) {
        online_candidates_ = std::move(candidates);
        emit onlineLyricsCandidatesChanged();
    }
}
QVariantList LyricsBinding::onlineLyricsCandidates() const { return online_candidates_; }
int LyricsBinding::selectedOnlineLyricsCandidate() const {
    if (variants_->selected || service_.onlineDocuments().empty())
        return -1;
    return online_candidate_context_ == service_.contextRevision() ? online_candidate_ : 0;
}
QString LyricsBinding::currentLyricsRevision() const {
    if (!canSaveCurrentLyrics())
        return {};
    return QString::number(service_.contextRevision()) + QLatin1Char(':') + QString::number(selection_revision_);
}
void LyricsBinding::setJapaneseRomanizer(JapaneseRomanizer romanizer) {
    romaji_->reset();
    romaji_->romanizer = std::move(romanizer);
    emit changed();
}
bool LyricsBinding::lyricsCopyProtected() const {
    return !variants_->selected && service_.source() == QtLyricsService::Source::Online &&
           service_.onlineProvider() == QtLyricsService::OnlineProvider::LyricFind;
}
QString LyricsBinding::lyricsAttribution() const {
    return lyricsCopyProtected() ? service_.onlineAttribution() : QString{};
}
bool LyricsBinding::lyricFindConfigured() const { return service_.lyricFindConfigured(); }
QString LyricsBinding::romajiSourceRevision() const {
    if (!variants_->allowed() || !variants_->ready || path_.isEmpty() || lyricsCopyProtected())
        return {};
    const auto selected = variants_->selected;
    if (!selected && service_.status() != QtLyricsService::Status::Ready)
        return {};
    const auto revision = QString::number(service_.contextRevision()) + QLatin1Char(':') +
                          QString::number(selection_revision_) + QLatin1Char(':') + QString::number(selected);
    if (romaji_->checked_revision != revision) {
        const auto found = selected ? variants_->find(selected) : variants_->library.end();
        const bool already_romanized = found != variants_->library.end() && found->kind == LyricsVariantKind::Romanized;
        const bool known_japanese =
            found != variants_->library.end() && (found->language == "ja" || found->language.starts_with("ja-"));
        romaji_->japanese = !already_romanized && containsJapaneseScript(displayedDocument(), known_japanese);
        romaji_->checked_revision = revision;
    }
    return romaji_->japanese ? revision : QString{};
}
bool LyricsBinding::canGenerateRomaji() const {
    return romaji_->romanizer && !romaji_->busy && variants_->editable() && !romajiSourceRevision().isEmpty();
}
QString LyricsBinding::romajiStatusText() const {
    const auto revision = romajiSourceRevision();
    if (revision.isEmpty())
        return {};
    if (!romaji_->romanizer)
        return tr("Prepare o componente local de romaji e reinicie o RC Music.");
    if (revision != romaji_->revision)
        return {};
    return romaji_->busy ? tr("Gerando romaji…") : romaji_->notice;
}
QString LyricsBinding::generateRomaji() {
    if (!canGenerateRomaji())
        return {};
    auto& state = *romaji_;
    const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    state.reset();
    state.token = token;
    state.revision = romajiSourceRevision();
    state.busy = true;
    try {
        state.watcher.setFuture(QtConcurrent::run([romanizer = state.romanizer, source = displayedDocument()] {
            try {
                return makeRomajiDraft(romanizer, source);
            } catch (...) {
                return std::optional<RomajiDraft>{};
            }
        }));
    } catch (...) {
        state.busy = false;
        state.notice = tr("Não foi possível iniciar o pykakasi.");
        emit changed();
        return {};
    }
    emit changed();
    return token;
}
bool LyricsBinding::saveRomaji(const QString& token, const QString& name, const QString& content) {
    auto& state = *romaji_;
    if (!variants_->editable() || state.busy || !state.current(token) || !state.draft)
        return false;
    const auto body =
        content.size() <= static_cast<qsizetype>(lyrics_max_input_bytes) ? utf8(content, false) : std::nullopt;
    if (!body)
        return false;
    const auto edited = parseLrc(*body);
    const auto same_cue = [](const LyricsLine& left, const LyricsLine& right) {
        return left.timestamp_ms == right.timestamp_ms && left.text.empty() == right.text.empty();
    };
    if (edited.error != LyricsParseError::None ||
        !std::ranges::equal(state.draft->cues, edited.document.lines, same_cue))
        return false;
    const QPointer<LyricsBinding> guard{this};
    if (!addVariant(name, QStringLiteral("ja"), QStringLiteral("Latn"), QStringLiteral("pykakasi"),
                    QStringLiteral("romanized"), content, true))
        return false;
    if (guard && state.token == token)
        state.reset();
    return true;
}
void LyricsBinding::discardRomaji(const QString& token) {
    if (!token.isEmpty() && token == romaji_->token) {
        romaji_->reset();
        emit changed();
    }
}
bool LyricsBinding::selectVariant(const QString& value) {
    if (!variants_->editable())
        return false;
    const auto id = value.isEmpty() ? std::optional<std::uint64_t>{0} : identifier(value);
    const auto previous = variants_->selected;
    if (!id || !variants_->select(*id))
        return false;
    if (previous != *id)
        ++selection_revision_;
    emit changed();
    return true;
}
bool LyricsBinding::selectOnlineLyricsCandidate(const int index) {
    if (!variants_->allowed() || variants_->busy || variants_->hasPending() || index < 0 ||
        static_cast<std::size_t>(index) >= service_.onlineDocuments().size())
        return false;
    if (index == selectedOnlineLyricsCandidate())
        return true;
    online_candidate_context_ = service_.contextRevision();
    online_candidate_ = index;
    variants_->clearSelection();
    ++selection_revision_;
    emit changed();
    return true;
}
bool LyricsBinding::addVariant(const QString& name, const QString& language, const QString& script,
                               const QString& source, const QString& kind, const QString& content,
                               const bool generated) {
    if (!variants_->editable())
        return false;
    const auto id = variants_->allocateId();
    auto draft = makeVariant(id, name, language, script, source, kind, content, generated);
    if (!draft)
        return variants_->invalid();
    auto candidate = variants_->library;
    candidate.push_back(std::move(*draft));
    if (!isValidLyricsVariants(candidate))
        return variants_->invalid();
    variants_->pending = std::move(candidate);
    variants_->pending_path = owned(path_);
    variants_->select_after_save = id;
    variants_->next_after_save = id == std::numeric_limits<std::uint64_t>::max() ? 1 : id + 1;
    return variants_->save();
}
bool LyricsBinding::saveCurrentLyrics(const QString& name, const QString& language, const QString& script,
                                      const QString& kind, const QString& expected_revision) {
    if (expected_revision.isEmpty() || expected_revision != currentLyricsRevision())
        return false;
    const auto content = lyricsDocumentAsLrc(displayedDocument());
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
bool LyricsBinding::retryVariantsSave() { return variants_->pending ? variants_->save() : variants_->loadAcquired(); }
bool LyricsBinding::reloadVariants() {
    if (!variants_->allowed() || variants_->busy || !variants_->store || (path_.isEmpty() && !variants_->hasPending()))
        return false;
    variants_->pending.reset();
    if (variants_->acquired_pending)
        variants_->acquired.state = AcquiredLyricsSaveState::Abandoned;
    variants_->acquired_pending = false;
    variants_->acquired_drafts.reset();
    variants_->pending_path.clear();
    ++selection_revision_;
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
    if (variants_->selected)
        return variants_->selected_document;
    const int index = selectedOnlineLyricsCandidate();
    return index >= 0 ? service_.onlineDocuments()[static_cast<std::size_t>(index)] : service_.document();
}
void LyricsBinding::setTrack(const QString& path, const QString& title, const QString& artist, const QString& album,
                             const qint64 duration, const QString& occurrence_id) {
    if (shutting_down_)
        return;
    const bool resuming = exit_preparing_;
    if (resuming) {
        exit_preparing_ = false;
        service_.setTrack({}, {}, {}, {}, 0);
    }
    const bool metadata_changed = title_ != title || artist_ != artist;
    const bool occurrence_changed = occurrence_ != occurrence_id;
    const bool track_changed = path_ != path;
    if (occurrence_changed)
        ++selection_revision_;
    if (track_changed) {
        position_ms_ = 0;
        variants_->clearView();
    }
    path_ = owned(path);
    occurrence_ = owned(occurrence_id);
    title_ = owned(title);
    artist_ = owned(artist);
    service_.setTrack(path_, title_, artist_, album, duration, occurrence_id);
    if ((track_changed || resuming) && !variants_->ready)
        static_cast<void>(variants_->load());
    if (metadata_changed || track_changed || occurrence_changed || resuming)
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
    variants_->queue_target.reset();
    romaji_->reset();
    service_.beginShutdown();
}
bool LyricsBinding::stopped() const {
    return service_.stopped() && !variants_->busy && !variants_->hasPending() && !romaji_->busy;
}
bool LyricsBinding::prepareExit() {
    if (!exit_preparing_) {
        exit_preparing_ = true;
        variants_->queue_target.reset();
        romaji_->reset();
        service_.cancel();
        // Force the presenter through its observable preparation phase even
        // when already drained; a synchronous save refusal can then rearm us.
        return false;
    }
    return service_.drained() && !variants_->busy && !variants_->hasPending() && !romaji_->busy;
}
QString LyricsBinding::title() const { return title_; }
QString LyricsBinding::artist() const { return artist_; }
QString LyricsBinding::statusText() const {
    if (variants_->hasPending() && path_.isEmpty())
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
        if (service_.source() == QtLyricsService::Source::Online) {
            const auto provider = service_.onlineProvider() == QtLyricsService::OnlineProvider::LyricFind
                                      ? QStringLiteral("LyricFind")
                                      : QStringLiteral("LRCLIB");
            return (hasSyncedLyrics() ? tr("Letra sincronizada · %1") : tr("Letra sem sincronização · %1"))
                .arg(provider);
        }
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
