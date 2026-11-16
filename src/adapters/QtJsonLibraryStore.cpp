#include "adapters/QtJsonLibraryStore.hpp"
#include "adapters/StrictJson.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace music_player::m0 {
namespace {
// The atomic write, backup and lock mechanics mirror QtJsonPlaylistStore.
enum class FileStatus { Missing, Valid, Invalid, FutureVersion, Unavailable };
struct InspectedFile final {
    FileStatus status{FileStatus::Missing};
    QByteArray bytes{};
    LibrarySnapshot snapshot{};
};
enum class WriteStatus { Failed, Committed, CommittedWithInjectedFailure };

QString primaryPath(const QString& directory) { return QDir(directory).filePath(QStringLiteral("library.json")); }
QString backupPath(const QString& directory) { return QDir(directory).filePath(QStringLiteral("library.json.bak")); }
QString lockPath(const QString& directory) { return QDir(directory).filePath(QStringLiteral("library.json.lock")); }

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
// Decimal strings without leading zeros keep 64-bit values exact in JSON.
std::optional<std::uint64_t> decodeDecimal(const QJsonValue& value) {
    if (!value.isString())
        return {};
    const auto text = value.toString();
    if (text.isEmpty() || text.size() > 20 || (text.front() == QLatin1Char('0') && text.size() > 1))
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
std::optional<QString> encodeText(const std::string& text) {
    const auto result = QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
    if (!scalarString(result) || result.toUtf8().toStdString() != text)
        return {};
    return result;
}
QByteArray encode(const LibrarySnapshot& snapshot) {
    if (!isValidLibrarySnapshot(snapshot))
        return {};
    QJsonArray records;
    for (const auto& record : snapshot.records) {
        const auto path = encodePath(record.source_path);
        const auto title = encodeText(record.title);
        const auto artist = encodeText(record.artist);
        const auto album = encodeText(record.album);
        if (!path || !title || !artist || !album)
            return {};
        records.push_back(QJsonObject{
            {QStringLiteral("id"), QString::number(record.id)},
            {QStringLiteral("sourcePath"), *path},
            {QStringLiteral("title"), *title},
            {QStringLiteral("artist"), *artist},
            {QStringLiteral("album"), *album},
            {QStringLiteral("durationMs"),
             record.duration_ms ? QJsonValue(QString::number(*record.duration_ms)) : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("available"), record.available}});
    }
    QJsonArray jobs;
    for (const auto& job : snapshot.download_jobs)
        jobs.push_back(QString::fromLatin1(job.data(), static_cast<qsizetype>(job.size())));
    const auto bytes = QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                                                 {QStringLiteral("nextId"), QString::number(snapshot.next_id)},
                                                 {QStringLiteral("downloadJobs"), jobs},
                                                 {QStringLiteral("records"), records}})
                           .toJson(QJsonDocument::Compact);
    return bytes.size() > QtJsonLibraryStore::maximum_file_size ? QByteArray{} : bytes;
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
    // Future schemas are opaque and never authorize overwriting either slot.
    if (version.toDouble() > 1)
        return {.status = FileStatus::FutureVersion};
    const auto next_id = decodeDecimal(root.value(QStringLiteral("nextId")));
    if (!detail::strictJsonPreflight(bytes, 4) || root.size() != 4 || !next_id ||
        !root.value(QStringLiteral("records")).isArray() || !root.value(QStringLiteral("downloadJobs")).isArray())
        return {.status = FileStatus::Invalid};
    const auto jobs = root.value(QStringLiteral("downloadJobs")).toArray();
    if (static_cast<std::size_t>(jobs.size()) > maximum_library_download_jobs)
        return {.status = FileStatus::Invalid};
    const auto records = root.value(QStringLiteral("records")).toArray();
    if (static_cast<std::size_t>(records.size()) > maximum_library_records)
        return {.status = FileStatus::Invalid};
    LibrarySnapshot snapshot;
    snapshot.next_id = *next_id;
    for (const auto& job : jobs) {
        // Domain validation below accepts printable ASCII only, so Latin-1 is exact.
        if (!job.isString())
            return {.status = FileStatus::Invalid};
        const auto text = job.toString();
        if (text.size() > static_cast<qsizetype>(maximum_library_job_id_bytes) ||
            std::any_of(text.begin(), text.end(), [](QChar c) { return c.unicode() > 0x7e; }))
            return {.status = FileStatus::Invalid};
        snapshot.download_jobs.push_back(text.toLatin1().toStdString());
    }
    snapshot.records.reserve(static_cast<std::size_t>(records.size()));
    for (const auto& value : records) {
        if (!value.isObject())
            return {.status = FileStatus::Invalid};
        const auto object = value.toObject();
        const auto id = decodeDecimal(object.value(QStringLiteral("id")));
        const auto path = decodePath(object.value(QStringLiteral("sourcePath")));
        const auto duration = object.value(QStringLiteral("durationMs"));
        const auto available = object.value(QStringLiteral("available"));
        const auto title = object.value(QStringLiteral("title"));
        const auto artist = object.value(QStringLiteral("artist"));
        const auto album = object.value(QStringLiteral("album"));
        if (object.size() != 7 || !id || !path || !title.isString() || !artist.isString() || !album.isString() ||
            !available.isBool() || (!duration.isNull() && !duration.isString()))
            return {.status = FileStatus::Invalid};
        std::optional<std::uint64_t> duration_ms;
        if (duration.isString()) {
            duration_ms = decodeDecimal(duration);
            if (!duration_ms)
                return {.status = FileStatus::Invalid};
        }
        const auto title_text = title.toString(), artist_text = artist.toString(), album_text = album.toString();
        if (!scalarString(title_text) || !scalarString(artist_text) || !scalarString(album_text))
            return {.status = FileStatus::Invalid};
        snapshot.records.push_back({*id, *path, title_text.toUtf8().toStdString(), artist_text.toUtf8().toStdString(),
                                    album_text.toUtf8().toStdString(), duration_ms, available.toBool()});
    }
    if (!isValidLibrarySnapshot(snapshot))
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
    if (information.size() > QtJsonLibraryStore::maximum_file_size)
        return {.status = FileStatus::Invalid};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {.status = FileStatus::Unavailable};
    auto bytes = file.read(QtJsonLibraryStore::maximum_file_size + 1);
    if (file.error() != QFileDevice::NoError)
        return {.status = FileStatus::Unavailable};
    if (bytes.size() > QtJsonLibraryStore::maximum_file_size)
        return {.status = FileStatus::Invalid};
    return decode(std::move(bytes));
}
bool fault(const QtJsonLibraryStoreOptions& options, const LibraryStoreFaultPoint point) noexcept {
    if (!options.inject_fault)
        return false;
    try {
        return options.inject_fault(point);
    } catch (...) {
        return true;
    }
}
WriteStatus writeAtomically(const QString& path, const QByteArray& bytes, const QtJsonLibraryStoreOptions& options,
                            const LibraryStoreFaultPoint before, const LibraryStoreFaultPoint after) {
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

QtJsonLibraryStore::QtJsonLibraryStore(QString directory, QtJsonLibraryStoreOptions options)
    : config_directory_(QDir::cleanPath(QString(directory.constData(), directory.size()))),
      options_(std::move(options)) {
    if (directory.isEmpty() || directory.contains(QChar{0}) || directory.startsWith(QLatin1Char(':')) ||
        !QDir::isAbsolutePath(directory))
        throw std::invalid_argument("library configuration directory must be an absolute file path");
}
LibraryLoadResult QtJsonLibraryStore::load() {
    const QFileInfo directory(config_directory_);
    if (directory.isSymLink() || (directory.exists() && !directory.isDir()))
        return {.status = LibraryLoadStatus::Unavailable};
    if (!directory.exists())
        return {};
    QLockFile transaction(lockPath(config_directory_));
    if (!transaction.tryLock(0))
        return {.status = LibraryLoadStatus::Unavailable};
    auto primary = inspect(primaryPath(config_directory_));
    auto backup = inspect(backupPath(config_directory_));
    if (primary.status == FileStatus::FutureVersion || backup.status == FileStatus::FutureVersion)
        return {.status = LibraryLoadStatus::FutureVersion};
    if (primary.status == FileStatus::Unavailable || backup.status == FileStatus::Unavailable)
        return {.status = LibraryLoadStatus::Unavailable};
    if (primary.status == FileStatus::Valid)
        return {.snapshot = std::move(primary.snapshot), .status = LibraryLoadStatus::Loaded};
    if (backup.status == FileStatus::Valid)
        return {.snapshot = std::move(backup.snapshot), .status = LibraryLoadStatus::RecoveredFromBackup};
    if (primary.status == FileStatus::Missing && backup.status == FileStatus::Missing)
        return {};
    return {.status = LibraryLoadStatus::Invalid};
}
LibrarySaveStatus QtJsonLibraryStore::save(const LibrarySnapshot& snapshot) {
    const auto bytes = encode(snapshot);
    if (bytes.isEmpty())
        return LibrarySaveStatus::Failed;
    const QFileInfo directory(config_directory_);
    if (directory.isSymLink() || (directory.exists() && !directory.isDir()) ||
        (!directory.exists() && !QDir{}.mkpath(config_directory_)))
        return LibrarySaveStatus::Failed;
    QLockFile transaction(lockPath(config_directory_));
    if (!transaction.tryLock(0))
        return LibrarySaveStatus::Failed;
    const auto primary = inspect(primaryPath(config_directory_));
    const auto backup = inspect(backupPath(config_directory_));
    if (primary.status == FileStatus::FutureVersion || backup.status == FileStatus::FutureVersion)
        return LibrarySaveStatus::BlockedByFutureVersion;
    if (primary.status == FileStatus::Unavailable || backup.status == FileStatus::Unavailable)
        return LibrarySaveStatus::Failed;
    // Never replace unreadable user data that no slot can still recover.
    if (primary.status != FileStatus::Valid && backup.status != FileStatus::Valid &&
        (primary.status != FileStatus::Missing || backup.status != FileStatus::Missing))
        return LibrarySaveStatus::Failed;
    if (primary.status == FileStatus::Valid &&
        writeAtomically(backupPath(config_directory_), primary.bytes, options_,
                        LibraryStoreFaultPoint::BeforeBackupCommit,
                        LibraryStoreFaultPoint::AfterBackupCommit) != WriteStatus::Committed)
        return LibrarySaveStatus::Failed;
    if (writeAtomically(primaryPath(config_directory_), bytes, options_, LibraryStoreFaultPoint::BeforePrimaryCommit,
                        LibraryStoreFaultPoint::AfterPrimaryCommit) != WriteStatus::Committed)
        return LibrarySaveStatus::Failed;
    return LibrarySaveStatus::Saved;
}
} // namespace music_player::m0
