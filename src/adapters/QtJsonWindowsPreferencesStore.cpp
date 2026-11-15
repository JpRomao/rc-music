#include "adapters/QtJsonWindowsPreferencesStore.hpp"

#include "adapters/PreferencesJsonCodec.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <stdexcept>
#include <utility>

namespace music_player::m0 {
namespace {

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
    Preferences preferences{};
};

enum class AtomicWriteStatus {
    Failed,
    Committed,
    CommittedWithInjectedFailure,
};

[[nodiscard]] QString primaryPath(const QString& directory) {
    return QDir{directory}.filePath(QStringLiteral("preferences.json"));
}

[[nodiscard]] QString backupPath(const QString& directory) {
    return QDir{directory}.filePath(QStringLiteral("preferences.json.bak"));
}

[[nodiscard]] InspectedFile inspectFile(const QString& path) {
    const QFileInfo information{path};
    if (!information.exists())
        return {};
    if (!information.isFile() || information.size() < 0)
        return {.status = InspectedFileStatus::Unreadable};
    if (information.size() > QtJsonWindowsPreferencesStore::maximum_file_size)
        return {.status = InspectedFileStatus::Invalid};

    QFile file{path};
    if (!file.open(QIODevice::ReadOnly))
        return {.status = InspectedFileStatus::Unreadable};
    QByteArray bytes = file.read(QtJsonWindowsPreferencesStore::maximum_file_size + 1);
    if (file.error() != QFileDevice::NoError)
        return {.status = InspectedFileStatus::Unreadable};
    if (bytes.size() > QtJsonWindowsPreferencesStore::maximum_file_size)
        return {.status = InspectedFileStatus::Invalid};

    PreferencesJsonDecodeResult decoded = PreferencesJsonCodec::decode(bytes);
    if (decoded.status == PreferencesJsonDecodeStatus::FutureVersion)
        return {.status = InspectedFileStatus::FutureVersion, .bytes = std::move(bytes)};
    if (decoded.status != PreferencesJsonDecodeStatus::Valid || !decoded.preferences.has_value())
        return {.status = InspectedFileStatus::Invalid, .bytes = std::move(bytes)};
    return {.status = InspectedFileStatus::Valid,
            .bytes = std::move(bytes),
            .preferences = std::move(*decoded.preferences)};
}

[[nodiscard]] bool shouldInject(const QtJsonWindowsPreferencesStoreOptions& options,
                                const PreferencesStoreFaultPoint point) noexcept {
    if (!options.inject_fault)
        return false;
    try {
        return options.inject_fault(point);
    } catch (...) {
        return true;
    }
}

[[nodiscard]] AtomicWriteStatus writeAtomically(const QString& path, const QByteArray& bytes,
                                                const QtJsonWindowsPreferencesStoreOptions* options) {
    QSaveFile file{path};
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.flush()) {
        file.cancelWriting();
        return AtomicWriteStatus::Failed;
    }
    if (options != nullptr && shouldInject(*options, PreferencesStoreFaultPoint::BeforePrimaryCommit)) {
        file.cancelWriting();
        return AtomicWriteStatus::Failed;
    }
    if (!file.commit())
        return AtomicWriteStatus::Failed;
    if (options != nullptr && shouldInject(*options, PreferencesStoreFaultPoint::AfterPrimaryCommit))
        return AtomicWriteStatus::CommittedWithInjectedFailure;
    return AtomicWriteStatus::Committed;
}

} // namespace

QtJsonWindowsPreferencesStore::QtJsonWindowsPreferencesStore(QString config_directory,
                                                             QtJsonWindowsPreferencesStoreOptions options)
    : config_directory_(QDir::cleanPath(std::move(config_directory))), options_(std::move(options)) {
    if (config_directory_.isEmpty() || !QFileInfo{config_directory_}.isAbsolute())
        throw std::invalid_argument{"preferences config directory must be an absolute path"};
}

PreferencesLoadResult QtJsonWindowsPreferencesStore::load() {
    const InspectedFile primary = inspectFile(primaryPath(config_directory_));
    if (primary.status == InspectedFileStatus::FutureVersion)
        return {.status = PreferencesLoadStatus::FutureVersion};
    if (primary.status == InspectedFileStatus::Valid)
        return {.preferences = primary.preferences, .status = PreferencesLoadStatus::Loaded};

    const InspectedFile backup = inspectFile(backupPath(config_directory_));
    if (backup.status == InspectedFileStatus::FutureVersion)
        return {.status = PreferencesLoadStatus::FutureVersion};
    if (backup.status == InspectedFileStatus::Valid) {
        return {.preferences = backup.preferences, .status = PreferencesLoadStatus::RecoveredFromBackup};
    }
    if (primary.status == InspectedFileStatus::Missing && backup.status == InspectedFileStatus::Missing)
        return {.status = PreferencesLoadStatus::Missing};
    return {.status = PreferencesLoadStatus::InvalidUsingDefaults};
}

PreferencesSaveStatus QtJsonWindowsPreferencesStore::save(const PreferencesSaveRequest& request) {
    const QByteArray encoded = PreferencesJsonCodec::encode(request.preferences);
    if (encoded.isEmpty() || encoded.size() > maximum_file_size)
        return PreferencesSaveStatus::Failed;

    const InspectedFile primary = inspectFile(primaryPath(config_directory_));
    const InspectedFile backup = inspectFile(backupPath(config_directory_));
    if (primary.status == InspectedFileStatus::Unreadable || backup.status == InspectedFileStatus::Unreadable)
        return PreferencesSaveStatus::Failed;
    const bool primary_valid = primary.status == InspectedFileStatus::Valid;
    const bool backup_is_future = backup.status == InspectedFileStatus::FutureVersion;
    if ((primary.status == InspectedFileStatus::FutureVersion || backup_is_future) && !request.replace_future_version) {
        return PreferencesSaveStatus::BlockedByFutureVersion;
    }

    QDir directory{config_directory_};
    if (!directory.exists() && !QDir{}.mkpath(config_directory_))
        return PreferencesSaveStatus::Failed;

    if (primary_valid && !backup_is_future &&
        writeAtomically(backupPath(config_directory_), primary.bytes, nullptr) != AtomicWriteStatus::Committed) {
        return PreferencesSaveStatus::Failed;
    }

    const AtomicWriteStatus written = writeAtomically(primaryPath(config_directory_), encoded, &options_);
    if (written != AtomicWriteStatus::Committed)
        return PreferencesSaveStatus::Failed;
    if (backup_is_future) {
        const QByteArray& replacement_backup = primary_valid ? primary.bytes : encoded;
        if (writeAtomically(backupPath(config_directory_), replacement_backup, nullptr) != AtomicWriteStatus::Committed)
            return PreferencesSaveStatus::Failed;
    }
    return PreferencesSaveStatus::Saved;
}

} // namespace music_player::m0
