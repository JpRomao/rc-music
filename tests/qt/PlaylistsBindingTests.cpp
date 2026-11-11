#include "m0/ports/AudioFilesPicker.hpp"
#include "m0/ports/PlaylistStore.hpp"
#include "presentation/PlaylistsBinding.hpp"

#include <QSignalSpy>
#include <QTest>
#include <QThread>

#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

using namespace music_player::m0;
namespace {
class Store final : public PlaylistStore {
  public:
    std::mutex mutex;
    PlaylistLoadResult disk{};
    PlaylistSaveStatus outcome{PlaylistSaveStatus::Saved};
    std::atomic<int> loads{}, saves{};
    std::atomic<bool> release{true}, off_thread{false};
    QThread* ui_thread{QThread::currentThread()};
    PlaylistLoadResult load() override {
        ++loads;
        off_thread = QThread::currentThread() != ui_thread;
        std::lock_guard lock(mutex);
        return disk;
    }
    PlaylistSaveStatus save(const PlaylistLibrarySnapshot& snapshot) override {
        ++saves;
        off_thread = QThread::currentThread() != ui_thread;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!release && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::lock_guard lock(mutex);
        if (outcome == PlaylistSaveStatus::Saved)
            disk = {snapshot, PlaylistLoadStatus::Loaded};
        return outcome;
    }
    void setOutcome(const PlaylistSaveStatus next) {
        std::lock_guard lock(mutex);
        outcome = next;
    }
};
class Picker final : public AudioFilesPicker {
  public:
    AudioFilesSelectionCompletion pending;
    bool accepts{true};
    int cancellations{};

    bool pick(AudioFilesSelectionCompletion completion) override {
        if (!accepts || pending)
            return false;
        pending = std::move(completion);
        return true;
    }
    void cancel() noexcept override {
        ++cancellations;
        pending = {};
    }
    AudioFilesSelectionCompletion takeCompletion() { return std::exchange(pending, {}); }
};
std::filesystem::path path(const char* name) {
#ifdef _WIN32
    return std::filesystem::path("C:/Music") / name;
#else
    return std::filesystem::path("/Music") / name;
#endif
}
QString firstId(const PlaylistsBinding& binding) { return binding.playlists().front().toMap().value("id").toString(); }
void load(PlaylistsBinding& binding) {
    QVERIFY(binding.retryLoad());
    QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
    QVERIFY(binding.ready());
}
} // namespace
class PlaylistsBindingTests final : public QObject {
    Q_OBJECT
  private slots:
    void constructionIsPassiveAndLoadingRunsOutsideTheUiThread() {
        auto store = std::make_shared<Store>();
        PlaylistsBinding binding(store, [] { return PlaylistsBinding::Paths{}; }, [](auto) { return true; });
        QCOMPARE(store->loads.load(), 0);
        QVERIFY(!binding.ready());
        QVERIFY(binding.interactionAllowed());
        load(binding);
        QVERIFY(store->off_thread);
        QVERIFY(binding.playlists().isEmpty());
        QVERIFY(!binding.statusText().isEmpty());
    }
    void aPlaylistIsPublishedOnlyAfterSavedAndEditingDoesNotTouchTheQueue() {
        auto store = std::make_shared<Store>();
        int appends{};
        PlaylistsBinding binding(
            store, [] { return PlaylistsBinding::Paths{}; },
            [&](auto) {
                ++appends;
                return true;
            });
        load(binding);
        store->release = false;
        QVERIFY(binding.createPlaylist("Favoritas 日本"));
        QVERIFY(binding.busy());
        QVERIFY(binding.hasPendingChange());
        QVERIFY(binding.playlists().isEmpty());
        QVERIFY(!binding.createPlaylist("Concurrent"));
        store->release = true;
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        QCOMPARE(binding.playlists().size(), 1);
        QCOMPARE(binding.playlists().front().toMap().value("name").toString(), "Favoritas 日本");
        QVERIFY(!binding.hasPendingChange());
        const auto id = firstId(binding);
        QVERIFY(binding.renamePlaylist(id, "Outro nome"));
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        QCOMPARE(binding.playlists().front().toMap().value("id").toString(), id);
        QCOMPARE(appends, 0);
    }
    void creatingAnEmptyPlaylistNeverReadsOrCopiesTheCurrentQueue() {
        auto store = std::make_shared<Store>();
        int reads{};
        PlaylistsBinding binding(
            store,
            [&] {
                ++reads;
                return PlaylistsBinding::Paths{path("current.mp3")};
            },
            [](auto) { return true; });
        load(binding);
        QVERIFY(binding.createPlaylist("Empty"));
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        QCOMPARE(reads, 0);
        QCOMPARE(binding.playlists().front().toMap().value("count").toInt(), 0);
    }
    void selectedFilesAreAppendedOnlyAfterSavedWithoutTouchingPlayback() {
        auto store = std::make_shared<Store>();
        Picker picker;
        int reads{}, appends{};
        PlaylistsBinding binding(
            store,
            [&] {
                ++reads;
                return PlaylistsBinding::Paths{path("currently-playing.mp3")};
            },
            [&](auto) {
                ++appends;
                return true;
            },
            nullptr, &picker);
        load(binding);
        QVERIFY(!binding.canAddFiles());
        QVERIFY(binding.createPlaylist("Empty"));
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        const auto id = binding.selectedPlaylistId();
        QVERIFY(binding.canAddFiles());
        QVERIFY(binding.requestAddFiles(id));
        QVERIFY(binding.busy());
        QVERIFY(binding.interactionAllowed());
        QVERIFY(!binding.canAddFiles());
        QVERIFY(!binding.requestAddFiles(id));
        QVERIFY(!binding.deletePlaylist(id));
        QVERIFY(!binding.retryLoad());

        store->release = false;
        const PlaylistsBinding::Paths expected{path("a.mp3"), path("b.flac"), path("a.mp3")};
        picker.takeCompletion()({AudioFilesSelectionStatus::Selected, expected});
        QTRY_COMPARE_WITH_TIMEOUT(store->saves.load(), 2, 1000);
        QVERIFY(binding.entries().isEmpty());
        store->release = true;
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        QCOMPARE(binding.entries().size(), 3);
        QCOMPARE(binding.selectedPlaylistId(), id);
        const auto saved = store->load().snapshot.playlists.front().entries;
        QVERIFY(saved[0].source_path == expected[0]);
        QVERIFY(saved[1].source_path == expected[1]);
        QVERIFY(saved[2].source_path == expected[2]);
        QVERIFY(saved[0].id != saved[2].id);
        QCOMPARE(reads, 0);
        QCOMPARE(appends, 0);
    }
    void cancellingOrInvalidatingASelectionCannotRetargetIt() {
        auto store = std::make_shared<Store>();
        Picker picker;
        PlaylistsBinding binding(store, {}, {}, nullptr, &picker);
        load(binding);
        QVERIFY(binding.createPlaylist("First"));
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        const auto first = binding.selectedPlaylistId();
        QVERIFY(binding.createPlaylist("Second"));
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        const auto second = binding.selectedPlaylistId();
        QVERIFY(!binding.requestAddFiles(first));

        QVERIFY(binding.requestAddFiles(second));
        auto stale = picker.takeCompletion();
        QVERIFY(binding.selectPlaylist(first));
        QCOMPARE(picker.cancellations, 1);
        QVERIFY(binding.requestAddFiles(first));
        stale({AudioFilesSelectionStatus::Selected, {path("late.mp3")}});
        QCoreApplication::processEvents();
        QVERIFY(binding.busy());
        picker.takeCompletion()({AudioFilesSelectionStatus::Cancelled, {path("ignored.mp3")}});
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);

        picker.accepts = false;
        QVERIFY(!binding.requestAddFiles(first));
        QVERIFY(!binding.busy());
        QVERIFY(binding.statusText().contains(QStringLiteral("selecionar")));
        picker.accepts = true;
        QVERIFY(binding.requestAddFiles(first));
        auto after_exit = picker.takeCompletion();
        QVERIFY(!binding.prepareExit());
        QCOMPARE(picker.cancellations, 2);
        QVERIFY(binding.prepareExit());
        binding.resume();
        after_exit({AudioFilesSelectionStatus::Selected, {path("late.mp3")}});
        QCoreApplication::processEvents();
        QCOMPARE(store->saves.load(), 2);
        QVERIFY(binding.entries().isEmpty());
        QCOMPARE(binding.selectedPlaylistId(), first);
        QVERIFY(binding.canAddFiles());
    }
    void savingTheQueueKeepsDuplicatePathsAndStableEntriesUntilExplicitEnqueue() {
        auto store = std::make_shared<Store>();
        PlaylistsBinding::Paths queue{path("a.mp3"), path("b.flac"), path("a.mp3")}, appended;
        int calls{};
        PlaylistsBinding binding(
            store, [&] { return queue; },
            [&](auto paths) {
                ++calls;
                appended = std::move(paths);
                return true;
            });
        load(binding);
        QVERIFY(binding.saveQueueAsPlaylist("Minha fila"));
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        const auto id = firstId(binding);
        QVERIFY(binding.selectPlaylist(id));
        auto entries = binding.entries();
        QCOMPARE(entries.size(), 3);
        const auto first = entries[0].toMap().value("id").toString();
        const auto duplicate = entries[2].toMap().value("id").toString();
        QVERIFY(first != duplicate);
        QVERIFY(binding.moveEntry(id, duplicate, 0));
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        QCOMPARE(binding.entries()[0].toMap().value("id").toString(), duplicate);
        QCOMPARE(calls, 0);
        QVERIFY(binding.enqueuePlaylist(id));
        QCOMPARE(calls, 1);
        const PlaylistsBinding::Paths expected{path("a.mp3"), path("a.mp3"), path("b.flac")};
        QVERIFY(appended == expected);
        QVERIFY(binding.removeEntry(id, first));
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        QCOMPARE(binding.entries().size(), 2);
        QCOMPARE(calls, 1);
    }
    void failedSaveRetainsTheCandidateForExplicitRetryAndBlocksExit() {
        auto store = std::make_shared<Store>();
        PlaylistsBinding binding(store, [] { return PlaylistsBinding::Paths{}; }, [](auto) { return true; });
        load(binding);
        store->setOutcome(PlaylistSaveStatus::Failed);
        QVERIFY(binding.createPlaylist("Pending"));
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        QVERIFY(binding.playlists().isEmpty());
        QVERIFY(binding.hasPendingChange());
        QVERIFY(binding.canRetrySave());
        QVERIFY(!binding.createPlaylist("Would erase pending"));
        QVERIFY(!binding.prepareExit());
        QVERIFY(!binding.prepareExit());
        binding.resume();
        store->setOutcome(PlaylistSaveStatus::Saved);
        QVERIFY(binding.retrySave());
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        QCOMPARE(binding.playlists().size(), 1);
        QVERIFY(!binding.hasPendingChange());
        QVERIFY(!binding.prepareExit());
        QVERIFY(binding.prepareExit());
    }
    void reloadResolvesAnUncertainWriteWithoutClaimingRollback() {
        auto store = std::make_shared<Store>();
        PlaylistsBinding binding(store, [] { return PlaylistsBinding::Paths{}; }, [](auto) { return true; });
        load(binding);
        store->setOutcome(PlaylistSaveStatus::Failed);
        QVERIFY(binding.createPlaylist("Unconfirmed"));
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        {
            std::lock_guard lock(store->mutex);
            store->disk = {{{NamedPlaylist{7, "Actual disk", {}}}}, PlaylistLoadStatus::Loaded};
        }
        binding.discardPending();
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        QCOMPARE(binding.playlists().size(), 1);
        QCOMPARE(binding.playlists().front().toMap().value("name").toString(), "Actual disk");
        QVERIFY(!binding.hasPendingChange());
    }
    void invalidFutureOrUnavailableDocumentsCannotBeOverwritten_data() {
        QTest::addColumn<PlaylistLoadStatus>("status");
        QTest::newRow("invalid") << PlaylistLoadStatus::Invalid;
        QTest::newRow("future") << PlaylistLoadStatus::FutureVersion;
        QTest::newRow("unavailable") << PlaylistLoadStatus::Unavailable;
    }
    void invalidFutureOrUnavailableDocumentsCannotBeOverwritten() {
        QFETCH(PlaylistLoadStatus, status);
        auto store = std::make_shared<Store>();
        store->disk.status = status;
        PlaylistsBinding binding(store, [] { return PlaylistsBinding::Paths{}; }, [](auto) { return true; });
        QVERIFY(binding.retryLoad());
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        QVERIFY(!binding.ready());
        QVERIFY(!binding.createPlaylist("New"));
        QCOMPARE(store->saves.load(), 0);
        QVERIFY(!binding.statusText().isEmpty());
    }
    void deletingAListCannotRetargetAnOldIdOrDeleteItsAudio() {
        auto store = std::make_shared<Store>();
        PlaylistsBinding binding(
            store, [] { return PlaylistsBinding::Paths{path("personal.mp3")}; }, [](auto) { return true; });
        load(binding);
        QVERIFY(binding.saveQueueAsPlaylist("First"));
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        const auto id = firstId(binding);
        QVERIFY(binding.deletePlaylist(id));
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        QVERIFY(binding.createPlaylist("Second"));
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        QVERIFY(firstId(binding) != id);
        QVERIFY(!binding.deletePlaylist(id));
        QCOMPARE(binding.playlists().size(), 1);
    }
    void shutdownClosesAdmissionAndCannotBeResumed() {
        auto store = std::make_shared<Store>();
        PlaylistsBinding binding(store, [] { return PlaylistsBinding::Paths{}; }, [](auto) { return true; });
        binding.beginShutdown();
        binding.resume();
        QVERIFY(!binding.interactionAllowed());
        QVERIFY(!binding.retryLoad());
        QVERIFY(!binding.createPlaylist("Late"));
        QCOMPARE(store->loads.load(), 0);
    }
    void failureToReadTheQueueCannotPersistAnInventedEmptyPlaylist() {
        auto store = std::make_shared<Store>();
        PlaylistsBinding binding(
            store, []() -> PlaylistsBinding::Paths { throw std::runtime_error("reader failed"); },
            [](auto) { return true; });
        load(binding);
        QVERIFY(!binding.saveQueueAsPlaylist("Should not exist"));
        QVERIFY(!binding.hasPendingChange());
        QCOMPARE(store->saves.load(), 0);
        QVERIFY(binding.playlists().isEmpty());
    }
    void exitDuringTheQueueCallbackCannotCreateAnUnsaveablePendingChange() {
        auto store = std::make_shared<Store>();
        PlaylistsBinding* active{};
        PlaylistsBinding binding(
            store,
            [&] {
                static_cast<void>(active->prepareExit());
                return PlaylistsBinding::Paths{path("current.mp3")};
            },
            [](auto) { return true; });
        active = &binding;
        load(binding);
        QVERIFY(!binding.saveQueueAsPlaylist("Late"));
        QVERIFY(!binding.hasPendingChange());
        QCOMPARE(store->saves.load(), 0);
        QVERIFY(binding.prepareExit());
    }
};
QTEST_GUILESS_MAIN(PlaylistsBindingTests)
#include "PlaylistsBindingTests.moc"
