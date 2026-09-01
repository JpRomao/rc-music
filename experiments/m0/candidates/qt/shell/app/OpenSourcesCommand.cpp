#include "OpenSourcesCommand.hpp"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace music_player::m0 {
namespace {

[[nodiscard]] bool validAbsolutePath(const QString& path) {
    return !path.isEmpty() && !path.contains(QChar::Null) && QDir::isAbsolutePath(path);
}

} // namespace

OpenSourcesEncodingResult encodeOpenSourcesCommand(const OpenSourcesCommand& command) {
    if (command.paths.size() > open_sources_max_path_count)
        return {.error = OpenSourcesCommandError::TooManyPaths};

    QJsonArray paths;
    for (const QString& path : command.paths) {
        if (!validAbsolutePath(path))
            return {.error = OpenSourcesCommandError::InvalidPath};
        paths.push_back(path);
    }

    const QByteArray payload = QJsonDocument{QJsonObject{{QStringLiteral("version"), 1},
                                                         {QStringLiteral("command"), QStringLiteral("openSources")},
                                                         {QStringLiteral("paths"), std::move(paths)}}}
                                   .toJson(QJsonDocument::Compact);
    if (payload.size() > open_sources_max_payload_bytes)
        return {.error = OpenSourcesCommandError::PayloadTooLarge};
    return {.payload = payload};
}

OpenSourcesDecodingResult decodeOpenSourcesCommand(const QByteArray& payload) {
    if (payload.size() > open_sources_max_payload_bytes)
        return {.error = OpenSourcesCommandError::PayloadTooLarge};
    if (payload.isEmpty())
        return {.error = OpenSourcesCommandError::MalformedPayload};

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || document.isNull())
        return {.error = OpenSourcesCommandError::MalformedPayload};
    if (!document.isObject())
        return {.error = OpenSourcesCommandError::InvalidSchema};

    const QJsonObject object = document.object();
    if (object.size() != 3 || !object.contains(QStringLiteral("version")) ||
        !object.contains(QStringLiteral("command")) || !object.contains(QStringLiteral("paths"))) {
        return {.error = OpenSourcesCommandError::InvalidSchema};
    }

    const QJsonValue version = object.value(QStringLiteral("version"));
    if (!version.isDouble())
        return {.error = OpenSourcesCommandError::InvalidSchema};
    if (version.toDouble() != 1.0)
        return {.error = OpenSourcesCommandError::UnsupportedVersion};

    const QJsonValue command_name = object.value(QStringLiteral("command"));
    if (!command_name.isString())
        return {.error = OpenSourcesCommandError::InvalidSchema};
    if (command_name.toString() != QStringLiteral("openSources"))
        return {.error = OpenSourcesCommandError::UnsupportedCommand};

    const QJsonValue paths_value = object.value(QStringLiteral("paths"));
    if (!paths_value.isArray())
        return {.error = OpenSourcesCommandError::InvalidSchema};
    const QJsonArray paths = paths_value.toArray();
    if (paths.size() > open_sources_max_path_count)
        return {.error = OpenSourcesCommandError::TooManyPaths};

    OpenSourcesCommand decoded;
    decoded.paths.reserve(paths.size());
    for (const QJsonValue& value : paths) {
        if (!value.isString())
            return {.error = OpenSourcesCommandError::InvalidSchema};
        const QString path = value.toString();
        if (!validAbsolutePath(path))
            return {.error = OpenSourcesCommandError::InvalidPath};
        decoded.paths.push_back(path);
    }
    return {.command = std::move(decoded)};
}

} // namespace music_player::m0
