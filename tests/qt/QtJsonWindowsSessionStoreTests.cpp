#include "adapters/QtJsonWindowsSessionStore.hpp"

#include "adapters/SessionJsonCodec.hpp"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QVector>
#include <QtTest>

#define NOMINMAX
#include <Windows.h>

#include <cstdio>
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

class RunningProcessGuard final {
  public:
    explicit RunningProcessGuard(QProcess& process) noexcept : process_(process) {}
    ~RunningProcessGuard() {
        if (process_.state() == QProcess::ProcessState::NotRunning)
            return;
        process_.kill();
        process_.waitForFinished(5'000);
    }

    RunningProcessGuard(const RunningProcessGuard&) = delete;
    RunningProcessGuard& operator=(const RunningProcessGuard&) = delete;

  private:
    QProcess& process_;
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

[[nodiscard]] QString lockPath(const QString& root) { return QDir{root}.filePath(u"session.json.lock"_s); }

[[nodiscard]] std::optional<SessionSnapshot> decodedSnapshot(const QString& path) {
    return SessionJsonCodec::decode(readAll(path)).snapshot;
}

[[nodiscard]] std::optional<SessionStoreFaultPoint> lockHolderFaultPoint(const QString& name) {
    if (name == u"before-backup"_s)
        return SessionStoreFaultPoint::BeforeBackupCommit;
    if (name == u"after-primary"_s)
        return SessionStoreFaultPoint::AfterPrimaryCommit;
    return std::nullopt;
}

[[nodiscard]] bool isProtocolLine(const QByteArray& line, const QByteArray& token) {
    return line == token + '\n' || line == token + "\r\n";
}

[[nodiscard]] int runSessionLockHolder(const QStringList& arguments) {
    if (arguments.size() != 4 || arguments.at(1) != u"--session-store-lock-holder"_s)
        return 2;
    const QString root = QDir::cleanPath(arguments.at(2));
    const std::optional<SessionStoreFaultPoint> target = lockHolderFaultPoint(arguments.at(3));
    if (!QDir::isAbsolutePath(root) || !target.has_value())
        return 2;

    QFile standard_input;
    QFile standard_output;
    if (!standard_input.open(stdin, QIODevice::ReadOnly) || !standard_output.open(stdout, QIODevice::WriteOnly))
        return 3;

    bool reached_barrier = false;
    bool barrier_succeeded = false;
    QtJsonWindowsSessionStore store{
        root,
        {.inject_fault =
             [&](const SessionStoreFaultPoint point) {
                 if (point != *target || reached_barrier)
                     return false;
                 reached_barrier = true;
                 static constexpr char ready[] = "LOCKED\n";
                 static constexpr qint64 ready_size = static_cast<qint64>(sizeof(ready) - 1);
                 const bool signaled =
                     standard_output.write(ready, ready_size) == ready_size && standard_output.flush();
                 barrier_succeeded = signaled && isProtocolLine(standard_input.readLine(), QByteArray{"RELEASE"});
                 return !barrier_succeeded;
             }},
    };
    const SessionSaveStatus status = store.save({.snapshot = replacementSession()});
    return reached_barrier && barrier_succeeded && status == SessionSaveStatus::Saved ? 0 : 4;
}

[[nodiscard]] QByteArray waitForProcessLine(QProcess& process, const qint64 timeout_ms) {
    QDeadlineTimer deadline{timeout_ms};
    while (!process.canReadLine() && process.state() != QProcess::ProcessState::NotRunning && !deadline.hasExpired()) {
        const qint64 remaining = deadline.remainingTime();
        if (remaining <= 0 || !process.waitForReadyRead(static_cast<int>(remaining)))
            break;
    }
    return process.canReadLine() ? process.readLine() : QByteArray{};
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
    void serializesSaveTransactionsAcrossProcesses_data();
    void serializesSaveTransactionsAcrossProcesses();
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

void QtJsonWindowsSessionStoreTests::serializesSaveTransactionsAcrossProcesses_data() {
    QTest::addColumn<QString>("fault_point");
    QTest::addColumn<bool>("primary_committed");

    QTest::newRow("before-backup-commit") << u"before-backup"_s << false;
    QTest::newRow("after-primary-commit") << u"after-primary"_s << true;
}

void QtJsonWindowsSessionStoreTests::serializesSaveTransactionsAcrossProcesses() {
    QFETCH(QString, fault_point);
    QFETCH(bool, primary_committed);

    QTemporaryDir temporary{QDir::tempPath() + u"/music player lock 日本-XXXXXX"_s};
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    const SessionSnapshot initial_snapshot = originalSession();
    const SessionSnapshot holder_snapshot = replacementSession();
    QtJsonWindowsSessionStore initial{root};
    QCOMPARE(initial.save({.snapshot = initial_snapshot}), SessionSaveStatus::Saved);

    QProcess holder;
    RunningProcessGuard holder_cleanup{holder};
    holder.setProcessChannelMode(QProcess::SeparateChannels);
    holder.setProgram(QCoreApplication::applicationFilePath());
    holder.setArguments({u"--session-store-lock-holder"_s, root, fault_point});
    holder.start();
    QVERIFY2(holder.waitForStarted(5'000), qPrintable(holder.errorString()));
    const QByteArray ready_line = waitForProcessLine(holder, 5'000);
    const QByteArray ready_diagnostics = holder.readAllStandardError();
    QVERIFY2(isProtocolLine(ready_line, QByteArray{"LOCKED"}), ready_diagnostics.constData());

    QLockFile observed_lock{lockPath(root)};
    qint64 owner_pid = 0;
    QString owner_host;
    QString owner_application;
    QVERIFY(observed_lock.getLockInfo(&owner_pid, &owner_host, &owner_application));
    QCOMPARE(owner_pid, holder.processId());

    if (primary_committed) {
        QCOMPARE(decodedSnapshot(sessionPath(root)), std::optional<SessionSnapshot>{holder_snapshot});
        QCOMPARE(decodedSnapshot(backupPath(root)), std::optional<SessionSnapshot>{initial_snapshot});
    } else {
        QCOMPARE(decodedSnapshot(sessionPath(root)), std::optional<SessionSnapshot>{initial_snapshot});
        QVERIFY(!QFile::exists(backupPath(root)));
    }
    const QByteArray primary_before_contention = readAll(sessionPath(root));
    const bool backup_existed_before_contention = QFile::exists(backupPath(root));
    const QByteArray backup_before_contention = readAll(backupPath(root));

    QtJsonWindowsSessionStore competing{root};
    QCOMPARE(competing.save({.snapshot = SessionSnapshot{}}), SessionSaveStatus::Failed);
    QCOMPARE(readAll(sessionPath(root)), primary_before_contention);
    QCOMPARE(QFile::exists(backupPath(root)), backup_existed_before_contention);
    QCOMPARE(readAll(backupPath(root)), backup_before_contention);

    const QByteArray release{"RELEASE\n"};
    QCOMPARE(holder.write(release), static_cast<qint64>(release.size()));
    if (holder.bytesToWrite() > 0)
        QVERIFY(holder.waitForBytesWritten(5'000));
    holder.closeWriteChannel();
    if (!holder.waitForFinished(5'000)) {
        holder.kill();
        holder.waitForFinished(5'000);
        QFAIL("the session lock holder did not finish after release");
    }
    const QByteArray holder_diagnostics = holder.readAllStandardError();
    QCOMPARE(holder.exitStatus(), QProcess::ExitStatus::NormalExit);
    QVERIFY2(holder.exitCode() == 0, holder_diagnostics.constData());

    QtJsonWindowsSessionStore after_release{root};
    QCOMPARE(after_release.load().snapshot, holder_snapshot);
    QCOMPARE(after_release.save({.snapshot = SessionSnapshot{}}), SessionSaveStatus::Saved);
    QCOMPARE(after_release.load().snapshot, SessionSnapshot{});
    QVERIFY(!QFile::exists(lockPath(root)));
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

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setAttribute(Qt::AA_Use96Dpi, true);
    if (application.arguments().size() > 1 && application.arguments().at(1) == u"--session-store-lock-holder"_s) {
        return runSessionLockHolder(application.arguments());
    }
    QtJsonWindowsSessionStoreTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "QtJsonWindowsSessionStoreTests.moc"
