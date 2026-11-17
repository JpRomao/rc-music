#include "adapters/QtJsonProfileStore.hpp"
#include "adapters/StrictJson.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>

#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>

namespace music_player::m0 {
namespace {
enum class FileStatus { Missing, Valid, Invalid, FutureVersion, Unavailable };
struct InspectedFile final {
    FileStatus status{FileStatus::Missing};
    QByteArray bytes{};
    ListenerProfile profile{};
};

QString primaryPath(const QString& directory) { return QDir(directory).filePath(QStringLiteral("profile.json")); }
QString backupPath(const QString& directory) { return QDir(directory).filePath(QStringLiteral("profile.json.bak")); }
QString lockPath(const QString& directory) { return QDir(directory).filePath(QStringLiteral("profile.json.lock")); }

QByteArray encode(const ListenerProfile& profile) {
    if (!isValidListenerProfile(profile))
        return {};
    QJsonObject avatar;
    if (profile.avatar.kind == AvatarKind::Bundled) {
        avatar = {{QStringLiteral("kind"), QStringLiteral("bundled")},
                  {QStringLiteral("index"), static_cast<int>(profile.avatar.bundled_index)}};
    } else {
        avatar = {{QStringLiteral("kind"), QStringLiteral("custom")},
                  {QStringLiteral("sha256"), QString::fromStdString(profile.avatar.custom_sha256)}};
    }
    const auto bytes = QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                                                 {QStringLiteral("nickname"), QString::fromStdString(profile.nickname)},
                                                 {QStringLiteral("avatar"), avatar}})
                           .toJson(QJsonDocument::Compact);
    return bytes.size() > QtJsonProfileStore::maximum_file_size ? QByteArray{} : bytes;
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
    // Future schemas are opaque; observing one never authorizes overwriting either slot.
    if (version.toDouble() > 1)
        return {.status = FileStatus::FutureVersion};
    if (!detail::strictJsonPreflight(bytes, 3) || root.size() != 3 ||
        !root.value(QStringLiteral("nickname")).isString() || !root.value(QStringLiteral("avatar")).isObject())
        return {.status = FileStatus::Invalid};
    const auto avatar = root.value(QStringLiteral("avatar")).toObject();
    const auto kind = avatar.value(QStringLiteral("kind")).toString();
    ListenerProfile profile;
    profile.nickname = root.value(QStringLiteral("nickname")).toString().toStdString();
    if (kind == QLatin1String("bundled")) {
        const auto index = avatar.value(QStringLiteral("index"));
        if (avatar.size() != 2 || !index.isDouble() || index.toDouble() < 0 ||
            index.toDouble() >= bundled_avatar_count || std::trunc(index.toDouble()) != index.toDouble())
            return {.status = FileStatus::Invalid};
        profile.avatar = {AvatarKind::Bundled, static_cast<std::uint32_t>(index.toDouble()), {}};
    } else if (kind == QLatin1String("custom")) {
        const auto hash = avatar.value(QStringLiteral("sha256"));
        if (avatar.size() != 2 || !hash.isString())
            return {.status = FileStatus::Invalid};
        profile.avatar = {AvatarKind::Custom, 0, hash.toString().toStdString()};
    } else {
        return {.status = FileStatus::Invalid};
    }
    if (!isValidListenerProfile(profile))
        return {.status = FileStatus::Invalid};
    return {.status = FileStatus::Valid, .bytes = std::move(bytes), .profile = std::move(profile)};
}
InspectedFile inspect(const QString& path) {
    const QFileInfo information(path);
    if (information.isSymLink())
        return {.status = FileStatus::Unavailable};
    if (!information.exists())
        return {};
    if (!information.isFile() || information.size() < 0)
        return {.status = FileStatus::Unavailable};
    if (information.size() > QtJsonProfileStore::maximum_file_size)
        return {.status = FileStatus::Invalid};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {.status = FileStatus::Unavailable};
    auto bytes = file.read(QtJsonProfileStore::maximum_file_size + 1);
    if (file.error() != QFileDevice::NoError)
        return {.status = FileStatus::Unavailable};
    if (bytes.size() > QtJsonProfileStore::maximum_file_size)
        return {.status = FileStatus::Invalid};
    return decode(std::move(bytes));
}
bool writeAtomically(const QString& path, const QByteArray& bytes) {
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.flush()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}
} // namespace

QtJsonProfileStore::QtJsonProfileStore(QString directory)
    : config_directory_(QDir::cleanPath(QString(directory.constData(), directory.size()))) {
    if (directory.isEmpty() || directory.contains(QChar{0}) || directory.startsWith(QLatin1Char(':')) ||
        !QDir::isAbsolutePath(directory))
        throw std::invalid_argument("profile configuration directory must be an absolute file path");
}
ProfileLoadResult QtJsonProfileStore::load() {
    const QFileInfo directory(config_directory_);
    if (directory.isSymLink() || (directory.exists() && !directory.isDir()))
        return {.status = ProfileLoadStatus::Unavailable};
    if (!directory.exists())
        return {};
    QLockFile transaction(lockPath(config_directory_));
    if (!transaction.tryLock(0))
        return {.status = ProfileLoadStatus::Unavailable};
    auto primary = inspect(primaryPath(config_directory_));
    auto backup = inspect(backupPath(config_directory_));
    if (primary.status == FileStatus::FutureVersion || backup.status == FileStatus::FutureVersion)
        return {.status = ProfileLoadStatus::FutureVersion};
    if (primary.status == FileStatus::Unavailable || backup.status == FileStatus::Unavailable)
        return {.status = ProfileLoadStatus::Unavailable};
    if (primary.status == FileStatus::Valid)
        return {.profile = std::move(primary.profile), .status = ProfileLoadStatus::Loaded};
    if (backup.status == FileStatus::Valid)
        return {.profile = std::move(backup.profile), .status = ProfileLoadStatus::RecoveredFromBackup};
    if (primary.status == FileStatus::Missing && backup.status == FileStatus::Missing)
        return {};
    return {.status = ProfileLoadStatus::Invalid};
}
ProfileSaveStatus QtJsonProfileStore::save(const ListenerProfile& profile) {
    const auto bytes = encode(profile);
    if (bytes.isEmpty())
        return ProfileSaveStatus::Failed;
    const QFileInfo directory(config_directory_);
    if (directory.isSymLink() || (directory.exists() && !directory.isDir()) ||
        (!directory.exists() && !QDir{}.mkpath(config_directory_)))
        return ProfileSaveStatus::Failed;
    QLockFile transaction(lockPath(config_directory_));
    if (!transaction.tryLock(0))
        return ProfileSaveStatus::Failed;
    const auto primary = inspect(primaryPath(config_directory_));
    const auto backup = inspect(backupPath(config_directory_));
    if (primary.status == FileStatus::FutureVersion || backup.status == FileStatus::FutureVersion)
        return ProfileSaveStatus::BlockedByFutureVersion;
    if (primary.status == FileStatus::Unavailable || backup.status == FileStatus::Unavailable)
        return ProfileSaveStatus::Failed;
    if (primary.status != FileStatus::Valid && backup.status != FileStatus::Valid &&
        (primary.status != FileStatus::Missing || backup.status != FileStatus::Missing))
        return ProfileSaveStatus::Failed;
    if (primary.status == FileStatus::Valid && !writeAtomically(backupPath(config_directory_), primary.bytes))
        return ProfileSaveStatus::Failed;
    if (!writeAtomically(primaryPath(config_directory_), bytes))
        return ProfileSaveStatus::Failed;
    return ProfileSaveStatus::Saved;
}
} // namespace music_player::m0
