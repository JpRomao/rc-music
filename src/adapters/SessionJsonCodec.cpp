#include "adapters/SessionJsonCodec.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>

namespace music_player::m0 {
namespace {

[[nodiscard]] bool containsNull(const QString& value) { return value.contains(QChar{0}); }

[[nodiscard]] std::optional<QString> encodePath(const std::filesystem::path& path) {
#ifdef _WIN32
    QString encoded = QString::fromStdWString(path.native());
#else
    const std::u8string native = path.u8string();
    QString encoded =
        QString::fromUtf8(reinterpret_cast<const char*>(native.data()), static_cast<qsizetype>(native.size()));
    const QByteArray round_trip = encoded.toUtf8();
    const QByteArray original{reinterpret_cast<const char*>(native.data()), static_cast<qsizetype>(native.size())};
    if (round_trip != original)
        return std::nullopt;
#endif
    if (encoded.isEmpty() || containsNull(encoded))
        return std::nullopt;
    return encoded;
}

[[nodiscard]] std::optional<std::filesystem::path> decodePath(const QJsonValue& value) {
    if (!value.isString())
        return std::nullopt;
    const QString encoded = value.toString();
    if (encoded.isEmpty() || containsNull(encoded))
        return std::nullopt;
#ifdef _WIN32
    return std::filesystem::path{encoded.toStdWString()};
#else
    const QByteArray utf8 = encoded.toUtf8();
    return std::filesystem::path{std::string{utf8.constData(), static_cast<std::size_t>(utf8.size())}};
#endif
}

[[nodiscard]] QString encodeUnsigned(const std::uint64_t value) {
    return QString::number(static_cast<qulonglong>(value));
}

[[nodiscard]] std::optional<std::uint64_t> decodeUnsigned(const QJsonValue& value, const std::uint64_t maximum,
                                                          const bool allow_zero) {
    if (!value.isString())
        return std::nullopt;
    const QString encoded = value.toString();
    if (encoded.isEmpty() || (encoded.size() > 1 && encoded.front() == QLatin1Char{'0'}))
        return std::nullopt;

    std::uint64_t decoded = 0;
    for (const QChar character : encoded) {
        const char16_t code_unit = character.unicode();
        if (code_unit < u'0' || code_unit > u'9')
            return std::nullopt;
        const std::uint64_t digit = static_cast<std::uint64_t>(code_unit - u'0');
        if (decoded > (maximum - digit) / 10)
            return std::nullopt;
        decoded = decoded * 10 + digit;
    }
    if (!allow_zero && decoded == 0)
        return std::nullopt;
    return decoded;
}

[[nodiscard]] std::optional<double> decodeSchemaVersion(const QJsonObject& root) {
    const QJsonValue value = root.value(QStringLiteral("schemaVersion"));
    if (!value.isDouble())
        return std::nullopt;
    const double version = value.toDouble();
    if (!std::isfinite(version) || std::trunc(version) != version || version <= 0.0)
        return std::nullopt;
    return version;
}

} // namespace

QByteArray SessionJsonCodec::encode(const SessionSnapshot& snapshot) {
    if (!isValidSessionSnapshot(snapshot))
        return {};

    QJsonArray items;
    for (const LocalFileQueueSnapshotItem& item : snapshot.queue.items) {
        const std::optional<QString> source_path = encodePath(item.source_path);
        if (!source_path.has_value())
            return {};
        items.append(QJsonObject{
            {QStringLiteral("occurrenceId"), encodeUnsigned(item.occurrence_id.value)},
            {QStringLiteral("sourcePath"), *source_path},
        });
    }

    QJsonObject queue{
        {QStringLiteral("items"), items},
        {QStringLiteral("currentOccurrenceId"), QJsonValue::Null},
    };
    if (snapshot.queue.current_occurrence_id.has_value()) {
        queue.insert(QStringLiteral("currentOccurrenceId"),
                     encodeUnsigned(snapshot.queue.current_occurrence_id->value));
    }

    const QJsonObject root{
        {QStringLiteral("schemaVersion"), schema_version},
        {QStringLiteral("queue"), queue},
        {QStringLiteral("positionMs"), QString::number(static_cast<qlonglong>(snapshot.position_ms))},
    };
    QByteArray encoded = QJsonDocument{root}.toJson(QJsonDocument::Compact);
    if (encoded.size() > maximum_document_size)
        return {};
    return encoded;
}

SessionJsonDecodeResult SessionJsonCodec::decode(const QByteArrayView bytes) {
    if (bytes.size() > maximum_document_size)
        return {};

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes.toByteArray(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        return {};

    const QJsonObject root = document.object();
    const std::optional<double> version = decodeSchemaVersion(root);
    if (!version.has_value())
        return {};
    if (*version > schema_version)
        return {.status = SessionJsonDecodeStatus::FutureVersion};
    if (*version != schema_version)
        return {};

    const QJsonValue queue_value = root.value(QStringLiteral("queue"));
    const QJsonValue position_value = root.value(QStringLiteral("positionMs"));
    if (!queue_value.isObject())
        return {};
    const QJsonObject queue = queue_value.toObject();
    const QJsonValue items_value = queue.value(QStringLiteral("items"));
    if (!items_value.isArray() || !queue.contains(QStringLiteral("currentOccurrenceId")))
        return {};

    SessionSnapshot snapshot;
    const QJsonArray items = items_value.toArray();
    snapshot.queue.items.reserve(static_cast<std::size_t>(items.size()));
    for (const QJsonValue& item_value : items) {
        if (!item_value.isObject())
            return {};
        const QJsonObject item = item_value.toObject();
        const std::optional<std::uint64_t> occurrence_id = decodeUnsigned(
            item.value(QStringLiteral("occurrenceId")), std::numeric_limits<std::uint64_t>::max(), false);
        const std::optional<std::filesystem::path> source_path = decodePath(item.value(QStringLiteral("sourcePath")));
        if (!occurrence_id.has_value() || !source_path.has_value())
            return {};
        snapshot.queue.items.push_back(
            {.occurrence_id = LocalQueueOccurrenceId{*occurrence_id}, .source_path = *source_path});
    }

    const QJsonValue current_value = queue.value(QStringLiteral("currentOccurrenceId"));
    if (!current_value.isNull()) {
        const std::optional<std::uint64_t> current =
            decodeUnsigned(current_value, std::numeric_limits<std::uint64_t>::max(), false);
        if (!current.has_value())
            return {};
        snapshot.queue.current_occurrence_id = LocalQueueOccurrenceId{*current};
    }

    const std::optional<std::uint64_t> position =
        decodeUnsigned(position_value, static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()), true);
    if (!position.has_value())
        return {};
    snapshot.position_ms = static_cast<std::int64_t>(*position);
    if (!isValidSessionSnapshot(snapshot))
        return {};

    return {.status = SessionJsonDecodeStatus::Valid, .snapshot = std::move(snapshot)};
}

} // namespace music_player::m0
