#include "m0/ports/LibraryStore.hpp"
#include "presentation/LibraryBinding.hpp"
#include "presentation/LibraryListModel.hpp"

#include <QSignalSpy>
#include <QTest>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

using namespace music_player::m0;

namespace {
using Path = std::filesystem::path;
Path track(int index) { return Path{"C:/Music/track-" + std::to_string(index) + ".mp3"}; }

// Thread-safe fake: the binding calls it from a worker thread.
class FakeLibraryStore final : public LibraryStore {
  public:
    LibraryLoadResult load() override {
        std::lock_guard lock(mutex_);
        ++loads_;
        return loaded_;
    }
    LibrarySaveStatus save(const LibrarySnapshot& snapshot) override {
        std::unique_lock lock(mutex_);
        changed_.wait(lock, [this] { return !hold_saves_; });
        saved_.push_back(snapshot);
        return next_save_;
    }
    void setLoaded(LibraryLoadResult result) {
        std::lock_guard lock(mutex_);
        loaded_ = std::move(result);
    }
    void setNextSave(LibrarySaveStatus status) {
        std::lock_guard lock(mutex_);
        next_save_ = status;
    }
    void holdSaves(bool hold) {
        {
            std::lock_guard lock(mutex_);
            hold_saves_ = hold;
        }
        changed_.notify_all();
    }
    std::vector<LibrarySnapshot> saved() const {
        std::lock_guard lock(mutex_);
        return saved_;
    }

  private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    LibraryLoadResult loaded_{};
    LibrarySaveStatus next_save_{LibrarySaveStatus::Saved};
    std::vector<LibrarySnapshot> saved_;
    bool hold_saves_{};
    int loads_{};
};

struct Harness final {
    std::shared_ptr<FakeLibraryStore> store = std::make_shared<FakeLibraryStore>();
    std::vector<LibraryBinding::Paths> enqueued;
    LibraryBinding binding{store, [this](LibraryBinding::Paths paths) {
                               enqueued.push_back(std::move(paths));
                               return true;
                           }};
    void startAndWait() {
        QVERIFY(binding.start());
        QTRY_VERIFY(!binding.busy());
    }
};
} // namespace

class LibraryBindingTests final : public QObject {
    Q_OBJECT
  private slots:
    void admittedFilesCountOnlyAfterTheStoreConfirms() {
        Harness h;
        h.startAndWait();
        QVERIFY(h.binding.ready());
        QCOMPARE(h.binding.count(), 0);
        h.binding.admit({track(1), track(2)});
        QCOMPARE(h.binding.count(), 0); // Not saved yet.
        QVERIFY(h.binding.hasPendingChange());
        QTRY_COMPARE(h.binding.count(), 2);
        QVERIFY(!h.binding.hasPendingChange());
        QCOMPARE(h.store->saved().back().records.size(), std::size_t{2});
    }
    void admissionsDuringASaveAreSavedNext() {
        Harness h;
        h.startAndWait();
        h.store->holdSaves(true);
        h.binding.admit({track(1)});
        QTRY_VERIFY(h.binding.busy());
        h.binding.admit({track(2)});
        h.store->holdSaves(false);
        QTRY_COMPARE(h.binding.count(), 2);
        QTRY_VERIFY(!h.binding.busy());
        QCOMPARE(h.store->saved().size(), std::size_t{2});
        QCOMPARE(h.store->saved().back().records.size(), std::size_t{2});
    }
    void aFailedSaveKeepsTheChangePendingUntilRetried() {
        Harness h;
        h.startAndWait();
        h.store->setNextSave(LibrarySaveStatus::Failed);
        h.binding.admit({track(1)});
        QTRY_VERIFY(!h.binding.busy());
        QCOMPARE(h.binding.count(), 0);
        QVERIFY(h.binding.hasPendingChange());
        QVERIFY(h.binding.canRetrySave());
        h.store->setNextSave(LibrarySaveStatus::Saved);
        QVERIFY(h.binding.retrySave());
        QTRY_COMPARE(h.binding.count(), 1);
        QVERIFY(!h.binding.hasPendingChange());
    }
    void readmittingAKnownPathDoesNotWrite() {
        Harness h;
        h.startAndWait();
        h.binding.admit({track(1)});
        QTRY_COMPARE(h.binding.count(), 1);
        h.binding.admit({track(1)});
        QVERIFY(!h.binding.hasPendingChange());
        QTest::qWait(50);
        QCOMPARE(h.store->saved().size(), std::size_t{1});
    }
    void seedingHappensOnlyOnFirstUse() {
        Harness first;
        first.startAndWait(); // Missing document.
        first.binding.seed({track(1), track(2)});
        QTRY_COMPARE(first.binding.count(), 2);

        Harness later;
        later.store->setLoaded(
            {.snapshot = {{{1, track(1), "", "", "", std::nullopt, true}}, 5}, .status = LibraryLoadStatus::Loaded});
        later.startAndWait();
        QCOMPARE(later.binding.count(), 1);
        later.binding.seed({track(2), track(3)}); // A removed track must not come back.
        QTest::qWait(50);
        QCOMPARE(later.binding.count(), 1);
        QVERIFY(later.store->saved().empty());
        later.binding.admit({track(2)}); // Explicit ingress still works.
        QTRY_COMPARE(later.binding.count(), 2);
    }
    void admissionsBeforeLoadingFinishesAreApplied() {
        Harness h;
        QVERIFY(h.binding.start());
        h.binding.admit({track(1)});
        h.binding.seed({track(2)});
        QTRY_COMPARE(h.binding.count(), 2);
    }
    void anUnreadableLibraryIsNeverOverwritten_data() {
        QTest::addColumn<int>("status");
        QTest::newRow("future version") << int(LibraryLoadStatus::FutureVersion);
        QTest::newRow("invalid") << int(LibraryLoadStatus::Invalid);
        QTest::newRow("unavailable") << int(LibraryLoadStatus::Unavailable);
    }
    void anUnreadableLibraryIsNeverOverwritten() {
        QFETCH(int, status);
        Harness h;
        h.store->setLoaded({.status = static_cast<LibraryLoadStatus>(status)});
        h.startAndWait();
        QVERIFY(!h.binding.ready());
        QVERIFY(!h.binding.statusText().isEmpty());
        h.binding.admit({track(1)});
        QTest::qWait(50);
        QVERIFY(h.store->saved().empty());
        QCOMPARE(h.binding.count(), 0);
    }
    void aDownloadJobIsAdmittedOnceEvenWhenReplayed() {
        Harness h;
        QVERIFY(h.binding.start());
        h.binding.admitDownload(track(1), "downloads-1"); // Before loading finishes.
        QTRY_COMPARE(h.binding.count(), 1);
        QCOMPARE(h.store->saved().back().download_jobs, std::vector<std::string>{"downloads-1"});
        const auto writes = h.store->saved().size();
        h.binding.admitDownload(track(1), "downloads-1");
        h.binding.admitDownload(track(2), "downloads-1");
        QVERIFY(!h.binding.hasPendingChange());
        QTest::qWait(50);
        QCOMPARE(h.store->saved().size(), writes);
        QCOMPARE(h.binding.count(), 1);
    }
    void theModelListsConfirmedRecordsWithReadableFallbacks() {
        Harness h;
        h.store->setLoaded({.snapshot = {{{3, std::filesystem::path(L"C:/Música/Canção do Mar.mp3"), "Canção do Mar",
                                           "Lia Campos", "Depois da Chuva", 217'000, true},
                                          {4, std::filesystem::path(L"D:/sem tags/faixa sem nome.flac"), "", "", "",
                                           std::nullopt, false}},
                                         5},
                            .status = LibraryLoadStatus::Loaded});
        h.startAndWait();
        auto* model = qobject_cast<LibraryListModel*>(h.binding.model());
        QVERIFY(model);
        QCOMPARE(model->rowCount(), 2);
        const auto row = [&](int index, int role) { return model->data(model->index(index, 0), role); };
        QCOMPARE(row(0, LibraryListModel::TitleRole).toString(), QStringLiteral("Canção do Mar"));
        QCOMPARE(row(0, LibraryListModel::ArtistRole).toString(), QStringLiteral("Lia Campos"));
        QCOMPARE(row(0, LibraryListModel::AlbumRole).toString(), QStringLiteral("Depois da Chuva"));
        QCOMPARE(row(0, LibraryListModel::DurationTextRole).toString(), QStringLiteral("3:37"));
        QCOMPARE(row(0, LibraryListModel::RecordIdRole).toString(), QStringLiteral("3"));
        QVERIFY(row(0, LibraryListModel::AvailableRole).toBool());
        // Without tags the file name stands in for the title.
        QCOMPARE(row(1, LibraryListModel::TitleRole).toString(), QStringLiteral("faixa sem nome"));
        QVERIFY(row(1, LibraryListModel::ArtistRole).toString().isEmpty());
        QVERIFY(row(1, LibraryListModel::DurationTextRole).toString().isEmpty());
        QVERIFY(!row(1, LibraryListModel::AvailableRole).toBool());
        QCOMPARE(h.binding.visibleCount(), 2);
    }
    void filteringIgnoresAccentsAndCaseAcrossTitleArtistAlbumAndFile_data() {
        QTest::addColumn<QString>("query");
        QTest::addColumn<int>("matches");
        QTest::newRow("accentless title") << QStringLiteral("cancao") << 1;
        QTest::newRow("upper artist") << QStringLiteral("LIA") << 1;
        QTest::newRow("album words") << QStringLiteral("depois chuva") << 1;
        QTest::newRow("file name") << QStringLiteral("sem nome") << 1;
        QTest::newRow("surrounding spaces") << QStringLiteral("  mar  ") << 1;
        QTest::newRow("no match") << QStringLiteral("rock") << 0;
        QTest::newRow("empty shows all") << QString{} << 2;
    }
    void filteringIgnoresAccentsAndCaseAcrossTitleArtistAlbumAndFile() {
        QFETCH(QString, query);
        QFETCH(int, matches);
        Harness h;
        h.store->setLoaded(
            {.snapshot = {{{3, std::filesystem::path(L"C:/Música/Canção do Mar.mp3"), "Canção do Mar", "Lia Campos",
                            "Depois da Chuva", 217'000, true},
                           {4, std::filesystem::path(L"D:/faixa sem nome.flac"), "", "", "", std::nullopt, true}},
                          5},
             .status = LibraryLoadStatus::Loaded});
        h.startAndWait();
        h.binding.setFilterText(query);
        QCOMPARE(h.binding.visibleCount(), matches);
        QCOMPARE(h.binding.model()->property("count").toInt(), matches);
        QCOMPARE(h.binding.count(), 2);
    }
    void enqueueingSendsTheFileToTheQueueWithoutChangingTheLibrary() {
        Harness h;
        h.startAndWait();
        h.binding.admit({track(1)});
        QTRY_COMPARE(h.binding.count(), 1);
        const auto id = QString::number(h.binding.confirmedSnapshot().records.front().id);
        QVERIFY(h.binding.enqueue(id));
        QCOMPARE(h.enqueued.size(), std::size_t{1});
        QCOMPARE(h.enqueued.front(), LibraryBinding::Paths{track(1)});
        QVERIFY(!h.binding.enqueue(QStringLiteral("999")));
        QVERIFY(!h.binding.enqueue(QStringLiteral("not-a-number")));
        QCOMPARE(h.binding.count(), 1);
    }
    void removingDropsOnlyTheRecordOnceSaved() {
        Harness h;
        h.startAndWait();
        h.binding.admit({track(1), track(2)});
        QTRY_COMPARE(h.binding.count(), 2);
        const auto id = QString::number(h.binding.confirmedSnapshot().records.front().id);
        QVERIFY(h.binding.remove(id));
        QCOMPARE(h.binding.count(), 2); // Not saved yet.
        QTRY_COMPARE(h.binding.count(), 1);
        QCOMPARE(h.store->saved().back().records.front().source_path, track(2));
        QVERIFY(!h.binding.remove(id));
        QVERIFY(h.enqueued.empty());
    }
    void exitWaitsOnlyForAnActiveSave() {
        Harness h;
        h.startAndWait();
        QVERIFY(!h.binding.savingForExit());
        h.store->holdSaves(true);
        h.binding.admit({track(1)});
        QTRY_VERIFY(h.binding.savingForExit());
        h.store->holdSaves(false);
        QTRY_VERIFY(!h.binding.savingForExit());
        h.store->setNextSave(LibrarySaveStatus::Failed);
        h.binding.admit({track(2)});
        QTRY_VERIFY(!h.binding.busy());
        // A failed catalog save never holds the player's exit.
        QVERIFY(!h.binding.savingForExit());
    }
};

QTEST_GUILESS_MAIN(LibraryBindingTests)
#include "LibraryBindingTests.moc"
