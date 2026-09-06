#pragma once

#include <QDir>
#include <QString>
#include <QUrl>

#include <filesystem>
#include <optional>
#include <string>

namespace music_player::m0 {
namespace filesystem_path_detail {

#ifdef Q_OS_WIN
[[nodiscard]] inline bool isWindowsSeparator(const QChar character) {
    return character == QLatin1Char('/') || character == QLatin1Char('\\');
}

[[nodiscard]] inline bool isAsciiDriveLetter(const QChar character) {
    const ushort value = character.unicode();
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

[[nodiscard]] inline bool isDriveQualifiedPath(const QString& path, const qsizetype offset = 0) {
    return path.size() >= offset + 3 && isAsciiDriveLetter(path.at(offset)) &&
           path.at(offset + 1) == QLatin1Char(':') && isWindowsSeparator(path.at(offset + 2));
}

[[nodiscard]] inline bool startsWithTwoWindowsSeparators(const QString& path) {
    return path.size() >= 2 && isWindowsSeparator(path.at(0)) && isWindowsSeparator(path.at(1));
}

[[nodiscard]] inline qsizetype nextWindowsSeparator(const QString& path, const qsizetype offset) {
    for (qsizetype index = offset; index < path.size(); ++index) {
        if (isWindowsSeparator(path.at(index)))
            return index;
    }
    return -1;
}

[[nodiscard]] inline bool hasUncServerAndShare(const QString& path, const qsizetype offset) {
    if (offset >= path.size() || isWindowsSeparator(path.at(offset)))
        return false;
    const qsizetype server_end = nextWindowsSeparator(path, offset);
    if (server_end < 0 || server_end + 1 >= path.size() || isWindowsSeparator(path.at(server_end + 1)))
        return false;
    const qsizetype share_end = nextWindowsSeparator(path, server_end + 1);
    return share_end < 0 || share_end > server_end + 1;
}
#endif

} // namespace filesystem_path_detail

[[nodiscard]] inline bool isAbsoluteFilesystemSourcePath(const QString& path) {
    using namespace filesystem_path_detail;
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

// URL spelling uses forward slashes even for Windows extended paths.
// Normalize separators only; never resolve, stat or canonicalize an input here.
[[nodiscard]] inline std::optional<std::filesystem::path> localFilesystemPathFromUrl(const QUrl& url) {
    if (!url.isValid() || !url.isLocalFile())
        return std::nullopt;
    const QString path = QDir::toNativeSeparators(url.toLocalFile());
    if (!isAbsoluteFilesystemSourcePath(path))
        return std::nullopt;
#ifdef Q_OS_WIN
    return std::filesystem::path{path.toStdWString()};
#else
    return std::filesystem::path{path.toUtf8().toStdString()};
#endif
}

} // namespace music_player::m0
