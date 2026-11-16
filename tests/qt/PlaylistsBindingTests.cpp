#include "m0/ports/AudioFileLocator.hpp"
#include "m0/ports/AudioFilesPicker.hpp"
#include "m0/ports/PlaylistStore.hpp"
#include "presentation/PlaylistsBinding.hpp"

#include <QSignalSpy>
#include <QStringList>
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
class Locator final : public AudioFileLocator {
  public:
    AudioFileLocationCompletion pending;
    std::filesystem::path original;
    bool accepts{true};
    int cancellations{};

    bool locate(const std::filesystem::path& source, AudioFileLocationCompletion completion) override {
        if (!accepts || pending)
            return false;
        original = source;
        pending = std::move(completion);
        return true;
    }
    void cancel() noexcept override {
        ++cancellations;
        pending = {};
    }
    AudioFileLocationCompletion takeCompletion() { return std::exchange(pending, {}); }
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
    void confirmedEntryPathsSeedTheLibraryOnlyOnceLoaded() {
        auto store = std::make_shared<Store>();
        const std::filesystem::path a{L"C:/Música/a.mp3"}, b{L"D:/b.flac"};
        store->disk = {{{{3, "Primeira", {{1, a}, {2, b}}}, {4, "Segunda", {{5, a}}}}}, PlaylistLoadStatus::Loaded};
        PlaylistsBinding binding(store, [] { return PlaylistsBinding::Paths{}; }, [](auto) { return true; });
        QVERIFY(binding.entryPaths().empty()); // Not loaded yet.
        load(binding);
        QCOMPARE(binding.entryPaths(), (PlaylistsBinding::Paths{a, b, a}));
    }
    void libraryFilesAreAppendedOnlyAfterSavedKeepingTheSelection() {
        auto store = std::make_shared<Store>();
        const std::filesystem::path a{L"C:/Música/a.mp3"}, b{L"D:/b.flac"};
        store->disk = {{{{3, "Primeira", {{1, a}}}, {4, "Segunda", {}}}}, PlaylistLoadStatus::Loaded};
        PlaylistsBinding binding(store, [] { return PlaylistsBinding::Paths{}; }, [](auto) { return true; });
        load(binding);
        QVERIFY(binding.selectPlaylist(QStringLiteral("3")));
        store->release = false;
        QVERIFY(binding.appendToPlaylist(QStringLiteral("4"), {b}));
        QVERIFY(binding.hasPendingChange());
        QCOMPARE(binding.entryPaths(), (PlaylistsBinding::Paths{a})); // Not saved yet.
        store->release = true;
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        QCOMPARE(binding.entryPaths(), (PlaylistsBinding::Paths{a, b}));
        QCOMPARE(binding.selectedPlaylistId(), QStringLiteral("3"));
        QVERIFY(!binding.appendToPlaylist(QStringLiteral("99"), {b}));
        QVERIFY(!binding.appendToPlaylist(QStringLiteral("4"), {}));
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
    void pageCancellationRejectsLateSelectionsButPreservesAnAcceptedSave_data() {
        QTest::addColumn<bool>("replacement");
        QTest::newRow("picker") << false;
        QTest::newRow("locator") << true;
    }
    void pageCancellationRejectsLateSelectionsButPreservesAnAcceptedSave() {
        QFETCH(bool, replacement);
        auto store = std::make_shared<Store>();
        const PlaylistLibrarySnapshot confirmed{{{7, "Confirmed", {{11, path("original.mp3")}}}}};
        store->disk = {confirmed, PlaylistLoadStatus::Loaded};
        Picker picker;
        Locator locator;
        PlaylistsBinding binding(store, {}, {}, nullptr, &picker, &locator);
        load(binding);
        QVERIFY(binding.selectPlaylist("7"));
        const auto before = binding.entries();
        QVERIFY(replacement ? binding.requestReplaceFile("7", "11") : binding.requestAddFiles("7"));
        auto stale_add = replacement ? AudioFilesSelectionCompletion{} : picker.takeCompletion();
        auto stale_location = replacement ? locator.takeCompletion() : AudioFileLocationCompletion{};
        QVERIFY2(binding.metaObject()->indexOfMethod("cancelFileSelection()") >= 0,
                 "Page ownership needs a public cancellation boundary that does not discard accepted saves");
        QVERIFY(QMetaObject::invokeMethod(&binding, "cancelFileSelection", Qt::DirectConnection));
        QVERIFY(!binding.busy());
        QCOMPARE(replacement ? locator.cancellations : picker.cancellations, 1);
        QVERIFY(replacement ? binding.requestReplaceFile("7", "11") : binding.requestAddFiles("7"));
        if (replacement)
            stale_location({AudioFileLocationStatus::Selected, path("stale.flac")});
        else
            stale_add({AudioFilesSelectionStatus::Selected, {path("stale.flac")}});
        QCoreApplication::processEvents();
        QVERIFY(binding.busy());
        QVERIFY(!binding.hasPendingChange());
        QCOMPARE(store->saves.load(), 0);
        QCOMPARE(binding.entries(), before);
        store->release = false;
        if (replacement)
            locator.takeCompletion()({AudioFileLocationStatus::Selected, path("accepted.flac")});
        else
            picker.takeCompletion()({AudioFilesSelectionStatus::Selected, {path("accepted.flac")}});
        QTRY_COMPARE_WITH_TIMEOUT(store->saves.load(), 1, 1000);
        QVERIFY(binding.hasPendingChange());
        QVERIFY(QMetaObject::invokeMethod(&binding, "cancelFileSelection", Qt::DirectConnection));
        QVERIFY(binding.busy());
        QVERIFY(binding.hasPendingChange());
        store->release = true;
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        QCOMPARE(store->saves.load(), 1);
        QCOMPARE(binding.entries().size(), replacement ? 1 : 2);
        QCOMPARE(binding.entries().back().toMap().value("title").toString(), QStringLiteral("accepted.flac"));
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
    void replacingOneSourceKeepsTheEntryAndProjectionUntilSaved() {
        auto store = std::make_shared<Store>();
        Locator locator;
        int reads{}, appends{};
        const PlaylistsBinding::Paths sources{path("old.mp3"), path("old.mp3")};
        PlaylistsBinding::Paths appended;
        PlaylistsBinding binding(
            store,
            [&] {
                ++reads;
                return sources;
            },
            [&](auto paths) {
                ++appends;
                appended = std::move(paths);
                return true;
            },
            nullptr, nullptr, &locator);
        load(binding);
        QVERIFY(!binding.canReplaceFile());
        QVERIFY(binding.saveQueueAsPlaylist("Playlist"));
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        const auto playlist_id = binding.selectedPlaylistId();
        const auto before = binding.entries();
        const auto entry_id = before[1].toMap().value("id").toString();
        QVERIFY(binding.canReplaceFile());
        QVERIFY(!binding.requestReplaceFile(playlist_id, "999"));
        QVERIFY(binding.requestReplaceFile(playlist_id, entry_id));
        QVERIFY(locator.original == sources[1]);
        QVERIFY(!binding.canReplaceFile());
        QVERIFY(!binding.requestReplaceFile(playlist_id, entry_id));
        QVERIFY(!binding.removeEntry(playlist_id, entry_id));

        store->setOutcome(PlaylistSaveStatus::Failed);
        store->release = false;
        const auto replacement = path("new.flac");
        locator.takeCompletion()({AudioFileLocationStatus::Selected, replacement});
        QVERIFY(binding.busy());
        QVERIFY(!binding.hasPendingChange());
        QTRY_COMPARE_WITH_TIMEOUT(store->saves.load(), 2, 1000);
        QCOMPARE(binding.entries(), before);
        store->release = true;
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        QCOMPARE(binding.entries(), before);
        QVERIFY(binding.canRetrySave());
        QVERIFY(!binding.canReplaceFile());

        store->setOutcome(PlaylistSaveStatus::Saved);
        QVERIFY(binding.retrySave());
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        QCOMPARE(binding.entries().size(), 2);
        QCOMPARE(binding.entries()[0], before[0]);
        QCOMPARE(binding.entries()[1].toMap().value("id").toString(), entry_id);
        QCOMPARE(binding.entries()[1].toMap().value("title").toString(), QStringLiteral("new.flac"));
        QVERIFY(store->load().snapshot.playlists.front().entries[1].source_path == replacement);
        QCOMPARE(reads, 1);
        QCOMPARE(appends, 0);
        QVERIFY(binding.enqueuePlaylist(playlist_id));
        QCOMPARE(appends, 1);
        QVERIFY(appended == (PlaylistsBinding::Paths{sources[0], replacement}));
    }
    void replaceSelectionCannotChangeTargetOrSurviveExit() {
        auto store = std::make_shared<Store>();
        Picker picker;
        Locator locator;
        PlaylistsBinding binding(
            store, [] { return PlaylistsBinding::Paths{path("original.mp3")}; }, {}, nullptr, &picker, &locator);
        load(binding);
        QVERIFY(binding.saveQueueAsPlaylist("First"));
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        const auto first = binding.selectedPlaylistId();
        const auto first_entry = binding.entries().front().toMap().value("id").toString();
        QVERIFY(binding.saveQueueAsPlaylist("Second"));
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        const auto second = binding.selectedPlaylistId();
        const auto second_entry = binding.entries().front().toMap().value("id").toString();
        QVERIFY(!binding.requestReplaceFile(first, first_entry));
        QVERIFY(!binding.requestReplaceFile(second, first_entry));

        QVERIFY(binding.requestReplaceFile(second, second_entry));
        QVERIFY(!binding.requestAddFiles(second));
        auto stale = locator.takeCompletion();
        QVERIFY(binding.selectPlaylist(first));
        QCOMPARE(locator.cancellations, 1);
        QVERIFY(binding.requestReplaceFile(first, first_entry));
        stale({AudioFileLocationStatus::Selected, path("stale.mp3")});
        QCoreApplication::processEvents();
        QVERIFY(binding.busy());
        locator.takeCompletion()({AudioFileLocationStatus::Cancelled, path("ignored.mp3")});
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);

        locator.accepts = false;
        QVERIFY(!binding.requestReplaceFile(first, first_entry));
        QVERIFY(!binding.busy());
        locator.accepts = true;
        QVERIFY(binding.requestReplaceFile(first, first_entry));
        locator.takeCompletion()({AudioFileLocationStatus::Selected, "relative.mp3"});
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        QVERIFY(binding.requestReplaceFile(first, first_entry));
        locator.takeCompletion()({AudioFileLocationStatus::Selected, path("after-exit.mp3")});
        QVERIFY(!binding.prepareExit());
        QCOMPARE(locator.cancellations, 2);
        QVERIFY(binding.prepareExit());
        binding.resume();
        QCoreApplication::processEvents();
        QCOMPARE(binding.selectedPlaylistId(), first);
        QCOMPARE(binding.entries().front().toMap().value("title").toString(), QStringLiteral("original.mp3"));
        QCOMPARE(store->saves.load(), 2);
        QVERIFY(binding.canReplaceFile());
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
    void failedReloadRetainsTheConfirmedCatalogAndOriginalEdit_data() {
        QTest::addColumn<PlaylistLoadStatus>("status");
        QTest::newRow("unavailable") << PlaylistLoadStatus::Unavailable;
        QTest::newRow("invalid") << PlaylistLoadStatus::Invalid;
        QTest::newRow("future") << PlaylistLoadStatus::FutureVersion;
    }
    void failedReloadRetainsTheConfirmedCatalogAndOriginalEdit() {
        QFETCH(PlaylistLoadStatus, status);
        const PlaylistLibrarySnapshot confirmed{
            {{7, "First", {}}, {9, "Second", {{21, path("a.mp3")}, {22, path("b.flac")}}}}};
        auto store = std::make_shared<Store>();
        store->disk = {confirmed, PlaylistLoadStatus::Loaded};
        PlaylistsBinding binding(store, [] { return PlaylistsBinding::Paths{}; }, [](auto) { return true; });
        load(binding);
        QVERIFY(binding.selectPlaylist("9"));
        const auto lists = binding.playlists();
        const auto entries = binding.entries();

        store->setOutcome(PlaylistSaveStatus::Failed);
        QVERIFY(binding.renamePlaylist("9", "Renamed 日本"));
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        QVERIFY(binding.hasPendingChange());

        {
            const std::lock_guard lock(store->mutex);
            store->disk = {{}, status};
        }
        binding.discardPending();
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        QCOMPARE(store->loads.load(), 2);
        QCOMPARE(store->saves.load(), 1);
        QVERIFY(binding.hasPendingChange());
        QVERIFY(binding.ready());
        QVERIFY(binding.canRetrySave());
        QCOMPARE(binding.selectedPlaylistId(), "9");
        QCOMPARE(binding.playlists(), lists);
        QCOMPARE(binding.entries(), entries);
        QVERIFY(!binding.prepareExit());
        QVERIFY(!binding.prepareExit());
        binding.resume();

        {
            const std::lock_guard lock(store->mutex);
            store->disk = {confirmed, PlaylistLoadStatus::Loaded};
        }
        store->setOutcome(PlaylistSaveStatus::Saved);
        QVERIFY(binding.retrySave());
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        QVERIFY(!binding.hasPendingChange());
        QCOMPARE(binding.selectedPlaylistId(), "9");
        QCOMPARE(binding.entries(), entries);
        QCOMPARE(store->saves.load(), 2);
        auto expected = confirmed;
        expected.playlists[1].name = "Renamed 日本";
        const std::lock_guard lock(store->mutex);
        QVERIFY(store->disk.snapshot == expected);
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
        QVERIFY(binding.busy());
        QVERIFY(binding.hasPendingChange());
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        QCOMPARE(binding.playlists().size(), 1);
        QCOMPARE(binding.playlists().front().toMap().value("name").toString(), "Actual disk");
        QVERIFY(!binding.hasPendingChange());
    }
    void discardingAnUnsavedEditAllowsExitWithoutReadingOrWritingTheCatalog() {
        const PlaylistLibrarySnapshot confirmed{{{7, "Confirmed", {{21, path("a.mp3")}}}}};
        auto store = std::make_shared<Store>();
        store->disk = {confirmed, PlaylistLoadStatus::Loaded};
        int queue_reads{}, queue_appends{};
        PlaylistsBinding binding(
            store,
            [&] {
                ++queue_reads;
                return PlaylistsBinding::Paths{path("playing.flac")};
            },
            [&](auto) {
                ++queue_appends;
                return true;
            });
        load(binding);
        QVERIFY(binding.selectPlaylist("7"));
        const auto lists = binding.playlists();
        const auto entries = binding.entries();
        QSignalSpy discards(&binding, &PlaylistsBinding::unsavedChangeDiscarded);
        QVERIFY(!binding.discardUnsavedChange());

        store->setOutcome(PlaylistSaveStatus::Failed);
        QVERIFY(binding.renamePlaylist("7", "Unsaved edit"));
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        QVERIFY(binding.hasPendingChange());
        {
            const std::lock_guard lock(store->mutex);
            store->disk.status = PlaylistLoadStatus::Unavailable;
        }
        binding.discardPending();
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        QVERIFY(binding.hasPendingChange());
        QVERIFY(!binding.prepareExit());
        QVERIFY(!binding.discardUnsavedChange());
        QVERIFY(binding.hasPendingChange());
        QVERIFY(discards.isEmpty());
        binding.resume();

        QStringList notifications;
        connect(&binding, &PlaylistsBinding::unsavedChangeDiscarded, &binding, [&] {
            QVERIFY(!binding.hasPendingChange());
            notifications.append("discarded");
        });
        connect(&binding, &PlaylistsBinding::changed, &binding, [&] { notifications.append("changed"); });
        QVERIFY(binding.discardUnsavedChange());
        QCOMPARE(notifications, (QStringList{"discarded", "changed"}));
        QVERIFY(!binding.hasPendingChange());
        QVERIFY(!binding.canRetrySave());
        QVERIFY(!binding.busy());
        QVERIFY(!binding.ready());
        QCOMPARE(binding.selectedPlaylistId(), "7");
        QCOMPARE(binding.playlists(), lists);
        QCOMPARE(binding.entries(), entries);
        QVERIFY(!binding.discardUnsavedChange());
        QVERIFY(!binding.retrySave());
        QVERIFY(!binding.renamePlaylist("7", "Stale edit"));
        QVERIFY(!binding.saveQueueAsPlaylist("Stale queue copy"));
        QVERIFY(!binding.enqueuePlaylist("7"));
        QCOMPARE(queue_reads, 0);
        QCOMPARE(queue_appends, 0);
        QCOMPARE(store->loads.load(), 2);
        QCOMPARE(store->saves.load(), 1);
        QVERIFY(!binding.prepareExit());
        QVERIFY(binding.prepareExit());

        binding.resume();
        auto restored = confirmed;
        restored.playlists[0].name = "Recovered from disk";
        {
            const std::lock_guard lock(store->mutex);
            store->disk = {restored, PlaylistLoadStatus::Loaded};
        }
        load(binding);
        QCOMPARE(binding.playlists().front().toMap().value("name").toString(), "Recovered from disk");
        store->setOutcome(PlaylistSaveStatus::Saved);
        QVERIFY(binding.renamePlaylist("7", "Fresh edit"));
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        QCOMPARE(binding.playlists().front().toMap().value("name").toString(), "Fresh edit");
        QCOMPARE(store->loads.load(), 3);
        QCOMPARE(store->saves.load(), 2);
        QCOMPARE(discards.size(), 1);
    }
    void discardingCannotInterruptAnActiveSave() {
        auto store = std::make_shared<Store>();
        PlaylistsBinding binding(store, [] { return PlaylistsBinding::Paths{}; }, [](auto) { return true; });
        load(binding);
        store->release = false;
        QVERIFY(binding.createPlaylist("Saving"));
        QVERIFY(binding.busy());
        QVERIFY(binding.hasPendingChange());
        QVERIFY(!binding.discardUnsavedChange());
        QVERIFY(binding.hasPendingChange());
        store->release = true;
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
        QCOMPARE(binding.playlists().front().toMap().value("name").toString(), "Saving");
        QCOMPARE(store->loads.load(), 1);
        QCOMPARE(store->saves.load(), 1);
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
