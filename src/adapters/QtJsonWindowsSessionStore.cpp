#include "adapters/QtJsonWindowsSessionStore.hpp"

#include "adapters/SessionJsonCodec.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>

#include <stdexcept>
#include <utility>

namespace music_player::m0 {
namespace {

static_assert(QtJsonWindowsSessionStore::maximum_file_size == SessionJsonCodec::maximum_document_size);

enum class InspectedFileStatus {
    Missing,
    Valid,
    Invalid,
    Unreadable,
    FutureVersion,
};

struct InspectedFile final {
    InspectedFileStatus status{InspectedFileStatus::Missing};
    QByteArray bytes{};
    SessionSnapshot snapshot{};
};

enum class AtomicWriteStatus {
    Failed,
    Committed,
    CommittedWithInjectedFailure,
};

[[nodiscard]] QString primaryPath(const QString& directory) {
    return QDir{directory}.filePath(QStringLiteral("session.json"));
}

[[nodiscard]] QString backupPath(const QString& directory) {
    return QDir{directory}.filePath(QStringLiteral("session.json.bak"));
}

[[nodiscard]] QString lockPath(const QString& directory) {
    return QDir{directory}.filePath(QStringLiteral("session.json.lock"));
}

[[nodiscard]] InspectedFile inspectFile(const QString& path) {
    const QFileInfo information{path};
    if (!information.exists())
        return {};
    if (!information.isFile() || information.size() < 0)
        return {.status = InspectedFileStatus::Unreadable};
    if (information.size() > QtJsonWindowsSessionStore::maximum_file_size)
        return {.status = InspectedFileStatus::Invalid};

    QFile file{path};
    if (!file.open(QIODevice::ReadOnly))
        return {.status = InspectedFileStatus::Unreadable};
    QByteArray bytes = file.read(QtJsonWindowsSessionStore::maximum_file_size + 1);
    if (file.error() != QFileDevice::NoError)
        return {.status = InspectedFileStatus::Unreadable};
    if (bytes.size() > QtJsonWindowsSessionStore::maximum_file_size)
        return {.status = InspectedFileStatus::Invalid};

    SessionJsonDecodeResult decoded = SessionJsonCodec::decode(bytes);
    if (decoded.status == SessionJsonDecodeStatus::FutureVersion)
        return {.status = InspectedFileStatus::FutureVersion, .bytes = std::move(bytes)};
    if (decoded.status != SessionJsonDecodeStatus::Valid || !decoded.snapshot.has_value())
        return {.status = InspectedFileStatus::Invalid, .bytes = std::move(bytes)};
    return {
        .status = InspectedFileStatus::Valid,
        .bytes = std::move(bytes),
        .snapshot = std::move(*decoded.snapshot),
    };
}

[[nodiscard]] bool shouldInject(const QtJsonWindowsSessionStoreOptions& options,
                                const SessionStoreFaultPoint point) noexcept {
    if (!options.inject_fault)
        return false;
    try {
        return options.inject_fault(point);
    } catch (...) {
        return true;
    }
}

[[nodiscard]] AtomicWriteStatus writeAtomically(const QString& path, const QByteArray& bytes,
                                                const QtJsonWindowsSessionStoreOptions& options,
                                                const SessionStoreFaultPoint before_commit,
                                                const SessionStoreFaultPoint after_commit) {
    QSaveFile file{path};
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.flush()) {
        file.cancelWriting();
        return AtomicWriteStatus::Failed;
    }
    if (shouldInject(options, before_commit)) {
        file.cancelWriting();
        return AtomicWriteStatus::Failed;
    }
    if (!file.commit())
        return AtomicWriteStatus::Failed;
    if (shouldInject(options, after_commit))
        return AtomicWriteStatus::CommittedWithInjectedFailure;
    return AtomicWriteStatus::Committed;
}

[[nodiscard]] AtomicWriteStatus writeBackup(const QString& directory, const QByteArray& bytes,
                                            const QtJsonWindowsSessionStoreOptions& options) {
    return writeAtomically(backupPath(directory), bytes, options, SessionStoreFaultPoint::BeforeBackupCommit,
                           SessionStoreFaultPoint::AfterBackupCommit);
}

[[nodiscard]] AtomicWriteStatus writePrimary(const QString& directory, const QByteArray& bytes,
                                             const QtJsonWindowsSessionStoreOptions& options) {
    return writeAtomically(primaryPath(directory), bytes, options, SessionStoreFaultPoint::BeforePrimaryCommit,
                           SessionStoreFaultPoint::AfterPrimaryCommit);
}

} // namespace

QtJsonWindowsSessionStore::QtJsonWindowsSessionStore(QString config_directory, QtJsonWindowsSessionStoreOptions options)
    : config_directory_(QDir::cleanPath(std::move(config_directory))), options_(std::move(options)) {
    if (config_directory_.isEmpty() || !QFileInfo{config_directory_}.isAbsolute())
        throw std::invalid_argument{"session config directory must be an absolute path"};
}

SessionLoadResult QtJsonWindowsSessionStore::load() {
    const InspectedFile primary = inspectFile(primaryPath(config_directory_));
    if (primary.status == InspectedFileStatus::FutureVersion)
        return {.status = SessionLoadStatus::FutureVersion};
    if (primary.status == InspectedFileStatus::Valid)
        return {.snapshot = primary.snapshot, .status = SessionLoadStatus::Loaded};

    const InspectedFile backup = inspectFile(backupPath(config_directory_));
    if (backup.status == InspectedFileStatus::FutureVersion)
        return {.status = SessionLoadStatus::FutureVersion};
    if (backup.status == InspectedFileStatus::Valid)
        return {.snapshot = backup.snapshot, .status = SessionLoadStatus::RecoveredFromBackup};
    if (primary.status == InspectedFileStatus::Missing && backup.status == InspectedFileStatus::Missing)
        return {.status = SessionLoadStatus::Missing};
    return {.status = SessionLoadStatus::InvalidUsingDefaults};
}

SessionSaveStatus QtJsonWindowsSessionStore::save(const SessionSaveRequest& request) {
    const QByteArray encoded = SessionJsonCodec::encode(request.snapshot);
    if (encoded.isEmpty() || encoded.size() > maximum_file_size)
        return SessionSaveStatus::Failed;

    const QFileInfo directory_information{config_directory_};
    if (directory_information.exists() && !directory_information.isDir())
        return SessionSaveStatus::Failed;
    if (!directory_information.exists() && !QDir{}.mkpath(config_directory_))
        return SessionSaveStatus::Failed;

    QLockFile transaction_lock{lockPath(config_directory_)};
    if (!transaction_lock.tryLock(0))
        return SessionSaveStatus::Failed;

    const InspectedFile primary = inspectFile(primaryPath(config_directory_));
    const InspectedFile backup = inspectFile(backupPath(config_directory_));
    if (primary.status == InspectedFileStatus::Unreadable || backup.status == InspectedFileStatus::Unreadable)
        return SessionSaveStatus::Failed;
    const bool primary_is_future = primary.status == InspectedFileStatus::FutureVersion;
    const bool backup_is_future = backup.status == InspectedFileStatus::FutureVersion;
    if ((primary_is_future || backup_is_future) && !request.replace_future_version)
        return SessionSaveStatus::BlockedByFutureVersion;

    // A future backup remains byte-for-byte intact until a replacement primary has committed.
    if (primary.status == InspectedFileStatus::Valid && !backup_is_future &&
        writeBackup(config_directory_, primary.bytes, options_) != AtomicWriteStatus::Committed) {
        return SessionSaveStatus::Failed;
    }

    if (writePrimary(config_directory_, encoded, options_) != AtomicWriteStatus::Committed)
        return SessionSaveStatus::Failed;

    if (backup_is_future) {
        const QByteArray& replacement_backup = primary.status == InspectedFileStatus::Valid ? primary.bytes : encoded;
        if (writeBackup(config_directory_, replacement_backup, options_) != AtomicWriteStatus::Committed)
            return SessionSaveStatus::Failed;
    }

    return SessionSaveStatus::Saved;
}

} // namespace music_player::m0
