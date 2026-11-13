#include "adapters/DownloadOwnershipJson.hpp"
#include "adapters/DownloadCheckpointJson.hpp"
#include "m0/domain/DownloadDirectory.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

namespace music_player::m0 {
namespace {
constexpr std::array<std::string_view, 4> stages{"planned", "workspace-ready", "publication-prepared", "published"};
bool hex(const std::string_view value, const std::size_t length) noexcept {
    return value.size() == length &&
           std::ranges::all_of(value, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
bool uuid(const std::string_view value) noexcept {
    if (value.size() != 36 || value[14] != '4' ||
        (value[19] != '8' && value[19] != '9' && value[19] != 'a' && value[19] != 'b'))
        return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (value[i] != '-')
                return false;
        } else if (!hex(value.substr(i, 1), 1))
            return false;
    }
    return true;
}
bool identityValid(const DownloadFileIdentity& value) noexcept {
    return std::ranges::any_of(value.file_id, [](std::uint8_t byte) { return byte != 0; });
}
bool keys(const QJsonObject& object, const std::initializer_list<const char*> required) {
    if (object.size() != static_cast<qsizetype>(required.size()))
        return false;
    return std::ranges::all_of(required, [&](const char* key) { return object.contains(QString::fromLatin1(key)); });
}
QString text(const std::string_view value) {
    return QString::fromLatin1(value.data(), static_cast<qsizetype>(value.size()));
}
QString number(std::uint64_t value) { return QString::number(static_cast<qulonglong>(value)); }
std::optional<std::uint64_t> number(const QJsonValue& value) {
    if (!value.isString())
        return {};
    const auto encoded = value.toString();
    if (encoded.isEmpty() || encoded.size() > 20 || (encoded.size() > 1 && encoded[0] == QLatin1Char('0')))
        return {};
    std::uint64_t result = 0;
    for (const auto c : encoded) {
        const auto unit = c.unicode();
        if (unit < u'0' || unit > u'9')
            return {};
        const auto digit = static_cast<std::uint64_t>(unit - u'0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
            return {};
        result = result * 10 + digit;
    }
    return result;
}
QJsonObject identity(const DownloadFileIdentity& value) {
    const QByteArray bytes(reinterpret_cast<const char*>(value.file_id.data()), 16);
    return {{QStringLiteral("volumeSerial"), number(value.volume_serial)},
            {QStringLiteral("fileId"), QString::fromLatin1(bytes.toHex())}};
}
QJsonValue identity(const std::optional<DownloadFileIdentity>& value) {
    return value ? QJsonValue(identity(*value)) : QJsonValue(QJsonValue::Null);
}
std::optional<DownloadFileIdentity> identity(const QJsonValue& value) {
    if (!value.isObject())
        return {};
    const auto object = value.toObject();
    if (!keys(object, {"volumeSerial", "fileId"}) || !object.value(QStringLiteral("fileId")).isString())
        return {};
    const auto volume = number(object.value(QStringLiteral("volumeSerial")));
    const auto encoded = object.value(QStringLiteral("fileId")).toString().toStdString();
    if (!volume || !hex(encoded, 32))
        return {};
    const auto decoded = QByteArray::fromHex(QByteArray::fromStdString(encoded));
    DownloadFileIdentity result{.volume_serial = *volume};
    for (std::size_t i = 0; i < result.file_id.size(); ++i)
        result.file_id[i] = static_cast<std::uint8_t>(decoded[static_cast<qsizetype>(i)]);
    return identityValid(result) ? std::optional{result} : std::nullopt;
}
std::optional<DownloadOwnershipRecord> record(const QJsonValue& value, const bool custom_directories) {
    if (!value.isObject())
        return {};
    auto object = value.toObject();
    const auto directory = object.take(QStringLiteral("publicationDirectory"));
    if (!directory.isUndefined() &&
        (!custom_directories || !directory.isString() || !validDownloadDirectory(directory.toString().toStdString()) ||
         QString::fromStdString(directory.toString().toStdString()) != directory.toString()))
        return {};
    if (!keys(object,
              {"jobId", "workspaceId", "stage", "stagingParent", "publishParent", "jobDirectory", "input", "output",
               "maximumWorkspaceBytes", "inputBytes", "outputBytes", "outputSha256", "cleanupComplete"}))
        return {};
    for (const auto* field : {"jobId", "workspaceId", "stage", "outputSha256"})
        if (!object.value(QString::fromLatin1(field)).isString())
            return {};
    if (!object.value(QStringLiteral("cleanupComplete")).isBool())
        return {};
    const auto staging = identity(object.value(QStringLiteral("stagingParent")));
    const auto publication = identity(object.value(QStringLiteral("publishParent")));
    const auto maximum = number(object.value(QStringLiteral("maximumWorkspaceBytes")));
    const auto input_bytes = number(object.value(QStringLiteral("inputBytes")));
    const auto output_bytes = number(object.value(QStringLiteral("outputBytes")));
    if (!staging || !publication || !maximum || !input_bytes || !output_bytes)
        return {};
    const auto stage = object.value(QStringLiteral("stage")).toString();
    auto index = stages.size();
    for (std::size_t i = 0; i < stages.size(); ++i)
        if (stage == text(stages[i]))
            index = i;
    if (index == stages.size())
        return {};
    DownloadOwnershipRecord result{.job_id = object.value(QStringLiteral("jobId")).toString().toStdString(),
                                   .workspace_id = object.value(QStringLiteral("workspaceId")).toString().toStdString(),
                                   .stage = static_cast<DownloadOwnershipStage>(index),
                                   .staging_parent = *staging,
                                   .publish_parent = *publication,
                                   .maximum_workspace_bytes = *maximum,
                                   .input_bytes = *input_bytes,
                                   .output_bytes = *output_bytes,
                                   .output_sha256 =
                                       object.value(QStringLiteral("outputSha256")).toString().toStdString(),
                                   .cleanup_complete = object.value(QStringLiteral("cleanupComplete")).toBool(),
                                   .publication_directory = directory.toString().toStdString()};
    for (auto [name, destination] : {std::pair{"jobDirectory", &result.job_directory},
                                     std::pair{"input", &result.input}, std::pair{"output", &result.output}}) {
        const auto field = object.value(QString::fromLatin1(name));
        if (field.isNull())
            continue;
        const auto decoded = identity(field);
        if (!decoded)
            return {};
        *destination = *decoded;
    }
    return validDownloadOwnershipRecord(result) ? std::optional{result} : std::nullopt;
}
} // namespace
bool validDownloadOwnershipRecord(const DownloadOwnershipRecord& value) noexcept {
    if (value.job_id.empty() || value.job_id.size() > 64 ||
        !std::ranges::all_of(value.job_id,
                             [](char c) {
                                 return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                                        c == '_' || c == '-';
                             }) ||
        !uuid(value.workspace_id) || static_cast<std::size_t>(value.stage) >= stages.size() ||
        !identityValid(value.staging_parent) || !identityValid(value.publish_parent) ||
        value.staging_parent.volume_serial != value.publish_parent.volume_serial ||
        value.maximum_workspace_bytes == 0 || value.maximum_workspace_bytes > 512ULL * 1024 * 1024 ||
        (!value.publication_directory.empty() && !validDownloadDirectory(value.publication_directory)))
        return false;
    if (value.stage == DownloadOwnershipStage::Planned)
        return !value.job_directory && !value.input && !value.output && value.input_bytes == 0 &&
               value.output_bytes == 0 && value.output_sha256.empty();
    if (!value.job_directory || !value.input || !value.output)
        return false;
    for (const auto* id : {&*value.job_directory, &*value.input, &*value.output})
        if (!identityValid(*id) || id->volume_serial != value.staging_parent.volume_serial ||
            *id == value.staging_parent || *id == value.publish_parent)
            return false;
    if (value.job_directory == value.input || value.job_directory == value.output || value.input == value.output)
        return false;
    if (value.stage == DownloadOwnershipStage::WorkspaceReady)
        return value.input_bytes == 0 && value.output_bytes == 0 && value.output_sha256.empty();
    return value.input_bytes > 0 && value.input_bytes <= value.maximum_workspace_bytes && value.output_bytes > 0 &&
           value.output_bytes <= value.maximum_workspace_bytes - value.input_bytes && hex(value.output_sha256, 64);
}
bool validDownloadOwnershipJournal(const DownloadOwnershipJournal& value) noexcept {
    if (value.records.size() > DownloadOwnershipJournal::maximum_records)
        return false;
    for (std::size_t i = 0; i < value.records.size(); ++i) {
        if (!validDownloadOwnershipRecord(value.records[i]))
            return false;
        for (std::size_t j = 0; j < i; ++j)
            if (value.records[i].job_id == value.records[j].job_id ||
                value.records[i].workspace_id == value.records[j].workspace_id)
                return false;
    }
    return true;
}
bool mayReplaceDownloadOwnershipRecord(const DownloadOwnershipRecord& previous,
                                       const DownloadOwnershipRecord& next) noexcept {
    if (!validDownloadOwnershipRecord(previous) || !validDownloadOwnershipRecord(next) ||
        previous.job_id != next.job_id || previous.workspace_id != next.workspace_id ||
        previous.staging_parent != next.staging_parent || previous.publish_parent != next.publish_parent ||
        previous.maximum_workspace_bytes != next.maximum_workspace_bytes ||
        previous.publication_directory != next.publication_directory ||
        (previous.cleanup_complete && !next.cleanup_complete))
        return false;
    const auto before = static_cast<std::size_t>(previous.stage), after = static_cast<std::size_t>(next.stage);
    // A failed Published acknowledgement may leave a proved final after its
    // temporaries were already cleaned. Recovery can acknowledge that same
    // immutable Prepared artifact; other cleaned stages cannot resume work.
    const bool confirm_cleaned_final = previous.stage == DownloadOwnershipStage::PublicationPrepared &&
                                       next.stage == DownloadOwnershipStage::Published;
    if (after < before || after > before + 1 ||
        (previous.cleanup_complete && after != before && !confirm_cleaned_final))
        return false;
    if (before != 0 && (previous.job_directory != next.job_directory || previous.input != next.input ||
                        previous.output != next.output))
        return false;
    if (before == after || previous.stage == DownloadOwnershipStage::PublicationPrepared)
        return previous.job_directory == next.job_directory && previous.input == next.input &&
               previous.output == next.output && previous.input_bytes == next.input_bytes &&
               previous.output_bytes == next.output_bytes && previous.output_sha256 == next.output_sha256;
    return !next.cleanup_complete;
}
QByteArray DownloadOwnershipJson::encode(const DownloadOwnershipJournal& journal) {
    if (!validDownloadOwnershipJournal(journal))
        return {};
    QJsonArray records;
    bool custom_directories = false;
    for (const auto& value : journal.records) {
        QJsonObject record{{QStringLiteral("jobId"), text(value.job_id)},
                           {QStringLiteral("workspaceId"), text(value.workspace_id)},
                           {QStringLiteral("stage"), text(stages[static_cast<std::size_t>(value.stage)])},
                           {QStringLiteral("stagingParent"), identity(value.staging_parent)},
                           {QStringLiteral("publishParent"), identity(value.publish_parent)},
                           {QStringLiteral("jobDirectory"), identity(value.job_directory)},
                           {QStringLiteral("input"), identity(value.input)},
                           {QStringLiteral("output"), identity(value.output)},
                           {QStringLiteral("maximumWorkspaceBytes"), number(value.maximum_workspace_bytes)},
                           {QStringLiteral("inputBytes"), number(value.input_bytes)},
                           {QStringLiteral("outputBytes"), number(value.output_bytes)},
                           {QStringLiteral("outputSha256"), text(value.output_sha256)},
                           {QStringLiteral("cleanupComplete"), value.cleanup_complete}};
        if (!value.publication_directory.empty()) {
            const auto directory = QString::fromStdString(value.publication_directory);
            if (directory.toStdString() != value.publication_directory)
                return {};
            record.insert(QStringLiteral("publicationDirectory"), directory);
            custom_directories = true;
        }
        records.append(record);
    }
    const auto encoded =
        QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), custom_directories ? 2 : 1},
                                  {QStringLiteral("owner"), QStringLiteral("rc-music-download-ownership")},
                                  {QStringLiteral("records"), records}})
            .toJson(QJsonDocument::Compact);
    return encoded.size() <= maximum_file_size ? encoded : QByteArray{};
}
DownloadOwnershipJsonResult DownloadOwnershipJson::decode(const QByteArrayView bytes) {
    if (bytes.isEmpty() || bytes.size() > maximum_file_size || !strictDownloadJsonPreflight(bytes))
        return {};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(QByteArray(bytes.data(), bytes.size()), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return {};
    const auto object = document.object();
    const auto version = object.value(QStringLiteral("schemaVersion"));
    if (!version.isDouble() || !std::isfinite(version.toDouble()) || version.toDouble() < 1 ||
        std::trunc(version.toDouble()) != version.toDouble() ||
        object.value(QStringLiteral("owner")) != QJsonValue(QStringLiteral("rc-music-download-ownership")))
        return {};
    if (version.toDouble() > 2)
        return {.status = DownloadCheckpointLoadStatus::FutureVersion};
    if (!keys(object, {"schemaVersion", "owner", "records"}) || !object.value(QStringLiteral("records")).isArray())
        return {};
    const auto records = object.value(QStringLiteral("records")).toArray();
    if (records.size() > static_cast<qsizetype>(DownloadOwnershipJournal::maximum_records))
        return {};
    DownloadOwnershipJournal result;
    result.records.reserve(static_cast<std::size_t>(records.size()));
    for (const auto& value : records) {
        const auto decoded = record(value, version.toInt() == 2);
        if (!decoded)
            return {};
        result.records.push_back(*decoded);
    }
    if (!validDownloadOwnershipJournal(result))
        return {};
    return {.status = DownloadCheckpointLoadStatus::Loaded, .journal = std::move(result)};
}
} // namespace music_player::m0
