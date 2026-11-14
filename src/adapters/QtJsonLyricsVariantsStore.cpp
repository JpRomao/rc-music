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

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace music_player::m0 {
namespace {
enum class FileStatus { Missing, Valid, Invalid, FutureVersion, Unavailable };
constexpr int initial_schema_version = 1;
constexpr int translation_schema_version = 2;
constexpr int selection_schema_version = 3;
struct InspectedFile final {
    FileStatus status{FileStatus::Missing};
    QByteArray bytes{};
    LyricsVariants variants{};
    LyricsSelection selection{};
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
std::optional<LyricsSelection> decodeSelection(const QJsonValue& value) {
    if (value == QStringLiteral("source"))
        return LyricsSelection{.kind = LyricsSelectionKind::Source};
    if (value == QStringLiteral("hidden"))
        return LyricsSelection{.kind = LyricsSelectionKind::Hidden};
    const auto id = decodeId(value);
    if (id)
        return LyricsSelection{.kind = LyricsSelectionKind::Variant, .variant_id = *id};
    return {};
}
LyricsSelection availableSelection(const LyricsSelection selection, const LyricsVariants& variants) {
    if (selection.kind == LyricsSelectionKind::Variant &&
        std::ranges::find(variants, selection.variant_id, &LyricsVariant::id) == variants.end())
        return {};
    return selection;
}
QByteArray encode(const QString& track, const LyricsVariants& variants, const LyricsSelection selection) {
    QJsonArray array;
    auto version = initial_schema_version;
    for (const auto& variant : variants) {
        if (variant.kind == LyricsVariantKind::Translation)
            version = translation_schema_version;
        array.push_back(QJsonObject{{QStringLiteral("id"), QString::number(variant.id)},
                                    {QStringLiteral("name"), QString::fromUtf8(variant.name)},
                                    {QStringLiteral("language"), QString::fromUtf8(variant.language)},
                                    {QStringLiteral("script"), QString::fromUtf8(variant.script)},
                                    {QStringLiteral("source"), QString::fromUtf8(variant.source)},
                                    {QStringLiteral("content"), QString::fromUtf8(variant.content)},
                                    {QStringLiteral("kind"), QString::fromUtf8(lyricsVariantKindName(variant.kind))},
                                    {QStringLiteral("generated"), variant.generated}});
    }
    QJsonObject document{{QStringLiteral("schemaVersion"), version},
                         {QStringLiteral("sourcePath"), track},
                         {QStringLiteral("variants"), array}};
    if (selection.kind != LyricsSelectionKind::Automatic) {
        document.insert(QStringLiteral("schemaVersion"), selection_schema_version);
        const auto encoded_selection = selection.kind == LyricsSelectionKind::Source ? QStringLiteral("source")
                                       : selection.kind == LyricsSelectionKind::Hidden
                                           ? QStringLiteral("hidden")
                                           : QString::number(selection.variant_id);
        document.insert(QStringLiteral("selection"), encoded_selection);
    }
    const auto bytes = QJsonDocument(document).toJson(QJsonDocument::Compact);
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
    if (version.toDouble() > selection_schema_version)
        return {.status = FileStatus::FutureVersion};
    const bool supports_translations = version.toInt() >= translation_schema_version;
    const bool has_selection = version.toInt() == selection_schema_version;
    if (!detail::strictJsonPreflight(bytes) || root.size() != (has_selection ? 4 : 3) ||
        !root.value(QStringLiteral("sourcePath")).isString() ||
        root.value(QStringLiteral("sourcePath")).toString() != track ||
        !root.value(QStringLiteral("variants")).isArray())
        return {.status = FileStatus::Invalid};
    const auto selection =
        has_selection ? decodeSelection(root.value(QStringLiteral("selection"))) : std::optional{LyricsSelection{}};
    if (!selection)
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
        const auto kind_name = string(object.value(QStringLiteral("kind")));
        const auto kind = kind_name ? lyricsVariantKindFromName(*kind_name) : std::nullopt;
        const auto generated = object.value(QStringLiteral("generated"));
        if (object.size() != 8 || !id || !name || !language || !script || !source || !content || !kind ||
            (!supports_translations && *kind == LyricsVariantKind::Translation) || !generated.isBool())
            return {.status = FileStatus::Invalid};
        variants.push_back({*id, *name, *language, *script, *source, *content, *kind, generated.toBool()});
    }
    if (!isValidLyricsVariants(variants))
        return {.status = FileStatus::Invalid};
    const auto available = availableSelection(*selection, variants);
    return {.status = FileStatus::Valid,
            .bytes = std::move(bytes),
            .variants = std::move(variants),
            .selection = available};
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
        return {.variants = std::move(primary.variants),
                .status = LyricsVariantsLoadStatus::Loaded,
                .selection = primary.selection};
    if (backup.status == FileStatus::Valid)
        return {.variants = std::move(backup.variants),
                .status = LyricsVariantsLoadStatus::RecoveredFromBackup,
                .selection = backup.selection};
    if (primary.status == FileStatus::Missing && backup.status == FileStatus::Missing)
        return {};
    return {.status = LyricsVariantsLoadStatus::Invalid};
}
LyricsVariantsSaveStatus QtJsonLyricsVariantsStore::save(const std::filesystem::path& track,
                                                         const LyricsVariants& variants) {
    if (!isValidLyricsVariants(variants))
        return LyricsVariantsSaveStatus::Failed;
    return saveDocument(track, &variants, std::nullopt);
}
LyricsVariantsSaveStatus QtJsonLyricsVariantsStore::saveSelection(const std::filesystem::path& track,
                                                                  const LyricsSelection selection) {
    switch (selection.kind) {
    case LyricsSelectionKind::Automatic:
    case LyricsSelectionKind::Source:
    case LyricsSelectionKind::Hidden:
        if (selection.variant_id != 0)
            return LyricsVariantsSaveStatus::Failed;
        break;
    case LyricsSelectionKind::Variant:
        if (selection.variant_id == 0)
            return LyricsVariantsSaveStatus::Failed;
        break;
    default:
        return LyricsVariantsSaveStatus::Failed;
    }
    return saveDocument(track, nullptr, selection);
}
LyricsVariantsSaveStatus
QtJsonLyricsVariantsStore::saveDocument(const std::filesystem::path& track, const LyricsVariants* updated_variants,
                                        const std::optional<LyricsSelection> updated_selection) {
    const auto identity = trackText(track);
    if (!identity)
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
    const auto& current = primary.status == FileStatus::Valid ? primary : backup;
    const auto& variants = updated_variants ? *updated_variants : current.variants;
    const auto selection = availableSelection(updated_selection.value_or(current.selection), variants);
    if (updated_selection && selection != *updated_selection)
        return LyricsVariantsSaveStatus::Failed;
    if (updated_selection && primary.status == FileStatus::Valid && selection == primary.selection)
        return LyricsVariantsSaveStatus::Saved;
    const auto bytes = encode(*identity, variants, selection);
    if (bytes.isEmpty())
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
