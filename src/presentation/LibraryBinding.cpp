#include "LibraryBinding.hpp"
#include "LibraryListModel.hpp"
#include "m0/domain/MetadataSanitization.hpp"
#include "m0/ports/AudioFileLocator.hpp"
#include "m0/ports/LibraryStore.hpp"
#include "m0/ports/MetadataExtractor.hpp"

#include <QFutureWatcher>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

#include <atomic>
#include <optional>
#include <unordered_set>
#include <utility>
#include <variant>

namespace music_player::m0 {
namespace {
struct Job final {
    bool loading{};
    LibraryLoadResult loaded{};
    LibrarySaveStatus saved{LibrarySaveStatus::Failed};
    LibrarySnapshot written{};
};
struct Buffered final {
    LibraryBinding::Paths paths;
    bool seed{};
    std::string job_id{}; // Non-empty: one published download.
};
// Admissions held while the catalog is unreadable stay bounded.
constexpr std::size_t maximum_buffered_paths = maximum_library_records;
constexpr std::size_t metadata_batch = 64;

struct MetadataUpdate final {
    std::uint64_t id{};
    std::filesystem::path path{};
    std::optional<LibraryMetadata> metadata{};
    bool available{true};
};
// Library text must stay in the canonical single-line form and within its limit.
std::string libraryText(const std::optional<std::string>& value) {
    if (!value)
        return {};
    auto sanitized = sanitizeMetadataText(*value);
    if (sanitized.disposition == MetadataTextDisposition::Empty ||
        sanitized.disposition == MetadataTextDisposition::InvalidEncoding ||
        sanitized.text.size() > maximum_library_text_bytes ||
        sanitizeMetadataText(sanitized.text).disposition != MetadataTextDisposition::Unchanged)
        return {};
    return std::move(sanitized.text);
}
std::vector<MetadataUpdate> readMetadata(const std::shared_ptr<const MetadataExtractor>& extractor,
                                         const std::vector<std::pair<std::uint64_t, std::filesystem::path>>& batch,
                                         const std::shared_ptr<std::atomic_bool>& cancel) {
    std::vector<MetadataUpdate> updates;
    for (const auto& [id, path] : batch) {
        if (cancel->load())
            break;
        MetadataReadResult result = MetadataReadError{};
        try {
            result = extractor->read(path, [cancel] { return cancel->load(); });
        } catch (...) {
        }
        MetadataUpdate update{.id = id, .path = path};
        if (const auto* track = std::get_if<TrackMetadata>(&result)) {
            const auto duration = track->audio.duration_ms;
            update.metadata = LibraryMetadata{
                .title = libraryText(track->tags.title),
                .artist = libraryText(track->tags.artist),
                .album = libraryText(track->tags.album),
                // 0 marks "read, duration unknown" so the file is not read on every start.
                .duration_ms = duration && *duration > 0 ? static_cast<std::uint64_t>(*duration) : 0,
            };
        } else {
            const auto code = std::get<MetadataReadError>(result).code;
            if (code == MetadataReadErrorCode::Cancelled)
                break;
            // Unreachable files are marked unavailable; unreadable formats stay listed.
            update.available = code != MetadataReadErrorCode::SourceUnavailable;
            if (update.available)
                update.metadata = LibraryMetadata{.duration_ms = 0};
        }
        updates.push_back(std::move(update));
    }
    return updates;
}
} // namespace

static std::optional<std::uint64_t> recordId(const QString& text) {
    bool valid{};
    const auto id = text.toULongLong(&valid);
    if (!valid || id == 0 || QString::number(id) != text)
        return {};
    return id;
}

struct LibraryBinding::Impl final {
    LibraryBinding& owner;
    std::shared_ptr<LibraryStore> store;
    QueueAppender appender;
    LibraryListModel model;
    QString filter;
    LocalLibrary confirmed;
    // Confirmed catalog plus admissions not yet acknowledged by the store.
    std::optional<LocalLibrary> working;
    std::vector<Buffered> buffered;
    std::size_t buffered_paths{};
    QFutureWatcher<Job> watcher;
    QString notice;
    bool busy{}, ready{}, seed_allowed{}, save_failed{}, stopping{};
    std::shared_ptr<const MetadataExtractor> extractor;
    QFutureWatcher<std::vector<MetadataUpdate>> metadata_watcher;
    std::shared_ptr<std::atomic_bool> cancel_reads = std::make_shared<std::atomic_bool>(false);
    std::unordered_set<std::uint64_t> attempted;
    bool reading{};
    AudioFileLocator* locator{};
    std::uint64_t locate_generation{};
    bool locating{};

    Impl(LibraryBinding& q, std::shared_ptr<LibraryStore> source, QueueAppender append)
        : owner(q), store(std::move(source)), appender(std::move(append)) {
        QObject::connect(&watcher, &QFutureWatcher<Job>::finished, &owner, [this] { finished(); });
        QObject::connect(&metadata_watcher, &QFutureWatcher<std::vector<MetadataUpdate>>::finished, &owner,
                         [this] { metadataRead(); });
    }
    void enrich() {
        if (stopping || reading || !ready || !extractor)
            return;
        std::vector<std::pair<std::uint64_t, std::filesystem::path>> batch;
        for (const auto& record : (working ? *working : confirmed).snapshot().records) {
            if ((record.duration_ms && record.available) || attempted.contains(record.id))
                continue;
            attempted.insert(record.id);
            batch.emplace_back(record.id, record.source_path);
            if (batch.size() == metadata_batch)
                break;
        }
        if (batch.empty())
            return;
        reading = true;
        metadata_watcher.setFuture(QtConcurrent::run(
            [reader = extractor, batch, cancel = cancel_reads] { return readMetadata(reader, batch, cancel); }));
    }
    void metadataRead() {
        reading = false;
        if (stopping)
            return;
        auto candidate = working ? *working : confirmed;
        for (const auto& update : metadata_watcher.result()) {
            const auto* record = candidate.find(update.id);
            // Removed or relocated while reading: the result belongs to another file.
            if (!record || record->source_path.native() != update.path.native())
                continue;
            if (update.metadata) {
                const LibraryMetadata current{record->title, record->artist, record->album, record->duration_ms};
                if (!(current == *update.metadata))
                    static_cast<void>(candidate.setMetadata(update.id, *update.metadata));
            }
            if (candidate.find(update.id)->available != update.available)
                static_cast<void>(candidate.setAvailable(update.id, update.available));
        }
        working = std::move(candidate);
        if (!unsaved())
            working.reset();
        // Enrichment never retries a failed write by itself.
        if (!save_failed)
            save();
        enrich();
        emit owner.changed();
    }
    bool unsaved() const { return working && !(working->snapshot() == confirmed.snapshot()); }

    void apply(const Paths& paths, const std::string& job_id = {}) {
        auto candidate = working ? *working : confirmed;
        const auto admitted = job_id.empty() ? candidate.admit(paths) : candidate.admitDownload(paths.front(), job_id);
        if (!admitted) {
            notice =
                LibraryBinding::tr("Não foi possível adicionar à biblioteca: limite atingido ou caminho inválido.");
            return;
        }
        working = std::move(candidate);
        if (!unsaved())
            working.reset();
    }
    void accept(Paths paths, const bool seed, std::string job_id = {}) {
        if (stopping || paths.empty())
            return;
        if (!ready) {
            if (paths.size() > maximum_buffered_paths - buffered_paths)
                return;
            buffered_paths += paths.size();
            buffered.push_back({std::move(paths), seed, std::move(job_id)});
            return;
        }
        if (seed && !seed_allowed)
            return;
        apply(paths, job_id);
        if (!unsaved())
            return emit owner.changed();
        // New ingress is a fresh attempt, even after an earlier failure.
        save_failed = false;
        save();
        enrich();
        emit owner.changed();
    }
    void save() {
        if (stopping || busy || !ready || !unsaved() || !store)
            return;
        const auto snapshot = working->snapshot();
        const auto retained = store;
        busy = true;
        watcher.setFuture(QtConcurrent::run([retained, snapshot] {
            Job result;
            result.written = snapshot;
            try {
                result.saved = retained->save(snapshot);
            } catch (...) {
            }
            return result;
        }));
    }
    bool load() {
        if (stopping || busy || !store)
            return false;
        const auto retained = store;
        busy = true;
        notice = LibraryBinding::tr("Carregando a biblioteca…");
        watcher.setFuture(QtConcurrent::run([retained] {
            Job result;
            result.loading = true;
            try {
                result.loaded = retained->load();
            } catch (...) {
                result.loaded.status = LibraryLoadStatus::Unavailable;
            }
            return result;
        }));
        emit owner.changed();
        return true;
    }
    void finished() {
        auto result = watcher.result();
        busy = false;
        if (result.loading)
            loaded(std::move(result.loaded));
        else
            saved(result.saved, result.written);
        // The list shows confirmed records only.
        model.setRecords(confirmed.snapshot().records);
        // A failure waits for new ingress or an explicit retry; it never loops.
        if (!stopping && !save_failed)
            save();
        enrich();
        emit owner.changed();
    }
    void located(const std::uint64_t generation, const std::uint64_t id, const std::filesystem::path& original,
                 const AudioFileLocationResult& result) {
        if (generation != locate_generation || !locating)
            return; // Cancelled or superseded.
        locating = false;
        if (stopping)
            return;
        if (result.status == AudioFileLocationStatus::Selected) {
            auto candidate = working ? *working : confirmed;
            const auto* record = candidate.find(id);
            // Removed or moved again while the dialog was open: the answer is stale.
            if (record && record->source_path.native() == original.native()) {
                switch (candidate.relocate(id, result.source_path)) {
                case LibraryRelocation::Relocated:
                    working = std::move(candidate);
                    if (!unsaved())
                        working.reset();
                    save_failed = false;
                    save();
                    notice =
                        LibraryBinding::tr("Arquivo localizado. A faixa continua com o mesmo lugar na biblioteca.");
                    break;
                case LibraryRelocation::Conflict:
                    notice = LibraryBinding::tr("Esse arquivo já está na biblioteca como outra faixa. Nada foi "
                                                "alterado.");
                    break;
                default:
                    notice = LibraryBinding::tr("Esse arquivo não pode ser usado. Escolha outro.");
                    break;
                }
            }
        } else if (result.status == AudioFileLocationStatus::Failed) {
            notice = LibraryBinding::tr("Não foi possível abrir a escolha de arquivo. Tente novamente.");
        }
        emit owner.changed();
    }
    void loaded(LibraryLoadResult result) {
        const auto status = result.status;
        const bool usable = (status == LibraryLoadStatus::Loaded || status == LibraryLoadStatus::RecoveredFromBackup ||
                             status == LibraryLoadStatus::Missing) &&
                            confirmed.restore(std::move(result.snapshot));
        if (!usable) {
            notice = status == LibraryLoadStatus::FutureVersion
                         ? LibraryBinding::tr("A biblioteca foi salva por uma versão mais recente do RC Music. "
                                              "O arquivo foi preservado.")
                         : LibraryBinding::tr("Não foi possível carregar a biblioteca. Confira library.json e "
                                              "library.json.bak na pasta de configuração e tente novamente.");
            return;
        }
        ready = true;
        // Only the very first use may adopt the existing queue and playlists.
        seed_allowed = status == LibraryLoadStatus::Missing;
        notice = status == LibraryLoadStatus::RecoveredFromBackup
                     ? LibraryBinding::tr("Biblioteca recuperada do backup.")
                     : LibraryBinding::tr("Sua biblioteca está pronta.");
        auto pending = std::move(buffered);
        buffered.clear();
        buffered_paths = 0;
        for (auto& batch : pending)
            if (!batch.seed || seed_allowed)
                apply(batch.paths, batch.job_id);
    }
    void saved(const LibrarySaveStatus status, LibrarySnapshot written) {
        if (status == LibrarySaveStatus::Saved) {
            static_cast<void>(confirmed.restore(std::move(written)));
            if (!unsaved())
                working.reset();
            notice = LibraryBinding::tr("Biblioteca salva.");
            return;
        }
        save_failed = true;
        notice = status == LibrarySaveStatus::BlockedByFutureVersion
                     ? LibraryBinding::tr("Uma versão mais recente da biblioteca foi encontrada. O arquivo foi "
                                          "preservado e as músicas novas não foram salvas.")
                     : LibraryBinding::tr("Não foi possível salvar a biblioteca. Tente novamente.");
    }
};

LibraryBinding::LibraryBinding(std::shared_ptr<LibraryStore> store, QueueAppender append_queue, QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>(*this, std::move(store), std::move(append_queue))) {}
LibraryBinding::~LibraryBinding() {
    impl_->stopping = true;
    impl_->cancel_reads->store(true);
    impl_->metadata_watcher.waitForFinished();
    impl_->watcher.waitForFinished();
}
bool LibraryBinding::start() { return !impl_->ready && impl_->load(); }
void LibraryBinding::setMetadataExtractor(std::shared_ptr<const MetadataExtractor> extractor) {
    impl_->extractor = std::move(extractor);
}
void LibraryBinding::setFileLocator(AudioFileLocator* locator) { impl_->locator = locator; }
bool LibraryBinding::canLocate() const {
    return impl_->locator && impl_->ready && !impl_->locating && !impl_->stopping;
}
bool LibraryBinding::locate(const QString& record_id) {
    const auto id = recordId(record_id);
    if (!id || !canLocate())
        return false;
    const auto* record = (impl_->working ? *impl_->working : impl_->confirmed).find(*id);
    if (!record)
        return false;
    const auto original = record->source_path;
    const auto generation = ++impl_->locate_generation;
    impl_->locating = true;
    const QPointer<LibraryBinding> alive(this);
    bool accepted{};
    try {
        accepted = impl_->locator->locate(original, [alive, generation, id = *id, original](auto result) {
            if (alive)
                alive->impl_->located(generation, id, original, result);
        });
    } catch (...) {
    }
    if (!alive)
        return false;
    if (!accepted)
        impl_->locating = false;
    emit changed();
    return accepted;
}
void LibraryBinding::cancelLocate() {
    if (!impl_->locating)
        return;
    impl_->locating = false;
    ++impl_->locate_generation;
    if (impl_->locator)
        impl_->locator->cancel();
    emit changed();
}
void LibraryBinding::admit(Paths paths) { impl_->accept(std::move(paths), false); }
void LibraryBinding::seed(Paths paths) { impl_->accept(std::move(paths), true); }
void LibraryBinding::admitDownload(std::filesystem::path path, std::string job_id) {
    if (job_id.empty())
        return impl_->accept({std::move(path)}, false);
    impl_->accept({std::move(path)}, false, std::move(job_id));
}
bool LibraryBinding::ready() const { return impl_->ready; }
bool LibraryBinding::busy() const { return impl_->busy; }
int LibraryBinding::count() const { return static_cast<int>(impl_->confirmed.snapshot().records.size()); }
bool LibraryBinding::hasPendingChange() const { return impl_->unsaved(); }
bool LibraryBinding::canRetrySave() const { return impl_->save_failed && !impl_->busy && impl_->unsaved(); }
QString LibraryBinding::statusText() const { return impl_->notice; }
LibrarySnapshot LibraryBinding::confirmedSnapshot() const { return impl_->confirmed.snapshot(); }
bool LibraryBinding::savingForExit() const {
    return impl_->busy || (impl_->ready && impl_->unsaved() && !impl_->save_failed);
}
bool LibraryBinding::retrySave() {
    if (!canRetrySave())
        return false;
    impl_->save_failed = false;
    impl_->save();
    emit changed();
    return impl_->busy;
}
QObject* LibraryBinding::model() const { return &impl_->model; }
QString LibraryBinding::filterText() const { return impl_->filter; }
void LibraryBinding::setFilterText(const QString& text) {
    if (text == impl_->filter)
        return;
    impl_->filter = text;
    impl_->model.setFilter(text);
    emit changed();
}
int LibraryBinding::visibleCount() const { return impl_->model.count(); }
bool LibraryBinding::enqueue(const QString& record_id) {
    const auto id = recordId(record_id);
    const auto* record = id ? impl_->confirmed.find(*id) : nullptr;
    if (!record || !impl_->appender || impl_->stopping)
        return false;
    try {
        return impl_->appender({record->source_path});
    } catch (...) {
        return false;
    }
}
bool LibraryBinding::remove(const QString& record_id) {
    const auto id = recordId(record_id);
    if (!id || !impl_->ready || impl_->stopping)
        return false;
    auto candidate = impl_->working ? *impl_->working : impl_->confirmed;
    if (!candidate.remove(*id))
        return false;
    impl_->working = std::move(candidate);
    impl_->save_failed = false;
    impl_->save();
    emit changed();
    return true;
}
bool LibraryBinding::retryLoad() { return !impl_->ready && impl_->load(); }
void LibraryBinding::beginShutdown() {
    cancelLocate();
    impl_->stopping = true;
    impl_->cancel_reads->store(true);
}
} // namespace music_player::m0
