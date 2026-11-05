#include "adapters/QtJsonPlaylistStore.hpp"
#include "adapters/StrictJson.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QTemporaryDir>
#include <QTest>

#include <filesystem>
#include <limits>
#include <stdexcept>

using namespace music_player::m0;
namespace {
std::filesystem::path nativePath(const QString& text) {
#ifdef _WIN32
    return std::filesystem::path(text.toStdWString());
#else
    const auto bytes = text.toUtf8();
    return std::filesystem::path(std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())));
#endif
}
PlaylistLibrarySnapshot original() {
    const auto duplicate = nativePath(QString::fromUtf8("C:/Música/Abertura 日本 ☕.mp3"));
    return {{{7, "Aberturas 日本", {{1, duplicate}, {9'007'199'254'740'993ULL, duplicate}}},
             {std::numeric_limits<std::uint64_t>::max(), "Fim", {{42, nativePath("C:/Music/end.flac")}}}}};
}
PlaylistLibrarySnapshot replacement() { return {{{9, "New", {{56, nativePath("D:/Music/new.mp3")}}}}}; }
QString primary(const QString& directory) { return QDir(directory).filePath("playlists.json"); }
QString backup(const QString& directory) { return QDir(directory).filePath("playlists.json.bak"); }
QString lock(const QString& directory) { return QDir(directory).filePath("playlists.json.lock"); }
bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size() && file.flush();
}
QByteArray read(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
QByteArray document(const QJsonValue& id = QStringLiteral("1"), const QString& name = QStringLiteral("Saved")) {
    return QJsonDocument(
               QJsonObject{{"schemaVersion", 1},
                           {"playlists",
                            QJsonArray{QJsonObject{
                                {"id", id},
                                {"name", name},
                                {"entries", QJsonArray{QJsonObject{{"id", "2"}, {"sourcePath", "C:/Music/a.mp3"}}}}}}}})
        .toJson(QJsonDocument::Compact);
}
} // namespace

class QtJsonPlaylistStoreTests final : public QObject {
    Q_OBJECT
  private slots:
    void sharedPreflightKeepsDefaultDepthAndAllowsOnlyTheCatalogDepth() {
        const auto nested_catalog = document();
        QVERIFY(!detail::strictJsonPreflight(nested_catalog));
        QVERIFY(detail::strictJsonPreflight(nested_catalog, 5));
        QVERIFY(!detail::strictJsonPreflight("{\"a\":[{\"b\":[{\"c\":[]}]}]}", 5));
        QVERIFY(!detail::strictJsonPreflight("{}", 0));
        QVERIFY(!detail::strictJsonPreflight("{}", 65));
    }
    void constructorOwnsTheDirectoryAndDoesNotCreateFiles() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto directory = temporary.filePath("not-created");
        auto borrowed_units = directory.toStdU16String();
        QtJsonPlaylistStore store(QString::fromRawData(reinterpret_cast<const QChar*>(borrowed_units.data()),
                                                       static_cast<qsizetype>(borrowed_units.size())));
        QVERIFY(!QFileInfo::exists(directory));
        borrowed_units.assign(borrowed_units.size(), u'x');
        QCOMPARE(store.load().status, PlaylistLoadStatus::Missing);
        QVERIFY(!QFileInfo::exists(directory));
        QCOMPARE(store.save(original()), PlaylistSaveStatus::Saved);
        QVERIFY(QFileInfo::exists(primary(directory)));
    }
    void constructorRejectsEmptyAndRelativeDirectories() {
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, QtJsonPlaylistStore{QString{}});
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, QtJsonPlaylistStore{QStringLiteral("relative")});
    }
    void firstWriteAndRestartRoundTripUnicodeDuplicatesAndExactIds() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto directory = temporary.filePath("settings 日本");
        QtJsonPlaylistStore writer(directory);
        QCOMPARE(writer.load().status, PlaylistLoadStatus::Missing);
        QCOMPARE(writer.save(original()), PlaylistSaveStatus::Saved);
        QtJsonPlaylistStore reader(directory);
        const auto loaded = reader.load();
        QCOMPARE(loaded.status, PlaylistLoadStatus::Loaded);
        QVERIFY(loaded.snapshot == original());
        const auto encoded = QJsonDocument::fromJson(read(primary(directory))).object();
        QCOMPARE(encoded.value("schemaVersion").toInt(), 1);
        const auto lists = encoded.value("playlists").toArray();
        QCOMPARE(lists.at(1).toObject().value("id").toString(), "18446744073709551615");
        const auto entries = lists.at(0).toObject().value("entries").toArray();
        QCOMPARE(entries.at(1).toObject().value("id").toString(), "9007199254740993");
        QVERIFY(!QFileInfo::exists(backup(directory)));
    }
    void eachReplacementKeepsThePreviousPrimaryAsTheExactBackup() {
        QTemporaryDir temporary;
        QtJsonPlaylistStore store(temporary.path());
        QCOMPARE(store.save(original()), PlaylistSaveStatus::Saved);
        const auto first = read(primary(temporary.path()));
        QCOMPARE(store.save(replacement()), PlaylistSaveStatus::Saved);
        QCOMPARE(read(backup(temporary.path())), first);
        const auto second = read(primary(temporary.path()));
        QCOMPARE(store.save({}), PlaylistSaveStatus::Saved);
        QCOMPARE(read(backup(temporary.path())), second);
        const auto loaded = store.load();
        QCOMPARE(loaded.status, PlaylistLoadStatus::Loaded);
        QVERIFY(loaded.snapshot.playlists.empty());
    }
    void corruptPrimaryRecoversWithoutWritingAndPreservesTheHealthyBackupOnSave() {
        QTemporaryDir temporary;
        QtJsonPlaylistStore store(temporary.path());
        QCOMPARE(store.save(original()), PlaylistSaveStatus::Saved);
        QCOMPARE(store.save(replacement()), PlaylistSaveStatus::Saved);
        const auto good_backup = read(backup(temporary.path()));
        const QByteArray corrupt("{broken");
        QVERIFY(write(primary(temporary.path()), corrupt));
        const auto loaded = store.load();
        QCOMPARE(loaded.status, PlaylistLoadStatus::RecoveredFromBackup);
        QVERIFY(loaded.snapshot == original());
        QCOMPARE(read(primary(temporary.path())), corrupt);
        QCOMPARE(read(backup(temporary.path())), good_backup);
        QCOMPARE(store.save(replacement()), PlaylistSaveStatus::Saved);
        QCOMPARE(read(backup(temporary.path())), good_backup);
        QVERIFY(store.load().snapshot == replacement());
    }
    void missingPrimaryCanRecoverItsBackupWithoutRepairingIt() {
        QTemporaryDir temporary;
        QVERIFY(write(backup(temporary.path()), document()));
        QtJsonPlaylistStore store(temporary.path());
        QCOMPARE(store.load().status, PlaylistLoadStatus::RecoveredFromBackup);
        QVERIFY(!QFileInfo::exists(primary(temporary.path())));
    }
    void futureVersionInEitherSlotBlocksEveryWrite_data() {
        QTest::addColumn<bool>("future_primary");
        QTest::newRow("primary") << true;
        QTest::newRow("backup") << false;
    }
    void futureVersionInEitherSlotBlocksEveryWrite() {
        QFETCH(bool, future_primary);
        QTemporaryDir temporary;
        const QByteArray future("{\"schemaVersion\":2,\"opaqueFutureData\":{\"keep\":true}}");
        QVERIFY(write(primary(temporary.path()), future_primary ? future : document()));
        QVERIFY(write(backup(temporary.path()), future_primary ? document() : future));
        const auto before_primary = read(primary(temporary.path())), before_backup = read(backup(temporary.path()));
        QtJsonPlaylistStore store(temporary.path());
        QCOMPARE(store.load().status, PlaylistLoadStatus::FutureVersion);
        QCOMPARE(store.save(replacement()), PlaylistSaveStatus::BlockedByFutureVersion);
        QCOMPARE(read(primary(temporary.path())), before_primary);
        QCOMPARE(read(backup(temporary.path())), before_backup);
    }
    void invalidFilesAreNotReplacedWithDefaults() {
        QTemporaryDir temporary;
        const QByteArray primary_bytes("not-json"), backup_bytes("[]");
        QVERIFY(write(primary(temporary.path()), primary_bytes));
        QVERIFY(write(backup(temporary.path()), backup_bytes));
        QtJsonPlaylistStore store(temporary.path());
        QCOMPARE(store.load().status, PlaylistLoadStatus::Invalid);
        QCOMPARE(store.save(replacement()), PlaylistSaveStatus::Failed);
        QCOMPARE(read(primary(temporary.path())), primary_bytes);
        QCOMPARE(read(backup(temporary.path())), backup_bytes);
    }
    void aDirectoryInEitherDocumentSlotIsUnavailableAndNeverReplaced_data() {
        QTest::addColumn<bool>("primary_directory");
        QTest::newRow("primary") << true;
        QTest::newRow("backup") << false;
    }
    void aDirectoryInEitherDocumentSlotIsUnavailableAndNeverReplaced() {
        QFETCH(bool, primary_directory);
        QTemporaryDir temporary;
        QVERIFY(QDir{}.mkdir(primary_directory ? primary(temporary.path()) : backup(temporary.path())));
        QVERIFY(write(primary_directory ? backup(temporary.path()) : primary(temporary.path()), document()));
        QtJsonPlaylistStore store(temporary.path());
        QCOMPARE(store.load().status, PlaylistLoadStatus::Unavailable);
        QCOMPARE(store.save(replacement()), PlaylistSaveStatus::Failed);
        QVERIFY(QFileInfo(primary_directory ? primary(temporary.path()) : backup(temporary.path())).isDir());
    }
    void cooperativeLockRefusesLoadAndSaveWithoutWaiting() {
        QTemporaryDir temporary;
        QVERIFY(write(primary(temporary.path()), document()));
        QLockFile other(lock(temporary.path()));
        QVERIFY(other.tryLock(0));
        const auto before = read(primary(temporary.path()));
        QtJsonPlaylistStore store(temporary.path());
        QCOMPARE(store.load().status, PlaylistLoadStatus::Unavailable);
        QCOMPARE(store.save(replacement()), PlaylistSaveStatus::Failed);
        QCOMPARE(read(primary(temporary.path())), before);
        QVERIFY(!QFileInfo::exists(backup(temporary.path())));
        other.unlock();
        QCOMPARE(store.load().status, PlaylistLoadStatus::Loaded);
    }
    void malformedDocumentsCannotPublishPartialPlaylists_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("numeric-id") << document(9007199254740992.0);
        QTest::newRow("zero-id") << document(QStringLiteral("0"));
        QTest::newRow("leading-zero") << document(QStringLiteral("01"));
        QTest::newRow("overflow") << document(QStringLiteral("18446744073709551616"));
        QTest::newRow("empty-name") << document(QStringLiteral("1"), QString{});
        QTest::newRow("duplicate-key") << QByteArray("{\"schemaVersion\":1,\"schemaVersion\":1,\"playlists\":[]}");
        QTest::newRow("duplicate-playlist-id")
            << QByteArray("{\"schemaVersion\":1,\"playlists\":[{\"id\":\"1\",\"name\":\"A\",\"entries\":[]},{\"id\":"
                          "\"1\",\"name\":\"B\",\"entries\":[]}]}");
        QTest::newRow("duplicate-entry-id")
            << QByteArray("{\"schemaVersion\":1,\"playlists\":[{\"id\":\"1\",\"name\":\"A\",\"entries\":[{\"id\":\"2\","
                          "\"sourcePath\":\"a.mp3\"},{\"id\":\"2\",\"sourcePath\":\"b.mp3\"}]}]}");
        QTest::newRow("isolated-surrogate")
            << QByteArray("{\"schemaVersion\":1,\"playlists\":[{\"id\":\"1\",\"name\":\"\\uD800\",\"entries\":[]}]}");
        QTest::newRow("unknown-field") << QByteArray("{\"schemaVersion\":1,\"playlists\":[],\"lostData\":true}");
        QTest::newRow("fractional-schema") << QByteArray("{\"schemaVersion\":1.5,\"playlists\":[]}");
        QTest::newRow("invalid-utf8") << (QByteArray("{\"schemaVersion\":1,\"playlists\":[{\"id\":\"1\",\"name\":\"") +
                                          QByteArray::fromHex("ff") + "\",\"entries\":[]}]}");
    }
    void malformedDocumentsCannotPublishPartialPlaylists() {
        QFETCH(QByteArray, bytes);
        QTemporaryDir temporary;
        QVERIFY(write(primary(temporary.path()), bytes));
        QtJsonPlaylistStore store(temporary.path());
        const auto loaded = store.load();
        QCOMPARE(loaded.status, PlaylistLoadStatus::Invalid);
        QVERIFY(loaded.snapshot.playlists.empty());
        QCOMPARE(read(primary(temporary.path())), bytes);
    }
    void documentByteLimitIsCheckedBeforeParsing() {
        QTemporaryDir temporary;
        const QByteArray oversized(QtJsonPlaylistStore::maximum_file_size + 1, ' ');
        QVERIFY(write(primary(temporary.path()), oversized));
        QtJsonPlaylistStore store(temporary.path());
        QCOMPARE(store.load().status, PlaylistLoadStatus::Invalid);
        QCOMPARE(QFileInfo(primary(temporary.path())).size(), oversized.size());
    }
    void invalidSnapshotDoesNotTouchAnExistingHealthyDocument() {
        QTemporaryDir temporary;
        QtJsonPlaylistStore store(temporary.path());
        QCOMPARE(store.save(original()), PlaylistSaveStatus::Saved);
        const auto before = read(primary(temporary.path()));
        auto invalid = original();
        invalid.playlists.front().entries.front().id = 0;
        QCOMPARE(store.save(invalid), PlaylistSaveStatus::Failed);
        QCOMPARE(read(primary(temporary.path())), before);
        QVERIFY(!QFileInfo::exists(backup(temporary.path())));
    }
    void injectedCommitFailuresNeverAcknowledgeSaved_data() {
        QTest::addColumn<int>("point");
        QTest::newRow("before-backup") << int(PlaylistStoreFaultPoint::BeforeBackupCommit);
        QTest::newRow("after-backup") << int(PlaylistStoreFaultPoint::AfterBackupCommit);
        QTest::newRow("before-primary") << int(PlaylistStoreFaultPoint::BeforePrimaryCommit);
        QTest::newRow("after-primary") << int(PlaylistStoreFaultPoint::AfterPrimaryCommit);
    }
    void injectedCommitFailuresNeverAcknowledgeSaved() {
        QFETCH(int, point);
        QTemporaryDir temporary;
        QtJsonPlaylistStore ordinary(temporary.path());
        QCOMPARE(ordinary.save(original()), PlaylistSaveStatus::Saved);
        QtJsonPlaylistStore faulty(temporary.path(), {.inject_fault = [point](PlaylistStoreFaultPoint observed) {
                                       return int(observed) == point;
                                   }});
        QCOMPARE(faulty.save(replacement()), PlaylistSaveStatus::Failed);
        const auto loaded = ordinary.load();
        QCOMPARE(loaded.status, PlaylistLoadStatus::Loaded);
        QVERIFY(loaded.snapshot ==
                (point == int(PlaylistStoreFaultPoint::AfterPrimaryCommit) ? replacement() : original()));
        if (point != int(PlaylistStoreFaultPoint::BeforeBackupCommit))
            QVERIFY(!read(backup(temporary.path())).isEmpty());
    }
    void thrownFaultDoesNotBecomeASavedAcknowledgement() {
        QTemporaryDir temporary;
        QtJsonPlaylistStore ordinary(temporary.path());
        QCOMPARE(ordinary.save(original()), PlaylistSaveStatus::Saved);
        QtJsonPlaylistStore faulty(temporary.path(), {.inject_fault = [](PlaylistStoreFaultPoint point) -> bool {
                                       if (point == PlaylistStoreFaultPoint::BeforePrimaryCommit)
                                           throw std::runtime_error("fixture fault");
                                       return false;
                                   }});
        QCOMPARE(faulty.save(replacement()), PlaylistSaveStatus::Failed);
        QVERIFY(ordinary.load().snapshot == original());
    }
    void savingAndDeletingAPlaylistDoesNotOpenOrDeleteItsAudioFile() {
        QTemporaryDir temporary;
        const auto audio = temporary.filePath("user-audio.mp3");
        const QByteArray marker("fixture-audio-bytes");
        QVERIFY(write(audio, marker));
        QtJsonPlaylistStore store(temporary.filePath("settings"));
        const PlaylistLibrarySnapshot snapshot{{{1, "Saved", {{1, nativePath(audio)}}}}};
        QCOMPARE(store.save(snapshot), PlaylistSaveStatus::Saved);
        QCOMPARE(store.save({}), PlaylistSaveStatus::Saved);
        QCOMPARE(read(audio), marker);
    }
};
QTEST_GUILESS_MAIN(QtJsonPlaylistStoreTests)
#include "QtJsonPlaylistStoreTests.moc"
