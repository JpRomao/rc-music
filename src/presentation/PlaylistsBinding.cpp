#include "PlaylistsBinding.hpp"
#include "m0/ports/AudioFileLocator.hpp"
#include "m0/ports/AudioFilesPicker.hpp"
#include "m0/ports/PlaylistStore.hpp"

#include <QFutureWatcher>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <optional>
#include <utility>

namespace music_player::m0 {
namespace {
std::optional<std::uint64_t> identifier(const QString& text) {
    bool valid{};
    const auto id = text.toULongLong(&valid);
    if (!valid || id == 0 || QString::number(id) != text)
        return {};
    return id;
}
QString pathText(const std::filesystem::path& path) {
#ifdef Q_OS_WIN
    return QString::fromStdWString(path.native());
#else
    return QString::fromUtf8(path.native().data(), static_cast<qsizetype>(path.native().size()));
#endif
}
std::string nameText(const QString& text) {
    const auto trimmed = text.trimmed();
    const auto utf8 = trimmed.toUtf8();
    if (QString::fromUtf8(utf8) != trimmed)
        return {};
    return {utf8.constData(), static_cast<std::size_t>(utf8.size())};
}
struct Job final {
    PlaylistLoadResult loaded{};
    PlaylistSaveStatus saved{PlaylistSaveStatus::Failed};
};
struct FileSelection final {
    std::uint64_t generation;
    std::uint64_t playlist_id;
    std::optional<std::uint64_t> entry_id{};
};
} // namespace
struct PlaylistsBinding::Impl final {
    PlaylistsBinding& owner;
    std::shared_ptr<PlaylistStore> store;
    QueueReader reader;
    QueueAppender appender;
    AudioFilesPicker* picker;
    AudioFileLocator* locator;
    PlaylistLibrary library;
    std::optional<PlaylistLibrary> pending;
    QFutureWatcher<Job> watcher;
    QString notice;
    std::uint64_t selected{}, select_after_save{};
    std::uint64_t file_selection_generation{};
    std::optional<FileSelection> file_selection;
    bool busy{}, ready{}, loading{}, exiting{}, stopping{}, callback_active{};
    Impl(PlaylistsBinding& q, std::shared_ptr<PlaylistStore> source, QueueReader read, QueueAppender append,
         AudioFilesPicker* file_picker, AudioFileLocator* file_locator)
        : owner(q), store(std::move(source)), reader(std::move(read)), appender(std::move(append)), picker(file_picker),
          locator(file_locator) {
        QObject::connect(&watcher, &QFutureWatcher<Job>::finished, &owner, [this] { finished(); });
    }
    bool allowed() const { return !exiting && !stopping && !callback_active; }
    bool catalogEditable() const { return allowed() && ready && !busy && !pending; }
    bool editable() const { return catalogEditable() && !file_selection; }
    const NamedPlaylist* replaceablePlaylist() const {
        if (!locator || !editable())
            return nullptr;
        const auto* playlist = library.find(selected);
        return playlist && !playlist->entries.empty() ? playlist : nullptr;
    }
    bool invalid() {
        notice = PlaylistsBinding::tr("Confira o nome, a seleção e os limites da playlist.");
        emit owner.changed();
        return false;
    }
    void finished() {
        const auto result = watcher.result();
        busy = false;
        if (loading) {
            loading = false;
            const auto status = result.loaded.status;
            const bool load_succeeded =
                (status == PlaylistLoadStatus::Loaded || status == PlaylistLoadStatus::RecoveredFromBackup ||
                 status == PlaylistLoadStatus::Missing) &&
                library.restore(result.loaded.snapshot);
            if (load_succeeded) {
                ready = true;
                selected = 0;
                pending.reset();
                notice = status == PlaylistLoadStatus::RecoveredFromBackup
                             ? PlaylistsBinding::tr("Playlists recuperadas do backup.")
                             : PlaylistsBinding::tr("Suas playlists estão prontas.");
            } else if (pending) {
                notice = status == PlaylistLoadStatus::FutureVersion
                             ? PlaylistsBinding::tr("O disco tem uma versão mais recente das playlists. "
                                                    "A edição continua em memória.")
                             : PlaylistsBinding::tr("Não foi possível recarregar as playlists. "
                                                    "A edição continua em memória.");
            } else {
                ready = false;
                notice =
                    status == PlaylistLoadStatus::FutureVersion
                        ? PlaylistsBinding::tr(
                              "As playlists foram salvas por uma versão mais recente. Os arquivos foram preservados.")
                        : PlaylistsBinding::tr(
                              "Não foi possível carregar as playlists. Confira os arquivos playlists.json e "
                              "playlists.json.bak na pasta de configuração e tente novamente.");
            }
        } else if (result.saved == PlaylistSaveStatus::Saved && pending) {
            library = std::move(*pending);
            pending.reset();
            if (library.find(select_after_save))
                selected = select_after_save;
            else if (!library.find(selected))
                selected = 0;
            notice = PlaylistsBinding::tr("Playlist salva.");
        } else {
            notice = result.saved == PlaylistSaveStatus::BlockedByFutureVersion
                         ? PlaylistsBinding::tr("Uma versão mais recente das playlists foi encontrada. Os arquivos "
                                                "foram preservados; recarregue as listas do disco.")
                         : PlaylistsBinding::tr("Não foi possível confirmar o salvamento. Tente salvar novamente ou "
                                                "recarregue as listas do disco.");
        }
        emit owner.changed();
    }
    bool load() {
        if (!allowed() || busy || file_selection || !store)
            return false;
        const auto retained_store = store;
        busy = true;
        if (!pending)
            ready = false;
        loading = true;
        notice = PlaylistsBinding::tr("Carregando playlists…");
        watcher.setFuture(QtConcurrent::run([retained_store] {
            Job result;
            try {
                result.loaded = retained_store->load();
            } catch (...) {
                result.loaded.status = PlaylistLoadStatus::Unavailable;
            }
            return result;
        }));
        emit owner.changed();
        return true;
    }
    bool save() {
        if (!allowed() || busy || !pending || !store)
            return false;
        const auto snapshot = pending->snapshot();
        const auto retained_store = store;
        busy = true;
        loading = false;
        notice = PlaylistsBinding::tr("Salvando playlist…");
        watcher.setFuture(QtConcurrent::run([retained_store, snapshot] {
            Job result;
            try {
                result.saved = retained_store->save(snapshot);
            } catch (...) {
            }
            return result;
        }));
        emit owner.changed();
        return true;
    }
    template <class Mutation> bool mutate(Mutation operation, const std::uint64_t next_selection = 0) {
        if (!editable())
            return false;
        auto candidate = library;
        if (!operation(candidate))
            return invalid();
        pending = std::move(candidate);
        select_after_save = next_selection;
        return save();
    }
    bool isCurrentFileSelection(std::uint64_t generation) const {
        return file_selection && file_selection->generation == generation;
    }
    template <typename Result>
    auto selectionCompletion(std::uint64_t generation, void (Impl::*finish)(std::uint64_t, Result)) {
        const QPointer<PlaylistsBinding> alive(&owner);
        return [alive, generation, finish](Result result) {
            if (alive) {
                QMetaObject::invokeMethod(
                    alive,
                    [alive, generation, finish, result = std::move(result)]() mutable {
                        if (alive)
                            (alive->impl_.get()->*finish)(generation, std::move(result));
                    },
                    Qt::QueuedConnection);
            }
        };
    }
    void finishFileSelection(std::uint64_t generation, AudioFilesSelectionResult result) {
        if (!isCurrentFileSelection(generation) || file_selection->entry_id)
            return;
        const auto target = file_selection->playlist_id;
        file_selection.reset();
        if (result.status == AudioFilesSelectionStatus::Selected) {
            static_cast<void>(mutate(
                [&](auto& candidate) { return candidate.append(target, std::move(result.source_paths)); }, target));
            return;
        }
        if (result.status == AudioFilesSelectionStatus::Failed)
            notice = PlaylistsBinding::tr("Não foi possível selecionar os arquivos. Tente novamente.");
        emit owner.changed();
    }
    void finishFileReplacement(std::uint64_t generation, AudioFileLocationResult result) {
        if (!isCurrentFileSelection(generation) || !file_selection->entry_id)
            return;
        const auto selection = *std::exchange(file_selection, std::nullopt);
        if (editable() && selected == selection.playlist_id) {
            if (result.status == AudioFileLocationStatus::Selected && result.source_path.is_absolute() &&
                !result.source_path.filename().empty()) {
                static_cast<void>(mutate(
                    [&](auto& candidate) {
                        return candidate.replaceEntrySource(selection.playlist_id, *selection.entry_id,
                                                            std::move(result.source_path));
                    },
                    selection.playlist_id));
                return;
            }
            if (result.status != AudioFileLocationStatus::Cancelled)
                notice =
                    PlaylistsBinding::tr("Não foi possível selecionar o arquivo de substituição. Tente novamente.");
        }
        emit owner.changed();
    }
    void cancelFileSelection() noexcept {
        const auto selection = std::exchange(file_selection, std::nullopt);
        if (!selection)
            return;
        if (selection->entry_id)
            locator->cancel();
        else
            picker->cancel();
    }
    std::optional<Paths> readQueue() {
        if (!editable() || !reader)
            return {};
        const QPointer<PlaylistsBinding> alive(&owner);
        callback_active = true;
        std::optional<Paths> paths;
        try {
            paths = reader();
        } catch (...) {
        }
        if (alive)
            callback_active = false;
        return paths;
    }
};
PlaylistsBinding::PlaylistsBinding(std::shared_ptr<PlaylistStore> store, QueueReader reader, QueueAppender appender,
                                   QObject* parent, AudioFilesPicker* picker, AudioFileLocator* locator)
    : QObject(parent),
      impl_(std::make_unique<Impl>(*this, std::move(store), std::move(reader), std::move(appender), picker, locator)) {}
PlaylistsBinding::~PlaylistsBinding() { impl_->cancelFileSelection(); }
bool PlaylistsBinding::busy() const { return impl_->busy || impl_->file_selection.has_value(); }
bool PlaylistsBinding::ready() const { return impl_->ready; }
bool PlaylistsBinding::interactionAllowed() const { return impl_->allowed(); }
QString PlaylistsBinding::statusText() const { return impl_->notice; }
QString PlaylistsBinding::selectedPlaylistId() const {
    return impl_->selected ? QString::number(impl_->selected) : QString{};
}
QVariantList PlaylistsBinding::playlists() const {
    QVariantList result;
    for (const auto& item : impl_->library.snapshot().playlists)
        result.push_back(QVariantMap{{"id", QString::number(item.id)},
                                     {"name", QString::fromUtf8(item.name)},
                                     {"count", static_cast<int>(item.entries.size())}});
    return result;
}
QVariantList PlaylistsBinding::entries() const {
    QVariantList result;
    const auto* playlist = impl_->library.find(impl_->selected);
    if (playlist)
        for (const auto& entry : playlist->entries)
            result.push_back(QVariantMap{{"id", QString::number(entry.id)},
                                         {"title", pathText(entry.source_path.filename())},
                                         {"path", pathText(entry.source_path)}});
    return result;
}
bool PlaylistsBinding::canAddFiles() const { return impl_->picker && impl_->selected != 0 && impl_->editable(); }
bool PlaylistsBinding::canReplaceFile() const { return impl_->replaceablePlaylist() != nullptr; }
bool PlaylistsBinding::canRetrySave() const { return impl_->allowed() && impl_->pending && !busy(); }
bool PlaylistsBinding::hasPendingChange() const { return impl_->pending.has_value(); }
bool PlaylistsBinding::selectPlaylist(const QString& value) {
    const auto id = identifier(value);
    if (!impl_->catalogEditable() || !id || !impl_->library.find(*id))
        return false;
    const bool changed_selection = impl_->selected != *id;
    impl_->selected = *id;
    const QPointer<PlaylistsBinding> alive(this);
    if (changed_selection)
        impl_->cancelFileSelection();
    if (alive)
        emit changed();
    return true;
}
bool PlaylistsBinding::createPlaylist(const QString& name) {
    if (!impl_->editable())
        return false;
    auto candidate = impl_->library;
    const auto created = candidate.create(nameText(name));
    if (!created)
        return impl_->invalid();
    impl_->pending = std::move(candidate);
    impl_->select_after_save = *created;
    return impl_->save();
}
bool PlaylistsBinding::renamePlaylist(const QString& value, const QString& name) {
    const auto id = identifier(value);
    return id && impl_->mutate([&](auto& candidate) { return candidate.rename(*id, nameText(name)); }, *id);
}
bool PlaylistsBinding::deletePlaylist(const QString& value) {
    const auto id = identifier(value);
    return id && impl_->mutate([&](auto& candidate) { return candidate.remove(*id); });
}
bool PlaylistsBinding::saveQueueAsPlaylist(const QString& name) {
    if (!impl_->editable())
        return false;
    const auto alive = QPointer<PlaylistsBinding>(this);
    auto paths = impl_->readQueue();
    if (!alive)
        return false;
    if (!impl_->editable())
        return false;
    if (!paths) {
        impl_->notice = tr("Não foi possível copiar a fila. Tente novamente.");
        emit changed();
        return false;
    }
    auto candidate = impl_->library;
    const auto created = candidate.create(nameText(name), std::move(*paths));
    if (!created)
        return impl_->invalid();
    impl_->pending = std::move(candidate);
    impl_->select_after_save = *created;
    return impl_->save();
}
bool PlaylistsBinding::addQueueToPlaylist(const QString& value) {
    const auto id = identifier(value);
    if (!id || !impl_->editable())
        return false;
    const auto alive = QPointer<PlaylistsBinding>(this);
    auto paths = impl_->readQueue();
    return alive && paths &&
           impl_->mutate([&](auto& candidate) { return candidate.append(*id, std::move(*paths)); }, *id);
}
bool PlaylistsBinding::requestAddFiles(const QString& value) {
    const auto id = identifier(value);
    if (!id || *id != impl_->selected || !canAddFiles())
        return false;

    const auto generation = ++impl_->file_selection_generation;
    impl_->file_selection = FileSelection{generation, *id};
    const QPointer<PlaylistsBinding> alive(this);
    bool accepted = false;
    try {
        accepted = impl_->picker->pick(impl_->selectionCompletion(generation, &Impl::finishFileSelection));
    } catch (...) {
    }
    if (!alive || !impl_->isCurrentFileSelection(generation))
        return false;
    if (!accepted) {
        impl_->finishFileSelection(generation, {.status = AudioFilesSelectionStatus::Failed});
        return false;
    }
    emit changed();
    return true;
}
bool PlaylistsBinding::requestReplaceFile(const QString& playlist_value, const QString& entry_value) {
    const auto playlist_id = identifier(playlist_value), entry_id = identifier(entry_value);
    const auto* playlist = impl_->replaceablePlaylist();
    if (!playlist || !playlist_id || *playlist_id != playlist->id || !entry_id)
        return false;
    const auto entry = std::ranges::find(playlist->entries, *entry_id, &PlaylistEntry::id);
    if (entry == playlist->entries.end())
        return false;

    const auto original = entry->source_path;
    const auto generation = ++impl_->file_selection_generation;
    impl_->file_selection = FileSelection{generation, *playlist_id, *entry_id};
    const QPointer<PlaylistsBinding> alive(this);
    bool accepted = false;
    try {
        accepted =
            impl_->locator->locate(original, impl_->selectionCompletion(generation, &Impl::finishFileReplacement));
    } catch (...) {
    }
    if (!alive || !impl_->isCurrentFileSelection(generation))
        return false;
    if (!accepted) {
        impl_->finishFileReplacement(generation, {.status = AudioFileLocationStatus::Failed});
        return false;
    }
    emit changed();
    return true;
}
bool PlaylistsBinding::removeEntry(const QString& playlist, const QString& entry) {
    const auto id = identifier(playlist), entry_id = identifier(entry);
    return id && entry_id && impl_->mutate([&](auto& candidate) { return candidate.removeEntry(*id, *entry_id); }, *id);
}
bool PlaylistsBinding::moveEntry(const QString& playlist, const QString& entry, const int destination) {
    const auto id = identifier(playlist), entry_id = identifier(entry);
    return destination >= 0 && id && entry_id &&
           impl_->mutate(
               [&](auto& candidate) {
                   return candidate.moveEntry(*id, *entry_id, static_cast<std::size_t>(destination));
               },
               *id);
}
bool PlaylistsBinding::enqueuePlaylist(const QString& value) {
    const auto id = identifier(value);
    if (!id || !impl_->editable() || !impl_->appender)
        return false;
    const auto* playlist = impl_->library.find(*id);
    if (!playlist || playlist->entries.empty())
        return false;
    Paths paths;
    for (const auto& entry : playlist->entries)
        paths.push_back(entry.source_path);
    const auto alive = QPointer<PlaylistsBinding>(this);
    impl_->callback_active = true;
    bool accepted{};
    try {
        accepted = impl_->appender(std::move(paths));
    } catch (...) {
    }
    if (!alive)
        return accepted;
    impl_->callback_active = false;
    impl_->notice = accepted ? tr("Faixas adicionadas à fila. Escolha uma faixa para tocar.")
                             : tr("Não foi possível adicionar as faixas. Aguarde a operação atual e tente novamente.");
    emit changed();
    return accepted;
}
bool PlaylistsBinding::retrySave() { return impl_->save(); }
void PlaylistsBinding::discardPending() {
    if (impl_->pending)
        static_cast<void>(impl_->load());
}
bool PlaylistsBinding::discardUnsavedChange() {
    if (!impl_->allowed() || busy() || !impl_->pending)
        return false;
    impl_->pending.reset();
    // An unconfirmed write can have reached disk. Reload before using the cached catalog again.
    impl_->ready = false;
    impl_->notice = tr("Edição descartada. Recarregue as playlists para voltar a editar.");
    const QPointer<PlaylistsBinding> alive(this);
    emit unsavedChangeDiscarded();
    if (alive)
        emit changed();
    return true;
}
bool PlaylistsBinding::retryLoad() { return !impl_->pending && impl_->load(); }
bool PlaylistsBinding::prepareExit() {
    if (!impl_->exiting) {
        impl_->exiting = true;
        const QPointer<PlaylistsBinding> alive(this);
        impl_->cancelFileSelection();
        if (alive)
            emit changed();
        return false;
    }
    return !impl_->busy && !impl_->pending;
}
void PlaylistsBinding::resume() {
    if (impl_->exiting && !impl_->stopping) {
        impl_->exiting = false;
        emit changed();
    }
}
void PlaylistsBinding::beginShutdown() {
    impl_->stopping = true;
    const QPointer<PlaylistsBinding> alive(this);
    impl_->cancelFileSelection();
    if (alive)
        emit changed();
}
} // namespace music_player::m0
