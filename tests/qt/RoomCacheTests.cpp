#include "adapters/RoomCache.hpp"

#include "m0/domain/ListeningRoomLimits.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <string>

using namespace music_player::m0;

namespace {
const std::string sha_a(64, 'a');
const std::string sha_b = std::string(32, '0') + std::string(32, 'f');

bool writeFile(const QString& path, const qint64 size) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(QByteArray(size, 'x')) == size;
}
} // namespace

class RoomCacheTests final : public QObject {
    Q_OBJECT

  private slots:
    void livesUnderTheConfigDirectory() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        const RoomCache cache(config.path());
        QCOMPARE(QDir(cache.directory()), QDir(QDir(config.path()).filePath("room-cache")));
    }

    void pathForReturnsOnlyCompleteFiles() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        RoomCache cache(config.path());
        QVERIFY(!cache.pathFor(sha_a).has_value());
        QVERIFY(QDir().mkpath(cache.directory()));
        const auto partial = cache.partialPathFor(sha_a);
        QVERIFY(partial.has_value());
        QCOMPARE(QFileInfo(*partial).fileName(), QString::fromStdString(sha_a + ".part"));
        QVERIFY(writeFile(*partial, 10));
        QVERIFY(!cache.pathFor(sha_a).has_value());
        QVERIFY(writeFile(QDir(cache.directory()).filePath(QString::fromStdString(sha_a)), 10));
        const auto complete = cache.pathFor(sha_a);
        QVERIFY(complete.has_value());
        QCOMPARE(QFileInfo(*complete).fileName(), QString::fromStdString(sha_a));
    }

    void purgeAllRemovesPartialAndCompleteFiles() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        RoomCache cache(config.path());
        QVERIFY(QDir().mkpath(cache.directory()));
        QVERIFY(writeFile(*cache.partialPathFor(sha_a), 100));
        QVERIFY(writeFile(QDir(cache.directory()).filePath(QString::fromStdString(sha_b)), 100));
        const auto sibling = QDir(config.path()).filePath("settings.json");
        QVERIFY(writeFile(sibling, 5));

        cache.purgeAll();

        QVERIFY(!QFileInfo::exists(*cache.partialPathFor(sha_a)));
        QVERIFY(!cache.pathFor(sha_b).has_value());
        QVERIFY(QDir(cache.directory()).entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty());
        QVERIFY(QFileInfo::exists(sibling)); // only the room cache goes
    }

    void reserveCountsWhatIsOnDiskAndWhatIsReserved() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        RoomCache cache(config.path());
        QVERIFY(cache.reserve(room_cache_quota_bytes));
        QVERIFY(!cache.reserve(1));
        cache.release(room_cache_quota_bytes);
        QVERIFY(QDir().mkpath(cache.directory()));
        QVERIFY(writeFile(*cache.partialPathFor(sha_a), 1000));
        QVERIFY(!cache.reserve(room_cache_quota_bytes - 999));
        QVERIFY(cache.reserve(room_cache_quota_bytes - 1000));
    }

    // Quota full: only complete files marked as played make room, oldest mark first.
    void reserveEvictsPlayedFilesOnly() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        RoomCache cache(config.path());
        QVERIFY(QDir().mkpath(cache.directory()));
        const auto file_a = QDir(cache.directory()).filePath(QString::fromStdString(sha_a));
        const auto file_b = QDir(cache.directory()).filePath(QString::fromStdString(sha_b));
        QVERIFY(writeFile(file_a, 1000));
        QVERIFY(writeFile(file_b, 1000));

        QVERIFY(!cache.reserve(room_cache_quota_bytes - 1000)); // nothing played yet
        QVERIFY(QFileInfo::exists(file_a) && QFileInfo::exists(file_b));

        cache.markPlayed(sha_b);
        cache.markPlayed(sha_a);
        QVERIFY(cache.reserve(room_cache_quota_bytes - 1000));
        QVERIFY(!QFileInfo::exists(file_b)); // the first played goes first
        QVERIFY(QFileInfo::exists(file_a));
    }

    // S12.1 fix round 1: remove() drops the complete file and the partial one of that hash. A complete file
    // still open elsewhere (Windows: the player or an outgoing transfer closing it) cannot go yet: it stays
    // marked as played, so reserve() evicts it once it is closed instead of keeping it until the room ends.
    void removeDeletesBothFilesOrLeavesAnOpenFileEvictable() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        RoomCache cache(config.path());
        QVERIFY(QDir().mkpath(cache.directory()));
        const auto file_a = QDir(cache.directory()).filePath(QString::fromStdString(sha_a));
        QVERIFY(writeFile(file_a, 1000));
        QVERIFY(writeFile(*cache.partialPathFor(sha_a), 10));
        cache.remove(sha_a);
        QVERIFY(!QFileInfo::exists(file_a));
        QVERIFY(!QFileInfo::exists(*cache.partialPathFor(sha_a)));

#ifndef Q_OS_WIN
        QSKIP("an open file can be deleted here; only Windows keeps it");
#else
        QVERIFY(writeFile(file_a, 1000));
        {
            QFile open(file_a);
            QVERIFY(open.open(QIODevice::ReadOnly));
            cache.remove(sha_a);
            QVERIFY(QFileInfo::exists(file_a));
        }
        QVERIFY(cache.reserve(room_cache_quota_bytes));
        QVERIFY(!QFileInfo::exists(file_a));
#endif
    }

    // The file name comes only from a validated hash.
    void refusesHashesThatAreNotLowercaseHex() {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        RoomCache cache(config.path());
        const std::string traversal = "../" + std::string(61, 'a');
        const std::string separator = std::string(30, 'a') + "\\" + std::string(33, 'a');
        const std::string slash = std::string(30, 'a') + "/" + std::string(33, 'a');
        const std::string upper(64, 'A');
        const std::string short_hash(63, 'a');
        const std::string long_hash(65, 'a');
        for (const auto& bad : {traversal, separator, slash, upper, short_hash, long_hash, std::string{}}) {
            QVERIFY(!RoomCache::isValidSha256(bad));
            QVERIFY(!cache.pathFor(bad).has_value());
            QVERIFY(!cache.partialPathFor(bad).has_value());
            cache.markPlayed(bad);
        }
        QVERIFY(RoomCache::isValidSha256(sha_a));
        QVERIFY(RoomCache::isValidSha256(sha_b));
    }
};

QTEST_GUILESS_MAIN(RoomCacheTests)
#include "RoomCacheTests.moc"
