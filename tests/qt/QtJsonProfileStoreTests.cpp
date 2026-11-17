#include "adapters/QtJsonProfileStore.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace music_player::m0;
namespace {
const std::string hash(64, 'a');
ListenerProfile bundled() { return {"Ana", {AvatarKind::Bundled, 3, {}}}; }
ListenerProfile custom() { return {"Rui", {AvatarKind::Custom, 0, hash}}; }
QString primary(const QString& d) { return QDir(d).filePath("profile.json"); }
QString backup(const QString& d) { return QDir(d).filePath("profile.json.bak"); }
bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
} // namespace

class QtJsonProfileStoreTests final : public QObject {
    Q_OBJECT
  private slots:
    void missingDirectoryAndFileLoadAsMissing() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QCOMPARE(QtJsonProfileStore(dir.path() + "/none").load().status, ProfileLoadStatus::Missing);
        const auto result = QtJsonProfileStore(dir.path()).load();
        QCOMPARE(result.status, ProfileLoadStatus::Missing);
        QVERIFY(!result.profile);
    }
    void roundTripsBundledAndCustomProfiles() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QtJsonProfileStore store(dir.path() + "/cfg");
        QCOMPARE(store.save(bundled()), ProfileSaveStatus::Saved);
        auto loaded = store.load();
        QCOMPARE(loaded.status, ProfileLoadStatus::Loaded);
        QVERIFY(loaded.profile && *loaded.profile == bundled());
        QCOMPARE(store.save(custom()), ProfileSaveStatus::Saved);
        loaded = store.load();
        QCOMPARE(loaded.status, ProfileLoadStatus::Loaded);
        QVERIFY(loaded.profile && *loaded.profile == custom());
    }
    void refusesToSaveAnInvalidProfile() {
        QTemporaryDir dir;
        QtJsonProfileStore store(dir.path());
        QCOMPARE(store.save({" Ana ", {}}), ProfileSaveStatus::Failed);
        QVERIFY(!QFile::exists(primary(dir.path())));
    }
    void usesTheBackupWhenThePrimaryIsCorrupt() {
        QTemporaryDir dir;
        QtJsonProfileStore store(dir.path());
        QCOMPARE(store.save(bundled()), ProfileSaveStatus::Saved);
        QCOMPARE(store.save(custom()), ProfileSaveStatus::Saved);
        QVERIFY(write(primary(dir.path()), "{broken"));
        const auto loaded = store.load();
        QCOMPARE(loaded.status, ProfileLoadStatus::RecoveredFromBackup);
        QVERIFY(loaded.profile && *loaded.profile == bundled());
    }
    void rejectsAProfileThatFailsDomainValidation() {
        QTemporaryDir dir;
        QtJsonProfileStore store(dir.path());
        QVERIFY(write(primary(dir.path()),
                      R"({"schemaVersion":1,"nickname":"Ana","avatar":{"kind":"bundled","index":99}})"));
        const auto loaded = store.load();
        QCOMPARE(loaded.status, ProfileLoadStatus::Invalid);
        QVERIFY(!loaded.profile);
    }
    void futureVersionBlocksSavingAndLeavesFilesUntouched() {
        QTemporaryDir dir;
        QtJsonProfileStore store(dir.path());
        const QByteArray future = R"({"schemaVersion":2,"whatever":true})";
        QVERIFY(write(primary(dir.path()), future));
        QCOMPARE(store.load().status, ProfileLoadStatus::FutureVersion);
        QCOMPARE(store.save(bundled()), ProfileSaveStatus::BlockedByFutureVersion);
        QCOMPARE(read(primary(dir.path())), future);
        QVERIFY(!QFile::exists(backup(dir.path())));
    }
};

QTEST_GUILESS_MAIN(QtJsonProfileStoreTests)
#include "QtJsonProfileStoreTests.moc"
