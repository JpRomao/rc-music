#include "adapters/QtJsonWindowsPreferencesStore.hpp"

#include "adapters/PreferencesJsonCodec.hpp"

#include "m0/domain/WindowPlacement.hpp"

#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>

#define NOMINMAX
#include <Windows.h>

#include <optional>

using namespace music_player::m0;

namespace {

[[nodiscard]] Preferences nonDefaultPreferences() {
    return Preferences{.theme = ThemeMode::Dark,
                       .motion = MotionMode::Reduced,
                       .volume = 0.35,
                       .muted = true,
                       .skip_backward_seconds = 7,
                       .skip_forward_seconds = 20,
                       .close_behavior = CloseBehavior::Exit,
                       .restore_last_session = false,
                       .start_minimized = true,
                       .queue_end_behavior = QueueEndBehavior::StopClearSelection,
                       .window_placement = WindowPlacement{-1'920, 40, 960, 640}};
}

[[nodiscard]] QByteArray readAll(const QString& path) {
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

[[nodiscard]] bool writeAll(const QString& path, const QByteArray& bytes) {
    QFile file{path};
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(bytes) == bytes.size() && file.flush();
}

[[nodiscard]] QString preferencesPath(const QString& root) {
    return QDir{root}.filePath(QStringLiteral("preferences.json"));
}

[[nodiscard]] QString backupPath(const QString& root) {
    return QDir{root}.filePath(QStringLiteral("preferences.json.bak"));
}

} // namespace

class QtJsonWindowsPreferencesStoreTests final : public QObject {
    Q_OBJECT

  private slots:
    void loadsDefaultsWhenBothFilesAreAbsent();
    void loadsVersionOneWithoutRewritingIt();
    void roundTripsAllPreferencesInAUnicodeDirectory();
    void enforcesTheExactFileSizeBoundaryWithValidJson();
    void recoversFromAValidBackupWithoutRewritingDuringLoad();
    void recoversFromBackupWhenThePrimaryIsAbsent();
    void usesDefaultsWithoutRewritingWhenBothFilesAreInvalid();
    void preservesAValidBackupWhenThePrimaryIsCorrupt();
    void blocksFuturePrimaryUntilReplacementIsExplicit();
    void blocksFutureBackupUntilReplacementIsExplicit();
    void preservesFutureBackupUntilPrimaryReplacementSucceeds_data();
    void preservesFutureBackupUntilPrimaryReplacementSucceeds();
    void refusesToWriteWhenAnExistingSlotCannotBeRead_data();
    void refusesToWriteWhenAnExistingSlotCannotBeRead();
    void injectedFaultBeforeCommitPreservesThePrimary();
    void injectedFaultAfterCommitReportsFailureButLeavesAValidReplacement();
    void retainsExactlyOneBackupAcrossRepeatedSaves();
};

void QtJsonWindowsPreferencesStoreTests::loadsDefaultsWhenBothFilesAreAbsent() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = QDir{temporary.path()}.filePath(QStringLiteral("configuração 日本"));

    QtJsonWindowsPreferencesStore store{root};
    const PreferencesLoadResult result = store.load();

    QCOMPARE(result.status, PreferencesLoadStatus::Missing);
    QCOMPARE(result.preferences, Preferences{});
    QVERIFY(!QFile::exists(preferencesPath(root)));
    QVERIFY(!QFile::exists(backupPath(root)));
}

void QtJsonWindowsPreferencesStoreTests::loadsVersionOneWithoutRewritingIt() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const QByteArray version_one{
        R"({"schemaVersion":1,"theme":"dark","motion":"reduced","volume":0.35,"muted":true,"skipBackwardSeconds":7,"skipForwardSeconds":20,"closeBehavior":"exit","restoreLastSession":false,"startMinimized":true,"queueEndBehavior":"stop_clear_selection"})"};
    QVERIFY(writeAll(preferencesPath(root), version_one));

    QtJsonWindowsPreferencesStore store{root};
    const PreferencesLoadResult result = store.load();

    Preferences expected = nonDefaultPreferences();
    expected.window_placement = std::nullopt;
    QCOMPARE(result.status, PreferencesLoadStatus::Loaded);
    QCOMPARE(result.preferences, expected);
    QCOMPARE(readAll(preferencesPath(root)), version_one);
    QVERIFY(!QFile::exists(backupPath(root)));
}

void QtJsonWindowsPreferencesStoreTests::roundTripsAllPreferencesInAUnicodeDirectory() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = QDir{temporary.path()}.filePath(QStringLiteral("ação 日本 ☕"));
    auto expected = nonDefaultPreferences();
    expected.repeat_queue_enabled = true;
    expected.shuffle_queue_enabled = true;

    QtJsonWindowsPreferencesStore writer{root};
    QCOMPARE(writer.save({.preferences = expected}), PreferencesSaveStatus::Saved);

    QtJsonWindowsPreferencesStore reader{root};
    const PreferencesLoadResult result = reader.load();
    QCOMPARE(result.status, PreferencesLoadStatus::Loaded);
    QCOMPARE(result.preferences, expected);
}

void QtJsonWindowsPreferencesStoreTests::enforcesTheExactFileSizeBoundaryWithValidJson() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const Preferences expected = nonDefaultPreferences();
    QByteArray document = PreferencesJsonCodec::encode(expected);
    QVERIFY(document.size() < QtJsonWindowsPreferencesStore::maximum_file_size);
    document.append(QByteArray(QtJsonWindowsPreferencesStore::maximum_file_size - document.size(), ' '));
    QCOMPARE(document.size(), QtJsonWindowsPreferencesStore::maximum_file_size);
    QVERIFY(writeAll(preferencesPath(root), document));

    QtJsonWindowsPreferencesStore store{root};
    const PreferencesLoadResult exact_limit = store.load();
    QCOMPARE(exact_limit.status, PreferencesLoadStatus::Loaded);
    QCOMPARE(exact_limit.preferences, expected);

    document.append(' ');
    QCOMPARE(document.size(), QtJsonWindowsPreferencesStore::maximum_file_size + 1);
    QVERIFY(writeAll(preferencesPath(root), document));
    const PreferencesLoadResult above_limit = store.load();
    QCOMPARE(above_limit.status, PreferencesLoadStatus::InvalidUsingDefaults);
    QCOMPARE(above_limit.preferences, Preferences{});
}

void QtJsonWindowsPreferencesStoreTests::recoversFromAValidBackupWithoutRewritingDuringLoad() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const QByteArray corrupt{"{truncated"};
    const Preferences backup_preferences = nonDefaultPreferences();
    QVERIFY(writeAll(preferencesPath(root), corrupt));
    QVERIFY(writeAll(backupPath(root), PreferencesJsonCodec::encode(backup_preferences)));

    QtJsonWindowsPreferencesStore store{root};
    const PreferencesLoadResult result = store.load();

    QCOMPARE(result.status, PreferencesLoadStatus::RecoveredFromBackup);
    QCOMPARE(result.preferences, backup_preferences);
    QCOMPARE(readAll(preferencesPath(root)), corrupt);
}

void QtJsonWindowsPreferencesStoreTests::recoversFromBackupWhenThePrimaryIsAbsent() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const Preferences backup_preferences = nonDefaultPreferences();
    QVERIFY(writeAll(backupPath(root), PreferencesJsonCodec::encode(backup_preferences)));

    QtJsonWindowsPreferencesStore store{root};
    const PreferencesLoadResult result = store.load();

    QCOMPARE(result.status, PreferencesLoadStatus::RecoveredFromBackup);
    QCOMPARE(result.preferences, backup_preferences);
    QVERIFY(!QFile::exists(preferencesPath(root)));
}

void QtJsonWindowsPreferencesStoreTests::usesDefaultsWithoutRewritingWhenBothFilesAreInvalid() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const QByteArray corrupt_primary{"{primary"};
    const QByteArray corrupt_backup{"{backup"};
    QVERIFY(writeAll(preferencesPath(root), corrupt_primary));
    QVERIFY(writeAll(backupPath(root), corrupt_backup));

    QtJsonWindowsPreferencesStore store{root};
    const PreferencesLoadResult result = store.load();

    QCOMPARE(result.status, PreferencesLoadStatus::InvalidUsingDefaults);
    QCOMPARE(result.preferences, Preferences{});
    QCOMPARE(readAll(preferencesPath(root)), corrupt_primary);
    QCOMPARE(readAll(backupPath(root)), corrupt_backup);
}

void QtJsonWindowsPreferencesStoreTests::preservesAValidBackupWhenThePrimaryIsCorrupt() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const QByteArray corrupt{"not json"};
    const Preferences backup_preferences = nonDefaultPreferences();
    Preferences replacement = backup_preferences;
    replacement.volume = 0.6;
    const QByteArray valid_backup = PreferencesJsonCodec::encode(backup_preferences);
    QVERIFY(writeAll(preferencesPath(root), corrupt));
    QVERIFY(writeAll(backupPath(root), valid_backup));

    QtJsonWindowsPreferencesStore store{root};
    QCOMPARE(store.save({.preferences = replacement}), PreferencesSaveStatus::Saved);

    QCOMPARE(readAll(backupPath(root)), valid_backup);
    QCOMPARE(store.load().preferences, replacement);
}

void QtJsonWindowsPreferencesStoreTests::blocksFuturePrimaryUntilReplacementIsExplicit() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const QByteArray future{"{\"schemaVersion\":3,\"future\":true}"};
    QVERIFY(writeAll(preferencesPath(root), future));
    QtJsonWindowsPreferencesStore store{root};

    const PreferencesLoadResult loaded = store.load();
    QCOMPARE(loaded.status, PreferencesLoadStatus::FutureVersion);
    QCOMPARE(loaded.preferences, Preferences{});
    QCOMPARE(store.save({.preferences = nonDefaultPreferences()}), PreferencesSaveStatus::BlockedByFutureVersion);
    QCOMPARE(readAll(preferencesPath(root)), future);

    QCOMPARE(store.save({.preferences = nonDefaultPreferences(), .replace_future_version = true}),
             PreferencesSaveStatus::Saved);
    QCOMPARE(store.load().status, PreferencesLoadStatus::Loaded);
    QCOMPARE(store.load().preferences, nonDefaultPreferences());
    QVERIFY(!QFile::exists(backupPath(root)));
}

void QtJsonWindowsPreferencesStoreTests::blocksFutureBackupUntilReplacementIsExplicit() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const QByteArray future{"{\"schemaVersion\":3,\"future\":true}"};
    QVERIFY(writeAll(backupPath(root), future));
    QtJsonWindowsPreferencesStore store{root};

    const PreferencesLoadResult loaded = store.load();
    QCOMPARE(loaded.status, PreferencesLoadStatus::FutureVersion);
    QCOMPARE(loaded.preferences, Preferences{});
    QCOMPARE(store.save({.preferences = nonDefaultPreferences()}), PreferencesSaveStatus::BlockedByFutureVersion);
    QVERIFY(!QFile::exists(preferencesPath(root)));
    QCOMPARE(readAll(backupPath(root)), future);

    QCOMPARE(store.save({.preferences = nonDefaultPreferences(), .replace_future_version = true}),
             PreferencesSaveStatus::Saved);
    QCOMPARE(store.load().status, PreferencesLoadStatus::Loaded);
    QCOMPARE(store.load().preferences, nonDefaultPreferences());
    QCOMPARE(PreferencesJsonCodec::decode(readAll(backupPath(root))).preferences,
             std::optional<Preferences>{nonDefaultPreferences()});
}

void QtJsonWindowsPreferencesStoreTests::preservesFutureBackupUntilPrimaryReplacementSucceeds_data() {
    QTest::addColumn<bool>("primary_committed");
    QTest::newRow("before primary commit") << false;
    QTest::newRow("ambiguous primary commit") << true;
}

void QtJsonWindowsPreferencesStoreTests::preservesFutureBackupUntilPrimaryReplacementSucceeds() {
    QFETCH(bool, primary_committed);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const QByteArray original = PreferencesJsonCodec::encode(Preferences{}) + " \r\n";
    const QByteArray future{"{\"schemaVersion\":3,\"future\":true}"};
    QVERIFY(writeAll(preferencesPath(root), original));
    QVERIFY(writeAll(backupPath(root), future));
    const auto failure = primary_committed ? PreferencesStoreFaultPoint::AfterPrimaryCommit
                                           : PreferencesStoreFaultPoint::BeforePrimaryCommit;
    QtJsonWindowsPreferencesStore failing{
        root, {.inject_fault = [failure](const PreferencesStoreFaultPoint point) { return point == failure; }}};
    const PreferencesSaveRequest request{.preferences = nonDefaultPreferences(), .replace_future_version = true};

    QCOMPARE(failing.save(request), PreferencesSaveStatus::Failed);
    const QByteArray expected_primary =
        primary_committed ? PreferencesJsonCodec::encode(request.preferences) : original;
    QCOMPARE(readAll(preferencesPath(root)), expected_primary);
    QCOMPARE(readAll(backupPath(root)), future);

    QtJsonWindowsPreferencesStore retry{root};
    QCOMPARE(retry.save({.preferences = request.preferences}), PreferencesSaveStatus::BlockedByFutureVersion);
    QCOMPARE(retry.save(request), PreferencesSaveStatus::Saved);
    QCOMPARE(retry.load().preferences, request.preferences);
    QCOMPARE(readAll(backupPath(root)), expected_primary);
}

void QtJsonWindowsPreferencesStoreTests::refusesToWriteWhenAnExistingSlotCannotBeRead_data() {
    QTest::addColumn<bool>("primary_locked");
    QTest::addColumn<bool>("replace_future_version");
    QTest::newRow("primary") << true << false;
    QTest::newRow("backup") << false << false;
    QTest::newRow("primary-with-future-replacement") << true << true;
    QTest::newRow("backup-with-future-replacement") << false << true;
}

void QtJsonWindowsPreferencesStoreTests::refusesToWriteWhenAnExistingSlotCannotBeRead() {
    QFETCH(bool, primary_locked);
    QFETCH(bool, replace_future_version);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const QString primary = preferencesPath(root);
    const QString backup = backupPath(root);
    const QString locked = primary_locked ? primary : backup;
    const QByteArray supported = PreferencesJsonCodec::encode(Preferences{});
    const QByteArray future{"{\"schemaVersion\":3,\"future\":true}"};
    QVERIFY(writeAll(primary, primary_locked ? future : supported));
    QVERIFY(writeAll(backup, primary_locked ? supported : future));

    HANDLE handle = INVALID_HANDLE_VALUE;
    const auto release_sharing = [&handle] {
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
        }
    };
    bool attempted_write = false;
    QtJsonWindowsPreferencesStore store{root, {.inject_fault = [&](const PreferencesStoreFaultPoint point) {
                                            attempted_write = true;
                                            // Another reader can close its handle after inspection but before commit.
                                            if (primary_locked &&
                                                point == PreferencesStoreFaultPoint::BeforePrimaryCommit)
                                                release_sharing();
                                            return false;
                                        }}};
    const Preferences replacement = nonDefaultPreferences();
    PreferencesSaveStatus status = PreferencesSaveStatus::Saved;
    {
        // Replacement is shareable, but inspection through a second handle is refused.
        handle = CreateFileW(reinterpret_cast<LPCWSTR>(locked.utf16()), GENERIC_READ, FILE_SHARE_DELETE, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(handle != INVALID_HANDLE_VALUE);
        const auto close = qScopeGuard(release_sharing);
        QFile unreadable{locked};
        QVERIFY(!unreadable.open(QIODevice::ReadOnly));
        const auto loaded = store.load();
        QCOMPARE(loaded.status,
                 primary_locked ? PreferencesLoadStatus::RecoveredFromBackup : PreferencesLoadStatus::Loaded);
        QCOMPARE(loaded.preferences, Preferences{});
        status = store.save({.preferences = replacement, .replace_future_version = replace_future_version});
    }

    QCOMPARE(readAll(primary), primary_locked ? future : supported);
    QCOMPARE(readAll(backup), primary_locked ? supported : future);
    QCOMPARE(status, PreferencesSaveStatus::Failed);
    QVERIFY(!attempted_write);
    QCOMPARE(store.save({.preferences = replacement}), PreferencesSaveStatus::BlockedByFutureVersion);
    QCOMPARE(store.save({.preferences = replacement, .replace_future_version = true}), PreferencesSaveStatus::Saved);
    QCOMPARE(store.load().preferences, replacement);
}

void QtJsonWindowsPreferencesStoreTests::injectedFaultBeforeCommitPreservesThePrimary() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const Preferences original{};
    Preferences replacement = nonDefaultPreferences();
    QtJsonWindowsPreferencesStore initial{root};
    QCOMPARE(initial.save({.preferences = original}), PreferencesSaveStatus::Saved);
    const QByteArray original_bytes = readAll(preferencesPath(root));

    QtJsonWindowsPreferencesStore failing{root, {.inject_fault = [](const PreferencesStoreFaultPoint point) {
                                              return point == PreferencesStoreFaultPoint::BeforePrimaryCommit;
                                          }}};
    QCOMPARE(failing.save({.preferences = replacement}), PreferencesSaveStatus::Failed);

    QCOMPARE(readAll(preferencesPath(root)), original_bytes);
    QCOMPARE(failing.load().preferences, original);
}

void QtJsonWindowsPreferencesStoreTests::injectedFaultAfterCommitReportsFailureButLeavesAValidReplacement() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const Preferences original{};
    const Preferences replacement = nonDefaultPreferences();
    QtJsonWindowsPreferencesStore initial{root};
    QCOMPARE(initial.save({.preferences = original}), PreferencesSaveStatus::Saved);

    QtJsonWindowsPreferencesStore failing{root, {.inject_fault = [](const PreferencesStoreFaultPoint point) {
                                              return point == PreferencesStoreFaultPoint::AfterPrimaryCommit;
                                          }}};
    QCOMPARE(failing.save({.preferences = replacement}), PreferencesSaveStatus::Failed);

    const PreferencesLoadResult loaded = failing.load();
    QCOMPARE(loaded.status, PreferencesLoadStatus::Loaded);
    QCOMPARE(loaded.preferences, replacement);
    QCOMPARE(PreferencesJsonCodec::decode(readAll(backupPath(root))).preferences, std::optional<Preferences>{original});
}

void QtJsonWindowsPreferencesStoreTests::retainsExactlyOneBackupAcrossRepeatedSaves() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    QtJsonWindowsPreferencesStore store{root};
    Preferences first{};
    Preferences second = nonDefaultPreferences();
    Preferences third = second;
    third.volume = 0.55;

    QCOMPARE(store.save({.preferences = first}), PreferencesSaveStatus::Saved);
    QCOMPARE(store.save({.preferences = second}), PreferencesSaveStatus::Saved);
    QCOMPARE(store.save({.preferences = third}), PreferencesSaveStatus::Saved);

    const QStringList backups = QDir{root}.entryList({QStringLiteral("preferences.json.bak*")}, QDir::Files);
    QCOMPARE(backups, QStringList{QStringLiteral("preferences.json.bak")});
    QCOMPARE(PreferencesJsonCodec::decode(readAll(backupPath(root))).preferences, std::optional<Preferences>{second});
    QCOMPARE(store.load().preferences, third);
}

QTEST_GUILESS_MAIN(QtJsonWindowsPreferencesStoreTests)

#include "QtJsonWindowsPreferencesStoreTests.moc"
