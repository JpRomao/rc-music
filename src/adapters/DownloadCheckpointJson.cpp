#include "adapters/DownloadCheckpointJson.hpp"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QString>

#include <array>
#include <cmath>
#include <limits>
#include <string_view>

namespace music_player::m0 {
namespace {

constexpr std::string_view owner = "rc-music-download-checkpoint";
constexpr std::array<std::string_view, 12> state_names{"queued",        "downloading", "converting", "publishing",
                                                       "cancelling",    "completed",   "cancelled",  "failed",
                                                       "import-failed", "interrupted", "importing",  "imported"};
constexpr std::array<std::string_view, 13> error_names{
    "none",           "unavailable", "network",     "expired", "restricted", "unsupported-media", "no-audio",
    "limit-exceeded", "conversion",  "publication", "import",  "timeout",    "invalid-result"};

QString latin(const std::string_view text) {
    return QString::fromLatin1(text.data(), static_cast<qsizetype>(text.size()));
}

bool scalarString(const QString& text) { return QString::fromUtf8(text.toUtf8()) == text; }

std::optional<QString> utf8Text(const std::string& text) {
    const auto encoded = QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
    if (encoded.toUtf8().toStdString() != text)
        return std::nullopt;
    return encoded;
}

std::optional<QString> pathText(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto encoded = QString::fromStdWString(path.native());
    if (encoded.toStdWString() != path.native() || !scalarString(encoded))
        return std::nullopt;
    return encoded;
#else
    return utf8Text(path.native());
#endif
}

std::optional<std::filesystem::path> nativePath(const QJsonValue& value) {
    if (!value.isString())
        return std::nullopt;
    const auto text = value.toString();
    if (!scalarString(text))
        return std::nullopt;
#ifdef _WIN32
    return std::filesystem::path{text.toStdWString()};
#else
    return std::filesystem::path{text.toUtf8().toStdString()};
#endif
}

bool exactKeys(const QJsonObject& object, const std::initializer_list<QString> required) {
    if (object.size() != static_cast<qsizetype>(required.size()))
        return false;
    for (const auto& key : required) {
        if (!object.contains(key))
            return false;
    }
    return true;
}

QString decimal(const std::uint64_t value) { return QString::number(static_cast<qulonglong>(value)); }

std::optional<std::uint64_t> unsignedDecimal(const QJsonValue& value) {
    if (!value.isString())
        return std::nullopt;
    const auto text = value.toString();
    if (text.isEmpty() || text.size() > 20 || (text.size() > 1 && text.front() == QLatin1Char{'0'}))
        return std::nullopt;
    std::uint64_t result = 0;
    for (const auto character : text) {
        const auto unit = character.unicode();
        if (unit < u'0' || unit > u'9')
            return std::nullopt;
        const auto digit = static_cast<std::uint64_t>(unit - u'0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
            return std::nullopt;
        result = result * 10 + digit;
    }
    return result;
}

template <typename Enum, std::size_t Size>
QString enumText(const Enum value, const std::array<std::string_view, Size>& names) {
    const auto index = static_cast<std::size_t>(value);
    return index < names.size() ? latin(names[index]) : QString{};
}

template <typename Enum, std::size_t Size>
std::optional<Enum> enumValue(const QJsonValue& value, const std::array<std::string_view, Size>& names) {
    if (!value.isString())
        return std::nullopt;
    const auto text = value.toString();
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (text == latin(names[index]))
            return static_cast<Enum>(index);
    }
    return std::nullopt;
}

// Qt's object representation has already lost duplicate keys. This bounded
// lexical preflight checks decoded key equality and scalar Unicode before the
// real Qt JSON parse; syntax/number validity remains Qt's responsibility.
class JsonPreflight final {
  public:
    explicit JsonPreflight(const QByteArrayView bytes) : bytes_(bytes) {}
    bool valid() {
        if (!value(0))
            return false;
        whitespace();
        return position_ == bytes_.size();
    }

  private:
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

    std::optional<QString> string() {
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
                QByteArray wrapped{"["};
                wrapped.append(bytes_.data() + begin, position_ - begin);
                wrapped.append(']');
                QJsonParseError error;
                const auto parsed = QJsonDocument::fromJson(wrapped, &error);
                if (error.error != QJsonParseError::NoError || !parsed.isArray() || parsed.array().size() != 1 ||
                    !parsed.array().at(0).isString())
                    return std::nullopt;
                const auto result = parsed.array().at(0).toString();
                return scalarString(result) ? std::optional<QString>{result} : std::nullopt;
            }
        }
        return std::nullopt;
    }

    bool value(const unsigned int depth) {
        whitespace();
        if (position_ == bytes_.size())
            return false;
        const auto character = bytes_[position_];
        if ((character == '{' || character == '[') && depth >= 64)
            return false;
        if (character == '"')
            return string().has_value();
        if (character == '{') {
            ++position_;
            QSet<QString> keys;
            whitespace();
            if (consume('}'))
                return true;
            do {
                const auto key = string();
                if (!key || keys.contains(*key) || !consume(':'))
                    return false;
                keys.insert(*key);
                if (!value(depth + 1))
                    return false;
            } while (consume(','));
            return consume('}');
        }
        if (character == '[') {
            ++position_;
            whitespace();
            if (consume(']'))
                return true;
            do {
                if (!value(depth + 1))
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

std::optional<DownloadCheckpointReceipt> receipt(const QJsonValue& value) {
    if (!value.isObject())
        return std::nullopt;
    const auto object = value.toObject();
    if (!exactKeys(object, {QStringLiteral("requestId"), QStringLiteral("fingerprint"), QStringLiteral("jobIds"),
                            QStringLiteral("retainedRequestBytes")}) ||
        !object.value(QStringLiteral("requestId")).isString() ||
        !object.value(QStringLiteral("fingerprint")).isString() || !object.value(QStringLiteral("jobIds")).isArray())
        return std::nullopt;
    const auto retained = unsignedDecimal(object.value(QStringLiteral("retainedRequestBytes")));
    if (!retained || *retained > download_checkpoint_max_bytes || *retained > std::numeric_limits<std::size_t>::max())
        return std::nullopt;
    const auto ids = object.value(QStringLiteral("jobIds")).toArray();
    if (ids.size() > 20)
        return std::nullopt;
    DownloadCheckpointReceipt decoded{
        .request_id = object.value(QStringLiteral("requestId")).toString().toUtf8().toStdString(),
        .fingerprint = object.value(QStringLiteral("fingerprint")).toString().toUtf8().toStdString(),
        .retained_request_bytes = static_cast<std::size_t>(*retained),
    };
    decoded.job_ids.reserve(static_cast<std::size_t>(ids.size()));
    for (const auto& id : ids) {
        if (!id.isString())
            return std::nullopt;
        decoded.job_ids.push_back(id.toString().toUtf8().toStdString());
    }
    return decoded;
}

std::optional<DownloadJobSnapshot> job(const QJsonValue& value) {
    if (!value.isObject())
        return std::nullopt;
    const auto object = value.toObject();
    if (!exactKeys(object, {QStringLiteral("jobId"), QStringLiteral("title"), QStringLiteral("state"),
                            QStringLiteral("completedUnits"), QStringLiteral("totalUnits"),
                            QStringLiteral("completedMp3"), QStringLiteral("error")}) ||
        !object.value(QStringLiteral("jobId")).isString() || !object.value(QStringLiteral("title")).isString())
        return std::nullopt;
    const auto state = enumValue<DownloadJobState>(object.value(QStringLiteral("state")), state_names);
    const auto error = enumValue<DownloadFailure>(object.value(QStringLiteral("error")), error_names);
    const auto completed = unsignedDecimal(object.value(QStringLiteral("completedUnits")));
    const auto path = nativePath(object.value(QStringLiteral("completedMp3")));
    if (!state || !error || !completed || !path)
        return std::nullopt;
    std::optional<std::uint64_t> total;
    const auto total_value = object.value(QStringLiteral("totalUnits"));
    if (!total_value.isNull()) {
        total = unsignedDecimal(total_value);
        if (!total)
            return std::nullopt;
    }
    return DownloadJobSnapshot{
        .job_id = object.value(QStringLiteral("jobId")).toString().toUtf8().toStdString(),
        .title = object.value(QStringLiteral("title")).toString().toUtf8().toStdString(),
        .state = *state,
        .completed_units = *completed,
        .total_units = total,
        .completed_mp3 = *path,
        .error = *error,
    };
}

void appendUnsigned(QCryptographicHash& hash, const std::uint64_t value) {
    std::array<char, 8> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index)
        bytes[index] = static_cast<char>((value >> (56 - index * 8)) & 0xff);
    hash.addData(QByteArrayView{bytes.data(), static_cast<qsizetype>(bytes.size())});
}

void appendText(QCryptographicHash& hash, const std::string& text) {
    appendUnsigned(hash, static_cast<std::uint64_t>(text.size()));
    hash.addData(QByteArrayView{text.data(), static_cast<qsizetype>(text.size())});
}

} // namespace

QByteArray DownloadCheckpointJson::encode(const DownloadCheckpoint& checkpoint) {
    if (!validDownloadCheckpoint(checkpoint))
        return {};
    QJsonArray receipts;
    for (const auto& saved : checkpoint.receipts) {
        QJsonArray ids;
        for (const auto& id : saved.job_ids)
            ids.append(latin(id));
        receipts.append(QJsonObject{
            {QStringLiteral("requestId"), latin(saved.request_id)},
            {QStringLiteral("fingerprint"), latin(saved.fingerprint)},
            {QStringLiteral("jobIds"), ids},
            {QStringLiteral("retainedRequestBytes"), decimal(static_cast<std::uint64_t>(saved.retained_request_bytes))},
        });
    }
    QJsonArray jobs;
    for (const auto& saved : checkpoint.jobs) {
        const auto title = utf8Text(saved.title);
        const auto path = pathText(saved.completed_mp3);
        if (!title || !path)
            return {};
        jobs.append(QJsonObject{
            {QStringLiteral("jobId"), latin(saved.job_id)},
            {QStringLiteral("title"), *title},
            {QStringLiteral("state"), enumText(saved.state, state_names)},
            {QStringLiteral("completedUnits"), decimal(saved.completed_units)},
            {QStringLiteral("totalUnits"),
             saved.total_units ? QJsonValue{decimal(*saved.total_units)} : QJsonValue::Null},
            {QStringLiteral("completedMp3"), *path},
            {QStringLiteral("error"), enumText(saved.error, error_names)},
        });
    }
    const auto encoded = QJsonDocument{
        QJsonObject{
            {QStringLiteral("schemaVersion"), schema_version},
            {QStringLiteral("owner"), latin(owner)},
            {QStringLiteral("receipts"), receipts},
            {QStringLiteral("jobs"), jobs},
        }}.toJson(QJsonDocument::Compact);
    return encoded.size() <= max_document_bytes ? encoded : QByteArray{};
}

bool strictDownloadJsonPreflight(const QByteArrayView bytes) {
    return !bytes.isEmpty() && bytes.size() <= DownloadCheckpointJson::max_document_bytes &&
           QString::fromUtf8(bytes.data(), bytes.size()).toUtf8() == QByteArray{bytes.data(), bytes.size()} &&
           JsonPreflight{bytes}.valid();
}

DownloadCheckpointJsonResult DownloadCheckpointJson::decode(const QByteArrayView bytes) {
    if (bytes.isEmpty() || bytes.size() > max_document_bytes)
        return {};
    const QByteArray document_bytes{bytes.data(), bytes.size()};
    if (!strictDownloadJsonPreflight(bytes))
        return {};
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(document_bytes, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        return {};
    const auto object = document.object();
    const auto version_value = object.value(QStringLiteral("schemaVersion"));
    if (!version_value.isDouble())
        return {};
    const auto version = version_value.toDouble();
    if (!std::isfinite(version) || version <= 0 || std::trunc(version) != version)
        return {};
    if (version > schema_version)
        return {.status = DownloadCheckpointJsonStatus::FutureVersion};
    if (version != schema_version ||
        !exactKeys(object, {QStringLiteral("schemaVersion"), QStringLiteral("owner"), QStringLiteral("receipts"),
                            QStringLiteral("jobs")}) ||
        object.value(QStringLiteral("owner")) != QJsonValue{latin(owner)} ||
        !object.value(QStringLiteral("receipts")).isArray() || !object.value(QStringLiteral("jobs")).isArray())
        return {};
    const auto receipts = object.value(QStringLiteral("receipts")).toArray();
    const auto jobs = object.value(QStringLiteral("jobs")).toArray();
    if (receipts.size() > static_cast<qsizetype>(download_checkpoint_max_receipts) ||
        jobs.size() > static_cast<qsizetype>(download_checkpoint_max_jobs))
        return {};
    DownloadCheckpoint checkpoint;
    checkpoint.receipts.reserve(static_cast<std::size_t>(receipts.size()));
    checkpoint.jobs.reserve(static_cast<std::size_t>(jobs.size()));
    for (const auto& saved : receipts) {
        auto decoded = receipt(saved);
        if (!decoded)
            return {};
        checkpoint.receipts.push_back(std::move(*decoded));
    }
    for (const auto& saved : jobs) {
        auto decoded = job(saved);
        if (!decoded)
            return {};
        checkpoint.jobs.push_back(std::move(*decoded));
    }
    if (!validDownloadCheckpoint(checkpoint))
        return {};
    return {.status = DownloadCheckpointJsonStatus::Valid, .checkpoint = std::move(checkpoint)};
}

std::string QtDownloadRequestIdentity::fingerprint(const DownloadRequest& request) {
    if (request.items.empty() || request.items.size() > 20)
        return {};
    for (const auto& item : request.items) {
        if ((item.kind != DownloadMediaKind::Audio && item.kind != DownloadMediaKind::Video) || item.url.empty() ||
            item.url.size() > 8192 || item.title.empty() || item.title.size() > 800 ||
            item.url.find('\0') != std::string::npos || item.title.find('\0') != std::string::npos ||
            !utf8Text(item.url) || !utf8Text(item.title))
            return {};
    }
    QCryptographicHash hash{QCryptographicHash::Sha256};
    constexpr char prefix[] = "RCMusic.DownloadRequest.v1";
    // sizeof includes the required domain separator NUL byte.
    hash.addData(QByteArrayView{prefix, static_cast<qsizetype>(sizeof(prefix))});
    appendUnsigned(hash, static_cast<std::uint64_t>(request.items.size()));
    for (const auto& item : request.items) {
        const char kind = item.kind == DownloadMediaKind::Audio ? 0 : 1;
        hash.addData(QByteArrayView{&kind, 1});
        appendText(hash, item.url);
        appendText(hash, item.title);
    }
    return hash.result().toHex().toStdString();
}

} // namespace music_player::m0
