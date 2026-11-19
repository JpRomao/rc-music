#include "TestProfileRoot.hpp"

#include "OpenSourcesCommand.hpp"

#include <QDir>
#include <QFileInfo>

#include <stdexcept>
#include <string>

namespace music_player::m0::experiment {
namespace {

const QString option = QStringLiteral("--profile-root");

[[nodiscard]] std::string describe(const QString& path) { return path.toUtf8().toStdString(); }

void requireFullyQualified(const QString& path) {
    if (path.contains(QChar::Null) || !music_player::m0::isFullyQualifiedFilesystemPath(path))
        throw std::invalid_argument{
            "--profile-root must be an absolute directory path such as C:\\rc-music\\guest, got '" + describe(path) +
            "'"};
}

} // namespace

TestProfileRootArguments takeTestProfileRoot(const QStringList& arguments) {
    TestProfileRootArguments result;
    bool source_only = false;
    for (qsizetype index = 0; index < arguments.size(); ++index) {
        const QString& argument = arguments.at(index);
        if (index == 0 || source_only) {
            result.remaining_arguments.push_back(argument);
            continue;
        }
        if (argument == QStringLiteral("--")) {
            source_only = true;
            result.remaining_arguments.push_back(argument);
            continue;
        }
        if (argument.startsWith(option + QLatin1Char('=')))
            throw std::invalid_argument{"write the profile directory as a separate argument: --profile-root <dir>"};
        if (argument != option) {
            result.remaining_arguments.push_back(argument);
            continue;
        }
        if (result.profile_root.has_value())
            throw std::invalid_argument{"--profile-root may be given only once"};
        if (index + 1 >= arguments.size() || arguments.at(index + 1).isEmpty())
            throw std::invalid_argument{"--profile-root requires a directory: --profile-root <dir>"};
        const QString& value = arguments.at(++index);
        requireFullyQualified(value);
        result.profile_root = QDir::cleanPath(value);
    }
    return result;
}

QString prepareTestProfileRoot(const QString& profile_root) {
    requireFullyQualified(profile_root);
    const QString root = QDir::cleanPath(profile_root);
    const QFileInfo info{root};
    if (info.exists() && !info.isDir())
        throw std::invalid_argument{"--profile-root '" + describe(root) + "' exists but is not a directory"};
    if (!info.exists() && !QDir{}.mkpath(root))
        throw std::invalid_argument{"--profile-root '" + describe(root) + "' cannot be created"};
    if (!QFileInfo{root}.isDir())
        throw std::invalid_argument{"--profile-root '" + describe(root) + "' cannot be created"};
    return root;
}

} // namespace music_player::m0::experiment
