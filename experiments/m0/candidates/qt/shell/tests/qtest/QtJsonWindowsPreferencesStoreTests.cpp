#include "adapters/QtJsonWindowsPreferencesStore.hpp"

#include "adapters/PreferencesJsonCodec.hpp"

#include "m0/domain/WindowPlacement.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

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
    const Preferences expected = nonDefaultPreferences();

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
