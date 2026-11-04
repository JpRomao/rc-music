#include "adapters/DownloadMediaProfileJson.hpp"
#include "adapters/DownloadCheckpointJson.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>

namespace music_player::m0 {
namespace {
constexpr std::array<std::string_view, 6> stages{"planned",         "created", "in-use",
                                                 "cleanup-pending", "removed", "abandoned"};

bool lowerHex(const std::string_view value, const std::size_t length) noexcept {
    return value.size() == length &&
           std::ranges::all_of(value, [](const char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool uuid(const std::string_view value) noexcept {
    if (value.size() != 36 || value[14] != '4' ||
        (value[19] != '8' && value[19] != '9' && value[19] != 'a' && value[19] != 'b'))
        return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (value[i] != '-')
                return false;
        } else if (!lowerHex(value.substr(i, 1), 1)) {
            return false;
        }
    }
    return true;
}

bool validSid(const DownloadMediaProfileSid& value) noexcept {
    // Canonical SID_PROFILE: revision/count, big-endian authority 15, then
    // little-endian base RID 2. The remaining seven RIDs are opaque evidence.
    if (value[0] != 1 || value[1] != 8 || value[7] != 15 || value[8] != 2 || value[9] != 0 || value[10] != 0 ||
        value[11] != 0)
        return false;
    for (std::size_t i = 2; i < 7; ++i)
        if (value[i] != 0)
            return false;
    return true;
}

bool terminal(const DownloadMediaProfileStage stage) noexcept {
    return stage == DownloadMediaProfileStage::Removed || stage == DownloadMediaProfileStage::Abandoned;
}

QString text(const std::string_view value) {
    return QString::fromLatin1(value.data(), static_cast<qsizetype>(value.size()));
}

bool keys(const QJsonObject& object, const std::initializer_list<const char*> required) {
    return object.size() == static_cast<qsizetype>(required.size()) &&
           std::ranges::all_of(required, [&](const char* key) { return object.contains(QString::fromLatin1(key)); });
}
} // namespace

bool validDownloadMediaProfileRecord(const DownloadMediaProfileRecord& value) noexcept {
    if (value.job_id.empty() || value.job_id.size() > 64 ||
        !std::ranges::all_of(value.job_id,
                             [](const char c) {
                                 return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                                        c == '_' || c == '-';
                             }) ||
        !uuid(value.operation_id) || static_cast<std::size_t>(value.stage) >= stages.size())
        return false;
    if (value.stage == DownloadMediaProfileStage::Planned || value.stage == DownloadMediaProfileStage::Abandoned)
        return !value.sid;
    return value.sid && validSid(*value.sid);
}

bool mayReplaceDownloadMediaProfileRecord(const DownloadMediaProfileRecord& previous,
                                          const DownloadMediaProfileRecord& next) noexcept {
    if (!validDownloadMediaProfileRecord(previous) || !validDownloadMediaProfileRecord(next))
        return false;
    if (previous.operation_id != next.operation_id)
        return terminal(previous.stage) && next.stage == DownloadMediaProfileStage::Planned;
    if (previous.job_id != next.job_id || (previous.sid && previous.sid != next.sid))
        return false;
    if (previous == next)
        return true;
    switch (previous.stage) {
    case DownloadMediaProfileStage::Planned:
        return next.stage == DownloadMediaProfileStage::Created || next.stage == DownloadMediaProfileStage::Abandoned;
    case DownloadMediaProfileStage::Created:
        return next.stage == DownloadMediaProfileStage::InUse ||
               next.stage == DownloadMediaProfileStage::CleanupPending;
    case DownloadMediaProfileStage::InUse:
        return next.stage == DownloadMediaProfileStage::CleanupPending;
    case DownloadMediaProfileStage::CleanupPending:
        return next.stage == DownloadMediaProfileStage::Removed;
    case DownloadMediaProfileStage::Removed:
    case DownloadMediaProfileStage::Abandoned:
        return false;
    }
    return false;
}

std::string downloadMediaProfileName(const std::string_view operation_id) {
    if (!uuid(operation_id))
        return {};
    return std::string{"RCMusic.Media."}.append(operation_id);
}

QByteArray DownloadMediaProfileJson::encode(const DownloadMediaProfileRecord& value) {
    if (!validDownloadMediaProfileRecord(value))
        return {};
    QJsonValue sid{QJsonValue::Null};
    if (value.sid) {
        const QByteArray bytes{reinterpret_cast<const char*>(value.sid->data()),
                               static_cast<qsizetype>(value.sid->size())};
        sid = QString::fromLatin1(bytes.toHex());
    }
    const auto encoded =
        QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                                  {QStringLiteral("owner"), QStringLiteral("rc-music-download-media-profile")},
                                  {QStringLiteral("jobId"), text(value.job_id)},
                                  {QStringLiteral("operationId"), text(value.operation_id)},
                                  {QStringLiteral("stage"), text(stages[static_cast<std::size_t>(value.stage)])},
                                  {QStringLiteral("sid"), sid}})
            .toJson(QJsonDocument::Compact);
    return encoded.size() <= maximum_file_size ? encoded : QByteArray{};
}

DownloadMediaProfileJsonResult DownloadMediaProfileJson::decode(const QByteArrayView bytes) {
    if (bytes.isEmpty() || bytes.size() > maximum_file_size || !strictDownloadJsonPreflight(bytes))
        return {};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(QByteArray{bytes.data(), bytes.size()}, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return {};
    const auto object = document.object();
    const auto version = object.value(QStringLiteral("schemaVersion"));
    if (!version.isDouble() || !std::isfinite(version.toDouble()) || version.toDouble() < 1 ||
        std::trunc(version.toDouble()) != version.toDouble() ||
        object.value(QStringLiteral("owner")) != QJsonValue{QStringLiteral("rc-music-download-media-profile")})
        return {};
    if (version.toDouble() > 1)
        return {.status = DownloadCheckpointLoadStatus::FutureVersion};
    if (!keys(object, {"schemaVersion", "owner", "jobId", "operationId", "stage", "sid"}))
        return {};
    for (const auto* field : {"jobId", "operationId", "stage"})
        if (!object.value(QString::fromLatin1(field)).isString())
            return {};
    const auto stage = object.value(QStringLiteral("stage")).toString();
    auto index = stages.size();
    for (std::size_t i = 0; i < stages.size(); ++i)
        if (stage == text(stages[i]))
            index = i;
    if (index == stages.size())
        return {};
    DownloadMediaProfileRecord record{.job_id = object.value(QStringLiteral("jobId")).toString().toStdString(),
                                      .operation_id =
                                          object.value(QStringLiteral("operationId")).toString().toStdString(),
                                      .stage = static_cast<DownloadMediaProfileStage>(index)};
    const auto sid_value = object.value(QStringLiteral("sid"));
    if (!sid_value.isNull()) {
        if (!sid_value.isString())
            return {};
        const auto hex = sid_value.toString().toStdString();
        if (!lowerHex(hex, 80))
            return {};
        const auto decoded = QByteArray::fromHex(QByteArray::fromStdString(hex));
        DownloadMediaProfileSid sid{};
        for (std::size_t i = 0; i < sid.size(); ++i)
            sid[i] = static_cast<std::uint8_t>(decoded[static_cast<qsizetype>(i)]);
        record.sid = sid;
    }
    if (!validDownloadMediaProfileRecord(record))
        return {};
    return {.status = DownloadCheckpointLoadStatus::Loaded, .record = std::move(record)};
}
} // namespace music_player::m0
