#include "adapters/QtJsonLyricsVariantsStore.hpp"
#include "adapters/StrictJson.hpp"

#include <QCryptographicHash>
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
    LyricsVariants variants{};
};
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
std::optional<QString> trackText(const std::filesystem::path& path) {
    if (!path.is_absolute() || path.native().size() > 32'768)
        return {};
#ifdef _WIN32
    const auto value = QString::fromStdWString(path.native());
#else
    const auto bytes = QByteArray(path.native().data(), static_cast<qsizetype>(path.native().size()));
    const auto value = QString::fromUtf8(bytes);
    if (value.toUtf8() != bytes)
        return {};
#endif
    if (value.isEmpty() || value.contains(QChar{0}) || !scalarString(value) || !QDir::isAbsolutePath(value))
        return {};
    return value;
}
QString primaryPath(const QString& directory, const QString& track) {
    const auto hash = QCryptographicHash::hash(track.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QDir(directory).filePath(QString::fromLatin1(hash) + QStringLiteral(".json"));
}
bool availableDirectory(const QString& path) {
    const QFileInfo info(path);
    return !info.isSymLink() && (!info.exists() || info.isDir());
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
std::optional<std::string> string(const QJsonValue& value) {
    if (!value.isString() || !scalarString(value.toString()))
        return {};
    return value.toString().toUtf8().toStdString();
}
QByteArray encode(const QString& track, const LyricsVariants& variants) {
    if (!isValidLyricsVariants(variants))
        return {};
    QJsonArray array;
    for (const auto& variant : variants) {
        array.push_back(QJsonObject{{QStringLiteral("id"), QString::number(variant.id)},
                                    {QStringLiteral("name"), QString::fromUtf8(variant.name)},
                                    {QStringLiteral("language"), QString::fromUtf8(variant.language)},
                                    {QStringLiteral("script"), QString::fromUtf8(variant.script)},
                                    {QStringLiteral("source"), QString::fromUtf8(variant.source)},
                                    {QStringLiteral("content"), QString::fromUtf8(variant.content)},
                                    {QStringLiteral("kind"), variant.kind == LyricsVariantKind::Original
                                                                 ? QStringLiteral("original")
                                                                 : QStringLiteral("romanized")},
                                    {QStringLiteral("generated"), variant.generated}});
    }
    const auto bytes = QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                                                 {QStringLiteral("sourcePath"), track},
                                                 {QStringLiteral("variants"), array}})
                           .toJson(QJsonDocument::Compact);
    return bytes.size() > QtJsonLyricsVariantsStore::maximum_file_size ? QByteArray{} : bytes;
}
InspectedFile decode(QByteArray bytes, const QString& track) {
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return {.status = FileStatus::Invalid};
    const auto root = document.object();
    const auto version = root.value(QStringLiteral("schemaVersion"));
    if (!version.isDouble() || !std::isfinite(version.toDouble()) || version.toDouble() <= 0 ||
        std::trunc(version.toDouble()) != version.toDouble())
        return {.status = FileStatus::Invalid};
    // A future schema is opaque; it never grants overwrite authority.
    if (version.toDouble() > 1)
        return {.status = FileStatus::FutureVersion};
    if (!detail::strictJsonPreflight(bytes) || root.size() != 3 ||
        !root.value(QStringLiteral("sourcePath")).isString() ||
        root.value(QStringLiteral("sourcePath")).toString() != track ||
        !root.value(QStringLiteral("variants")).isArray())
        return {.status = FileStatus::Invalid};
    const auto array = root.value(QStringLiteral("variants")).toArray();
    if (static_cast<std::size_t>(array.size()) > maximum_lyrics_variants)
        return {.status = FileStatus::Invalid};
    LyricsVariants variants;
    variants.reserve(static_cast<std::size_t>(array.size()));
    for (const auto& value : array) {
        if (!value.isObject())
            return {.status = FileStatus::Invalid};
        const auto object = value.toObject();
        const auto id = decodeId(object.value(QStringLiteral("id")));
        const auto name = string(object.value(QStringLiteral("name")));
        const auto language = string(object.value(QStringLiteral("language")));
        const auto script = string(object.value(QStringLiteral("script")));
        const auto source = string(object.value(QStringLiteral("source")));
        const auto content = string(object.value(QStringLiteral("content")));
        const auto kind = object.value(QStringLiteral("kind"));
        const auto generated = object.value(QStringLiteral("generated"));
        if (object.size() != 8 || !id || !name || !language || !script || !source || !content || !kind.isString() ||
            (kind.toString() != QLatin1String("original") && kind.toString() != QLatin1String("romanized")) ||
            !generated.isBool())
            return {.status = FileStatus::Invalid};
        variants.push_back(
            {*id, *name, *language, *script, *source, *content,
             kind.toString() == QLatin1String("original") ? LyricsVariantKind::Original : LyricsVariantKind::Romanized,
             generated.toBool()});
    }
    if (!isValidLyricsVariants(variants))
        return {.status = FileStatus::Invalid};
    return {.status = FileStatus::Valid, .bytes = std::move(bytes), .variants = std::move(variants)};
}
InspectedFile inspect(const QString& path, const QString& track) {
    const QFileInfo information(path);
    if (information.isSymLink())
        return {.status = FileStatus::Unavailable};
    if (!information.exists())
        return {};
    if (!information.isFile() || information.size() < 0)
        return {.status = FileStatus::Unavailable};
    if (information.size() > QtJsonLyricsVariantsStore::maximum_file_size)
        return {.status = FileStatus::Invalid};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {.status = FileStatus::Unavailable};
    auto bytes = file.read(QtJsonLyricsVariantsStore::maximum_file_size + 1);
    if (file.error() != QFileDevice::NoError)
        return {.status = FileStatus::Unavailable};
    if (bytes.size() > QtJsonLyricsVariantsStore::maximum_file_size)
        return {.status = FileStatus::Invalid};
    return decode(std::move(bytes), track);
}
bool fault(const QtJsonLyricsVariantsStoreOptions& options, const LyricsVariantsStoreFaultPoint point) noexcept {
    if (!options.inject_fault)
        return false;
    try {
        return options.inject_fault(point);
    } catch (...) {
        return true;
    }
}
bool writeAtomically(const QString& path, const QByteArray& bytes, const QtJsonLyricsVariantsStoreOptions& options,
                     const LyricsVariantsStoreFaultPoint before, const LyricsVariantsStoreFaultPoint after) {
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.flush() ||
        fault(options, before)) {
        file.cancelWriting();
        return false;
    }
    return file.commit() && !fault(options, after);
}
} // namespace
QtJsonLyricsVariantsStore::QtJsonLyricsVariantsStore(QString directory, QtJsonLyricsVariantsStoreOptions options)
    : config_directory_(QDir::cleanPath(QString(directory.constData(), directory.size()))),
      options_(std::move(options)) {
    if (directory.isEmpty() || directory.contains(QChar{0}) || directory.startsWith(QLatin1Char(':')) ||
        !QDir::isAbsolutePath(directory))
        throw std::invalid_argument("lyrics variants configuration directory must be an absolute file path");
}
LyricsVariantsLoadResult QtJsonLyricsVariantsStore::load(const std::filesystem::path& track) {
    const auto identity = trackText(track);
    const auto directory = QDir(config_directory_).filePath(QStringLiteral("lyrics"));
    if (!identity || !availableDirectory(config_directory_) || !availableDirectory(directory))
        return {.status = LyricsVariantsLoadStatus::Unavailable};
    if (!QFileInfo::exists(directory))
        return {};
    const auto primary_path = primaryPath(directory, *identity);
    QLockFile transaction(primary_path + QStringLiteral(".lock"));
    if (!transaction.tryLock(0))
        return {.status = LyricsVariantsLoadStatus::Unavailable};
    auto primary = inspect(primary_path, *identity);
    auto backup = inspect(primary_path + QStringLiteral(".bak"), *identity);
    if (primary.status == FileStatus::FutureVersion || backup.status == FileStatus::FutureVersion)
        return {.status = LyricsVariantsLoadStatus::FutureVersion};
    if (primary.status == FileStatus::Unavailable || backup.status == FileStatus::Unavailable)
        return {.status = LyricsVariantsLoadStatus::Unavailable};
    if (primary.status == FileStatus::Valid)
        return {.variants = std::move(primary.variants), .status = LyricsVariantsLoadStatus::Loaded};
    if (backup.status == FileStatus::Valid)
        return {.variants = std::move(backup.variants), .status = LyricsVariantsLoadStatus::RecoveredFromBackup};
    if (primary.status == FileStatus::Missing && backup.status == FileStatus::Missing)
        return {};
    return {.status = LyricsVariantsLoadStatus::Invalid};
}
LyricsVariantsSaveStatus QtJsonLyricsVariantsStore::save(const std::filesystem::path& track,
                                                         const LyricsVariants& variants) {
    const auto identity = trackText(track);
    if (!identity)
        return LyricsVariantsSaveStatus::Failed;
    const auto bytes = encode(*identity, variants);
    if (bytes.isEmpty())
        return LyricsVariantsSaveStatus::Failed;
    const auto directory = QDir(config_directory_).filePath(QStringLiteral("lyrics"));
    if (!availableDirectory(config_directory_) || !availableDirectory(directory) ||
        (!QFileInfo::exists(directory) && !QDir{}.mkpath(directory)))
        return LyricsVariantsSaveStatus::Failed;
    const auto primary_path = primaryPath(directory, *identity);
    QLockFile transaction(primary_path + QStringLiteral(".lock"));
    if (!transaction.tryLock(0))
        return LyricsVariantsSaveStatus::Failed;
    const auto primary = inspect(primary_path, *identity);
    const auto backup = inspect(primary_path + QStringLiteral(".bak"), *identity);
    if (primary.status == FileStatus::FutureVersion || backup.status == FileStatus::FutureVersion)
        return LyricsVariantsSaveStatus::BlockedByFutureVersion;
    if (primary.status == FileStatus::Unavailable || backup.status == FileStatus::Unavailable)
        return LyricsVariantsSaveStatus::Failed;
    if (primary.status != FileStatus::Valid && backup.status != FileStatus::Valid &&
        (primary.status != FileStatus::Missing || backup.status != FileStatus::Missing))
        return LyricsVariantsSaveStatus::Failed;
    if (primary.status == FileStatus::Valid &&
        !writeAtomically(primary_path + QStringLiteral(".bak"), primary.bytes, options_,
                         LyricsVariantsStoreFaultPoint::BeforeBackupCommit,
                         LyricsVariantsStoreFaultPoint::AfterBackupCommit))
        return LyricsVariantsSaveStatus::Failed;
    if (!writeAtomically(primary_path, bytes, options_, LyricsVariantsStoreFaultPoint::BeforePrimaryCommit,
                         LyricsVariantsStoreFaultPoint::AfterPrimaryCommit))
        return LyricsVariantsSaveStatus::Failed;
    return LyricsVariantsSaveStatus::Saved;
}
} // namespace music_player::m0
