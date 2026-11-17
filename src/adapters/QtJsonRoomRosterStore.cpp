#include "adapters/QtJsonRoomRosterStore.hpp"
#include "adapters/StrictJson.hpp"
#include "m0/domain/ListenerProfile.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>

#include <array>
#include <cmath>
#include <initializer_list>
#include <stdexcept>
#include <utility>

namespace music_player::m0 {
namespace {
enum class FileStatus { Missing, Valid, Invalid, FutureVersion, Unavailable };
struct InspectedFile final {
    FileStatus status{FileStatus::Missing};
    QByteArray bytes{};
    RoomRosterSnapshot snapshot{};
};

// Ids are JSON numbers; keep them exactly representable as doubles.
constexpr double maximum_exact_integer = 9007199254740992.0;
constexpr std::size_t id_bytes = 16;
// Stable on-disk names; independent of the RoomPermission enumerator order.
constexpr std::array<const char*, room_permission_count> permission_names = {
    "skip", "pause", "addFromRoom", "addPlaylist", "editQueue", "uploadOwnTracks", "addYoutube"};

QString primaryPath(const QString& directory) { return QDir(directory).filePath(QStringLiteral("room-roles.json")); }
QString backupPath(const QString& directory) { return QDir(directory).filePath(QStringLiteral("room-roles.json.bak")); }
QString lockPath(const QString& directory) { return QDir(directory).filePath(QStringLiteral("room-roles.json.lock")); }

QString permissionName(const RoomPermission permission) {
    return QString::fromLatin1(permission_names.at(static_cast<std::size_t>(permission)));
}
std::optional<RoomPermission> permissionFromName(const QString& name) {
    for (std::size_t i = 0; i < permission_names.size(); ++i)
        if (name == QLatin1String(permission_names[i]))
            return static_cast<RoomPermission>(i);
    return std::nullopt;
}
QString encodeBytes(const std::array<std::uint8_t, id_bytes>& bytes) {
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(bytes.data()), bytes.size())
                                   .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
std::optional<std::array<std::uint8_t, id_bytes>> decodeBytes(const QJsonValue& value) {
    if (!value.isString() || value.toString().contains(QLatin1Char('=')))
        return std::nullopt;
    const auto decoded = QByteArray::fromBase64Encoding(value.toString().toLatin1(),
                                                        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals |
                                                            QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.size() != static_cast<qsizetype>(id_bytes))
        return std::nullopt;
    std::array<std::uint8_t, id_bytes> out{};
    for (std::size_t i = 0; i < id_bytes; ++i)
        out[i] = static_cast<std::uint8_t>(decoded.decoded.at(static_cast<qsizetype>(i)));
    return out;
}
std::optional<std::uint64_t> readId(const QJsonValue& value) {
    if (!value.isDouble())
        return std::nullopt;
    const auto number = value.toDouble();
    if (!std::isfinite(number) || number < 0 || number > maximum_exact_integer || std::trunc(number) != number)
        return std::nullopt;
    return static_cast<std::uint64_t>(number);
}
bool nicknameAllowed(const std::string& nickname) {
    if (nickname.empty())
        return true;
    const auto canonical = canonicalNickname(nickname);
    return canonical.has_value() && *canonical == nickname;
}
bool fitsExactly(const std::uint64_t id) { return static_cast<double>(id) <= maximum_exact_integer; }
bool snapshotValid(const RoomRosterSnapshot& snapshot) {
    for (const auto& listener : snapshot.listeners)
        if (!nicknameAllowed(listener.last_nickname) || !fitsExactly(listener.role_id))
            return false;
    for (const auto& role : snapshot.roles)
        if (!fitsExactly(role.id))
            return false;
    if (snapshot.invite && !fitsExactly(snapshot.invite->default_role_id))
        return false;
    RoomRoster validator;
    return validator.restore(snapshot);
}

QByteArray encode(const RoomRosterSnapshot& snapshot) {
    if (!snapshotValid(snapshot))
        return {};
    QJsonArray roles;
    for (const auto& role : snapshot.roles) {
        QJsonArray permissions;
        for (std::size_t i = 0; i < room_permission_count; ++i)
            if (role.permissions.test(i))
                permissions.append(permissionName(static_cast<RoomPermission>(i)));
        roles.append(QJsonObject{{QStringLiteral("id"), static_cast<double>(role.id)},
                                 {QStringLiteral("name"), QString::fromStdString(role.name)},
                                 {QStringLiteral("permissions"), permissions}});
    }
    QJsonArray listeners;
    for (const auto& listener : snapshot.listeners) {
        QJsonArray overrides;
        for (const auto& o : listener.overrides)
            overrides.append(QJsonObject{{QStringLiteral("permission"), permissionName(o.permission)},
                                         {QStringLiteral("allowed"), o.allowed}});
        listeners.append(QJsonObject{{QStringLiteral("fingerprint"), QString::fromStdString(listener.fingerprint)},
                                     {QStringLiteral("lastNickname"), QString::fromStdString(listener.last_nickname)},
                                     {QStringLiteral("roleId"), static_cast<double>(listener.role_id)},
                                     {QStringLiteral("overrides"), overrides},
                                     {QStringLiteral("blocked"), listener.blocked}});
    }
    QJsonObject root{{QStringLiteral("schemaVersion"), 1},
                     {QStringLiteral("roles"), roles},
                     {QStringLiteral("listeners"), listeners}};
    if (snapshot.invite) {
        root.insert(
            QStringLiteral("invite"),
            QJsonObject{{QStringLiteral("roomId"), encodeBytes(snapshot.invite->room_id)},
                        {QStringLiteral("secret"), encodeBytes(snapshot.invite->secret)},
                        {QStringLiteral("maxListeners"), static_cast<double>(snapshot.invite->max_listeners)},
                        {QStringLiteral("defaultRoleId"), static_cast<double>(snapshot.invite->default_role_id)}});
    }
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    return bytes.size() > QtJsonRoomRosterStore::maximum_file_size ? QByteArray{} : bytes;
}

bool hasExactKeys(const QJsonObject& object, std::initializer_list<const char*> keys) {
    if (object.size() != static_cast<int>(keys.size()))
        return false;
    for (const auto* key : keys)
        if (!object.contains(QLatin1String(key)))
            return false;
    return true;
}
std::optional<RoomPermissionSet> readPermissions(const QJsonValue& value) {
    if (!value.isArray())
        return std::nullopt;
    RoomPermissionSet set;
    for (const auto& entry : value.toArray()) {
        const auto permission = entry.isString() ? permissionFromName(entry.toString()) : std::nullopt;
        if (!permission || set.test(static_cast<std::size_t>(*permission)))
            return std::nullopt;
        set.set(static_cast<std::size_t>(*permission));
    }
    return set;
}
bool decodeRoles(const QJsonValue& value, RoomRosterSnapshot& out) {
    if (!value.isArray())
        return false;
    for (const auto& entry : value.toArray()) {
        if (!entry.isObject())
            return false;
        const auto object = entry.toObject();
        const auto id = readId(object.value(QStringLiteral("id")));
        const auto permissions = readPermissions(object.value(QStringLiteral("permissions")));
        if (!hasExactKeys(object, {"id", "name", "permissions"}) || !id || !permissions ||
            !object.value(QStringLiteral("name")).isString())
            return false;
        out.roles.push_back({*id, object.value(QStringLiteral("name")).toString().toStdString(), *permissions});
    }
    return true;
}
bool decodeListeners(const QJsonValue& value, RoomRosterSnapshot& out) {
    if (!value.isArray())
        return false;
    for (const auto& entry : value.toArray()) {
        if (!entry.isObject())
            return false;
        const auto object = entry.toObject();
        const auto role = readId(object.value(QStringLiteral("roleId")));
        if (!hasExactKeys(object, {"fingerprint", "lastNickname", "roleId", "overrides", "blocked"}) || !role ||
            !object.value(QStringLiteral("fingerprint")).isString() ||
            !object.value(QStringLiteral("lastNickname")).isString() ||
            !object.value(QStringLiteral("blocked")).isBool() || !object.value(QStringLiteral("overrides")).isArray())
            return false;
        KnownListener listener;
        listener.fingerprint = object.value(QStringLiteral("fingerprint")).toString().toStdString();
        listener.last_nickname = object.value(QStringLiteral("lastNickname")).toString().toStdString();
        listener.role_id = *role;
        listener.blocked = object.value(QStringLiteral("blocked")).toBool();
        for (const auto& item : object.value(QStringLiteral("overrides")).toArray()) {
            if (!item.isObject())
                return false;
            const auto override_object = item.toObject();
            const auto name = override_object.value(QStringLiteral("permission"));
            const auto permission = name.isString() ? permissionFromName(name.toString()) : std::nullopt;
            if (!hasExactKeys(override_object, {"permission", "allowed"}) || !permission ||
                !override_object.value(QStringLiteral("allowed")).isBool())
                return false;
            listener.overrides.push_back({*permission, override_object.value(QStringLiteral("allowed")).toBool()});
        }
        out.listeners.push_back(std::move(listener));
    }
    return true;
}
bool decodeInvite(const QJsonValue& value, RoomRosterSnapshot& out) {
    if (!value.isObject())
        return false;
    const auto object = value.toObject();
    const auto room = decodeBytes(object.value(QStringLiteral("roomId")));
    const auto secret = decodeBytes(object.value(QStringLiteral("secret")));
    const auto maximum = readId(object.value(QStringLiteral("maxListeners")));
    const auto role = readId(object.value(QStringLiteral("defaultRoleId")));
    if (!hasExactKeys(object, {"roomId", "secret", "maxListeners", "defaultRoleId"}) || !room || !secret || !maximum ||
        !role)
        return false;
    out.invite = PersistedInvite{*room, *secret, static_cast<std::size_t>(*maximum), *role};
    return true;
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
    const bool has_invite = root.contains(QStringLiteral("invite"));
    if (!detail::strictJsonPreflight(bytes, 6) || root.size() != (has_invite ? 4 : 3))
        return {.status = FileStatus::Invalid};
    RoomRosterSnapshot snapshot;
    snapshot.roles.clear();
    if (!decodeRoles(root.value(QStringLiteral("roles")), snapshot) ||
        !decodeListeners(root.value(QStringLiteral("listeners")), snapshot) ||
        (has_invite && !decodeInvite(root.value(QStringLiteral("invite")), snapshot)) || !snapshotValid(snapshot))
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
    if (information.size() > QtJsonRoomRosterStore::maximum_file_size)
        return {.status = FileStatus::Invalid};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {.status = FileStatus::Unavailable};
    auto bytes = file.read(QtJsonRoomRosterStore::maximum_file_size + 1);
    if (file.error() != QFileDevice::NoError)
        return {.status = FileStatus::Unavailable};
    if (bytes.size() > QtJsonRoomRosterStore::maximum_file_size)
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

QtJsonRoomRosterStore::QtJsonRoomRosterStore(QString directory)
    : config_directory_(QDir::cleanPath(QString(directory.constData(), directory.size()))) {
    if (directory.isEmpty() || directory.contains(QChar{0}) || directory.startsWith(QLatin1Char(':')) ||
        !QDir::isAbsolutePath(directory))
        throw std::invalid_argument("room roster configuration directory must be an absolute file path");
}
RoomRosterLoadResult QtJsonRoomRosterStore::load() {
    const QFileInfo directory(config_directory_);
    if (directory.isSymLink() || (directory.exists() && !directory.isDir()))
        return {.status = RoomRosterLoadStatus::Unavailable};
    if (!directory.exists())
        return {};
    QLockFile transaction(lockPath(config_directory_));
    if (!transaction.tryLock(0))
        return {.status = RoomRosterLoadStatus::Unavailable};
    auto primary = inspect(primaryPath(config_directory_));
    auto backup = inspect(backupPath(config_directory_));
    if (primary.status == FileStatus::FutureVersion || backup.status == FileStatus::FutureVersion)
        return {.status = RoomRosterLoadStatus::FutureVersion};
    if (primary.status == FileStatus::Unavailable || backup.status == FileStatus::Unavailable)
        return {.status = RoomRosterLoadStatus::Unavailable};
    if (primary.status == FileStatus::Valid)
        return {.snapshot = std::move(primary.snapshot), .status = RoomRosterLoadStatus::Loaded};
    if (backup.status == FileStatus::Valid)
        return {.snapshot = std::move(backup.snapshot), .status = RoomRosterLoadStatus::RecoveredFromBackup};
    if (primary.status == FileStatus::Missing && backup.status == FileStatus::Missing)
        return {};
    return {.status = RoomRosterLoadStatus::Invalid};
}
RoomRosterSaveStatus QtJsonRoomRosterStore::save(const RoomRosterSnapshot& snapshot) {
    const auto bytes = encode(snapshot);
    if (bytes.isEmpty())
        return RoomRosterSaveStatus::Failed;
    const QFileInfo directory(config_directory_);
    if (directory.isSymLink() || (directory.exists() && !directory.isDir()) ||
        (!directory.exists() && !QDir{}.mkpath(config_directory_)))
        return RoomRosterSaveStatus::Failed;
    QLockFile transaction(lockPath(config_directory_));
    if (!transaction.tryLock(0))
        return RoomRosterSaveStatus::Failed;
    const auto primary = inspect(primaryPath(config_directory_));
    const auto backup = inspect(backupPath(config_directory_));
    if (primary.status == FileStatus::FutureVersion || backup.status == FileStatus::FutureVersion)
        return RoomRosterSaveStatus::BlockedByFutureVersion;
    if (primary.status == FileStatus::Unavailable || backup.status == FileStatus::Unavailable)
        return RoomRosterSaveStatus::Failed;
    if (primary.status != FileStatus::Valid && backup.status != FileStatus::Valid &&
        (primary.status != FileStatus::Missing || backup.status != FileStatus::Missing))
        return RoomRosterSaveStatus::Failed;
    if (primary.status == FileStatus::Valid && !writeAtomically(backupPath(config_directory_), primary.bytes))
        return RoomRosterSaveStatus::Failed;
    if (!writeAtomically(primaryPath(config_directory_), bytes))
        return RoomRosterSaveStatus::Failed;
    return RoomRosterSaveStatus::Saved;
}
} // namespace music_player::m0
