#include "LyricsBinding.hpp"
#include "adapters/QtLyricsService.hpp"
#include "boundary/FilesystemPath.hpp"
#include "m0/domain/AcquiredLyrics.hpp"
#include "m0/domain/LocalAudioFilePolicy.hpp"
#include "m0/ports/LyricsVariantsStore.hpp"

#include <QDir>
#include <QFileInfo>
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
QString recordingSearchError(const LyricsSearchError error) {
    switch (error) {
    case LyricsSearchError::None:
        return {};
    case LyricsSearchError::InvalidQuery:
        return LyricsBinding::tr("Confira o título, o artista e o álbum da busca.");
    case LyricsSearchError::Unavailable:
        return LyricsBinding::tr("A busca de letras está indisponível agora.");
    case LyricsSearchError::RateLimited:
        return LyricsBinding::tr("Aguarde antes de buscar novamente no LRCLIB.");
    case LyricsSearchError::Timeout:
        return LyricsBinding::tr("A busca no LRCLIB excedeu o tempo limite.");
    case LyricsSearchError::Network:
        return LyricsBinding::tr("Falha de conexão com o LRCLIB. Confira sua conexão e tente novamente.");
    case LyricsSearchError::AccessDenied:
        return LyricsBinding::tr("O LRCLIB não permitiu a consulta.");
    case LyricsSearchError::InvalidResponse:
        return LyricsBinding::tr("O LRCLIB enviou uma resposta inválida.");
    case LyricsSearchError::ResponseTooLarge:
        return LyricsBinding::tr("A resposta do LRCLIB excedeu os limites. Tente uma busca mais específica.");
    }
    return {};
}
struct VariantsJob final {
    LyricsVariantsLoadResult loaded{};
    LyricsVariantsSaveStatus saved{LyricsVariantsSaveStatus::Failed};
    AcquiredLyricsMergeResult merged{};
    bool invalid_drafts{};
    bool stale_catalog{};
};
bool readableCatalog(const LyricsVariantsLoadStatus status) {
    return status == LyricsVariantsLoadStatus::Missing || status == LyricsVariantsLoadStatus::Loaded ||
           status == LyricsVariantsLoadStatus::RecoveredFromBackup;
}
std::uint64_t nextVariantId(const std::uint64_t id) {
    return id == std::numeric_limits<std::uint64_t>::max() ? 1 : id + 1;
}
std::uint64_t allocateVariantId(const LyricsVariants& library, std::uint64_t id) {
    while (std::ranges::any_of(library, [id](const LyricsVariant& variant) { return variant.id == id; }))
        id = nextVariantId(id);
    return id;
}
QVariantMap variantFields(const LyricsVariant& item) {
    return {{"id", QString::number(item.id)},  {"name", text(item.name)},
            {"language", text(item.language)}, {"script", text(item.script)},
            {"source", text(item.source)},     {"kind", QString::fromUtf8(lyricsVariantKindName(item.kind))},
            {"generated", item.generated}};
}
QVariantMap variantEditFields(const QString& token, const LyricsVariant& item) {
    auto result = variantFields(item);
    result.insert("token", token);
    result.insert("content", text(item.content));
    result.insert("synchronized", !parseLrc(item.content).document.lines.empty());
    return result;
}
QVariantList variantRows(const LyricsVariants& library) {
    QVariantList result;
    for (const auto& item : library)
        result.push_back(variantFields(item));
    return result;
}
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
    struct EditTarget final {
        QString token, path, occurrence;
        LyricsVariant original;
        QString manager_token;
    };
    struct QueueManager final {
        QueueTarget target;
        LyricsVariants library;
        QString notice;
        bool ready{};
        std::uint64_t next_id{1};
    };
    struct QueueMutation final {
        QString token;
        LyricsVariants original;
        std::uint64_t next_id{1};
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
    std::optional<EditTarget> edit_target;
    std::optional<QueueManager> queue_manager;
    std::optional<QueueMutation> queued_mutation;
    QString loading_manager_token;
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
    bool queueCurrent(const QString& token) const {
        return !token.isEmpty() && queue_manager && queue_manager->target.token == token && queue_source &&
               queue_source(queue_manager->target.occurrence) == queue_manager->target.path;
    }
    bool queueEditable(const QString& token) const {
        return canAppend() && queueCurrent(token) && queue_manager->ready;
    }
    void closeQueueManager() {
        if (edit_target && !edit_target->manager_token.isEmpty())
            edit_target.reset();
        queue_manager.reset();
    }
    bool loadQueueManager(const QString& token) {
        if (!canAppend() || !queueCurrent(token))
            return false;
        auto& manager = *queue_manager;
        manager.ready = false;
        manager.library.clear();
        manager.notice = LyricsBinding::tr("Carregando versões da letra da faixa…");
        const auto retained_store = store;
        const auto path = nativePath(manager.target.path);
        loading_manager_token = token;
        busy = true;
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
            loading_manager_token.clear();
            manager.notice = LyricsBinding::tr("Não foi possível carregar as versões da faixa.");
        }
        emit owner.changed();
        return true;
    }
    bool queueMutation(LyricsVariants candidate, const std::uint64_t following_id) {
        auto& manager = *queue_manager;
        if (!isValidLyricsVariants(candidate)) {
            manager.notice = LyricsBinding::tr("Confira o nome, o texto e os limites da versão da letra.");
            emit owner.changed();
            return false;
        }
        queued_mutation = QueueMutation{manager.target.token, manager.library, following_id};
        pending_path = manager.target.path;
        pending = std::move(candidate);
        return save();
    }
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
        if (edit_target && edit_target->manager_token.isEmpty())
            edit_target.reset();
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
                next_id = nextVariantId(item.id);
        next_id = allocateVariantId(library, next_id);
    }
    std::uint64_t allocateId() const { return allocateVariantId(library, next_id); }
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
        const auto expected = queued_mutation ? std::optional<LyricsVariants>{queued_mutation->original} : std::nullopt;
        job_path = owned(pending_path);
        busy = true;
        loading = false;
        if (acquired_pending)
            acquired.state = AcquiredLyricsSaveState::Saving;
        notice = LyricsBinding::tr("Salvando versão da letra…");
        try {
            watcher.setFuture(QtConcurrent::run([retained_store, path, candidate, expected] {
                VariantsJob result;
                try {
                    if (expected) {
                        const auto loaded = retained_store->load(path);
                        if (readableCatalog(loaded.status) && loaded.variants == candidate) {
                            result.saved = LyricsVariantsSaveStatus::Saved;
                            return result;
                        }
                        if (!readableCatalog(loaded.status) || loaded.variants != *expected) {
                            result.stale_catalog = true;
                            return result;
                        }
                    }
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
        busy = false;
        if (!loading_manager_token.isEmpty()) {
            const auto token = std::exchange(loading_manager_token, QString{});
            if (queueCurrent(token)) {
                auto& manager = *queue_manager;
                manager.ready = readableCatalog(result.loaded.status) && isValidLyricsVariants(result.loaded.variants);
                if (manager.ready) {
                    manager.library = std::move(result.loaded.variants);
                    for (const auto& entry : manager.library)
                        if (entry.id >= manager.next_id)
                            manager.next_id = nextVariantId(entry.id);
                    manager.notice =
                        manager.library.empty() ? LyricsBinding::tr("Esta faixa não tem versões salvas.") : QString{};
                } else {
                    manager.notice = LyricsBinding::tr(
                        "Não foi possível carregar as versões da faixa. Os arquivos foram preservados.");
                }
            }
            if (!ready && allowed() && !hasPending() && !owner.path_.isEmpty())
                static_cast<void>(load());
            emit owner.changed();
            return;
        }
        const bool same_track = job_path == owner.path_;
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
                    owner.updateReaderDocument();
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
            if (queued_mutation && queueCurrent(queued_mutation->token)) {
                queue_manager->library = *pending;
                queue_manager->next_id = queued_mutation->next_id;
                queue_manager->notice = LyricsBinding::tr("Versões da faixa salvas.");
            }
            if (same_track) {
                const auto keep_selection = acquired_pending || queued_mutation ? selected : select_after_save;
                library = std::move(*pending);
                ready = true;
                if (!select(keep_selection))
                    clearSelection();
                if (acquired_pending || queued_mutation)
                    resetAllocator();
                else
                    next_id = next_after_save;
            }
            notice = LyricsBinding::tr("Versão da letra salva.");
            pending.reset();
            pending_path.clear();
            queued_mutation.reset();
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
            if (result.stale_catalog)
                notice =
                    LyricsBinding::tr("As versões da faixa mudaram no disco. Recarregue antes de editar novamente.");
            if (queued_mutation && queueCurrent(queued_mutation->token))
                queue_manager->notice = notice;
        }
        if (!same_track && !ready && allowed() && !hasPending() && !owner.path_.isEmpty())
            static_cast<void>(load());
        owner.updateReaderDocument();
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
struct LyricsBinding::RecordingSearchState final {
    std::vector<LyricsSearchCandidate> candidates;
    std::optional<LyricsDocument> displayed;
    QString status;
    quint64 revision{};
    bool busy{};
    bool importing{};
};
LyricsBinding::LyricsBinding(QtLyricsService& service, QObject* parent) : LyricsBinding(service, {}, parent) {}
LyricsBinding::LyricsBinding(QtLyricsService& service, std::shared_ptr<LyricsVariantsStore> variants_store,
                             QObject* parent)
    : QObject(parent), variants_(std::make_unique<VariantsState>(*this, std::move(variants_store))),
      romaji_(std::make_unique<RomajiState>(*this)), recording_search_(std::make_unique<RecordingSearchState>()),
      service_(service) {
    updateOnlineLyricsCandidates();
    updateReaderDocument();
    connect(&service_, &QtLyricsService::changed, this, [this] {
        finishDirectSearch();
        updateOnlineLyricsCandidates();
        updateReaderDocument();
        emit changed();
    });
}
LyricsBinding::~LyricsBinding() = default;
void LyricsBinding::setQueueLyricsSource(std::function<QString(const QString&)> source) {
    variants_->queue_target.reset();
    variants_->closeQueueManager();
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
QString LyricsBinding::openQueuedManager(const QString& occurrence_id) {
    if (!queueVariantAvailable() || occurrence_id.isEmpty())
        return {};
    const auto path = variants_->queue_source(occurrence_id);
    if (!queuedTarget(path))
        return {};
    const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    variants_->closeQueueManager();
    variants_->queue_manager =
        VariantsState::QueueManager{.target = {owned(occurrence_id), owned(path), token}, .library = {}, .notice = {}};
    return variants_->loadQueueManager(token) ? token : QString{};
}
void LyricsBinding::closeQueuedManager(const QString& token) {
    if (!variants_->queue_manager || variants_->queue_manager->target.token != token)
        return;
    variants_->closeQueueManager();
    emit changed();
}
QVariantList LyricsBinding::queuedManagerVariants() const {
    const auto& manager = variants_->queue_manager;
    return manager && variants_->queueCurrent(manager->target.token) ? variantRows(manager->library) : QVariantList{};
}
bool LyricsBinding::queuedManagerBusy() const { return variants_->busy; }
bool LyricsBinding::queuedManagerCanEdit() const {
    const auto& manager = variants_->queue_manager;
    return manager && variants_->queueEditable(manager->target.token);
}
QString LyricsBinding::queuedManagerStatus() const {
    const auto& manager = variants_->queue_manager;
    if (!manager)
        return {};
    if (!variants_->queueCurrent(manager->target.token))
        return tr("A faixa escolhida saiu da fila ou mudou de arquivo.");
    if (variants_->busy && variants_->queued_mutation && variants_->queued_mutation->token == manager->target.token)
        return tr("Salvando versões da faixa…");
    return manager->notice;
}
bool LyricsBinding::reloadQueuedManager(const QString& token) {
    if (!variants_->allowed() || variants_->busy || !variants_->queueCurrent(token))
        return false;
    if (variants_->queued_mutation && variants_->queued_mutation->token == token) {
        variants_->queued_mutation.reset();
        variants_->pending.reset();
        variants_->pending_path.clear();
    }
    if (variants_->edit_target && variants_->edit_target->manager_token == token)
        variants_->edit_target.reset();
    return variants_->loadQueueManager(token);
}
QVariantMap LyricsBinding::prepareQueuedVariantEdit(const QString& token, const QString& value) {
    const auto id = identifier(value);
    if (!id || !variants_->queueEditable(token))
        return {};
    const auto& manager = *variants_->queue_manager;
    const auto found = std::ranges::find(manager.library, *id, &LyricsVariant::id);
    if (found == manager.library.end())
        return {};
    const auto edit = QUuid::createUuid().toString(QUuid::WithoutBraces);
    variants_->edit_target =
        VariantsState::EditTarget{edit, manager.target.path, manager.target.occurrence, *found, token};
    return variantEditFields(edit, *found);
}
bool LyricsBinding::removeQueuedVariant(const QString& token, const QString& value) {
    const auto id = identifier(value);
    if (!id || !variants_->queueEditable(token))
        return false;
    auto candidate = variants_->queue_manager->library;
    if (!std::erase_if(candidate, [id](const LyricsVariant& entry) { return entry.id == *id; }))
        return false;
    if (variants_->edit_target && variants_->edit_target->manager_token == token)
        variants_->edit_target.reset();
    return variants_->queueMutation(std::move(candidate), variants_->queue_manager->next_id);
}
bool LyricsBinding::addQueuedManagerVariant(const QString& token, const QString& name, const QString& language,
                                            const QString& script, const QString& source, const QString& kind,
                                            const QString& content, const bool generated) {
    if (!variants_->queueEditable(token))
        return false;
    const auto& manager = *variants_->queue_manager;
    const auto id = allocateVariantId(manager.library, manager.next_id);
    const auto draft = makeVariant(id, name, language, script, source, kind, content, generated);
    if (!draft)
        return variants_->invalid();
    auto candidate = manager.library;
    candidate.push_back(*draft);
    return variants_->queueMutation(std::move(candidate), nextVariantId(id));
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
QVariantList LyricsBinding::variants() const { return variantRows(variants_->library); }
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
        (!recording_search_->displayed && service_.status() != QtLyricsService::Status::Ready))
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
    if (variants_->selected || recording_search_->displayed || service_.onlineDocuments().empty())
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
    return !variants_->selected && !recording_search_->displayed &&
           service_.source() == QtLyricsService::Source::Online &&
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
    if (!selected && !recording_search_->displayed && service_.status() != QtLyricsService::Status::Ready)
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
    updateReaderDocument();
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
    recording_search_->displayed.reset();
    ++selection_revision_;
    updateReaderDocument();
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
    const bool lrclib = recording_search_->displayed || service_.source() == QtLyricsService::Source::Online;
    return addVariant(name, language, script, lrclib ? QStringLiteral("LRCLIB") : QStringLiteral("arquivo local"), kind,
                      text(*content), false);
}
bool LyricsBinding::removeVariant(const QString& value) {
    const auto id = identifier(value);
    if (!variants_->editable() || !id || variants_->find(*id) == variants_->library.end())
        return false;
    auto candidate = variants_->library;
    std::erase_if(candidate, [&](const auto& item) { return item.id == *id; });
    const auto fallback =
        service_.source() != QtLyricsService::Source::Local && !candidate.empty() ? candidate.front().id : 0;
    variants_->pending = std::move(candidate);
    variants_->pending_path = owned(path_);
    variants_->select_after_save = variants_->selected == *id ? fallback : variants_->selected;
    variants_->next_after_save = variants_->next_id;
    return variants_->save();
}
QVariantMap LyricsBinding::prepareVariantEdit(const QString& value) {
    const auto id = identifier(value);
    if (!variants_->editable() || !id)
        return {};
    const auto found = variants_->find(*id);
    if (found == variants_->library.end())
        return {};
    const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    variants_->edit_target = VariantsState::EditTarget{token, owned(path_), owned(occurrence_), *found, {}};
    return variantEditFields(token, *found);
}
bool LyricsBinding::saveVariantEdit(const QString& token, const QString& name, const QString& language,
                                    const QString& script, const QString& kind, const QString& content,
                                    const bool as_new, const bool edit_timing) {
    auto& edit = variants_->edit_target;
    if (!edit || token != edit->token)
        return false;
    const bool queued = !edit->manager_token.isEmpty();
    if (queued ? !variants_->queueEditable(edit->manager_token)
               : (!variants_->editable() || edit->path != path_ || edit->occurrence != occurrence_))
        return false;
    const auto& library = queued ? variants_->queue_manager->library : variants_->library;
    const auto found = std::ranges::find(library, edit->original.id, &LyricsVariant::id);
    if (found == library.end() || *found != edit->original)
        return false;
    const auto next_id = queued ? variants_->queue_manager->next_id : variants_->next_id;
    const auto id = as_new ? allocateVariantId(library, next_id) : found->id;
    auto draft = makeVariant(id, name, language, script, text(found->source), kind, content, found->generated);
    if (!draft)
        return variants_->invalid();
    const auto parsed = parseLrc(draft->content);
    if (parsed.error != LyricsParseError::None)
        return variants_->invalid();
    if (!edit_timing) {
        const auto original = parseLrc(found->content);
        if (!std::ranges::equal(original.document.lines, parsed.document.lines, {}, &LyricsLine::timestamp_ms,
                                &LyricsLine::timestamp_ms))
            return variants_->invalid();
    }
    auto candidate = library;
    if (as_new)
        candidate.push_back(std::move(*draft));
    else
        candidate[static_cast<std::size_t>(found - library.begin())] = std::move(*draft);
    if (!isValidLyricsVariants(candidate))
        return variants_->invalid();
    edit.reset();
    if (queued)
        return variants_->queueMutation(std::move(candidate), as_new ? nextVariantId(id) : next_id);
    variants_->pending = std::move(candidate);
    variants_->pending_path = owned(path_);
    variants_->select_after_save = variants_->selected;
    variants_->next_after_save = as_new ? nextVariantId(id) : next_id;
    return variants_->save();
}
void LyricsBinding::cancelVariantEdit(const QString& token) {
    auto& edit = variants_->edit_target;
    if (edit && edit->token == token)
        edit.reset();
}
bool LyricsBinding::importVariantLrc(const QUrl& selected_file, const QString& expected_track, const QString& token) {
    if (!variants_->editable() || path_.isEmpty() || expected_track != occurrence_ || token.isEmpty() ||
        token.size() > 80 || recording_search_->importing || !selected_file.isLocalFile() ||
        !selected_file.host().isEmpty() || selected_file.hasQuery() || selected_file.hasFragment())
        return false;
    const auto file = selected_file.toLocalFile();
    if (file.isEmpty() || file.size() > 8192 || file.contains(QChar{0}) || !QDir::isAbsolutePath(file) ||
        file.startsWith("//") || file.startsWith("\\\\"))
        return false;
    using Import = std::pair<QString, QString>;
    auto* watcher = new QFutureWatcher<Import>(this);
    const auto occurrence = owned(occurrence_);
    const auto path = owned(path_);
    const auto request = owned(token);
    recording_search_->importing = true;
    connect(watcher, &QFutureWatcher<Import>::finished, this, [this, watcher, occurrence, path, request] {
        const auto result = watcher->result();
        watcher->deleteLater();
        recording_search_->importing = false;
        if (variants_->allowed() && occurrence == occurrence_ && path == path_)
            emit importedVariantLrc(request, result.first, result.second);
    });
    try {
        watcher->setFuture(QtConcurrent::run([file]() -> Import {
            try {
                const auto document = QtLyricsService::readLrcFile(file);
                const auto lyrics = document ? lyricsDocumentAsLrc(*document) : std::nullopt;
                if (lyrics)
                    return {QFileInfo(file).completeBaseName().left(64), text(*lyrics)};
            } catch (...) {
            }
            return {};
        }));
    } catch (...) {
        watcher->deleteLater();
        recording_search_->importing = false;
        return false;
    }
    return true;
}
bool LyricsBinding::retryVariantsSave() { return variants_->pending ? variants_->save() : variants_->loadAcquired(); }
bool LyricsBinding::reloadVariants() {
    if (!variants_->allowed() || variants_->busy || !variants_->store || (path_.isEmpty() && !variants_->hasPending()))
        return false;
    variants_->pending.reset();
    variants_->queued_mutation.reset();
    if (variants_->acquired_pending)
        variants_->acquired.state = AcquiredLyricsSaveState::Abandoned;
    variants_->acquired_pending = false;
    variants_->acquired_drafts.reset();
    variants_->pending_path.clear();
    ++selection_revision_;
    variants_->clearView();
    updateReaderDocument();
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
    if (recording_search_->displayed)
        return *recording_search_->displayed;
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
    const bool metadata_changed = title_ != title || artist_ != artist || album_ != album || duration_ms_ != duration;
    const bool occurrence_changed = occurrence_ != occurrence_id;
    const bool track_changed = path_ != path;
    if (track_changed || occurrence_changed || resuming) {
        direct_search_selection_.reset();
        recording_search_->displayed.reset();
        cancelRecordingSearch();
        ++selection_revision_;
    }
    if (track_changed) {
        position_ms_ = 0;
        variants_->clearView();
    }
    path_ = owned(path);
    occurrence_ = owned(occurrence_id);
    title_ = owned(title);
    artist_ = owned(artist);
    album_ = owned(album);
    duration_ms_ = duration;
    service_.setTrack(path_, title_, artist_, album_, duration_ms_, occurrence_);
    if ((track_changed || resuming) && !variants_->ready)
        static_cast<void>(variants_->load());
    if (metadata_changed || track_changed || occurrence_changed || resuming) {
        updateReaderDocument();
        emit changed();
    }
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
    variants_->closeQueueManager();
    romaji_->reset();
    direct_search_selection_.reset();
    recording_search_->displayed.reset();
    cancelRecordingSearch();
    service_.beginShutdown();
}
bool LyricsBinding::stopped() const {
    return service_.stopped() && !variants_->busy && !variants_->hasPending() && !romaji_->busy &&
           !recording_search_->importing;
}
bool LyricsBinding::prepareExit() {
    if (!exit_preparing_) {
        exit_preparing_ = true;
        variants_->queue_target.reset();
        variants_->closeQueueManager();
        romaji_->reset();
        direct_search_selection_.reset();
        recording_search_->displayed.reset();
        cancelRecordingSearch();
        service_.cancel();
        // Force the presenter through its observable preparation phase even
        // when already drained; a synchronous save refusal can then rearm us.
        return false;
    }
    return service_.drained() && !variants_->busy && !variants_->hasPending() && !romaji_->busy &&
           !recording_search_->importing;
}
QString LyricsBinding::title() const { return title_; }
QString LyricsBinding::artist() const { return artist_; }
QString LyricsBinding::album() const { return album_; }
qint64 LyricsBinding::durationMs() const { return duration_ms_; }
QString LyricsBinding::statusText() const {
    if (variants_->hasPending() && path_.isEmpty())
        return variantsStatusText();
    if (variants_->selected) {
        const auto found = variants_->find(variants_->selected);
        if (found != variants_->library.end())
            return tr("%1 · versão salva%2").arg(text(found->name), found->generated ? tr(" · gerada") : QString{});
    }
    if (recording_search_->displayed) {
        if (hasSyncedLyrics())
            return tr("Letra sincronizada · LRCLIB");
        return plainText().isEmpty() ? tr("Faixa instrumental · LRCLIB") : tr("Letra sem sincronização · LRCLIB");
    }
    if (!service_.message().isEmpty())
        return service_.message();
    switch (service_.status()) {
    case QtLyricsService::Status::Idle:
        return {};
    case QtLyricsService::Status::LoadingLocal:
        return tr("Carregando letra…");
    case QtLyricsService::Status::Searching:
        return tr("Buscando letra…");
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
void LyricsBinding::updateReaderDocument() {
    QVariantList verses;
    const auto& document = displayedDocument();
    verses.reserve(static_cast<qsizetype>(document.lines.size()));
    for (const auto& line : document.lines)
        verses.push_back(QVariantMap{{"text", text(line.text)}, {"timeMs", qint64{line.timestamp_ms}}});
    if (reader_verses_ != verses) {
        reader_verses_ = std::move(verses);
        emit readerDocumentChanged();
    }
}
QVariantList LyricsBinding::readerVerses() const { return reader_verses_; }
int LyricsBinding::activeReaderVerse() const {
    const auto at = lyricsLineAt(displayedDocument(), position_ms_);
    return at ? static_cast<int>(*at) : -1;
}
QString LyricsBinding::plainText() const { return text(displayedDocument().plain_text); }
bool LyricsBinding::hasSyncedLyrics() const { return !displayedDocument().lines.empty(); }
bool LyricsBinding::busy() const { return service_.busy(); }
bool LyricsBinding::canSearch() const { return !shutting_down_ && !exit_preparing_ && service_.canSearch(); }
bool LyricsBinding::search(const QString& title, const QString& artist) {
    if (!canSearch())
        return false;
    direct_search_context_ = service_.contextRevision();
    direct_search_selection_ = selection_revision_;
    if (!service_.searchOnline(title, artist)) {
        direct_search_selection_.reset();
        return false;
    }
    return true;
}
void LyricsBinding::finishDirectSearch() {
    const auto status = service_.status();
    if (!direct_search_selection_ || service_.contextRevision() == direct_search_context_ ||
        status == QtLyricsService::Status::Searching)
        return;
    const auto selection = std::exchange(direct_search_selection_, {});
    if (status == QtLyricsService::Status::Ready && selection == selection_revision_) {
        variants_->clearSelection();
        recording_search_->displayed.reset();
        ++selection_revision_;
    }
}
bool LyricsBinding::recordingSearchBusy() const { return recording_search_->busy; }
QString LyricsBinding::recordingSearchStatus() const { return recording_search_->status; }
QString LyricsBinding::recordingSearchRevision() const { return QString::number(recording_search_->revision); }
QVariantList LyricsBinding::recordingCandidates() const {
    QVariantList rows;
    rows.reserve(static_cast<qsizetype>(recording_search_->candidates.size()));
    for (const auto& candidate : recording_search_->candidates) {
        QVariantList formats;
        for (const auto& document : candidate.formats) {
            const bool synchronized = !document.lines.empty();
            formats.push_back(QVariantMap{{"label", synchronized ? tr("Sincronizada") : tr("Texto simples")},
                                          {"synchronized", synchronized}});
        }
        rows.push_back(QVariantMap{{"id", QString::number(candidate.id)},
                                   {"title", candidate.recording.title},
                                   {"artist", candidate.recording.artist},
                                   {"album", candidate.recording.album},
                                   {"durationMs", candidate.recording.duration_ms},
                                   {"instrumental", candidate.instrumental},
                                   {"formats", std::move(formats)}});
    }
    return rows;
}
bool LyricsBinding::searchRecordings(const QString& title, const QString& artist, const QString& album,
                                     qint64 duration_ms) {
    auto& state = *recording_search_;
    if (shutting_down_ || exit_preparing_ || path_.isEmpty() || state.busy)
        return false;
    state.candidates.clear();
    state.status = tr("Buscando gravações no LRCLIB…");
    state.busy = true;
    const auto revision = ++state.revision;
    emit recordingSearchChanged();
    if (state.revision != revision)
        return false;
    return service_.search({owned(title), owned(artist), owned(album), duration_ms},
                           [guard = QPointer<LyricsBinding>{this}, revision](LyricsSearchResult result) {
                               if (!guard || guard->recording_search_->revision != revision)
                                   return;
                               auto& search = *guard->recording_search_;
                               search.busy = false;
                               search.candidates = std::move(result.candidates);
                               search.status =
                                   result.error != LyricsSearchError::None ? recordingSearchError(result.error)
                                   : search.candidates.empty() ? tr("Nenhuma gravação encontrada no LRCLIB.")
                                                               : QString{};
                               emit guard->recordingSearchChanged();
                           });
}
void LyricsBinding::cancelRecordingSearch() {
    auto& state = *recording_search_;
    if (!state.busy && state.candidates.empty() && state.status.isEmpty())
        return;
    ++state.revision;
    state.busy = false;
    state.candidates.clear();
    state.status.clear();
    service_.cancelSearch();
    emit recordingSearchChanged();
}
const LyricsSearchCandidate* LyricsBinding::recordingCandidate(const int index, const QString& revision) const {
    if (!variants_->allowed() || path_.isEmpty() || recording_search_->busy || revision != recordingSearchRevision() ||
        index < 0 || static_cast<std::size_t>(index) >= recording_search_->candidates.size())
        return nullptr;
    return &recording_search_->candidates[static_cast<std::size_t>(index)];
}
const LyricsDocument* LyricsBinding::recordingFormat(const int index, const int format, const QString& revision) const {
    const auto* candidate = recordingCandidate(index, revision);
    if (!candidate || format < 0 || static_cast<std::size_t>(format) >= candidate->formats.size())
        return nullptr;
    return &candidate->formats[static_cast<std::size_t>(format)];
}
QString LyricsBinding::previewRecording(const int index, const int format, const QString& revision) const {
    const auto* document = recordingFormat(index, format, revision);
    return document ? text(document->plain_text) : QString{};
}
bool LyricsBinding::useRecording(const int index, const int format, const QString& revision) {
    if (variants_->busy || variants_->hasPending())
        return false;
    const auto* candidate = recordingCandidate(index, revision);
    if (!candidate)
        return false;
    if (candidate->instrumental && format == -1) {
        recording_search_->displayed.emplace();
    } else {
        const auto* document = recordingFormat(index, format, revision);
        if (!document)
            return false;
        recording_search_->displayed = *document;
    }
    variants_->clearSelection();
    ++selection_revision_;
    updateReaderDocument();
    emit changed();
    return true;
}
bool LyricsBinding::canSaveRecording() const { return !path_.isEmpty() && variants_->editable(); }
bool LyricsBinding::saveRecording(const int index, const int format, const QString& name, const QString& revision) {
    const auto* document = recordingFormat(index, format, revision);
    if (!document || !canSaveRecording())
        return false;
    const auto content = lyricsDocumentAsLrc(*document);
    if (!content)
        return variants_->invalid();
    return addVariant(name, {}, {}, QStringLiteral("LRCLIB"), QStringLiteral("original"), text(*content), false);
}
} // namespace music_player::m0
