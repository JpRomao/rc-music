#include "adapters/QtJsonWindowsSessionStore.hpp"

#include "adapters/SessionJsonCodec.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QVector>
#include <QtTest>

#define NOMINMAX
#include <Windows.h>

#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

using namespace music_player::m0;
using namespace Qt::StringLiterals;

namespace {

class UniqueWindowsHandle final {
  public:
    explicit UniqueWindowsHandle(HANDLE handle) noexcept : handle_(handle) {}
    ~UniqueWindowsHandle() {
        if (isValid())
            CloseHandle(handle_);
    }

    UniqueWindowsHandle(const UniqueWindowsHandle&) = delete;
    UniqueWindowsHandle& operator=(const UniqueWindowsHandle&) = delete;

    [[nodiscard]] bool isValid() const noexcept { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }

  private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
};

[[nodiscard]] UniqueWindowsHandle denyReadSharing(const QString& path) {
    return UniqueWindowsHandle{CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, FILE_SHARE_DELETE,
                                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
}

[[nodiscard]] UniqueWindowsHandle denyReplacement(const QString& path) {
    return UniqueWindowsHandle{CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, FILE_SHARE_READ,
                                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
}

[[nodiscard]] std::filesystem::path nativePath(const QString& value) {
#ifdef _WIN32
    return std::filesystem::path{value.toStdWString()};
#else
    const QByteArray utf8 = value.toUtf8();
    return std::filesystem::path{std::string{utf8.constData(), static_cast<std::size_t>(utf8.size())}};
#endif
}

[[nodiscard]] SessionSnapshot originalSession() {
    const std::filesystem::path duplicate = nativePath(uR"(C:\Música\Abertura 日本語.mp3)"_s);
    return {
        .queue =
            {
                .items =
                    {
                        {{1}, duplicate},
                        {{9'007'199'254'740'993ULL}, duplicate},
                        {{std::numeric_limits<std::uint64_t>::max()},
                         nativePath(uR"(\\servidor\Mídia compartilhada\Final ☕.flac)"_s)},
                    },
                .current_occurrence_id = LocalQueueOccurrenceId{std::numeric_limits<std::uint64_t>::max()},
            },
        .position_ms = std::numeric_limits<std::int64_t>::max(),
    };
}

[[nodiscard]] SessionSnapshot replacementSession() {
    return {
        .queue =
            {
                .items =
                    {
                        {{42}, nativePath(uR"(C:\Música\Nova abertura.ogg)"_s)},
                        {{43}, nativePath(uR"(C:\Música\Encerramento.wav)"_s)},
                    },
                .current_occurrence_id = LocalQueueOccurrenceId{42},
            },
        .position_ms = 98'765,
    };
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

[[nodiscard]] QString sessionPath(const QString& root) { return QDir{root}.filePath(u"session.json"_s); }

[[nodiscard]] QString backupPath(const QString& root) { return QDir{root}.filePath(u"session.json.bak"_s); }

[[nodiscard]] std::optional<SessionSnapshot> decodedSnapshot(const QString& path) {
    return SessionJsonCodec::decode(readAll(path)).snapshot;
}

} // namespace

class QtJsonWindowsSessionStoreTests final : public QObject {
    Q_OBJECT

  private slots:
    void rejectsNonAbsoluteConfigurationDirectories();
    void loadsDefaultsWhenBothFilesAreAbsent();
    void roundTripsExactSessionInAUnicodeDirectory();
    void enforcesTheExactFileSizeBoundaryWithValidJson();
    void recoversFromAValidBackupWithoutRewritingDuringLoad();
    void recoversFromBackupWhenThePrimaryIsAbsent();
    void usesDefaultsWithoutRewritingWhenBothFilesAreInvalid();
    void preservesAValidBackupWhenThePrimaryIsCorrupt();
    void refusesInvalidSnapshotsWithoutMutatingFiles();
    void blocksFuturePrimaryUntilReplacementIsExplicit();
    void preservesFutureBackupWhenExplicitReplacementFailsBeforeCommit();
    void replacesFutureBackupOnlyAfterACommittedPrimary();
    void injectedFaultBeforeBackupCommitPreservesBothFiles();
    void injectedFaultAfterBackupCommitPreservesPrimaryAndRotatesBackup();
    void injectedFaultBeforeCommitPreservesThePrimary();
    void injectedFaultAfterCommitReportsFailureButLeavesAValidReplacement();
    void futureBackupRemainsWhenFailureIsInjectedAfterPrimaryCommit();
    void futureBackupCleanupFaultOccursAfterPrimaryCommit();
    void futureBackupCleanupAfterCommitReportsFailureWithTwoValidFiles();
    void exposesTheTransactionalCommitOrder();
    void faultCallbackExceptionsFailClosed();
    void copiesThePreviousPrimaryToBackupByteForByte();
    void serializesSaveTransactionsAcrossStoreInstances();
    void refusesToOverwriteAnUnreadablePrimary();
    void refusesToOverwriteAnUnreadableBackup();
    void realBackupCommitFailurePreservesBothFiles();
    void realPrimaryCommitFailurePreservesThePrimaryAndLeavesAValidBackup();
    void neverTouchesPreferencesFiles();
    void failsClosedWhenTheConfigurationRootIsAFile();
    void retainsExactlyOneBackupAcrossRepeatedSaves();
};

void QtJsonWindowsSessionStoreTests::rejectsNonAbsoluteConfigurationDirectories() {
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, QtJsonWindowsSessionStore{QString{}});
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, QtJsonWindowsSessionStore{u"relative/config"_s});
}

void QtJsonWindowsSessionStoreTests::loadsDefaultsWhenBothFilesAreAbsent() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = QDir{temporary.path()}.filePath(u"configuração 日本"_s);

    QtJsonWindowsSessionStore store{root};
    const SessionLoadResult result = store.load();

    QCOMPARE(result.status, SessionLoadStatus::Missing);
    QCOMPARE(result.snapshot, SessionSnapshot{});
    QVERIFY(!QFile::exists(sessionPath(root)));
    QVERIFY(!QFile::exists(backupPath(root)));
}

void QtJsonWindowsSessionStoreTests::roundTripsExactSessionInAUnicodeDirectory() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = QDir{temporary.path()}.filePath(u"ação 日本 ☕"_s);
    const SessionSnapshot expected = originalSession();

    QtJsonWindowsSessionStore writer{root};
    QCOMPARE(writer.save({.snapshot = expected}), SessionSaveStatus::Saved);

    QtJsonWindowsSessionStore reader{root};
    const SessionLoadResult result = reader.load();
    QCOMPARE(result.status, SessionLoadStatus::Loaded);
    QCOMPARE(result.snapshot, expected);
}

void QtJsonWindowsSessionStoreTests::enforcesTheExactFileSizeBoundaryWithValidJson() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const SessionSnapshot expected = originalSession();
    QCOMPARE(QtJsonWindowsSessionStore::maximum_file_size, SessionJsonCodec::maximum_document_size);
    QByteArray document = SessionJsonCodec::encode(expected);
    QVERIFY(document.size() < QtJsonWindowsSessionStore::maximum_file_size);
    document.append(QtJsonWindowsSessionStore::maximum_file_size - document.size(), ' ');
    QCOMPARE(document.size(), QtJsonWindowsSessionStore::maximum_file_size);
    QVERIFY(writeAll(sessionPath(root), document));

    QtJsonWindowsSessionStore store{root};
    const SessionLoadResult exact_limit = store.load();
    QCOMPARE(exact_limit.status, SessionLoadStatus::Loaded);
    QCOMPARE(exact_limit.snapshot, expected);

    document.append(' ');
    QCOMPARE(document.size(), QtJsonWindowsSessionStore::maximum_file_size + 1);
    QVERIFY(writeAll(sessionPath(root), document));
    const SessionLoadResult above_limit = store.load();
    QCOMPARE(above_limit.status, SessionLoadStatus::InvalidUsingDefaults);
    QCOMPARE(above_limit.snapshot, SessionSnapshot{});
}

void QtJsonWindowsSessionStoreTests::recoversFromAValidBackupWithoutRewritingDuringLoad() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const QByteArray corrupt{"{truncated"};
    const SessionSnapshot backup_session = originalSession();
    QVERIFY(writeAll(sessionPath(root), corrupt));
    QVERIFY(writeAll(backupPath(root), SessionJsonCodec::encode(backup_session)));

    QtJsonWindowsSessionStore store{root};
    const SessionLoadResult result = store.load();

    QCOMPARE(result.status, SessionLoadStatus::RecoveredFromBackup);
    QCOMPARE(result.snapshot, backup_session);
    QCOMPARE(readAll(sessionPath(root)), corrupt);
}

void QtJsonWindowsSessionStoreTests::recoversFromBackupWhenThePrimaryIsAbsent() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const SessionSnapshot backup_session = originalSession();
    QVERIFY(writeAll(backupPath(root), SessionJsonCodec::encode(backup_session)));

    QtJsonWindowsSessionStore store{root};
    const SessionLoadResult result = store.load();

    QCOMPARE(result.status, SessionLoadStatus::RecoveredFromBackup);
    QCOMPARE(result.snapshot, backup_session);
    QVERIFY(!QFile::exists(sessionPath(root)));
}

void QtJsonWindowsSessionStoreTests::usesDefaultsWithoutRewritingWhenBothFilesAreInvalid() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const QByteArray corrupt_primary{"{primary"};
    const QByteArray corrupt_backup{"{backup"};
    QVERIFY(writeAll(sessionPath(root), corrupt_primary));
    QVERIFY(writeAll(backupPath(root), corrupt_backup));

    QtJsonWindowsSessionStore store{root};
    const SessionLoadResult result = store.load();

    QCOMPARE(result.status, SessionLoadStatus::InvalidUsingDefaults);
    QCOMPARE(result.snapshot, SessionSnapshot{});
    QCOMPARE(readAll(sessionPath(root)), corrupt_primary);
    QCOMPARE(readAll(backupPath(root)), corrupt_backup);
}

void QtJsonWindowsSessionStoreTests::preservesAValidBackupWhenThePrimaryIsCorrupt() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const QByteArray corrupt{"not json"};
    const SessionSnapshot backup_session = originalSession();
    const SessionSnapshot replacement = replacementSession();
    const QByteArray valid_backup = SessionJsonCodec::encode(backup_session);
    QVERIFY(writeAll(sessionPath(root), corrupt));
    QVERIFY(writeAll(backupPath(root), valid_backup));

    QtJsonWindowsSessionStore store{root};
    QCOMPARE(store.save({.snapshot = replacement}), SessionSaveStatus::Saved);

    QCOMPARE(readAll(backupPath(root)), valid_backup);
    QCOMPARE(store.load().snapshot, replacement);
}

void QtJsonWindowsSessionStoreTests::refusesInvalidSnapshotsWithoutMutatingFiles() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const SessionSnapshot original = originalSession();
    QtJsonWindowsSessionStore store{root};
    QCOMPARE(store.save({.snapshot = original}), SessionSaveStatus::Saved);
    const QByteArray original_bytes = readAll(sessionPath(root));

    SessionSnapshot invalid = replacementSession();
    invalid.position_ms = -1;
    QCOMPARE(store.save({.snapshot = invalid}), SessionSaveStatus::Failed);

    QCOMPARE(readAll(sessionPath(root)), original_bytes);
    QVERIFY(!QFile::exists(backupPath(root)));
}

void QtJsonWindowsSessionStoreTests::blocksFuturePrimaryUntilReplacementIsExplicit() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const QByteArray future{"{\"schemaVersion\":2,\"future\":true}"};
    const SessionSnapshot backup_session = originalSession();
    const SessionSnapshot replacement = replacementSession();
    const QByteArray backup_bytes = SessionJsonCodec::encode(backup_session);
    QVERIFY(writeAll(sessionPath(root), future));
    QVERIFY(writeAll(backupPath(root), backup_bytes));
    QtJsonWindowsSessionStore store{root};

    const SessionLoadResult loaded = store.load();
    QCOMPARE(loaded.status, SessionLoadStatus::FutureVersion);
    QCOMPARE(loaded.snapshot, SessionSnapshot{});
    QCOMPARE(store.save({.snapshot = replacement}), SessionSaveStatus::BlockedByFutureVersion);
    QCOMPARE(readAll(sessionPath(root)), future);
    QCOMPARE(readAll(backupPath(root)), backup_bytes);

    QCOMPARE(store.save({.snapshot = replacement, .replace_future_version = true}), SessionSaveStatus::Saved);
    QCOMPARE(store.load().status, SessionLoadStatus::Loaded);
    QCOMPARE(store.load().snapshot, replacement);
    QCOMPARE(readAll(backupPath(root)), backup_bytes);
}

void QtJsonWindowsSessionStoreTests::preservesFutureBackupWhenExplicitReplacementFailsBeforeCommit() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const SessionSnapshot original = originalSession();
    const SessionSnapshot replacement = replacementSession();
    const QByteArray original_bytes = SessionJsonCodec::encode(original);
    const QByteArray future{"{\"schemaVersion\":2,\"future\":true}"};
    QVERIFY(writeAll(sessionPath(root), original_bytes));
    QVERIFY(writeAll(backupPath(root), future));

    QtJsonWindowsSessionStore failing{root, {.inject_fault = [](const SessionStoreFaultPoint point) {
                                          return point == SessionStoreFaultPoint::BeforePrimaryCommit;
                                      }}};
    QCOMPARE(failing.save({.snapshot = replacement, .replace_future_version = true}), SessionSaveStatus::Failed);

    QCOMPARE(readAll(sessionPath(root)), original_bytes);
    QCOMPARE(readAll(backupPath(root)), future);
}

void QtJsonWindowsSessionStoreTests::replacesFutureBackupOnlyAfterACommittedPrimary() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const SessionSnapshot original = originalSession();
    const SessionSnapshot replacement = replacementSession();
    QByteArray original_bytes = SessionJsonCodec::encode(original);
    QVERIFY(original_bytes.endsWith('}'));
    original_bytes.chop(1);
    original_bytes.append(",\"futureHint\":true} \r\n");
    const QByteArray future{"{\"schemaVersion\":2,\"future\":true}"};
    QVERIFY(writeAll(sessionPath(root), original_bytes));
    QVERIFY(writeAll(backupPath(root), future));
    QtJsonWindowsSessionStore store{root};

    QCOMPARE(store.load().status, SessionLoadStatus::Loaded);
    QCOMPARE(store.save({.snapshot = replacement}), SessionSaveStatus::BlockedByFutureVersion);
    QCOMPARE(readAll(sessionPath(root)), original_bytes);
    QCOMPARE(readAll(backupPath(root)), future);

    QCOMPARE(store.save({.snapshot = replacement, .replace_future_version = true}), SessionSaveStatus::Saved);
    QCOMPARE(store.load().snapshot, replacement);
    QCOMPARE(readAll(backupPath(root)), original_bytes);
}

void QtJsonWindowsSessionStoreTests::injectedFaultBeforeBackupCommitPreservesBothFiles() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const SessionSnapshot original = originalSession();
    const QByteArray original_bytes = SessionJsonCodec::encode(original);
    const QByteArray prior_backup = SessionJsonCodec::encode(SessionSnapshot{});
    QVERIFY(writeAll(sessionPath(root), original_bytes));
    QVERIFY(writeAll(backupPath(root), prior_backup));

    QtJsonWindowsSessionStore failing{root, {.inject_fault = [](const SessionStoreFaultPoint point) {
                                          return point == SessionStoreFaultPoint::BeforeBackupCommit;
                                      }}};
    QCOMPARE(failing.save({.snapshot = replacementSession()}), SessionSaveStatus::Failed);

    QCOMPARE(readAll(sessionPath(root)), original_bytes);
    QCOMPARE(readAll(backupPath(root)), prior_backup);
}

void QtJsonWindowsSessionStoreTests::injectedFaultAfterBackupCommitPreservesPrimaryAndRotatesBackup() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const SessionSnapshot original = originalSession();
    const QByteArray original_bytes = SessionJsonCodec::encode(original);
    QVERIFY(writeAll(sessionPath(root), original_bytes));
    QVERIFY(writeAll(backupPath(root), SessionJsonCodec::encode(SessionSnapshot{})));

    QtJsonWindowsSessionStore failing{root, {.inject_fault = [](const SessionStoreFaultPoint point) {
                                          return point == SessionStoreFaultPoint::AfterBackupCommit;
                                      }}};
    QCOMPARE(failing.save({.snapshot = replacementSession()}), SessionSaveStatus::Failed);

    QCOMPARE(readAll(sessionPath(root)), original_bytes);
    QCOMPARE(readAll(backupPath(root)), original_bytes);
}

void QtJsonWindowsSessionStoreTests::injectedFaultBeforeCommitPreservesThePrimary() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const SessionSnapshot original = originalSession();
    const SessionSnapshot replacement = replacementSession();
    QtJsonWindowsSessionStore initial{root};
    QCOMPARE(initial.save({.snapshot = original}), SessionSaveStatus::Saved);
    const QByteArray original_bytes = readAll(sessionPath(root));

    QtJsonWindowsSessionStore failing{root, {.inject_fault = [](const SessionStoreFaultPoint point) {
                                          return point == SessionStoreFaultPoint::BeforePrimaryCommit;
                                      }}};
    QCOMPARE(failing.save({.snapshot = replacement}), SessionSaveStatus::Failed);

    QCOMPARE(readAll(sessionPath(root)), original_bytes);
    QCOMPARE(failing.load().snapshot, original);
}

void QtJsonWindowsSessionStoreTests::injectedFaultAfterCommitReportsFailureButLeavesAValidReplacement() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const SessionSnapshot original = originalSession();
    const SessionSnapshot replacement = replacementSession();
    QtJsonWindowsSessionStore initial{root};
    QCOMPARE(initial.save({.snapshot = original}), SessionSaveStatus::Saved);

    QtJsonWindowsSessionStore failing{root, {.inject_fault = [](const SessionStoreFaultPoint point) {
                                          return point == SessionStoreFaultPoint::AfterPrimaryCommit;
                                      }}};
    QCOMPARE(failing.save({.snapshot = replacement}), SessionSaveStatus::Failed);

    const SessionLoadResult loaded = failing.load();
    QCOMPARE(loaded.status, SessionLoadStatus::Loaded);
    QCOMPARE(loaded.snapshot, replacement);
    QCOMPARE(decodedSnapshot(backupPath(root)), std::optional<SessionSnapshot>{original});
}

void QtJsonWindowsSessionStoreTests::futureBackupRemainsWhenFailureIsInjectedAfterPrimaryCommit() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const SessionSnapshot original = originalSession();
    const SessionSnapshot replacement = replacementSession();
    const QByteArray future{"{\"schemaVersion\":2,\"future\":true}"};
    QVERIFY(writeAll(sessionPath(root), SessionJsonCodec::encode(original)));
    QVERIFY(writeAll(backupPath(root), future));

    QtJsonWindowsSessionStore failing{root, {.inject_fault = [](const SessionStoreFaultPoint point) {
                                          return point == SessionStoreFaultPoint::AfterPrimaryCommit;
                                      }}};
    QCOMPARE(failing.save({.snapshot = replacement, .replace_future_version = true}), SessionSaveStatus::Failed);

    QCOMPARE(decodedSnapshot(sessionPath(root)), std::optional<SessionSnapshot>{replacement});
    QCOMPARE(readAll(backupPath(root)), future);
}

void QtJsonWindowsSessionStoreTests::futureBackupCleanupFaultOccursAfterPrimaryCommit() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const QByteArray future{"{\"schemaVersion\":2,\"future\":true}"};
    const SessionSnapshot replacement = replacementSession();
    QVERIFY(writeAll(backupPath(root), future));

    QtJsonWindowsSessionStore failing{root, {.inject_fault = [](const SessionStoreFaultPoint point) {
                                          return point == SessionStoreFaultPoint::BeforeBackupCommit;
                                      }}};
    QCOMPARE(failing.load().status, SessionLoadStatus::FutureVersion);
    QCOMPARE(failing.save({.snapshot = replacement, .replace_future_version = true}), SessionSaveStatus::Failed);

    QCOMPARE(decodedSnapshot(sessionPath(root)), std::optional<SessionSnapshot>{replacement});
    QCOMPARE(readAll(backupPath(root)), future);
}

void QtJsonWindowsSessionStoreTests::futureBackupCleanupAfterCommitReportsFailureWithTwoValidFiles() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const QByteArray future{"{\"schemaVersion\":2,\"future\":true}"};
    const SessionSnapshot replacement = replacementSession();
    QVERIFY(writeAll(backupPath(root), future));

    QtJsonWindowsSessionStore failing{root, {.inject_fault = [](const SessionStoreFaultPoint point) {
                                          return point == SessionStoreFaultPoint::AfterBackupCommit;
                                      }}};
    QCOMPARE(failing.save({.snapshot = replacement, .replace_future_version = true}), SessionSaveStatus::Failed);

    QCOMPARE(decodedSnapshot(sessionPath(root)), std::optional<SessionSnapshot>{replacement});
    QCOMPARE(decodedSnapshot(backupPath(root)), std::optional<SessionSnapshot>{replacement});
}

void QtJsonWindowsSessionStoreTests::exposesTheTransactionalCommitOrder() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString normal_root = QDir{temporary.path()}.filePath(u"normal"_s);
    QVERIFY(QDir{}.mkpath(normal_root));
    QVERIFY(writeAll(sessionPath(normal_root), SessionJsonCodec::encode(originalSession())));
    QVector<SessionStoreFaultPoint> normal_points;
    QtJsonWindowsSessionStore normal{normal_root,
                                     {.inject_fault = [&normal_points](const SessionStoreFaultPoint point) {
                                         normal_points.push_back(point);
                                         return false;
                                     }}};
    QCOMPARE(normal.save({.snapshot = replacementSession()}), SessionSaveStatus::Saved);
    const QVector<SessionStoreFaultPoint> expected_normal_points{
        SessionStoreFaultPoint::BeforeBackupCommit,
        SessionStoreFaultPoint::AfterBackupCommit,
        SessionStoreFaultPoint::BeforePrimaryCommit,
        SessionStoreFaultPoint::AfterPrimaryCommit,
    };
    QCOMPARE(normal_points, expected_normal_points);

    const QString future_root = QDir{temporary.path()}.filePath(u"future"_s);
    QVERIFY(QDir{}.mkpath(future_root));
    QVERIFY(writeAll(sessionPath(future_root), SessionJsonCodec::encode(originalSession())));
    QVERIFY(writeAll(backupPath(future_root), QByteArray{"{\"schemaVersion\":2}"}));
    QVector<SessionStoreFaultPoint> future_points;
    QtJsonWindowsSessionStore future{future_root,
                                     {.inject_fault = [&future_points](const SessionStoreFaultPoint point) {
                                         future_points.push_back(point);
                                         return false;
                                     }}};
    QCOMPARE(future.save({.snapshot = replacementSession(), .replace_future_version = true}), SessionSaveStatus::Saved);
    const QVector<SessionStoreFaultPoint> expected_future_points{
        SessionStoreFaultPoint::BeforePrimaryCommit,
        SessionStoreFaultPoint::AfterPrimaryCommit,
        SessionStoreFaultPoint::BeforeBackupCommit,
        SessionStoreFaultPoint::AfterBackupCommit,
    };
    QCOMPARE(future_points, expected_future_points);
}

void QtJsonWindowsSessionStoreTests::faultCallbackExceptionsFailClosed() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();

    QtJsonWindowsSessionStore failing{
        root, {.inject_fault = [](SessionStoreFaultPoint) -> bool { throw std::runtime_error{"fault callback"}; }}};
    QCOMPARE(failing.save({.snapshot = replacementSession()}), SessionSaveStatus::Failed);

    QVERIFY(!QFile::exists(sessionPath(root)));
    QVERIFY(!QFile::exists(backupPath(root)));
}

void QtJsonWindowsSessionStoreTests::copiesThePreviousPrimaryToBackupByteForByte() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    QByteArray decorated = SessionJsonCodec::encode(originalSession());
    QVERIFY(decorated.endsWith('}'));
    decorated.chop(1);
    decorated.append(",\"futureHint\":{\"keep\":true}} \r\n");
    QVERIFY(writeAll(sessionPath(root), decorated));
    QCOMPARE(SessionJsonCodec::decode(decorated).status, SessionJsonDecodeStatus::Valid);

    QtJsonWindowsSessionStore store{root};
    QCOMPARE(store.save({.snapshot = replacementSession()}), SessionSaveStatus::Saved);

    QCOMPARE(readAll(backupPath(root)), decorated);
}

void QtJsonWindowsSessionStoreTests::serializesSaveTransactionsAcrossStoreInstances() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    QtJsonWindowsSessionStore initial{root};
    QCOMPARE(initial.save({.snapshot = originalSession()}), SessionSaveStatus::Saved);

    QtJsonWindowsSessionStore competing{root};
    std::optional<SessionSaveStatus> competing_status;
    QtJsonWindowsSessionStore active{
        root,
        {.inject_fault =
             [&](const SessionStoreFaultPoint point) {
                 if (point == SessionStoreFaultPoint::BeforeBackupCommit && !competing_status.has_value())
                     competing_status = competing.save({.snapshot = SessionSnapshot{}});
                 return false;
             }},
    };

    QCOMPARE(active.save({.snapshot = replacementSession()}), SessionSaveStatus::Saved);
    QCOMPARE(competing_status, std::optional<SessionSaveStatus>{SessionSaveStatus::Failed});
    QCOMPARE(active.load().snapshot, replacementSession());
    QVERIFY(!QFile::exists(QDir{root}.filePath(u"session.json.lock"_s)));
}

void QtJsonWindowsSessionStoreTests::refusesToOverwriteAnUnreadablePrimary() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const QByteArray original_bytes = SessionJsonCodec::encode(originalSession());
    QVERIFY(writeAll(sessionPath(root), original_bytes));

    SessionSaveStatus status = SessionSaveStatus::Saved;
    bool attempted_write = false;
    {
        const UniqueWindowsHandle denied = denyReadSharing(sessionPath(root));
        QVERIFY(denied.isValid());
        QtJsonWindowsSessionStore store{root, {.inject_fault = [&attempted_write](SessionStoreFaultPoint) {
                                            attempted_write = true;
                                            return false;
                                        }}};
        status = store.save({.snapshot = replacementSession()});
    }

    QCOMPARE(status, SessionSaveStatus::Failed);
    QVERIFY(!attempted_write);
    QCOMPARE(readAll(sessionPath(root)), original_bytes);
    QVERIFY(!QFile::exists(backupPath(root)));
}

void QtJsonWindowsSessionStoreTests::refusesToOverwriteAnUnreadableBackup() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const QByteArray original_bytes = SessionJsonCodec::encode(originalSession());
    const QByteArray future{"{\"schemaVersion\":2,\"future\":true}"};
    QVERIFY(writeAll(sessionPath(root), original_bytes));
    QVERIFY(writeAll(backupPath(root), future));

    SessionSaveStatus status = SessionSaveStatus::Saved;
    bool attempted_write = false;
    {
        const UniqueWindowsHandle denied = denyReadSharing(backupPath(root));
        QVERIFY(denied.isValid());
        QtJsonWindowsSessionStore store{root, {.inject_fault = [&attempted_write](SessionStoreFaultPoint) {
                                            attempted_write = true;
                                            return false;
                                        }}};
        status = store.save({.snapshot = replacementSession()});
    }

    QCOMPARE(status, SessionSaveStatus::Failed);
    QVERIFY(!attempted_write);
    QCOMPARE(readAll(sessionPath(root)), original_bytes);
    QCOMPARE(readAll(backupPath(root)), future);
}

void QtJsonWindowsSessionStoreTests::realBackupCommitFailurePreservesBothFiles() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const QByteArray original_bytes = SessionJsonCodec::encode(originalSession());
    const QByteArray backup_bytes = SessionJsonCodec::encode(SessionSnapshot{});
    QVERIFY(writeAll(sessionPath(root), original_bytes));
    QVERIFY(writeAll(backupPath(root), backup_bytes));

    SessionSaveStatus status = SessionSaveStatus::Saved;
    {
        const UniqueWindowsHandle denied = denyReplacement(backupPath(root));
        QVERIFY(denied.isValid());
        QtJsonWindowsSessionStore store{root};
        status = store.save({.snapshot = replacementSession()});
    }

    QCOMPARE(status, SessionSaveStatus::Failed);
    QCOMPARE(readAll(sessionPath(root)), original_bytes);
    QCOMPARE(readAll(backupPath(root)), backup_bytes);
}

void QtJsonWindowsSessionStoreTests::realPrimaryCommitFailurePreservesThePrimaryAndLeavesAValidBackup() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const QByteArray original_bytes = SessionJsonCodec::encode(originalSession());
    QVERIFY(writeAll(sessionPath(root), original_bytes));

    SessionSaveStatus status = SessionSaveStatus::Saved;
    {
        const UniqueWindowsHandle denied = denyReplacement(sessionPath(root));
        QVERIFY(denied.isValid());
        QtJsonWindowsSessionStore store{root};
        status = store.save({.snapshot = replacementSession()});
    }

    QCOMPARE(status, SessionSaveStatus::Failed);
    QCOMPARE(readAll(sessionPath(root)), original_bytes);
    QCOMPARE(readAll(backupPath(root)), original_bytes);
}

void QtJsonWindowsSessionStoreTests::neverTouchesPreferencesFiles() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const QString preferences = QDir{root}.filePath(u"preferences.json"_s);
    const QString preferences_backup = QDir{root}.filePath(u"preferences.json.bak"_s);
    const QByteArray primary_bytes{"user preferences"};
    const QByteArray backup_bytes{"older user preferences"};
    QVERIFY(writeAll(preferences, primary_bytes));
    QVERIFY(writeAll(preferences_backup, backup_bytes));

    QtJsonWindowsSessionStore store{root};
    QCOMPARE(store.save({.snapshot = replacementSession()}), SessionSaveStatus::Saved);

    QCOMPARE(readAll(preferences), primary_bytes);
    QCOMPARE(readAll(preferences_backup), backup_bytes);
}

void QtJsonWindowsSessionStoreTests::failsClosedWhenTheConfigurationRootIsAFile() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = QDir{temporary.path()}.filePath(u"config-as-file"_s);
    const QByteArray sentinel{"do not overwrite"};
    QVERIFY(writeAll(root, sentinel));

    QtJsonWindowsSessionStore store{root};
    QCOMPARE(store.save({.snapshot = replacementSession()}), SessionSaveStatus::Failed);

    QCOMPARE(readAll(root), sentinel);
}

void QtJsonWindowsSessionStoreTests::retainsExactlyOneBackupAcrossRepeatedSaves() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    QtJsonWindowsSessionStore store{root};
    const SessionSnapshot first{};
    const SessionSnapshot second = originalSession();
    const SessionSnapshot third = replacementSession();

    QCOMPARE(store.save({.snapshot = first}), SessionSaveStatus::Saved);
    QCOMPARE(store.save({.snapshot = second}), SessionSaveStatus::Saved);
    QCOMPARE(store.save({.snapshot = third}), SessionSaveStatus::Saved);

    const QStringList backups = QDir{root}.entryList({u"session.json.bak*"_s}, QDir::Files);
    QCOMPARE(backups, QStringList{u"session.json.bak"_s});
    QCOMPARE(decodedSnapshot(backupPath(root)), std::optional<SessionSnapshot>{second});
    QCOMPARE(store.load().snapshot, third);
}

QTEST_GUILESS_MAIN(QtJsonWindowsSessionStoreTests)

#include "QtJsonWindowsSessionStoreTests.moc"
