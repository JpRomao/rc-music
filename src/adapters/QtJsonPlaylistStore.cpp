#include "adapters/QtJsonPlaylistStore.hpp"
#include "adapters/StrictJson.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>

#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace music_player::m0 {
namespace {
enum class FileStatus { Missing, Valid, Invalid, FutureVersion, Unavailable };
struct InspectedFile final {
    FileStatus status{FileStatus::Missing};
    QByteArray bytes{};
    PlaylistLibrarySnapshot snapshot{};
};
enum class WriteStatus { Failed, Committed, CommittedWithInjectedFailure };

QString primaryPath(const QString& directory) { return QDir(directory).filePath(QStringLiteral("playlists.json")); }
QString backupPath(const QString& directory) { return QDir(directory).filePath(QStringLiteral("playlists.json.bak")); }
QString lockPath(const QString& directory) { return QDir(directory).filePath(QStringLiteral("playlists.json.lock")); }

bool scalarString(const QString& value) {
    for (qsizetype index = 0; index < value.size(); ++index) {
        const auto unit = value.at(index).unicode();
        if (unit >= 0xdc00 && unit <= 0xdfff)
            return false;
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (++index == value.size())
                return false;
            const auto next = value.at(index).unicode();
            if (next < 0xdc00 || next > 0xdfff)
                return false;
        }
    }
    return true;
}
std::optional<QString> encodePath(const std::filesystem::path& path) {
    const auto& native = path.native();
#ifdef _WIN32
    const auto result = QString::fromWCharArray(native.data(), static_cast<qsizetype>(native.size()));
#else
    const auto result = QString::fromUtf8(native.data(), static_cast<qsizetype>(native.size()));
    if (result.toUtf8() != QByteArray(native.data(), static_cast<qsizetype>(native.size())))
        return {};
#endif
    if (result.isEmpty() || result.contains(QChar{0}) || !scalarString(result))
        return {};
    return result;
}
std::optional<std::filesystem::path> decodePath(const QJsonValue& value) {
    if (!value.isString())
        return {};
    const auto text = value.toString();
    if (text.isEmpty() || text.contains(QChar{0}) || !scalarString(text))
        return {};
#ifdef _WIN32
    return std::filesystem::path(text.toStdWString());
#else
    const auto bytes = text.toUtf8();
    return std::filesystem::path(std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())));
#endif
}
std::optional<std::uint64_t> decodeId(const QJsonValue& value) {
    if (!value.isString())
        return {};
    const auto text = value.toString();
    if (text.isEmpty() || text.size() > 20 || text.front() == QLatin1Char('0'))
        return {};
    std::uint64_t result{};
    for (const auto unit : text) {
        if (unit < QLatin1Char('0') || unit > QLatin1Char('9'))
            return {};
        const auto digit = static_cast<std::uint64_t>(unit.unicode() - u'0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
            return {};
        result = result * 10 + digit;
    }
    return result;
}
QByteArray encode(const PlaylistLibrarySnapshot& snapshot) {
    if (!isValidPlaylistLibrarySnapshot(snapshot))
        return {};
    QJsonArray playlists;
    for (const auto& playlist : snapshot.playlists) {
        const auto name = QString::fromUtf8(playlist.name.data(), static_cast<qsizetype>(playlist.name.size()));
        if (!scalarString(name) || name.toUtf8().toStdString() != playlist.name)
            return {};
        QJsonArray entries;
        for (const auto& entry : playlist.entries) {
            const auto path = encodePath(entry.source_path);
            if (!path)
                return {};
            entries.push_back(
                QJsonObject{{QStringLiteral("id"), QString::number(entry.id)}, {QStringLiteral("sourcePath"), *path}});
        }
        playlists.push_back(QJsonObject{{QStringLiteral("id"), QString::number(playlist.id)},
                                        {QStringLiteral("name"), name},
                                        {QStringLiteral("entries"), entries}});
    }
    const auto bytes =
        QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("playlists"), playlists}})
            .toJson(QJsonDocument::Compact);
    return bytes.size() > QtJsonPlaylistStore::maximum_file_size ? QByteArray{} : bytes;
}
InspectedFile decode(QByteArray bytes) {
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return {.status = FileStatus::Invalid};
    const auto root = document.object();
    const auto version = root.value(QStringLiteral("schemaVersion"));
    if (!version.isDouble() || !std::isfinite(version.toDouble()) || version.toDouble() <= 0 ||
        std::trunc(version.toDouble()) != version.toDouble())
        return {.status = FileStatus::Invalid};
    // Future schemas are opaque. Their shape/depth is not constrained by v1,
    // and observing one never authorizes overwriting either document slot.
    if (version.toDouble() > 1)
        return {.status = FileStatus::FutureVersion};
    if (!detail::strictJsonPreflight(bytes, 5) || root.size() != 2 ||
        !root.value(QStringLiteral("playlists")).isArray())
        return {.status = FileStatus::Invalid};
    const auto playlists = root.value(QStringLiteral("playlists")).toArray();
    if (static_cast<std::size_t>(playlists.size()) > maximum_named_playlists)
        return {.status = FileStatus::Invalid};
    PlaylistLibrarySnapshot snapshot;
    snapshot.playlists.reserve(static_cast<std::size_t>(playlists.size()));
    std::size_t entry_count{};
    for (const auto& playlist_value : playlists) {
        if (!playlist_value.isObject())
            return {.status = FileStatus::Invalid};
        const auto object = playlist_value.toObject();
        const auto id = decodeId(object.value(QStringLiteral("id")));
        const auto name_value = object.value(QStringLiteral("name"));
        if (object.size() != 3 || !id || !name_value.isString() || !object.value(QStringLiteral("entries")).isArray())
            return {.status = FileStatus::Invalid};
        const auto name = name_value.toString();
        if (!scalarString(name))
            return {.status = FileStatus::Invalid};
        const auto entries = object.value(QStringLiteral("entries")).toArray();
        const auto size = static_cast<std::size_t>(entries.size());
        if (size > maximum_playlist_entries - entry_count)
            return {.status = FileStatus::Invalid};
        entry_count += size;
        NamedPlaylist playlist{*id, name.toUtf8().toStdString(), {}};
        playlist.entries.reserve(size);
        for (const auto& entry_value : entries) {
            if (!entry_value.isObject())
                return {.status = FileStatus::Invalid};
            const auto entry = entry_value.toObject();
            const auto entry_id = decodeId(entry.value(QStringLiteral("id")));
            const auto path = decodePath(entry.value(QStringLiteral("sourcePath")));
            if (entry.size() != 2 || !entry_id || !path)
                return {.status = FileStatus::Invalid};
            playlist.entries.push_back({*entry_id, *path});
        }
        snapshot.playlists.push_back(std::move(playlist));
    }
    if (!isValidPlaylistLibrarySnapshot(snapshot))
        return {.status = FileStatus::Invalid};
    return {.status = FileStatus::Valid, .bytes = std::move(bytes), .snapshot = std::move(snapshot)};
}
InspectedFile inspect(const QString& path) {
    const QFileInfo information(path);
    if (information.isSymLink())
        return {.status = FileStatus::Unavailable};
    if (!information.exists())
        return {};
    if (!information.isFile() || information.size() < 0)
        return {.status = FileStatus::Unavailable};
    if (information.size() > QtJsonPlaylistStore::maximum_file_size)
        return {.status = FileStatus::Invalid};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {.status = FileStatus::Unavailable};
    auto bytes = file.read(QtJsonPlaylistStore::maximum_file_size + 1);
    if (file.error() != QFileDevice::NoError)
        return {.status = FileStatus::Unavailable};
    if (bytes.size() > QtJsonPlaylistStore::maximum_file_size)
        return {.status = FileStatus::Invalid};
    return decode(std::move(bytes));
}
bool fault(const QtJsonPlaylistStoreOptions& options, const PlaylistStoreFaultPoint point) noexcept {
    if (!options.inject_fault)
        return false;
    try {
        return options.inject_fault(point);
    } catch (...) {
        return true;
    }
}
WriteStatus writeAtomically(const QString& path, const QByteArray& bytes, const QtJsonPlaylistStoreOptions& options,
                            const PlaylistStoreFaultPoint before, const PlaylistStoreFaultPoint after) {
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.flush() ||
        fault(options, before)) {
        file.cancelWriting();
        return WriteStatus::Failed;
    }
    if (!file.commit())
        return WriteStatus::Failed;
    return fault(options, after) ? WriteStatus::CommittedWithInjectedFailure : WriteStatus::Committed;
}
} // namespace

QtJsonPlaylistStore::QtJsonPlaylistStore(QString directory, QtJsonPlaylistStoreOptions options)
    : config_directory_(QDir::cleanPath(QString(directory.constData(), directory.size()))),
      options_(std::move(options)) {
    if (directory.isEmpty() || directory.contains(QChar{0}) || directory.startsWith(QLatin1Char(':')) ||
        !QDir::isAbsolutePath(directory))
        throw std::invalid_argument("playlist configuration directory must be an absolute file path");
}
PlaylistLoadResult QtJsonPlaylistStore::load() {
    const QFileInfo directory(config_directory_);
    if (directory.isSymLink() || (directory.exists() && !directory.isDir()))
        return {.status = PlaylistLoadStatus::Unavailable};
    if (!directory.exists())
        return {};
    QLockFile transaction(lockPath(config_directory_));
    if (!transaction.tryLock(0))
        return {.status = PlaylistLoadStatus::Unavailable};
    auto primary = inspect(primaryPath(config_directory_));
    auto backup = inspect(backupPath(config_directory_));
    if (primary.status == FileStatus::FutureVersion || backup.status == FileStatus::FutureVersion)
        return {.status = PlaylistLoadStatus::FutureVersion};
    if (primary.status == FileStatus::Unavailable || backup.status == FileStatus::Unavailable)
        return {.status = PlaylistLoadStatus::Unavailable};
    if (primary.status == FileStatus::Valid)
        return {.snapshot = std::move(primary.snapshot), .status = PlaylistLoadStatus::Loaded};
    if (backup.status == FileStatus::Valid)
        return {.snapshot = std::move(backup.snapshot), .status = PlaylistLoadStatus::RecoveredFromBackup};
    if (primary.status == FileStatus::Missing && backup.status == FileStatus::Missing)
        return {};
    return {.status = PlaylistLoadStatus::Invalid};
}
PlaylistSaveStatus QtJsonPlaylistStore::save(const PlaylistLibrarySnapshot& snapshot) {
    const auto bytes = encode(snapshot);
    if (bytes.isEmpty())
        return PlaylistSaveStatus::Failed;
    const QFileInfo directory(config_directory_);
    if (directory.isSymLink() || (directory.exists() && !directory.isDir()) ||
        (!directory.exists() && !QDir{}.mkpath(config_directory_)))
        return PlaylistSaveStatus::Failed;
    QLockFile transaction(lockPath(config_directory_));
    if (!transaction.tryLock(0))
        return PlaylistSaveStatus::Failed;
    const auto primary = inspect(primaryPath(config_directory_));
    const auto backup = inspect(backupPath(config_directory_));
    if (primary.status == FileStatus::FutureVersion || backup.status == FileStatus::FutureVersion)
        return PlaylistSaveStatus::BlockedByFutureVersion;
    if (primary.status == FileStatus::Unavailable || backup.status == FileStatus::Unavailable)
        return PlaylistSaveStatus::Failed;
    if (primary.status != FileStatus::Valid && backup.status != FileStatus::Valid &&
        (primary.status != FileStatus::Missing || backup.status != FileStatus::Missing))
        return PlaylistSaveStatus::Failed;
    if (primary.status == FileStatus::Valid &&
        writeAtomically(backupPath(config_directory_), primary.bytes, options_,
                        PlaylistStoreFaultPoint::BeforeBackupCommit,
                        PlaylistStoreFaultPoint::AfterBackupCommit) != WriteStatus::Committed)
        return PlaylistSaveStatus::Failed;
    if (writeAtomically(primaryPath(config_directory_), bytes, options_, PlaylistStoreFaultPoint::BeforePrimaryCommit,
                        PlaylistStoreFaultPoint::AfterPrimaryCommit) != WriteStatus::Committed)
        return PlaylistSaveStatus::Failed;
    return PlaylistSaveStatus::Saved;
}
} // namespace music_player::m0
