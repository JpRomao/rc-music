#include "adapters/QtJsonLibraryStore.hpp"

#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QTemporaryDir>
#include <QTest>

#include <stdexcept>

using namespace music_player::m0;

namespace {
LibrarySnapshot sample() {
    LibrarySnapshot snapshot;
    snapshot.records.push_back(
        {7, std::filesystem::path(L"C:/Música/日本/canção.mp3"), "Canção 日本", "Artista", "Álbum", 183'000, true});
    snapshot.records.push_back({9, std::filesystem::path(L"D:/Removível/song.flac"), "", "", "", std::nullopt, false});
    snapshot.next_id = 12; // Higher than every live ID: removed IDs are never reused.
    snapshot.download_jobs = {"downloads-1", "downloads-2"};
    return snapshot;
}
QByteArray read(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
void write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(bytes), bytes.size());
}
} // namespace

class QtJsonLibraryStoreTests final : public QObject {
    Q_OBJECT
  private slots:
    void missingDirectoryLoadsAsAnEmptyLibraryWithoutCreatingIt() {
        QTemporaryDir root;
        const auto directory = root.filePath(QStringLiteral("not-created"));
        QtJsonLibraryStore store(directory);
        const auto result = store.load();
        QCOMPARE(result.status, LibraryLoadStatus::Missing);
        QVERIFY(result.snapshot.records.empty());
        QVERIFY(!QDir(directory).exists());
    }
    void savedLibraryRoundTripsExactPathsMetadataAndTheNextId() {
        QTemporaryDir root;
        QtJsonLibraryStore store(root.path());
        QCOMPARE(store.save(sample()), LibrarySaveStatus::Saved);
        const auto result = store.load();
        QCOMPARE(result.status, LibraryLoadStatus::Loaded);
        QVERIFY(result.snapshot == sample());
        QVERIFY(QFile::exists(root.filePath(QStringLiteral("library.json"))));
    }
    void eachSaveKeepsThePreviousDocumentAsTheBackup() {
        QTemporaryDir root;
        QtJsonLibraryStore store(root.path());
        QCOMPARE(store.save(sample()), LibrarySaveStatus::Saved);
        const auto first = read(root.filePath(QStringLiteral("library.json")));
        auto changed = sample();
        changed.records.pop_back();
        QCOMPARE(store.save(changed), LibrarySaveStatus::Saved);
        QCOMPARE(read(root.filePath(QStringLiteral("library.json.bak"))), first);
        write(root.filePath(QStringLiteral("library.json")), "{ broken");
        const auto recovered = store.load();
        QCOMPARE(recovered.status, LibraryLoadStatus::RecoveredFromBackup);
        QVERIFY(recovered.snapshot == sample());
    }
    void aFutureVersionIsNeverOverwritten() {
        QTemporaryDir root;
        const auto future = QByteArray(R"({"schemaVersion":2,"anything":{"nested":[1,2,3]}})");
        write(root.filePath(QStringLiteral("library.json")), future);
        QtJsonLibraryStore store(root.path());
        QCOMPARE(store.load().status, LibraryLoadStatus::FutureVersion);
        QCOMPARE(store.save(sample()), LibrarySaveStatus::BlockedByFutureVersion);
        QCOMPARE(read(root.filePath(QStringLiteral("library.json"))), future);
    }
    void aHeldLockMakesTheStoreUnavailable() {
        QTemporaryDir root;
        QtJsonLibraryStore store(root.path());
        QLockFile other(root.filePath(QStringLiteral("library.json.lock")));
        QVERIFY(other.tryLock(0));
        QCOMPARE(store.load().status, LibraryLoadStatus::Unavailable);
        QCOMPARE(store.save(sample()), LibrarySaveStatus::Failed);
        QVERIFY(!QFile::exists(root.filePath(QStringLiteral("library.json"))));
    }
    void strictDocumentsRejectUnknownOrInconsistentContent_data() {
        QTest::addColumn<QByteArray>("document");
        const QByteArray record =
            R"({"id":"1","sourcePath":"C:/a.mp3","title":"","artist":"","album":"","durationMs":null,"available":true})";
        auto document = [](const QByteArray& records, const QByteArray& next = "\"2\"") {
            return R"({"schemaVersion":1,"nextId":)" + next + R"(,"downloadJobs":[],"records":[)" + records + "]}";
        };
        QTest::newRow("valid control") << QByteArray{};
        QTest::newRow("extra root key") << QByteArray(
            R"({"schemaVersion":1,"nextId":"2","downloadJobs":[],"records":[],"x":1})");
        QTest::newRow("missing job ledger") << QByteArray(R"({"schemaVersion":1,"nextId":"2","records":[]})");
        QTest::newRow("numeric job") << QByteArray(
            R"({"schemaVersion":1,"nextId":"2","downloadJobs":[7],"records":[]})");
        QTest::newRow("duplicate job") << QByteArray(
            R"({"schemaVersion":1,"nextId":"2","downloadJobs":["a","a"],"records":[]})");
        QTest::newRow("job with space") << QByteArray(
            R"({"schemaVersion":1,"nextId":"2","downloadJobs":["a b"],"records":[]})");
        QTest::newRow("leading zero id") << document(QByteArray(record).replace(R"("id":"1")", R"("id":"01")"));
        QTest::newRow("zero next id") << document(record, "\"0\"");
        QTest::newRow("numeric id") << document(QByteArray(record).replace(R"("id":"1")", R"("id":1)"));
        QTest::newRow("duplicate path") << document(
            record + "," + QByteArray(record).replace(R"("id":"1")", R"("id":"5")"), "\"6\"");
        QTest::newRow("extra record key") << document(QByteArray(record).replace("}", R"(,"cover":"x"})"));
        QTest::newRow("numeric duration")
            << document(QByteArray(record).replace(R"("durationMs":null)", R"("durationMs":12)"));
        QTest::newRow("availability string")
            << document(QByteArray(record).replace(R"("available":true)", R"("available":"true")"));
        QTest::newRow("multi-line title") << document(QByteArray(record).replace(R"("title":"")", R"("title":"a\nb")"));
        QTest::newRow("empty path") << document(QByteArray(record).replace(R"("C:/a.mp3")", R"("")"));
    }
    void strictDocumentsRejectUnknownOrInconsistentContent() {
        QFETCH(QByteArray, document);
        QTemporaryDir root;
        QtJsonLibraryStore store(root.path());
        if (document.isEmpty()) {
            write(root.filePath(QStringLiteral("library.json")),
                  R"({"schemaVersion":1,"nextId":"2","downloadJobs":["downloads-3"],"records":[{"id":"1",)"
                  R"("sourcePath":"C:/a.mp3","title":"",)"
                  R"("artist":"","album":"","durationMs":"5","available":true}]})");
            const auto result = store.load();
            QCOMPARE(result.status, LibraryLoadStatus::Loaded);
            QCOMPARE(result.snapshot.records.front().duration_ms, std::optional<std::uint64_t>{5});
            QCOMPARE(result.snapshot.download_jobs, std::vector<std::string>{"downloads-3"});
            return;
        }
        write(root.filePath(QStringLiteral("library.json")), document);
        QCOMPARE(store.load().status, LibraryLoadStatus::Invalid);
        // Neither slot is valid: saving would discard the user's data, so it is refused.
        QCOMPARE(store.save(sample()), LibrarySaveStatus::Failed);
        QCOMPARE(read(root.filePath(QStringLiteral("library.json"))), document);
    }
    void aFailedCommitLeavesThePreviousDocumentInPlace() {
        QTemporaryDir root;
        QCOMPARE(QtJsonLibraryStore(root.path()).save(sample()), LibrarySaveStatus::Saved);
        const auto before = read(root.filePath(QStringLiteral("library.json")));
        QtJsonLibraryStore failing(root.path(), {.inject_fault = [](LibraryStoreFaultPoint point) {
                                       return point == LibraryStoreFaultPoint::BeforePrimaryCommit;
                                   }});
        QCOMPARE(failing.save(LibrarySnapshot{}), LibrarySaveStatus::Failed);
        QCOMPARE(read(root.filePath(QStringLiteral("library.json"))), before);
    }
    void oversizedFilesAreInvalidWithoutBeingParsed() {
        QTemporaryDir root;
        write(root.filePath(QStringLiteral("library.json")),
              QByteArray(QtJsonLibraryStore::maximum_file_size + 1, ' '));
        QCOMPARE(QtJsonLibraryStore(root.path()).load().status, LibraryLoadStatus::Invalid);
    }
    void relativeDirectoriesAreRefused() {
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, QtJsonLibraryStore(QStringLiteral("relative/config")));
    }
};

QTEST_GUILESS_MAIN(QtJsonLibraryStoreTests)
#include "QtJsonLibraryStoreTests.moc"
