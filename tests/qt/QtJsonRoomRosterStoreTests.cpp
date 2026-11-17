#include "adapters/QtJsonRoomRosterStore.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <QtLogging>

#include <stdexcept>

using namespace music_player::m0;
namespace {
QString primary(const QString& d) { return QDir(d).filePath("room-roles.json"); }
QString backup(const QString& d) { return QDir(d).filePath("room-roles.json.bak"); }
bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
std::array<std::uint8_t, 16> sequence(const std::uint8_t start) {
    std::array<std::uint8_t, 16> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::uint8_t>(start + i * 7);
    return bytes;
}
QByteArray b64(const std::array<std::uint8_t, 16>& bytes) {
    return QByteArray(reinterpret_cast<const char*>(bytes.data()), 16)
        .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}
RoomRosterSnapshot rich() {
    auto s = defaultRoomRoster();
    RoomPermissionSet some;
    some.set(static_cast<std::size_t>(RoomPermission::Skip));
    some.set(static_cast<std::size_t>(RoomPermission::AddYoutube));
    s.roles.push_back({3, "Moderador", some});
    s.listeners.push_back({"fp-ana", "Ana", 3, {{RoomPermission::Pause, true}, {RoomPermission::Skip, false}}, false});
    s.listeners.push_back({"fp-rui", "", 1, {}, true});
    s.invite = PersistedInvite{sequence(1), sequence(100), 5, 3};
    return s;
}
bool same(const RoomRosterSnapshot& a, const RoomRosterSnapshot& b) {
    if (a.roles.size() != b.roles.size() || a.listeners.size() != b.listeners.size() ||
        a.invite.has_value() != b.invite.has_value())
        return false;
    for (std::size_t i = 0; i < a.roles.size(); ++i)
        if (a.roles[i].id != b.roles[i].id || a.roles[i].name != b.roles[i].name ||
            a.roles[i].permissions != b.roles[i].permissions)
            return false;
    for (std::size_t i = 0; i < a.listeners.size(); ++i) {
        const auto &x = a.listeners[i], &y = b.listeners[i];
        if (x.fingerprint != y.fingerprint || x.last_nickname != y.last_nickname || x.role_id != y.role_id ||
            x.blocked != y.blocked || x.overrides.size() != y.overrides.size())
            return false;
        for (std::size_t j = 0; j < x.overrides.size(); ++j)
            if (x.overrides[j].permission != y.overrides[j].permission ||
                x.overrides[j].allowed != y.overrides[j].allowed)
                return false;
    }
    if (a.invite)
        return a.invite->room_id == b.invite->room_id && a.invite->secret == b.invite->secret &&
               a.invite->max_listeners == b.invite->max_listeners &&
               a.invite->default_role_id == b.invite->default_role_id;
    return true;
}
QByteArray captured;
void capture(QtMsgType, const QMessageLogContext&, const QString& message) { captured += message.toUtf8() + '\n'; }
const QByteArray future = R"({"schemaVersion":2,"whatever":true})";
const QByteArray goodRoles =
    R"("roles":[{"id":1,"name":"Ouvinte","permissions":[]},{"id":2,"name":"DJ","permissions":["skip"]}])";
QByteArray doc(const QByteArray& listeners, const QByteArray& extra = {}) {
    return R"({"schemaVersion":1,)" + goodRoles + R"(,"listeners":)" + listeners + extra + "}";
}
QByteArray invite(const QByteArray& room, const QByteArray& secret, const QByteArray& max) {
    return R"(,"invite":{"roomId":")" + room + R"(","secret":")" + secret + R"(","maxListeners":)" + max +
           R"(,"defaultRoleId":1})";
}
} // namespace

class QtJsonRoomRosterStoreTests final : public QObject {
    Q_OBJECT
  private slots:
    void missingFileLoadsTheDefaultRoster() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        for (const auto& path : {dir.path() + "/none", dir.path()}) {
            const auto result = QtJsonRoomRosterStore(path).load();
            QCOMPARE(result.status, RoomRosterLoadStatus::Missing);
            QVERIFY(same(result.snapshot, defaultRoomRoster()));
        }
    }
    void roundTripsRolesListenersOverridesBlocksAndInvite() {
        QTemporaryDir dir;
        QtJsonRoomRosterStore store(dir.path() + "/cfg");
        QCOMPARE(store.save(rich()), RoomRosterSaveStatus::Saved);
        const auto loaded = store.load();
        QCOMPARE(loaded.status, RoomRosterLoadStatus::Loaded);
        QVERIFY(same(loaded.snapshot, rich()));
        QCOMPARE(store.save(defaultRoomRoster()), RoomRosterSaveStatus::Saved);
        QVERIFY(same(store.load().snapshot, defaultRoomRoster()));
    }
    void storesPermissionsAsNamesAndIdsAsBase64Url() {
        QTemporaryDir dir;
        QtJsonRoomRosterStore store(dir.path());
        QCOMPARE(store.save(rich()), RoomRosterSaveStatus::Saved);
        const auto text = read(primary(dir.path()));
        QVERIFY(text.contains("\"addYoutube\""));
        QVERIFY(text.contains("\"pause\""));
        QVERIFY(text.contains(b64(sequence(1))));
        QVERIFY(text.contains(b64(sequence(100))));
    }
    void refusesToSaveAnInvalidSnapshot() {
        QTemporaryDir dir;
        QtJsonRoomRosterStore store(dir.path());
        auto bad = rich();
        bad.listeners[0].role_id = 99;
        QCOMPARE(store.save(bad), RoomRosterSaveStatus::Failed);
        bad = rich();
        bad.listeners[0].last_nickname = " Ana ";
        QCOMPARE(store.save(bad), RoomRosterSaveStatus::Failed);
        QVERIFY(!QFile::exists(primary(dir.path())));
    }
    void usesTheBackupWhenThePrimaryIsCorrupt() {
        QTemporaryDir dir;
        QtJsonRoomRosterStore store(dir.path());
        QCOMPARE(store.save(rich()), RoomRosterSaveStatus::Saved);
        QCOMPARE(store.save(defaultRoomRoster()), RoomRosterSaveStatus::Saved);
        QVERIFY(write(primary(dir.path()), "{broken"));
        const auto loaded = store.load();
        QCOMPARE(loaded.status, RoomRosterLoadStatus::RecoveredFromBackup);
        QVERIFY(same(loaded.snapshot, rich()));
    }
    void doesNotOverwriteAGoodBackupWithACorruptPrimary() {
        QTemporaryDir dir;
        QtJsonRoomRosterStore store(dir.path());
        QCOMPARE(store.save(rich()), RoomRosterSaveStatus::Saved);
        QCOMPARE(store.save(rich()), RoomRosterSaveStatus::Saved);
        QVERIFY(write(primary(dir.path()), "{broken"));
        const auto good = read(backup(dir.path()));
        QCOMPARE(store.save(defaultRoomRoster()), RoomRosterSaveStatus::Saved);
        QCOMPARE(read(backup(dir.path())), good);
    }
    void invalidFilesReturnTheDefaultRosterWithInvalidStatus() {
        QTemporaryDir dir;
        QtJsonRoomRosterStore store(dir.path());
        const QByteArray id = "AAAAAAAAAAAAAAAAAAAAAA";
        const QList<QByteArray> bad = {
            "[]",
            "{broken",
            doc("[]", R"(,"extra":1)"),
            R"({"schemaVersion":1,"roles":[{"id":1,"name":"A","permissions":["fly"]}],"listeners":[]})",
            doc(R"([{"fingerprint":"f","lastNickname":"","roleId":9,"overrides":[],"blocked":false}])"),
            doc(R"([{"fingerprint":"f","lastNickname":" Ana ","roleId":1,"overrides":[],"blocked":false}])"),
            doc(R"([{"fingerprint":"f","lastNickname":"A\u0007","roleId":1,"overrides":[],"blocked":false}])"),
            doc(R"([{"fingerprint":"f","lastNickname":"","roleId":1,"blocked":false,"overrides":[)"
                R"({"permission":"skip","allowed":true},{"permission":"skip","allowed":false}]}])"),
            doc("[]", invite("AAAA", id, "5")),
            doc("[]", invite(id + "==", id, "5")),
            doc("[]", invite(id, id, "0")),
            R"({"schemaVersion":1,"roles":[{"id":1.5,"name":"A","permissions":[]}],"listeners":[]})",
        };
        for (const auto& text : bad) {
            QVERIFY(write(primary(dir.path()), text));
            const auto loaded = store.load();
            QVERIFY2(loaded.status == RoomRosterLoadStatus::Invalid, text.constData());
            QVERIFY(same(loaded.snapshot, defaultRoomRoster()));
        }
        // The helpers produce a loadable document when nothing is wrong.
        QVERIFY(write(primary(dir.path()), doc("[]", invite(id, id, "5"))));
        QCOMPARE(store.load().status, RoomRosterLoadStatus::Loaded);
    }
    void oversizedFilesAreInvalidAndNotSavedOver() {
        QTemporaryDir dir;
        QtJsonRoomRosterStore store(dir.path());
        QVERIFY(write(primary(dir.path()), QByteArray(QtJsonRoomRosterStore::maximum_file_size + 1, ' ')));
        QCOMPARE(store.load().status, RoomRosterLoadStatus::Invalid);
        QCOMPARE(store.save(rich()), RoomRosterSaveStatus::Failed);
    }
    void futureVersionBlocksSavingAndLeavesFilesUntouched() {
        QTemporaryDir dir;
        QtJsonRoomRosterStore store(dir.path());
        QVERIFY(write(primary(dir.path()), future));
        const auto loaded = store.load();
        QCOMPARE(loaded.status, RoomRosterLoadStatus::FutureVersion);
        QVERIFY(same(loaded.snapshot, defaultRoomRoster()));
        QCOMPARE(store.save(rich()), RoomRosterSaveStatus::BlockedByFutureVersion);
        QCOMPARE(read(primary(dir.path())), future);
        QVERIFY(!QFile::exists(backup(dir.path())));
    }
    void futureVersionInTheBackupAlsoBlocksSaving() {
        QTemporaryDir dir;
        QtJsonRoomRosterStore store(dir.path());
        QCOMPARE(store.save(rich()), RoomRosterSaveStatus::Saved);
        QVERIFY(write(backup(dir.path()), future));
        QCOMPARE(store.load().status, RoomRosterLoadStatus::FutureVersion);
        QCOMPARE(store.save(defaultRoomRoster()), RoomRosterSaveStatus::BlockedByFutureVersion);
    }
    void rejectsRelativeDirectories() {
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, QtJsonRoomRosterStore("relative"));
    }
    void neverLogsTheSecretOnAnyPath() {
        QTemporaryDir dir;
        const auto snapshot = rich();
        const QByteArray raw(reinterpret_cast<const char*>(snapshot.invite->secret.data()), 16);
        const auto text = b64(snapshot.invite->secret);
        const auto hex = raw.toHex();
        captured.clear();
        const auto previous = qInstallMessageHandler(capture);
        QtJsonRoomRosterStore store(dir.path());
        (void)store.save(snapshot);
        (void)store.load();
        (void)store.save(snapshot); // creates the backup
        write(primary(dir.path()), read(primary(dir.path())).replace("schemaVersion", "schemaVersio_"));
        (void)store.load(); // corrupted primary, falls back to the backup
        (void)store.save(snapshot);
        write(primary(dir.path()), read(primary(dir.path())).append("garbage"));
        write(backup(dir.path()), read(backup(dir.path())).append("garbage"));
        (void)store.load(); // invalid
        (void)store.save(snapshot);
        write(primary(dir.path()), future);
        (void)store.load();
        (void)store.save(snapshot);
        qInstallMessageHandler(previous);
        QVERIFY(!captured.contains(text));
        QVERIFY(!captured.contains(hex));
        QVERIFY(!captured.contains(hex.toUpper()));
    }
};

QTEST_GUILESS_MAIN(QtJsonRoomRosterStoreTests)
#include "QtJsonRoomRosterStoreTests.moc"
