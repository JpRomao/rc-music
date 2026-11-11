#include "adapters/SessionJsonCodec.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QString>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>

namespace music_player::m0 {
namespace {

constexpr int import_receipt_schema_version = 2;

[[nodiscard]] bool containsNull(const QString& value) { return value.contains(QChar{0}); }

[[nodiscard]] std::optional<std::filesystem::path> decodePath(const QJsonValue& value);

[[nodiscard]] bool scalarString(const QString& value) {
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

// Qt objects have already lost duplicate keys. Only the acknowledgement
// fields need this lexical pass; legacy queue and unknown-field tolerance stay
// unchanged. Syntax and numbers are still parsed by Qt before this pass.
class ImportKeysPreflight final {
  public:
    explicit ImportKeysPreflight(const QByteArrayView bytes) : bytes_(bytes) {}
    bool valid() {
        if (!value(Context::Root, 0))
            return false;
        whitespace();
        return position_ == bytes_.size();
    }

  private:
    enum class Context { Ordinary, Root, Imports, Receipt };
    QByteArrayView bytes_;
    qsizetype position_{0};

    void whitespace() {
        while (position_ < bytes_.size()) {
            const auto character = bytes_[position_];
            if (character != ' ' && character != '\t' && character != '\r' && character != '\n')
                break;
            ++position_;
        }
    }
    bool consume(const char expected) {
        whitespace();
        if (position_ == bytes_.size() || bytes_[position_] != expected)
            return false;
        ++position_;
        return true;
    }
    std::optional<QString> string(const bool decode) {
        whitespace();
        if (position_ == bytes_.size() || bytes_[position_] != '"')
            return std::nullopt;
        const auto begin = position_++;
        while (position_ < bytes_.size()) {
            const auto character = bytes_[position_++];
            if (character == '\\') {
                if (position_ == bytes_.size())
                    return std::nullopt;
                ++position_;
            } else if (character == '"') {
                if (!decode)
                    return QString{};
                QByteArray wrapped{"["};
                wrapped.append(bytes_.data() + begin, position_ - begin);
                wrapped.append(']');
                const auto parsed = QJsonDocument::fromJson(wrapped);
                if (!parsed.isArray() || parsed.array().size() != 1)
                    return std::nullopt;
                return parsed.array().at(0).toString();
            }
        }
        return std::nullopt;
    }
    bool value(const Context context, const unsigned int depth) {
        whitespace();
        if (position_ == bytes_.size())
            return false;
        const auto character = bytes_[position_];
        if ((character == '{' || character == '[') && depth >= 1024)
            return false;
        if (character == '"')
            return string(false).has_value();
        if (character == '{') {
            ++position_;
            QSet<QString> keys;
            if (consume('}'))
                return true;
            do {
                const auto key = string(context == Context::Root || context == Context::Receipt);
                if (!key || !consume(':'))
                    return false;
                const bool acknowledgement_key =
                    context == Context::Receipt ||
                    (context == Context::Root &&
                     (*key == QStringLiteral("downloadImports") || *key == QStringLiteral("schemaVersion") ||
                      *key == QStringLiteral("retiredDownloadImportJobIds")));
                if (acknowledgement_key) {
                    if (keys.contains(*key))
                        return false;
                    keys.insert(*key);
                }
                const auto child = context == Context::Root && *key == QStringLiteral("downloadImports")
                                       ? Context::Imports
                                       : Context::Ordinary;
                if (!value(child, depth + 1))
                    return false;
            } while (consume(','));
            return consume('}');
        }
        if (character == '[') {
            ++position_;
            if (consume(']'))
                return true;
            do {
                if (!value(context == Context::Imports ? Context::Receipt : Context::Ordinary, depth + 1))
                    return false;
            } while (consume(','));
            return consume(']');
        }
        const auto begin = position_;
        while (position_ < bytes_.size()) {
            const auto next = bytes_[position_];
            if (next == ',' || next == '}' || next == ']' || next == ' ' || next == '\t' || next == '\r' ||
                next == '\n')
                break;
            ++position_;
        }
        return position_ != begin;
    }
};

[[nodiscard]] std::optional<std::size_t> receiptNativeBytes(const QString& text) {
    if (text.isEmpty() || containsNull(text) || !scalarString(text))
        return std::nullopt;
#ifdef _WIN32
    if (static_cast<std::size_t>(text.size()) > maximum_download_import_receipt_bytes / sizeof(wchar_t))
        return std::nullopt;
    return static_cast<std::size_t>(text.size()) * sizeof(wchar_t);
#else
    std::size_t bytes = 0;
    for (qsizetype index = 0; index < text.size(); ++index) {
        const auto unit = text.at(index).unicode();
        if (unit >= 0xd800 && unit <= 0xdbff) {
            bytes += 4;
            ++index;
        } else {
            bytes += unit <= 0x7f ? 1 : unit <= 0x7ff ? 2 : 3;
        }
        if (bytes > maximum_download_import_receipt_bytes)
            return std::nullopt;
    }
    return bytes;
#endif
}

[[nodiscard]] bool validJobId(const QString& id) {
    if (id.isEmpty() || id.size() > 64)
        return false;
    for (const auto unit : id) {
        const auto code = unit.unicode();
        if (!((code >= u'a' && code <= u'z') || (code >= u'A' && code <= u'Z') || (code >= u'0' && code <= u'9') ||
              code == u'_' || code == u'-'))
            return false;
    }
    return true;
}

[[nodiscard]] bool decodeReceipts(const QJsonValue& value, SessionSnapshot& snapshot) {
    if (!value.isArray())
        return false;
    const auto receipts = value.toArray();
    if (receipts.size() > static_cast<qsizetype>(maximum_download_import_receipts))
        return false;
    std::size_t remaining_bytes = maximum_download_import_receipt_bytes;
    QSet<QString> ids;
    // Check closed fields, count and aggregate native bytes before allocating
    // any receipt/path vector; Qt's document itself has the independent 16 MiB cap.
    for (const auto& item : receipts) {
        if (!item.isObject())
            return false;
        const auto object = item.toObject();
        if (object.size() != 2 || !object.value(QStringLiteral("jobId")).isString() ||
            !object.value(QStringLiteral("sourcePath")).isString())
            return false;
        const auto id = object.value(QStringLiteral("jobId")).toString();
        if (!validJobId(id) || ids.contains(id))
            return false;
        const auto id_bytes = static_cast<std::size_t>(id.size());
        if (id_bytes > remaining_bytes)
            return false;
        remaining_bytes -= id_bytes;
        const auto native_bytes = receiptNativeBytes(object.value(QStringLiteral("sourcePath")).toString());
        if (!native_bytes || *native_bytes > remaining_bytes)
            return false;
        remaining_bytes -= *native_bytes;
        ids.insert(id);
    }
    snapshot.download_import_receipts.reserve(static_cast<std::size_t>(receipts.size()));
    for (const auto& item : receipts) {
        const auto object = item.toObject();
        const auto path = decodePath(object.value(QStringLiteral("sourcePath")));
        if (!path)
            return false;
        snapshot.download_import_receipts.push_back(
            {object.value(QStringLiteral("jobId")).toString().toStdString(), *path});
    }
    return isValidDownloadImportReceipts(snapshot.download_import_receipts);
}

[[nodiscard]] bool decodeRetiredJobIds(const QJsonValue& value, SessionSnapshot& snapshot) {
    if (!value.isArray())
        return false;
    const auto ids = value.toArray();
    if (ids.isEmpty() || ids.size() > static_cast<qsizetype>(maximum_retired_download_import_job_ids))
        return false;
    auto& retired = snapshot.retired_download_import_job_ids;
    retired.reserve(static_cast<std::size_t>(ids.size()));
    for (const auto& item : ids) {
        const auto id = item.toString();
        if (!validJobId(id))
            return false;
        retired.push_back(id.toStdString());
    }
    return true;
}

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

    QJsonArray imports;
    for (const auto& receipt : snapshot.download_import_receipts) {
        const auto source_path = encodePath(receipt.source_path);
        if (!source_path || !scalarString(*source_path))
            return {};
        imports.append(QJsonObject{
            {QStringLiteral("jobId"), QString::fromStdString(receipt.job_id)},
            {QStringLiteral("sourcePath"), *source_path},
        });
    }

    const bool has_retired_ids = !snapshot.retired_download_import_job_ids.empty();
    QJsonObject root{
        {QStringLiteral("schemaVersion"), has_retired_ids ? schema_version : import_receipt_schema_version},
        {QStringLiteral("queue"), queue},
        {QStringLiteral("positionMs"), QString::number(static_cast<qlonglong>(snapshot.position_ms))},
        {QStringLiteral("downloadImports"), imports},
    };
    if (has_retired_ids) {
        QJsonArray retired;
        for (const auto& id : snapshot.retired_download_import_job_ids)
            retired.append(QString::fromStdString(id));
        root.insert(QStringLiteral("retiredDownloadImportJobIds"), retired);
    }
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
    if (*version != 1 && *version != import_receipt_schema_version && *version != schema_version)
        return {};

    const auto retired_key = QStringLiteral("retiredDownloadImportJobIds");
    if ((*version != schema_version && root.contains(retired_key)) ||
        (*version >= import_receipt_schema_version && !ImportKeysPreflight{bytes}.valid()))
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
    if (*version >= import_receipt_schema_version &&
        !decodeReceipts(root.value(QStringLiteral("downloadImports")), snapshot))
        return {};
    if (*version == schema_version && !decodeRetiredJobIds(root.value(retired_key), snapshot))
        return {};
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
