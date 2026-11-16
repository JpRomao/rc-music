#include "LibraryBinding.hpp"
#include "LibraryListModel.hpp"
#include "m0/ports/LibraryStore.hpp"

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include <optional>
#include <utility>

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

    Impl(LibraryBinding& q, std::shared_ptr<LibraryStore> source, QueueAppender append)
        : owner(q), store(std::move(source)), appender(std::move(append)) {
        QObject::connect(&watcher, &QFutureWatcher<Job>::finished, &owner, [this] { finished(); });
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
    impl_->watcher.waitForFinished();
}
bool LibraryBinding::start() { return !impl_->ready && impl_->load(); }
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
void LibraryBinding::beginShutdown() { impl_->stopping = true; }
} // namespace music_player::m0
