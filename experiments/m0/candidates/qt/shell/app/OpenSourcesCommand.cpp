#include "OpenSourcesCommand.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace music_player::m0 {
namespace {

#ifdef Q_OS_WIN
[[nodiscard]] bool isWindowsSeparator(const QChar character) {
    return character == QLatin1Char('/') || character == QLatin1Char('\\');
}

[[nodiscard]] bool isAsciiDriveLetter(const QChar character) {
    const ushort value = character.unicode();
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

[[nodiscard]] bool isDriveQualifiedPath(const QString& path, const qsizetype offset = 0) {
    return path.size() >= offset + 3 && isAsciiDriveLetter(path.at(offset)) &&
           path.at(offset + 1) == QLatin1Char(':') && isWindowsSeparator(path.at(offset + 2));
}

[[nodiscard]] bool startsWithTwoWindowsSeparators(const QString& path) {
    return path.size() >= 2 && isWindowsSeparator(path.at(0)) && isWindowsSeparator(path.at(1));
}

[[nodiscard]] qsizetype nextWindowsSeparator(const QString& path, const qsizetype offset) {
    for (qsizetype index = offset; index < path.size(); ++index) {
        if (isWindowsSeparator(path.at(index)))
            return index;
    }
    return -1;
}

[[nodiscard]] bool hasUncServerAndShare(const QString& path, const qsizetype offset) {
    if (offset >= path.size() || isWindowsSeparator(path.at(offset)))
        return false;
    const qsizetype server_end = nextWindowsSeparator(path, offset);
    if (server_end < 0 || server_end + 1 >= path.size() || isWindowsSeparator(path.at(server_end + 1)))
        return false;
    const qsizetype share_end = nextWindowsSeparator(path, server_end + 1);
    return share_end < 0 || share_end > server_end + 1;
}
#endif

} // namespace

bool isFullyQualifiedFilesystemPath(const QString& path) {
    if (path.isEmpty() || path.contains(QChar::Null) || path.startsWith(QStringLiteral(":/")))
        return false;

#ifdef Q_OS_WIN
    if (isDriveQualifiedPath(path))
        return true;

    if (!startsWithTwoWindowsSeparators(path))
        return false;

    const bool namespace_marker =
        path.size() >= 3 && (path.at(2) == QLatin1Char('?') || path.at(2) == QLatin1Char('.'));
    if (!namespace_marker)
        return hasUncServerAndShare(path, 2);

    const bool canonical_extended_prefix = path.size() >= 4 && path.at(0) == QLatin1Char('\\') &&
                                           path.at(1) == QLatin1Char('\\') && path.at(2) == QLatin1Char('?') &&
                                           path.at(3) == QLatin1Char('\\') && !path.contains(QLatin1Char('/'));
    if (canonical_extended_prefix) {
        if (isDriveQualifiedPath(path, 4))
            return true;
        return path.size() >= 8 && path.sliced(4, 3).compare(QStringLiteral("UNC"), Qt::CaseInsensitive) == 0 &&
               isWindowsSeparator(path.at(7)) && hasUncServerAndShare(path, 8);
    }
    return false;
#else
    return path.startsWith(QLatin1Char('/'));
#endif
}

OpenSourcesEncodingResult encodeOpenSourcesCommand(const OpenSourcesCommand& command) {
    if (command.paths.size() > open_sources_max_path_count)
        return {.error = OpenSourcesCommandError::TooManyPaths};

    QJsonArray paths;
    for (const QString& path : command.paths) {
        if (!isFullyQualifiedFilesystemPath(path))
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
        if (!isFullyQualifiedFilesystemPath(path))
            return {.error = OpenSourcesCommandError::InvalidPath};
        decoded.paths.push_back(path);
    }
    return {.command = std::move(decoded)};
}

} // namespace music_player::m0
